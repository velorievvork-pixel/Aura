# Протокол Aura

Транспорт — WebSocket (RFC 6455) поверх TCP. Все сообщения — JSON в текстовых
фреймах. Один TCP-сокет = одна `Session` на сервере.

Клиенты: Qt/QML (desktop), Swift/SwiftUI (iOS, этап 10 — см. docs/IOS.md),
Kotlin/Compose (Android, этап 11 — см. docs/ANDROID.md), Python (e2e).
Протокол один на всех; сверка имён обработчиков мобильных клиентов —
`tools/check_ios_protocol.py` и `tools/check_android_protocol.py`.

## Конверт

```jsonc
// запрос клиента
{ "id": "req-1", "type": "chat.send", "payload": { ... } }

// ответ сервера
{ "id": "req-1", "type": "ok",    "payload": { ... } }
{ "id": "req-1", "type": "error", "code": "forbidden", "message": "..." }

// push-событие сервера (без id)
{ "type": "event", "event": "chat.message", "payload": { ... } }
```

`id` — строка или число; сервер возвращает его в том же виде (число — числом).

Авторизация двумя способами:

1. заголовок `Authorization: Bearer <jwt>` в HTTP-рукопожатии — сервер сразу
   считает сессию авторизованной и шлёт событие `session.ready`;
2. сообщение `auth.login` / `auth.token` уже после подключения.

Ограничения соединения: до входа кадр не больше 64 КБ (`AURA_MAX_PREAUTH_FRAME`,
больше — закрытие 1009/1002); соединение без входа закрывается через 30 с
(`AURA_AUTH_TIMEOUT`, код закрытия 4408); с одного IP — не больше 20 соединений
(`AURA_MAX_CONNECTIONS_PER_IP`, лишнее получает HTTP 429 на рукопожатии). В JSON-объекте
не больше 1000 ключей.

Открытое соединение не переживает свою сессию: при отзыве сессии
(`sessions.revoke`/`revokeAll`, `auth.logout` с тем же токеном на другом сокете),
смене/сбросе пароля, повторном использовании refresh-токена или блокировке
аккаунта сервер шлёт событие `session.revoked` (`{reason}`) и закрывает сокет с
кодом 4401. Кроме того, сессия соединения сверяется с БД не реже чем раз в 15 с
(`AURA_AUTH_RECHECK`): отозванная/истёкшая → `unauthorized` «сессия истекла или
отозвана — войдите снова», соединение разлогинивается.

Все типы, кроме `auth.register`, `auth.login`, `auth.login2fa`, `auth.verifyEmail`,
`auth.resendCode`, `auth.forgotPassword`, `auth.resetPassword`,
`auth.refresh`, `ping`, `server.info`, требуют авторизации.

## Коды ошибок

| code | смысл |
| --- | --- |
| `bad_request` | неизвестный тип, пустое поле, некорректный JSON, код подтверждения/сброса неверен |
| `unauthorized` | нет токена, токен недействителен, неверный пароль, refresh-токен отозван/истёк |
| `email_not_verified` | пароль верный, но email не подтверждён — нужен `auth.verifyEmail` |
| `requires_2fa` | пароль верный, но включена 2FA — нужен `auth.login2fa` с кодом |
| `forbidden` | нет доступа к чату (в т.ч. `agent.ask` с чужим `chat_id`), аккаунт отключён, слишком много попыток, инструмент запрещён (`deny`) |
| `not_found` | пользователь/чат не найдены |
| `conflict` | email уже занят |
| `upstream_error` | AI-сервис недоступен или вернул мусор |
| `internal_error` | необработанное исключение; текст всегда обобщённый («внутренняя ошибка сервера»), подробности (SQL и т.п.) — только в логе сервера |

## Методы

### Авторизация

Поток аккаунта (auth v3): регистрация → код подтверждения email → вход.
Регистрация сессию не выдаёт.

