"""/start-анкета: день рождения → аллергии → диеты → привычки → часовой пояс → подтверждение."""
from __future__ import annotations

from datetime import date

from aiogram import F, Router
from aiogram.filters import Command, CommandStart, StateFilter
from aiogram.filters.callback_data import CallbackData
from aiogram.fsm.context import FSMContext
from aiogram.fsm.state import State, StatesGroup
from aiogram.types import CallbackQuery, InlineKeyboardButton, InlineKeyboardMarkup, Message
from sqlalchemy.ext.asyncio import AsyncSession

from ..models import User
from ..onboarding import (
    DIETS, TIMEZONES, ValidationError, parse_birthday, parse_list, parse_timezone, profile_summary,
)
from ..repo import is_onboarded, save_profile

class Ob(StatesGroup):
    birthday = State()
    allergies = State()
    diets = State()
    habits = State()
    timezone = State()
    timezone_custom = State()
    confirm = State()


class DietCb(CallbackData, prefix="diet"):
    code: str


class TzCb(CallbackData, prefix="tz"):
    name: str


def _btn(text: str, data: str) -> InlineKeyboardButton:
    return InlineKeyboardButton(text=text, callback_data=data)


SKIP_KB = InlineKeyboardMarkup(inline_keyboard=[[_btn("Пропустить", "ob:skip")]])


def diets_kb(selected: list[str]) -> InlineKeyboardMarkup:
    rows = [
        [_btn(("✅ " if code in selected else "▫️ ") + label, DietCb(code=code).pack())]
        for code, label in DIETS.items()
    ]
    rows.append([_btn("Готово" if selected else "Без ограничений", "ob:diets_done")])
    return InlineKeyboardMarkup(inline_keyboard=rows)


def tz_kb() -> InlineKeyboardMarkup:
    rows = [[_btn(label, TzCb(name=name).pack())] for name, label in TIMEZONES.items()]
    rows.append([_btn("Другой — введу сам", "ob:tz_custom")])
    return InlineKeyboardMarkup(inline_keyboard=rows)


CONFIRM_KB = InlineKeyboardMarkup(inline_keyboard=[[_btn("💾 Сохранить", "ob:save"), _btn("↺ Заново", "ob:restart")]])
RESTART_KB = InlineKeyboardMarkup(inline_keyboard=[[_btn("Пройти анкету заново", "ob:restart")]])


def _summary(data: dict) -> str:
    bday = date.fromisoformat(data["birthday"]) if data.get("birthday") else None
    return profile_summary(bday, data.get("allergies", []), data.get("diets", []),
                           data.get("habits", []), data.get("tz", ""))


# ---------- шаги анкеты (каждый задаёт вопрос и ставит состояние) ----------

async def ask_birthday(msg: Message, state: FSMContext) -> None:
    await state.set_state(Ob.birthday)
    await msg.answer("1/5 🎂 Когда у вас день рождения? Формат ДД.ММ.ГГГГ.", reply_markup=SKIP_KB)


async def ask_allergies(msg: Message, state: FSMContext) -> None:
    await state.set_state(Ob.allergies)
    await msg.answer("2/5 ⚠️ Есть аллергии? Перечислите через запятую (например: арахис, мёд) или напишите «нет».",
                     reply_markup=SKIP_KB)


async def ask_diets(msg: Message, state: FSMContext) -> None:
    await state.set_state(Ob.diets)
    data = await state.get_data()
    await msg.answer("3/5 🥗 Придерживаетесь диеты? Отметьте всё подходящее.", reply_markup=diets_kb(data.get("diets", [])))


async def ask_habits(msg: Message, state: FSMContext) -> None:
    await state.set_state(Ob.habits)
    await msg.answer("4/5 🔁 Расскажите о привычках через запятую: режим сна, спорт, кофе — всё, что стоит учитывать.",
                     reply_markup=SKIP_KB)


async def ask_timezone(msg: Message, state: FSMContext) -> None:
    await state.set_state(Ob.timezone)
    await msg.answer("5/5 🕒 Ваш часовой пояс — нужен для напоминаний.", reply_markup=tz_kb())


async def ask_confirm(msg: Message, state: FSMContext) -> None:
    await state.set_state(Ob.confirm)
    await msg.answer("Проверьте анкету:\n\n" + _summary(await state.get_data()), reply_markup=CONFIRM_KB)


NEXT = {
    Ob.birthday.state: ask_allergies,
    Ob.allergies.state: ask_diets,
    Ob.habits.state: ask_timezone,
}


async def start_survey(msg: Message, state: FSMContext, user: User) -> None:
    await state.clear()
    await state.update_data(tz=user.timezone)  # дефолт, если шаг пояса пропущен
    await ask_birthday(msg, state)


# ---------- команды ----------

async def cmd_start(message: Message, state: FSMContext, user: User) -> None:
    if is_onboarded(user):
        p = user.profile
        await state.clear()
        await message.answer(
            "С возвращением! Ваша анкета:\n\n"
            + profile_summary(p.birthday, p.allergies, p.diets, p.habits, user.timezone),
            reply_markup=RESTART_KB,
        )
        return
    await message.answer(
        "Привет! Я Aura — ваш личный ИИ-агент. Запоминаю важное о вас, веду задачи и напоминания.\n"
        "Сначала короткая анкета из 5 вопросов — чтобы сразу советовать правильно. /cancel — прервать."
    )
    await start_survey(message, state, user)


