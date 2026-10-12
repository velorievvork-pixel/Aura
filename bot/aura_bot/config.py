"""Настройки только из окружения (.env) — никаких ключей в коде."""
from __future__ import annotations

import os
from dataclasses import dataclass, fields
from zoneinfo import ZoneInfo

from dotenv import load_dotenv


def _bool(v: str) -> bool:
    return v.strip().lower() not in ("0", "false", "no", "off", "")


@dataclass(frozen=True)
class Settings:
    # Telegram (без токена бот не стартует, HTTP API работает)
    bot_token: str | None = None
    bot_username: str | None = None          # для ссылок t.me/<bot>; если пусто — узнаём через getMe
    support_contact: str = ""                # для /paysupport (обязательно для цифровых товаров)
    # БД
    database_url: str = "sqlite+aiosqlite:///aura.db"
    # LLM
    openai_api_key: str | None = None
    openai_base_url: str = "https://api.openai.com/v1"
    openai_model: str = "gpt-4o-mini"
    llm_json_mode: bool = True
    # Голос: STT (Whisper-совместимый) и опциональный TTS для голосовых ответов в TG
    stt_model: str = "whisper-1"
    stt_base_url: str | None = None          # пусто — тот же OPENAI_BASE_URL
    tts_model: str = ""                      # пусто — TTS выключен (APK озвучивает сам)
    tts_voice: str = "alloy"
    # HTTP API для APK
    api_host: str = "0.0.0.0"
    api_port: int = 8080
    # Пуши FCM (HTTP v1): путь к JSON сервисного аккаунта Firebase
    fcm_credentials_file: str | None = None
    # Деньги
    default_tz: str = "Europe/Moscow"
    free_messages_per_day: int = 20
    premium_messages_per_day: int = 500     # «безлимит» с защитой от злоупотреблений
    premium_days: int = 30
    premium_price_rub: int = 399
    premium_price_stars: int = 250
    ton_wallet: str | None = None
    premium_price_ton: str = ""              # например 2.5; пусто — оплата TON выключена
    toncenter_url: str = "https://toncenter.com/api/v2"
    toncenter_api_key: str | None = None

    @classmethod
    def from_env(cls) -> "Settings":
        load_dotenv()  # .env не перетирает уже заданные переменные окружения
        kw = {}
        for f in fields(cls):
            raw = os.getenv(f.name.upper())
            if raw is None or raw == "":
                continue
            kw[f.name] = _bool(raw) if f.type == "bool" else int(raw) if f.type == "int" else raw
        s = cls(**kw)
        ZoneInfo(s.default_tz)  # падаем сразу, если пояс в .env кривой
        if s.premium_price_ton:
            float(s.premium_price_ton)
        return s

    @property
    def ton_enabled(self) -> bool:
        return bool(self.ton_wallet and self.premium_price_ton)
