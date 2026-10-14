"""Общие фикстуры тестов AI-сервиса."""

import pytest


@pytest.fixture(autouse=True)
def _loopback_bind(monkeypatch):
    """Тесты считают, что сервис слушает loopback (иначе AURA_AI_INSECURE_DEV не действует).

    Адрес из окружения разработчика (AURA_AI_HOST=0.0.0.0, UVICORN_HOST) не должен
    менять результат тестов; тесты привязки задают его сами.
    """
    monkeypatch.delenv("AURA_AI_HOST", raising=False)
    monkeypatch.delenv("UVICORN_HOST", raising=False)
