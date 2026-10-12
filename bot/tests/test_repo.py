"""Тесты слоя БД."""
from datetime import date

import pytest
from sqlalchemy import select
from sqlalchemy.exc import IntegrityError

from aura_bot.models import Memory, Profile, User
from aura_bot.repo import get_or_create_tg_user, is_onboarded, save_profile, user_context


async def test_user_created_once(sessionmaker):
    async with sessionmaker() as s:
        u1 = await get_or_create_tg_user(s, 777, "Аня", "Europe/Moscow")
        u2 = await get_or_create_tg_user(s, 777, "Аня", "Europe/Moscow")
        assert u1.id == u2.id and not is_onboarded(u1)
        assert len((await s.scalars(select(User))).all()) == 1


async def test_save_profile_and_context(sessionmaker):
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, 1, None, "Europe/Moscow")
        await save_profile(s, u, birthday=date(1990, 1, 2), allergies=["арахис"],
                           diets=["vegan", "unknown"], habits=[], tz="Asia/Omsk")
    async with sessionmaker() as s:  # новая сессия — данные реально в БД
        u = await get_or_create_tg_user(s, 1, None, "Europe/Moscow")
        assert is_onboarded(u) and u.timezone == "Asia/Omsk"
        assert u.profile.diets == ["vegan"]  # неизвестный код отброшен
        ctx = user_context(u)
        assert "арахис" in ctx and "НИКОГДА" in ctx


async def test_memory_kind_constraint(sessionmaker):
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, 2, None, "Europe/Moscow")
        s.add(Memory(user_id=u.id, kind="fact", text="любит чай"))
        await s.commit()
        s.add(Memory(user_id=u.id, kind="secret", text="x"))
        with pytest.raises(IntegrityError):
            await s.commit()


async def test_cascade_delete(sessionmaker):
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, 3, None, "Europe/Moscow")
        await save_profile(s, u, birthday=None, allergies=[], diets=[], habits=[], tz="Europe/Moscow")
        await s.delete(u)
        await s.commit()
        assert (await s.scalars(select(Profile))).all() == []
