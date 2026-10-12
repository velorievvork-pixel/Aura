"""Сквозные сценарии этапа 2 через фейковый Telegram: чат, /memory, /forget."""
import pytest
from aiogram.methods import EditMessageReplyMarkup, EditMessageText, SendChatAction

from aura_bot.handlers.chat import split_text
from aura_bot.handlers.memory import MemDel
from aura_bot.models import Memory
from aura_bot.repo import add_memories, get_or_create_tg_user, list_memories, recent_messages


async def onboard(tg, allergies="арахис"):
    await tg.send("/start")
    await tg.press("ob:skip")
    await tg.send(allergies)
    await tg.press("ob:diets_done")
    await tg.press("ob:skip")
    await tg.press("tz:Europe/Moscow")
    await tg.press("ob:save")


async def db_user(sessionmaker, tg):
    s = sessionmaker()
    return s, await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")


async def test_chat_answers_with_profile_and_learns(tg, llm, sessionmaker):
    await onboard(tg)
    llm.answer = "Советую кешью — без арахиса."
    llm.extraction = '{"memories": [{"kind": "preference", "text": "Любит орехи"}]}'
    calls = await tg.send("Что погрызть вечером?")
    assert any(isinstance(c, SendChatAction) for c in calls)
    assert tg.texts(calls) == ["Советую кешью — без арахиса."]

    chat_prompt, json_mode = llm.calls[0]
    assert not json_mode and "арахис" in chat_prompt[0]["content"]  # анкета применена в ответе
    assert llm.calls[1][1] is True                                   # затем извлечение памяти

    s, u = await db_user(sessionmaker, tg)
    async with s:
        assert [m.text for m in await list_memories(s, u)] == ["Любит орехи"]
        assert len(await recent_messages(s, u, 10)) == 2

    shown = tg.texts(await tg.send("/memory"))[0]
    assert "Предпочтения" in shown and "Любит орехи" in shown


async def test_chat_requires_onboarding_and_llm(tg, sessionmaker, llm):
    assert "/start" in tg.texts(await tg.send("привет"))[0]
    assert llm.calls == []
    from conftest import TgHarness
    no_llm = TgHarness(sessionmaker, user_id=99, llm=None)
    await onboard(no_llm)
    assert "OPENAI_API_KEY" in no_llm.texts(await no_llm.send("привет"))[0]


async def test_llm_down_is_polite(tg, llm, sessionmaker):
    await onboard(tg)
    llm.fail = True
    assert "Попробуйте ещё раз" in tg.texts(await tg.send("привет"))[0]
    s, u = await db_user(sessionmaker, tg)
    async with s:
        assert await recent_messages(s, u, 10) == []


async def test_forget_one_by_button(tg, sessionmaker):
    await onboard(tg)
    s, u = await db_user(sessionmaker, tg)
    async with s:
        _, coffee = await add_memories(s, u, [("fact", "Живёт в Казани"), ("preference", "Любит кофе")])
    text = tg.texts(await tg.send("/forget кофе"))[0]
    assert "1. Любит кофе" in text and "Казани" not in text
    mid = coffee.id
    calls = await tg.press(MemDel(id=mid).pack())
    assert any(isinstance(c, EditMessageReplyMarkup) for c in calls)
    async with sessionmaker() as s2:
        assert [m.text for m in await list_memories(s2, u)] == ["Живёт в Казани"]


async def test_cannot_delete_foreign_memory(tg, sessionmaker):
    await onboard(tg)
    async with sessionmaker() as s:
        other = await get_or_create_tg_user(s, 555, None, "Europe/Moscow")
        (m,) = await add_memories(s, other, [("fact", "Чужой секрет")])
    await tg.send("/forget")
    calls = await tg.press(MemDel(id=m.id).pack())  # подделанный id чужой записи
    assert not any(isinstance(c, EditMessageReplyMarkup) for c in calls)
    async with sessionmaker() as s:
        assert await s.get(Memory, m.id) is not None


async def test_wipe_requires_confirmation(tg, llm, sessionmaker):
    await onboard(tg)
    await tg.send("Привет")
    s, u = await db_user(sessionmaker, tg)
    async with s:
        await add_memories(s, u, [("fact", "Живёт в Казани")])
    await tg.send("/forget")
    assert "Отменить нельзя" in tg.texts(await tg.press("mem:wipe"))[0]
    await tg.press("mem:wipe_no")
    async with sessionmaker() as s2:
        assert len(await list_memories(s2, u)) == 1          # отмена ничего не трогает
    await tg.press("mem:wipe")
    calls = await tg.press("mem:wipe_yes")
    assert any(isinstance(c, EditMessageText) and "Стёрто записей памяти: 1" in c.text for c in calls)
    async with sessionmaker() as s3:
        assert await list_memories(s3, u) == [] and await recent_messages(s3, u, 10) == []
        u3 = await get_or_create_tg_user(s3, tg.tg_user.id, None, "Europe/Moscow")
        assert u3.profile.allergies == ["арахис"]            # анкета осталась


async def test_unknown_command_not_sent_to_llm(tg, llm):
    await onboard(tg)
    assert "Не знаю такой команды" in tg.texts(await tg.send("/tasks"))[0]
    assert llm.calls == []


@pytest.mark.parametrize("n", [10, 4000, 4001, 9000])
def test_split_text(n):
    text = ("слово " * (n // 6 + 1))[:n]
    parts = split_text(text)
    assert all(len(p) <= 4000 for p in parts)
    assert "".join(parts).replace(" ", "") == text.replace(" ", "")
