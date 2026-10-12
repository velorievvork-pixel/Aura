"""Доступ к БД: пользователи, анкета, память, история чата."""
from __future__ import annotations

from datetime import date, datetime

from sqlalchemy import delete, func, select
from sqlalchemy.ext.asyncio import AsyncSession

from .models import ChatMessage, Memory, Profile, Task, User, utcnow
from .onboarding import clean_diets, profile_prompt


async def get_or_create_tg_user(session: AsyncSession, tg_id: int, name: str | None, default_tz: str) -> User:
    """TG ID = аккаунт: первое сообщение создаёт пользователя, без паролей и телефона."""
    user = await session.scalar(select(User).where(User.tg_id == tg_id))
    if user is None:
        user = User(tg_id=tg_id, name=(name or "")[:128] or None, timezone=default_tz)
        session.add(user)
        await session.commit()
        await session.refresh(user, ["profile"])
    return user


async def save_profile(
    session: AsyncSession,
    user: User,
    *,
    birthday: date | None,
    allergies: list[str],
    diets: list[str],
    habits: list[str],
    tz: str,
) -> Profile:
    """Сохраняет (или перезаписывает) анкету и помечает онбординг завершённым."""
    profile = user.profile or Profile(user_id=user.id)
    profile.birthday = birthday
    profile.allergies = list(allergies)
    profile.diets = clean_diets(diets)
    profile.habits = list(habits)
    profile.completed_at = utcnow()
    user.timezone = tz
    user.profile = profile
    session.add(user)
    await session.commit()
    return profile


def is_onboarded(user: User) -> bool:
    return user.profile is not None and user.profile.completed_at is not None


def user_context(user: User) -> str:
    """Блок анкеты для системного промпта."""
    p = user.profile
    if p is None:
        return f"Часовой пояс пользователя: {user.timezone}."
    return profile_prompt(p.birthday, p.allergies or [], p.diets or [], p.habits or [], user.timezone)


# ---------- память ----------

async def list_memories(session: AsyncSession, user: User, limit: int | None = None) -> list[Memory]:
    """Самые свежие сверху при limit, в хронологическом порядке на выходе."""
    q = select(Memory).where(Memory.user_id == user.id).order_by(Memory.created_at.desc(), Memory.id.desc())
    if limit:
        q = q.limit(limit)
    return list(reversed((await session.scalars(q)).all()))


async def add_memories(session: AsyncSession, user: User, items: list[tuple[str, str]]) -> list[Memory]:
    rows = [Memory(user_id=user.id, kind=k, text=t) for k, t in items]
    session.add_all(rows)
    await session.commit()
    return rows


async def find_memories(session: AsyncSession, user: User, needle: str, limit: int = 20) -> list[Memory]:
    """Поиск по подстроке без учёта регистра (в Python: SQLite lower() не знает кириллицу)."""
    n = needle.casefold()
    return [m for m in await list_memories(session, user) if n in m.text.casefold()][-limit:]


async def delete_memory(session: AsyncSession, user: User, memory_id: int) -> Memory | None:
    """Удаляет только СВОЮ запись: чужой id из подделанной кнопки -> None."""
    m = await session.scalar(select(Memory).where(Memory.id == memory_id, Memory.user_id == user.id))
    if m is not None:
        await session.delete(m)
        await session.commit()
    return m


async def forget_all(session: AsyncSession, user: User) -> int:
    """Стирает всю память и историю чата (анкета остаётся). Возвращает число записей памяти."""
    n = await session.scalar(select(func.count()).select_from(Memory).where(Memory.user_id == user.id))
    await session.execute(delete(Memory).where(Memory.user_id == user.id))
    await session.execute(delete(ChatMessage).where(ChatMessage.user_id == user.id))
    await session.commit()
    return int(n or 0)


# ---------- история чата ----------

async def add_message(session: AsyncSession, user: User, role: str, text: str) -> None:
    session.add(ChatMessage(user_id=user.id, role=role, text=text))
    await session.commit()


async def recent_messages(session: AsyncSession, user: User, limit: int) -> list[ChatMessage]:
    q = (select(ChatMessage).where(ChatMessage.user_id == user.id)
         .order_by(ChatMessage.id.desc()).limit(limit))
    return list(reversed((await session.scalars(q)).all()))


# ---------- задачи ----------

async def create_task(session: AsyncSession, user: User, due: datetime, text: str, repeat: str = "none") -> Task:
    t = Task(user_id=user.id, text=text[:500], due_at=due, repeat=repeat)
    session.add(t)
    await session.commit()
    return t


async def list_tasks(session: AsyncSession, user: User, limit: int = 50) -> list[Task]:
    """Активные задачи: ждущие и уже напомненные, но не отмеченные выполненными."""
    q = (select(Task).where(Task.user_id == user.id, Task.status.in_(("pending", "sent")))
         .order_by(Task.due_at).limit(limit))
    return list((await session.scalars(q)).all())


async def get_own_task(session: AsyncSession, user: User, task_id: int) -> Task | None:
    return await session.scalar(select(Task).where(Task.id == task_id, Task.user_id == user.id))


async def busy_times(session: AsyncSession, user: User, start: datetime, end: datetime) -> list[datetime]:
    q = select(Task.due_at).where(Task.user_id == user.id, Task.status == "pending",
                                  Task.due_at >= start, Task.due_at <= end).order_by(Task.due_at)
    return list((await session.scalars(q)).all())


async def find_user_by_ref(session: AsyncSession, ref: str) -> User | None:
    """@username (Telegram) или email (APK)."""
    ref = ref.strip()
    if ref.startswith("@"):
        name = ref[1:].lower()
        return await session.scalar(select(User).where(func.lower(User.tg_username) == name)) if name else None
    if "@" in ref:
        return await session.scalar(select(User).where(User.email == ref.lower()))
    return None


def display_name(user: User) -> str:
    if user.name:
        return user.name
    if user.tg_username:
        return "@" + user.tg_username
    return (user.email or "пользователь").split("@")[0]
