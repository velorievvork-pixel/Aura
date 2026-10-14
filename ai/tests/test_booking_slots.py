"""Слоты для страниц записи (booking.*): новые поля POST /v1/agent/slots.

per_day, min_notice_minutes, buffer_minutes, step_minutes, work_hours, weekdays;
без них поведение прежнее (см. test_invite_slots.py).
"""

from datetime import datetime, timedelta, timezone

import pytest
from fastapi.testclient import TestClient

from aura_ai import app as app_module
from aura_ai import memory as memory_module
from aura_ai import planner
from aura_ai.agent import AuraAgent
from aura_ai.config import Settings, reset_settings
from aura_ai.llm import MockLLM
from aura_ai.models import CalendarEvent, Preferences
from aura_ai.tools import ToolManager

MONDAY = datetime(2026, 9, 14, 0, 0, tzinfo=timezone.utc)  # понедельник, 03:00 по Москве
MSK = planner._zone("Europe/Moscow")
EVENING = list(range(15, 21))  # занятие целиком с 15:00 до 21:00
SIX_DAYS = [0, 1, 2, 3, 4, 5]


def _prefs(work=None, preferred=None) -> Preferences:
    return Preferences(work_hours=list(range(9, 19)) if work is None else work, preferred_hours=preferred or [])


def _book(now=MONDAY, hours=14 * 24, calendar=(), prefs=None, tz="Europe/Moscow", duration=60,
          max_slots=200, per_day=12, **extra):
    """Параметры как у страницы записи: per_day=12, max_slots=200, вечер по Москве."""
    params = {"work_hours": EVENING, "weekdays": SIX_DAYS, "min_notice_minutes": 0}
    params.update(extra)
    return planner.offer_slots(
        planner.TimeWindow(start=now, end=now + timedelta(hours=hours)),
        list(calendar),
        prefs or _prefs(),
        tz,
        duration_minutes=duration,
        max_slots=max_slots,
        per_day=per_day,
        now=now,
        **params,
    )


def _msk(day: int, hour: int, minute: int = 0) -> datetime:
    """Местное московское время сентября 2026 → aware UTC."""
    return datetime(2026, 9, day, hour, minute, tzinfo=MSK).astimezone(timezone.utc)


def _local(slot) -> datetime:
    return slot.start.astimezone(MSK)


def _by_day(slots):
    days = {}
    for slot in slots:
        days.setdefault(_local(slot).date(), []).append(slot)
    return days


# ---------------------------------------------------- страница записи: все варианты
def test_evening_moscow_all_slots_chronological():
    slots = _book()
    # пн 14 … вс 27 сентября: 12 дней без воскресений, в каждом 15:00 … 20:00 через 30 минут
    days = _by_day(slots)
    assert len(days) == 12
    assert all(len(day) == 11 for day in days.values())
    assert len(slots) == 132
    starts = [s.start for s in slots]
    assert starts == sorted(starts)
    assert len(set(starts)) == len(starts)
    for slot in slots:
        local = _local(slot)
        assert slot.start.utcoffset() == timedelta(0)
        assert slot.end - slot.start == timedelta(minutes=60)
        assert 15 <= local.hour and slot.end.astimezone(MSK) <= local.replace(hour=21, minute=0)
        assert local.weekday() != 6
    monday = days[_local(slots[0]).date()]
    assert [_local(s).strftime("%H:%M") for s in monday] == [
        "15:00", "15:30", "16:00", "16:30", "17:00", "17:30", "18:00", "18:30", "19:00", "19:30", "20:00",
    ]


def test_booking_allows_overlapping_options():
    slots = _book(hours=24)
    first, second = slots[0], slots[1]
    assert second.start < first.end  # 15:00–16:00 и 15:30–16:30: гость выберет один


def test_saturday_included_sunday_excluded():
    weekdays = {_local(s).weekday() for s in _book()}
    assert weekdays == {0, 1, 2, 3, 4, 5}


def test_weekdays_are_strict_without_fallback():
    # только суббота: будни не подмешиваются, причина — не «запасной выходной»
    saturdays = _book(weekdays=[5])
    assert saturdays and {_local(s).weekday() for s in saturdays} == {5}
    assert all("выходной" not in s.reason for s in saturdays)
    # только воскресенье в окне пн..пт — пусто (а не будни как запасной вариант)
    assert _book(hours=5 * 24, weekdays=[6]) == []


