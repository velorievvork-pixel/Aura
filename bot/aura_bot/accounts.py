"""Аккаунты APK: email + пароль (Argon2id), сессии-токены, привязка Telegram одноразовым кодом."""
from __future__ import annotations

import hashlib
import re
import secrets
import time
from datetime import datetime, timedelta

from argon2 import PasswordHasher
from argon2.exceptions import InvalidHashError, VerificationError
from sqlalchemy import delete, select, update
from sqlalchemy.ext.asyncio import AsyncSession

from .models import (
    ApiSession, ChatMessage, DailyUsage, Device, LinkCode, Memory, Negotiation, Payment, Profile, Task,
    TonInvoice, User,
)

_ph = PasswordHasher()  # параметры по умолчанию argon2-cffi — Argon2id (RFC 9106, low-memory профиль)
EMAIL_RE = re.compile(r"^[^@\s]{1,64}@[^@\s]+\.[^@\s]{2,}$")
LINK_TTL = timedelta(minutes=10)
LINK_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"  # без похожих 0/O, 1/I


class AuthError(ValueError):
    """Текст — для пользователя (без подсказок, существует ли email)."""


def normalize_email(email: str) -> str:
    e = email.strip().lower()
    if len(e) > 254 or not EMAIL_RE.match(e):
        raise AuthError("Некорректный email")
    return e


def check_password_policy(pw: str) -> None:
    if not (8 <= len(pw) <= 128):
        raise AuthError("Пароль: от 8 до 128 символов")
    if not (re.search(r"[^\W\d_]", pw) and re.search(r"\d", pw)):
        raise AuthError("Пароль должен содержать буквы и цифры")


def hash_password(pw: str) -> str:
    return _ph.hash(pw)


def verify_password(stored: str | None, pw: str) -> bool:
    if not stored:
        _ph.hash(pw)  # выравниваем время ответа для несуществующих аккаунтов
        return False
    try:
        return _ph.verify(stored, pw)
    except (VerificationError, InvalidHashError):
        return False


def _sha(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


async def register(session: AsyncSession, email: str, password: str, tz: str) -> User:
    email = normalize_email(email)
    check_password_policy(password)
    if await session.scalar(select(User).where(User.email == email)):
        raise AuthError("Этот email уже зарегистрирован")
    user = User(email=email, password_hash=hash_password(password), timezone=tz)
    session.add(user)
    await session.commit()
    await session.refresh(user, ["profile"])
    return user


async def login(session: AsyncSession, email: str, password: str) -> User:
    try:
        email = normalize_email(email)
    except AuthError:
        raise AuthError("Неверный email или пароль") from None
    user = await session.scalar(select(User).where(User.email == email))
    if not verify_password(user.password_hash if user else None, password):
        raise AuthError("Неверный email или пароль")
    if _ph.check_needs_rehash(user.password_hash):
        user.password_hash = hash_password(password)
        await session.commit()
    return user


async def new_session(session: AsyncSession, user: User) -> str:
    """Токен 256 бит; в БД — только хэш, утечка базы не даёт войти."""
    token = secrets.token_urlsafe(32)
    session.add(ApiSession(user_id=user.id, token_hash=_sha(token)))
    await session.commit()
    return token


async def user_by_token(session: AsyncSession, token: str, now: datetime) -> User | None:
    s = await session.scalar(select(ApiSession).where(ApiSession.token_hash == _sha(token)))
    if s is None:
        return None
    if now - s.last_used_at > timedelta(minutes=5):  # не пишем в БД на каждый запрос
        s.last_used_at = now
        await session.commit()
    user = await session.get(User, s.user_id)
    if user is not None:
        await session.refresh(user, ["profile"])
    return user


async def end_session(session: AsyncSession, token: str) -> None:
    await session.execute(delete(ApiSession).where(ApiSession.token_hash == _sha(token)))
    await session.commit()


# ---------- привязка Telegram ----------

async def new_link_code(session: AsyncSession, user: User, now: datetime) -> str:
    await session.execute(delete(LinkCode).where(LinkCode.user_id == user.id))
    code = "".join(secrets.choice(LINK_ALPHABET) for _ in range(8))
    session.add(LinkCode(code=code, user_id=user.id, expires_at=now + LINK_TTL))
    await session.commit()
    return code


async def merge_users(session: AsyncSession, source: User, target: User) -> User:
    """Переносит всё из source (аккаунт APK) в target (аккаунт TG) и удаляет source."""
    if target.email and source.email and target.email != source.email:
        raise AuthError("К этому Telegram уже привязан другой email")
    sid, tid = source.id, target.id
    for model in (Memory, ChatMessage, Task, Device, ApiSession, Payment, TonInvoice):
        await session.execute(update(model).where(model.user_id == sid).values(user_id=tid))
    await session.execute(update(Negotiation).where(Negotiation.initiator_id == sid).values(initiator_id=tid))
    await session.execute(update(Negotiation).where(Negotiation.counterpart_id == sid).values(counterpart_id=tid))
    await session.execute(delete(DailyUsage).where(DailyUsage.user_id == sid))
    await session.execute(delete(LinkCode).where(LinkCode.user_id == sid))
    src_profile = await session.get(Profile, sid)
    tgt_profile = await session.get(Profile, tid)
    if src_profile is not None:
        if tgt_profile is None or tgt_profile.completed_at is None:
            if tgt_profile is not None:
                await session.delete(tgt_profile)
                await session.flush()
            await session.execute(update(Profile).where(Profile.user_id == sid).values(user_id=tid))
        else:
            await session.delete(src_profile)
    email, pw = source.email, source.password_hash
    if source.premium_until and (not target.premium_until or source.premium_until > target.premium_until):
        target.premium_until = source.premium_until
    session.expunge(source)
    await session.execute(delete(User).where(User.id == sid))
    await session.flush()
    target.email, target.password_hash = target.email or email, target.password_hash or pw
    await session.commit()
    await session.refresh(target)  # колонки и профиль заново из БД после массовых UPDATE
    await session.refresh(target, ["profile"])
    return target


async def link_telegram(session: AsyncSession, apk_user: User, code: str, now: datetime) -> User:
    """APK вводит код из /link: аккаунты сливаются в один — общая память и задачи в обоих окнах."""
    row = await session.get(LinkCode, code.strip().upper())
    if row is None or row.expires_at < now:
        raise AuthError("Код неверный или истёк — получите новый командой /link в боте")
    await session.delete(row)
    await session.commit()
    if row.user_id == apk_user.id:
        return apk_user
    tg_user = await session.get(User, row.user_id)
    if tg_user is None or tg_user.tg_id is None:
        raise AuthError("Код неверный или истёк — получите новый командой /link в боте")
    return await merge_users(session, apk_user, tg_user)


class RateLimiter:
    """Простой ограничитель попыток входа в памяти процесса (ключ — email и IP)."""

    def __init__(self, max_attempts: int = 5, window_s: int = 900):
        self.max, self.window = max_attempts, window_s
        self.hits: dict[str, list[float]] = {}

    def blocked(self, key: str) -> bool:
        now = time.monotonic()
        hits = [t for t in self.hits.get(key, []) if now - t < self.window]
        self.hits[key] = hits
        return len(hits) >= self.max

    def fail(self, key: str) -> None:
        self.hits.setdefault(key, []).append(time.monotonic())

    def reset(self, key: str) -> None:
        self.hits.pop(key, None)
