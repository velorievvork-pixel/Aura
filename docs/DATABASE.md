# База данных Aura

PostgreSQL 14+ (прод) или встроенное JSON-хранилище (разработка/тесты,
`AURA_EMBEDDED_DB`). Оба бэкенда реализуют один интерфейс `IDatabase`
(`server/include/aura/idatabase.h`); C++-тесты работают на встроенном,
e2e — на реальном PostgreSQL.

Схема: `schema/schema.sql` (идемпотентна, `CREATE … IF NOT EXISTS`);
изменения — файлами `schema/migrations/` (применяются по порядку имени).
Демо-данные: `schema/seed.sql`.

## 26 таблиц по доменам

### Аутентификация и безопасность
| таблица | назначение |
| --- | --- |
| `users` | аккаунты: email (уникальный, нормализованный), хэш пароля Argon2id, `email_verified`, `active`, `role` (`user`/`moderator`/`admin`, админ-панель; миграция `2026_10_13_roles.sql`) |
| `sessions` | живые сессии: jwt_id, устройство, адрес, `expires_at`, `revoked_at` (отзыв мгновенный) |
| `auth_tokens` | одноразовые коды (подтверждение email, сброс пароля) — только SHA-256 хэш, TTL |
| `refresh_tokens` | refresh-пары с ротацией: хэш, `family`, отозванные переиспользования |
| `audit_logs` | журнал событий безопасности (вход, блокировка, 2FA, отзывы) |
| `two_factor_settings` | TOTP-секрет (зашифрован AES-256-GCM), статус, счётчик окон |
| `recovery_codes` | резервные коды 2FA — хэши, одноразовые |
| `trusted_devices` | устройства, доверенные для входа без 2FA (по device_id, TTL) |

### Общение
| таблица | назначение |
| --- | --- |
| `chats` | диалоги: `kind` CHECK (`direct`/`group`/`agent`) |
| `chat_members` | участники + роль |
| `messages` | сообщения: `kind` CHECK (`text`/`agent_action`/`agent_reply`/`system`), payload JSONB |

### Память и настройки
| таблица | назначение |
| --- | --- |
| `user_memory` | факты/предпочтения/расписание/контакты: `kind` CHECK (`fact`/`preference`/`schedule`/`contact`), текст, вес |
| `user_preferences` | JSONB-настройки (тема, диета, город, бюджет, `notifications.quiet_hours`, `notifications.muted_kinds`); онбординг-анкета: `birthday`, `allergies[]`, `onboarded` (миграция `2026_09_21_onboarding.sql`) |

### Инструменты и задачи
| таблица | назначение |
| --- | --- |
| `tool_permissions` | режимы инструментов на пользователя: `allow`/`ask`/`deny` |
| `pending_actions` | отложенные опасные действия (барьер подтверждения): аргументы, `expires_at`, статус |
| `tasks` | задачи/напоминания: статус, приоритет, `remind_at`, `reminded_at` |

### Интеграции
| таблица | назначение |
| --- | --- |
| `integration_connections` | подключения Google: access/refresh токены **только шифром** (AES-256-GCM), scope, статус |
| `integration_oauth_states` | одноразовые state + PKCE verifier (TTL `AURA_OAUTH_STATE_TTL`) |

### Уведомления (этап 13)
| таблица | назначение |
| --- | --- |
| `notifications` | in-app «входящая»: kind, заголовок, тело, payload JSONB, `read_at`; индексы (user_id, id DESC) и частичный по непрочитанным |
| `push_devices` | токены push-доставки: `platform` CHECK (`apns`/`fcm`/`webhook`/`dev`), UNIQUE(user_id, token), `enabled`, `last_used_at`; `fcm` добавлена миграцией `2026_09_20_push_fcm.sql` (этап 11) |

### Приглашения по ссылке
| таблица | назначение |
| --- | --- |
| `meeting_invites` | встреча с человеком без Aura (`invite.*`): `owner_id`, `token_hash` UNIQUE (sha256 токена ссылки — сам токен не хранится), тема, заметка, `duration_minutes` CHECK 15–240, `slots` JSONB `[{start,end}]`, `status` CHECK (`open`/`accepted`/`declined`/`cancelled`; `expired` вычисляется по `expires_at`), ответ гостя (`guest_name/email/comment`, `chosen` JSONB), `responded_at`; индекс (owner_id, id DESC). Миграция `2026_10_14_invites.sql` |
| `booking_pages` | страницы записи (`booking.*`): `slug` UNIQUE (публичный), название, описание, `duration_minutes`, `window_days`, `min_notice_hours`, `buffer_minutes` (CHECK-диапазоны), `hours`/`weekdays` JSONB, `allow_weekly`, `active` |
| `bookings` | записи гостей: `manage_token_hash` UNIQUE (sha256 личной ссылки), `page_id` (ON DELETE SET NULL), гость, `first_start`, `weekly`/`weeks` (серия: занятие k — +7k суток), `skipped` JSONB (отменённые индексы), `moved` JSONB (`{"индекс": "новое начало"}`), `reminded` JSONB, `status` (`active`/`cancelled`) |
| `referrals` | кто кого пригласил: `referrer_id`, `invitee_id` UNIQUE, `qualified_at` (приглашённый начал пользоваться — бонус выдан); в `users` — `referral_code` (UNIQUE) и `pro_until`. Миграция `2026_10_14_booking_referrals.sql` |

## Соглашения

- Все запросы параметризованы ($1, $2, …) — строки пользователя never
  конкатенируются в SQL (проверено e2e-сценариями SQLi, TESTING.md).
- Время — UTC, ISO-8601; идентификаторы — BIGSERIAL.
- Секреты в БД: только хэши (пароли, коды, refresh) или шифры
  (TOTP, токены интеграций); ключи — из окружения, не в БД.
- Удаление аккаунта: большинство таблиц — `ON DELETE CASCADE` по
  `user_id`; осознанные `SET NULL`: `audit_logs.user_id` (журнал
  безопасности сохраняется), `messages.sender_id` (история собеседника
  не разрушается), `chats.created_by`, `tasks.chat_id`,
  `pending_actions.chat_id`. Отдельного UI-обработчика «удалить аккаунт»
  пока нет — честно указано в PRIVACY.md.