| тип | payload | ответ |
| --- | --- | --- |
| `auth.register` | `display_name`, `email`, `password` | `requires_verification`, `user`, `email_code` (только `AURA_MAIL_DRIVER=dev` + `AURA_DEV_MODE=1`); занятый email → `conflict`; больше 10 регистраций с IP в час → `forbidden` |
| `auth.verifyEmail` | `email`, `code` (6 цифр) | `verified`, `user`; после 5 неверных кодов код сгорает (нужен `auth.resendCode`) |
| `auth.resendCode` | `email` | `message` (+`email_code` в dev); ответ не раскрывает существование email; не больше 3 писем на адрес в час (сверх лимита ответ тот же, письмо не уходит) |
| `auth.login` | `email`, `password`, `device?`, `device_id?` | `token`, `token_type`, `expires_in`, `refresh_token`, `refresh_expires_in`, `session_id`, `user`; до подтверждения email — `email_not_verified`; при включённой 2FA и недоверенном `device_id` — `requires_2fa`; отключённый аккаунт — `forbidden` «аккаунт отключён» (только при верном пароле) |
| `auth.login2fa` | `email`, `password`, `code`, `trust_device?`, `device_id?`, `device?` | как `auth.login`; `code` — 6-значный TOTP или резервный `XXXXX-XXXXX`; `trust_device` помечает устройство доверенным на 90 дней; 5 неверных кодов за 15 мин → `forbidden` до конца окна (счётчик на аккаунт, верный пароль его не сбрасывает) |
| `auth.refresh` | `refresh_token`, `device?` | новая пара `token`+`refresh_token` (ротация); текущее соединение авторизуется новой сессией, как после `auth.login` |
| `auth.token` | `token` | `user` |
| `auth.me` | — | `user` + `preferences` |
| `auth.changePassword` | `old_password`, `new_password` | `changed` + новая пара для текущего устройства, как у `auth.login`: `token`, `token_type`, `expires_in`, `refresh_token`, `refresh_expires_in`, `session_id`, `user`. Все прежние сессии и refresh-токены (включая текущие) отозваны — клиент сохраняет новую пару; соединение продолжает работать под новой сессией, остальные соединения пользователя закрываются (`session.revoked`) |
| `auth.forgotPassword` | `email` | `message` (+`reset_code` в dev — **6 цифр**); ответ всегда «ок»; не больше 3 писем на адрес в час |
| `auth.resetPassword` | `email`, `code` (6 цифр, живёт 1 ч), `new_password` | `reset`; после 5 неверных кодов код сгорает; все сессии и refresh-токены отзываются, открытые соединения закрываются |
| `auth.logout` | — | `revoked`; отзывает сессию и refresh-токены этого устройства |
| `profile.update` | `display_name?` (1–80 симв.), `bio?` (≤300 симв.), `avatar_url?` (пусто, `https://…` или `data:image/{png,jpeg,webp};base64,…` до ~100 КБ) | `user`; меняются только переданные поля; пустой payload или неверное значение → `bad_request` |

### Сессии

| тип | payload | ответ |
| --- | --- | --- |
| `sessions.list` | — | `sessions[]`: `id`, `device`, `remote_addr`, `created_at`, `expires_at`, `current` |
| `sessions.revoke` | `session_id` | `revoked`; отзывает и refresh-токены этой сессии, её открытые соединения закрываются; при отзыве своей сессии соединение разлогинивается |
| `sessions.revokeAll` | — | `revoked`; текущая сессия и её refresh-токен остаются живыми, все прочие сессии и refresh-токены отозваны, их соединения закрыты |

### Двухфакторная аутентификация (TOTP)

Порядок входа: пароль → TOTP → сессия. Секрет TOTP хранится зашифрованным
(AES-256-GCM, ключ — `AURA_2FA_KEY`). Настройка: `auth.setup2fa` выдаёт секрет
(base32) и `otpauth://`-URI для приложения-аутентификатора (Google Authenticator,
1Password и др.); `auth.confirm2fa` проверяет одноразовый код, включает 2FA и
возвращает 10 резервных кодов (показываются один раз). RFC 6238: 6 цифр, период
30 с, HMAC-SHA1, окно ±1 период, код одноразовый.

