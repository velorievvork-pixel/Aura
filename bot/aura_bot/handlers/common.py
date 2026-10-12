"""Фолбэки: неизвестные команды, нетекстовые сообщения, устаревшие кнопки."""
from __future__ import annotations

from aiogram import F, Router
from aiogram.filters import StateFilter
from aiogram.types import CallbackQuery, Message

router_hint = "Команды: /memory — что я знаю, /forget — забыть, /start — анкета."


async def unknown_command(message: Message) -> None:
    await message.answer("Не знаю такой команды. " + router_hint)


async def non_text(message: Message) -> None:
    """Голосовые подключим на этапе 4; фото/стикеры пока не обрабатываем."""
    await message.answer("Пока понимаю только текст. " + router_hint)


async def stale_button(cb: CallbackQuery) -> None:
    """Кнопка из старого сообщения/другого шага — снимаем «часики» и объясняем."""
    await cb.answer("Эта кнопка уже неактуальна", show_alert=False)


def make_router() -> Router:
    r = Router(name="common")
    r.message.register(unknown_command, StateFilter(None), F.text.startswith("/"))
    r.message.register(non_text, StateFilter(None))
    r.callback_query.register(stale_button)
    return r
