"""TON-оплата, STT/TTS-клиент, автомиграция старой базы."""
from dataclasses import replace
from datetime import datetime, timedelta, timezone

import httpx
import pytest
from sqlalchemy import inspect, text
from sqlalchemy.ext.asyncio import create_async_engine

from aura_bot.billing import TonClient, create_ton_invoice, settle_ton, ton_link
from aura_bot.db import init_db
from aura_bot.llm import LLMError
from aura_bot.models import TonInvoice
from aura_bot.repo import get_or_create_tg_user
from aura_bot.scheduler import check_ton
from aura_bot.speech import Speech
from conftest import make_services

NOW = datetime(2026, 10, 12, 9, 0, tzinfo=timezone.utc)
WALLET = "UQ" + "A" * 46


@pytest.fixture
def ton_settings(settings):
    return replace(settings, ton_wallet=WALLET, premium_price_ton="2.5")


async def test_ton_invoice_and_settlement(sessionmaker, ton_settings):
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, 1, None, "Europe/Moscow")
        inv = await create_ton_invoice(s, u, ton_settings, NOW)
        assert inv.amount_nano == 2_500_000_000 and inv.comment.startswith("AURA-")
        assert ton_link(ton_settings, inv) == (f"https://app.tonkeeper.com/transfer/{WALLET}"
                                              f"?amount=2500000000&text={inv.comment}")
        txs = [{"hash": "h0", "value": 2_400_000_000, "comment": inv.comment},     # недоплата
               {"hash": "h1", "value": 2_500_000_000, "comment": "AURA-OTHER"}]    # чужой комментарий
        assert await settle_ton(s, ton_settings, txs, NOW) == []
        txs.append({"hash": "h2", "value": 3_000_000_000, "comment": inv.comment})
        paid = await settle_ton(s, ton_settings, txs, NOW)
        assert [p.id for p in paid] == [inv.id] and u.premium_until == NOW + timedelta(days=30)
        assert await settle_ton(s, ton_settings, txs, NOW) == []                 # повтор не продлевает
        old = await create_ton_invoice(s, u, ton_settings, NOW - timedelta(days=3))
        await settle_ton(s, ton_settings, [], NOW)
        assert (await s.get(TonInvoice, old.id)).status == "expired"


async def test_toncenter_client_and_job(sessionmaker, ton_settings):
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, 1, None, "Europe/Moscow")
        inv = await create_ton_invoice(s, u, ton_settings, datetime.now(timezone.utc))

    def handler(req: httpx.Request):
        assert req.url.path.endswith("/getTransactions") and req.url.params["address"] == WALLET
        return httpx.Response(200, json={"ok": True, "result": [
            {"transaction_id": {"hash": "abc"}, "in_msg": {"value": "2500000000", "message": inv.comment}},
            {"transaction_id": {"hash": "def"}, "in_msg": {"value": "oops"}},
            {"in_msg": None},
        ]})

    services = make_services(sessionmaker, ton_settings, None)
    services.ton = TonClient(ton_settings, transport=httpx.MockTransport(handler))
    assert await check_ton(services) == 1
    async with sessionmaker() as s:
        assert (await s.get(TonInvoice, inv.id)).status == "paid"
    services.ton = TonClient(ton_settings, transport=httpx.MockTransport(lambda r: httpx.Response(500)))
    assert await check_ton(services) == 0                                         # сбой сети не роняет job


async def test_speech_client():
    seen = {}

    def handler(req: httpx.Request):
        seen[req.url.path] = req
        if req.url.path.endswith("/audio/transcriptions"):
            assert b'name="model"' in req.content and b"whisper-1" in req.content and b"OggS" in req.content
            return httpx.Response(200, json={"text": " Привет "})
        return httpx.Response(200, content=b"OggS-opus")

    sp = Speech("k", "https://llm.example/v1", "whisper-1", "tts-1", "alloy", transport=httpx.MockTransport(handler))
    assert await sp.transcribe(b"OggS...", "v.ogg", "audio/ogg") == "Привет"
    assert await sp.synthesize("ответ") == b"OggS-opus" and sp.tts_enabled
    with pytest.raises(LLMError):
        await sp.transcribe(b"", "v.ogg")
    bad = Speech("k", "https://x/v1", "w", transport=httpx.MockTransport(lambda r: httpx.Response(200, json={"text": ""})))
    with pytest.raises(LLMError):
        await bad.transcribe(b"x", "v.ogg")
    assert not bad.tts_enabled


async def test_old_database_gets_new_columns(tmp_path):
    """База этапа 1 (без новых колонок users) дополняется при старте без потери данных."""
    eng = create_async_engine(f"sqlite+aiosqlite:///{tmp_path / 'old.db'}")
    async with eng.begin() as c:
        await c.execute(text("CREATE TABLE users (id INTEGER PRIMARY KEY, tg_id BIGINT UNIQUE, email VARCHAR(254), "
                             "password_hash VARCHAR(255), name VARCHAR(128), timezone VARCHAR(64), created_at DATETIME)"))
        await c.execute(text("INSERT INTO users (id, tg_id, timezone) VALUES (1, 42, 'Europe/Moscow')"))
    await init_db(eng)
    async with eng.connect() as c:
        cols = await c.run_sync(lambda sc: {col["name"] for col in inspect(sc).get_columns("users")})
        row = (await c.execute(text("SELECT tg_id, voice_replies FROM users"))).one()
    assert {"tg_username", "voice_replies", "premium_until"} <= cols and tuple(row) == (42, 0)
    await eng.dispose()
