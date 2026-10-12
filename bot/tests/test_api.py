"""HTTP API для APK: тот же агент и те же проверки, что в Telegram."""
from dataclasses import replace

import httpx
import pytest
from sqlalchemy import select

from aura_bot.api import create_app
from aura_bot.models import Device, Memory, Task, User
from aura_bot.repo import add_memories, get_or_create_tg_user
from conftest import FakeSpeech, TgHarness, chat_json, local_iso, make_services, onboard

PW = "secret123"


@pytest.fixture
def services(sessionmaker, llm, settings):
    return make_services(sessionmaker, settings, llm, speech=FakeSpeech(text="напомни про хлеб"))


@pytest.fixture
async def client(services):
    transport = httpx.ASGITransport(app=create_app(services))
    async with httpx.AsyncClient(transport=transport, base_url="http://aura") as c:
        yield c


async def register(client, email="anya@example.com", pw=PW):
    r = await client.post("/api/auth/register", json={"email": email, "password": pw, "timezone": "Europe/Moscow"})
    assert r.status_code == 200, r.text
    return {"Authorization": f"Bearer {r.json()['token']}"}


PROFILE = {"birthday": "1994-03-07", "allergies": ["арахис"], "diets": ["vegan"], "habits": ["бег"],
           "timezone": "Europe/Moscow"}


async def onboarded_client(client, **kw):
    h = await register(client, **kw)
    r = await client.put("/api/profile", json=PROFILE, headers=h)
    assert r.status_code == 200 and r.json()["onboarded"]
    return h


@pytest.mark.parametrize("email,pw,msg", [
    ("bad", PW, "email"), ("a@b.co", "short1", "8"), ("a@b.co", "onlyletters", "цифры"), ("a@b.co", "12345678", "цифры"),
])
async def test_register_policy(client, email, pw, msg):
    r = await client.post("/api/auth/register", json={"email": email, "password": pw})
    assert r.status_code == 400 and msg in r.json()["error"]["message"]


async def test_register_login_me_logout(client, sessionmaker):
    h = await register(client)
    assert (await client.post("/api/auth/register", json={"email": "ANYA@example.com", "password": PW})).status_code == 400
    async with sessionmaker() as s:
        u = (await s.scalars(select(User))).one()
        assert u.password_hash.startswith("$argon2id$") and PW not in u.password_hash
    me = (await client.get("/api/me", headers=h)).json()
    assert me["email"] == "anya@example.com" and not me["onboarded"] and not me["telegram_linked"]
    r = await client.post("/api/auth/login", json={"email": "anya@example.com", "password": PW})
    h2 = {"Authorization": f"Bearer {r.json()['token']}"}
    await client.post("/api/auth/logout", headers=h2)
    assert (await client.get("/api/me", headers=h2)).status_code == 401
    assert (await client.get("/api/me", headers=h)).status_code == 200      # другая сессия жива
    assert (await client.get("/api/me")).status_code == 401
    assert (await client.get("/api/me", headers={"Authorization": "Bearer " + "x" * 40})).status_code == 401


async def test_login_rate_limited(client):
    await register(client)
    for _ in range(5):
        r = await client.post("/api/auth/login", json={"email": "anya@example.com", "password": "wrong123"})
        assert r.status_code == 401 and "Неверный email или пароль" in r.json()["error"]["message"]
    r = await client.post("/api/auth/login", json={"email": "anya@example.com", "password": PW})
    assert r.status_code == 429


async def test_profile_validation(client):
    h = await register(client)
    for bad in [{**PROFILE, "diets": ["hack"]}, {**PROFILE, "timezone": "MSK"}, {**PROFILE, "birthday": "1800-01-01"},
                {**PROFILE, "allergies": ["я" * 200]}]:
        r = await client.put("/api/profile", json=bad, headers=h)
        assert r.status_code in (400, 422), bad
    assert (await client.post("/api/chat", json={"text": "привет"}, headers=h)).status_code == 409
    opts = (await client.get("/api/options")).json()
    assert {"code": "vegan", "title": "веганство"} in opts["diets"]


