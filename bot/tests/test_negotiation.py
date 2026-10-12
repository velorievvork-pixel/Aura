"""Переговоры агентов: сервер проверяет каждый ход, 5 раундов, эскалация, подтверждение обоих."""
import json
from datetime import datetime, timedelta, timezone

import pytest
from aiogram.methods import SendMessage
from sqlalchemy import select

from aura_bot import negotiation
from aura_bot.models import Negotiation, Task
from aura_bot.repo import create_task, get_or_create_tg_user, save_profile
from conftest import TgHarness, onboard

NOW = datetime(2026, 10, 12, 6, 0, tzinfo=timezone.utc)  # пн 09:00 МСК


def mv(action, slot=None, reason=""):
    return json.dumps({"action": action, "slot": slot, "reason": reason})


async def pair(sessionmaker):
    async with sessionmaker() as s:
        a = await get_or_create_tg_user(s, 1, "Аня", "Europe/Moscow")
        b = await get_or_create_tg_user(s, 2, "Олег", "Europe/Moscow")
        b.tg_username = "oleg"
        for u in (a, b):
            await save_profile(s, u, birthday=None, allergies=[], diets=[], habits=[], tz="Europe/Moscow")
        neg = await negotiation.start(s, a, {"with": "@oleg", "topic": "проект", "duration_min": 60}, NOW)
        return a, b, neg


class Spy:
    def __init__(self):
        self.notes = []

    async def notify(self, user_id, note):
        self.notes.append((user_id, note))


async def test_agree_then_both_confirm_creates_tasks(sessionmaker, llm):
    a, b, neg = await pair(sessionmaker)
    llm.moves = [mv("propose", "2026-10-13T15:00"), mv("accept")]
    spy = Spy()
    async with sessionmaker() as s:
        n = await negotiation.run(s, llm, spy, neg.id, now=NOW)
    assert n.status == "agreed" and n.slot == datetime(2026, 10, 13, 12, 0, tzinfo=timezone.utc) and n.rounds == 1
    assert {uid for uid, note in spy.notes} == {a.id, b.id}
    assert all(note.buttons[0][1] == f"neg:ok:{n.id}" for _, note in spy.notes)
    # ходы агента видят только время и свою занятость, но не чужие данные
    assert "Олег" in llm.calls[1][0][0]["content"]
    async with sessionmaker() as s:
        n = await negotiation.confirm(s, spy, neg.id, a, True, now=NOW)
        assert n.status == "agreed"                                  # ждём второго
        assert (await s.scalars(select(Task))).all() == []
        n = await negotiation.confirm(s, spy, neg.id, b, True, now=NOW)
        assert n.status == "confirmed"
        tasks = (await s.scalars(select(Task))).all()
    assert {t.user_id for t in tasks} == {a.id, b.id}
    assert all(t.due_at == n.slot - timedelta(minutes=15) and "проект" in t.text for t in tasks)


async def test_server_overrides_agent_on_busy_or_night_slot(sessionmaker, llm):
    a, b, neg = await pair(sessionmaker)
    async with sessionmaker() as s:
        await create_task(s, b, datetime(2026, 10, 13, 12, 0, tzinfo=timezone.utc), "Стоматолог")
    llm.moves = [
        mv("propose", "2026-10-13T15:00"),   # Аня свободна
        mv("accept"),                        # агент Олега «согласился», но Олег занят — сервер отклоняет
        mv("propose", "2026-10-14T03:00"),   # ночь — отклонено
        mv("propose", "2026-10-14T17:00"),   # Олег предлагает своё
        mv("accept"),                        # Аня принимает
    ]
    async with sessionmaker() as s:
        n = await negotiation.run(s, llm, Spy(), neg.id, now=NOW)
    actions = [(e["side"], e["action"]) for e in n.log]
    assert actions == [("initiator", "propose"), ("counterpart", "invalid"), ("initiator", "invalid"),
                       ("counterpart", "propose"), ("initiator", "accept")]
    assert n.log[1]["reason"] == "владелец занят" and n.log[2]["reason"] == "нерабочее время владельца"
    assert n.status == "agreed" and n.slot == datetime(2026, 10, 14, 14, 0, tzinfo=timezone.utc)


