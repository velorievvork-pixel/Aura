# Протокол Selfvoy

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

Ограничения соединения: до входа кадр не больше 64 КБ (`SELFVOY_MAX_PREAUTH_FRAME`,
больше — закрытие 1009/1002); соединение без входа закрывается через 30 с
(`SELFVOY_AUTH_TIMEOUT`, код закрытия 4408); с одного IP — не больше 20 соединений
(`SELFVOY_MAX_CONNECTIONS_PER_IP`, лишнее получает HTTP 429 на рукопожатии). В JSON-объекте
не больше 1000 ключей.

Открытое соединение не переживает свою сессию: при отзыве сессии
(`sessions.revoke`/`revokeAll`, `auth.logout` с тем же токеном на другом сокете),
смене/сбросе пароля, повторном использовании refresh-токена или блокировке
аккаунта сервер шлёт событие `session.revoked` (`{reason}`) и закрывает сокет с
кодом 4401. Кроме того, сессия соединения сверяется с БД не реже чем раз в 15 с
(`SELFVOY_AUTH_RECHECK`): отозванная/истёкшая → `unauthorized` «сессия истекла или
отозвана — войдите снова», соединение разлогинивается.

Все типы, кроме `auth.register`, `auth.login`, `auth.login2fa`, `auth.verifyEmail`,
`auth.resendCode`, `auth.forgotPassword`, `auth.resetPassword`,
`auth.refresh`, `ping`, `server.info`, `invite.get`, `invite.respond`,
`booking.get`, `booking.book`, `booking.manage`, `booking.reschedule`,
`booking.cancel`, `booking.resendLink`, `booking.confirm`, `booking.paymentClaim`,
требуют авторизации.

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
| `pro_required` | действие доступно только на тарифе Pro: сервер запущен с `SELFVOY_PLANS_ENFORCED=1`, а Pro у пользователя не действует (`user.plan.name = "free"`). При `SELFVOY_PLANS_ENFORCED=0` (бета, по умолчанию) не возвращается |

## Методы

### Авторизация

Поток аккаунта (auth v3): регистрация → код подтверждения email → вход.
Регистрация сессию не выдаёт. В каждом `user` есть `role`
(`user`|`moderator`|`admin`, см. «Администрирование») и тариф
`plan: {name: "pro"|"free", until: ISO|"", enforced: bool}` — `name` честный
(Pro, пока `until` в будущем) независимо от `enforced`; `enforced` —
`SELFVOY_PLANS_ENFORCED` (false — бета: функции Pro открыты всем; true — без
действующего Pro они отвечают `pro_required`). Pro выдаёт администратор
(`admin.setPlan`) или команда деплоя `selfvoy pro EMAIL ДНЕЙ`.

| тип | payload | ответ |
| --- | --- | --- |
| `auth.register` | `display_name`, `email`, `password`, `ref?` (реферальный код, 8 букв/цифр, регистр не важен), `ref_source?` (`link`/`booking`/`invite`) | `requires_verification`, `user`, `email_code` (только `SELFVOY_MAIL_DRIVER=dev` + `SELFVOY_DEV_MODE=1`); занятый email → `conflict`; больше 10 регистраций с IP в час → `forbidden`. Неверный `ref` молча игнорируется |
| `auth.verifyEmail` | `email`, `code` (6 цифр) | `verified`, `user` (повтор для уже подтверждённого адреса — только `verified`, без данных пользователя); после 5 неверных кодов код сгорает (нужен `auth.resendCode`) |
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
(AES-256-GCM, ключ — `SELFVOY_2FA_KEY`). Настройка: `auth.setup2fa` выдаёт секрет
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

### Selfvoy

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
`SELFVOY_STT_URL`/`SELFVOY_STT_API_KEY` работает детерминированный `mock`
(для разработки), иначе — реальный Whisper-бэкенд. Пустое аудио → `bad_request`,
превышение `SELFVOY_STT_MAX_BYTES` → ошибка, недоступность бэкенда → `upstream_error`.

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

