"""ORM-модели: пользователи, анкета, память, чат, задачи, устройства, оплаты, переговоры."""
from __future__ import annotations

from datetime import date, datetime, timezone

from sqlalchemy import (
    JSON, BigInteger, Boolean, CheckConstraint, Date, DateTime, ForeignKey, Integer, String, Text,
    TypeDecorator, false,
)
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column, relationship

MEMORY_KINDS = ("fact", "preference", "schedule", "contact")
CHAT_ROLES = ("user", "assistant")
TASK_STATUSES = ("pending", "sent", "done", "cancelled")
TASK_REPEATS = ("none", "daily", "weekly")
NEGOTIATION_STATUSES = ("negotiating", "agreed", "confirmed", "escalated", "declined")


def utcnow() -> datetime:
    return datetime.now(timezone.utc)


class UTCDateTime(TypeDecorator):
    """Храним наивный UTC, отдаём aware UTC: SQLite теряет пояс, а сравнения дат должны быть точными."""
    impl = DateTime
    cache_ok = True

    def process_bind_param(self, value, dialect):
        if value is None:
            return None
        if value.tzinfo is None:
            raise ValueError("нужна дата с часовым поясом")
        return value.astimezone(timezone.utc).replace(tzinfo=None)

    def process_result_value(self, value, dialect):
        if value is None:
            return None
        return value.replace(tzinfo=timezone.utc) if value.tzinfo is None else value.astimezone(timezone.utc)


def _in(col: str, values: tuple[str, ...], name: str) -> CheckConstraint:
    return CheckConstraint(f"{col} IN {values!r}", name=name)


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
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)
    # с этапа 3+ (добавляются в старую базу автомиграцией db.init_db)
    tg_username: Mapped[str | None] = mapped_column(String(64), index=True)
    voice_replies: Mapped[bool] = mapped_column(Boolean, default=False, server_default=false())
    premium_until: Mapped[datetime | None] = mapped_column(UTCDateTime)

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
    completed_at: Mapped[datetime | None] = mapped_column(UTCDateTime)
    updated_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow, onupdate=utcnow)

    user: Mapped[User] = relationship(back_populates="profile")


class Memory(Base):
    """Долгая память: {kind, text}, извлекается LLM после диалога (этап 2)."""
    __tablename__ = "memories"
    __table_args__ = (
        _in("kind", MEMORY_KINDS, "ck_memories_kind"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    kind: Mapped[str] = mapped_column(String(16))
    text: Mapped[str] = mapped_column(Text)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)

    user: Mapped[User] = relationship(back_populates="memories")


class ChatMessage(Base):
    """История диалога — короткий контекст (последние реплики) для LLM."""
    __tablename__ = "messages"
    __table_args__ = (
        _in("role", CHAT_ROLES, "ck_messages_role"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(ForeignKey("users.id", ondelete="CASCADE"), index=True)
    role: Mapped[str] = mapped_column(String(16))
    text: Mapped[str] = mapped_column(Text)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)


def _fk_user() -> ForeignKey:
    return ForeignKey("users.id", ondelete="CASCADE")


class Task(Base):
    """Напоминание: due_at в UTC; планировщик шлёт его во все каналы пользователя."""
    __tablename__ = "tasks"
    __table_args__ = (
        _in("status", TASK_STATUSES, "ck_tasks_status"),
        _in("repeat", TASK_REPEATS, "ck_tasks_repeat"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    text: Mapped[str] = mapped_column(String(500))
    due_at: Mapped[datetime] = mapped_column(UTCDateTime, index=True)
    repeat: Mapped[str] = mapped_column(String(8), default="none")
    status: Mapped[str] = mapped_column(String(12), default="pending", index=True)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)
    sent_at: Mapped[datetime | None] = mapped_column(UTCDateTime)


class Device(Base):
    """FCM-токен устройства Android (один пользователь — несколько устройств)."""
    __tablename__ = "devices"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    fcm_token: Mapped[str] = mapped_column(String(512), unique=True)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)


class ApiSession(Base):
    """Сессия APK: в БД только SHA-256 токена, сам токен знает лишь клиент."""
    __tablename__ = "api_sessions"

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    token_hash: Mapped[str] = mapped_column(String(64), unique=True)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)
    last_used_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)


class LinkCode(Base):
    """Одноразовый код из TG (/link) для привязки Telegram к аккаунту APK."""
    __tablename__ = "link_codes"

    code: Mapped[str] = mapped_column(String(16), primary_key=True)
    user_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    expires_at: Mapped[datetime] = mapped_column(UTCDateTime)


class DailyUsage(Base):
    """Счётчик сообщений к агенту за день (в поясе пользователя) — общий для TG и APK."""
    __tablename__ = "daily_usage"

    user_id: Mapped[int] = mapped_column(_fk_user(), primary_key=True)
    day: Mapped[date] = mapped_column(Date, primary_key=True)
    count: Mapped[int] = mapped_column(Integer, default=0)


class Payment(Base):
    """Зачисленная оплата; external_id уникален — повторное уведомление не продлит дважды."""
    __tablename__ = "payments"
    __table_args__ = (_in("provider", ("stars", "ton"), "ck_payments_provider"),)

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    provider: Mapped[str] = mapped_column(String(8))
    amount: Mapped[int] = mapped_column(BigInteger)  # звёзды или нанотоны
    external_id: Mapped[str] = mapped_column(String(128), unique=True)
    days: Mapped[int] = mapped_column(Integer)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)


class TonInvoice(Base):
    """Счёт в TON: пользователь переводит amount на кошелёк с комментарием comment."""
    __tablename__ = "ton_invoices"
    __table_args__ = (_in("status", ("pending", "paid", "expired"), "ck_ton_status"),)

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    user_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    comment: Mapped[str] = mapped_column(String(32), unique=True)
    amount_nano: Mapped[int] = mapped_column(BigInteger)
    status: Mapped[str] = mapped_column(String(8), default="pending", index=True)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)
    expires_at: Mapped[datetime] = mapped_column(UTCDateTime)


class Negotiation(Base):
    """Переговоры двух агентов о времени встречи: максимум 5 раундов, затем эскалация к людям."""
    __tablename__ = "negotiations"
    __table_args__ = (_in("status", NEGOTIATION_STATUSES, "ck_negotiations_status"),)

    id: Mapped[int] = mapped_column(Integer, primary_key=True)
    initiator_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    counterpart_id: Mapped[int] = mapped_column(_fk_user(), index=True)
    topic: Mapped[str] = mapped_column(String(200))
    duration_min: Mapped[int] = mapped_column(Integer, default=60)
    status: Mapped[str] = mapped_column(String(12), default="negotiating")
    slot: Mapped[datetime | None] = mapped_column(UTCDateTime)
    rounds: Mapped[int] = mapped_column(Integer, default=0)
    log: Mapped[list] = mapped_column(JSON, default=list)
    initiator_ok: Mapped[bool] = mapped_column(Boolean, default=False)
    counterpart_ok: Mapped[bool] = mapped_column(Boolean, default=False)
    created_at: Mapped[datetime] = mapped_column(UTCDateTime, default=utcnow)
