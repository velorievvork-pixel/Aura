"""/meet @user тема — агенты договариваются о встрече; итог подтверждают оба человека."""
from __future__ import annotations

import re

from aiogram import F, Router
from aiogram.filters import Command, CommandObject
from aiogram.types import CallbackQuery, Message
from sqlalchemy.ext.asyncio import AsyncSession

from .. import negotiation
from ..agent import utc_now
from ..models import User
from ..repo import is_onboarded
from ..services import Services

USAGE = "Формат: /meet @username тема [минут], например: /meet @oleg обсудить проект 30"


async def cmd_meet(message: Message, command: CommandObject, session: AsyncSession, user: User,
                   services: Services) -> None:
    m = re.fullmatch(r"\s*(\S+)\s+(.+?)(?:\s+(\d{2,3}))?\s*", command.args or "")
    if not m:
        await message.answer(USAGE)
        return
    if not is_onboarded(user) or services.llm is None:
        await message.answer("Сначала анкета (/start); также нужна настроенная LLM.")
        return
    raw = {"with": m.group(1), "topic": m.group(2), "duration_min": int(m.group(3) or 60)}
    try:
        neg = await negotiation.start(session, user, raw, utc_now())
    except negotiation.NegotiationError as e:
        await message.answer(f"⚠️ {e}")
        return
    await message.answer(f"🤝 Начинаю переговоры о «{neg.topic}» (до {negotiation.MAX_ROUNDS} раундов)…")
    await negotiation.run(session, services.llm, services.notifier, neg.id)


async def cb_confirm(cb: CallbackQuery, session: AsyncSession, user: User, services: Services) -> None:
    _, verdict, raw_id = cb.data.split(":")
    try:
        neg = await negotiation.confirm(session, services.notifier, int(raw_id), user, verdict == "ok")
    except negotiation.NegotiationError as e:
        await cb.answer(str(e), show_alert=True)
        return
    await cb.answer("Принято")
    if cb.message:
        await cb.message.edit_reply_markup(reply_markup=None)
    if neg.status == "agreed" and cb.message:
        await cb.message.answer("✅ Ваше подтверждение принято, ждём собеседника.")


def make_router() -> Router:
    r = Router(name="meet")
    r.message.register(cmd_meet, Command("meet"))
    r.callback_query.register(cb_confirm, F.data.regexp(r"^neg:(ok|no):\d+$"))
    return r