`privacy` — что Selfvoy раскрывает чужому Selfvoy в переговорах (`agent.negotiate`,
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
| `tool.run` | `tool`, `args{}`, `chat_id?` | результат инструмента; проходит тот же барьер `tool_permissions`, что и действия Selfvoy: `deny` → `forbidden`, `ask` (по умолчанию для опасных) → `{requires_confirmation: true, confirmation_id, summary, tool, status: "pending"}` без исполнения (дальше `confirmation.approve`/`deny`) |

Инструменты: `send_message`, `create_note`, `create_reminder`, `send_email`,
`find_cafe`, `book_table`, `check_calendar`, `suggest_time`. В режиме
`SELFVOY_TOOLS_MODE=sandbox` внешние API заменяются детерминированными ответами
(тот же контракт, что в `ai/selfvoy_ai/tools.py`). `tool.list` возвращает для
каждого инструмента флаг `dangerous` и `default_mode`.

### Разрешения и подтверждения (этап 8)

Ядро безопасности v3: **LLM лишь формирует намерение, исполняет сервер и только
после проверки прав.** Каждое действие Selfvoy проходит барьер:

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
сообщением `kind: agent_action` (`origin: selfvoy-agent`, `confirmed: true`).
Изменения разрешений и подтверждения пишутся в `audit_logs`
(`tool_permission`, `action_approved`, `action_denied`).

### Задачи и напоминания (этап 8)

Задачи хранятся в `tasks(id, user_id, chat_id, title, notes, status, priority,
due_at, remind_at, reminded_at, …)`; статусы `pending`/`done`/`cancelled`.
Напоминание — задача с `remind_at`: фоновый планировщик сервера
(`SELFVOY_SCHEDULER_INTERVAL_MS`, по умолчанию 30 с) находит задачи с наступившим
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
(`integration_oauth_states`, TTL `SELFVOY_OAUTH_STATE_TTL`, по умолчанию 600 с).
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

Переменные окружения: `SELFVOY_GOOGLE_CLIENT_ID`, `SELFVOY_GOOGLE_CLIENT_SECRET`,
`SELFVOY_GOOGLE_AUTH_URL`, `SELFVOY_GOOGLE_TOKEN_URL`, `SELFVOY_GOOGLE_REVOKE_URL`,
`SELFVOY_GOOGLE_CALENDAR_URL`, `SELFVOY_GOOGLE_GMAIL_URL`, `SELFVOY_OAUTH_STATE_TTL`.
По умолчанию URL — реальные эндпоинты Google (прод — за TLS-терминирующим
прокси); e2e подменяет их локальным mock-сервером (`SELFVOY_E2E_GOOGLE_MOCK=1`).

### Уведомления и push (этап 13)

Единая «входящая» уведомлений: каждое уведомление сохраняется в таблице
`notifications` и доставляется живым сессиям событием `notification.new`.
Виды (`kind`): `task.due` (напоминание), `confirmation.requested` (опасная
операция ждёт решения), `a2a.proposal` (другой агент начал переговоры),
`login.new` (новый вход), `twofactor.enabled` / `twofactor.disabled`
(изменение 2FA), `invite.accepted` / `invite.declined` (гость ответил на
приглашение по ссылке), `booking.created` / `booking.rescheduled` /
`booking.cancelled` (гость записался, перенёс или отменил встречу на
странице записи; `payload.cancelled_by: "system"` — запись отменена, потому что
оплата не поступила), `booking.confirmed` (гость нажал «Приду»), `booking.payment`
(оплата записи: `{booking_id, page_id, status, amount, currency, guest_name}`,
`status` — `claimed` гость отметил «Я оплатил(а)», `paid` оплата через ЮKassa
прошла, `refunded` деньги возвращены через ЮKassa, `refund_due` — верните оплату
гостю вручную), `referral.joined`
(приглашённый по реферальной ссылке подтвердил email), `referral.reward`
(приглашённому начислен месяц Pro). Push-доставка — через устройства в `push_devices`
(платформы `apns` | `fcm` | `webhook` | `dev`); платформенный конверт
уходит во внешний шлюз (`SELFVOY_PUSH_DRIVER=webhook`,
`SELFVOY_PUSH_WEBHOOK_URL`), который подписывает ключи провайдера (JWT для
APNs, OAuth2 сервисного аккаунта для FCM) и форвардит запрос — сам
C++-сервер TLS/HTTP2 не делает и ключи провайдеров не хранит.
Тихие часы и отключённые виды читаются из настроек пользователя:
`prefs.notifications = {"quiet_hours": {"start":"22:00","end":"08:00"},
"muted_kinds": ["login.new"]}` (время UTC; гасят только push и Telegram, in-app
доставляется всегда). `prefs.notifications.telegram: false` — не дублировать
уведомления в Telegram (см. «Telegram-бот»).

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
  {title, body}, android: {priority, notification: {channel_id: "selfvoy",
  sound}}, data: {kind, notification_id, data}}}` — тело готово к
  `POST {fcm.url}` (FCM HTTP v1) с заголовком `Authorization: Bearer
  <OAuth2-токен сервисного аккаунта>`; значения `data` — строки
  (требование FCM), полезная нагрузка уведомления сериализуется в
  `data.data`.

Переменные окружения: `SELFVOY_PUSH_DRIVER` (`dev`|`webhook`),
`SELFVOY_PUSH_WEBHOOK_URL`, `SELFVOY_APNS_TOPIC` (`app.selfvoy`), `SELFVOY_APNS_URL`
(`https://api.push.apple.com`), `SELFVOY_FCM_URL`
(`https://fcm.googleapis.com/v1/projects/selfvoy/messages:send` — замените
`selfvoy` на id вашего проекта Firebase).

### Приглашения по ссылке (встреча с человеком без Selfvoy)

Владелец договаривается о встрече с человеком, у которого нет Selfvoy: сервер
подбирает свободные слоты владельца (AI-сервис, `POST /v1/agent/slots` — только
рабочие часы в часовом поясе владельца, не раньше чем через час, до 2 слотов в
день, не больше 6) и выдаёт ссылку. Гость открывает её в веб-клиенте
(`<SELFVOY_PUBLIC_URL>/#/i/<токен>`) без регистрации, выбирает время или
отказывается. Сервер не доверяет ответу AI: проверяет длительность, окно,
будущее время, занятость и пересечения. Токен — 32 случайных байта
(base64url, 43 символа); в БД только его sha256, поэтому ссылка показывается
владельцу **один раз** — в `invite.list` её нет.

Методы владельца (нужен вход):

| тип | payload | ответ |
| --- | --- | --- |
| `invite.create` | `{topic (1–120 символов, одна строка), duration_minutes? (15–240, 60), window_hours? (1–336, 168), note? (≤ 500), guest_name? (≤ 80)}` | `{invite, token, url}` — `url` пуст, если не задан `SELFVOY_PUBLIC_URL` (клиент строит ссылку сам) |
| `invite.list` | `{limit? (1–100, 50)}` | `{invites: [invite]}` — новые сверху |
| `invite.cancel` | `{invite_id}` | `{invite}` — только своё и открытое |

`invite` (вид владельца): `{id, topic, note, duration_minutes, status, slots:
[{start, end}], guest_name, guest_email, guest_comment, chosen: {start, end} | {},
expires_at, created_at, responded_at}`. `status`: `open` | `accepted` |
`declined` | `cancelled` | `expired` (открытое, у которого прошло начало
последнего слота; в БД не хранится). Ошибки `invite.create`: `bad_request`;
`conflict` «нет свободных окон в выбранный период»; `upstream_error` — AI-сервис
недоступен; `forbidden` — больше 20 открытых приглашений.

Методы гостя (публичные, без входа; доступ — по токену из ссылки):

| тип | payload | ответ |
| --- | --- | --- |
| `invite.get` | `{token}` | `{invite}` — вид гостя |
| `invite.respond` | `{token, action: "accept" \| "decline", start? (для accept — start одного из слотов), guest_name (1–80), guest_email? (≤ 254), comment? (≤ 500)}` | `{invite}` — вид гостя |

