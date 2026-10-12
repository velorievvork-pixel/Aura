"""Свободный чат с агентом: ответ LLM с памятью, затем извлечение новых фактов."""
from __future__ import annotations

import logging

from aiogram import Bot, F, Router
from aiogram.enums import ChatAction
from aiogram.filters import StateFilter
from aiogram.types import Message
from sqlalchemy.ext.asyncio import AsyncSession

from .. import agent
from ..llm import LLM, LLMError
from ..models import User
from ..repo import is_onboarded

log = logging.getLogger(__name__)
TG_LIMIT = 4000  # у Telegram 4096, берём с запасом


def split_text(text: str, limit: int = TG_LIMIT) -> list[str]:
    """Режет длинный ответ по абзацам/строкам, в крайнем случае — жёстко."""
    parts: list[str] = []
    while len(text) > limit:
        cut = max(text.rfind("\n", 0, limit), text.rfind(" ", 0, limit))
        cut = cut if cut > limit // 2 else limit
        parts.append(text[:cut].rstrip())
        text = text[cut:].lstrip()
    return parts + ([text] if text else [])


async def on_text(message: Message, bot: Bot, session: AsyncSession, user: User, llm: LLM | None) -> None:
    if not is_onboarded(user):
        await message.answer("Сначала короткая анкета — нажмите /start.")
        return
    if llm is None:
        await message.answer("LLM не настроена: администратору нужно задать OPENAI_API_KEY в .env.")
        return
    await bot.send_chat_action(message.chat.id, ChatAction.TYPING)
    try:
        answer = await agent.reply(session, user, message.text, llm)
    except LLMError as e:
        log.warning("LLM недоступна: %s", e)
        await message.answer("Не получилось связаться с моделью. Попробуйте ещё раз чуть позже.")
        return
    for part in split_text(answer):
        await message.answer(part)
    # ответ уже у пользователя — теперь спокойно учимся на этом обмене
    await agent.learn(session, user, message.text, answer, llm)


def make_router() -> Router:
    r = Router(name="chat")
    r.message.register(on_text, StateFilter(None), F.text, ~F.text.startswith("/"))
    return r