| тип | payload | ответ |
| --- | --- | --- |
| `auth.setup2fa` | — | `secret` (base32), `otpauth_uri`, `issuer`, `algorithm`, `digits`, `period`; 2FA остаётся выключенной до подтверждения; если 2FA уже включена → `bad_request` (сначала `auth.disable2fa`) |
| `auth.confirm2fa` | `code` | `recovery_codes[]` (10 шт., формат `XXXXX-XXXXX`, одноразовые); включает 2FA |
| `auth.status2fa` | — | `enabled`, `pending`, `recovery_codes_left` |
| `auth.disable2fa` | `password` | `ok`; отключение только после повторной проверки пароля; удаляет секрет, резервные коды и доверенные устройства |
| `devices.list` | — | `devices[]`: `id`, `device`, `remote_addr`, `created_at`, `expires_at`, `last_used_at` |
| `devices.revoke` | `id` | `ok`; отзывает одно доверенное устройство |
| `devices.revokeAll` | — | `ok`; отзывает все доверенные устройства |

Резервные коды хранятся как SHA-256 в `recovery_codes` (одноразовые). Доверенные
устройства (90 дней) — в `trusted_devices`; вход с доверенным `device_id`
пропускает ввод кода. События 2FA (`2fa_enabled`, `2fa_disabled`, `2fa_failed`,
`login_2fa_required` и др.) пишутся в `audit_logs`.

Пароль: минимум 8 символов (кодовых точек UTF-8), хотя бы одна буква (латиница,
в т.ч. Latin-1/расширенная, греческий, кириллица — «пароль12» допустим) и цифра,
не больше 256 байт. Хранится как
`argon2id$v=19$m=65536,t=3,p=4$<соль>$<хэш>` (Argon2id, RFC 9106). Старые
хэши `pbkdf2$…` проверяются и при первом успешном входе прозрачно
пересчитываются в Argon2id.
Access-токен: JWT HS256 с claims `sub`, `email`, `jti`, `iat`, `exp`; `jti`
сверяется с таблицей `sessions`, поэтому выход действительно отзывает токен.
`sub` должен совпадать с владельцем сессии `jti`, аккаунт должен быть активен.
Refresh-токен: случайные 32 байта, в БД хранится SHA-256; ротация по
«семействам» — повторное использование токена, уже заменённого при ротации
(в т.ч. два одновременных `auth.refresh` одним токеном), отзывает всё
семейство и все сессии пользователя (защита от кражи). Токен, отозванный
выходом/отзывом сессии, просто `unauthorized`.
Одноразовые коды (подтверждение email, сброс пароля) — 6 цифр, хранятся как
SHA-256 (с привязкой к пользователю) в `auth_tokens` со сроком действия
(подтверждение — 24 ч, сброс — 1 ч); 5 неверных попыток — код сгорает.
События безопасности пишутся в `audit_logs`.

### Чаты

| тип | payload | ответ |
| --- | --- | --- |
| `chat.list` | `limit?` | `chats[]` с `members[]` и последним сообщением |
| `chat.open` | `contact` (email или id), `title?` | `chat`, `created` |
| `chat.history` | `chat_id`, `before_id?`, `limit?` | `messages[]`, `chat` |
| `chat.members` | `chat_id` | `members[]` с флагом `online` |
| `chat.read` | `chat_id` | `read` |
| `chat.send` | `chat_id`, `body` (≤ 8000 симв.), `kind?`, `payload?` | сообщение; от клиента допустим только `kind: "text"` (`agent_action`/`agent_reply`/`system` ставит сервер) — иначе `bad_request`; `payload` — объект ≤ 4 КБ без служебных полей `origin`, `proposal`, `confirmed`, `tool` |
| `users.search` | `query` (≥ 2 симв.), `limit?` | `users[]`: только `id`, `display_name`, `avatar_url`, `online` (без email и `last_seen_at`); поиск по подстроке имени, по email — только точное совпадение (запрос с `@`) |

