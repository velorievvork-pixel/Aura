"""Доступ к БД: пользователи и анкета."""
from __future__ import annotations

from datetime import date

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from .models import Profile, User, utcnow
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
    """Контекст для LLM из анкеты; с этапа 2 сюда добавится память."""
    p = user.profile
    if p is None:
        return f"Часовой пояс пользователя: {user.timezone}."
    return profile_prompt(p.birthday, p.allergies or [], p.diets or [], p.habits or [], user.timezone)