def test_weekdays_absent_keeps_weekend_skip():
    slots = _book(weekdays=None)
    assert slots and {_local(s).weekday() for s in slots} == {0, 1, 2, 3, 4}


def test_weekdays_are_local_days():
    # 21:30 UTC пятницы — уже суббота по Москве; слоты субботы (по Москве) есть
    friday_night = datetime(2026, 9, 18, 21, 30, tzinfo=timezone.utc)
    slots = _book(now=friday_night, hours=24, weekdays=[5])
    assert slots and all(_local(s).weekday() == 5 for s in slots)


def test_work_hours_override_preferences():
    # в настройках 9..18, у страницы — вечер: работают часы страницы
    slots = _book(prefs=_prefs(work=list(range(9, 19))), hours=24)
    assert _local(slots[0]).hour == 15 and _local(slots[-1]).hour == 20
    # пустой список — часы из настроек
    slots = _book(prefs=_prefs(work=[10, 11]), hours=24, work_hours=[])
    assert [_local(s).strftime("%H:%M") for s in slots] == ["10:00", "10:30", "11:00"]


# ------------------------------------------------------------------ буфер
def test_buffer_widens_busy_event():
    busy = [CalendarEvent(title="урок", start=_msk(14, 16), end=_msk(14, 17))]
    plain = [_local(s).strftime("%H:%M") for s in _book(hours=24, calendar=busy, step_minutes=15)]
    assert "15:00" in plain and "17:00" in plain  # впритык к уроку можно без буфера
    padded = _book(hours=24, calendar=busy, step_minutes=15, buffer_minutes=15)
    labels = [_local(s).strftime("%H:%M") for s in padded]
    assert "15:00" not in labels and "17:00" not in labels
    assert labels[0] == "17:15"  # 17:00 + 15 минут буфера
    for slot in padded:
        assert slot.end <= busy[0].start - timedelta(minutes=15) or slot.start >= busy[0].end + timedelta(minutes=15)


def test_buffer_merges_adjacent_events():
    busy = [
        CalendarEvent(title="a", start=_msk(14, 16), end=_msk(14, 17)),
        CalendarEvent(title="b", start=_msk(14, 18), end=_msk(14, 19)),
    ]
    # с буфером 30 окно 17:00–18:00 между уроками закрыто полностью
    labels = [_local(s).strftime("%H:%M") for s in _book(hours=24, calendar=busy, buffer_minutes=30)]
    assert labels == ["19:30", "20:00"]


def test_zero_length_event_still_blocks():
    point = [CalendarEvent(title="звонок", start=_msk(14, 15, 15), end=_msk(14, 15, 15))]
    labels = [_local(s).strftime("%H:%M") for s in _book(hours=24, calendar=point)]
    assert "15:00" not in labels and "15:30" in labels


# ---------------------------------------------------------- min_notice и шаг
def test_min_notice_minutes():
    now = _msk(14, 15, 10)
    assert _local(_book(now=now, hours=24)[0]).strftime("%H:%M") == "15:30"
    later = _book(now=now, hours=24, min_notice_minutes=180)  # 18:10 → 18:30
    assert _local(later[0]).strftime("%H:%M") == "18:30"
    assert all(s.start >= now + timedelta(minutes=180) for s in later)
    week = _book(now=now, hours=10 * 24, min_notice_minutes=7 * 24 * 60)
    assert week[0].start >= now + timedelta(days=7)


def test_default_min_notice_is_one_hour():
    now = _msk(14, 15, 10)
    slots = planner.offer_slots(
        planner.TimeWindow(start=now, end=now + timedelta(hours=24)), [], _prefs(), "Europe/Moscow",
        max_slots=200, per_day=12, now=now, work_hours=EVENING,
    )
    assert _local(slots[0]).strftime("%H:%M") == "16:30"


def test_step_60_aligns_to_local_hour():
    slots = _book(hours=24, step_minutes=60)
    assert [_local(s).strftime("%H:%M") for s in slots] == ["15:00", "16:00", "17:00", "18:00", "19:00", "20:00"]
    # Индия UTC+5:30: начало часа по местному времени — :30 по UTC
    kolkata = _book(hours=24, step_minutes=60, tz="Asia/Kolkata", now=_msk(14, 13, 5))
    assert kolkata and all(s.start.astimezone(planner._zone("Asia/Kolkata")).minute == 0 for s in kolkata)
    assert all(s.start.minute == 30 for s in kolkata)


