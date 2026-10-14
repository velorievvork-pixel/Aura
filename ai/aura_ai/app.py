"""app.py — HTTP-слой Python AI Service.

Эндпоинты, которые вызывает C++-сервер:

* ``POST /v1/agent/run``        — главный запрос Ауры (AgentManager);
* ``POST /v1/agent/negotiate``  — Agent-to-Agent переговоры между двумя Аурами;
* ``POST /v1/agent/slots``      — слоты владельца для приглашения по ссылке;
* ``POST /v1/memory/extract``   — извлечение фактов в долговременную память;
* ``POST /v1/planner/slots``    — свободные слоты (Planner напрямую);
* ``GET  /v1/tools``            — список инструментов (ToolManager).

Запуск: ``AURA_AI_TOKEN=… uvicorn aura_ai.app:app --host 127.0.0.1 --port 8000``

Без ``AURA_AI_TOKEN`` все эндпоинты, кроме ``/healthz``, отвечают 503, если не
задан ``AURA_AI_INSECURE_DEV=1`` (только локальная разработка).
"""

from __future__ import annotations

import base64
import binascii
import logging
import os
import secrets
from datetime import datetime, timedelta
from contextlib import asynccontextmanager
from typing import Any, Dict, List, Optional

from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, ValidationError
from starlette.concurrency import run_in_threadpool
from starlette.datastructures import UploadFile

from . import memory as memory_module
from . import planner
from .agent import AuraAgent
from .config import Settings, get_settings
from .llm import build_llm
from .speech import STTError, BaseSTT, build_stt
from .models import (
    AgentContext,
    AgentRequest,
    AgentResponse,
    Calendar,
    MAX_DURATION_MINUTES,
    MAX_SLOTS,
    MAX_WINDOW_HOURS,
    MemoryEntry,
    NegotiateRequest,
    NegotiateResponse,
    Preferences,
    SlotsRequest as AgentSlotsRequest,
    SlotsResponse as AgentSlotsResponse,
    as_aware,
    utcnow,
)

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")
logger = logging.getLogger("aura.ai")


@asynccontextmanager
async def _lifespan(_: FastAPI):
    settings = get_settings()
    if not settings.service_token:
        if settings.insecure_dev:
            logger.warning(
                "AURA_AI_TOKEN не задан, AURA_AI_INSECURE_DEV=1: эндпоинты открыты без "
                "авторизации — только для локальной разработки, никогда в проде."
            )
        else:
            logger.error(
                "AURA_AI_TOKEN не задан: эндпоинты AI-сервиса закрыты (503). Задайте общий "
                "с C++-сервером токен (openssl rand -hex 32) или для локальной разработки "
                "AURA_AI_INSECURE_DEV=1."
            )
    logger.info(
        "Aura AI Service запущен (llm=%s, stt=%s)",
        build_llm(settings).name,
        build_stt(settings).name,
    )
    yield

app = FastAPI(
    title="Aura AI Service",
    version="0.1.0",
    description="Сеть ИИ-агентов: планирование, память, инструменты, Agent-to-Agent.",
    lifespan=_lifespan,
)

