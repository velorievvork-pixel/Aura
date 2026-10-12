"""Общие фикстуры: временная SQLite-база, фейковая LLM и фейковый Telegram (без сети и токена)."""
from __future__ import annotations

import itertools
from datetime import datetime, timezone
from typing import Any

import pytest
from aiogram import Bot
from aiogram.client.session.base import BaseSession
from aiogram.methods import GetFile, SendMessage, SendVoice, TelegramMethod
from aiogram.types import CallbackQuery, Chat, File, Message, MessageEntity, Update, User as TgUser

from aura_bot.bot import build_dispatcher
from aura_bot.config import Settings
from aura_bot.db import init_db, make_engine, make_sessionmaker
from aura_bot.llm import LLMError
from aura_bot.memory import EXTRACTION_PROMPT
from aura_bot.negotiation import NEGOTIATION_MARK
from aura_bot.notifier import Notifier
from aura_bot.services import Services


@pytest.fixture
async def engine(tmp_path):
    eng = make_engine(f"sqlite+aiosqlite:///{tmp_path / 'test.db'}")
    await init_db(eng)
    yield eng
    await eng.dispose()


@pytest.fixture
def sessionmaker(engine):
    return make_sessionmaker(engine)


@pytest.fixture
def settings():
    return Settings(free_messages_per_day=20, premium_messages_per_day=500, premium_price_stars=250)


class FakeLLM:
    """LLM-заглушка: ответ чата, JSON памяти и ходы переговоров задаются в тесте; вызовы пишутся."""

    def __init__(self, answer: str = "Ответ Aura", extraction: str = '{"memories": []}') -> None:
        self.answer = answer
        self.extraction = extraction
        self.moves: list[str] = []   # ходы агентов в переговорах по очереди
        self.fail = False
        self.calls: list[tuple[list[dict], bool]] = []

    async def complete(self, messages, *, json_mode=False):
        self.calls.append((messages, json_mode))
        if self.fail:
            raise LLMError("недоступна")
        system = messages[0]["content"]
        if system == EXTRACTION_PROMPT:
            return self.extraction
        if system.startswith(NEGOTIATION_MARK):
            return self.moves.pop(0) if self.moves else '{"action": "reject", "reason": "нет вариантов"}'
        return self.answer

    def chat_calls(self):
        return [c for c in self.calls if c[0][0]["content"] != EXTRACTION_PROMPT
                and not c[0][0]["content"].startswith(NEGOTIATION_MARK)]


@pytest.fixture
def llm():
    return FakeLLM()


class FakeSession(BaseSession):
    """Перехватывает вызовы Bot API и запоминает их вместо отправки в Telegram."""

    def __init__(self) -> None:
        super().__init__()
        self.calls: list[TelegramMethod] = []
        self._ids = itertools.count(1000)
        self.file_bytes = b"OggS-fake-voice"

    async def make_request(self, bot: Bot, method: TelegramMethod, timeout: int | None = None) -> Any:
        self.calls.append(method)
        if isinstance(method, GetFile):
            return File(file_id=method.file_id, file_unique_id="u", file_path="voice/file_1.oga")
        if isinstance(method, (SendMessage, SendVoice)) or method.__returning__ is Message:
            return Message(message_id=next(self._ids), date=datetime.now(timezone.utc),
                           chat=Chat(id=getattr(method, "chat_id", 0) or 0, type="private"),
                           text=getattr(method, "text", None))
        return True  # answerCallbackQuery / editMessage* / answerPreCheckoutQuery и т.п.

    async def close(self) -> None:  # pragma: no cover
        pass

    async def stream_content(self, *a, **kw):
        yield self.file_bytes


def make_services(sessionmaker, settings, llm, bot=None, speech=None) -> Services:
    return Services(settings=settings, sessionmaker=sessionmaker, notifier=Notifier(sessionmaker, bot),
                    llm=llm, speech=speech, bot_username="aura_test_bot")


