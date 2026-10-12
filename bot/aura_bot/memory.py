"""Долгая память: промпт извлечения, строгая валидация JSON от LLM, формат для контекста."""
from __future__ import annotations

import json
import logging
import re
from typing import Iterable, Literal

from pydantic import BaseModel, ConfigDict, Field, ValidationError

from .models import MEMORY_KINDS, Memory

log = logging.getLogger(__name__)

KIND_TITLES = {"fact": "Факты", "preference": "Предпочтения", "schedule": "Расписание", "contact": "Контакты"}
MAX_ITEMS_PER_EXTRACTION = 10
# Секреты в память не пишем, даже если LLM их «извлекла»
SECRET_RE = re.compile(r"парол|password|passwd|cvv|cvc|пин-?код|pin-?code|токен|token|api.?key|сид-?фраз|seed", re.I)
MAX_DIGITS = 12  # телефон (11 цифр) можно, номер карты/паспорта+серии — нет

EXTRACTION_PROMPT = """Ты модуль памяти личного ассистента. Из последнего обмена репликами извлеки \
НОВЫЕ устойчивые сведения о ПОЛЬЗОВАТЕЛЕ, полезные в будущих разговорах.
Типы (kind):
- fact — факты о жизни: работа, город, семья, здоровье, питомцы;
- preference — вкусы, что любит и не любит, как с ним общаться;
- schedule — регулярные дела и важные даты (тренировки по вт, отпуск в июле);
- contact — люди и как с ними связаться (жена Маша, коллега Олег — @oleg).
Не извлекай: разовые просьбы, сиюминутное, сведения об ассистенте, то, что уже есть в известном.
НИКОГДА не сохраняй пароли, коды, номера карт и документов.
Пиши text коротко, от третьего лица, на русском («Работает дизайнером»).
Ответ — СТРОГО JSON без пояснений: {"memories": [{"kind": "...", "text": "..."}]}; нечего сохранять — {"memories": []}."""


class MemoryItem(BaseModel):
    model_config = ConfigDict(extra="ignore", str_strip_whitespace=True)
    kind: Literal["fact", "preference", "schedule", "contact"]
    text: str = Field(min_length=2, max_length=300)


assert set(MemoryItem.model_fields["kind"].annotation.__args__) == set(MEMORY_KINDS)


def normalize(text: str) -> str:
    """Ключ для дедупликации: регистр, пробелы и финальная пунктуация не важны."""
    return " ".join(text.casefold().replace("ё", "е").split()).strip(" .!")


def looks_secret(text: str) -> bool:
    return bool(SECRET_RE.search(text)) or sum(ch.isdigit() for ch in text) > MAX_DIGITS


def _strip_fences(raw: str) -> str:
    m = re.fullmatch(r"\s*```(?:json)?\s*(.*?)\s*```\s*", raw, re.S)
    return m.group(1) if m else raw


def parse_extraction(raw: str, known: Iterable[str] = ()) -> list[MemoryItem]:
    """Сервер — источник истины: всё, что не прошло схему, отбрасывается поштучно."""
    try:
        data = json.loads(_strip_fences(raw))
    except (json.JSONDecodeError, TypeError):
        log.warning("память: не JSON: %r", raw[:200])
        return []
    if isinstance(data, dict):
        data = data.get("memories")
    if not isinstance(data, list):
        log.warning("память: нет списка memories: %r", raw[:200])
        return []
    seen = {normalize(t) for t in known}
    items: list[MemoryItem] = []
    for raw_item in data[: MAX_ITEMS_PER_EXTRACTION * 2]:
        try:
            item = MemoryItem.model_validate(raw_item)
        except ValidationError:
            log.info("память: отброшен элемент %r", raw_item)
            continue
        if looks_secret(item.text):
            log.info("память: отброшен похожий на секрет элемент")
            continue
        key = normalize(item.text)
        if key and key not in seen:
            seen.add(key)
            items.append(item)
    return items[:MAX_ITEMS_PER_EXTRACTION]


def memories_prompt(memories: list[Memory]) -> str:
    """Память для системного промпта, по типам; дата помогает LLM выбрать свежее при противоречиях."""
    if not memories:
        return ""
    lines = ["Что ты знаешь о пользователе (дата — когда узнал; при противоречии верь более свежему):"]
    for kind in MEMORY_KINDS:
        group = [m for m in memories if m.kind == kind]
        if group:
            lines.append(f"{KIND_TITLES[kind]}:")
            lines += [f"- [{m.created_at:%d.%m.%Y}] {m.text}" for m in group]
    return "\n".join(lines)


def memories_listing(memories: list[Memory]) -> str:
    """Человекочитаемый список для /memory."""
    if not memories:
        return "Пока ничего не запомнено. Просто общайтесь — важное сохранится само."
    out = []
    for kind in MEMORY_KINDS:
        group = [m for m in memories if m.kind == kind]
        if group:
            out.append(f"\n{KIND_TITLES[kind]}:")
            out += [f"• {m.text}" for m in group]
    return "\n".join(out).strip()