События: `chat.message` (всем участникам, включая другие устройства отправителя),
`chat.created` (приглашённому).

### Аура

| тип | payload | ответ |
| --- | --- | --- |
| `agent.ask` | `message`, `chat_id?`, `peers[]?`, `execute?` | `reply`, `intent`, `confidence`, `plan`, `actions[]`, `results[]`, `memory_saved`, `a2a`; `chat_id` чужого чата → `forbidden`; `peers[]` учитываются только люди, с которыми есть общий чат, и их контекст урезается по их `prefs.privacy` (как в A2A) |
| `agent.negotiate` | `target` (email/имя), `topic?`, `duration_minutes?`, `window_hours?` | `accepted`, `slot`, `place`, `counter_slots[]`, `message` |
| `agent.status` | — | `available`, `ai_service` |

Цепочка `agent.ask`:

1. `AgentManager` собирает контекст (память, настройки, расписание, история чата);
2. `POST /v1/agent/run` в Python AI Service с `execute: false` — AI только планирует;
3. C++ `ToolManager` выполняет вернувшиеся действия (почта, заметки, бронь…);
4. `MemoryManager` сохраняет `memory_updates` в `user_memory`;
5. если есть `a2a` — сервер находит пользователя-собеседника, вызывает
   `POST /v1/agent/negotiate` и публикует итог в чат сообщением с `kind: agent_reply`.

### Речь (STT)

| тип | payload | ответ |
| --- | --- | --- |
| `speech.transcribe` | `audio` (base64), `language?` (`ru`/`en`/`auto`, по умолч. `auto`), `format?` | `text`, `language`, `provider` |

Гибридная схема: на платформах с системным распознаванием (iOS Speech
Framework) клиент распознаёт на устройстве; на десктопе клиент пишет микрофон
(16 кГц, моно, 16 бит → WAV), кодирует в base64 и шлёт `speech.transcribe`.
C++-сервер форвардит аудио в Python AI Service `POST /v1/speech/transcribe`
(Whisper-совместимый). Провайдер STT выбирается на AI-сервисе: без
`AURA_STT_URL`/`AURA_STT_API_KEY` работает детерминированный `mock`
(для разработки), иначе — реальный Whisper-бэкенд. Пустое аудио → `bad_request`,
превышение `AURA_STT_MAX_BYTES` → ошибка, недоступность бэкенда → `upstream_error`.

Озвучка (TTS) выполняется платформенно на клиенте (Qt TextToSpeech /
AVSpeechSynthesizer), отдельного сетевого метода не требует.

### Память и настройки

| тип | payload | ответ |
| --- | --- | --- |
| `memory.list` | `query?`, `limit?` | `entries[]` (сначала релевантные запросу) |
| `memory.add` | `text`/`entries[]`, `kind?` | `saved` |
| `memory.extract` | `message`, `commit?` | `entries[]`, `source` (`ai` или `local`) |
| `prefs.get` | — | настройки |
| `prefs.set` | любые поля `user_preferences` | обновлённые настройки |

Онбординг-опрос после регистрации: клиенты показывают анкету (день
рождения, аллергии, диета, город, транспорт, бюджет), пока
`prefs.onboarded != true`. Флаг выставляет **сервер** в `prefs.set`, как
только в payload пришло хотя бы одно поле анкеты (`birthday`, `allergies`,
`diet`, `transport`, `city`, `budget_limit`, `preferred_hours`,
`work_hours`); клиент может передать `onboarded` явно. Анкета — обычные
поля настроек, она сразу попадает в контекст AI (`preferences`).

