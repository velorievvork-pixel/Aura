"""Контейнер зависимостей: одни и те же сервисы у TG-бота, HTTP API и планировщика."""
from __future__ import annotations

from dataclasses import dataclass, field

from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker

from .accounts import RateLimiter
from .billing import TonClient
from .config import Settings
from .llm import LLM
from .notifier import Notifier
from .speech import Speech


@dataclass
class Services:
    settings: Settings
    sessionmaker: async_sessionmaker[AsyncSession]
    notifier: Notifier
    llm: LLM | None = None
    speech: Speech | None = None
    ton: TonClient | None = None
    bot_username: str | None = None
    login_limiter: RateLimiter = field(default_factory=RateLimiter)
