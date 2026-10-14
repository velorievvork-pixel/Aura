# Установка и настройка Aura

Порядок: PostgreSQL → AI Service → C++-сервер → клиенты. Для разработки
достаточно `make`-целей; для продакшена — те же шаги плюс реальные секреты
и TLS-терминирующий прокси.

## 1. Требования

- Linux/macOS, C++17-компилятор (GCC 12+ / Clang), CMake ≥ 3.21, Ninja;
- Python 3.11+;
- PostgreSQL 14+ (разработка: подойдёт `pgserver` из pip);
- Qt 6.5+ для desktop-клиента (Core, Gui, Network, Qml, Quick,
  QuickControls2, WebSockets, Multimedia, TextToSpeech);
- macOS + Xcode 15 + XcodeGen для iOS-клиента.

## 2. PostgreSQL

```bash
createdb aura
psql aura -v ON_ERROR_STOP=1 -f schema/schema.sql
# обновления схемы — идемпотентные файлы из schema/migrations/:
psql aura -v ON_ERROR_STOP=1 -f schema/migrations/2026_09_20_notifications.sql
psql aura -v ON_ERROR_STOP=1 -f schema/migrations/2026_10_13_roles.sql   # роли админ-панели
psql aura -v ON_ERROR_STOP=1 -f schema/migrations/2026_10_14_invites.sql  # приглашения по ссылке
psql aura -v ON_ERROR_STOP=1 -f schema/migrations/2026_10_14_booking_referrals.sql  # запись и рефералы
# после обновления сервера: аккаунты PostgreSQL, обошедшие подтверждение email
# (ошибка до октября 2026) — сначала отчёт, затем -v apply=1 [-v revoke=1]:
psql aura -f tools/sql/unverified_accounts.sql
# демо-данные (необязательно):
psql aura -f schema/seed.sql
```

Без `AURA_DATABASE_URL` сервер использует встроенное JSON-хранилище
(`AURA_EMBEDDED_DB`) — только для разработки и тестов.

## 3. Python AI Service

```bash
make venv                     # .venv + ai/requirements.txt
# общий с C++-сервером секрет (тот же AURA_AI_TOKEN задайте серверу):
export AURA_AI_TOKEN="$(openssl rand -hex 32)"
cd ai && PYTHONPATH=. uvicorn aura_ai.app:app --host 127.0.0.1 --port 8000
# или для локальной разработки без токена (только 127.0.0.1):
make ai-run                   # = AURA_AI_INSECURE_DEV=1 python -m aura_ai.app
```

Без `AURA_AI_TOKEN` AI-сервис **закрыт**: все эндпоинты, кроме `/healthz`,
отвечают `503` (fail closed). Открыть его без токена можно только явным
`AURA_AI_INSECURE_DEV=1` — для локальной разработки; в продакшене эту
переменную не задавать.

Ключевые переменные (полный список — `ai/aura_ai/config.py`):

| переменная | смысл |
| --- | --- |
| `AURA_AI_TOKEN` | общий секрет C++ ↔ AI (заголовок `X-Aura-Token`); без него — `503` на всё, кроме `/healthz` |
| `AURA_AI_INSECURE_DEV` | `1` — разрешить работу без `AURA_AI_TOKEN` (только локальная разработка; если токен задан, он проверяется всё равно) |
| `AURA_AI_HOST`, `AURA_AI_PORT` | адрес `python -m aura_ai.app` (по умолчанию `127.0.0.1:8000`) |
| `AURA_LLM_PROVIDER` | `mock` (по умолчанию) или `openai` |
| `OPENAI_API_KEY`, `OPENAI_BASE_URL`, `AURA_LLM_MODEL` | реальный LLM |
| `AURA_MEMORY_BACKEND` | `file` (по умолчанию, `./.aura_memory.json`), `file://путь`, `memory` или `postgres` (DSN — из `AURA_DATABASE_URL`) |
| `AURA_DATABASE_URL` | DSN PostgreSQL для `AURA_MEMORY_BACKEND=postgres` (в compose — роль `aura_ai`) |
| `AURA_RAG_MODE` | `auto`: BM25 всегда, эмбеддинги если заданы |
| `AURA_EMBEDDINGS_URL/_MODEL/_API_KEY` | семантический поиск памяти |
| `AURA_STT_PROVIDER/_URL/_MODEL/_API_KEY` | Whisper-совместимый STT; без них — mock |
| `AURA_CORS_ORIGINS` | для браузерных клиентов |