Вид гостя: `{topic, note, duration_minutes, owner_name, owner_timezone, status,
slots, guest_name, chosen, expires_at, ref}` (`ref` — реферальный код владельца) — без email владельца и данных других
гостей; `slots` у открытого — только будущие и ещё свободные у владельца (может
быть пустым). Ошибки: `not_found` — неизвестный токен; `conflict` — приглашение
уже закрыто, срок истёк или время уже занято; `bad_request` — слот не из списка,
пустое имя, неверный email; `forbidden` — больше 30 `invite.get` или 10
`invite.respond` в минуту с одного IP.

`accept` атомарно закрывает приглашение (второй ответ — `conflict`) и
добавляет владельцу: запись расписания «Встреча: <тема> — <гость>
(<start>/<end>)» (интервал занимает время в переговорах и других
приглашениях), задачу с `due_at = start` и `remind_at = start − 15 мин`,
уведомление `invite.accepted` (`payload: {invite_id, start, end, guest_name,
task_id}`). `decline` — уведомление `invite.declined` (`payload: {invite_id,
guest_name, comment}`).

### Страницы записи (постоянная ссылка для людей без Selfvoy)

Владелец заводит постоянную публичную ссылку `<SELFVOY_PUBLIC_URL>/#/b/<slug>`
(`slug` — 10 символов `[a-z0-9]`). Гость без регистрации видит свободное время
владельца и записывается сам — на одну встречу или на еженедельную серию.
Свободное время подбирает AI-сервис (`POST /v1/agent/slots` с `per_day`,
`lead_minutes`, `weekends`; только рабочие часы в поясе владельца), ответ
кэшируется на минуту. Сервер не доверяет AI: проверяет длительность, окно,
пересечения, а при каждом показе и записи — запас до начала, занятость
владельца (расписание, другие записи, встречи из приглашений) и лимит записей
в местный день владельца. Встречи записей сами занимают время владельца — в
переговорах, приглашениях и на других страницах.

Методы владельца (нужен вход):

| тип | payload | ответ |
| --- | --- | --- |
| `booking.pageCreate` | `{title (1–120, одна строка), description? (≤ 1000), duration_minutes? (15–240, 30), days_ahead? (1–60, 14), max_per_day? (1–16, 4), notice_hours? (0–168, 2), max_series? (1–12, 1), weekends? (bool, false), guest_reminders? (bool, true), hide_branding? (bool, false), …поля цены и политики отмены (F4, ниже)}` | `{page}`; больше 10 страниц → `forbidden`; `hide_branding: true` или `prepay` ≠ `none` без Pro при `SELFVOY_PLANS_ENFORCED=1` → `pro_required` |
| `booking.pageList` | — | `{pages: [page]}` — новые сверху, без удалённых |
| `booking.pageUpdate` | `{page_id, …любые поля pageCreate, status? (active \| paused)}` | `{page}` — меняются только переданные поля; включить `hide_branding` или предоплату (`prepay` из `none`) без Pro (при `SELFVOY_PLANS_ENFORCED=1`) → `pro_required`, выключить можно всегда |
| `booking.pageDelete` | `{page_id}` | `{deleted: true}` — ссылка перестаёт работать, сделанные записи остаются |
| `booking.list` | `{page_id? (0 — все), active_only?, limit? (1–200, 50)}` | `{bookings: [booking]}` — новые сверху |
| `booking.ownerCancel` | `{booking_id, occurrence? (start одной встречи)}` | `{booking}`; без `occurrence` — все предстоящие встречи; оплаченная запись, отменённая целиком, — возврат (см. «Цена, предоплата и политика отмены») |
| `booking.paymentSet` | `{booking_id, status: paid \| waived \| refunded}` | `{booking}` — отметка оплаты владельцем (`paid_at` / `refunded_at` = сейчас); у записи нет оплаты → `bad_request`; `paid` — гостю письмо «Оплата получена» |

`page`: `{id, slug, url, title, description, duration_minutes, days_ahead,
max_per_day, notice_hours, max_series, weekends, status (active | paused),
guest_reminders, hide_branding, price, currency, prepay, deposit, payment_url,
payment_note, pay_within_minutes, cancel_hours, late_cancel, policy_text, upcoming,
created_at, updated_at}`; `url` пуст
без `SELFVOY_PUBLIC_URL`. `guest_reminders` — напоминать гостю о встрече
(бесплатно), `hide_branding` — скрыть подпись «Запись через Selfvoy» (Pro).
`booking` (вид владельца): `{id, page_id, page_title, guest_name, guest_email,
guest_comment, status (active | cancelled), cancelled_by ("" | guest | owner),
reschedules, occurrences: [{start, end, local, status (active | cancelled),
confirmed_at}], reminders: [{start, kind (24h | 2h), due_at, sent_at}],
payment (объект оплаты целиком или null), created_at, updated_at}`;
`cancelled_by` у отменённой записи — `guest`, `owner` или `system` (оплата не
поступила к сроку); `confirmed_at` — когда гость нажал «Приду» ("" — не
подтверждал), `reminders` — запланированные и отправленные напоминания гостю
(`sent_at` "" — ещё не отправлено).

Методы гостя (публичные, без входа). Соединение без входа сервер закрывает
через `SELFVOY_AUTH_TIMEOUT` (30 с), но если по нему успешно вызывались гостевые
методы `invite.*` / `booking.*` — через 10 минут после последнего такого вызова:
гость успевает выбрать время, а ошибочные запросы срок не продлевают.

