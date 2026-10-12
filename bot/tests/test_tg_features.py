"""Сквозные сценарии Telegram: задачи, голос, настройки, оплата Stars."""
from datetime import timedelta

from aiogram.methods import AnswerCallbackQuery, AnswerPreCheckoutQuery, EditMessageText, SendInvoice, SendVoice
from aiogram.types import PreCheckoutQuery, SuccessfulPayment, Voice
from sqlalchemy import select

from aura_bot.agent import utc_now
from aura_bot.models import LinkCode, Task
from aura_bot.repo import create_task, get_or_create_tg_user
from conftest import FakeSpeech, TgHarness, chat_json, local_iso, onboard


async def tasks_of(sessionmaker, tg_id):
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg_id, None, "Europe/Moscow")
        return (await s.scalars(select(Task).where(Task.user_id == u.id))).all()


async def test_natural_language_task_and_cancel_button(tg, llm, sessionmaker):
    await onboard(tg)
    llm.answer = chat_json("Хорошо, напомню.", {"datetime": local_iso(1, 15), "text": "Позвонить маме"})
    calls = await tg.send("напомни завтра в 15:00 позвонить маме")
    reply = [c for c in calls if getattr(c, "text", None) and "Напомню" in c.text][0]
    assert "завтра в 15:00: Позвонить маме" in reply.text
    data = reply.reply_markup.inline_keyboard[0][0].callback_data
    assert data.startswith("task:cancel:")
    (task,) = await tasks_of(sessionmaker, tg.tg_user.id)
    assert task.status == "pending"
    await tg.press(data)
    (task,) = await tasks_of(sessionmaker, tg.tg_user.id)
    assert task.status == "cancelled"


async def test_tasks_list_done_snooze_and_wipe(tg, sessionmaker):
    await onboard(tg)
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        a = await create_task(s, u, utc_now() + timedelta(hours=2), "Хлеб")
        b = await create_task(s, u, utc_now() + timedelta(hours=3), "Йога", "weekly")
    text = tg.texts(await tg.send("/tasks"))[0]
    assert "Хлеб" in text and "(каждую неделю) — Йога" in text
    calls = await tg.press(f"task:done:{a.id}")
    assert any(isinstance(c, EditMessageText) and "Хлеб" not in c.text for c in calls)  # список обновился
    await tg.press(f"task:snooze:{b.id}")
    tasks = {t.id: t for t in await tasks_of(sessionmaker, tg.tg_user.id)}
    assert tasks[a.id].status == "done" and tasks[b.id].due_at < utc_now() + timedelta(hours=1, minutes=1)
    await tg.send("/tasks")
    assert "ВСЕ" in tg.texts(await tg.press("task:wipe"))[0]
    await tg.press("task:wipe_yes")
    assert {t.status for t in await tasks_of(sessionmaker, tg.tg_user.id)} == {"done", "cancelled"}


async def test_foreign_task_button_ignored(tg, sessionmaker):
    await onboard(tg)
    async with sessionmaker() as s:
        other = await get_or_create_tg_user(s, 777, None, "Europe/Moscow")
        t = await create_task(s, other, utc_now() + timedelta(hours=2), "Чужое")
    calls = await tg.press(f"task:cancel:{t.id}")
    assert any(isinstance(c, AnswerCallbackQuery) and "закрыта" in c.text for c in calls)
    async with sessionmaker() as s:
        assert (await s.get(Task, t.id)).status == "pending"


def voice_msg(tg, duration=5):
    return tg._msg(voice=Voice(file_id="f1", file_unique_id="u1", duration=duration, mime_type="audio/ogg"))


