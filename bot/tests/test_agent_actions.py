"""Ядро агента: действия из JSON-ответа LLM проверяются сервером; лимиты."""
from datetime import date, datetime, timedelta, timezone

import pytest
from sqlalchemy import select

from aura_bot import agent
from aura_bot.billing import LimitExceeded, grant_premium
from aura_bot.models import Negotiation, Task
from aura_bot.repo import get_or_create_tg_user, save_profile
from conftest import chat_json

NOW = datetime(2026, 10, 12, 9, 0, tzinfo=timezone.utc)


async def mk_user(s, tg_id=5, username=None):
    u = await get_or_create_tg_user(s, tg_id, "Аня", "Europe/Moscow")
    u.tg_username = username
    await save_profile(s, u, birthday=None, allergies=[], diets=[], habits=[], tz="Europe/Moscow")
    return u


def test_parse_output_variants():
    assert agent.parse_agent_output("просто текст").reply == "просто текст"
    out = agent.parse_agent_output('```json\n{"reply": "ок", "task": {"datetime": "x", "text": "y"}, "meeting": 5}\n```')
    assert out.reply == "ок" and out.task == {"datetime": "x", "text": "y"} and out.meeting is None
    assert agent.parse_agent_output("[1, 2]").reply == "[1, 2]"


async def test_task_created_and_confirmed_by_server(sessionmaker, llm, settings):
    llm.answer = chat_json("Хорошо!", {"datetime": "2026-10-13T15:00", "text": "Позвонить маме", "repeat": "none"})
    async with sessionmaker() as s:
        u = await mk_user(s)
        res = await agent.handle(s, u, "напомни завтра в 15 позвонить маме", llm, settings, now=NOW)
        assert res.task is not None and res.task.due_at == datetime(2026, 10, 13, 12, 0, tzinfo=timezone.utc)
        assert "⏰ Напомню завтра в 15:00: Позвонить маме" in res.reply
        assert res.quota.used == 1


async def test_bad_task_rejected_with_note(sessionmaker, llm, settings):
    llm.answer = chat_json("Напомню!", {"datetime": "2020-01-01T10:00", "text": "x"})
    async with sessionmaker() as s:
        u = await mk_user(s)
        res = await agent.handle(s, u, "напомни вчера", llm, settings, now=NOW)
        assert res.task is None and "уже прошло" in res.reply
        assert (await s.scalars(select(Task))).all() == []


async def test_meeting_validation(sessionmaker, llm, settings):
    async with sessionmaker() as s:
        u = await mk_user(s)
        await mk_user(s, tg_id=6, username="oleg")
        llm.answer = chat_json("Договорюсь", meeting={"with": "@nobody", "topic": "кофе"})
        res = await agent.handle(s, u, "договорись с @nobody", llm, settings, now=NOW)
        assert res.negotiation is None and "не пользуется Aura" in res.reply
        llm.answer = chat_json("Договорюсь", meeting={"with": "@Oleg", "topic": "кофе", "duration_min": 30})
        res = await agent.handle(s, u, "договорись с @oleg", llm, settings, now=NOW)
        assert res.negotiation is not None and res.negotiation.duration_min == 30
        assert (await s.scalars(select(Negotiation))).one().status == "negotiating"


async def test_daily_limit_and_premium(sessionmaker, llm, settings):
    from dataclasses import replace
    st = replace(settings, free_messages_per_day=2, premium_messages_per_day=3)
    async with sessionmaker() as s:
        u = await mk_user(s)
        await agent.handle(s, u, "1", llm, st, now=NOW)
        await agent.handle(s, u, "2", llm, st, now=NOW)
        with pytest.raises(LimitExceeded):
            await agent.handle(s, u, "3", llm, st, now=NOW)
        n_calls = len(llm.calls)
        assert await grant_premium(s, u, provider="stars", amount=250, external_id="x1", days=30, now=NOW)
        assert not await grant_premium(s, u, provider="stars", amount=250, external_id="x1", days=30, now=NOW)
        assert u.premium_until == NOW + timedelta(days=30)
        await agent.handle(s, u, "3", llm, st, now=NOW)       # премиум расширил лимит
        assert len(llm.calls) == n_calls + 1
        # новый день в поясе пользователя — счётчик с нуля
        tomorrow = NOW + timedelta(days=1)
        res = await agent.handle(s, u, "4", llm, st, now=tomorrow)
        assert res.quota.used == 1
