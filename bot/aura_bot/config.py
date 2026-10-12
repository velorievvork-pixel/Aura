"""Настройки только из окружения (.env) — никаких ключей в коде."""
from __future__ import annotations

import os
from dataclasses import dataclass
from zoneinfo import ZoneInfo

from dotenv import load_dotenv


@dataclass(frozen=True)
class Settings:
    bot_token: str | None
    database_url: str
    openai_api_key: str | None
    openai_base_url: str
    openai_model: str
    default_tz: str
    free_messages_per_day: int

    @classmethod
    def from_env(cls) -> "Settings":
        load_dotenv()  # .env не перетирает уже заданные переменные окружения
        tz = os.getenv("DEFAULT_TZ") or "Europe/Moscow"
        ZoneInfo(tz)  # падаем сразу, если пояс в .env кривой
        return cls(
            bot_token=os.getenv("BOT_TOKEN") or None,
            database_url=os.getenv("DATABASE_URL") or "sqlite+aiosqlite:///aura.db",
            openai_api_key=os.getenv("OPENAI_API_KEY") or None,
            openai_base_url=os.getenv("OPENAI_BASE_URL") or "https://api.openai.com/v1",
            openai_model=os.getenv("OPENAI_MODEL") or "gpt-4o-mini",
            default_tz=tz,
            free_messages_per_day=int(os.getenv("FREE_MESSAGES_PER_DAY") or 20),
        )
