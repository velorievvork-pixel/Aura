"""Переговоры агентов о времени встречи: ход — JSON от LLM, сервер проверяет занятость и рамки.
Максимум 5 раундов (ход каждого агента), затем эскалация к людям. Итог — только после подтверждения обоих."""
from __future__ import annotations

import json
import logging
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

from pydantic import BaseModel, ConfigDict, Field, ValidationError as PydanticError
from sqlalchemy import or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from .llm import LLM, LLMError
from .models import Memory, Negotiation, User
from .notifier import Note, Notifier
from .repo import busy_times, create_task, display_name, find_user_by_ref, is_onboarded
from .tasks import format_due

log = logging.getLogger(__name__)

MAX_ROUNDS = 5
WINDOW = timedelta(days=14)
TASK_BLOCK = timedelta(minutes=60)     # задача без длительности считается занятым часом
DAY_START, DAY_END = 8, 22             # переговоры только в «человеческие» часы владельца
REMIND_BEFORE = timedelta(minutes=15)
NEGOTIATION_MARK = "[переговоры агентов]"


class NegotiationError(ValueError):
    pass


class MeetingRequest(BaseModel):
    model_config = ConfigDict(extra="ignore", str_strip_whitespace=True)
    with_: str = Field(alias="with", min_length=3, max_length=254)
    topic: str = Field(default="встреча", min_length=1, max_length=200)
    duration_min: int = Field(default=60, ge=15, le=240)


class Move(BaseModel):
    model_config = ConfigDict(extra="ignore", str_strip_whitespace=True)
    action: str
    slot: str | None = None
    reason: str = Field(default="", max_length=300)


async def start(session: AsyncSession, initiator: User, raw: object, now: datetime) -> Negotiation:
    try:
        req = MeetingRequest.model_validate(raw)
    except PydanticError:
        raise NegotiationError("нужен @username или email собеседника") from None
    other = await find_user_by_ref(session, req.with_)
    if other is None:
        raise NegotiationError(f"{req.with_} не пользуется Aura — пригласите его в бота")
    if other.id == initiator.id:
        raise NegotiationError("нельзя договариваться с самим собой")
    if not is_onboarded(other):
        raise NegotiationError(f"{req.with_} ещё не настроил Aura")
    neg = Negotiation(initiator_id=initiator.id, counterpart_id=other.id, topic=req.topic,
                      duration_min=req.duration_min, log=[])
    session.add(neg)
    await session.commit()
    return neg


def _parse_slot(slot: str | None, tz: str) -> datetime | None:
    if not slot:
        return None
    try:
        dt = datetime.fromisoformat(slot.replace("Z", "+00:00"))
    except ValueError:
        return None
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=ZoneInfo(tz))
    return dt.astimezone(timezone.utc)


async def slot_problem(session: AsyncSession, owner: User, slot: datetime, duration: timedelta,
                       now: datetime, window_end: datetime) -> str | None:
    """Почему слот не подходит владельцу (None — подходит). Здесь сервер, а не LLM, решает истину."""
    if slot < now + timedelta(hours=1) or slot > window_end:
        return "вне окна переговоров"
    local = slot.astimezone(ZoneInfo(owner.timezone))
    end = local + duration
    if local.hour < DAY_START or (end.hour, end.minute) > (DAY_END, 0) or end.date() != local.date():
        return "нерабочее время владельца"
    if local.minute % 15:
        return "время должно быть кратно 15 минутам"
    for busy in await busy_times(session, owner, slot - TASK_BLOCK, slot + duration):
        if busy < slot + duration and slot < busy + TASK_BLOCK:
            return "владелец занят"
    return None


