"""Async-движок SQLAlchemy и фабрика сессий (SQLite сейчас, PostgreSQL — сменой DATABASE_URL)."""
from __future__ import annotations

import logging

from sqlalchemy import event, inspect
from sqlalchemy.schema import CreateColumn
from sqlalchemy.ext.asyncio import AsyncEngine, AsyncSession, async_sessionmaker, create_async_engine

from .models import Base

log = logging.getLogger(__name__)


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


def _add_missing_columns(conn) -> None:
    """Мини-миграция: новые nullable/с server_default колонки добавляются в существующие таблицы."""
    insp = inspect(conn)
    for table in Base.metadata.sorted_tables:
        existing = {c["name"] for c in insp.get_columns(table.name)}
        for col in table.columns:
            if col.name in existing:
                continue
            if not (col.nullable or col.server_default is not None):
                raise RuntimeError(f"{table.name}.{col.name}: нужна ручная миграция (NOT NULL без default)")
            ddl = CreateColumn(col).compile(dialect=conn.dialect)
            conn.exec_driver_sql(f"ALTER TABLE {table.name} ADD COLUMN {ddl}")
            log.info("миграция: добавлена колонка %s.%s", table.name, col.name)


async def init_db(engine: AsyncEngine) -> None:
    """Создаёт таблицы и дополняет старые колонками (Alembic — при переезде на PostgreSQL)."""
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
        await conn.run_sync(_add_missing_columns)
