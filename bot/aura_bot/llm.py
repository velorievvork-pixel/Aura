"""Клиент OpenAI-совместимого Chat Completions API (ключ и адрес — только из env)."""
from __future__ import annotations

import logging
from typing import Protocol

import httpx

log = logging.getLogger(__name__)


class LLMError(RuntimeError):
    """LLM недоступна или вернула мусор — наружу отдаём вежливую ошибку."""


class LLM(Protocol):
    async def complete(self, messages: list[dict[str, str]], *, json_mode: bool = False) -> str: ...


class OpenAIChat:
    def __init__(
        self,
        api_key: str,
        base_url: str,
        model: str,
        *,
        json_mode: bool = True,
        timeout: float = 60.0,
        transport: httpx.AsyncBaseTransport | None = None,
    ) -> None:
        self.model = model
        self.json_mode = json_mode  # часть совместимых провайдеров не знает response_format
        self._http = httpx.AsyncClient(
            base_url=base_url.rstrip("/") + "/",
            headers={"Authorization": f"Bearer {api_key}"},
            timeout=timeout,
            transport=transport,
        )

    async def complete(self, messages: list[dict[str, str]], *, json_mode: bool = False) -> str:
        body: dict = {"model": self.model, "messages": messages, "temperature": 0.2 if json_mode else 0.7}
        if json_mode and self.json_mode:
            body["response_format"] = {"type": "json_object"}
        try:
            r = await self._http.post("chat/completions", json=body)
        except httpx.HTTPError as e:
            raise LLMError(f"сеть: {e.__class__.__name__}") from e
        if r.status_code != 200:
            log.warning("LLM HTTP %s: %s", r.status_code, r.text[:300])
            raise LLMError(f"HTTP {r.status_code}")
        try:
            content = r.json()["choices"][0]["message"]["content"]
        except (ValueError, KeyError, IndexError, TypeError) as e:
            raise LLMError("неожиданный формат ответа") from e
        if not isinstance(content, str) or not content.strip():
            raise LLMError("пустой ответ")
        return content.strip()

    async def aclose(self) -> None:
        await self._http.aclose()
