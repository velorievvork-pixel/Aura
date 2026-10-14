"""planner.py — планирование задач (напоминания, встречи, слоты в расписании).

Модуль полностью детерминированный: никаких внешних вызовов, поэтому его
поведение покрыто юнит-тестами (ai/tests/test_planner.py).
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from typing import Dict, Iterable, List, Optional, Sequence, Tuple
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from .models import CalendarEvent, Preferences, Slot, utcnow

WEEKDAY_ALIASES: Dict[str, int] = {
    "понедельник": 0, "в понедельник": 0, "пн": 0,
    "вторник": 1, "во вторник": 1, "вт": 1,
    "среда": 2, "в среду": 2, "ср": 2,
    "четверг": 3, "в четверг": 3, "чт": 3,
    "пятница": 4, "в пятницу": 4, "пт": 4,
    "суббота": 5, "в субботу": 5, "сб": 5,
    "воскресенье": 6, "в воскресенье": 6, "вск": 6, "вс": 6,
}

WEEKDAY_NAMES = ("понедельник", "вторник", "среда", "четверг", "пятница", "суббота", "воскресенье")

# Сильные сигналы в начале фразы: «напомни купить кофе» — это напоминание,
# а не встреча, даже если слово «кофе» есть в словаре встреч.
INTENT_PREFIXES: Tuple[Tuple[str, str], ...] = (
    ("reminder", r"^\s*(напомни|напомните|не забудь|не забудьте)"),
    ("note", r"^\s*(запиши|запишите|сделай заметку|законспектируй)"),
    ("find_place", r"^\s*(найди|найдите|подбери|подберите)"),
    ("email", r"^\s*(отправь письмо|напиши письмо|отправьте письмо)"),
    ("message", r"^\s*(напиши|отправь|скинь|ответь)"),
    ("memory", r"^\s*(запомни|запомните)"),
)

INTENT_KEYWORDS: Dict[str, Tuple[str, ...]] = {
    "schedule_meeting": (
        "встрет", "созвон", "встреча", "кофе", "обсудить", "обсудим", "meeting",
        "пересечься", "увидеться", "свидание", "переговоры", "созвониться",
    ),
    "reminder": ("напомни", "напомнить", "напоминание", "не забыть", "reminder", "не забудь"),
    "note": ("заметк", "запиши", "записать", "конспект", "note"),
    "email": ("почт", "письмо", "email", "e-mail", "напиши письмо", "отправь письмо"),
    "message": ("напиши", "отправь", "сообщение", "скинь", "message", "ответь"),
    "find_place": ("найди", "подбери", "где", "кафе", "кофейн", "ресторан", "место", "place"),
    "budget": ("бюджет", "потрат", "деньги", "стоимость", "budget", "оплати"),
    "memory": ("запомни", "помни", "запомнить", "я люблю", "я не люблю", "у меня аллергия"),
}

DAY_PARTS: Dict[str, Tuple[int, int]] = {
    "утром": (8, 12), "утро": (8, 12),
    "днём": (12, 17), "днем": (12, 17), "после обеда": (13, 18),
    "вечером": (17, 23), "вечер": (17, 23),
    "ночью": (0, 6),
}

TIME_RE = re.compile(r"\b(\d{1,2})[:.](\d{2})\b")
# «в 10», «в 7 вечера», «в 19 часов» — час без минут.
HOUR_RE = re.compile(r"(?:^|\s)в\s+(\d{1,2})(?:\s*(?:час\w*|ч\b))?(?:\s+(утра|дня|вечера|ночи))?(?=$|[\s,.!?])")
IN_N_RE = re.compile(r"через\s+(\d+)\s*(минут\w*|час\w*|день|дня|дней|недел\w*)")
DURATION_RE = re.compile(r"\bна\s+(\d+)\s*(минут\w*|час\w*|ч\b)")


@dataclass
class TimeWindow:
    """Окно времени, которое упомянул пользователь."""

    start: datetime
    end: datetime
    label: str = ""
    source: str = "inferred"

    def as_dict(self) -> Dict[str, str]:
        return {
            "start": self.start.isoformat(),
            "end": self.end.isoformat(),
            "label": self.label,
            "source": self.source,
        }


@dataclass
class Understanding:
    """Результат разбора свободной фразы."""

    text: str
    intent: str
    window: Optional[TimeWindow] = None
    duration_minutes: int = 60
    person: str = ""
    topic: str = ""
    keywords: List[str] = field(default_factory=list)

    def as_dict(self) -> Dict[str, object]:
        return {
            "text": self.text,
            "intent": self.intent,
            "window": self.window.as_dict() if self.window else None,
            "duration_minutes": self.duration_minutes,
            "person": self.person,
            "topic": self.topic,
            "keywords": list(self.keywords),
        }


def _as_aware(dt: datetime) -> datetime:
    return dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)


def detect_intent(text: str) -> str:
    """Определяет намерение по ключевым словам. Возвращает один из INTENT_KEYWORDS."""
    low = (text or "").lower()
    if not low.strip():
        return "chat"
    for intent, pattern in INTENT_PREFIXES:
        if re.match(pattern, low):
            return intent
    scores: List[Tuple[int, str]] = []
    for intent, words in INTENT_KEYWORDS.items():
        hits = sum(1 for w in words if w in low)
        if hits:
            scores.append((hits, intent))
    if not scores:
        return "chat"
    # Стабильная сортировка: больше совпадений → выше; при равенстве — порядок словаря.
    scores.sort(key=lambda item: (-item[0], list(INTENT_KEYWORDS).index(item[1])))
    return scores[0][1]


def parse_duration(text: str, default: int = 60) -> int:
    low = (text or "").lower()
    match = DURATION_RE.search(low)
    if match:
        value = int(match.group(1))
        unit = match.group(2)
        return value * 60 if unit.startswith(("час", "ч")) else value
    if "полчаса" in low:
        return 30
    if "на час" in low:
        return 60
    return default


def extract_person(text: str) -> str:
    """'встретиться с Аней' → 'Аней' (грубо, но достаточно для поиска контакта)."""
    match = re.search(r"\bс\s+([А-ЯЁA-Z][а-яёa-zA-Z]{1,30})\b", text or "")
    if match:
        return match.group(1)
    match = re.search(r"\bс другом\b|\bс подругой\b", (text or "").lower())
    return match.group(0).replace("с ", "") if match else ""


def extract_topic(text: str) -> str:
    match = re.search(r"(?:обсудить|про|о|по поводу)\s+([а-яёa-zA-Z0-9 ,\-]{3,60})", (text or "").lower())
    return match.group(1).strip(" .,") if match else ""


def next_weekday(now: datetime, weekday: int) -> datetime:
    """Ближайшая дата с указанным номером дня недели (0 = понедельник)."""
    delta = (weekday - now.weekday()) % 7
    day = now + timedelta(days=delta)
    return day.replace(hour=0, minute=0, second=0, microsecond=0)


def _clock_time(low: str, has_day: bool = True) -> Optional[Tuple[int, int]]:
    """Время суток из фразы: «9:30» / «9.30» или «в 10», «в 7 вечера».

    Голое «в 10» — время, только если рядом назван день («завтра в 10»):
    иначе «в 2 раза» превратилось бы в 2 часа ночи.
    """
    match = TIME_RE.search(low)
    if match:
        return min(int(match.group(1)), 23), min(int(match.group(2)), 59)
    match = HOUR_RE.search(low)
    if not match:
        return None
    hour = int(match.group(1))
    part = match.group(2)
    explicit = part is not None or re.search(r"^\s*в\s+\d{1,2}\s*(?:час|ч\b)", match.group(0)) is not None
    if hour > 23 or not (explicit or has_day):
        return None
    if part in ("дня", "вечера") and hour < 12:
        hour += 12
    elif part == "ночи" and hour == 12:
        hour = 0
    return hour, 0


# Служебные слова напоминания: «Напомни завтра в 10 позвонить юристу» → «Позвонить юристу».
_REMINDER_NOISE = (
    r"^\s*(?:напомни(?:те)?|не\s+забудь(?:те)?)(?:\s+мне)?\b",
    r"\b(?:послезавтра|завтра|сегодня)\b",
    r"\bв\s+\d{1,2}[:.]\d{2}(?:\s+(?:утра|дня|вечера|ночи))?",
    r"\bв\s+\d{1,2}(?:\s*(?:час\w*|ч\b))?(?:\s+(?:утра|дня|вечера|ночи))?(?=$|[\s,.!?])",
    r"\b\d{1,2}[:.]\d{2}\b",
    r"\bчерез\s+\d+\s*(?:минут\w*|час\w*|день|дня|дней|недел\w*)",
    r"\b(?:утром|днём|днем|вечером|ночью)\b",
    r"\b(?:в|во)\s+(?:понедельник|вторник|среду|четверг|пятницу|субботу|воскресенье)\b",
)


def reminder_text(text: str) -> str:
    """Суть напоминания без «напомни» и указаний времени; если ничего не осталось — исходная фраза."""
    value = text or ""
    for pattern in _REMINDER_NOISE:
        value = re.sub(pattern, " ", value, flags=re.IGNORECASE)
    value = re.sub(r"^[\s,.:;\-—]*(?:(?:о\s+том,?\s+)?чтобы?|что)\b", " ", value, flags=re.IGNORECASE)
    value = re.sub(r"\s+", " ", value).strip(" ,.:;-—")
    if not value:
        return (text or "").strip()
    return value[0].upper() + value[1:]


def parse_time_window(text: str, now: Optional[datetime] = None) -> Optional[TimeWindow]:
    """Разбирает русские временные выражения в окно [start, end)."""
    now = _as_aware(now or utcnow())
    low = (text or "").lower()
    if not low.strip():
        return None

    base_day = now.replace(hour=0, minute=0, second=0, microsecond=0)
    day_start = base_day
    label = ""
    source = "phrase"

    if "послезавтра" in low:
        day_start = base_day + timedelta(days=2)
        label = "послезавтра"
    elif "завтра" in low:
        day_start = base_day + timedelta(days=1)
        label = "завтра"
    elif "выходн" in low:
        # «в эти выходные»: ближайшая суббота (если сегодня сб/вс — сегодняшний день)
        offset = (5 - now.weekday()) % 7
        day_start = base_day + timedelta(days=offset)
        label = "выходные"
    elif "на следующей неделе" in low or "следующая неделя" in low:
        day_start = base_day + timedelta(days=7 - now.weekday())
        label = "следующая неделя"
    elif "на этой неделе" in low or "эта неделя" in low:
        label = "эта неделя"
    elif "сегодня" in low:
        label = "сегодня"
    else:
        for alias, weekday in WEEKDAY_ALIASES.items():
            if re.search(rf"(^|[^а-яё]){re.escape(alias)}([^а-яё]|$)", low):
                day_start = next_weekday(now, weekday)
                label = WEEKDAY_NAMES[weekday]
                break

    day_end = day_start + timedelta(days=2 if label == "выходные" else (7 if "неделе" in label or "неделя" in label else 1))

    # Ограничение по времени суток / конкретному часу
    start, end = day_start, day_end
    clock = _clock_time(low, has_day=bool(label))
    if clock and label:
        hour, minute = clock
        start = day_start.replace(hour=hour, minute=minute)
        end = start + timedelta(hours=3)
    elif clock and not label:
        hour, minute = clock
        candidate = now.replace(hour=hour, minute=minute, second=0, microsecond=0)
        if candidate <= now:
            candidate += timedelta(days=1)
        start, end = candidate, candidate + timedelta(hours=3)
        label = label or "ближайшее время"
    else:
        for part, (from_h, to_h) in DAY_PARTS.items():
            if part in low:
                start = day_start.replace(hour=from_h)
                end = day_start.replace(hour=to_h)
                label = label or part
                break

    # «через N часов/дней» — относительное окно
    in_match = IN_N_RE.search(low)
    if in_match:
        value = int(in_match.group(1))
        unit = in_match.group(2)
        if unit.startswith("минут"):
            start = now + timedelta(minutes=value)
        elif unit.startswith("час"):
            start = now + timedelta(hours=value)
        elif unit.startswith("недел"):
            start = now + timedelta(weeks=value)
        else:
            start = now + timedelta(days=value)
        end = start + timedelta(hours=4)
        label = label or f"через {value} {unit}"
        source = "relative"

    if end <= start:
        end = start + timedelta(hours=3)
    if start < now:
        start = now
        if end <= start:
            end = start + timedelta(hours=3)

    return TimeWindow(start=start, end=end, label=label or "ближайшее время", source=source)


def understand(text: str, now: Optional[datetime] = None) -> Understanding:
    """Полный разбор фразы: намерение + окно + длительность + персона + тема."""
    return Understanding(
        text=text,
        intent=detect_intent(text),
        window=parse_time_window(text, now),
        duration_minutes=parse_duration(text),
        person=extract_person(text),
        topic=extract_topic(text),
        keywords=[w for w in re.findall(r"[а-яёa-z]{4,}", (text or "").lower())],
    )


def _busy_intervals(calendar: Sequence[CalendarEvent]) -> List[Tuple[datetime, datetime]]:
    return sorted((_as_aware(e.start), _as_aware(e.end)) for e in calendar)


def _overlaps(a_start: datetime, a_end: datetime, busy: Iterable[Tuple[datetime, datetime]]) -> bool:
    return any(a_start < b_end and b_start < a_end for b_start, b_end in busy)


def score_slot(
    start: datetime,
    prefs: Preferences,
    busy: Sequence[Tuple[datetime, datetime]],
    work_hours: bool = True,
) -> float:
    """Оценка слота: предпочтения по часам > рабочие часы > близость к сейчас."""
    score = 0.5
    if start.hour in (prefs.preferred_hours or []):
        score += 0.25
    if work_hours and start.hour in (prefs.work_hours or []):
        score += 0.1
    if start.weekday() >= 5:
        score -= 0.05
    if _overlaps(start, start + timedelta(minutes=30), busy):
        score -= 0.4
    score += max(0.0, 0.1 - _hours_from_now(start) / 240.0)
    return round(max(0.0, min(1.0, score)), 3)


def _hours_from_now(start: datetime) -> float:
    return max(0.0, (start - utcnow()).total_seconds() / 3600.0)


def free_slots(
    window: TimeWindow,
    calendar: Sequence[CalendarEvent],
    prefs: Preferences,
    duration_minutes: int = 60,
    step_minutes: int = 30,
    max_slots: int = 5,
) -> List[Slot]:
    """Ищет свободные слоты внутри окна, не пересекающиеся с календарём."""
    busy = _busy_intervals(calendar)
    duration = timedelta(minutes=max(15, duration_minutes))
    step = timedelta(minutes=max(15, step_minutes))
    results: List[Slot] = []
    cursor = window.start
    # Округляем вверх до получаса, чтобы слоты выглядели по-человечески.
    if cursor.minute % 30:
        cursor = cursor.replace(minute=30, second=0, microsecond=0)
    guard = 0
    while cursor + duration <= window.end and guard < 500:
        guard += 1
        if not _overlaps(cursor, cursor + duration, busy):
            score = score_slot(cursor, prefs, busy)
            reason = "свободно"
            if cursor.hour in (prefs.preferred_hours or []):
                reason = "удобное время"
            results.append(
                Slot(start=cursor, end=cursor + duration, score=score, reason=reason)
            )
        cursor += step
    results.sort(key=lambda s: (-s.score, s.start))
    return results[:max_slots]


def intersect_slots(
    a: Sequence[Slot],
    b: Sequence[Slot],
    duration_minutes: int = 60,
) -> List[Slot]:
    """Пересечение свободных слотов двух Аур (ядро Agent-to-Agent)."""
    duration = timedelta(minutes=max(15, duration_minutes))
    merged: List[Slot] = []
    for left in a:
        for right in b:
            start = max(left.start, right.start)
            end = min(left.end, right.end)
            if end - start >= duration:
                merged.append(
                    Slot(
                        start=start,
                        end=start + duration,
                        score=round((left.score + right.score) / 2.0, 3),
                        reason="общее окно в графиках",
                    )
                )
    merged.sort(key=lambda s: (-s.score, s.start))
    return merged


# ------------------------------------------- слоты для приглашения и страниц записи
#: Минимальный зазор между «сейчас» и началом встречи по умолчанию (min_notice_minutes).
OFFER_LEAD = timedelta(hours=1)
#: Шаг кандидатов по умолчанию (по местному времени владельца: :00 и :30).
OFFER_STEP_MINUTES = 30
#: Допустимые шаги: кандидаты выровнены по местным :00/:15/:30/:45.
OFFER_STEPS = (15, 30, 60)
#: Рабочие часы по умолчанию, если у владельца список пуст (или весь был мусором).
DEFAULT_WORK_HOURS = tuple(range(9, 19))
#: per_day больше этого — режим «страница записи»: все подходящие варианты подряд.
INVITE_MAX_PER_DAY = 2


def _zone(tz_name: str) -> ZoneInfo:
    """IANA-пояс владельца; неизвестное имя → UTC (а не исключение)."""
    try:
        return ZoneInfo(tz_name or "UTC")
    except (ZoneInfoNotFoundError, ValueError, OSError):
        return ZoneInfo("UTC")


def _ceil_local_step(moment: datetime, zone: ZoneInfo, step_minutes: int = OFFER_STEP_MINUTES) -> datetime:
    """Округляет момент вверх до кратного step_minutes по местному времени; результат в UTC.

    Шаг 15/30/60 делит час, поэтому кратность считаем по минутам внутри часа:
    60 → :00, 30 → :00/:30, 15 → :00/:15/:30/:45.
    """
    local = moment.astimezone(zone)
    rounded = local.replace(second=0, microsecond=0)
    if rounded < local:
        rounded += timedelta(minutes=1)
    extra = rounded.minute % step_minutes
    if extra:
        rounded += timedelta(minutes=step_minutes - extra)
    return rounded.astimezone(timezone.utc)


def _ceil_local_half_hour(moment: datetime, zone: ZoneInfo) -> datetime:
    """Округляет момент вверх до :00/:30 по местному времени; результат в UTC."""
    return _ceil_local_step(moment, zone, OFFER_STEP_MINUTES)


def _within_work_hours(start: datetime, end: datetime, zone: ZoneInfo, hours: frozenset) -> bool:
    """Вся встреча [start, end) лежит в рабочих часах владельца (местное время).

    Проверяем каждую четверть часа и последний миг встречи: смещения поясов
    кратны 15 минутам, поэтому начало каждого затронутого часа попадает в сетку.
    """
    probe = start
    quarter = timedelta(minutes=15)
    while probe < end:
        if probe.astimezone(zone).hour not in hours:
            return False
        probe += quarter
    return (end - timedelta(microseconds=1)).astimezone(zone).hour in hours


def _merged_busy(
    calendar: Sequence[CalendarEvent], buffer: timedelta
) -> List[Tuple[datetime, datetime]]:
    """Занятые интервалы, расширенные на buffer с обеих сторон и слитые.

    После слияния интервалы не пересекаются и упорядочены и по началу, и по концу —
    это позволяет проверять монотонно растущие кандидаты одним указателем.
    """
    merged: List[Tuple[datetime, datetime]] = []
    for start, end in _busy_intervals(calendar):
        # Событие нулевой длины (точка) тоже занимает: как и в _overlaps, оно
        # блокирует кандидата, если лежит строго внутри него.
        start, end = start - buffer, end + buffer
        if merged and start <= merged[-1][1]:
            if end > merged[-1][1]:
                merged[-1] = (merged[-1][0], end)
        else:
            merged.append((start, end))
    return merged


def offer_slots(
    window: TimeWindow,
    calendar: Sequence[CalendarEvent],
    prefs: Preferences,
    tz_name: str,
    duration_minutes: int = 60,
    max_slots: int = 6,
    per_day: int = INVITE_MAX_PER_DAY,
    now: Optional[datetime] = None,
    min_notice_minutes: int = 60,
    buffer_minutes: int = 0,
    step_minutes: int = OFFER_STEP_MINUTES,
    work_hours: Optional[Sequence[int]] = None,
    weekdays: Optional[Sequence[int]] = None,
) -> List[Slot]:
    """Слоты владельца: приглашение по ссылке (invite.*) и страница записи (booking.*).

    Общие правила (спецификации invite.* и booking.*, раздел «AI-сервис»):
    * кандидаты каждые step_minutes (15/30/60; иначе 30), выровненные по местному
      времени владельца (60 → :00, 30 → :00/:30, 15 → четверти часа), не раньше
      now + min_notice_minutes (по умолчанию 1 час) и целиком внутри окна;
    * вся встреча [start, end) — в рабочих часах владельца (work_hours, если список
      не пуст, иначе prefs.work_hours, иначе 9..18; часы местные) в ЕГО поясе
      tz_name (неизвестный пояс → UTC): часы 9..18 означают начало не раньше 09:00
      и конец не позже 19:00;
    * дни недели: weekdays задан (непуст) — строго эти местные дни (0 = пн), без
      запасного выхода; не задан — сб/вс пропускаются, если без них есть хоть
      один слот;
    * без пересечения с событиями календаря, каждое расширено на buffer_minutes
      с обеих сторон;
    * start/end — aware UTC, итог хронологический, не больше max_slots.

    Выбор зависит от per_day:
    * per_day ≤ 2 — приглашение (гость без Ауры выбирает один из немногих): не больше
      per_day слотов в местный день; внутри дня сначала предпочтительные часы
      (prefs.preferred_hours, местные), затем раньше; слоты одного дня не
      перекрываются; по дням — равномерно (по кругу: сначала лучший слот каждого
      дня, потом второй);
    * per_day > 2 — страница записи: ВСЕ подходящие варианты подряд, в т.ч.
      перекрывающиеся (15:00, 15:30, 16:00 для часового урока — гость берёт один);
      в дне — первые per_day по времени, затем по дням хронологически до max_slots
      (ближайшие дни важнее дальних). Именно «первые», а не «равномерно по дню»:
      при сдвиге now уже показанные гостю варианты не пропадают (booking.book
      сверяет start с текущими слотами); длинные рабочие часы при малом per_day
      вызывающий покрывает шагом step_minutes=60.
    """
    zone = _zone(tz_name)
    now = _as_aware(now or utcnow())
    duration = timedelta(minutes=max(15, int(duration_minutes)))
    step_value = int(step_minutes) if int(step_minutes) in OFFER_STEPS else OFFER_STEP_MINUTES
    step = timedelta(minutes=step_value)
    lead = timedelta(minutes=max(0, int(min_notice_minutes)))
    buffer = timedelta(minutes=max(0, int(buffer_minutes)))
    max_slots = max(0, int(max_slots))
    per_day = max(1, int(per_day))
    if max_slots == 0:
        return []

    work = frozenset(h for h in (work_hours or []) if 0 <= int(h) <= 23) or frozenset(
        prefs.work_hours or DEFAULT_WORK_HOURS
    )
    allowed_days = frozenset(d for d in (weekdays or []) if 0 <= int(d) <= 6)
    preferred = frozenset(prefs.preferred_hours or [])
    busy = _merged_busy(calendar, buffer)

    window_start = _as_aware(window.start)
    window_end = _as_aware(window.end)
    cursor = _ceil_local_step(max(window_start, now + lead), zone, step_value)

    regular: List[Tuple[datetime, datetime]] = []
    weekends: List[Tuple[datetime, datetime]] = []
    pointer = 0
    while cursor + duration <= window_end:
        end = cursor + duration
        # Интервалы, закончившиеся до начала кандидата, дальше не нужны (кандидаты растут).
        while pointer < len(busy) and busy[pointer][1] <= cursor:
            pointer += 1
        free = pointer >= len(busy) or busy[pointer][0] >= end
        if free and _within_work_hours(cursor, end, zone, work):
            day = cursor.astimezone(zone).weekday()
            if allowed_days:
                if day in allowed_days:
                    regular.append((cursor, end))
            elif day >= 5:
                weekends.append((cursor, end))
            else:
                regular.append((cursor, end))
        cursor += step

    fallback = not regular and bool(weekends)
    candidates = regular or weekends
    if not candidates:
        return []

    # Группировка по местному дню владельца (кандидаты уже хронологические).
    by_day: Dict[object, List[Tuple[datetime, datetime]]] = {}
    for start, end in candidates:
        by_day.setdefault(start.astimezone(zone).date(), []).append((start, end))

    selected: List[Tuple[datetime, datetime]] = []
    if per_day > INVITE_MAX_PER_DAY:
        for day in sorted(by_day):
            selected.extend(by_day[day][:per_day])
        selected = selected[:max_slots]
    else:
        picks_by_day: List[List[Tuple[datetime, datetime]]] = []
        for day in sorted(by_day):
            ranked = sorted(
                by_day[day],
                key=lambda item: (item[0].astimezone(zone).hour not in preferred, item[0]),
            )
            chosen: List[Tuple[datetime, datetime]] = []
            for start, end in ranked:
                if len(chosen) >= per_day:
                    break
                if _overlaps(start, end, chosen):
                    continue
                chosen.append((start, end))
            picks_by_day.append(chosen)

        # По кругу: первый (лучший) слот каждого дня, затем второй и т.д.
        for rank in range(per_day):
            for chosen in picks_by_day:
                if rank < len(chosen) and len(selected) < max_slots:
                    selected.append(chosen[rank])
        selected.sort()

    result: List[Slot] = []
    for start, end in selected:
        local = start.astimezone(zone)
        is_preferred = local.hour in preferred
        score = 0.6 + (0.3 if is_preferred else 0.0)
        if fallback:
            score -= 0.05
        hours_ahead = max(0.0, (start - now).total_seconds() / 3600.0)
        score += max(0.0, 0.05 - hours_ahead / 2000.0)
        if fallback:
            reason = "выходной день (в будни свободных окон нет)"
        elif is_preferred:
            reason = "удобное время"
        else:
            reason = "рабочее время"
        result.append(
            Slot(
                start=start.astimezone(timezone.utc),
                end=end.astimezone(timezone.utc),
                score=round(max(0.0, min(1.0, score)), 3),
                reason=reason,
            )
        )
    return result