# CORS должен быть добавлен до старта приложения (иначе Starlette ругается
# на изменение стека middleware), поэтому делаем это на уровне модуля.
app.add_middleware(
    CORSMiddleware,
    allow_origins=get_settings().cors_origins or ["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

_agent: Optional[AuraAgent] = None


def get_agent() -> AuraAgent:
    global _agent
    if _agent is None:
        _agent = AuraAgent(get_settings())
    return _agent


def set_agent(agent: Optional[AuraAgent]) -> None:
    """Тесты подменяют агента детерминированной реализацией."""
    global _agent
    _agent = agent


_stt: Optional[BaseSTT] = None


def get_stt() -> BaseSTT:
    global _stt
    if _stt is None:
        _stt = build_stt(get_settings())
    return _stt


def set_stt(stt: Optional[BaseSTT]) -> None:
    """Тесты подменяют STT детерминированной реализацией."""
    global _stt
    _stt = stt


def _settings() -> Settings:
    return get_settings()


def require_token(x_aura_token: str = Header(default="")) -> None:
    settings = get_settings()
    expected = settings.service_token
    if not expected:
        # Fail closed: без токена сервис открыт только при явном
        # AURA_AI_INSECURE_DEV=1 (локальная разработка).
        if settings.insecure_dev:
            return
        raise HTTPException(status_code=503, detail="AI service token is not configured")
    # compare_digest по байтам: строки с не-ASCII символами compare_digest не
    # принимает (TypeError → 500), а заголовок приходит от клиента.
    if not secrets.compare_digest(x_aura_token.encode("utf-8"), expected.encode("utf-8")):
        raise HTTPException(status_code=401, detail="invalid service token")



# ----------------------------------------------------------------- сервисные
@app.get("/healthz")
def healthz() -> Dict[str, Any]:
    """Публичная проверка живости: только типы бэкендов, без DSN, путей и ключей."""
    settings = get_settings()
    agent = get_agent()
    return {
        "status": "ok",
        "service": "aura-ai",
        "llm": agent.llm.name,
        "stt": get_stt().name,
        "tools_backend": settings.tools_backend,
        "memory_backend": settings.memory_backend_kind,
        "time": utcnow().isoformat(),
    }


@app.get("/v1/tools", dependencies=[Depends(require_token)])
def list_tools() -> Dict[str, Any]:
    return {"tools": get_agent().tools.list()}


# ------------------------------------------------------------------- агент
@app.post("/v1/agent/run", response_model=AgentResponse, dependencies=[Depends(require_token)])
def agent_run(request: AgentRequest) -> AgentResponse:
    agent = get_agent()
    return agent.run(request)


@app.post("/v1/agent/negotiate", response_model=NegotiateResponse, dependencies=[Depends(require_token)])
def agent_negotiate(request: NegotiateRequest) -> NegotiateResponse:
    return get_agent().negotiate(request)


@app.post("/v1/agent/slots", response_model=AgentSlotsResponse, dependencies=[Depends(require_token)])
def agent_slots(request: AgentSlotsRequest) -> AgentSlotsResponse:
    """Слоты владельца для приглашения по ссылке (invite.create на C++-сервере)."""
    try:
        return get_agent().offer_slots(request)
    except OverflowError as exc:  # context.now у самой границы datetime.max
        raise HTTPException(status_code=422, detail="time is out of range") from exc


# -------------------------------------------------------------------- речь
class TranscribeResponse(BaseModel):
    text: str
    language: str = ""
    provider: str = ""


# JSON-путь несёт base64 (+~33%) и служебные поля; multipart — заголовки частей.
_BODY_OVERHEAD = 64 * 1024


def _body_limit(settings: Settings) -> int:
    return settings.stt_max_bytes * 4 // 3 + _BODY_OVERHEAD


async def _read_body_limited(request: Request, limit: int) -> bytes:
    """Читает тело потоком и обрывает его на ``limit`` байтах (413).

    Content-Length проверяется до чтения; для chunked-тела предел соблюдается
    по мере чтения, а не после того, как всё тело уже в памяти.
    """
    declared = request.headers.get("content-length")
    if declared is not None:
        try:
            if int(declared) > limit:
                raise HTTPException(status_code=413, detail="audio too large")
        except ValueError as exc:
            raise HTTPException(status_code=400, detail="invalid content-length") from exc
    chunks: List[bytes] = []
    total = 0
    async for chunk in request.stream():
        total += len(chunk)
        if total > limit:
            raise HTTPException(status_code=413, detail="audio too large")
        chunks.append(chunk)
    body = b"".join(chunks)
    # Starlette берёт уже прочитанное тело из request._body (form()/json()).
    request._body = body
    return body


async def _read_audio(request: Request, limit: int) -> tuple[bytes, str, str]:
    """Читает аудио из запроса: multipart/form-data (Whisper) либо JSON (base64).

    Возвращает ``(audio_bytes, language, filename)``.
    """
    await _read_body_limited(request, limit)
    ctype = request.headers.get("content-type", "")
    if ctype.startswith("multipart/form-data"):
        try:
            form = await request.form()
        except Exception as exc:  # битый multipart → 400, а не 500
            raise HTTPException(status_code=400, detail="invalid multipart body") from exc
        upload = form.get("file")
        if upload is None:
            raise HTTPException(status_code=400, detail="no audio file")
        if not isinstance(upload, UploadFile):
            raise HTTPException(status_code=400, detail="file must be an uploaded file")
        audio = await upload.read()
        language = form.get("language", "")
        language = language if isinstance(language, str) else ""
        filename = upload.filename or "audio.webm"
        return audio, language[:16], filename[:128]
    # JSON-путь (внутренний C++-клиент шлёт base64, чтобы не строить multipart).
    try:
        payload = await request.json()
    except Exception as exc:
        raise HTTPException(status_code=400, detail="invalid request body") from exc
    if not isinstance(payload, dict):
        raise HTTPException(status_code=400, detail="request body must be a JSON object")
    b64 = payload.get("audio") or payload.get("audio_base64") or ""
    if not b64:
        raise HTTPException(status_code=400, detail="no audio data")
    if not isinstance(b64, str):
        raise HTTPException(status_code=400, detail="audio must be a base64 string")
    try:
        audio = base64.b64decode(b64, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise HTTPException(status_code=400, detail="invalid base64 audio") from exc
    language = payload.get("language", "")
    filename = payload.get("filename", "")
    language = language if isinstance(language, str) else ""
    filename = filename if isinstance(filename, str) and filename else "audio.webm"
    return audio, language[:16], filename[:128]


@app.post("/v1/speech/transcribe", response_model=TranscribeResponse, dependencies=[Depends(require_token)])
async def speech_transcribe(request: Request) -> TranscribeResponse:
    """Распознавание речи (STT).

    Whisper-совместимый вход: ``multipart/form-data`` с полем ``file`` и
    необязательным ``language`` (ru/en/auto) — как OpenAI
    ``/v1/audio/transcriptions``. Дополнительно принимает JSON с base64-аудио
    (внутренний путь C++-сервера). Ответ: ``{text, language, provider}``.
    """
    settings = get_settings()
    audio, language, filename = await _read_audio(request, _body_limit(settings))
    if not audio:
        raise HTTPException(status_code=400, detail="empty audio")
    if len(audio) > settings.stt_max_bytes:
        raise HTTPException(status_code=413, detail="audio too large")

    stt = get_stt()
    lang = language or settings.stt_default_language
    try:
        # Бэкенд синхронный (httpx.post) — в пул потоков, чтобы не блокировать
        # event loop на время распознавания.
        result = await run_in_threadpool(stt.transcribe, audio, lang, filename)
    except STTError as exc:
        # Бэкенд недоступен/ошибся — отдаём 502, клиент покажет fallback на текст.
        logger.warning("STT error: %s", exc)
        raise HTTPException(status_code=502, detail="speech backend unavailable") from exc
    return TranscribeResponse(
        text=result.get("text", ""),
        language=result.get("language", ""),
        provider=stt.name,
    )


# ------------------------------------------------------------------- память
class MemoryExtractRequest(BaseModel):
    user_key: str = Field(default="", max_length=320)
    message: str = ""
    context: AgentContext = Field(default_factory=AgentContext)
    commit: bool = True


class MemoryExtractResponse(BaseModel):
    entries: List[MemoryEntry]
    stored: List[MemoryEntry]


@app.post("/v1/memory/extract", response_model=MemoryExtractResponse, dependencies=[Depends(require_token)])
def memory_extract(request: MemoryExtractRequest) -> MemoryExtractResponse:
    agent = get_agent()
    context = request.context.model_copy(update={"message": request.message or request.context.message})
    entries = memory_module.extract_from_context(context)
    stored: List[MemoryEntry] = []
    if entries and request.commit:
        key = request.user_key or memory_module.user_key(context)
        stored = agent.store.save(key, entries)
    return MemoryExtractResponse(entries=entries, stored=stored)


@app.get("/v1/memory/{user_key}", dependencies=[Depends(require_token)])
def memory_load(user_key: str, query: str = "") -> Dict[str, Any]:
    agent = get_agent()
    entries = agent.store.load(user_key)
    if query:
        # Гибридный RAG: BM25 всегда + семантика, если настроены эмбеддинги.
        entries = memory_module.rank(query, entries, embed_client=agent.embed_client)
    return {"user_key": user_key, "entries": memory_module.as_dicts(entries)}


# ---------------------------------------------------------------- планировщик
class SlotsWindow(BaseModel):
    start: Optional[datetime] = None
    end: Optional[datetime] = None
    label: str = Field(default="окно", max_length=64)


class SlotsRequest(BaseModel):
    window: Optional[SlotsWindow] = None
    calendar: Calendar = Field(default_factory=list)
    preferences: Preferences = Field(default_factory=Preferences)
    duration_minutes: int = Field(default=60, ge=1, le=MAX_DURATION_MINUTES)
    max_slots: int = Field(default=5, ge=1, le=MAX_SLOTS)


@app.post("/v1/planner/slots", dependencies=[Depends(require_token)])
def planner_slots(request: SlotsRequest) -> Dict[str, Any]:
    now = utcnow()
    window = request.window or SlotsWindow()
    # Время без пояса — UTC (иначе сравнение с календарём падает в 500).
    start = as_aware(window.start) if window.start else now
    try:
        end = as_aware(window.end) if window.end else start + timedelta(hours=24)
    except OverflowError as exc:
        raise HTTPException(status_code=422, detail="window is out of range") from exc
    if end <= start:
        raise HTTPException(status_code=422, detail="window.end must be after window.start")
    if end - start > timedelta(hours=MAX_WINDOW_HOURS):
        raise HTTPException(status_code=422, detail=f"window is longer than {MAX_WINDOW_HOURS} hours")
    time_window = planner.TimeWindow(start=start, end=end, label=window.label)
    try:
        slots = planner.free_slots(
            time_window,
            request.calendar,
            request.preferences,
            duration_minutes=request.duration_minutes,
            max_slots=request.max_slots,
        )
    except OverflowError as exc:  # окно у самой границы datetime.max
        raise HTTPException(status_code=422, detail="window is out of range") from exc
    return {
        "window": time_window.as_dict(),
        "slots": [slot.model_dump(mode="json") for slot in slots],
    }


class UnderstandRequest(BaseModel):
    text: str = Field(max_length=4000)
    now: Optional[Any] = None


@app.post("/v1/planner/understand", dependencies=[Depends(require_token)])
def planner_understand(request: UnderstandRequest) -> Dict[str, Any]:
    moment = _parse_dt(request.now)
    try:
        return planner.understand(request.text, moment).as_dict()
    except OverflowError as exc:  # «через 10^9 недель»
        raise HTTPException(status_code=422, detail="time is out of range") from exc


def _parse_dt(value: Any) -> Optional[datetime]:
    """ISO-время из запроса (всегда с поясом); мусор → None (берём «сейчас»)."""
    if not value:
        return None
    if isinstance(value, datetime):
        return as_aware(value)
    if not isinstance(value, str):
        return None
    try:
        return as_aware(datetime.fromisoformat(value))
    except ValueError:
        return None


# ------------------------------------------------------------------- превью
def _preview_dir() -> str:
    configured = get_settings().preview_dir
    if configured:
        return configured
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "preview"))


def mount_preview() -> None:
    """Отдаёт HTML-превью дизайна Qt-клиента (та же палитра и «стеклянные» кнопки)."""
    directory = _preview_dir()
    if not os.path.isdir(directory):
        logger.warning("preview dir not found: %s", directory)
        return

    app.mount("/static", StaticFiles(directory=directory), name="preview-static")

    @app.get("/", include_in_schema=False)
    def preview_index() -> FileResponse:
        return FileResponse(os.path.join(directory, "index.html"))


@app.exception_handler(ValidationError)
async def invalid_data(_request, exc: ValidationError) -> JSONResponse:
    """Ошибка валидации внутри обработчика (не тела запроса) — 422 без деталей."""
    logger.info("validation error: %d errors", exc.error_count())
    return JSONResponse(status_code=422, content={"detail": "invalid input"})


@app.exception_handler(Exception)
async def unhandled(_request, exc: Exception) -> JSONResponse:
    # Текст исключения — только в лог: он может содержать DSN, пути и данные.
    logger.exception("unhandled error")
    return JSONResponse(status_code=500, content={"detail": "internal error"})


mount_preview()


def main() -> None:  # pragma: no cover
    import uvicorn

    settings = get_settings()
    uvicorn.run(app, host=settings.host, port=settings.port)


if __name__ == "__main__":  # pragma: no cover
    main()