## 4. C++-сервер

```bash
cmake -S . -B build/server -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DAURA_WITH_LIBPQ=ON -DAURA_BUILD_TESTS=ON -DAURA_WITH_CLIENT=OFF
cmake --build build/server -j
AURA_DATABASE_URL="postgresql://user:pass@host/aura" \
AURA_AI_URL=http://127.0.0.1:8000 \
AURA_AI_TOKEN="$AURA_AI_TOKEN" \
AURA_JWT_SECRET="$(openssl rand -hex 32)" \
AURA_2FA_KEY="$(openssl rand -hex 32)" \
./build/server/server/aura-server
```

Переменные сервера (значения по умолчанию — в `server/include/aura/config.h`):

| группа | переменные |
| --- | --- |
| сеть | `AURA_HOST` (0.0.0.0), `AURA_PORT` (9000), `AURA_MAX_CONNECTIONS`, `AURA_MAX_CONNECTIONS_PER_IP` (20; 0 — без лимита), `AURA_MAX_FRAME`, `AURA_MAX_PREAUTH_FRAME` (64 КБ — кадр до входа), `AURA_AUTH_TIMEOUT` (30 с — соединение без входа закрывается), `AURA_AUTH_RECHECK` (15 с — перепроверка сессии открытого соединения), `AURA_PING_INTERVAL`, `AURA_SOCKET_TIMEOUT` |
| БД | `AURA_DATABASE_URL`, `AURA_EMBEDDED_DB` |
| AI | `AURA_AI_URL`, `AURA_AI_TOKEN`, `AURA_AI_TIMEOUT_MS` |
| секреты | `AURA_JWT_SECRET` — случайная строка ≥ 32 байт (`openssl rand -hex 32`), `AURA_2FA_KEY` (шифр TOTP-секретов и токенов интеграций). Без них, со слабым/шаблонным секретом (`aura-dev-secret`, `CHANGE-IN-PRODUCTION…`, < 32 байт) сервер **не стартует** |
| режим разработки | `AURA_DEV_MODE=1` — только локально: разрешает `AURA_MAIL_DRIVER=dev` и запуск без секретов (JWT-секрет генерируется случайно на время жизни процесса — токены не переживают перезапуск) |
| токены/лимиты | `AURA_TOKEN_TTL` (7 дней), `AURA_REFRESH_TTL` (30 дней), `AURA_VERIFY_TTL`, `AURA_RESET_TTL` (≤ 1 ч), `AURA_MAX_LOGIN_ATTEMPTS` (8 на email), `AURA_MAX_IP_ATTEMPTS` (30 неудач с IP), `AURA_LOGIN_WINDOW` (300 с), `AURA_MAX_2FA_ATTEMPTS` (5 неверных TOTP на аккаунт) / `AURA_2FA_WINDOW` (900 с), `AURA_MAX_CODE_ATTEMPTS` (5 — затем код подтверждения/сброса сгорает), `AURA_MAIL_PER_EMAIL_HOUR` (3 письма на адрес), `AURA_MAIL_PER_IP_HOUR` (20), `AURA_REGISTER_PER_IP_HOUR` (10; 0 — без лимита) |
| почта | `AURA_MAIL_DRIVER` (по умолчанию `log`): `smtp` — письма с кодами (`AURA_SMTP_HOST/PORT/USER/PASSWORD/FROM/TLS`, `AURA_SMTP_CA_FILE`), `log` — без отправки, в лог пишется только факт выдачи кода, `dev` — код в ответе API (только вместе с `AURA_DEV_MODE=1`, иначе сервер не стартует) |
| Google OAuth | `AURA_GOOGLE_CLIENT_ID/_SECRET`, `AURA_GOOGLE_AUTH_URL/_TOKEN_URL/_REVOKE_URL/_CALENDAR_URL/_GMAIL_URL`, `AURA_OAUTH_STATE_TTL` |
| push | `AURA_PUSH_DRIVER` (`dev`/`webhook`), `AURA_PUSH_WEBHOOK_URL`, `AURA_APNS_TOPIC` (ai.aura.app), `AURA_APNS_URL`, `AURA_FCM_URL` (FCM HTTP v1, этап 11: замените `aura` на id проекта Firebase) |
| админ-панель | `AURA_ADMIN_EMAILS` — email первых администраторов через запятую; роль `admin` выдаётся только подтверждённому адресу при старте сервера и при входе (см. PROTOCOL.md → «Администрирование»). Нужна миграция `schema/migrations/2026_10_13_roles.sql` |
| приглашения по ссылке | `AURA_PUBLIC_URL` — адрес веб-клиента (`https://aura.example.com`): `invite.create` вернёт готовую ссылку `…/#/i/<токен>`; без него ссылку собирает клиент (в браузере на своём домене) или показывает только токен (Electron/Capacitor/localhost). Нужна миграция `schema/migrations/2026_10_14_invites.sql` |
| страницы записи и рефералы | тот же `AURA_PUBLIC_URL`: `booking.pageCreate` вернёт `…/#/b/<slug>`, `booking.book` — ссылку управления `…/#/m/<токен>`, `referral.get` — `…/#/signup?ref=<код>`. Нужны AI-сервис (подбор свободного времени) и миграция `schema/migrations/2026_10_14_booking_referrals.sql` |
| прочее | `AURA_SCHEDULER_INTERVAL_MS` (0 — только ручной tick), `AURA_TOOLS_MODE`, `AURA_MAPS_API_URL`, `AURA_LOG_LEVEL` |

