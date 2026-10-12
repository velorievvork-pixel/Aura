"""Мидлварь: сессия БД на апдейт + текущий пользователь (создаётся по TG ID)."""
from __future__ import annotations

from typing import Any, Awaitable, Callable

from aiogram import BaseMiddleware
from aiogram.types import TelegramObject, User as TgUser
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker

from .repo import get_or_create_tg_user


class DbMiddleware(BaseMiddleware):
    def __init__(self, sessionmaker: async_sessionmaker[AsyncSession], default_tz: str):
        self.sessionmaker = sessionmaker
        self.default_tz = default_tz

    async def __call__(
        self,
        handler: Callable[[TelegramObject, dict[str, Any]], Awaitable[Any]],
        event: TelegramObject,
        data: dict[str, Any],
    ) -> Any:
        async with self.sessionmaker() as session:
            data["session"] = session
            tg_user: TgUser | None = data.get("event_from_user")
            if tg_user is not None and not tg_user.is_bot:
                data["user"] = await get_or_create_tg_user(
                    session, tg_user.id, tg_user.full_name, self.default_tz
                )
            return await handler(event, data)
