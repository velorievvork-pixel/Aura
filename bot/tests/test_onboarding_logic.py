"""Юнит-тесты парсинга анкеты и промпта профиля."""
from datetime import date

import pytest

from aura_bot.onboarding import (
    ValidationError, age, clean_diets, parse_birthday, parse_list, parse_timezone, profile_prompt,
)

TODAY = date(2026, 10, 12)


@pytest.mark.parametrize("text,expected", [
    ("07.03.1994", date(1994, 3, 7)),
    ("7/3/1994", date(1994, 3, 7)),
    (" 29-02-2000 ", date(2000, 2, 29)),
])
def test_birthday_ok(text, expected):
    assert parse_birthday(text, TODAY) == expected


@pytest.mark.parametrize("text", ["1994-03-07", "31.02.1990", "01.01.1899", "01.01.2030", "вчера", "07.03"])
def test_birthday_bad(text):
    with pytest.raises(ValidationError):
        parse_birthday(text, TODAY)


def test_list_parsing():
    assert parse_list("арахис,  мёд; Арахис\nкиви.") == ["арахис", "мёд", "киви"]
    assert parse_list("нет") == []
    assert parse_list(" , ; ") == []


def test_list_limits():
    with pytest.raises(ValidationError):
        parse_list("x" * 101)
    with pytest.raises(ValidationError):
        parse_list(",".join(f"a{i}" for i in range(21)))


def test_timezone():
    assert parse_timezone(" Asia/Almaty ") == "Asia/Almaty"
    for bad in ["MSK", "+3", "Mars/Olympus", "../etc/passwd"]:
        with pytest.raises(ValidationError):
            parse_timezone(bad)


def test_age_and_diets():
    assert age(date(1994, 10, 13), TODAY) == 31
    assert age(date(1994, 10, 12), TODAY) == 32
    assert clean_diets(["keto", "hack", "vegan"]) == ["vegan", "keto"]


def test_profile_prompt_forbids_allergens():
    p = profile_prompt(date(1994, 3, 7), ["арахис"], ["vegan"], ["бегаю утром"], "Europe/Moscow", TODAY)
    assert "арахис" in p and "НИКОГДА не советуй" in p
    assert "веганство" in p and "бегаю утром" in p and "32 лет" in p and "Europe/Moscow" in p


def test_profile_prompt_empty_sections_omitted():
    p = profile_prompt(None, [], [], [], "Asia/Omsk")
    assert "Аллерг" not in p and "Питание" not in p and "Asia/Omsk" in p