Поля настроек: `diet[]`, `transport`, `preferred_hours[]`, `work_hours[]`,
`birthday` (`YYYY-MM-DD`), `allergies[]`, `onboarded` (bool),
`budget_limit`, `city`, `lat`, `lon`, `theme`, `notifications{}`, `privacy{}`.

`privacy` — что Аура раскрывает чужой Ауре в переговорах (`agent.negotiate`,
A2A из `agent.ask`); все флаги по умолчанию `true`, `prefs.get` всегда отдаёт
все три, `prefs.set` принимает любую их часть (неизвестный ключ или не bool →
`bad_request`):

| флаг | что делает сервер |
| --- | --- |
| `share_free_slots_only` | в контекст респондента для AI идут только занятость и настройки, нужные для времени и места: факты памяти, `birthday`, `allergies` вырезаются |
| `hide_event_titles` | названия событий календаря респондента заменяются на «занято» |
| `allow_place_choice` | `false` у любой из сторон — место не предлагается (`place` пустой, в `message` «Место выберете сами») |

### Инструменты

| тип | payload | ответ |
| --- | --- | --- |
| `tool.list` | — | `tools[]`, `mode` |
| `tool.run` | `tool`, `args{}`, `chat_id?` | результат инструмента; проходит тот же барьер `tool_permissions`, что и действия Ауры: `deny` → `forbidden`, `ask` (по умолчанию для опасных) → `{requires_confirmation: true, confirmation_id, summary, tool, status: "pending"}` без исполнения (дальше `confirmation.approve`/`deny`) |

Инструменты: `send_message`, `create_note`, `create_reminder`, `send_email`,
`find_cafe`, `book_table`, `check_calendar`, `suggest_time`. В режиме
`AURA_TOOLS_MODE=sandbox` внешние API заменяются детерминированными ответами
(тот же контракт, что в `ai/aura_ai/tools.py`). `tool.list` возвращает для
каждого инструмента флаг `dangerous` и `default_mode`.

### Разрешения и подтверждения (этап 8)

Ядро безопасности v3: **LLM лишь формирует намерение, исполняет сервер и только
после проверки прав.** Каждое действие Ауры проходит барьер:

1. сервер вычисляет эффективный режим инструмента — явное разрешение
   пользователя из `tool_permissions` или режим по умолчанию
   (опасные `send_message`/`send_email`/`book_table` → `ask`, остальные → `allow`);
2. `allow` — исполнить сразу; `deny` — отклонить; `ask` — **не исполнять**, а
   создать отложенное действие (`pending_actions`) и вернуть в `results[]`
   элемент с `requires_confirmation: true` и `confirmation_id`;
3. пользователь подтверждает (`confirmation.approve`) или отклоняет
   (`confirmation.deny`); только после подтверждения сервер исполняет инструмент.

Элемент `results[]` для действия, требующего подтверждения:
`{ "tool": "send_email", "mode": "ask", "requires_confirmation": true,
"confirmation_id": 12, "summary": "Отправить письмо на x@y.z", "ok": false }`.
Отклонённое по `deny`: `{ "tool": "...", "mode": "deny", "denied": true, "ok": false }`.

| тип | payload | ответ |
| --- | --- | --- |
| `permissions.list` | — | `tools[]`: `tool`, `description`, `dangerous`, `default_mode`, `mode` (эффективный) |
| `permissions.set` | `tool`, `mode` (`allow`/`ask`/`deny`) | `tool`, `mode`; неизвестный инструмент или режим → `bad_request` |
| `confirmation.list` | `status?` (по умолч. `pending`), `limit?` | `actions[]`: `id`, `tool`, `args{}`, `summary`, `status`, `result{}`, `created_at` |
| `confirmation.approve` | `id` | `id`, `status` (`executed`/`failed`), `result{}`; чужое/несуществующее → `not_found`, уже обработанное (в т.ч. параллельный второй approve) → `bad_request` |
| `confirmation.deny` | `id` | `id`, `status` (`denied`) |