Лимиты по IP считаются по адресу без порта. Если сервер стоит за обратным
прокси (все клиенты приходят с одного IP), поднимите или отключите (0)
`AURA_MAX_CONNECTIONS_PER_IP`, `AURA_MAX_IP_ATTEMPTS`, `AURA_REGISTER_PER_IP_HOUR`.

Локальный запуск без секретов и SMTP (код подтверждения — прямо в ответе API):

```bash
AURA_DEV_MODE=1 AURA_MAIL_DRIVER=dev ./build/server/aura-server
```

Сервер ходит во внешние HTTP API без TLS: при `AURA_GOOGLE_CLIENT_ID` и
https-адресах `AURA_GOOGLE_TOKEN_URL/REVOKE_URL/CALENDAR_URL/GMAIL_URL` в лог при
старте пишется предупреждение — направьте их на http-адрес TLS-прокси (egress).

### Почта (коды подтверждения и сброса пароля)

Сервер сам отправляет письма по SMTP (OpenSSL, TLS 1.2+, проверка сертификата
и имени хоста). Подойдёт любой почтовый сервис с SMTP: Яндекс 360, Mail.ru для
бизнеса, Google Workspace, Amazon SES, Mailgun, Postmark, SendGrid.

```env
AURA_MAIL_DRIVER=smtp
AURA_SMTP_HOST=smtp.yandex.ru      # адрес SMTP провайдера
AURA_SMTP_PORT=587                 # 587 + starttls или 465 + tls
AURA_SMTP_TLS=starttls
AURA_SMTP_USER=no-reply@example.com
AURA_SMTP_PASSWORD=<пароль приложения>
AURA_SMTP_FROM=Aura <no-reply@example.com>
```

- Письма: «Код подтверждения Aura: 123456» (код в теме, тексте и HTML) и
  «Сброс пароля Aura». Код в ответе API не возвращается.
- Без TLS (`AURA_SMTP_TLS=none`) авторизация разрешена только к ретранслятору
  на этой же машине (`localhost`).
- Ошибки доставки пишутся в лог (`[mail] … не отправлено …: причина`); пользователь
  может запросить код ещё раз.
- Для доставляемости настройте у домена SPF, DKIM и DMARC (у провайдера).
- Проверка локально: `aiosmtpd` со STARTTLS и своим CA (`AURA_SMTP_CA_FILE`).

