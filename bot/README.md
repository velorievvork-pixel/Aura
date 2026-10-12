# Aura Bot — личный ИИ-агент (Telegram + API для APK)

Один Python-бэкенд (aiogram 3 + FastAPI, SQLAlchemy, APScheduler) — два клиента:
Telegram-бот и Android APK. Код — в `bot/`, остальной монорепо его не затрагивает.

## Статус этапов

| # | Этап | Статус |
|---|---|---|
| 1 | БД + онбординг-анкета в TG | ✅ |
| 2 | Чат + долгая память | — |
| 3 | Задачи + пуши | — |
| 4 | Голос (STT) | — |
| 5 | HTTP API + Android-клиент + сборка APK | — |
| 6 | Подписка и оплата (Stars / TON) | — |

## Запуск

```bash
cd bot
python3.11 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env      # впишите BOT_TOKEN — токен от @BotFather
python -m aura_bot        # long polling; база aura.db создастся сама
pytest                    # тесты (без сети и без токена)
```

Из корня монорепо: `make bot-test`, `make bot-run`.

## Как устроено (этап 1)

- `aura_bot/models.py` — `users` (TG ID = аккаунт; email/пароль — для APK с этапа 5, телефона нет),
  `profiles` (анкета), `memories` (`kind ∈ fact|preference|schedule|contact`, CHECK в БД).
- `aura_bot/onboarding.py` — парсинг и валидация ответов + `profile_prompt()`: блок системного
  промпта («аллергия: арахис — НИКОГДА не советуй…»), который подмешивается в каждый запрос к LLM.
- `aura_bot/handlers/onboarding.py` — `/start`-анкета на FSM: день рождения → аллергии →
  диеты (мультивыбор кнопками) → привычки → часовой пояс → подтверждение. `/cancel` — прервать.
- Данные кнопок валидируются сервером (подделанный `callback_data` отклоняется).
- Все секреты — только из окружения (`.env`, шаблон `.env.example`).

## Ограничения этапа 1

- Состояние незаконченной анкеты — в памяти процесса (рестарт = начать заново).
- Таблицы создаются `create_all`; при переезде на PostgreSQL добавим Alembic.
