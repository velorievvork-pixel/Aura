"""Строгая валидация JSON памяти от LLM."""
from datetime import datetime, timezone

from aura_bot.memory import memories_listing, memories_prompt, normalize, parse_extraction
from aura_bot.models import Memory


def test_valid_object_and_bare_list():
    raw = '{"memories": [{"kind": "fact", "text": " Работает дизайнером "}, {"kind": "contact", "text": "Жена Маша"}]}'
    items = parse_extraction(raw)
    assert [(i.kind, i.text) for i in items] == [("fact", "Работает дизайнером"), ("contact", "Жена Маша")]
    assert len(parse_extraction('[{"kind": "preference", "text": "Любит чай"}]')) == 1


def test_code_fences_allowed():
    assert parse_extraction('```json\n{"memories": [{"kind": "fact", "text": "Живёт в Казани"}]}\n```')[0].text == "Живёт в Казани"


def test_garbage_and_wrong_shapes():
    for raw in ["", "нет", "null", "42", '{"memories": "x"}', '{"other": []}']:
        assert parse_extraction(raw) == []


def test_invalid_items_dropped_individually():
    raw = """{"memories": [
        {"kind": "secret", "text": "пароль 123"},
        {"kind": "fact"},
        {"kind": "fact", "text": "x"},
        {"kind": "fact", "text": "%s"},
        "строка",
        {"kind": "schedule", "text": "Йога по вторникам", "extra": 1}
    ]}""" % ("я" * 301)
    assert [i.text for i in parse_extraction(raw)] == ["Йога по вторникам"]


def test_dedupe_against_known_and_inside_batch():
    raw = '{"memories": [{"kind": "fact", "text": "Любит ёжиков."}, {"kind": "fact", "text": "любит ежиков"}, {"kind": "fact", "text": "Живёт в Туле"}]}'
    assert [i.text for i in parse_extraction(raw, known=["Живёт  в туле"])] == ["Любит ёжиков."]
    assert normalize(" Любит  Ёжиков! ") == "любит ежиков"


def test_limit_ten():
    raw = '{"memories": [%s]}' % ",".join(f'{{"kind": "fact", "text": "Факт {i}"}}' for i in range(30))
    assert len(parse_extraction(raw)) == 10


def test_prompt_and_listing_grouped():
    d = datetime(2026, 10, 1, tzinfo=timezone.utc)
    ms = [Memory(kind="contact", text="Жена Маша", created_at=d), Memory(kind="fact", text="Живёт в Казани", created_at=d)]
    p = memories_prompt(ms)
    assert p.index("Факты") < p.index("Контакты") and "[01.10.2026] Жена Маша" in p
    assert "• Живёт в Казани" in memories_listing(ms)
    assert memories_prompt([]) == "" and "Пока ничего" in memories_listing([])


def test_secrets_never_stored():
    raw = """{"memories": [
        {"kind": "fact", "text": "Пароль от почты qwerty1"},
        {"kind": "fact", "text": "Карта 4276 1234 5678 9012"},
        {"kind": "fact", "text": "API key sk-abc"},
        {"kind": "contact", "text": "Олег: +7 999 123-45-67"}
    ]}"""
    assert [i.text for i in parse_extraction(raw)] == ["Олег: +7 999 123-45-67"]
