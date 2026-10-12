"""Сквозной сценарий анкеты через диспетчер aiogram с фейковым Telegram."""
from datetime import date

from aiogram.methods import EditMessageReplyMarkup

from aura_bot.handlers.onboarding import DietCb, TzCb
from aura_bot.repo import get_or_create_tg_user, user_context


async def test_full_survey(tg, sessionmaker):
    texts = tg.texts(await tg.send("/start"))
    assert "Aura" in texts[0] and "1/5" in texts[1]

    assert "Формат" in tg.texts(await tg.send("вчера"))[0]  # невалидно — остаёмся на шаге
    assert "2/5" in tg.texts(await tg.send("07.03.1994"))[0]
    assert "3/5" in tg.texts(await tg.send("арахис, мёд"))[0]

    calls = await tg.press(DietCb(code="vegan").pack())
    assert any(isinstance(c, EditMessageReplyMarkup) for c in calls)
    assert "4/5" in tg.texts(await tg.press("ob:diets_done"))[0]
    assert "5/5" in tg.texts(await tg.send("бегаю по утрам, не пью кофе"))[0]

    summary = tg.texts(await tg.press(TzCb(name="Asia/Novosibirsk").pack()))[0]
    assert "арахис, мёд" in summary and "веганство" in summary and "Новосибирск" in summary

    done = tg.texts(await tg.press("ob:save"))[0]
    assert "арахис" in done

    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        assert u.profile.birthday == date(1994, 3, 7)
        assert u.profile.allergies == ["арахис", "мёд"] and u.profile.diets == ["vegan"]
        assert u.profile.habits == ["бегаю по утрам", "не пью кофе"] and u.timezone == "Asia/Novosibirsk"
        assert "НИКОГДА не советуй" in user_context(u)

    # повторный /start показывает профиль, а не анкету
    assert "С возвращением" in tg.texts(await tg.send("/start"))[0]


async def test_skip_and_custom_tz(tg, sessionmaker):
    await tg.send("/start")
    await tg.press("ob:skip")            # день рождения
    await tg.press("ob:skip")            # аллергии
    await tg.press("ob:diets_done")      # без диет
    await tg.press("ob:skip")            # привычки
    await tg.press("ob:tz_custom")
    assert "IANA" in tg.texts(await tg.send("MSK"))[0]
    assert "Проверьте" in tg.texts(await tg.send("Asia/Almaty"))[0]
    await tg.press("ob:save")
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        assert u.profile.birthday is None and u.profile.allergies == [] and u.timezone == "Asia/Almaty"


async def test_cancel_and_fallback(tg, sessionmaker):
    await tg.send("/start")
    assert "прервана" in tg.texts(await tg.send("/cancel"))[0]
    assert "/start" in tg.texts(await tg.send("привет"))[0]  # без анкеты — назад к /start
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        assert u.profile is None


async def test_forged_callback_rejected(tg):
    await tg.send("/start")
    await tg.press("ob:skip")
    await tg.press("ob:skip")
    calls = await tg.press(DietCb(code="hack").pack())  # подделанная кнопка
    assert any(getattr(c, "show_alert", False) for c in calls)


async def test_stale_button_answered(tg):
    from aiogram.methods import AnswerCallbackQuery
    await tg.send("/start")
    calls = await tg.press("ob:save")  # «Сохранить» не на шаге подтверждения
    assert any(isinstance(c, AnswerCallbackQuery) for c in calls)