class TgHarness:
    """Мини-эмулятор чата: шлёт апдейты в диспетчер и отдаёт ответы бота."""

    def __init__(self, sessionmaker, user_id: int = 42, llm=None, settings=None, username: str | None = "anya",
                 speech=None, fake: FakeSession | None = None) -> None:
        self.fake = fake or FakeSession()
        # Токен-заглушка нужного формата: в сеть он не уходит (FakeSession)
        self.bot = Bot("123456:" + "A" * 35, session=self.fake)
        self.services = make_services(sessionmaker, settings or Settings(), llm, self.bot, speech)
        self.dp = build_dispatcher(self.services)
        self.tg_user = TgUser(id=user_id, is_bot=False, first_name="Аня", username=username)
        self.chat = Chat(id=user_id, type="private")
        self._ids = itertools.count(1)
        self.last_bot_message: Message | None = None

    def _msg(self, text: str | None = None, **kw) -> Message:
        return Message(message_id=next(self._ids), date=datetime.now(timezone.utc),
                       chat=self.chat, from_user=self.tg_user, text=text, **kw)

    async def _feed(self, **kw) -> list[TelegramMethod]:
        before = len(self.fake.calls)
        await self.dp.feed_update(self.bot, Update(update_id=next(self._ids), **kw))
        new = self.fake.calls[before:]
        for c in new:
            if isinstance(c, SendMessage) and c.chat_id == self.chat.id:
                self.last_bot_message = Message(
                    message_id=next(self._ids), date=datetime.now(timezone.utc), chat=self.chat,
                    from_user=TgUser(id=1, is_bot=True, first_name="Aura"), text=c.text,
                    reply_markup=c.reply_markup,
                )
        return new

    async def send(self, text: str) -> list[TelegramMethod]:
        msg = self._msg(text)
        if text.startswith("/"):
            msg = msg.model_copy(update={"entities": [
                MessageEntity(type="bot_command", offset=0, length=len(text.split()[0]))]})
        return await self._feed(message=msg)

    async def send_message(self, msg: Message) -> list[TelegramMethod]:
        return await self._feed(message=msg)

    async def press(self, data: str) -> list[TelegramMethod]:
        cb = CallbackQuery(id=str(next(self._ids)), from_user=self.tg_user, chat_instance="ci",
                           message=self.last_bot_message, data=data)
        return await self._feed(callback_query=cb)

    @staticmethod
    def texts(calls: list[TelegramMethod]) -> list[str]:
        return [c.text for c in calls if isinstance(c, SendMessage)]

    def texts_to_me(self, calls) -> list[str]:
        return [c.text for c in calls if isinstance(c, SendMessage) and c.chat_id == self.chat.id]


@pytest.fixture
def tg(sessionmaker, llm, settings):
    return TgHarness(sessionmaker, llm=llm, settings=settings)


async def onboard(tg, allergies="арахис", tz="Europe/Moscow"):
    """Быстро проходит анкету в эмуляторе."""
    await tg.send("/start")
    await tg.press("ob:skip")
    await tg.send(allergies)
    await tg.press("ob:diets_done")
    await tg.press("ob:skip")
    await tg.press(f"tz:{tz}")
    await tg.press("ob:save")


class FakeSpeech:
    """STT/TTS-заглушка."""

    def __init__(self, text: str = "напомни завтра купить хлеб", tts: bool = True) -> None:
        self.text, self.tts_enabled, self.fail = text, tts, False
        self.heard: list[bytes] = []
        self.spoken: list[str] = []

    async def transcribe(self, audio: bytes, filename: str, mime: str = "") -> str:
        self.heard.append(audio)
        if self.fail:
            raise LLMError("не распознано")
        return self.text

    async def synthesize(self, text: str) -> bytes:
        self.spoken.append(text)
        return b"OggS-reply"


def chat_json(reply: str, task: dict | None = None, meeting: dict | None = None) -> str:
    import json
    return json.dumps({"reply": reply, "task": task, "meeting": meeting}, ensure_ascii=False)


def local_iso(days: int = 1, hour: int = 15, minute: int = 0, tz: str = "Europe/Moscow") -> str:
    """Дата через N дней в поясе пользователя в формате, который отдаёт LLM."""
    from datetime import timedelta
    from zoneinfo import ZoneInfo
    d = datetime.now(ZoneInfo(tz)) + timedelta(days=days)
    return d.replace(hour=hour, minute=minute).strftime("%Y-%m-%dT%H:%M")
