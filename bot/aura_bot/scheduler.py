"""Фоновые задачи (APScheduler): рассылка наступивших напоминаний и проверка оплат TON.
Источник истины — БД: после рестарта просроченные напоминания всё равно уйдут."""
from __future__ import annotations

import logging
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

from apscheduler.schedulers.asyncio import AsyncIOScheduler
from sqlalchemy import select

from .billing import settle_ton
from .models import Task, User
from .notifier import Note
from .services import Services
from .tasks import REPEAT_TITLES, format_due, next_due

log = logging.getLogger(__name__)
LATE = timedelta(minutes=10)


async def dispatch_due(services: Services, now: datetime | None = None) -> int:
    """Шлёт все наступившие напоминания; повторяющиеся переносит на следующий срок."""
    now = now or datetime.now(timezone.utc)
    sent = 0
    async with services.sessionmaker() as s:
        due = (await s.scalars(select(Task).where(Task.status == "pending", Task.due_at <= now)
                               .order_by(Task.due_at).limit(200))).all()
        for t in due:
            user = await s.get(User, t.user_id)
            late = f" (с опозданием: было {format_due(t.due_at, user.timezone, now)})" if now - t.due_at > LATE else ""
            if t.repeat != "none":
                t.due_at = next_due(t.due_at, t.repeat, user.timezone, now)
            else:
                t.status, t.sent_at = "sent", now
            await s.commit()  # сначала фиксируем, потом шлём: лучше не повторить, чем заспамить
            buttons = [("✅ Готово", f"task:done:{t.id}"), ("⏰ +1 час", f"task:snooze:{t.id}")]
            await services.notifier.notify(t.user_id, Note(
                kind="task.due", title="⏰ Напоминание", body=f"{t.text}{late}{REPEAT_TITLES[t.repeat]}",
                data={"task_id": t.id}, buttons=buttons if t.repeat == "none" else []))
            sent += 1
    return sent


async def check_ton(services: Services, now: datetime | None = None) -> int:
    if services.ton is None:
        return 0
    now = now or datetime.now(timezone.utc)
    try:
        txs = await services.ton.incoming()
    except Exception as e:  # сеть/лимиты toncenter — попробуем в следующий раз
        log.warning("toncenter: %s", e)
        return 0
    async with services.sessionmaker() as s:
        paid = await settle_ton(s, services.settings, txs, now)
        for inv in paid:
            user = await s.get(User, inv.user_id)
            await services.notifier.notify(inv.user_id, Note(
                kind="premium.paid", title="💎 Премиум активирован",
                body=f"Оплата TON получена. Премиум до {user.premium_until.astimezone(ZoneInfo(user.timezone)):%d.%m.%Y}."))
    return len(paid)


def start_scheduler(services: Services) -> AsyncIOScheduler:
    sch = AsyncIOScheduler(timezone="UTC")
    opts = {"max_instances": 1, "coalesce": True, "misfire_grace_time": 60}
    sch.add_job(dispatch_due, "interval", seconds=20, args=[services], id="tasks", **opts)
    if services.ton is not None:
        sch.add_job(check_ton, "interval", seconds=60, args=[services], id="ton", **opts)
    sch.start()
    return sch
