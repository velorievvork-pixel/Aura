"""Подписка: дневной лимит сообщений, продление премиума (Stars / TON), идемпотентные платежи."""
from __future__ import annotations

import logging
import secrets
from dataclasses import dataclass
from datetime import date, datetime, timedelta, timezone
from decimal import Decimal
from zoneinfo import ZoneInfo

import httpx
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from .config import Settings
from .models import DailyUsage, Payment, TonInvoice, User

log = logging.getLogger(__name__)
NANO = 10**9
TON_INVOICE_TTL = timedelta(hours=1)


class LimitExceeded(Exception):
    def __init__(self, used: int, limit: int):
        super().__init__(f"{used}/{limit}")
        self.used, self.limit = used, limit


def is_premium(user: User, now: datetime) -> bool:
    return user.premium_until is not None and user.premium_until > now


def _today(user: User, now: datetime) -> date:
    return now.astimezone(ZoneInfo(user.timezone)).date()


@dataclass
class Quota:
    used: int
    limit: int
    premium: bool


async def quota(session: AsyncSession, user: User, settings: Settings, now: datetime) -> Quota:
    row = await session.get(DailyUsage, (user.id, _today(user, now)))
    prem = is_premium(user, now)
    limit = settings.premium_messages_per_day if prem else settings.free_messages_per_day
    return Quota(row.count if row else 0, limit, prem)


async def check_quota(session: AsyncSession, user: User, settings: Settings, now: datetime) -> Quota:
    q = await quota(session, user, settings, now)
    if q.used >= q.limit:
        raise LimitExceeded(q.used, q.limit)
    return q


async def count_message(session: AsyncSession, user: User, now: datetime) -> None:
    """+1 к счётчику дня (вызывается только после успешного ответа LLM)."""
    key = (user.id, _today(user, now))
    row = await session.get(DailyUsage, key)
    if row is None:
        session.add(DailyUsage(user_id=key[0], day=key[1], count=1))
    else:
        row.count += 1
    await session.commit()


async def grant_premium(
    session: AsyncSession, user: User, *, provider: str, amount: int, external_id: str, days: int, now: datetime
) -> bool:
    """Продлевает премиум. False — платёж с таким external_id уже учтён (повторный вебхук)."""
    if await session.scalar(select(Payment).where(Payment.external_id == external_id)):
        return False
    start = user.premium_until if is_premium(user, now) else now
    user.premium_until = start + timedelta(days=days)
    session.add(Payment(user_id=user.id, provider=provider, amount=amount, external_id=external_id, days=days))
    await session.commit()
    return True


# ---------- Telegram Stars ----------

def stars_payload(user: User, settings: Settings) -> str:
    return f"premium:{user.id}:{settings.premium_days}:{settings.premium_price_stars}"


def check_stars_payment(payload: str, currency: str, amount: int, user: User, settings: Settings) -> str | None:
    """Проверка pre_checkout/successful_payment. None — всё верно, иначе причина отказа."""
    if currency != "XTR":
        return "неверная валюта"
    if payload != stars_payload(user, settings) or amount != settings.premium_price_stars:
        return "счёт устарел — запросите новый через /premium"
    return None


# ---------- TON ----------

def ton_amount_nano(settings: Settings) -> int:
    return int(Decimal(settings.premium_price_ton) * NANO)


async def create_ton_invoice(session: AsyncSession, user: User, settings: Settings, now: datetime) -> TonInvoice:
    """Уникальный комментарий к переводу — по нему находим платёж в блокчейне."""
    inv = TonInvoice(
        user_id=user.id, comment="AURA-" + secrets.token_hex(4).upper(),
        amount_nano=ton_amount_nano(settings), expires_at=now + TON_INVOICE_TTL,
    )
    session.add(inv)
    await session.commit()
    return inv


def ton_link(settings: Settings, inv: TonInvoice) -> str:
    """Универсальная ссылка Tonkeeper (открывает кошелёк с заполненным переводом)."""
    return f"https://app.tonkeeper.com/transfer/{settings.ton_wallet}?amount={inv.amount_nano}&text={inv.comment}"


class TonClient:
    """Чтение входящих транзакций кошелька через toncenter API v2."""

    def __init__(self, settings: Settings, transport: httpx.AsyncBaseTransport | None = None):
        headers = {"X-API-Key": settings.toncenter_api_key} if settings.toncenter_api_key else {}
        self._http = httpx.AsyncClient(base_url=settings.toncenter_url.rstrip("/") + "/", headers=headers,
                                       timeout=20, transport=transport)
        self.wallet = settings.ton_wallet

    async def incoming(self, limit: int = 50) -> list[dict]:
        """-> [{hash, value (нанотоны), comment}] входящих переводов."""
        r = await self._http.get("getTransactions", params={"address": self.wallet, "limit": limit})
        r.raise_for_status()
        out = []
        for tx in r.json().get("result", []):
            msg = tx.get("in_msg") or {}
            try:
                out.append({"hash": tx["transaction_id"]["hash"], "value": int(msg.get("value", 0)),
                            "comment": (msg.get("message") or "").strip()})
            except (KeyError, TypeError, ValueError):
                continue
        return out

    async def aclose(self) -> None:
        await self._http.aclose()


async def settle_ton(session: AsyncSession, settings: Settings, txs: list[dict], now: datetime) -> list[TonInvoice]:
    """Сопоставляет транзакции с ожидающими счетами; сумма меньше счёта не засчитывается."""
    pending = (await session.scalars(select(TonInvoice).where(TonInvoice.status == "pending"))).all()
    by_comment = {i.comment: i for i in pending}
    paid: list[TonInvoice] = []
    for tx in txs:
        inv = by_comment.get(tx["comment"])
        if inv is None or tx["value"] < inv.amount_nano:
            continue
        user = await session.get(User, inv.user_id)
        if user and await grant_premium(session, user, provider="ton", amount=tx["value"],
                                        external_id=f"ton:{tx['hash']}", days=settings.premium_days, now=now):
            inv.status = "paid"
            paid.append(inv)
            del by_comment[inv.comment]
    for inv in by_comment.values():
        if inv.expires_at < now - timedelta(days=1):  # сутки запаса на медленный блокчейн
            inv.status = "expired"
    await session.commit()
    return paid


def utc_now() -> datetime:
    return datetime.now(timezone.utc)