async def _context(session: AsyncSession, owner: User, neg: Negotiation, offer: datetime | None,
                   now: datetime, window_end: datetime) -> list[dict]:
    tz = owner.timezone
    fmt = lambda d: d.astimezone(ZoneInfo(tz)).strftime("%Y-%m-%dT%H:%M")  # noqa: E731
    busy = await busy_times(session, owner, now, window_end)
    mems = (await session.scalars(select(Memory.text).where(
        Memory.user_id == owner.id, Memory.kind.in_(("schedule", "preference"))).limit(30))).all()
    history = [f"{e['side']}: {e['action']} {e.get('slot_utc') and fmt(datetime.fromisoformat(e['slot_utc'])) or ''}"
               f" {e.get('reason', '')}".strip() for e in neg.log]
    system = (
        f"{NEGOTIATION_MARK} Ты — ИИ-агент пользователя «{display_name(owner)}». Договариваешься с агентом "
        f"другого человека о встрече «{neg.topic}» на {neg.duration_min} мин. Защищай интересы своего пользователя: "
        f"предлагай только время, когда он свободен, с {DAY_START}:00 до {DAY_END}:00 его пояса ({tz}), "
        "шаг 15 минут. Не раскрывай личные детали — только время. Если предложение собеседника подходит — accept. "
        'Ответ — СТРОГО JSON: {"action": "accept"|"propose"|"reject", "slot": "ГГГГ-ММ-ДДTЧЧ:ММ", "reason": "коротко"}.'
    )
    user = (
        f"Сейчас: {fmt(now)}. Встреча должна начаться до {fmt(window_end)}.\n"
        f"Занят (начало, ~1 ч): {', '.join(fmt(b) for b in busy) or 'свободен'}.\n"
        f"Расписание и предпочтения: {'; '.join(mems) or 'нет данных'}.\n"
        f"История: {' | '.join(history) or 'начало переговоров'}.\n"
        f"Предложение собеседника: {fmt(offer) if offer else 'нет'}."
    )
    return [{"role": "system", "content": system}, {"role": "user", "content": user}]


async def _turn(session, llm: LLM, owner: User, neg: Negotiation, offer: datetime | None,
                now: datetime, window_end: datetime) -> tuple[str, datetime | None, str]:
    """-> (accept|propose|reject|invalid, слот UTC, причина)."""
    try:
        raw = await llm.complete(await _context(session, owner, neg, offer, now, window_end), json_mode=True)
        move = Move.model_validate(json.loads(raw))
    except (LLMError, json.JSONDecodeError, PydanticError, TypeError) as e:
        return "invalid", None, f"ошибка агента: {e.__class__.__name__}"
    duration = timedelta(minutes=neg.duration_min)
    if move.action == "accept":
        if offer is None:
            return "invalid", None, "нечего принимать"
        problem = await slot_problem(session, owner, offer, duration, now, window_end)
        return ("invalid", offer, problem) if problem else ("accept", offer, move.reason)
    if move.action == "propose":
        slot = _parse_slot(move.slot, owner.timezone)
        if slot is None:
            return "invalid", None, "непонятное время"
        problem = await slot_problem(session, owner, slot, duration, now, window_end)
        return ("invalid", slot, problem) if problem else ("propose", slot, move.reason)
    if move.action == "reject":
        return "reject", None, move.reason
    return "invalid", None, "неизвестное действие"


