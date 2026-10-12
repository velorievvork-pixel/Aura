"""Голос: STT (Whisper-совместимый /audio/transcriptions) и TTS (/audio/speech, ogg/opus для TG)."""
from __future__ import annotations

import httpx

from .llm import LLMError

MAX_AUDIO_BYTES = 20 * 1024 * 1024  # лимит скачивания файлов ботом и разумный предел для Whisper
MAX_TTS_CHARS = 1500


class Speech:
    def __init__(self, api_key: str, base_url: str, stt_model: str, tts_model: str = "", tts_voice: str = "alloy",
                 transport: httpx.AsyncBaseTransport | None = None):
        self.stt_model, self.tts_model, self.tts_voice = stt_model, tts_model, tts_voice
        self._http = httpx.AsyncClient(base_url=base_url.rstrip("/") + "/", timeout=120, transport=transport,
                                       headers={"Authorization": f"Bearer {api_key}"})

    @property
    def tts_enabled(self) -> bool:
        return bool(self.tts_model)

    async def transcribe(self, audio: bytes, filename: str, mime: str = "application/octet-stream") -> str:
        if not audio:
            raise LLMError("пустой аудиофайл")
        if len(audio) > MAX_AUDIO_BYTES:
            raise LLMError("аудио длиннее допустимого")
        try:
            r = await self._http.post("audio/transcriptions", data={"model": self.stt_model},
                                      files={"file": (filename, audio, mime)})
        except httpx.HTTPError as e:
            raise LLMError(f"STT сеть: {e.__class__.__name__}") from e
        if r.status_code != 200:
            raise LLMError(f"STT HTTP {r.status_code}")
        try:
            text = r.json().get("text", "")
        except ValueError:
            raise LLMError("STT: не JSON") from None
        if not isinstance(text, str) or not text.strip():
            raise LLMError("речь не распознана")
        return text.strip()

    async def synthesize(self, text: str) -> bytes:
        """-> ogg/opus (формат голосовых Telegram)."""
        try:
            r = await self._http.post("audio/speech", json={
                "model": self.tts_model, "voice": self.tts_voice, "input": text[:MAX_TTS_CHARS],
                "response_format": "opus"})
        except httpx.HTTPError as e:
            raise LLMError(f"TTS сеть: {e.__class__.__name__}") from e
        if r.status_code != 200 or not r.content:
            raise LLMError(f"TTS HTTP {r.status_code}")
        return r.content

    async def aclose(self) -> None:
        await self._http.aclose()