Разрешения хранятся в `tool_permissions(user_id, tool, mode)`; отложенные
действия — в `pending_actions(id, user_id, chat_id, tool, args, summary, status,
result, …)` со статусами `pending`/`approved`/`denied`/`executed`/`failed`/
`expired`. Успешное подтверждённое действие с `chat_id` публикуется в чат
сообщением `kind: agent_action` (`origin: aura-agent`, `confirmed: true`).
Изменения разрешений и подтверждения пишутся в `audit_logs`
(`tool_permission`, `action_approved`, `action_denied`).

### Задачи и напоминания (этап 8)

Задачи хранятся в `tasks(id, user_id, chat_id, title, notes, status, priority,
due_at, remind_at, reminded_at, …)`; статусы `pending`/`done`/`cancelled`.
Напоминание — задача с `remind_at`: фоновый планировщик сервера
(`AURA_SCHEDULER_INTERVAL_MS`, по умолчанию 30 с) находит задачи с наступившим
`remind_at`, помечает их отправленными (`reminded_at`, одноразово) и шлёт
владельцу push-событие `task.due`. Инструмент `create_reminder` тоже создаёт
задачу (возвращает `task_id`).

| тип | payload | ответ |
| --- | --- | --- |
| `tasks.list` | `status?`, `limit?` | `tasks[]` |
| `tasks.create` | `title`, `notes?`, `due_at?`, `remind_at?`, `priority?`, `chat_id?` | задача |
| `tasks.complete` | `id` | задача со статусом `done` |
| `tasks.cancel` | `id` | задача со статусом `cancelled` |
| `tasks.reopen` | `id` | задача со статусом `pending` |
| `tasks.delete` | `id` | `deleted`; чужая/несуществующая → `not_found` |
| `tasks.due` | — | `sent` — сколько напоминаний **вызывающего пользователя** отправлено (ручной проход планировщика только по своим задачам) |

Событие: `task.due` (владельцу задачи, когда наступил срок напоминания).

### Интеграции: Google OAuth 2.0 + PKCE (этап 9)

Провайдеры: `google_calendar` (scope `calendar.events.readonly`) и
`google_gmail` (scope `gmail.send`). Поток подключения: `integrations.begin` →
пользователь открывает `authorize_url` и разрешает доступ на consent-экране
Google → Google редиректит на `redirect_uri` с `code` + `state` → клиент
шлёт `integrations.callback`. Сервер обменивает код на токены (PKCE
`code_verifier`, метод S256), шифрует их AES-256-GCM и сохраняет в
`integration_connections`; `state`/`verifier` одноразовые
(`integration_oauth_states`, TTL `AURA_OAUTH_STATE_TTL`, по умолчанию 600 с).
**Токены клиенту не отдаются никогда.**

| тип | payload | ответ |
| --- | --- | --- |
| `integrations.list` | — | `providers[]` (`provider`, `name`, `description`, `scope`), `connections[]` (`id`, `provider`, `account`, `scope`, `status`, `last_error?`, `created_at`, `last_used_at?`) |
| `integrations.begin` | `provider`, `redirect_uri` | `provider`, `authorize_url`, `state`, `expires_in`; неизвестный провайдер → `bad_request`, OAuth не настроен → `not_configured` |
| `integrations.callback` | `provider`, `code`, `state` | подключение (без токенов); недействительный/повторный/истёкший `state` → `bad_request`, отказ провайдера → `oauth_error`, эндаунт недоступен → `upstream_error` |
| `integrations.revoke` | `id` | `id`, `status=revoked`; чужое/несуществующее → `not_found`; провайдер недоступен → `upstream_error` (запись сохраняется для повтора) |
| `integrations.sync` | `id` | календарь: `events`, `facts` (события сохраняются в память, kind `schedule.google`); Gmail: `profile_email` |