async def run(session: AsyncSession, llm: LLM, notifier: Notifier, neg_id: int, now: datetime | None = None) -> Negotiation:
    neg = await session.get(Negotiation, neg_id)
    if neg is None or neg.status != "negotiating":
        raise NegotiationError("переговоры не найдены или уже завершены")
    now = now or datetime.now(timezone.utc)
    window_end = now + WINDOW
    parties = {"initiator": await session.get(User, neg.initiator_id),
               "counterpart": await session.get(User, neg.counterpart_id)}
    offer: tuple[datetime, str] | None = None
    outcome = None
    for rnd in range(1, MAX_ROUNDS + 1):
        for side in ("initiator", "counterpart"):
            their = offer[0] if offer and offer[1] != side else None
            action, slot, reason = await _turn(session, llm, parties[side], neg, their, now, window_end)
            neg.log = [*neg.log, {"round": rnd, "side": side, "action": action, "reason": reason[:300],
                                  "slot_utc": slot.isoformat() if slot else None}]
            if action == "propose":
                offer = (slot, side)
            elif action in ("accept", "reject"):
                outcome = (action, slot)
                break
        neg.rounds = rnd
        await session.commit()
        if outcome:
            break
    a, b = parties["initiator"], parties["counterpart"]
    if outcome and outcome[0] == "accept":
        neg.status, neg.slot = "agreed", outcome[1]
        await session.commit()
        for me, other in ((a, b), (b, a)):
            await notifier.notify(me.id, Note(
                kind="negotiation.agreed", title="🤝 Агенты договорились",
                body=f"Встреча «{neg.topic}» с {display_name(other)}: {format_due(neg.slot, me.timezone, now)} "
                     f"({neg.duration_min} мин). Подтвердите — без вашего «да» ничего не запланируется.",
                data={"negotiation_id": neg.id},
                buttons=[("✅ Подтвердить", f"neg:ok:{neg.id}"), ("✖ Отклонить", f"neg:no:{neg.id}")]))
    else:
        neg.status = "escalated"
        await session.commit()
        last = f" Последнее предложение: {{}}." if offer else ""
        why = "агент собеседника отказался" if outcome else f"за {MAX_ROUNDS} раундов не нашли общее время"
        for me, other in ((a, b), (b, a)):
            contact = f" Напишите напрямую: @{other.tg_username}." if other.tg_username else ""
            tail = last.format(format_due(offer[0], me.timezone, now)) if offer else ""
            await notifier.notify(me.id, Note(
                kind="negotiation.escalated", title="🙋 Нужна помощь человека",
                body=f"Встреча «{neg.topic}» с {display_name(other)}: {why}.{tail}{contact}",
                data={"negotiation_id": neg.id}))
    return neg


async def confirm(session: AsyncSession, notifier: Notifier, neg_id: int, user: User, accept: bool,
                  now: datetime | None = None) -> Negotiation:
    """Опасное действие (встреча в календарях двоих) — только по явному «да» обоих."""
    now = now or datetime.now(timezone.utc)
    neg = await session.scalar(select(Negotiation).where(
        Negotiation.id == neg_id, or_(Negotiation.initiator_id == user.id, Negotiation.counterpart_id == user.id)))
    if neg is None:
        raise NegotiationError("переговоры не найдены")
    if neg.status != "agreed":
        raise NegotiationError("подтверждать уже нечего")
    other = await session.get(User, neg.counterpart_id if user.id == neg.initiator_id else neg.initiator_id)
    if not accept:
        neg.status = "declined"
        await session.commit()
        await notifier.notify(other.id, Note(kind="negotiation.declined", title="✖ Встреча отклонена",
                                             body=f"{display_name(user)} отклонил встречу «{neg.topic}».",
                                             data={"negotiation_id": neg.id}))
        return neg
    if user.id == neg.initiator_id:
        neg.initiator_ok = True
    else:
        neg.counterpart_ok = True
    if neg.initiator_ok and neg.counterpart_ok:
        neg.status = "confirmed"
        for me_id, other_id in ((neg.initiator_id, neg.counterpart_id), (neg.counterpart_id, neg.initiator_id)):
            me, them = await session.get(User, me_id), await session.get(User, other_id)
            due = neg.slot - REMIND_BEFORE if neg.slot - REMIND_BEFORE > now else neg.slot
            local = neg.slot.astimezone(ZoneInfo(me.timezone))
            await create_task(session, me, due, f"Встреча «{neg.topic}» с {display_name(them)} в {local:%H:%M}")
            await notifier.notify(me.id, Note(kind="negotiation.confirmed", title="📅 Встреча подтверждена",
                                              body=f"«{neg.topic}» — {format_due(neg.slot, me.timezone, now)}. "
                                                   "Напомню за 15 минут.", data={"negotiation_id": neg.id}))
    await session.commit()
    return neg
