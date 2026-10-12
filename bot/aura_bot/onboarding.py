"""Чистая логика анкеты: парсинг/валидация ответов и превращение профиля в инструкции для LLM."""
from __future__ import annotations

import re
from datetime import date
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

# Каталог диет: код -> подпись (коды хранятся в БД, подписи — для людей и LLM)
DIETS: dict[str, str] = {
    "vegetarian": "вегетарианство",
    "vegan": "веганство",
    "gluten_free": "без глютена",
    "lactose_free": "без лактозы",
    "halal": "халяль",
    "kosher": "кошер",
    "keto": "кето",
    "low_sugar": "без сахара",
}

# Частые пояса РФ/СНГ для кнопок; любой другой — вводом IANA-имени
TIMEZONES: dict[str, str] = {
    "Europe/Kaliningrad": "Калининград (UTC+2)",
    "Europe/Moscow": "Москва (UTC+3)",
    "Europe/Samara": "Самара (UTC+4)",
    "Asia/Yekaterinburg": "Екатеринбург (UTC+5)",
    "Asia/Omsk": "Омск (UTC+6)",
    "Asia/Novosibirsk": "Новосибирск (UTC+7)",
    "Asia/Irkutsk": "Иркутск (UTC+8)",
    "Asia/Vladivostok": "Владивосток (UTC+10)",
}

NONE_WORDS = {"нет", "-", "—", "никаких", "ничего", "no", "none"}
MAX_ITEMS = 20
MAX_ITEM_LEN = 100


class ValidationError(ValueError):
    """Ответ анкеты не прошёл проверку; текст — для пользователя."""


def parse_birthday(text: str, today: date | None = None) -> date:
    """ДД.ММ.ГГГГ (также через / или -) -> date; не из будущего и не раньше 1900."""
    today = today or date.today()
    m = re.fullmatch(r"\s*(\d{1,2})[./-](\d{1,2})[./-](\d{4})\s*", text)
    if not m:
        raise ValidationError("Формат: ДД.ММ.ГГГГ, например 07.03.1994")
    d, mth, y = map(int, m.groups())
    try:
        bday = date(y, mth, d)
    except ValueError:
        raise ValidationError("Такой даты нет — проверьте день и месяц") from None
    if bday > today or y < 1900:
        raise ValidationError("Дата должна быть между 1900 годом и сегодняшним днём")
    return bday


def parse_list(text: str) -> list[str]:
    """«арахис, мёд; киви» -> ['арахис', 'мёд', 'киви']; «нет» -> []. Дубли убираются."""
    if text.strip().lower() in NONE_WORDS:
        return []
    items: list[str] = []
    for raw in re.split(r"[,;\n]+", text):
        item = " ".join(raw.split()).strip(" .")
        if not item:
            continue
        if len(item) > MAX_ITEM_LEN:
            raise ValidationError(f"Слишком длинный пункт (до {MAX_ITEM_LEN} символов): «{item[:30]}…»")
        if item.lower() not in {i.lower() for i in items}:
            items.append(item)
    if len(items) > MAX_ITEMS:
        raise ValidationError(f"Не больше {MAX_ITEMS} пунктов")
    return items


def parse_timezone(text: str) -> str:
    """Проверяет IANA-имя пояса (Europe/Moscow, Asia/Almaty …)."""
    name = text.strip()
    if "/" not in name:  # отсекаем «MSK», «+3» и т.п. — они неоднозначны
        raise ValidationError("Нужно IANA-имя, например Asia/Almaty или Europe/Berlin")
    try:
        ZoneInfo(name)
    except (ZoneInfoNotFoundError, ValueError):
        raise ValidationError(f"Не знаю пояс «{name}». Пример: Asia/Almaty") from None
    return name


def clean_diets(codes: list[str]) -> list[str]:
    """Оставляет только известные коды диет в порядке каталога."""
    return [c for c in DIETS if c in set(codes)]


def age(bday: date, today: date | None = None) -> int:
    today = today or date.today()
    return today.year - bday.year - ((today.month, today.day) < (bday.month, bday.day))


def profile_prompt(
    birthday: date | None,
    allergies: list[str],
    diets: list[str],
    habits: list[str],
    tz: str,
    today: date | None = None,
) -> str:
    """Блок системного промпта: агент обязан учитывать анкету в каждом ответе."""
    lines = ["Профиль пользователя (из анкеты, соблюдай строго):"]
    if birthday:
        lines.append(f"- День рождения: {birthday:%d.%m.%Y} ({age(birthday, today)} лет).")
    if allergies:
        lines.append(
            f"- Аллергии: {', '.join(allergies)}. НИКОГДА не советуй продукты, блюда, "
            "косметику или лекарства с этими компонентами; если они могут встретиться — предупреди."
        )
    if diets:
        lines.append(
            f"- Питание: {', '.join(DIETS.get(d, d) for d in diets)}. "
            "Предлагай только совместимые варианты."
        )
    if habits:
        lines.append(f"- Привычки: {'; '.join(habits)}. Учитывай их в советах и планах.")
    lines.append(f"- Часовой пояс: {tz}. Все даты и время — в нём.")
    return "\n".join(lines)


def profile_summary(birthday: date | None, allergies: list[str], diets: list[str], habits: list[str], tz: str) -> str:
    """Человекочитаемая сводка анкеты (plain text)."""
    dash = "—"
    return "\n".join([
        f"🎂 День рождения: {birthday.strftime('%d.%m.%Y') if birthday else dash}",
        f"⚠️ Аллергии: {', '.join(allergies) or 'нет'}",
        f"🥗 Питание: {', '.join(DIETS.get(d, d) for d in diets) or 'без ограничений'}",
        f"🔁 Привычки: {'; '.join(habits) or dash}",
        f"🕒 Часовой пояс: {TIMEZONES.get(tz, tz)}",
    ])