| тип | payload | ответ |
| --- | --- | --- |
| `booking.get` | `{slug}` | `{page}` — вид гостя |
| `booking.book` | `{slug, start (start свободного слота), guest_name (1–80), guest_email? (≤ 254), comment? (≤ 500), count? (1–max_series, 1)}` | `{booking, token, url, telegram_url}` — токен ссылки управления (43 символа base64url) показывается **один раз**; `url` — `<SELFVOY_PUBLIC_URL>/#/m/<токен>` или пусто; `telegram_url` — ссылка на бота для напоминаний (`https://t.me/<бот>?start=g_…`) или "" (бот не настроен) |
| `booking.manage` | `{token}` | `{booking}` — вид гостя по ссылке управления |
| `booking.reschedule` | `{token, start, occurrence? (start переносимой встречи; по умолчанию ближайшая)}` | `{booking}`; не больше 3 переносов на запись |
| `booking.cancel` | `{token, occurrence?, reason? (≤ 500)}` | `{booking}`; без `occurrence` — все предстоящие встречи |
| `booking.resendLink` | `{slug, email}` | `{sent: true}` — всегда одинаково (нельзя узнать, есть ли запись); если на странице есть активная запись с этим email, ссылка управления перевыпускается (прежняя перестаёт работать) и уходит письмом |
| `booking.confirm` | `{token, occurrence? (start встречи; по умолчанию ближайшая предстоящая активная)}` | `{booking}` (вид гостя, `slots` пуст) — у встречи ставится `confirmed_at`; повтор ничего не меняет и не уведомляет владельца второй раз; лимит — как у переноса/отмены |
| `booking.paymentClaim` | `{token}` | `{booking}` (вид гостя, `slots` пуст) — «Я оплатил(а)» для оплаты по ссылке: `pending` → `claimed`, владельцу — `booking.payment` (`claimed`); повтор и уже `paid`/`waived` — без изменений; оплата через ЮKassa → `bad_request`; нет оплаты → `bad_request`; запись отменена (оплата `expired`) → `conflict`; лимит — как у переноса/отмены |

Письма гостю (если при записи указан email; драйвер почты — как у кодов
подтверждения, `SELFVOY_MAIL_DRIVER`): подтверждение записи со ссылкой управления
(и строкой «Напоминания в Telegram: <url>», если бот настроен),
перенос, отмена гостем и отмена владельцем (со ссылкой «Выбрать другое время»),
новая ссылка по `booking.resendLink`. Те же извещения уходят в Telegram-чаты,
подключённые к записи (простой текст; ссылка управления — url-кнопкой, если адрес
https); лимит — 10 в час на адрес и на чат, 200 в час на владельца. Время — местное время владельца с
поясом. В письмах только текст сервера и владельца (название страницы, имя
владельца): имя и комментарий гостя туда не попадают, на один адрес — не больше
10 писем в час.

Вид страницы для гостя: `{slug, title, description, duration_minutes,
max_series, owner_name, owner_timezone, status, slots: [{start, end, day}],
slots_unavailable, ref, branding, price, currency, prepay, deposit, payment_note,
payment_url, pay_within_minutes, policy: {cancel_hours, late_cancel, text}}` —
без email владельца и чужих записей; `day` —
местная дата владельца; `slots_unavailable: true` — AI-сервис недоступен
(страница видна, время не подобрано); `ref` — реферальный код владельца для
ссылки «Заведите свой Selfvoy». Вид записи для гостя: `{page_title, page_slug
("" — запись на странице закрыта), owner_name, owner_timezone,
duration_minutes, guest_name, status, cancelled_by, occurrences, can_cancel,
can_reschedule, can_change, reschedules_left, slots (свободное время для
переноса ближайшей встречи), slots_unavailable, ref, branding, telegram_url,
telegram_linked, payment (без `provider_id`; null — оплаты нет), late, policy,
payment_note}`, у каждой встречи в `occurrences` — `confirmed_at` ("" — не
подтверждена); `telegram_url` — ссылка подписки на напоминания в Telegram
("" — бот не настроен или запись отменена; после `booking.resendLink` прежняя
ссылка перестаёт работать), `telegram_linked` — к записи уже подключён чат. `can_cancel` — есть
предстоящая встреча (отменить можно и на паузе, и после удаления страницы);
`can_reschedule` — к тому же страница открыта и переносы не исчерпаны;
`can_change` — устаревший синоним `can_reschedule`. `late` — до ближайшей встречи
меньше `cancel_hours` часов; при `late_cancel: block` тогда `can_cancel` и
`can_reschedule` — `false`.

Подпись (F3): `branding: {show, text: "Запись через Selfvoy", url}`, `url` —
`<SELFVOY_PUBLIC_URL>/#/signup?ref=<код владельца>&src=booking` ("" без
`SELFVOY_PUBLIC_URL`). `show: false` — только пока владелец скрыл подпись
(`hide_branding`) и у него действует Pro (или `SELFVOY_PLANS_ENFORCED=0`):
Pro закончился — подпись снова показывается. При `show` и непустом `url` в
письма гостю последней строкой и в сообщения бота гостю добавляется «Запись
через Selfvoy — заведите свой бесплатно: <url>». Поле `ref` оставлено для
совместимости.

Напоминания гостю (F1, если у страницы `guest_reminders`): при записи и переносе
на каждую активную встречу планируются `24h` (за сутки; если до начала ≥ 25 ч)
и `2h` (за 2 часа; если до начала ≥ 2 ч 30 мин); отмена или перенос встречи
удаляет её неотправленные напоминания. Планировщик сервера отправляет наступившие
(атомарный захват: на PostgreSQL `FOR UPDATE SKIP LOCKED` — каждое уходит один
раз даже при нескольких экземплярах) письмом и в Telegram-чаты записи с теми же
лимитами, что у извещений. Не отправляются, если запись или встреча уже не
активна, встреча началась, у страницы выключен `guest_reminders` (проверяется в
момент отправки) или «за сутки» опоздало до последних 2 часов. Тема — «Напоминание:
завтра в 15:00 — <название страницы>» («сегодня в …» или дата), время — местное
время владельца, в тексте — с поясом и имя владельца. Кнопка письма «Приду» —
`<SELFVOY_PUBLIC_URL>/#/m/<токен>?confirm=<start ISO>`, строкой — «Перенести или
отменить: <SELFVOY_PUBLIC_URL>/#/m/<токен>»; у старых записей (токен не сохранён)
или без `SELFVOY_PUBLIC_URL` вместо ссылок — «Перенести или отменить можно по
ссылке из письма о записи». В Telegram — кнопка «Приду» (callback
`cf:<booking_id>:<start_unix>`) и url-кнопка «Перенести или отменить» (только
https-ссылка). «Приду» по ссылке вызывает `booking.confirm`; владельцу —
уведомление `booking.confirmed` (`{booking_id, page_id, start, guest_name}`) —
один раз на встречу. Перенос встречи сбрасывает её `confirmed_at`.