async def test_chat_with_task_history_and_memory(client, llm, sessionmaker):
    h = await onboarded_client(client)
    llm.answer = chat_json("Хорошо!", {"datetime": local_iso(1, 9), "text": "Купить хлеб"})
    llm.extraction = '{"memories": [{"kind": "preference", "text": "Любит ржаной хлеб"}]}'
    r = await client.post("/api/chat", json={"text": "напомни завтра в 9 купить хлеб"}, headers=h)
    body = r.json()
    assert r.status_code == 200 and body["task"]["text"] == "Купить хлеб" and "завтра в 09:00" in body["task"]["due_human"]
    assert body["usage"] == {"used": 1, "limit": 20}
    assert "арахис" in llm.calls[0][0][0]["content"]                       # анкета из APK применяется
    hist = (await client.get("/api/chat/history", headers=h)).json()
    assert [m["role"] for m in hist] == ["user", "assistant"]
    mems = (await client.get("/api/memories", headers=h)).json()           # память выучена в фоне
    assert mems[0]["text"] == "Любит ржаной хлеб"
    tasks = (await client.get("/api/tasks", headers=h)).json()
    tid = tasks[0]["id"]
    assert (await client.post(f"/api/tasks/{tid}/snooze", headers=h)).json()["status"] == "pending"
    assert (await client.post(f"/api/tasks/{tid}/done", headers=h)).json()["status"] == "done"
    assert (await client.delete(f"/api/tasks/{tid}", headers=h)).status_code == 404
    r = await client.post("/api/tasks", json={"datetime": "2020-01-01T10:00", "text": "x"}, headers=h)
    assert r.status_code == 400 and "прошло" in r.json()["error"]["message"]
    r = await client.post("/api/tasks", json={"datetime": local_iso(3, 10), "text": "Йога", "repeat": "weekly"}, headers=h)
    assert r.json()["repeat"] == "weekly"


async def test_memory_delete_and_wipe_needs_confirmation(client, sessionmaker):
    h = await onboarded_client(client)
    h_other = await onboarded_client(client, email="oleg@example.com")
    async with sessionmaker() as s:
        u = await s.scalar(select(User).where(User.email == "anya@example.com"))
        m1, m2 = await add_memories(s, u, [("fact", "Живёт в Казани"), ("contact", "Жена Маша")])
    assert (await client.delete(f"/api/memories/{m1.id}", headers=h_other)).status_code == 404   # чужая
    assert (await client.delete(f"/api/memories/{m1.id}", headers=h)).status_code == 200
    assert (await client.post("/api/memories/wipe", json={"confirm": "yes"}, headers=h)).status_code == 400
    assert (await client.post("/api/memories/wipe", json={"confirm": "WIPE"}, headers=h)).json() == {"deleted": 1}


async def test_limits_and_errors(services, llm):
    services.settings = replace(services.settings, free_messages_per_day=1)
    async with httpx.AsyncClient(transport=httpx.ASGITransport(app=create_app(services)), base_url="http://a") as c:
        h = await onboarded_client(c)
        assert (await c.post("/api/chat", json={"text": "1"}, headers=h)).status_code == 200
        r = await c.post("/api/chat", json={"text": "2"}, headers=h)
        assert r.status_code == 429 and r.json()["error"]["code"] == "limit"
        assert (await c.post("/api/chat", json={"text": ""}, headers=h)).status_code == 422
        billing = (await c.get("/api/billing", headers=h)).json()
        assert billing["used"] == 1 and billing["stars_link"] == "https://t.me/aura_test_bot?start=premium"
        assert billing["ton_enabled"] is False
        assert (await c.post("/api/billing/ton", headers=h)).status_code == 503


async def test_voice_upload(client, services, llm):
    h = await onboarded_client(client)
    llm.answer = chat_json("Ок")
    r = await client.post("/api/voice", files={"file": ("rec.m4a", b"\x00\x01audio", "audio/mp4")}, headers=h)
    assert r.status_code == 200 and r.json()["transcript"] == "напомни про хлеб" and r.json()["reply"] == "Ок"
    assert services.speech.heard == [b"\x00\x01audio"]
    services.speech.fail = True
    r = await client.post("/api/voice", files={"file": ("rec.m4a", b"x", "audio/mp4")}, headers=h)
    assert r.status_code == 422