async def cmd_cancel(message: Message, state: FSMContext) -> None:
    await state.clear()
    await message.answer("Анкета прервана, ничего не сохранено. /start — начать снова.")


async def cb_restart(cb: CallbackQuery, state: FSMContext, user: User) -> None:
    await cb.answer()
    await start_survey(cb.message, state, user)


async def cb_skip(cb: CallbackQuery, state: FSMContext) -> None:
    await cb.answer("Пропущено")
    await NEXT[await state.get_state()](cb.message, state)


# ---------- ответы на шаги ----------

async def on_birthday(message: Message, state: FSMContext) -> None:
    try:
        bday = parse_birthday(message.text)
    except ValidationError as e:
        await message.answer(f"{e}", reply_markup=SKIP_KB)
        return
    await state.update_data(birthday=bday.isoformat())
    await ask_allergies(message, state)


async def on_allergies(message: Message, state: FSMContext) -> None:
    try:
        items = parse_list(message.text)
    except ValidationError as e:
        await message.answer(f"{e}", reply_markup=SKIP_KB)
        return
    await state.update_data(allergies=items)
    await ask_diets(message, state)


async def cb_diet_toggle(cb: CallbackQuery, callback_data: DietCb, state: FSMContext) -> None:
    if callback_data.code not in DIETS:  # данные из кнопки тоже не доверяем
        await cb.answer("Неизвестная диета", show_alert=True)
        return
    selected = list((await state.get_data()).get("diets", []))
    if callback_data.code in selected:
        selected.remove(callback_data.code)
    else:
        selected.append(callback_data.code)
    await state.update_data(diets=selected)
    await cb.answer()
    await cb.message.edit_reply_markup(reply_markup=diets_kb(selected))


async def cb_diets_done(cb: CallbackQuery, state: FSMContext) -> None:
    await cb.answer()
    await cb.message.edit_reply_markup(reply_markup=None)
    await ask_habits(cb.message, state)


async def on_habits(message: Message, state: FSMContext) -> None:
    try:
        items = parse_list(message.text)
    except ValidationError as e:
        await message.answer(f"{e}", reply_markup=SKIP_KB)
        return
    await state.update_data(habits=items)
    await ask_timezone(message, state)


async def cb_tz(cb: CallbackQuery, callback_data: TzCb, state: FSMContext) -> None:
    if callback_data.name not in TIMEZONES:
        await cb.answer("Неизвестный пояс", show_alert=True)
        return
    await state.update_data(tz=callback_data.name)
    await cb.answer()
    await ask_confirm(cb.message, state)


async def cb_tz_custom(cb: CallbackQuery, state: FSMContext) -> None:
    await cb.answer()
    await state.set_state(Ob.timezone_custom)
    await cb.message.answer("Введите пояс в формате IANA, например Asia/Almaty или Europe/Berlin.")


async def on_tz_custom(message: Message, state: FSMContext) -> None:
    try:
        tz = parse_timezone(message.text)
    except ValidationError as e:
        await message.answer(f"{e}")
        return
    await state.update_data(tz=tz)
    await ask_confirm(message, state)


async def cb_save(cb: CallbackQuery, state: FSMContext, session: AsyncSession, user: User) -> None:
    data = await state.get_data()
    await save_profile(
        session, user,
        birthday=date.fromisoformat(data["birthday"]) if data.get("birthday") else None,
        allergies=data.get("allergies", []),
        diets=data.get("diets", []),
        habits=data.get("habits", []),
        tz=data.get("tz") or user.timezone,
    )
    await state.clear()
    await cb.answer("Сохранено")
    await cb.message.edit_reply_markup(reply_markup=None)
    hint = f" Например, не посоветую ничего с: {', '.join(data['allergies'])}." if data.get("allergies") else ""
    await cb.message.answer("Готово! Анкета сохранена и учитывается в каждом ответе." + hint)


async def on_unexpected(message: Message) -> None:
    """Стикер/фото/голос посреди анкеты — просим ответить текстом или кнопкой."""
    await message.answer("Ответьте, пожалуйста, текстом или кнопкой под вопросом. /cancel — прервать анкету.")


def make_router() -> Router:
    """Новый роутер на каждый диспетчер (aiogram не даёт переиспользовать один)."""
    r = Router(name="onboarding")
    r.message.register(cmd_start, CommandStart())
    r.message.register(cmd_cancel, Command("cancel"), StateFilter(Ob))
    r.callback_query.register(cb_restart, F.data == "ob:restart")
    r.callback_query.register(cb_skip, F.data == "ob:skip", StateFilter(Ob.birthday, Ob.allergies, Ob.habits))
    r.message.register(on_birthday, Ob.birthday, F.text)
    r.message.register(on_allergies, Ob.allergies, F.text)
    r.callback_query.register(cb_diet_toggle, DietCb.filter(), Ob.diets)
    r.callback_query.register(cb_diets_done, F.data == "ob:diets_done", Ob.diets)
    r.message.register(on_habits, Ob.habits, F.text)
    r.callback_query.register(cb_tz, TzCb.filter(), Ob.timezone)
    r.callback_query.register(cb_tz_custom, F.data == "ob:tz_custom", Ob.timezone)
    r.message.register(on_tz_custom, Ob.timezone_custom, F.text)
    r.callback_query.register(cb_save, F.data == "ob:save", Ob.confirm)
    r.message.register(on_unexpected, StateFilter(Ob))
    return r
