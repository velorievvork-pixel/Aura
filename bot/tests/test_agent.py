"""Ядро агента: сборка контекста, история, обучение памяти."""
from datetime import date, datetime
from zoneinfo import ZoneInfo

import pytest

from aura_bot import agent
from aura_bot.llm import LLMError
from aura_bot.repo import add_memories, get_or_create_tg_user, list_memories, recent_messages, save_profile


@pytest.fixture
async def user(sessionmaker):
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, 5, "Аня", "Europe/Moscow")
        await save_profile(s, u, birthday=date(1994, 3, 7), allergies=["арахис"], diets=[], habits=[], tz="Asia/Omsk")
        return u


async def test_context_contains_profile_memory_time_history(sessionmaker, user, llm, settings):
    async with sessionmaker() as s:
        await add_memories(s, user, [("contact", "Жена Маша")])
        await agent.handle(s, user, "Привет", llm, settings)
        msgs = await agent.build_messages(s, user, "Что подарить Маше?",
                                          now=datetime(2026, 10, 12, 9, 0, tzinfo=ZoneInfo("UTC")))
    system = msgs[0]["content"]
    assert msgs[0]["role"] == "system" and "Aura" in system
    assert "арахис" in system and "НИКОГДА" in system      # анкета сразу в контексте
    assert "Жена Маша" in system                           # долгая память
    assert "2026-10-12 15:00" in system                    # время в поясе пользователя (Омск, UTC+6)
    assert '"task"' in system and '"meeting"' in system      # формат действий
    assert [m["role"] for m in msgs[1:]] == ["user", "assistant", "user"]
    assert msgs[-1]["content"] == "Что подарить Маше?"


async def test_reply_saves_history_only_on_success(sessionmaker, user, llm, settings):
    async with sessionmaker() as s:
        assert (await agent.handle(s, user, "  Привет  ", llm, settings)).reply == "Ответ Aura"
        llm.fail = True
        with pytest.raises(LLMError):
            await agent.handle(s, user, "ещё", llm, settings)
        hist = await recent_messages(s, user, 10)
    assert [(m.role, m.text) for m in hist] == [("user", "Привет"), ("assistant", "Ответ Aura")]


async def test_learn_validates_and_dedupes(sessionmaker, user, llm):
    llm.extraction = ('{"memories": [{"kind": "fact", "text": "Работает дизайнером"},'
                      '{"kind": "hack", "text": "drop table"}, {"kind": "contact", "text": "жена маша"}]}')
    async with sessionmaker() as s:
        await add_memories(s, user, [("contact", "Жена Маша")])
        assert await agent.learn(s, user, "я дизайнер, жена Маша", "Понял", llm) == 1
        texts = [m.text for m in await list_memories(s, user)]
    assert texts == ["Жена Маша", "Работает дизайнером"]
    prompt, json_mode = llm.calls[-1]
    assert json_mode and "Жена Маша" in prompt[1]["content"]  # известное передаётся, чтобы не дублировать


async def test_learn_survives_llm_failure(sessionmaker, user, llm):
    llm.fail = True
    async with sessionmaker() as s:
        assert await agent.learn(s, user, "a", "b", llm) == 0