Серия — та же встреча каждую неделю в том же **местном времени владельца**
(AI `POST /v1/agent/series` считает повторы в его поясе: 10:00 остаётся 10:00
после перехода на летнее/зимнее время; сервер проверяет первую встречу,
длительность и шаг «неделя ± 2 часа»). Каждая встреча должна быть свободна и не
превышать лимит своего дня, иначе `conflict` «в неделю … это время занято»;
без AI-сервиса серия не записывается (`upstream_error`). У каждой встречи в
`occurrences` есть `local` — местное время владельца `YYYY-MM-DDTHH:MM`. На каждую встречу владельцу создаётся задача с `due_at = start`
и `remind_at = start − 15 мин` (при переносе — пересоздаётся, при отмене —
удаляется) и уведомление `booking.created` (`{booking_id, page_id, start, end,
count, guest_name}`), `booking.rescheduled` (`{booking_id, page_id, from,
start, end, guest_name}`), `booking.cancelled` (`{booking_id, page_id,
cancelled, guest_name, reason}`). Запись без предстоящих встреч получает
`status: cancelled`.

Ошибки: `not_found` — неизвестный/удалённый slug или токен, чужая запись;
`conflict` — время заняли, запись на паузе, запись уже отменена, переносы
исчерпаны, подтверждаемой встречи нет (прошла или отменена), до встречи меньше
`cancel_hours` при `late_cancel: block` («до встречи меньше N ч — свяжитесь со
специалистом»); `bad_request` — слот не из свободных, неверные поля; `upstream_error` —
AI-сервис недоступен при записи/переносе, ЮKassa не создала платёж; `forbidden` — лимиты по IP
(30 `booking.get`/`booking.manage` в минуту, 10 записей в минуту и 30 в час,
10 переносов/отмен в минуту).

### Цена, предоплата и политика отмены (F4)

Поля страницы записи (`booking.pageCreate` / `booking.pageUpdate`; в
`pageUpdate` меняются только переданные):

| поле | значение |
| --- | --- |
| `price` | цена в минимальных единицах валюты (копейки), целое 0–100 000 000; 0 — не показывать |
| `currency` | `RUB` (по умолчанию) \| `USD` \| `EUR` \| `KZT` \| `BYN` |
| `prepay` | `none` (по умолчанию) \| `full` (вся сумма: `price × число встреч`) \| `deposit` (депозит один раз на запись) |
| `deposit` | депозит в минимальных единицах; при `prepay: deposit` — от 1 до `price` |
| `payment_url` | ссылка оплаты владельца: только `https://`, ≤ 500 символов, без пробелов |
| `payment_note` | реквизиты / СБП — одна строка, ≤ 300 символов |
| `pay_within_minutes` | сколько держится время до оплаты: 15–1440, 60 |
| `cancel_hours` | отмена с возвратом — не позже чем за N часов: 0–168, 0 (отменять можно всегда) |
| `late_cancel` | `allow` (по умолчанию: поздняя отмена разрешена, без возврата) \| `block` (позже срока гость не отменяет и не переносит) |
| `policy_text` | текст политики отмены для гостя, ≤ 500 символов |

`prepay` ≠ `none` требует `price > 0` и способ оплаты: `payment_url`,
`payment_note` или подключённую ЮKassa (иначе `bad_request`), а при
`SELFVOY_PLANS_ENFORCED=1` — действующий Pro для включения (`pro_required`; уже
включённую предоплату можно править и после окончания Pro). Цена без
предоплаты и политика отмены — бесплатно. Способ оплаты проверяется при
создании страницы и при правке полей оплаты (`prepay`, `price`, `deposit`,
`payment_url`, `payment_note`). Цена страницы попадает и в пост «свободные
окна» (`booking.slotsPost`).

Подключение ЮKassa владельцем (свой магазин — деньги гостей идут ему напрямую,
Selfvoy их не касается):

| тип | payload | ответ |
| --- | --- | --- |
| `payments.status` | — | `{connected, provider ("yookassa" \| ""), shop_id, connected_at}` |
| `payments.connect` | `{provider: "yookassa", shop_id (цифры, ≤ 20), secret_key (секретный ключ магазина live_…/test_…)}` | `{connected: true, provider, shop_id, connected_at}`; ключи проверяются запросом `GET /me` (Basic `shop_id:secret_key`): неверные ключи или ключ другого магазина → `bad_request`, ЮKassa не отвечает → `upstream_error`; без Pro при `SELFVOY_PLANS_ENFORCED=1` → `pro_required`; без `SELFVOY_PUBLIC_URL` на сервере → `bad_request` (гостю некуда вернуться после оплаты); 10 попыток в час |
| `payments.disconnect` | — | `{connected: false}` |

Секретный ключ хранится зашифрованным (AES-256-GCM, ключ — как у TOTP-секретов,
AAD `selfvoy-payment-secret`), наружу и в журнал не попадает. Адрес API —
`SELFVOY_YOOKASSA_API_URL` (по умолчанию `https://api.yookassa.ru/v3`). Чеки
54-ФЗ (объект `receipt`) сервер не отправляет: самозанятый формирует чек в «Мой
налог», ИП/организация — настройками магазина в личном кабинете ЮKassa.

Оплата записи — объект `payment` (`bookings.payment`; тот же формат у пакетов F5):
`{status, amount, currency, kind (full | deposit | package), method (link |
yookassa), provider_id (id платежа ЮKassa; гостю не отдаётся), action_url
(куда отправить гостя платить; "" — только реквизиты), due_at, paid_at,
refunded_at}`; `status`: `pending` (ждём оплату) → `paid` / `claimed` (гость
сказал, что оплатил) / `expired` (не оплачено к сроку или запись отменена до
оплаты); владелец может поставить `paid`, `waived` (без оплаты) или `refunded`.