Access-токен автоматически обновляется по `refresh_token` (при HTTP 401 или
перед истечением). Инструменты используют подключения прозрачно: `send_email`
отправляет письмо через Gmail API (`messages.send`, RFC 2822 в base64url),
если подключён `google_gmail`, иначе — прежний почтовый путь; `check_calendar`
возвращает реальные события (`source=google_calendar`), иначе память
(`source=memory`).

Переменные окружения: `AURA_GOOGLE_CLIENT_ID`, `AURA_GOOGLE_CLIENT_SECRET`,
`AURA_GOOGLE_AUTH_URL`, `AURA_GOOGLE_TOKEN_URL`, `AURA_GOOGLE_REVOKE_URL`,
`AURA_GOOGLE_CALENDAR_URL`, `AURA_GOOGLE_GMAIL_URL`, `AURA_OAUTH_STATE_TTL`.
По умолчанию URL — реальные эндпоинты Google (прод — за TLS-терминирующим
прокси); e2e подменяет их локальным mock-сервером (`AURA_E2E_GOOGLE_MOCK=1`).

### Уведомления и push (этап 13)

Единая «входящая» уведомлений: каждое уведомление сохраняется в таблице
`notifications` и доставляется живым сессиям событием `notification.new`.
Виды (`kind`): `task.due` (напоминание), `confirmation.requested` (опасная
операция ждёт решения), `a2a.proposal` (другой агент начал переговоры),
`login.new` (новый вход), `twofactor.enabled` / `twofactor.disabled`
(изменение 2FA). Push-доставка — через устройства в `push_devices`
(платформы `apns` | `fcm` | `webhook` | `dev`); платформенный конверт
уходит во внешний шлюз (`AURA_PUSH_DRIVER=webhook`,
`AURA_PUSH_WEBHOOK_URL`), который подписывает ключи провайдера (JWT для
APNs, OAuth2 сервисного аккаунта для FCM) и форвардит запрос — сам
C++-сервер TLS/HTTP2 не делает и ключи провайдеров не хранит.
Тихие часы и отключённые виды читаются из настроек пользователя:
`prefs.notifications = {"quiet_hours": {"start":"22:00","end":"08:00"},
"muted_kinds": ["login.new"]}` (время UTC; гасят только push, in-app
доставляется всегда).

| тип | payload | ответ |
| --- | --- | --- |
| `notifications.list` | `unread?` (bool), `limit?` (≤200) | `notifications[]` (`id`, `kind`, `title`, `body`, `payload`, `read`, `created_at`), `unread` (счётчик) |
| `notifications.read` | `id?` (0/нет — все) | `unread`; чужое/несуществующее `id` → `not_found` |
| `devices.push.register` | `platform`, `token` | `id`, `platform`; повтор тем же токеном — upsert; плохие входные данные → `bad_request` |
| `devices.push.list` | — | `devices[]` (`id`, `platform`, `enabled`, `created_at`, `last_used_at?`); **токен не отдаётся** |
| `devices.push.revoke` | `id` | `id`, `revoked`; чужое/несуществующее → `not_found` |

Событие: `notification.new` (владельцу; payload — запись уведомления).
Push-конверт для шлюза: `{platform, token, notification_id, kind, …}`, где
платформенная секция зависит от `platform`:

- `apns` (и устройства без специальной платформы):
  `apns: {url, headers: {apns-topic, apns-push-type}, payload: {aps:
  {alert, badge, sound}, kind, data}}`;
- `fcm` (Android, этап 11): `fcm: {url, message: {token, notification:
  {title, body}, android: {priority, notification: {channel_id: "aura",
  sound}}, data: {kind, notification_id, data}}}` — тело готово к
  `POST {fcm.url}` (FCM HTTP v1) с заголовком `Authorization: Bearer
  <OAuth2-токен сервисного аккаунта>`; значения `data` — строки
  (требование FCM), полезная нагрузка уведомления сериализуется в
  `data.data`.

