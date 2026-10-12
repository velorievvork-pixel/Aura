"""Async-движок SQLAlchemy и фабрика сессий (SQLite сейчас, PostgreSQL — сменой DATABASE_URL)."""
from __future__ import annotations

from sqlalchemy import event
from sqlalchemy.ext.asyncio import AsyncEngine, AsyncSession, async_sessionmaker, create_async_engine

from .models import Base


def make_engine(url: str) -> AsyncEngine:
    engine = create_async_engine(url)
    if engine.dialect.name == "sqlite":
        # В SQLite внешние ключи по умолчанию выключены — включаем на каждом соединении
        @event.listens_for(engine.sync_engine, "connect")
        def _fk_on(dbapi_conn, _):
            cur = dbapi_conn.cursor()
            cur.execute("PRAGMA foreign_keys=ON")
            cur.close()
    return engine


def make_sessionmaker(engine: AsyncEngine) -> async_sessionmaker[AsyncSession]:
    return async_sessionmaker(engine, expire_on_commit=False)


async def init_db(engine: AsyncEngine) -> None:
    """Создаёт таблицы (до появления Alembic-миграций при переезде на PostgreSQL)."""
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
