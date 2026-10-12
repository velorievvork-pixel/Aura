"""/memory — что агент знает; /forget — удалить запись кнопкой или стереть всё с подтверждением."""
from __future__ import annotations

from aiogram import F, Router
from aiogram.filters import Command, CommandObject
from aiogram.filters.callback_data import CallbackData
from aiogram.types import CallbackQuery, InlineKeyboardButton, InlineKeyboardMarkup, Message
from sqlalchemy.ext.asyncio import AsyncSession

from ..memory import memories_listing
from ..models import Memory, User
from ..repo import delete_memory, find_memories, forget_all, list_memories

LIST_LIMIT = 20


class MemDel(CallbackData, prefix="mdel"):
    id: int


def _btn(text: str, data: str) -> InlineKeyboardButton:
    return InlineKeyboardButton(text=text, callback_data=data)


def forget_kb(memories: list[Memory]) -> InlineKeyboardMarkup:
    nums = [_btn(f"🗑 {i}", MemDel(id=m.id).pack()) for i, m in enumerate(memories, 1)]
    rows = [nums[i:i + 5] for i in range(0, len(nums), 5)]
    rows.append([_btn("Стереть всё…", "mem:wipe")])
    return InlineKeyboardMarkup(inline_keyboard=rows)


WIPE_CONFIRM_KB = InlineKeyboardMarkup(inline_keyboard=[[
    _btn("Да, стереть всё", "mem:wipe_yes"), _btn("Отмена", "mem:wipe_no"),
]])


async def cmd_memory(message: Message, session: AsyncSession, user: User) -> None:
    memories = await list_memories(session, user)
    text = memories_listing(memories)
    if len(text) > 3800:  # длинную память показываем хвостом — самое свежее
        text = "…\n" + text[-3800:]
    await message.answer(f"🧠 Что я о вас знаю:\n\n{text}\n\nУдалить: /forget или /forget <слово>")


async def cmd_forget(message: Message, command: CommandObject, session: AsyncSession, user: User) -> None:
    needle = (command.args or "").strip()
    memories = (await find_memories(session, user, needle, LIST_LIMIT) if needle
                else await list_memories(session, user, LIST_LIMIT))
    if not memories:
        await message.answer("Ничего не нашлось." if needle else "Память пуста.")
        return
    lines = [f"{i}. {m.text}" for i, m in enumerate(memories, 1)]
    head = f"Записи с «{needle}»:" if needle else f"Последние {len(memories)} записей:"
    await message.answer(head + "\n" + "\n".join(lines) + "\n\nНажмите номер, чтобы забыть.",
                         reply_markup=forget_kb(memories))


async def cb_delete(cb: CallbackQuery, callback_data: MemDel, session: AsyncSession, user: User) -> None:
    m = await delete_memory(session, user, callback_data.id)
    if m is None:
        await cb.answer("Уже удалено", show_alert=False)
        return
    await cb.answer(f"Забыто: {m.text[:150]}")
    # убираем нажатую кнопку, остальные остаются
    kb = cb.message.reply_markup
    if kb:
        rows = [[b for b in row if b.callback_data != MemDel(id=m.id).pack()] for row in kb.inline_keyboard]
        await cb.message.edit_reply_markup(reply_markup=InlineKeyboardMarkup(inline_keyboard=[r for r in rows if r]))


async def cb_wipe(cb: CallbackQuery) -> None:
    await cb.answer()
    await cb.message.answer("⚠️ Стереть ВСЮ память и историю переписки? Анкета останется. Отменить нельзя.",
                            reply_markup=WIPE_CONFIRM_KB)


async def cb_wipe_yes(cb: CallbackQuery, session: AsyncSession, user: User) -> None:
    n = await forget_all(session, user)
    await cb.answer("Готово")
    await cb.message.edit_text(f"Стёрто записей памяти: {n}. История переписки очищена.")


async def cb_wipe_no(cb: CallbackQuery) -> None:
    await cb.answer("Отменено")
    await cb.message.edit_text("Ничего не удалено.")


def make_router() -> Router:
    r = Router(name="memory")
    r.message.register(cmd_memory, Command("memory"))
    r.message.register(cmd_forget, Command("forget"))
    r.callback_query.register(cb_delete, MemDel.filter())
    r.callback_query.register(cb_wipe, F.data == "mem:wipe")
    r.callback_query.register(cb_wipe_yes, F.data == "mem:wipe_yes")
    r.callback_query.register(cb_wipe_no, F.data == "mem:wipe_no")
    return r