- `booking.book` на странице с предоплатой создаёт оплату `pending` на
  `price × count` (`full`) или `deposit`; `due_at` = сейчас + `pay_within_minutes`
  (не позже начала первой встречи). ЮKassa подключена — `POST /payments`
  (`Idempotence-Key: bk-<booking_id>`, `capture: true`, `confirmation: {type:
  redirect, return_url: <ссылка управления>?paid=1}`, `description` ≤ 128
  символов «<страница>, <дата>», `metadata.booking_id`), `action_url` —
  `confirmation_url` ЮKassa; платёж не создался → запись отменяется
  (`cancelled_by: system`), ответ `upstream_error`. Иначе способ `link`:
  `action_url` = `payment_url`, гость платит сам и нажимает «Я оплатил(а)»
  (`booking.paymentClaim`). Письмо о записи — с суммой, сроком, ссылкой оплаты
  и реквизитами. Если к моменту записи способа оплаты нет (ЮKassa отключена, а
  ссылки и реквизитов нет), запись создаётся без оплаты.
- Время держится до `due_at`. Не оплачено (`pending`; `claimed` не истекает) —
  планировщик отменяет запись (`cancelled_by: system`), оплата `expired`;
  гостю — письмо/Telegram «Selfvoy: оплата не поступила — запись отменена» со
  ссылкой «Выбрать другое время», владельцу — `booking.cancelled`
  (`cancelled_by: "system"`).
- ЮKassa: статус платежа проверяется в планировщике (не чаще раза в 30 с на
  платёж; к сроку — ещё раз) и при каждом `booking.manage`: `succeeded` →
  `paid` (владельцу `booking.payment`, гостю письмо «Оплата получена»),
  `canceled` → `expired` и отмена записи. ЮKassa не отвечает к сроку — запись
  ждёт до часа сверх срока. Гость заплатил уже после отмены — деньги
  возвращаются автоматически (при `booking.manage`).
- Возвраты (запись отменена целиком): владелец отменил оплаченную через ЮKassa
  запись — `POST /refunds` на всю сумму (`Idempotence-Key: rf-<booking_id>`),
  оплата `refunded`; оплаченную по ссылке (`paid`/`claimed`) — владельцу
  `booking.payment` `refund_due` «верните оплату вручную». Гость отменил не
  позже чем за `cancel_hours` — так же полный возврат (ЮKassa — автоматически,
  по ссылке — `refund_due` владельцу); позже — без возврата (депозит остаётся у
  специалиста, в письме об отмене — «оплата не возвращается»), а при
  `late_cancel: block` гость не может ни отменить, ни перенести встречу
  (`conflict` «до встречи меньше N ч — свяжитесь со специалистом»). Отмена одной
  встречи серии оплату не меняет. ЮKassa не приняла возврат — `refund_due`.

### Пост «свободные окна» (F6)

Владелец копирует готовый текст с реальными свободными окнами страницы записи
в Telegram-канал, VK или куда угодно. Окна — те же, что гость видит в
`booking.get` (кандидаты AI-сервиса + запас до начала, занятость владельца,
лимит записей в день).

| тип | payload | ответ |
| --- | --- | --- |
| `booking.slotsPost` | `{page_id, days? (1–14, 7), format? ("telegram" \| "vk" \| "plain", telegram)}` | `{text, slots (число окон в тексте), url (ссылка страницы или "")}` |

```
Свободные окна: Консультация (50 мин, 2 500 ₽)

Пн, 21 окт: 10:00, 11:30, 15:00
Вт, 22 окт: 12:00

Записаться: https://selfvoy.example.com/#/b/<slug>
```

- Время — местное время владельца (`local` слотов AI); если у какого-то окна
  его нет, все окна в UTC и в заголовке — «, время UTC».
- В строке дня не больше 8 времён, остальные — «и ещё N».
- Нет окон — «На ближайшие N дней свободных окон нет» (и ссылка).
- `vk` — то же, ссылка строкой «Запись онлайн: …»; `plain` — без пустых строк.
- Цена в заголовке — только если у страницы задана цена (поле `price`, F4;
  пока его нет — заголовок без цены).
- Ссылка — `<SELFVOY_PUBLIC_URL>/#/b/<slug>`; без `SELFVOY_PUBLIC_URL` строки
  со ссылкой нет, `url` пуст (клиент добавляет адрес сам).

Ошибки: `not_found` — нет страницы, чужая или удалённая; `conflict` — запись
на странице на паузе; `forbidden` — email владельца не подтверждён или больше
30 постов в час; `bad_request` — неверные `days`/`format`; `upstream_error` —
AI-сервис недоступен.

### Календарь: ICS-подписка (F6)

Владелец подписывается на свои встречи в Google/Apple Календаре, Outlook
(«Добавить календарь по URL»).

| тип | payload | ответ |
| --- | --- | --- |
| `calendar.feed` | — | `{enabled, url, path}` — `url`/`path` всегда пусты: токен не хранится |
| `calendar.feedReset` | — | `{enabled: true, url, path}` — новая ссылка, прежняя перестаёт работать; токен виден только в этом ответе; больше 20 в час → `forbidden` |
| `calendar.feedDisable` | — | `{enabled: false}` — ссылка перестаёт работать |

`path` = `/ics/<токен>.ics` (токен — 43 символа base64url; в БД только sha256,
`users.ics_token_hash`). `url` = `SELFVOY_SERVER_PUBLIC_URL` + `path` (внешний
адрес сервера, например `https://api.example.com`); без переменной `url` = "",
клиент собирает адрес из адреса сервера: `wss://host` → `https://host`,
`ws://host` → `http://host`.

HTTP на том же порту, что WebSocket (обычный запрос без `Upgrade: websocket`):

| запрос | ответ |
| --- | --- |
| `GET /healthz` | `200 application/json` `{"status":"ok"}` — только факт работы |
| `GET /ics/<токен>.ics` | `200 text/calendar; charset=utf-8`, `Cache-Control: no-store` |
| неизвестный/выключенный/старый токен, владелец заблокирован | `404` |
| другой метод на `/ics/…` | `405`, `Allow: GET` |
| больше 60 запросов `/ics/…` в минуту с одного IP | `429`, `Retry-After: 60` |
| прочие пути без `Upgrade` | `400` (как раньше) |

