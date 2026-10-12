"""Уведомления во все окна агента: Telegram, FCM (APK в фоне), WebSocket (APK открыт)."""
from __future__ import annotations

import asyncio
import json
import logging
import time
from dataclasses import dataclass, field

import httpx
from aiogram import Bot
from aiogram.types import InlineKeyboardButton, InlineKeyboardMarkup
from sqlalchemy import delete, select
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker

from .models import Device, User

log = logging.getLogger(__name__)


@dataclass
class Note:
    """Одно уведомление: kind — для клиента (task.due, negotiation.*, premium.paid …)."""
    kind: str
    title: str
    body: str
    data: dict = field(default_factory=dict)
    buttons: list[tuple[str, str]] = field(default_factory=list)  # (текст, callback_data) для TG


class FcmSender:
    """FCM HTTP v1: OAuth-токен из сервисного аккаунта (подпись RS256 через google-auth)."""
    SCOPE = "https://www.googleapis.com/auth/firebase.messaging"

    def __init__(self, credentials_file: str, transport: httpx.AsyncBaseTransport | None = None):
        with open(credentials_file, encoding="utf-8") as f:
            self.info = json.load(f)
        self.project = self.info["project_id"]
        self._http = httpx.AsyncClient(timeout=15, transport=transport)
        self._token: str | None = None
        self._exp = 0.0

    async def _access_token(self) -> str:
        if self._token and time.time() < self._exp - 60:
            return self._token
        from google.auth import crypt, jwt  # тяжёлый импорт — только если FCM включён
        now = int(time.time())
        assertion = jwt.encode(crypt.RSASigner.from_service_account_info(self.info), {
            "iss": self.info["client_email"], "scope": self.SCOPE, "aud": self.info["token_uri"],
            "iat": now, "exp": now + 3600,
        })
        r = await self._http.post(self.info["token_uri"], data={
            "grant_type": "urn:ietf:params:oauth:grant-type:jwt-bearer",
            "assertion": assertion.decode() if isinstance(assertion, bytes) else assertion,
        })
        r.raise_for_status()
        body = r.json()
        self._token, self._exp = body["access_token"], now + int(body.get("expires_in", 3600))
        return self._token

    async def send(self, token: str, note: Note) -> bool:
        """False — токен мёртв (приложение удалено), его надо забыть."""
        msg = {"message": {
            "token": token,
            "notification": {"title": note.title, "body": note.body[:1000]},
            "data": {"kind": note.kind, **{k: str(v) for k, v in note.data.items()}},
            "android": {"priority": "high"},
        }}
        r = await self._http.post(
            f"https://fcm.googleapis.com/v1/projects/{self.project}/messages:send",
            json=msg, headers={"Authorization": f"Bearer {await self._access_token()}"},
        )
        if r.status_code == 200:
            return True
        if r.status_code in (400, 404) and ("UNREGISTERED" in r.text or "registration token" in r.text):
            return False
        log.warning("FCM %s: %s", r.status_code, r.text[:200])
        return True

    async def aclose(self) -> None:
        await self._http.aclose()


class WsHub:
    """Открытые WebSocket-соединения APK по пользователям."""

    def __init__(self) -> None:
        self.conns: dict[int, set] = {}

    def add(self, user_id: int, ws) -> None:
        self.conns.setdefault(user_id, set()).add(ws)

    def remove(self, user_id: int, ws) -> None:
        self.conns.get(user_id, set()).discard(ws)

    async def send(self, user_id: int, payload: dict) -> int:
        sent = 0
        for ws in list(self.conns.get(user_id, ())):
            try:
                await ws.send_json(payload)
                sent += 1
            except Exception:  # соединение умерло — убираем
                self.remove(user_id, ws)
        return sent


class Notifier:
    def __init__(self, sessionmaker: async_sessionmaker[AsyncSession], bot: Bot | None = None,
                 fcm: FcmSender | None = None, ws: WsHub | None = None):
        self.sessionmaker, self.bot, self.fcm, self.ws = sessionmaker, bot, fcm, ws or WsHub()

    async def notify(self, user_id: int, note: Note) -> dict[str, int]:
        """Шлёт во все каналы; сбой одного канала не мешает остальным."""
        stats = {"tg": 0, "fcm": 0, "ws": 0}
        async with self.sessionmaker() as s:
            user = await s.get(User, user_id)
            if user is None:
                return stats
            tokens = (await s.scalars(select(Device.fcm_token).where(Device.user_id == user_id))).all()
            tg_id = user.tg_id
        if self.bot and tg_id:
            kb = None
            if note.buttons:
                kb = InlineKeyboardMarkup(inline_keyboard=[[
                    InlineKeyboardButton(text=t, callback_data=d) for t, d in note.buttons]])
            try:
                await self.bot.send_message(tg_id, f"{note.title}\n{note.body}".strip(), reply_markup=kb)
                stats["tg"] = 1
            except Exception as e:  # заблокировали бота и т.п.
                log.warning("TG-уведомление %s не доставлено: %s", user_id, e)
        if self.fcm and tokens:
            results = await asyncio.gather(*(self.fcm.send(t, note) for t in tokens), return_exceptions=True)
            dead = [t for t, ok in zip(tokens, results) if ok is False]
            stats["fcm"] = sum(1 for r in results if r is True)
            if dead:
                async with self.sessionmaker() as s:
                    await s.execute(delete(Device).where(Device.fcm_token.in_(dead)))
                    await s.commit()
        stats["ws"] = await self.ws.send(user_id, {
            "type": "event", "event": note.kind, "title": note.title, "body": note.body, "data": note.data})
        return stats