async def test_devices(client, sessionmaker):
    h = await register(client)
    tok = "fcm-" + "x" * 40
    await client.post("/api/devices", json={"fcm_token": tok}, headers=h)
    await client.post("/api/devices", json={"fcm_token": tok}, headers=h)   # идемпотентно
    async with sessionmaker() as s:
        assert len((await s.scalars(select(Device))).all()) == 1
    await client.request("DELETE", "/api/devices", json={"fcm_token": tok}, headers=h)
    async with sessionmaker() as s:
        assert (await s.scalars(select(Device))).all() == []


async def test_link_telegram_merges_accounts(client, sessionmaker, llm, settings):
    """APK-аккаунт с данными сливается с TG-аккаунтом: одна память и задачи в обоих окнах."""
    tg = TgHarness(sessionmaker, llm=llm, settings=settings)
    await onboard(tg, allergies="мёд")
    async with sessionmaker() as s:
        tg_user = await get_or_create_tg_user(s, tg.tg_user.id, None, "Europe/Moscow")
        await add_memories(s, tg_user, [("fact", "Из Telegram")])
    h = await onboarded_client(client)
    async with sessionmaker() as s:
        apk = await s.scalar(select(User).where(User.email == "anya@example.com"))
        await add_memories(s, apk, [("fact", "Из APK")])
    code = tg.texts(await tg.send("/link"))[0].split("Код привязки: ")[1].split()[0]
    assert (await client.post("/api/link/telegram", json={"code": "WRONG123"}, headers=h)).status_code == 400
    r = await client.post("/api/link/telegram", json={"code": code.lower()}, headers=h)
    me = r.json()
    assert r.status_code == 200 and me["telegram_linked"] and me["email"] == "anya@example.com"
    assert me["profile"]["allergies"] == ["мёд"]                          # анкета TG важнее (уже заполнена)
    texts = {m["text"] for m in (await client.get("/api/memories", headers=h)).json()}
    assert texts == {"Из Telegram", "Из APK"}                             # та же сессия, общий аккаунт
    async with sessionmaker() as s:
        assert len((await s.scalars(select(User))).all()) == 1
        assert {m.user_id for m in (await s.scalars(select(Memory))).all()} == {tg_user.id}
    assert (await client.post("/api/link/telegram", json={"code": code}, headers=h)).status_code == 400  # одноразовый
    # вход по email теперь открывает TG-аккаунт
    r = await client.post("/api/auth/login", json={"email": "anya@example.com", "password": PW})
    assert r.json()["user"]["telegram_linked"]


async def test_negotiation_api(client, sessionmaker, llm, services):
    import json
    h = await onboarded_client(client)
    h2 = await onboarded_client(client, email="oleg@example.com")
    llm.moves = [json.dumps({"action": "propose", "slot": local_iso(2, 15)}), json.dumps({"action": "accept"})]
    r = await client.post("/api/negotiations", json={"with": "oleg@example.com", "topic": "обед"}, headers=h)
    assert r.status_code == 200, r.text
    lst = (await client.get("/api/negotiations", headers=h2)).json()
    assert lst[0]["status"] == "agreed" and lst[0]["needs_my_confirmation"] and lst[0]["with"] == "anya"
    nid = lst[0]["id"]
    await client.post(f"/api/negotiations/{nid}/confirm", json={"accept": True}, headers=h)
    r = await client.post(f"/api/negotiations/{nid}/confirm", json={"accept": True}, headers=h2)
    assert r.json()["status"] == "confirmed"
    assert len((await client.get("/api/tasks", headers=h2)).json()) == 1
    r = await client.post("/api/negotiations", json={"with": "nobody@example.com", "topic": "x"}, headers=h)
    assert r.status_code == 400


async def test_stats_and_health(client):
    h = await onboarded_client(client)
    assert (await client.get("/api/stats", headers=h)).json()["memories"] == 0
    assert (await client.get("/api/health")).json()["ok"]
