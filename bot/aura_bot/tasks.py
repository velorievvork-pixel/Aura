"""Задачи: строгая валидация {datetime, text, repeat} от LLM/клиента и арифметика времени."""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

from pydantic import BaseModel, ConfigDict, Field, ValidationError as PydanticError

from .models import TASK_REPEATS

MAX_AHEAD = timedelta(days=366 * 2)
MIN_AHEAD = timedelta(seconds=30)
WEEKDAYS = ["пн", "вт", "ср", "чт", "пт", "сб", "вс"]


class TaskError(ValueError):
    """Задача отклонена сервером; текст — для пользователя."""


class TaskDraft(BaseModel):
    model_config = ConfigDict(extra="ignore", str_strip_whitespace=True)
    datetime: str = Field(min_length=10, max_length=40)
    text: str = Field(min_length=1, max_length=500)
    repeat: str = "none"


def validate_task(raw: object, tz: str, now: datetime) -> tuple[datetime, str, str]:
    """-> (due_at в UTC, text, repeat). LLM — не источник истины: проверяем всё."""
    try:
        d = TaskDraft.model_validate(raw)
    except PydanticError:
        raise TaskError("не понял, что и когда напомнить") from None
    if d.repeat not in TASK_REPEATS:
        raise TaskError("повтор бывает только ежедневный или еженедельный")
    try:
        local = datetime.fromisoformat(d.datetime.replace("Z", "+00:00"))
    except ValueError:
        raise TaskError("не понял время") from None
    if local.tzinfo is None:
        local = local.replace(tzinfo=ZoneInfo(tz))  # время от LLM — в поясе пользователя
    due = local.astimezone(timezone.utc).replace(second=0, microsecond=0)
    if due < now + MIN_AHEAD:
        raise TaskError("это время уже прошло")
    if due > now + MAX_AHEAD:
        raise TaskError("можно планировать не дальше чем на 2 года")
    return due, d.text, d.repeat


def next_due(due: datetime, repeat: str, tz: str, now: datetime) -> datetime:
    """Следующее срабатывание повторяющейся задачи (то же «настенное» время в поясе пользователя)."""
    step = {"daily": timedelta(days=1), "weekly": timedelta(days=7)}[repeat]
    local = due.astimezone(ZoneInfo(tz)).replace(tzinfo=None)
    while True:
        local += step
        nxt = local.replace(tzinfo=ZoneInfo(tz)).astimezone(timezone.utc)
        if nxt > now:
            return nxt


def format_due(due: datetime, tz: str, now: datetime) -> str:
    """«сегодня в 15:00», «завтра в 09:30», «пт 17.10 в 15:00», «12.01.2027 в 10:00»."""
    z = ZoneInfo(tz)
    local, today = due.astimezone(z), now.astimezone(z).date()
    t = f"{local:%H:%M}"
    if local.date() == today:
        return f"сегодня в {t}"
    if local.date() == today + timedelta(days=1):
        return f"завтра в {t}"
    if local.year == today.year:
        return f"{WEEKDAYS[local.weekday()]} {local:%d.%m} в {t}"
    return f"{local:%d.%m.%Y} в {t}"


REPEAT_TITLES = {"none": "", "daily": " (каждый день)", "weekly": " (каждую неделю)"}
