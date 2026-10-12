"""Сборка диспетчера aiogram (переиспользуется в main и тестах)."""
from __future__ import annotations

from aiogram import Dispatcher
from aiogram.fsm.storage.memory import MemoryStorage

from .handlers import chat, common, meet, memory, onboarding, payments, settings, tasks
from .middlewares import DbMiddleware
from .services import Services


def build_dispatcher(services: Services) -> Dispatcher:
    # MemoryStorage: незаконченная анкета теряется при рестарте — приемлемо
    dp = Dispatcher(storage=MemoryStorage(), services=services)  # services доступны хендлерам как аргумент
    mw = DbMiddleware(services.sessionmaker, services.settings.default_tz)
    dp.message.outer_middleware(mw)
    dp.callback_query.outer_middleware(mw)
    dp.pre_checkout_query.outer_middleware(mw)
    dp.include_routers(
        payments.make_router(),   # раньше анкеты: /start premium и successful_payment
        onboarding.make_router(), memory.make_router(), tasks.make_router(), settings.make_router(),
        meet.make_router(), chat.make_router(),
        common.make_router(),     # последним — фолбэки
    )
    return dp
