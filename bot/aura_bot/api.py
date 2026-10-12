"""HTTP/WS API для Android-клиента. Тот же агент, та же БД, те же проверки, что у Telegram-бота.
Ошибки: {"error": {"code", "message"}}; авторизация: Authorization: Bearer <token>."""
from __future__ import annotations

import logging
from datetime import date, datetime, timedelta
from typing import AsyncIterator
from zoneinfo import ZoneInfo

from fastapi import BackgroundTasks, Depends, FastAPI, File, Request, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from . import accounts, agent, negotiation
from .billing import LimitExceeded, create_ton_invoice, is_premium, quota, ton_link
from .llm import LLMError
from .memory import KIND_TITLES
from .models import ChatMessage, Device, Memory, Negotiation, Task, TonInvoice, User
from .onboarding import (
    DIETS, TIMEZONES, ValidationError as OnbError, age, clean_diets, parse_list, parse_timezone,
)
from .repo import (
    delete_memory, display_name, forget_all, get_own_task, is_onboarded, list_memories, list_tasks,
    recent_messages, save_profile,
)
from .scheduler import check_ton
from .services import Services
from .speech import MAX_AUDIO_BYTES
from .tasks import REPEAT_TITLES, TaskError, format_due, validate_task

log = logging.getLogger(__name__)


class ApiError(Exception):
    def __init__(self, status: int, code: str, message: str):
        super().__init__(message)
        self.status, self.code, self.message = status, code, message


# ---------- схемы запросов ----------

class Credentials(BaseModel):
    email: str = Field(max_length=254)
    password: str = Field(max_length=128)
    timezone: str | None = None


class ProfileIn(BaseModel):
    birthday: date | None = None
    allergies: list[str] = Field(default_factory=list, max_length=20)
    diets: list[str] = Field(default_factory=list, max_length=len(DIETS))
    habits: list[str] = Field(default_factory=list, max_length=20)
    timezone: str


class ChatIn(BaseModel):
    text: str = Field(min_length=1, max_length=4000)


class TaskIn(BaseModel):
    datetime: str
    text: str
    repeat: str = "none"


class SettingsIn(BaseModel):
    voice_replies: bool | None = None
    timezone: str | None = None


class DeviceIn(BaseModel):
    fcm_token: str = Field(min_length=20, max_length=512)


class CodeIn(BaseModel):
    code: str = Field(min_length=4, max_length=16)


class ConfirmIn(BaseModel):
    confirm: str


class MeetingIn(BaseModel):
    with_: str = Field(alias="with")
    topic: str
    duration_min: int = 60


class VerdictIn(BaseModel):
    accept: bool


# ---------- сериализация ----------

def _fmt_day(dt: datetime | None, tz: str) -> str | None:
    return dt.astimezone(ZoneInfo(tz)).strftime("%d.%m.%Y") if dt else None


def user_json(u: User) -> dict:
    p, now = u.profile, agent.utc_now()
    return {
        "id": u.id, "email": u.email, "name": display_name(u), "timezone": u.timezone,
        "telegram_linked": u.tg_id is not None, "voice_replies": u.voice_replies,
        "onboarded": is_onboarded(u), "premium": is_premium(u, now),
        "premium_until": _fmt_day(u.premium_until, u.timezone),
        "profile": None if p is None else {
            "birthday": p.birthday.isoformat() if p.birthday else None,
            "age": age(p.birthday) if p.birthday else None,
            "allergies": p.allergies, "diets": p.diets, "habits": p.habits,
        },
    }


def task_json(t: Task, tz: str) -> dict:
    return {"id": t.id, "text": t.text, "due_at": t.due_at.isoformat(), "repeat": t.repeat, "status": t.status,
            "due_human": format_due(t.due_at, tz, agent.utc_now()) + REPEAT_TITLES[t.repeat]}


def neg_json(n: Negotiation, me: User, other: User | None) -> dict:
    mine_ok = n.initiator_ok if me.id == n.initiator_id else n.counterpart_ok
    return {"id": n.id, "topic": n.topic, "status": n.status, "duration_min": n.duration_min,
            "with": display_name(other) if other else "?", "rounds": n.rounds,
            "slot": n.slot.isoformat() if n.slot else None,
            "slot_human": format_due(n.slot, me.timezone, agent.utc_now()) if n.slot else None,
            "needs_my_confirmation": n.status == "agreed" and not mine_ok}