def test_step_15_uses_quarters():
    slots = _book(hours=24, step_minutes=15)
    minutes = {_local(s).minute for s in slots}
    assert minutes == {0, 15, 30, 45}
    assert len(slots) == 12  # больше 21 варианта в день, но per_day=12: первые 12
    assert _local(slots[-1]).strftime("%H:%M") == "17:45"


def test_per_day_and_max_slots_caps():
    full = _book()
    capped = _book(max_slots=20)
    assert capped == full[:20]  # ближайшие дни важнее, порядок хронологический
    three = _book(per_day=3)
    assert all(len(day) == 3 for day in _by_day(three).values())
    assert [_local(s).strftime("%H:%M") for s in three[:3]] == ["15:00", "15:30", "16:00"]


def test_long_window_60_days():
    slots = _book(hours=60 * 24, max_slots=200)
    assert len(slots) == 200
    assert [s.start for s in slots] == sorted(s.start for s in slots)


def test_invite_defaults_unchanged_with_new_arguments():
    # явные значения по умолчанию дают ровно то же, что прежний вызов
    window = planner.TimeWindow(start=MONDAY, end=MONDAY + timedelta(hours=96))
    before = planner.offer_slots(window, [], _prefs(preferred=[16]), "Europe/Moscow", max_slots=6, now=MONDAY)
    after = planner.offer_slots(
        window, [], _prefs(preferred=[16]), "Europe/Moscow", max_slots=6, now=MONDAY, per_day=2,
        min_notice_minutes=60, buffer_minutes=0, step_minutes=30, work_hours=None, weekdays=None,
    )
    assert before == after and len(before) == 6


# ------------------------------------------------------------------ HTTP API
def _agent() -> AuraAgent:
    settings = Settings()
    settings.memory_backend = "memory://"
    return AuraAgent(settings=settings, llm=MockLLM(), store=memory_module.InMemoryStore(), tools=ToolManager())


@pytest.fixture()
def env(monkeypatch):
    monkeypatch.setenv("AURA_MEMORY_BACKEND", "memory://")
    monkeypatch.delenv("AURA_AI_TOKEN", raising=False)
    monkeypatch.delenv("AURA_AI_INSECURE_DEV", raising=False)
    reset_settings()
    app_module.set_agent(_agent())
    yield monkeypatch
    app_module.set_agent(None)
    reset_settings()


@pytest.fixture()
def client(env):
    env.setenv("AURA_AI_INSECURE_DEV", "1")
    reset_settings()
    with TestClient(app_module.app, raise_server_exceptions=False) as test_client:
        yield test_client


def _future_monday() -> datetime:
    """Понедельник 00:00 UTC через 2+ недели — тест не зависит от сегодняшней даты."""
    base = (datetime.now(timezone.utc) + timedelta(days=14)).replace(hour=0, minute=0, second=0, microsecond=0)
    return base + timedelta(days=(7 - base.weekday()) % 7)


