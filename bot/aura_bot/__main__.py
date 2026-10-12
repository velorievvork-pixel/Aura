"""Запуск: python -m aura_bot (токен и прочее — из .env)."""
from __future__ import annotations

import asyncio
import logging
import sys

from aiogram import Bot
from aiogram.types import BotCommand

from .bot import build_dispatcher
from .config import Settings
from .db import init_db, make_engine, make_sessionmaker
from .llm import OpenAIChat


async def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")
    settings = Settings.from_env()
    if not settings.bot_token:
        sys.exit("BOT_TOKEN не задан: получите токен у @BotFather и пропишите его в .env")
    engine = make_engine(settings.database_url)
    await init_db(engine)
    llm = None
    if settings.openai_api_key:
        llm = OpenAIChat(settings.openai_api_key, settings.openai_base_url, settings.openai_model,
                         json_mode=settings.llm_json_mode)
    else:
        logging.warning("OPENAI_API_KEY не задан — чат ответит, что LLM не настроена")
    bot = Bot(settings.bot_token)
    dp = build_dispatcher(make_sessionmaker(engine), settings.default_tz, llm)
    await bot.set_my_commands([
        BotCommand(command="start", description="Анкета / профиль"),
        BotCommand(command="memory", description="Что Aura обо мне знает"),
        BotCommand(command="forget", description="Забыть запись или всё"),
        BotCommand(command="cancel", description="Прервать анкету"),
    ])
    try:
        await dp.start_polling(bot)
    finally:
        await bot.session.close()
        if llm:
            await llm.aclose()
        await engine.dispose()


if __name__ == "__main__":
    asyncio.run(main())
