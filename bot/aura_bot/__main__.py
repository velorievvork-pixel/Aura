"""Запуск: python -m aura_bot — Telegram-бот + HTTP API для APK + планировщик в одном процессе."""
from __future__ import annotations

import asyncio
import logging

import uvicorn
from aiogram import Bot
from aiogram.types import BotCommand

from .api import create_app
from .billing import TonClient
from .bot import build_dispatcher
from .config import Settings
from .db import init_db, make_engine, make_sessionmaker
from .llm import OpenAIChat
from .notifier import FcmSender, Notifier
from .scheduler import start_scheduler
from .services import Services
from .speech import Speech

COMMANDS = [
    ("start", "Анкета / профиль"), ("tasks", "Мои задачи и напоминания"), ("memory", "Что Aura обо мне знает"),
    ("forget", "Забыть запись или всё"), ("meet", "Договориться о встрече"), ("settings", "Настройки"),
    ("stats", "Статистика"), ("premium", "Премиум"), ("link", "Привязать приложение Android"),
    ("paysupport", "Поддержка по оплате"), ("cancel", "Прервать анкету"),
]


async def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    st = Settings.from_env()
    engine = make_engine(st.database_url)
    await init_db(engine)
    sm = make_sessionmaker(engine)

    llm = speech = None
    if st.openai_api_key:
        llm = OpenAIChat(st.openai_api_key, st.openai_base_url, st.openai_model, json_mode=st.llm_json_mode)
        speech = Speech(st.openai_api_key, st.stt_base_url or st.openai_base_url, st.stt_model,
                        st.tts_model, st.tts_voice)
    else:
        logging.warning("OPENAI_API_KEY не задан — чат ответит, что LLM не настроена")
    bot = Bot(st.bot_token) if st.bot_token else None
    fcm = FcmSender(st.fcm_credentials_file) if st.fcm_credentials_file else None
    ton = TonClient(st) if st.ton_enabled else None
    services = Services(settings=st, sessionmaker=sm, notifier=Notifier(sm, bot, fcm), llm=llm,
                        speech=speech, ton=ton, bot_username=st.bot_username)

    scheduler = start_scheduler(services)
    server = uvicorn.Server(uvicorn.Config(create_app(services), host=st.api_host, port=st.api_port,
                                           log_level="info"))
    polling = None
    if bot:
        dp = build_dispatcher(services)
        me = await bot.get_me()
        services.bot_username = services.bot_username or me.username
        await bot.set_my_commands([BotCommand(command=c, description=d) for c, d in COMMANDS])
        # сигналы ловит uvicorn; когда он завершится — останавливаем и бота
        polling = asyncio.create_task(dp.start_polling(bot, handle_signals=False))
    else:
        logging.warning("BOT_TOKEN не задан — работает только HTTP API для APK")
    try:
        await server.serve()
    finally:
        if polling:
            await dp.stop_polling()
            await asyncio.gather(polling, return_exceptions=True)
        scheduler.shutdown(wait=False)
        for c in (llm, speech, fcm, ton):
            if c:
                await c.aclose()
        if bot:
            await bot.session.close()
        await engine.dispose()


if __name__ == "__main__":
    asyncio.run(main())
