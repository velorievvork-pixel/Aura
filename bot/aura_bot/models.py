"""ORM-модели: пользователи, анкета, долгая память, история чата."""
from __future__ import annotations

from datetime import date, datetime, timezone

from sqlalchemy import (
    JSON, BigInteger, CheckConstraint, Date, DateTime, ForeignKey, Integer, String, Text,
)
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column, relationship

MEMORY_KINDS = ("fact", "preference", "schedule", "contact")
CHAT_ROLES = ("user", "assistant")


def utcnow() -> datetime:
    return datetime.now(timezone.utc)


class Base(DeclarativeBase):
    pass


class User(Base):
    """Аккаунт: в TG — tg_id, в APK — email + Argon2id-хэш (этап 5). Телефона нет by design."""
    __tablename__ = "users"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    tg_id: Mapped[int | None] = mapped_column(BigInteger, unique=True, index=True)
    email: Mapped[str | None] = mapped_column(String(254), unique=True)
    password_hash: Mapped[str | None] = mapped_column(String(255))
    name: Mapped[str | None] = mapped_column(String(128))
    timezone: Mapped[str] = mapped_column(String(64), default="Europe/Moscow")
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)

    profile: Mapped["Profile | None"] = relationship(
        back_populates="user", uselist=False, cascade="all, delete-orphan", lazy="selectin"
    )
    memories: Mapped[list["Memory"]] = relationship(
        back_populates="user", cascade="all, delete-orphan"
    )
    messages: Mapped[list["ChatMessage"]] = relationship(cascade="all, delete-orphan")


class Profile(Base):
    """Анкета онбординга — подмешивается в системный промпт каждого запроса."""
    __tablename__ = "profiles"

    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), primary_key=True)
    birthday: Mapped[date | None] = mapped_column(Date)
    allergies: Mapped[list[str]] = mapped_column(JSON, default=list)
    diets: Mapped[list[str]] = mapped_column(JSON, default=list)
    habits: Mapped[list[str]] = mapped_column(JSON, default=list)
    completed_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True))
    updated_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow, onupdate=utcnow)

    user: Mapped[User] = relationship(back_populates="profile")


class Memory(Base):
    """Долгая память: {kind, text}, извлекается LLM после диалога (этап 2)."""
    __tablename__ = "memories"
    __table_args__ = (
        CheckConstraint(f"kind IN {MEMORY_KINDS!r}", name="ck_memories_kind"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    kind: Mapped[str] = mapped_column(String(16))
    text: Mapped[str] = mapped_column(Text)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)

    user: Mapped[User] = relationship(back_populates="memories")


class ChatMessage(Base):
    """История диалога — короткий контекст (последние реплики) для LLM."""
    __tablename__ = "messages"
    __table_args__ = (
        CheckConstraint(f"role IN {CHAT_ROLES!r}", name="ck_messages_role"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    role: Mapped[str] = mapped_column(String(16))
    text: Mapped[str] = mapped_column(Text)
    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), default=utcnow)