async def test_voice_transcribed_answered_and_spoken(sessionmaker, llm, settings):
    speech = FakeSpeech(text="Что подарить жене?")
    tg = TgHarness(sessionmaker, llm=llm, settings=settings, speech=speech)
    await onboard(tg)
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        u.voice_replies = True
        await s.commit()
    llm.answer = chat_json("Цветы и книгу.")
    calls = await tg.send_message(voice_msg(tg))
    texts = tg.texts(calls)
    assert texts[0] == "🎙 «Что подарить жене?»" and texts[1] == "Цветы и книгу."
    assert speech.heard == [b"OggS-fake-voice"]                      # файл скачан из Telegram
    assert any(isinstance(c, SendVoice) for c in calls) and speech.spoken == ["Цветы и книгу."]
    assert llm.chat_calls()[-1][0][-1]["content"] == "Что подарить жене?"


async def test_voice_limits_and_failures(sessionmaker, llm, settings):
    speech = FakeSpeech()
    tg = TgHarness(sessionmaker, llm=llm, settings=settings, speech=speech)
    await onboard(tg)
    assert "Слишком длинное" in tg.texts(await tg.send_message(voice_msg(tg, duration=999)))[0]
    speech.fail = True
    assert "Не разобрал" in tg.texts(await tg.send_message(voice_msg(tg)))[0]
    no_stt = TgHarness(sessionmaker, user_id=43, llm=llm, settings=settings)
    await onboard(no_stt)
    assert "не настроено" in no_stt.texts(await no_stt.send_message(voice_msg(no_stt)))[0]


async def test_settings_tz_link_stats(tg, sessionmaker):
    await onboard(tg)
    assert "Настройки" in tg.texts(await tg.send("/settings"))[0]
    calls = await tg.press("set:voice")
    assert any(isinstance(c, EditMessageText) and "Голосовые ответы на голосовые: вкл" in c.text for c in calls)
    assert "Asia/Almaty" in tg.texts(await tg.send("/tz Asia/Almaty"))[0]
    assert "IANA" in tg.texts(await tg.send("/tz MSK"))[0]
    code_text = tg.texts(await tg.send("/link"))[0]
    async with sessionmaker() as s:
        code = (await s.scalars(select(LinkCode))).one().code
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        assert u.timezone == "Asia/Almaty" and u.voice_replies
    assert code in code_text
    assert "Сообщений сегодня: 0 из 20" in tg.texts(await tg.send("/stats"))[0]


async def test_limit_offers_premium_and_stars_payment(sessionmaker, llm, settings):
    from dataclasses import replace
    tg = TgHarness(sessionmaker, llm=llm, settings=replace(settings, free_messages_per_day=1))
    await onboard(tg)
    await tg.send("привет")
    calls = await tg.send("ещё")
    assert "лимит исчерпан" in tg.texts(calls)[0] and len(llm.chat_calls()) == 1
    await tg.press("prem:open")
    invoice = [c for c in await tg.press("prem:stars") if isinstance(c, SendInvoice)][0]
    assert invoice.currency == "XTR" and invoice.prices[0].amount == 250

    async def pre_checkout(amount):
        q = PreCheckoutQuery(id="pc1", from_user=tg.tg_user, currency="XTR", total_amount=amount,
                             invoice_payload=invoice.payload)
        calls = await tg._feed(pre_checkout_query=q)
        return [c for c in calls if isinstance(c, AnswerPreCheckoutQuery)][0]

    assert (await pre_checkout(1)).ok is False                      # подменённая сумма
    assert (await pre_checkout(250)).ok is True

    def paid_msg():
        return tg._msg(successful_payment=SuccessfulPayment(
            currency="XTR", total_amount=250, invoice_payload=invoice.payload,
            telegram_payment_charge_id="ch_1", provider_payment_charge_id=""))

    assert "Премиум активен" in tg.texts(await tg.send_message(paid_msg()))[0]
    assert tg.texts(await tg.send_message(paid_msg())) == []          # повтор не продлевает дважды
    assert tg.texts(await tg.send("снова"))[0] == "Ответ Aura"       # лимит снят
    assert "/paysupport" in tg.texts(await tg.send("/premium"))[0]
    assert "Поддержка" in tg.texts(await tg.send("/paysupport"))[0]