async def test_escalation_after_five_rounds(sessionmaker, llm):
    a, b, neg = await pair(sessionmaker)
    llm.moves = [mv("propose", f"2026-10-{13 + i // 2}T{10 + i % 2}:00") for i in range(20)]
    spy = Spy()
    async with sessionmaker() as s:
        n = await negotiation.run(s, llm, spy, neg.id, now=NOW)
    assert n.status == "escalated" and n.rounds == negotiation.MAX_ROUNDS and len(n.log) == 10
    texts = {uid: note.body for uid, note in spy.notes}
    assert "за 5 раундов" in texts[a.id] and "@oleg" in texts[a.id]
    assert len(llm.moves) == 10                                       # 11-го хода не было


async def test_reject_escalates_and_garbage_is_invalid(sessionmaker, llm):
    a, b, neg = await pair(sessionmaker)
    llm.moves = ["не JSON", mv("reject", reason="занят всю неделю")]
    async with sessionmaker() as s:
        n = await negotiation.run(s, llm, Spy(), neg.id, now=NOW)
    assert [e["action"] for e in n.log] == ["invalid", "reject"] and n.status == "escalated"


async def test_confirm_guards(sessionmaker, llm):
    a, b, neg = await pair(sessionmaker)
    spy = Spy()
    async with sessionmaker() as s:
        stranger = await get_or_create_tg_user(s, 3, None, "Europe/Moscow")
        with pytest.raises(negotiation.NegotiationError):
            await negotiation.confirm(s, spy, neg.id, a, True, now=NOW)   # ещё не договорились
    llm.moves = [mv("propose", "2026-10-13T15:00"), mv("accept")]
    async with sessionmaker() as s:
        await negotiation.run(s, llm, spy, neg.id, now=NOW)
        with pytest.raises(negotiation.NegotiationError):
            await negotiation.confirm(s, spy, neg.id, stranger, True, now=NOW)
        n = await negotiation.confirm(s, spy, neg.id, b, False, now=NOW)
    assert n.status == "declined" and spy.notes[-1][0] == a.id


async def test_start_validation(sessionmaker):
    a, b, _ = await pair(sessionmaker)
    async with sessionmaker() as s:
        for raw in [{"with": "@nobody"}, {"with": "@anya_self"}, {"with": "x"}, {"topic": "без собеседника"}]:
            with pytest.raises(negotiation.NegotiationError):
                await negotiation.start(s, a, raw, NOW)


async def test_meet_command_end_to_end(sessionmaker, llm, settings):
    from conftest import local_iso
    anya = TgHarness(sessionmaker, user_id=42, llm=llm, settings=settings, username="anya")
    oleg = TgHarness(sessionmaker, user_id=43, llm=llm, settings=settings, username="oleg", fake=anya.fake)
    await onboard(anya)
    await onboard(oleg)
    llm.moves = [mv("propose", local_iso(2, 15)), mv("accept")]
    calls = await anya.send("/meet @oleg обсудить проект 30")
    to_oleg = [c for c in calls if isinstance(c, SendMessage) and c.chat_id == 43]
    assert "Агенты договорились" in to_oleg[0].text and "обсудить проект" in to_oleg[0].text
    async with sessionmaker() as s:
        n = (await s.scalars(select(Negotiation))).one()
    await anya.press(f"neg:ok:{n.id}")
    calls = await oleg.press(f"neg:ok:{n.id}")
    assert any(isinstance(c, SendMessage) and "Встреча подтверждена" in c.text for c in calls)
    async with sessionmaker() as s:
        assert len((await s.scalars(select(Task))).all()) == 2
    assert "Формат" in anya.texts(await anya.send("/meet"))[0]