### Google OAuth (интеграции Calendar/Gmail)

1. Google Cloud Console → OAuth Client (Web application).
2. Redirect URI: desktop — `http://127.0.0.1:<порт>/callback` (клиент
   поднимает loopback-сервер сам); iOS — `aura://oauth`.
3. Scopes: `calendar.events.readonly`, `gmail.send`.
4. Заполните `AURA_GOOGLE_*`; без client_id интеграции отвечают
   `not_configured` (честно, без заглушек).

### Push (APNs через шлюз)

Ядро не делает TLS/HTTP2/ES256 — это задача внешнего шлюза:

```
AURA_PUSH_DRIVER=webhook
AURA_PUSH_WEBHOOK_URL=https://push-gateway.internal/push
```

Сервер POST-ит шлюзу конверт `{platform, token, notification_id, kind,
apns: {url, headers, payload}}`; шлюз подписывает JWT ключом `.p8` и
форвардит `apns.payload` на `apns.url`. Драйвер `dev` логирует доставки.

## 5. Desktop-клиент (Qt)

```bash
cmake -S . -B build/client -G Ninja -DAURA_WITH_CLIENT=ON
cmake --build build/client -j
./build/client/client/aura-client
```

Адрес сервера задаётся в окне входа (сохраняется в QSettings вместе с
токенами). Для продакшена — `wss://` за TLS-прокси.

### Готовый .exe для Windows — через GitHub Actions

Локальная сборка не обязательна: workflow **`build-windows.yml`** на каждый
push собирает `aura-client.exe` с Qt-рантаймом и libpq (Windows Server 2022,
Qt 6.7 MSVC). Скачать: вкладка **Actions** → «Build Windows client» →
последний запуск → **Artifacts → aura-desktop-windows** (zip). Это релизная
сборка без подписи кода; для распространения подпишите exe своим
сертификатом и/или соберите установщик (NSIS/MSI).

## 6. iOS-клиент

```bash
brew install xcodegen
cd ios && xcodegen generate && open Aura.xcodeproj
```

- В схеме Aura включите Push Notifications (entitlement `aps-environment`
  уже в `project.yml`) и свой bundle id `ai.aura.app`.
- Адрес сервера — в поле входа (`wss://…` для ATS; `ws://` только для
  локальной разработки с исключением в Info.plist).
- Siri/Команды: App Intents регистрируются при первом запуске; Back Tap
  настраивается пользователем в системных настройках через Команду —
  приложение честно показывает пошаговую инструкцию (см. MOBILE_FEATURES).

## 7. 2FA (TOTP)

Работает из коробки: `auth.setup2fa` → QR/otpauth URI → `auth.confirm2fa`
→ 10 резервных кодов. Секреты шифруются ключом `AURA_2FA_KEY` (fallback —
`AURA_JWT_SECRET`); в продакшене задайте отдельный `AURA_2FA_KEY` и не
меняйте его (иначе существующие секреты не расшифруются).

## 8. Production-стек через Docker Compose

Весь серверный стек (PostgreSQL 16 + AI Service + C++-сервер) поднимается
одной командой; Qt-клиент собирается на хосте (нужен GUI), iOS — на Mac.

```bash
cp .env.example .env      # .env в git не коммитить (он в .gitignore)
# Заполните ВСЕ секреты, каждый — openssl rand -hex 32:
#   AURA_JWT_SECRET  AURA_2FA_KEY  AURA_AI_TOKEN
#   POSTGRES_PASSWORD  AURA_DB_PASSWORD  AURA_AI_DB_PASSWORD
# + по необходимости: AURA_MAIL_DRIVER=smtp и AURA_SMTP_*, OPENAI_API_KEY,
#   AURA_LLM_PROVIDER=openai, AURA_PUSH_DRIVER=webhook AURA_PUSH_WEBHOOK_URL=…,
#   AURA_GOOGLE_CLIENT_ID/_SECRET

make docker-build   # собрать образы (server: multi-stage CMake+libpq; ai: python:3.12-slim)
make docker-up      # поднять стек в фоне
make docker-logs    # логи сервера и AI-сервиса
make docker-down    # остановить
```

