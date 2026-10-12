"""/tasks и кнопки напоминаний: готово, +1 час, отменить; «удалить все» — только с подтверждением."""
from __future__ import annotations

from datetime import timedelta

from aiogram import F, Router
from aiogram.filters import Command
from aiogram.types import CallbackQuery, InlineKeyboardButton, InlineKeyboardMarkup, Message
from sqlalchemy import update
from sqlalchemy.ext.asyncio import AsyncSession

from ..agent import utc_now
from ..models import Task, User
from ..repo import get_own_task, list_tasks
from ..tasks import REPEAT_TITLES, format_due

LIST_LIMIT = 20


def _btn(text: str, data: str) -> InlineKeyboardButton:
    return InlineKeyboardButton(text=text, callback_data=data)


def render_tasks(tasks: list[Task], tz: str) -> tuple[str, InlineKeyboardMarkup | None]:
    if not tasks:
        return "Задач нет. Скажите, например: «напомни завтра в 15:00 позвонить маме».", None
    now = utc_now()
    lines, rows = [], []
    for i, t in enumerate(tasks, 1):
        mark = "🔔" if t.status == "sent" else "⏳"
        lines.append(f"{i}. {mark} {format_due(t.due_at, tz, now)}{REPEAT_TITLES[t.repeat]} — {t.text}")
        row = [_btn(f"✅ {i}", f"task:done:{t.id}")] if t.repeat == "none" else []
        rows.append(row + [_btn(f"🗑 {i}", f"task:cancel:{t.id}")])
    rows.append([_btn("Удалить все…", "task:wipe")])
    return "📋 Ваши задачи:\n" + "\n".join(lines), InlineKeyboardMarkup(inline_keyboard=rows)


async def cmd_tasks(message: Message, session: AsyncSession, user: User) -> None:
    text, kb = render_tasks(await list_tasks(session, user, LIST_LIMIT), user.timezone)
    await message.answer(text, reply_markup=kb)


async def cb_task(cb: CallbackQuery, session: AsyncSession, user: User) -> None:
    _, action, raw_id = cb.data.split(":")
    task = await get_own_task(session, user, int(raw_id)) if raw_id.isdigit() else None
    if task is None or task.status in ("done", "cancelled"):
        await cb.answer("Задача уже закрыта")
        return
    now = utc_now()
    if action == "done":
        task.status, msg = "done", "Отмечено как выполненное ✅"
    elif action == "snooze":
        task.status, task.due_at = "pending", now + timedelta(hours=1)
        msg = f"Напомню {format_due(task.due_at, user.timezone, now)}"
    elif action == "cancel":
        task.status, msg = "cancelled", "Напоминание отменено"
    else:
        await cb.answer()
        return
    await session.commit()
    await cb.answer(msg)
    if cb.message and cb.message.text and cb.message.text.startswith("📋"):
        text, kb = render_tasks(await list_tasks(session, user, LIST_LIMIT), user.timezone)
        await cb.message.edit_text(text, reply_markup=kb)
    elif cb.message:
        await cb.message.edit_reply_markup(reply_markup=None)


async def cb_wipe(cb: CallbackQuery) -> None:
    await cb.answer()
    await cb.message.answer("⚠️ Удалить ВСЕ активные задачи и напоминания?", reply_markup=InlineKeyboardMarkup(
        inline_keyboard=[[_btn("Да, удалить все", "task:wipe_yes"), _btn("Отмена", "task:wipe_no")]]))


async def cb_wipe_yes(cb: CallbackQuery, session: AsyncSession, user: User) -> None:
    res = await session.execute(update(Task).where(Task.user_id == user.id, Task.status.in_(("pending", "sent")))
                                .values(status="cancelled"))
    await session.commit()
    await cb.answer("Готово")
    await cb.message.edit_text(f"Удалено задач: {res.rowcount}.")


async def cb_wipe_no(cb: CallbackQuery) -> None:
    await cb.answer("Отменено")
    await cb.message.edit_text("Ничего не удалено.")


def make_router() -> Router:
    r = Router(name="tasks")
    r.message.register(cmd_tasks, Command("tasks"))
    r.callback_query.register(cb_wipe, F.data == "task:wipe")
    r.callback_query.register(cb_wipe_yes, F.data == "task:wipe_yes")
    r.callback_query.register(cb_wipe_no, F.data == "task:wipe_no")
    r.callback_query.register(cb_task, F.data.regexp(r"^task:(done|snooze|cancel):\d+$"))
    return r
