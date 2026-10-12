"""/settings, /tz, /link, /stats — настройки, привязка APK и статистика."""
from __future__ import annotations

from zoneinfo import ZoneInfo

from aiogram import F, Router
from aiogram.filters import Command, CommandObject
from aiogram.filters.callback_data import CallbackData
from aiogram.types import CallbackQuery, InlineKeyboardButton, InlineKeyboardMarkup, Message
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from ..accounts import LINK_TTL, new_link_code
from ..agent import utc_now
from ..billing import is_premium, quota
from ..models import ChatMessage, Memory, Task, User
from ..onboarding import TIMEZONES, ValidationError, parse_timezone
from ..services import Services


class SetTz(CallbackData, prefix="stz"):
    name: str


def _btn(text: str, data: str) -> InlineKeyboardButton:
    return InlineKeyboardButton(text=text, callback_data=data)


def settings_view(user: User) -> tuple[str, InlineKeyboardMarkup]:
    now = utc_now()
    prem = (f"до {user.premium_until.astimezone(ZoneInfo(user.timezone)).strftime('%d.%m.%Y')}" if is_premium(user, now) else "нет")
    text = (f"⚙️ Настройки\n\n🕒 Часовой пояс: {TIMEZONES.get(user.timezone, user.timezone)}\n"
            f"🔊 Голосовые ответы на голосовые: {'вкл' if user.voice_replies else 'выкл'}\n💎 Премиум: {prem}")
    kb = InlineKeyboardMarkup(inline_keyboard=[
        [_btn("🕒 Сменить пояс", "set:tz")],
        [_btn(f"🔊 Голосовые ответы: {'выключить' if user.voice_replies else 'включить'}", "set:voice")],
        [_btn("📝 Пройти анкету заново", "ob:restart")],
        [_btn("🔗 Привязать приложение Android", "set:link")],
        [_btn("💎 Премиум", "prem:open")],
    ])
    return text, kb


async def cmd_settings(message: Message, user: User) -> None:
    text, kb = settings_view(user)
    await message.answer(text, reply_markup=kb)


async def cb_voice(cb: CallbackQuery, session: AsyncSession, user: User) -> None:
    user.voice_replies = not user.voice_replies
    await session.commit()
    await cb.answer("Готово")
    text, kb = settings_view(user)
    await cb.message.edit_text(text, reply_markup=kb)


async def cb_tz_menu(cb: CallbackQuery) -> None:
    await cb.answer()
    rows = [[_btn(label, SetTz(name=name).pack())] for name, label in TIMEZONES.items()]
    await cb.message.answer("Выберите пояс или отправьте /tz Asia/Almaty:",
                            reply_markup=InlineKeyboardMarkup(inline_keyboard=rows))


async def cb_tz_set(cb: CallbackQuery, callback_data: SetTz, session: AsyncSession, user: User) -> None:
    if callback_data.name not in TIMEZONES:
        await cb.answer("Неизвестный пояс", show_alert=True)
        return
    user.timezone = callback_data.name
    await session.commit()
    await cb.answer("Пояс обновлён")
    await cb.message.edit_text(f"🕒 Часовой пояс: {TIMEZONES[user.timezone]}")


async def cmd_tz(message: Message, command: CommandObject, session: AsyncSession, user: User) -> None:
    try:
        user.timezone = parse_timezone(command.args or "")
    except ValidationError as e:
        await message.answer(f"{e}")
        return
    await session.commit()
    await message.answer(f"🕒 Часовой пояс: {user.timezone}")


async def send_link_code(message: Message, session: AsyncSession, user: User) -> None:
    code = await new_link_code(session, user, utc_now())
    await message.answer(
        f"🔗 Код привязки: {code}\nВведите его в приложении Aura: Настройки → «Привязать Telegram». "
        f"Действует {int(LINK_TTL.total_seconds() // 60)} минут. После привязки память и задачи станут общими.")


async def cmd_link(message: Message, session: AsyncSession, user: User) -> None:
    await send_link_code(message, session, user)


async def cb_link(cb: CallbackQuery, session: AsyncSession, user: User) -> None:
    await cb.answer()
    await send_link_code(cb.message, session, user)


async def cmd_stats(message: Message, session: AsyncSession, user: User, services: Services) -> None:
    now = utc_now()
    q = await quota(session, user, services.settings, now)
    count = lambda model, *cond: session.scalar(select(func.count()).select_from(model).where(model.user_id == user.id, *cond))  # noqa: E731
    msgs = await count(ChatMessage, ChatMessage.role == "user")
    mems = await count(Memory)
    active = await count(Task, Task.status.in_(("pending", "sent")))
    done = await count(Task, Task.status == "done")
    prem = f"до {user.premium_until.astimezone(ZoneInfo(user.timezone)).strftime('%d.%m.%Y')}" if q.premium else "нет"
    await message.answer(
        f"📊 Статистика\n\nСообщений сегодня: {q.used} из {q.limit}\nВсего сообщений: {msgs}\n"
        f"Записей памяти: {mems}\nАктивных задач: {active}, выполнено: {done}\nПремиум: {prem}")


def make_router() -> Router:
    r = Router(name="settings")
    r.message.register(cmd_settings, Command("settings"))
    r.message.register(cmd_tz, Command("tz"))
    r.message.register(cmd_link, Command("link"))
    r.message.register(cmd_stats, Command("stats"))
    r.callback_query.register(cb_voice, F.data == "set:voice")
    r.callback_query.register(cb_tz_menu, F.data == "set:tz")
    r.callback_query.register(cb_tz_set, SetTz.filter())
    r.callback_query.register(cb_link, F.data == "set:link")
    return r