def _parse(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def _booking_body(monday: datetime, **extra):
    body = {
        "context": {
            "now": monday.isoformat(),
            "timezone": "Europe/Moscow",
            "preferences": {"work_hours": list(range(9, 19))},
            "calendar": [
                # вторник 16:00–17:00 МСК = 13:00–14:00 UTC
                {"title": "урок", "start": (monday + timedelta(days=1, hours=13)).isoformat(),
                 "end": (monday + timedelta(days=1, hours=14)).isoformat()},
            ],
        },
        "duration_minutes": 60,
        "window_hours": 14 * 24,
        "max_slots": 200,
        "per_day": 12,
        "min_notice_minutes": 720,
        "buffer_minutes": 10,
        "step_minutes": 30,
        "work_hours": EVENING,
        "weekdays": SIX_DAYS,
    }
    body.update(extra)
    return body


def test_slots_endpoint_booking_page(client):
    monday = _future_monday()
    response = client.post("/v1/agent/slots", json=_booking_body(monday))
    assert response.status_code == 200, response.text
    slots = response.json()["slots"]
    starts = [_parse(s["start"]) for s in slots]
    assert starts == sorted(starts)
    busy_start = monday + timedelta(days=1, hours=13) - timedelta(minutes=10)
    busy_end = monday + timedelta(days=1, hours=14) + timedelta(minutes=10)
    per_day = {}
    for slot in slots:
        start, end = _parse(slot["start"]), _parse(slot["end"])
        local = start.astimezone(MSK)
        per_day[local.date()] = per_day.get(local.date(), 0) + 1
        assert start >= monday + timedelta(hours=12)  # min_notice 12 часов
        assert local.weekday() != 6 and 15 <= local.hour <= 20
        assert end <= busy_start or start >= busy_end
    assert max(per_day.values()) <= 12
    assert {d.weekday() for d in per_day} == {0, 1, 2, 3, 4, 5}
    # вторник: 15:00 (до 16:00, но буфер 10 минут) и 16:00–16:30 закрыты, 17:30 — первое после
    tuesday = sorted(s for s in starts if s.astimezone(MSK).weekday() == 1 and s < monday + timedelta(days=2))
    assert [s.astimezone(MSK).strftime("%H:%M") for s in tuesday][:1] == ["17:30"]
    # 12 дней без воскресений по 11 вариантов (пн: 03:00 МСК + 12 часов = 15:00 — вечер целиком)
    assert len(slots) == 11 * 12 - 5  # во вторник нет 15:00, 15:30, 16:00, 16:30, 17:00


def test_slots_endpoint_booking_caps(client):
    monday = _future_monday()
    response = client.post("/v1/agent/slots", json=_booking_body(monday, max_slots=7, step_minutes=60))
    assert response.status_code == 200, response.text
    slots = response.json()["slots"]
    assert len(slots) == 7
    assert all(_parse(s["start"]).astimezone(MSK).minute == 0 for s in slots)


def test_slots_endpoint_upper_bounds_accepted(client):
    body = {
        "context": {"timezone": "UTC", "preferences": {"work_hours": list(range(24))}},
        "window_hours": 1440, "max_slots": 200, "per_day": 48, "min_notice_minutes": 10080,
        "buffer_minutes": 120, "step_minutes": 15, "work_hours": list(range(24)), "weekdays": [0, 1, 2, 3, 4, 5, 6],
    }
    response = client.post("/v1/agent/slots", json=body)
    assert response.status_code == 200, response.text
    assert len(response.json()["slots"]) == 200


def test_slots_endpoint_empty_lists_mean_absent(client):
    body = {"context": {"timezone": "UTC", "preferences": {"work_hours": list(range(24))}},
            "work_hours": [], "weekdays": []}
    response = client.post("/v1/agent/slots", json=body)
    assert response.status_code == 200, response.text
    assert 1 <= len(response.json()["slots"]) <= 6


@pytest.mark.parametrize(
    "extra",
    [
        {"window_hours": 1441},
        {"max_slots": 201},
        {"per_day": 0},
        {"per_day": 49},
        {"min_notice_minutes": -1},
        {"min_notice_minutes": 10081},
        {"buffer_minutes": -1},
        {"buffer_minutes": 121},
        {"step_minutes": 0},
        {"step_minutes": 20},
        {"step_minutes": 45},
        {"step_minutes": 120},
        {"work_hours": [24]},
        {"work_hours": [-1]},
        {"work_hours": ["15"]},
        {"work_hours": [15.5]},
        {"work_hours": list(range(24)) + [0]},
        {"work_hours": 15},
        {"weekdays": [7]},
        {"weekdays": [-1]},
        {"weekdays": [True]},
        {"weekdays": [0, 1, 2, 3, 4, 5, 6, 0]},
        {"weekdays": "пн"},
    ],
)
def test_slots_endpoint_booking_bad_input_is_422(client, extra):
    response = client.post("/v1/agent/slots", json={"context": {}, **extra})
    assert response.status_code == 422, response.text


def test_slots_endpoint_booking_auth_unchanged(env):
    body = {"context": {}, "per_day": 12, "max_slots": 200}
    with TestClient(app_module.app) as test_client:
        assert test_client.post("/v1/agent/slots", json=body).status_code == 503
    env.setenv("AURA_AI_TOKEN", "secret")
    reset_settings()
    with TestClient(app_module.app) as test_client:
        assert test_client.post("/v1/agent/slots", json=body).status_code == 401
        assert test_client.post("/v1/agent/slots", json=body, headers={"X-Aura-Token": "secret"}).status_code == 200