IP клиента за доверенным прокси — из `X-Forwarded-For` (как у WebSocket,
`SELFVOY_TRUSTED_PROXIES`); на PostgreSQL лимит общий для всех экземпляров
(`rate_limits`).

Содержимое (RFC 5545): `VCALENDAR` с `X-WR-CALNAME:Selfvoy`, `METHOD:PUBLISH`,
`REFRESH-INTERVAL`/`X-PUBLISHED-TTL` = 1 час; `VEVENT` — активные встречи
записей владельца и принятые приглашения со стартом от −30 до +180 суток:

- `UID:booking-<id>-<start unix>@selfvoy` (приглашения — `invite-<id>-<start unix>@selfvoy`);
- `DTSTART`/`DTEND` в UTC (`20261021T070000Z`), `DTSTAMP` — время последнего
  изменения записи (ответа на приглашение);
- `SUMMARY` — «<страница>: <имя гостя>» (приглашение — «<тема>: <имя гостя>»);
- `DESCRIPTION` — комментарий гостя (если есть);
- `STATUS:CONFIRMED`, `TRANSP:OPAQUE`.

Строки — CRLF; TEXT экранируется (`\\`, `\;`, `\,`, перевод строки → `\n`,
прочие управляющие символы выбрасываются); строки длиннее 75 октетов
складываются (продолжение с пробела) без разрыва символов UTF-8. Email гостя
в календарь не попадает.

### Рефералы

| тип | payload | ответ |
| --- | --- | --- |
| `referral.get` | `{limit? (1–100, 50)}` | `{code, url, joined, verified, rewarded, reward_days: 30, reward_cap: 12, plan, referrals: [{display_name, source, joined_at, verified}]}` |

У пользователя один код из 8 символов `[A-Z0-9]` (выдаётся при первом
запросе); `url` — `<SELFVOY_PUBLIC_URL>/#/signup?ref=<код>` или пусто. Код
передаётся в `auth.register` (`ref`, `ref_source`); приглашённый
засчитывается (`verified`) после `auth.verifyEmail`, тогда пригласившему
приходит уведомление `referral.joined` (`{display_name, reward_days}`). Email и id
приглашённых пригласившему не раскрываются.

Награда: за подтверждённого приглашённого обоим начисляется 30 дней Pro
(`plan_until = greatest(now, plan_until) + 30 дней`) — один раз
(`referrals.rewarded_at`). Пригласивший получает награду максимум за 12
приглашённых (дальше растёт только счётчик `verified`), приглашённый — всегда.
В тело `referral.joined` добавляется «· +1 месяц Pro», если пригласившему
начислено; приглашённому — уведомление `referral.reward` «Месяц Pro в подарок»
(`{days, until}`). `rewarded` — число начисленных пригласившему наград (≤ 12),
`plan` — как в объекте `user`. Код владельца есть и в виде гостя
приглашения (`invite.get` → `ref`) и страницы записи.

### Telegram-бот

Бот включается на сервере переменными `SELFVOY_TELEGRAM_BOT_TOKEN` и
`SELFVOY_TELEGRAM_BOT_USERNAME`; его имя отдаёт `server.info` → `telegram_bot`
("" — бот не настроен; есть и в ответе без входа). Привязка — по deep link
`https://t.me/<бот>?start=<payload>`, payload (≤ 64 символов `[A-Za-z0-9_-]`)
подписан сервером (HMAC-SHA256, 16 байт подписи в base64url):
`o_<user_id>_<expires_unix>_<sig>` — владелец, ссылка живёт 15 минут;
`g_<booking_id>_<sig>` — гость записи, подпись над `manage_hash` (после
перевыпуска ссылки управления прежняя ссылка бота не работает).

| тип | payload | ответ |
| --- | --- | --- |
| `telegram.status` | — | `{enabled, bot_username, linked, username}` (`username` — ник в Telegram или "") |
| `telegram.link` | — | `{url, expires_at}` — ссылка владельца на 15 минут; бот не настроен → `bad_request` «Telegram-бот не настроен на сервере» |
| `telegram.unlink` | — | `{linked: false}` |

Команды бота (только личные чаты): `/start o_…` — чат привязывается к аккаунту
(если он был привязан к другому — перепривязывается); `/start g_…` — к записи
(запись активна, не больше 3 чатов), ответ — ближайшая встреча в местном времени
владельца; `/start` без payload и любой текст — справка; `/stop` — чат
отвязывается от аккаунта и от всех записей. Поддельная или просроченная ссылка —
ответ «Ссылка недействительна или устарела». Если пользователь заблокировал бота
(Telegram отвечает 403), чат отвязывается везде.

Владельцу в привязанный чат дублируются уведомления видов `booking.*`,
`package.*`, `invite.*`, `referral.*`, `task.due` текстом «<title>\n<body>» —
если вид не в `prefs.notifications.muted_kinds`, сейчас не тихие часы и
`prefs.notifications.telegram` не `false`. Гостю — те же извещения, что письмом
(см. «Страницы записи»). Кнопка «Приду» (callback `cf:<booking_id>:<start_unix>`)
принимается только из чата, привязанного к этой записи, и подтверждает встречу
так же, как `booking.confirm` (ответ «Спасибо, ждём вас», клавиатура сообщения
убирается; встреча прошла или отменена — «встреча уже прошла или отменена»).

### Администрирование

Роли (`user.role`): `user` (по умолчанию), `moderator`, `admin`. Роль
приходит в каждом `user` (`auth.login`, `auth.login2fa`, `auth.token`,
`auth.me`, `auth.verifyEmail`), но права проверяет **сервер** — по роли из
БД на каждый запрос, поэтому понижение действует сразу, без перевхода.
Методы `admin.*` требуют входа (без него — `unauthorized`).

| | moderator | admin |
| --- | --- | --- |
| `admin.stats`, `admin.users`, `admin.user` | да | да |
| `admin.setActive`, `admin.revokeSessions` | только над `role=user` | над любым, кроме себя |
| `admin.setRole`, `admin.audit` | нет (`forbidden`) | да |

