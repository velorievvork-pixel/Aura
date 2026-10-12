"""Фолбэк вне анкеты: без анкеты — отправляем на /start; чат с LLM появится на этапе 2."""
from __future__ import annotations

from aiogram import Router
from aiogram.filters import StateFilter
from aiogram.types import CallbackQuery, Message

from ..models import User
from ..repo import is_onboarded

async def fallback(message: Message, user: User) -> None:
    if not is_onboarded(user):
        await message.answer("Сначала короткая анкета — нажмите /start.")
        return
    await message.answer("Анкета учтена. Чат с агентом подключается на следующем этапе.")


async def stale_button(cb: CallbackQuery) -> None:
    """Кнопка из старого сообщения/другого шага — снимаем «часики» и объясняем."""
    await cb.answer("Эта кнопка уже неактуальна", show_alert=False)


def make_router() -> Router:
    r = Router(name="common")
    r.message.register(fallback, StateFilter(None))
    r.callback_query.register(stale_button)
    return r
