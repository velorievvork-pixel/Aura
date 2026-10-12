"""Сборка диспетчера aiogram (переиспользуется в main и тестах)."""
from __future__ import annotations

from aiogram import Dispatcher
from aiogram.fsm.storage.memory import MemoryStorage
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker

from .handlers import common, onboarding
from .middlewares import DbMiddleware


def build_dispatcher(sessionmaker: async_sessionmaker[AsyncSession], default_tz: str) -> Dispatcher:
    # MemoryStorage: незаконченная анкета теряется при рестарте — для этапа 1 приемлемо
    dp = Dispatcher(storage=MemoryStorage())
    mw = DbMiddleware(sessionmaker, default_tz)
    dp.message.outer_middleware(mw)
    dp.callback_query.outer_middleware(mw)
    dp.include_routers(onboarding.make_router(), common.make_router())  # common — последним (фолбэк)
    return dp