Что делает compose:

- **postgres** — при первом старте пустого тома применяет только
  `schema/schema.sql` (уже включает все миграции) и
  `deploy/postgres/20-app-roles.sh`, который создаёт роли приложения.
  `schema/seed.sql` (демо-аккаунты) в прод **не** монтируется. Порт 5432
  наружу не публикуется; обслуживание — `docker compose exec postgres psql -U postgres aura`.
  Новые миграции применяйте вручную под `postgres`
  (`docker compose exec -T postgres psql -U postgres aura < schema/migrations/<файл>.sql`):
  права `aura_app` на новые таблицы выдаются автоматически (default privileges).
- **ai** — образ с `psycopg`, пользователь без root (uid 10001). Порт 8000
  наружу не публикуется — к сервису ходит только `server` по сети compose с
  `X-Aura-Token`. Healthcheck — `GET /healthz` средствами Python (curl в
  slim-образе нет).
- **server** — образ без компилятора (только `libpq5`), без root; стартует
  после healthy-зависимостей; healthcheck — проверка порта 9000. Наружу
  публикуется только 9000 (`AURA_BIND=127.0.0.1`, если TLS-прокси на этой же машине).

### Безопасность развёртывания

| что | как устроено |
| --- | --- |
| секреты | `${VAR:?}` в `docker-compose.yml`: без `AURA_JWT_SECRET`, `AURA_2FA_KEY`, `AURA_AI_TOKEN`, `POSTGRES_PASSWORD`, `AURA_DB_PASSWORD`, `AURA_AI_DB_PASSWORD` стек не стартует; в `.env.example` значения пустые. Генерация — `openssl rand -hex 32` (hex безопасен внутри URL БД) |
| роли БД | `postgres` — только инициализация и миграции; `aura_app` (C++-сервер) — `SELECT/INSERT/UPDATE/DELETE`, без DDL и суперпользователя; `aura_ai` (AI-сервис) — только `user_memory` и `users(id, email)` |
| AI-сервис | fail closed без `AURA_AI_TOKEN` (`503`); `/healthz` не раскрывает DSN и пути; ошибки `500` без текста исключения; инструменты с внешним эффектом (`send_email`, `send_message`, `book_table`) исполняет только C++-сервер после `tool_permissions` и подтверждения |
| режим разработки | `AURA_DEV_MODE` и `AURA_AI_INSECURE_DEV` в продакшене **не задавать**; почта в compose по умолчанию `AURA_MAIL_DRIVER=log` (для писем — `smtp` + `AURA_SMTP_*`) |
| контейнеры | `ai` и `server` работают от непривилегированного пользователя |

Продакшен-чек-лист: заполните все секреты (см. выше), поставьте
TLS-терминирующий прокси перед портом 9000 (`wss://`), для реального push —
шлюз APNs (`AURA_PUSH_DRIVER=webhook`), для почты — `AURA_MAIL_DRIVER=smtp` +
`AURA_SMTP_*` (см. «Почта» выше). Бэкап — `pg_dump` тома `pgdata`.
Сменить пароли ролей на существующем томе: `ALTER ROLE aura_app PASSWORD '…'`
(init-скрипты выполняются только при создании тома).

## 9. Проверка установки

```bash
make check    # тесты Python + C++
make e2e      # сквозной прогон (серверы должны быть запущены)
python3 tools/check_ios_protocol.py
```

CI (GitHub Actions, `.github/workflows/ci.yml`) на каждый push гоняет
сборку C++ + ctest, pytest AI-сервиса, сверку протокола iOS/Android,
синтаксис e2e и JVM-тесты AuraKit. Сборочные job'ы работают с токеном
только на чтение (`persist-credentials: false`); запись (ветки `builds/*`,
релиз «builds») — отдельными job'ами публикации и только для push в `main`,
логи — в `arena/build-logs` на любом push. Qt/iOS в CI не собираются (нужны Qt 6 и macOS) — это
локальные проверки из [TESTING.md](TESTING.md).

Частые проблемы — [TROUBLESHOOTING.md](TROUBLESHOOTING.md).
