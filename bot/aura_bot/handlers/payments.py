"""Премиум: Telegram Stars (XTR, без комиссий сторов) и TON; /paysupport обязателен для цифровых товаров."""
from __future__ import annotations

from zoneinfo import ZoneInfo

from aiogram import F, Router
from aiogram.filters import Command, CommandStart
from aiogram.types import (
    CallbackQuery, InlineKeyboardButton, InlineKeyboardMarkup, LabeledPrice, Message, PreCheckoutQuery,
)
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from ..agent import utc_now
from ..billing import (
    check_stars_payment, create_ton_invoice, grant_premium, is_premium, stars_payload, ton_link,
)
from ..models import TonInvoice, User
from ..scheduler import check_ton
from ..services import Services


def _btn(text: str, data: str) -> InlineKeyboardButton:
    return InlineKeyboardButton(text=text, callback_data=data)


def premium_view(user: User, services: Services) -> tuple[str, InlineKeyboardMarkup]:
    st = services.settings
    status = (f"✅ активен до {user.premium_until.astimezone(ZoneInfo(user.timezone)).strftime('%d.%m.%Y')}"
              if is_premium(user, utc_now()) else "не активен")
    price = f"{st.premium_price_stars} ⭐" + (f" или {st.premium_price_ton} TON" if st.ton_enabled else "")
    text = (f"💎 Премиум Aura — {st.premium_price_rub} ₽/мес ({price}) за {st.premium_days} дней.\n"
            f"До {st.premium_messages_per_day} сообщений в день вместо {st.free_messages_per_day}.\n\n"
            f"Статус: {status}\nПродление суммируется с остатком. Вопросы по оплате — /paysupport.")
    rows = [[_btn(f"⭐ Оплатить {st.premium_price_stars} Stars", "prem:stars")]]
    if st.ton_enabled:
        rows.append([_btn(f"💎 Оплатить {st.premium_price_ton} TON", "prem:ton")])
    return text, InlineKeyboardMarkup(inline_keyboard=rows)


async def cmd_premium(message: Message, user: User, services: Services) -> None:
    text, kb = premium_view(user, services)
    await message.answer(text, reply_markup=kb)


async def cb_open(cb: CallbackQuery, user: User, services: Services) -> None:
    await cb.answer()
    text, kb = premium_view(user, services)
    await cb.message.answer(text, reply_markup=kb)


async def cb_stars(cb: CallbackQuery, user: User, services: Services) -> None:
    st = services.settings
    await cb.answer()
    await cb.message.answer_invoice(
        title="Aura Премиум", description=f"Премиум на {st.premium_days} дней: больше сообщений каждый день.",
        payload=stars_payload(user, st), currency="XTR", provider_token="",
        prices=[LabeledPrice(label=f"Премиум {st.premium_days} дн.", amount=st.premium_price_stars)],
    )


async def on_pre_checkout(q: PreCheckoutQuery, user: User, services: Services) -> None:
    """Последняя проверка перед списанием звёзд: счёт должен быть выписан этому пользователю и по текущей цене."""
    problem = check_stars_payment(q.invoice_payload, q.currency, q.total_amount, user, services.settings)
    await q.answer(ok=problem is None, error_message=problem)


async def on_paid(message: Message, session: AsyncSession, user: User, services: Services) -> None:
    p = message.successful_payment
    if check_stars_payment(p.invoice_payload, p.currency, p.total_amount, user, services.settings):
        await message.answer("Платёж получен, но счёт не совпал. Напишите в /paysupport — разберёмся.")
        return
    granted = await grant_premium(session, user, provider="stars", amount=p.total_amount,
                                  external_id=f"stars:{p.telegram_payment_charge_id}",
                                  days=services.settings.premium_days, now=utc_now())
    if granted:
        await message.answer(f"💎 Спасибо! Премиум активен до {user.premium_until.astimezone(ZoneInfo(user.timezone)).strftime('%d.%m.%Y')}.")


async def cb_ton(cb: CallbackQuery, session: AsyncSession, user: User, services: Services) -> None:
    st = services.settings
    if not st.ton_enabled:
        await cb.answer("Оплата TON не настроена", show_alert=True)
        return
    inv = await create_ton_invoice(session, user, st, utc_now())
    await cb.answer()
    await cb.message.answer(
        f"💎 Переведите {st.premium_price_ton} TON на адрес\n{st.ton_wallet}\nс комментарием {inv.comment}\n\n"
        "Без комментария платёж не найти! Счёт действует 1 час, зачисление — 1–2 минуты после перевода.",
        reply_markup=InlineKeyboardMarkup(inline_keyboard=[
            [InlineKeyboardButton(text="Открыть кошелёк", url=ton_link(st, inv))],
            [_btn("Проверить оплату", f"prem:tonchk:{inv.id}")]]))


async def cb_ton_check(cb: CallbackQuery, session: AsyncSession, user: User, services: Services) -> None:
    inv_id = int(cb.data.rsplit(":", 1)[1])
    await check_ton(services)
    inv = await session.scalar(select(TonInvoice).where(TonInvoice.id == inv_id, TonInvoice.user_id == user.id)
                               .execution_options(populate_existing=True))
    if inv and inv.status == "paid":
        await cb.answer("Оплата получена — премиум активен!", show_alert=True)
    else:
        await cb.answer("Пока не вижу перевода. Подождите минуту и проверьте снова.", show_alert=True)


async def cmd_paysupport(message: Message, services: Services) -> None:
    contact = services.settings.support_contact or "ответьте на это сообщение — администратор свяжется с вами"
    await message.answer(f"🛟 Поддержка по оплате: {contact}.\nУкажите дату платежа и способ (Stars/TON).")


def make_router() -> Router:
    r = Router(name="payments")
    r.message.register(cmd_premium, CommandStart(deep_link=True, magic=F.args == "premium"))
    r.message.register(cmd_premium, Command("premium"))
    r.message.register(cmd_paysupport, Command("paysupport"))
    r.message.register(on_paid, F.successful_payment)
    r.pre_checkout_query.register(on_pre_checkout)
    r.callback_query.register(cb_open, F.data == "prem:open")
    r.callback_query.register(cb_stars, F.data == "prem:stars")
    r.callback_query.register(cb_ton, F.data == "prem:ton")
    r.callback_query.register(cb_ton_check, F.data.regexp(r"^prem:tonchk:\d+$"))
    return r
