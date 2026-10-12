"""Общие фикстуры: временная SQLite-база и фейковый Telegram (без сети и без токена)."""
from __future__ import annotations

import itertools
from datetime import datetime, timezone
from typing import Any

import pytest
from aiogram import Bot
from aiogram.client.session.base import BaseSession
from aiogram.methods import SendMessage, TelegramMethod
from aiogram.types import CallbackQuery, Chat, Message, Update, User as TgUser

from aura_bot.bot import build_dispatcher
from aura_bot.db import init_db, make_engine, make_sessionmaker
from aura_bot.llm import LLMError


@pytest.fixture
async def engine(tmp_path):
    eng = make_engine(f"sqlite+aiosqlite:///{tmp_path / 'test.db'}")
    await init_db(eng)
    yield eng
    await eng.dispose()


@pytest.fixture
def sessionmaker(engine):
    return make_sessionmaker(engine)


class FakeSession(BaseSession):
    """Перехватывает вызовы Bot API и запоминает их вместо отправки в Telegram."""

    def __init__(self) -> None:
        super().__init__()
        self.calls: list[TelegramMethod] = []
        self._ids = itertools.count(1000)

    async def make_request(self, bot: Bot, method: TelegramMethod, timeout: int | None = None) -> Any:
        self.calls.append(method)
        if isinstance(method, SendMessage):
            return Message(
                message_id=next(self._ids), date=datetime.now(timezone.utc),
                chat=Chat(id=method.chat_id, type="private"), text=method.text,
            )
        return True  # answerCallbackQuery / editMessageReplyMarkup и т.п.

    async def close(self) -> None:  # pragma: no cover
        pass

    async def stream_content(self, *a, **kw):  # pragma: no cover
        raise NotImplementedError
        yield b""


class FakeLLM:
    """LLM-заглушка: ответ чата и JSON извлечения задаются в тесте; все вызовы пишутся."""

    def __init__(self, answer: str = "Ответ Aura", extraction: str = '{"memories": []}') -> None:
        self.answer = answer
        self.extraction = extraction
        self.fail = False
        self.calls: list[tuple[list[dict], bool]] = []

    async def complete(self, messages, *, json_mode=False):
        self.calls.append((messages, json_mode))
        if self.fail:
            raise LLMError("недоступна")
        return self.extraction if json_mode else self.answer


@pytest.fixture
def llm():
    return FakeLLM()


class TgHarness:
    """Мини-эмулятор чата: шлёт апдейты в диспетчер и отдаёт ответы бота."""

    def __init__(self, sessionmaker, user_id: int = 42, llm=None) -> None:
        self.fake = FakeSession()
        # Токен-заглушка нужного формата: в сеть он не уходит (FakeSession)
        self.bot = Bot("123456:" + "A" * 35, session=self.fake)
        self.dp = build_dispatcher(sessionmaker, "Europe/Moscow", llm)
        self.tg_user = TgUser(id=user_id, is_bot=False, first_name="Аня")
        self.chat = Chat(id=user_id, type="private")
        self._ids = itertools.count(1)
        self.last_bot_message: Message | None = None

    def _msg(self, text: str) -> Message:
        return Message(message_id=next(self._ids), date=datetime.now(timezone.utc),
                       chat=self.chat, from_user=self.tg_user, text=text)

    async def _feed(self, **kw) -> list[TelegramMethod]:
        before = len(self.fake.calls)
        await self.dp.feed_update(self.bot, Update(update_id=next(self._ids), **kw))
        new = self.fake.calls[before:]
        for c in new:
            if isinstance(c, SendMessage):
                self.last_bot_message = Message(
                    message_id=next(self._ids), date=datetime.now(timezone.utc), chat=self.chat,
                    from_user=TgUser(id=1, is_bot=True, first_name="Aura"), text=c.text,
                    reply_markup=c.reply_markup,
                )
        return new

    async def send(self, text: str) -> list[TelegramMethod]:
        msg = self._msg(text)
        if text.startswith("/"):
            from aiogram.types import MessageEntity
            msg = msg.model_copy(update={"entities": [
                MessageEntity(type="bot_command", offset=0, length=len(text.split()[0]))]})
        return await self._feed(message=msg)

    async def press(self, data: str) -> list[TelegramMethod]:
        cb = CallbackQuery(id=str(next(self._ids)), from_user=self.tg_user, chat_instance="ci",
                           message=self.last_bot_message, data=data)
        return await self._feed(callback_query=cb)

    @staticmethod
    def texts(calls: list[TelegramMethod]) -> list[str]:
        return [c.text for c in calls if isinstance(c, SendMessage)]


@pytest.fixture
def tg(sessionmaker, llm):
    return TgHarness(sessionmaker, llm=llm)