Переменные окружения: `AURA_PUSH_DRIVER` (`dev`|`webhook`),
`AURA_PUSH_WEBHOOK_URL`, `AURA_APNS_TOPIC` (`ai.aura.app`), `AURA_APNS_URL`
(`https://api.push.apple.com`), `AURA_FCM_URL`
(`https://fcm.googleapis.com/v1/projects/aura/messages:send` — замените
`aura` на id вашего проекта Firebase).

## HTTP API Python AI Service

| метод | путь | назначение |
| --- | --- | --- |
| `POST` | `/v1/agent/run` | главный запрос Ауры |
| `POST` | `/v1/agent/negotiate` | Agent-to-Agent переговоры |
| `POST` | `/v1/speech/transcribe` | распознавание речи (Whisper-совместимое, ru/en/auto) |
| `POST` | `/v1/memory/extract` | извлечение фактов в память |
| `GET` | `/v1/memory/{user_key}` | записи памяти (можно `?query=` — гибридный RAG) |
| `POST` | `/v1/planner/slots` | свободные слоты |
| `POST` | `/v1/planner/understand` | разбор фразы (намерение, окно, длительность) |
| `GET` | `/v1/tools` | список инструментов |
| `GET` | `/healthz` | состояние (провайдер LLM, провайдер STT, бэкенды) |

Все запросы, кроме `/healthz`, требуют заголовок `X-Aura-Token` со значением
`AURA_AI_TOKEN`. Без настроенного токена сервис отвечает `503` (fail closed),
кроме локальной разработки с `AURA_AI_INSECURE_DEV=1`. `/healthz` отдаёт
только типы бэкендов (без DSN и путей). `POST /v1/agent/run`: `execute`
по умолчанию `false` — AI только планирует; даже при `execute: true`
действия с внешним эффектом (`send_email`, `send_message`, `book_table`) не
исполняются (`results[].error = "server_only: …"`) — их выполняет C++-сервер.
Ответ LLM проверяется: `confidence` — число 0..1, `memory_updates` — не больше
5 записей вида `fact|preference|schedule|contact` с текстом до 300 символов,
`actions[].args` — только объект. Некорректный ввод — `4xx`, не `500`.
Swagger — `/docs`.

### Гибридный RAG памяти (этап 8)

Ранжирование воспоминаний под запрос (`memory.rank`, используется в
`load_context_memory` и `GET /v1/memory/{user_key}?query=`) — гибридное:

* **лексика (Okapi BM25)** по основам токенов + буст весом записи — работает
  всегда, без внешних зависимостей;
* **семантика** по косинусной близости эмбеддингов — включается, когда задан
  эндпоинт эмбеддингов; итоговый порядок объединяется **Reciprocal Rank Fusion**;
* **graceful degrade**: если эмбеддинги не настроены или недоступны (ошибка
  сети/формата), ранжирование остаётся лексическим и не падает.

Настройки AI-сервиса: `AURA_RAG_MODE` (`lexical` | `hybrid` | `auto`, по
умолчанию `auto` — семантика включается при заданном `AURA_EMBEDDINGS_URL`),
`AURA_EMBEDDINGS_URL` (OpenAI-совместимый `POST /embeddings`),
`AURA_EMBEDDINGS_MODEL` (по умолч. `text-embedding-3-small`),
`AURA_EMBEDDINGS_API_KEY`, `AURA_EMBEDDINGS_TIMEOUT`. Без `AURA_EMBEDDINGS_URL`
гибрид выключен — используется чистый BM25.

## Как выглядит A2A-ответ

```json
{
  "accepted": true,
  "slot": {"start": "2026-09-19T10:00:00+00:00", "end": "2026-09-19T11:00:00+00:00",
           "score": 0.72, "reason": "общее окно в графиках"},
  "place": {"name": "Кофе на полпути", "city": "Керкраде", "rating": 4.7, "tags": ["vegan"]},
  "message": "Да, 19.09 в 10:00 подходит. Предлагаю Кофе на полпути."
}
```
