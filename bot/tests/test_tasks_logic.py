"""Валидация задач от LLM и арифметика времени."""
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

import pytest

from aura_bot.tasks import TaskError, format_due, next_due, validate_task

NOW = datetime(2026, 10, 12, 9, 0, tzinfo=timezone.utc)  # 12:00 в Москве, понедельник


def test_local_time_converted_to_utc():
    due, text, rep = validate_task({"datetime": "2026-10-13T15:00", "text": " Позвонить маме "}, "Europe/Moscow", NOW)
    assert due == datetime(2026, 10, 13, 12, 0, tzinfo=timezone.utc) and text == "Позвонить маме" and rep == "none"


def test_explicit_offset_respected():
    due, *_ = validate_task({"datetime": "2026-10-13T15:00+00:00", "text": "x"}, "Europe/Moscow", NOW)
    assert due.hour == 15


@pytest.mark.parametrize("raw,msg", [
    ({"datetime": "2026-10-12T11:00", "text": "x"}, "прошло"),        # 11:00 МСК уже было
    ({"datetime": "2029-01-01T10:00", "text": "x"}, "2 года"),
    ({"datetime": "завтра в обед", "text": "x"}, "время"),
    ({"datetime": "2026-10-13T15:00", "text": ""}, "не понял"),
    ({"datetime": "2026-10-13T15:00", "text": "x", "repeat": "hourly"}, "повтор"),
    ("строка", "не понял"),
    ({"text": "без времени"}, "не понял"),
])
def test_rejected(raw, msg):
    with pytest.raises(TaskError, match=msg):
        validate_task(raw, "Europe/Moscow", NOW)


def test_next_due_keeps_wall_clock_across_dst():
    tz = "Europe/Berlin"  # 25.10.2026 — переход на зимнее время
    due = datetime(2026, 10, 24, 7, 0, tzinfo=timezone.utc)  # 09:00 по Берлину (UTC+2)
    nxt = next_due(due, "daily", tz, due)
    assert nxt.astimezone(ZoneInfo(tz)).hour == 9 and nxt == datetime(2026, 10, 25, 8, 0, tzinfo=timezone.utc)
    # пропущенные из-за простоя срабатывания не догоняем пачкой
    late = next_due(due, "weekly", "Europe/Moscow", datetime(2026, 11, 20, tzinfo=timezone.utc))
    assert late > datetime(2026, 11, 20, tzinfo=timezone.utc) and late.weekday() == due.weekday()


def test_format_due():
    tz = "Europe/Moscow"
    assert format_due(datetime(2026, 10, 12, 12, 0, tzinfo=timezone.utc), tz, NOW) == "сегодня в 15:00"
    assert format_due(datetime(2026, 10, 13, 6, 30, tzinfo=timezone.utc), tz, NOW) == "завтра в 09:30"
    assert format_due(datetime(2026, 10, 16, 12, 0, tzinfo=timezone.utc), tz, NOW) == "пт 16.10 в 15:00"
    assert format_due(datetime(2027, 1, 5, 7, 0, tzinfo=timezone.utc), tz, NOW) == "05.01.2027 в 10:00"
