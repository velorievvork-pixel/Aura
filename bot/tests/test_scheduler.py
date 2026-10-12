"""Планировщик напоминаний и каналы уведомлений."""
from datetime import datetime, timedelta, timezone

import httpx
from aiogram.methods import SendMessage
from sqlalchemy import select

from aura_bot.models import Device, Task
from aura_bot.notifier import FcmSender, Note, Notifier, WsHub
from aura_bot.repo import create_task, get_or_create_tg_user
from aura_bot.scheduler import dispatch_due
from conftest import TgHarness

NOW = datetime(2026, 10, 12, 9, 0, tzinfo=timezone.utc)


async def test_due_tasks_sent_once_and_repeats_rescheduled(sessionmaker, llm, settings):
    tg = TgHarness(sessionmaker, llm=llm, settings=settings)
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        once = await create_task(s, u, NOW - timedelta(minutes=1), "Купить хлеб")
        daily = await create_task(s, u, NOW - timedelta(minutes=1), "Витамины", "daily")
        future = await create_task(s, u, NOW + timedelta(hours=1), "Позже")
    assert await dispatch_due(tg.services, NOW) == 2
    sent = [c for c in tg.fake.calls if isinstance(c, SendMessage)]
    assert any("Купить хлеб" in c.text and c.reply_markup for c in sent)
    assert await dispatch_due(tg.services, NOW) == 0          # повторно не шлём
    async with sessionmaker() as s:
        assert (await s.get(Task, once.id)).status == "sent"
        d = await s.get(Task, daily.id)
        assert d.status == "pending" and d.due_at == NOW - timedelta(minutes=1) + timedelta(days=1)
        assert (await s.get(Task, future.id)).status == "pending"


async def test_late_reminder_marked(sessionmaker, llm, settings):
    tg = TgHarness(sessionmaker, llm=llm, settings=settings)
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        await create_task(s, u, NOW - timedelta(hours=3), "Старое")
    await dispatch_due(tg.services, NOW)
    assert "с опозданием" in [c for c in tg.fake.calls if isinstance(c, SendMessage)][-1].text


class FakeFcm:
    def __init__(self):
        self.sent = []

    async def send(self, token, note):
        self.sent.append((token, note.kind))
        return token != "dead-token-xxxxxxxxxxxxxxx"


class FakeWs:
    def __init__(self, broken=False):
        self.got, self.broken = [], broken

    async def send_json(self, payload):
        if self.broken:
            raise RuntimeError("closed")
        self.got.append(payload)


async def test_notifier_fans_out_and_drops_dead_tokens(sessionmaker):
    fcm, hub = FakeFcm(), WsHub()
    async with sessionmaker() as s:
        u = await get_or_create_tg_user(s, 1, None, "Europe/Moscow")
        s.add_all([Device(user_id=u.id, fcm_token="good-token-xxxxxxxxxxxxxxx"),
                   Device(user_id=u.id, fcm_token="dead-token-xxxxxxxxxxxxxxx")])
        await s.commit()
    ok, broken = FakeWs(), FakeWs(broken=True)
    hub.add(u.id, ok)
    hub.add(u.id, broken)
    stats = await Notifier(sessionmaker, None, fcm, hub).notify(u.id, Note("task.due", "⏰", "Хлеб", {"task_id": 1}))
    assert stats == {"tg": 0, "fcm": 1, "ws": 1}
    assert ok.got[0]["event"] == "task.due" and broken not in hub.conns[u.id]
    async with sessionmaker() as s:
        assert (await s.scalars(select(Device.fcm_token))).all() == ["good-token-xxxxxxxxxxxxxxx"]


async def test_fcm_sender_payload_and_unregistered(tmp_path):
    import json
    cred = tmp_path / "sa.json"
    cred.write_text(json.dumps({"project_id": "aura-p", "client_email": "x@y", "token_uri": "https://oauth2/token",
                                "private_key": "unused"}))
    seen = []

    def handler(req: httpx.Request):
        seen.append(json.loads(req.content))
        if seen[-1]["message"]["token"] == "gone":
            return httpx.Response(404, json={"error": {"status": "NOT_FOUND", "details": [{"errorCode": "UNREGISTERED"}]}})
        assert req.headers["authorization"] == "Bearer cached"
        assert str(req.url) == "https://fcm.googleapis.com/v1/projects/aura-p/messages:send"
        return httpx.Response(200, json={"name": "ok"})

    fcm = FcmSender(str(cred), transport=httpx.MockTransport(handler))
    fcm._token, fcm._exp = "cached", 10**12  # без подписи JWT в тесте
    assert await fcm.send("tok", Note("task.due", "⏰ Напоминание", "Хлеб", {"task_id": 7})) is True
    assert seen[0]["message"]["data"] == {"kind": "task.due", "task_id": "7"}
    assert await fcm.send("gone", Note("x", "t", "b")) is False