Первые администраторы — env **`SELFVOY_ADMIN_EMAILS`** (адреса через запятую,
`;` или пробел, без учёта регистра): пользователь с адресом из списка
получает `admin` при старте сервера и при успешных `auth.login` /
`auth.login2fa` / `auth.verifyEmail` — **только если email подтверждён**
(иначе роль досталась бы тому, кто первым зарегистрировал чужой адрес).
Список — источник истины: снятая вручную роль вернётся при следующем входе,
пока адрес в списке. Каждое назначение пишется в журнал (`role_bootstrap`).

`AdminUser` = `id`, `email`, `display_name`, `avatar_url`, `role`,
`is_active`, `email_verified`, `two_factor`, `created_at`, `last_seen_at`,
`plan` (как в `user`; хэш пароля и токены не отдаются).

| тип | payload | ответ |
| --- | --- | --- |
| `admin.stats` | — | `users_total`, `users_verified`, `users_disabled`, `users_2fa`, `users_new_7d`, `moderators`, `admins`, `sessions_active` (не отозваны и не истекли), `online_now` (разных пользователей с живым соединением) |
| `admin.users` | `query?` (подстрока email или имени, ≤100 симв.), `role?` (`''`\|`user`\|`moderator`\|`admin`), `status?` (`''`\|`active`\|`disabled`\|`unverified`), `offset?`, `limit?` (1–100, по умолчанию 50) | `users[]` (AdminUser, новые первыми), `total` (без учёта `offset`/`limit`); неизвестные `role`/`status` → `bad_request` |
| `admin.user` | `user_id` | `user` (AdminUser), `sessions_active`, `audit[]` (30 последних записей журнала пользователя; модератору — без `remote_addr`); нет такого — `not_found` |
| `admin.setActive` | `user_id`, `active` (bool), `reason` (обязательна при блокировке, ≤200 симв.) | `user`; блокировка (`active=false`) отзывает все сессии и refresh-токены и закрывает открытые соединения пользователя (WS close `4001`, причина `account_disabled`); заблокированный не входит (`auth.login` → `forbidden`) и не проходит по старому токену (`unauthorized`) |
| `admin.setRole` | `user_id`, `role` | `user`; свою роль менять нельзя, последнего активного администратора понизить нельзя (`forbidden`); `moderator`/`admin` — только активному пользователю с подтверждённым email (`bad_request`) |
| `admin.revokeSessions` | `user_id` | `revoked` (сколько живых сессий завершено); refresh-токены отозваны, соединения закрыты (WS close `4001`, причина `sessions_revoked`); аккаунт не блокируется — войти заново можно |
| `admin.setPlan` | `user_id`, `days` (целое 0–3650; 0 — сбросить в Free) | `user` (AdminUser + `plan`); только admin (`forbidden`), `plan_until = now + days`; запись `admin_plan_changed` (`days`, `plan_until`, `old_plan_until`, `actor_email` \| `target_email`) в журнал; неверный `days` → `bad_request` |
| `admin.audit` | `user_id?` (0 — все), `before_id?` (0 — с самых новых; иначе записи с `id < before_id`), `limit?` (1–200, по умолчанию 100) | `entries[]`: `id`, `user_id`, `user_email?`, `kind`, `detail`, `remote_addr`, `created_at` — новые первыми; следующая страница — `before_id` = `id` последней записи |

Ошибки: `forbidden` — не хватает роли / цель выше по роли / действие над
собой; `not_found` — нет пользователя; `bad_request` — неверные поля.

Журнал (`audit_logs.kind`) действий панели; запись пишется и цели, и актору:
`admin_user_disabled` / `admin_user_enabled` (`detail`: `actor_id`,
`actor_email` | `target_id`, `target_email`; `reason`; `revoked` при
блокировке), `admin_role_changed` (`role`, `old_role`, `actor_email` |
`target_email`), `admin_sessions_revoked` (`revoked`, `actor_email` |
`target_email`), `role_bootstrap` (`role`).

## HTTP API Python AI Service

| метод | путь | назначение |
| --- | --- | --- |
| `POST` | `/v1/agent/run` | главный запрос к Selfvoy |
| `POST` | `/v1/agent/negotiate` | Agent-to-Agent переговоры |
| `POST` | `/v1/agent/slots` | слоты владельца для приглашения по ссылке: `{context, duration_minutes (15–240), window_hours, max_slots (1–12)}` → `{slots: [{start, end, score, reason}]}` |
| `POST` | `/v1/speech/transcribe` | распознавание речи (Whisper-совместимое, ru/en/auto) |
| `POST` | `/v1/memory/extract` | извлечение фактов в память |
| `GET` | `/v1/memory/{user_key}` | записи памяти (можно `?query=` — гибридный RAG) |
| `POST` | `/v1/planner/slots` | свободные слоты |
| `POST` | `/v1/planner/understand` | разбор фразы (намерение, окно, длительность) |
| `GET` | `/v1/tools` | список инструментов |
| `GET` | `/healthz` | состояние (провайдер LLM, провайдер STT, бэкенды) |

Все запросы, кроме `/healthz`, требуют заголовок `X-Selfvoy-Token` со значением
`SELFVOY_AI_TOKEN`. Без настроенного токена сервис отвечает `503` (fail closed),
кроме локальной разработки с `SELFVOY_AI_INSECURE_DEV=1`. `/healthz` отдаёт
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

Настройки AI-сервиса: `SELFVOY_RAG_MODE` (`lexical` | `hybrid` | `auto`, по
умолчанию `auto` — семантика включается при заданном `SELFVOY_EMBEDDINGS_URL`),
`SELFVOY_EMBEDDINGS_URL` (OpenAI-совместимый `POST /embeddings`),
`SELFVOY_EMBEDDINGS_MODEL` (по умолч. `text-embedding-3-small`),
`SELFVOY_EMBEDDINGS_API_KEY`, `SELFVOY_EMBEDDINGS_TIMEOUT`. Без `SELFVOY_EMBEDDINGS_URL`
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
