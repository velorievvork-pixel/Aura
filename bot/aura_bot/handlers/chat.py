"""Свободный чат и голосовые: ответ агента, действия (задача/встреча), озвучка, обучение памяти."""
from __future__ import annotations

import logging

from aiogram import Bot, F, Router
from aiogram.enums import ChatAction
from aiogram.filters import StateFilter
from aiogram.types import BufferedInputFile, InlineKeyboardButton, InlineKeyboardMarkup, Message
from sqlalchemy.ext.asyncio import AsyncSession

from .. import agent, negotiation
from ..billing import LimitExceeded, check_quota
from ..llm import LLMError
from ..models import User
from ..repo import is_onboarded
from ..services import Services

log = logging.getLogger(__name__)
TG_LIMIT = 4000  # у Telegram 4096, берём с запасом
MAX_VOICE_SECONDS = 300

PREMIUM_KB = InlineKeyboardMarkup(inline_keyboard=[[InlineKeyboardButton(text="💎 Премиум", callback_data="prem:open")]])


def split_text(text: str, limit: int = TG_LIMIT) -> list[str]:
    """Режет длинный ответ по абзацам/строкам, в крайнем случае — жёстко."""
    parts: list[str] = []
    while len(text) > limit:
        cut = max(text.rfind("\n", 0, limit), text.rfind(" ", 0, limit))
        cut = cut if cut > limit // 2 else limit
        parts.append(text[:cut].rstrip())
        text = text[cut:].lstrip()
    return parts + ([text] if text else [])


def limit_text(e: LimitExceeded) -> str:
    return (f"Дневной лимит исчерпан ({e.used}/{e.limit}). Он обновится в полночь, "
            "а с премиумом сообщений почти без ограничений.")


async def _ready(message: Message, user: User, services: Services) -> bool:
    if not is_onboarded(user):
        await message.answer("Сначала короткая анкета — нажмите /start.")
        return False
    if services.llm is None:
        await message.answer("LLM не настроена: администратору нужно задать OPENAI_API_KEY в .env.")
        return False
    return True


async def process_text(message: Message, text: str, bot: Bot, session: AsyncSession, user: User,
                       services: Services, *, voice: bool = False) -> None:
    await bot.send_chat_action(message.chat.id, ChatAction.TYPING)
    try:
        res = await agent.handle(session, user, text, services.llm, services.settings)
    except LimitExceeded as e:
        await message.answer(limit_text(e), reply_markup=PREMIUM_KB)
        return
    except LLMError as e:
        log.warning("LLM недоступна: %s", e)
        await message.answer("Не получилось связаться с моделью. Попробуйте ещё раз чуть позже.")
        return
    parts = split_text(res.reply)
    for i, part in enumerate(parts):
        kb = None
        if res.task is not None and i == len(parts) - 1:
            kb = InlineKeyboardMarkup(inline_keyboard=[[
                InlineKeyboardButton(text="✖ Отменить напоминание", callback_data=f"task:cancel:{res.task.id}")]])
        await message.answer(part, reply_markup=kb)
    if voice and user.voice_replies and services.speech and services.speech.tts_enabled:
        try:
            audio = await services.speech.synthesize(res.reply)
            await message.answer_voice(BufferedInputFile(audio, "aura.ogg"))
        except LLMError as e:
            log.warning("TTS: %s", e)
    # ответ уже у пользователя — теперь учимся на обмене и, если надо, ведём переговоры
    await agent.learn(session, user, text, res.reply, services.llm)
    if res.negotiation is not None:
        await negotiation.run(session, services.llm, services.notifier, res.negotiation.id)


async def on_text(message: Message, bot: Bot, session: AsyncSession, user: User, services: Services) -> None:
    if await _ready(message, user, services):
        await process_text(message, message.text, bot, session, user, services)


async def on_voice(message: Message, bot: Bot, session: AsyncSession, user: User, services: Services) -> None:
    media = message.voice or message.audio or message.video_note
    if not await _ready(message, user, services):
        return
    if services.speech is None:
        await message.answer("Распознавание речи не настроено. Напишите, пожалуйста, текстом.")
        return
    if (media.duration or 0) > MAX_VOICE_SECONDS:
        await message.answer(f"Слишком длинное голосовое — до {MAX_VOICE_SECONDS // 60} минут.")
        return
    try:
        await check_quota(session, user, services.settings, agent.utc_now())  # STT тоже стоит денег
    except LimitExceeded as e:
        await message.answer(limit_text(e), reply_markup=PREMIUM_KB)
        return
    await bot.send_chat_action(message.chat.id, ChatAction.TYPING)
    try:
        file = await bot.get_file(media.file_id)
        buf = await bot.download_file(file.file_path)
        text = await services.speech.transcribe(buf.read(), "voice.ogg", getattr(media, "mime_type", None) or "audio/ogg")
    except LLMError as e:
        log.warning("STT: %s", e)
        await message.answer("Не разобрал голосовое. Попробуйте ещё раз или напишите текстом.")
        return
    await message.answer(f"🎙 «{text[:500]}»")
    await process_text(message, text, bot, session, user, services, voice=True)


def make_router() -> Router:
    r = Router(name="chat")
    r.message.register(on_text, StateFilter(None), F.text, ~F.text.startswith("/"))
    r.message.register(on_voice, StateFilter(None), F.voice | F.audio | F.video_note)
    return r