# ---------- приложение ----------

def create_app(services: Services) -> FastAPI:
    app = FastAPI(title="Aura API", version="1.0")
    app.state.services = services
    st = services.settings

    @app.exception_handler(ApiError)
    async def _api_error(_: Request, e: ApiError):
        return JSONResponse({"error": {"code": e.code, "message": e.message}}, status_code=e.status)

    @app.exception_handler(RequestValidationError)
    async def _validation(_: Request, e: RequestValidationError):
        return JSONResponse({"error": {"code": "validation", "message": "Некорректные данные запроса"}},
                            status_code=422)

    async def db() -> AsyncIterator[AsyncSession]:
        async with services.sessionmaker() as s:
            yield s

    def _token(request: Request) -> str:
        auth = request.headers.get("authorization", "")
        if not auth.lower().startswith("bearer ") or len(auth) < 20:
            raise ApiError(401, "unauthorized", "Нужна авторизация")
        return auth[7:].strip()

    async def me(request: Request, s: AsyncSession = Depends(db)) -> User:
        user = await accounts.user_by_token(s, _token(request), agent.utc_now())
        if user is None:
            raise ApiError(401, "unauthorized", "Сессия истекла — войдите снова")
        return user

    def onboarded(user: User) -> None:
        if not is_onboarded(user):
            raise ApiError(409, "onboarding_required", "Сначала заполните анкету")

    async def learn_bg(user_id: int, text: str, reply: str, neg_id: int | None) -> None:
        """После ответа: память и переговоры в отдельной сессии (запросная уже закрыта)."""
        async with services.sessionmaker() as s:
            user = await s.get(User, user_id)
            await s.refresh(user, ["profile"])
            await agent.learn(s, user, text, reply, services.llm)
            if neg_id is not None:
                await negotiation.run(s, services.llm, services.notifier, neg_id)

    async def run_agent(s: AsyncSession, user: User, text: str, bg: BackgroundTasks) -> dict:
        onboarded(user)
        if services.llm is None:
            raise ApiError(503, "llm_unavailable", "LLM не настроена на сервере")
        try:
            res = await agent.handle(s, user, text, services.llm, st)
        except LimitExceeded as e:
            raise ApiError(429, "limit", f"Дневной лимит исчерпан ({e.used}/{e.limit}). "
                                         "Обновится в полночь; премиум снимает ограничение.") from None
        except LLMError:
            raise ApiError(502, "llm_error", "Не получилось связаться с моделью, попробуйте позже") from None
        bg.add_task(learn_bg, user.id, text, res.reply, res.negotiation.id if res.negotiation else None)
        return {"reply": res.reply, "task": task_json(res.task, user.timezone) if res.task else None,
                "task_error": res.task_error, "meeting_error": res.meeting_error,
                "negotiation_started": res.negotiation is not None,
                "usage": {"used": res.quota.used, "limit": res.quota.limit}}

    # --- служебное ---
    @app.get("/api/health")
    async def health():
        return {"ok": True, "llm": services.llm is not None, "stt": services.speech is not None}

    @app.get("/api/options")
    async def options():
        return {"diets": [{"code": c, "title": t} for c, t in DIETS.items()],
                "timezones": [{"name": n, "title": t} for n, t in TIMEZONES.items()],
                "memory_kinds": KIND_TITLES}

    # --- аккаунт ---
    @app.post("/api/auth/register")
    async def register(body: Credentials, s: AsyncSession = Depends(db)):
        tz = st.default_tz
        if body.timezone:
            try:
                tz = parse_timezone(body.timezone)
            except OnbError:
                pass
        try:
            user = await accounts.register(s, body.email, body.password, tz)
        except accounts.AuthError as e:
            raise ApiError(400, "auth", str(e)) from None
        return {"token": await accounts.new_session(s, user), "user": user_json(user)}

    @app.post("/api/auth/login")
    async def login(body: Credentials, request: Request, s: AsyncSession = Depends(db)):
        keys = [f"email:{body.email.strip().lower()}", f"ip:{request.client.host if request.client else '-'}"]
        if any(services.login_limiter.blocked(k) for k in keys):
            raise ApiError(429, "rate_limited", "Слишком много попыток. Попробуйте через 15 минут")
        try:
            user = await accounts.login(s, body.email, body.password)
        except accounts.AuthError as e:
            for k in keys:
                services.login_limiter.fail(k)
            raise ApiError(401, "auth", str(e)) from None
        services.login_limiter.reset(keys[0])
        await s.refresh(user, ["profile"])
        return {"token": await accounts.new_session(s, user), "user": user_json(user)}

    @app.post("/api/auth/logout")
    async def logout(request: Request, s: AsyncSession = Depends(db)):
        await accounts.end_session(s, _token(request))
        return {"ok": True}

    @app.get("/api/me")
    async def get_me(user: User = Depends(me)):
        return user_json(user)

    @app.put("/api/profile")
    async def put_profile(body: ProfileIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        """Анкета из APK — те же правила, что в Telegram."""
        try:
            allergies = parse_list(", ".join(body.allergies)) if body.allergies else []
            habits = parse_list(", ".join(body.habits)) if body.habits else []
            tz = parse_timezone(body.timezone)
        except OnbError as e:
            raise ApiError(400, "validation", str(e)) from None
        if body.birthday and not (date(1900, 1, 1) <= body.birthday <= date.today()):
            raise ApiError(400, "validation", "Дата рождения вне допустимого диапазона")
        if set(body.diets) - set(DIETS):
            raise ApiError(400, "validation", "Неизвестная диета")
        await save_profile(s, user, birthday=body.birthday, allergies=allergies, diets=clean_diets(body.diets),
                           habits=habits, tz=tz)
        await s.refresh(user, ["profile"])
        return user_json(user)

    @app.patch("/api/settings")
    async def patch_settings(body: SettingsIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        if body.timezone is not None:
            try:
                user.timezone = parse_timezone(body.timezone)
            except OnbError as e:
                raise ApiError(400, "validation", str(e)) from None
        if body.voice_replies is not None:
            user.voice_replies = body.voice_replies
        await s.commit()
        return user_json(user)

    @app.post("/api/link/telegram")
    async def link_tg(body: CodeIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        try:
            merged = await accounts.link_telegram(s, user, body.code, agent.utc_now())
        except accounts.AuthError as e:
            raise ApiError(400, "link", str(e)) from None
        return user_json(merged)

    # --- чат и голос ---
    @app.post("/api/chat")
    async def chat(body: ChatIn, bg: BackgroundTasks, user: User = Depends(me), s: AsyncSession = Depends(db)):
        return await run_agent(s, user, body.text, bg)

    @app.post("/api/voice")
    async def voice(bg: BackgroundTasks, file: UploadFile = File(...), user: User = Depends(me),
                    s: AsyncSession = Depends(db)):
        onboarded(user)
        if services.speech is None:
            raise ApiError(503, "stt_unavailable", "Распознавание речи не настроено на сервере")
        audio = await file.read(MAX_AUDIO_BYTES + 1)
        if len(audio) > MAX_AUDIO_BYTES:
            raise ApiError(413, "too_large", "Запись слишком длинная")
        q = await quota(s, user, st, agent.utc_now())
        if q.used >= q.limit:
            raise ApiError(429, "limit", f"Дневной лимит исчерпан ({q.used}/{q.limit})")
        try:
            text = await services.speech.transcribe(audio, file.filename or "voice.m4a",
                                                    file.content_type or "audio/mp4")
        except LLMError:
            raise ApiError(422, "stt_failed", "Не удалось распознать речь") from None
        return {"transcript": text, **(await run_agent(s, user, text, bg))}

    @app.get("/api/chat/history")
    async def history(limit: int = 50, user: User = Depends(me), s: AsyncSession = Depends(db)):
        msgs = await recent_messages(s, user, max(1, min(limit, 200)))
        return [{"role": m.role, "text": m.text, "created_at": m.created_at.isoformat()} for m in msgs]

    # --- память ---
    @app.get("/api/memories")
    async def memories(user: User = Depends(me), s: AsyncSession = Depends(db)):
        return [{"id": m.id, "kind": m.kind, "text": m.text, "created_at": m.created_at.isoformat()}
                for m in await list_memories(s, user)]

    @app.delete("/api/memories/{memory_id}")
    async def del_memory(memory_id: int, user: User = Depends(me), s: AsyncSession = Depends(db)):
        if await delete_memory(s, user, memory_id) is None:
            raise ApiError(404, "not_found", "Запись не найдена")
        return {"ok": True}

    @app.post("/api/memories/wipe")
    async def wipe(body: ConfirmIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        """Опасное действие: клиент обязан показать диалог и прислать confirm=WIPE."""
        if body.confirm != "WIPE":
            raise ApiError(400, "confirmation_required", "Нужно подтверждение")
        return {"deleted": await forget_all(s, user)}

    # --- задачи ---
    @app.get("/api/tasks")
    async def tasks(user: User = Depends(me), s: AsyncSession = Depends(db)):
        return [task_json(t, user.timezone) for t in await list_tasks(s, user, 100)]

    @app.post("/api/tasks")
    async def add_task(body: TaskIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        from .repo import create_task
        try:
            due, text, repeat = validate_task(body.model_dump(), user.timezone, agent.utc_now())
        except TaskError as e:
            raise ApiError(400, "validation", str(e)) from None
        return task_json(await create_task(s, user, due, text, repeat), user.timezone)

    async def _own_active(s: AsyncSession, user: User, task_id: int) -> Task:
        t = await get_own_task(s, user, task_id)
        if t is None or t.status in ("done", "cancelled"):
            raise ApiError(404, "not_found", "Задача не найдена")
        return t

    @app.post("/api/tasks/{task_id}/done")
    async def task_done(task_id: int, user: User = Depends(me), s: AsyncSession = Depends(db)):
        t = await _own_active(s, user, task_id)
        t.status = "done"
        await s.commit()
        return task_json(t, user.timezone)

    @app.post("/api/tasks/{task_id}/snooze")
    async def task_snooze(task_id: int, user: User = Depends(me), s: AsyncSession = Depends(db)):
        t = await _own_active(s, user, task_id)
        t.status, t.due_at = "pending", agent.utc_now() + timedelta(hours=1)
        await s.commit()
        return task_json(t, user.timezone)

    @app.delete("/api/tasks/{task_id}")
    async def task_cancel(task_id: int, user: User = Depends(me), s: AsyncSession = Depends(db)):
        t = await _own_active(s, user, task_id)
        t.status = "cancelled"
        await s.commit()
        return {"ok": True}

    # --- встречи (переговоры агентов) ---
    @app.get("/api/negotiations")
    async def negotiations(user: User = Depends(me), s: AsyncSession = Depends(db)):
        rows = (await s.scalars(select(Negotiation).where(
            (Negotiation.initiator_id == user.id) | (Negotiation.counterpart_id == user.id))
            .order_by(Negotiation.id.desc()).limit(30))).all()
        out = []
        for n in rows:
            other = await s.get(User, n.counterpart_id if n.initiator_id == user.id else n.initiator_id)
            out.append(neg_json(n, user, other))
        return out

    @app.post("/api/negotiations")
    async def start_meeting(body: MeetingIn, bg: BackgroundTasks, user: User = Depends(me),
                            s: AsyncSession = Depends(db)):
        onboarded(user)
        if services.llm is None:
            raise ApiError(503, "llm_unavailable", "LLM не настроена на сервере")
        try:
            n = await negotiation.start(s, user, body.model_dump(by_alias=True), agent.utc_now())
        except negotiation.NegotiationError as e:
            raise ApiError(400, "negotiation", str(e)) from None

        async def _run(neg_id: int) -> None:
            async with services.sessionmaker() as s2:
                await negotiation.run(s2, services.llm, services.notifier, neg_id)
        bg.add_task(_run, n.id)
        return neg_json(n, user, await s.get(User, n.counterpart_id))

    @app.post("/api/negotiations/{neg_id}/confirm")
    async def confirm_meeting(neg_id: int, body: VerdictIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        try:
            n = await negotiation.confirm(s, services.notifier, neg_id, user, body.accept)
        except negotiation.NegotiationError as e:
            raise ApiError(400, "negotiation", str(e)) from None
        other = await s.get(User, n.counterpart_id if n.initiator_id == user.id else n.initiator_id)
        return neg_json(n, user, other)

    # --- устройства (FCM) ---
    @app.post("/api/devices")
    async def add_device(body: DeviceIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        d = await s.scalar(select(Device).where(Device.fcm_token == body.fcm_token))
        if d is None:
            s.add(Device(user_id=user.id, fcm_token=body.fcm_token))
        else:
            d.user_id = user.id  # токен переехал к другому аккаунту на том же телефоне
        await s.commit()
        return {"ok": True}

    @app.delete("/api/devices")
    async def del_device(body: DeviceIn, user: User = Depends(me), s: AsyncSession = Depends(db)):
        d = await s.scalar(select(Device).where(Device.fcm_token == body.fcm_token, Device.user_id == user.id))
        if d:
            await s.delete(d)
            await s.commit()
        return {"ok": True}

    # --- подписка ---
    @app.get("/api/billing")
    async def billing(user: User = Depends(me), s: AsyncSession = Depends(db)):
        q = await quota(s, user, st, agent.utc_now())
        bot = services.bot_username
        return {"premium": q.premium, "premium_until": _fmt_day(user.premium_until, user.timezone),
                "used": q.used, "limit": q.limit, "price_rub": st.premium_price_rub,
                "price_stars": st.premium_price_stars, "days": st.premium_days,
                "stars_link": f"https://t.me/{bot}?start=premium" if bot else None,
                "ton_enabled": st.ton_enabled, "price_ton": st.premium_price_ton or None}

    @app.post("/api/billing/ton")
    async def billing_ton(user: User = Depends(me), s: AsyncSession = Depends(db)):
        if not st.ton_enabled:
            raise ApiError(503, "ton_disabled", "Оплата TON не настроена")
        inv = await create_ton_invoice(s, user, st, agent.utc_now())
        return {"id": inv.id, "wallet": st.ton_wallet, "amount_ton": st.premium_price_ton, "comment": inv.comment,
                "link": ton_link(st, inv), "expires_at": inv.expires_at.isoformat()}

    @app.post("/api/billing/ton/{invoice_id}/check")
    async def billing_ton_check(invoice_id: int, user: User = Depends(me), s: AsyncSession = Depends(db)):
        await check_ton(services)
        inv = await s.scalar(select(TonInvoice).where(TonInvoice.id == invoice_id, TonInvoice.user_id == user.id)
                             .execution_options(populate_existing=True))
        if inv is None:
            raise ApiError(404, "not_found", "Счёт не найден")
        return {"status": inv.status}

    @app.get("/api/stats")
    async def stats(user: User = Depends(me), s: AsyncSession = Depends(db)):
        async def count(model, *cond):
            return await s.scalar(select(func.count()).select_from(model).where(model.user_id == user.id, *cond))
        q = await quota(s, user, st, agent.utc_now())
        return {"today": q.used, "limit": q.limit, "messages": await count(ChatMessage, ChatMessage.role == "user"),
                "memories": await count(Memory), "tasks_active": await count(Task, Task.status.in_(("pending", "sent"))),
                "tasks_done": await count(Task, Task.status == "done")}

    # --- события в реальном времени ---
    @app.websocket("/api/ws")
    async def ws(socket: WebSocket, token: str = ""):
        async with services.sessionmaker() as s:
            user = await accounts.user_by_token(s, token, agent.utc_now()) if token else None
        if user is None:
            await socket.close(code=4401)
            return
        await socket.accept()
        services.notifier.ws.add(user.id, socket)
        try:
            while True:
                await socket.receive_text()  # клиентские пинги
        except WebSocketDisconnect:
            pass
        finally:
            services.notifier.ws.remove(user.id, socket)

    return app
