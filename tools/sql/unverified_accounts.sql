-- Аккаунты PostgreSQL, обошедшие подтверждение email.
--
-- До исправления в database_pg.cpp (createUser без email_verified) новый
-- аккаунт получал email_verified = TRUE из DEFAULT схемы: auth.verifyEmail
-- уходил в ветку «уже подтверждён», а вход работал без кода. Встроенное
-- хранилище (JSON) ошибки не имело.
--
-- Признак затронутого аккаунта: в audit_logs есть «register» (регистрация
-- через auth.register), но нет «email_verified», а в users стоит TRUE.
-- Старые аккаунты (созданные до подтверждения email, seed, ручные вставки)
-- события «register» не имеют и сюда не попадают.
--
-- Отчёт (ничего не меняет):
--   psql "$DATABASE_URL" -f tools/sql/unverified_accounts.sql
-- Сбросить подтверждение затронутым аккаунтам (в транзакции):
--   psql "$DATABASE_URL" -v apply=1 -f tools/sql/unverified_accounts.sql
-- Плюс отозвать их сессии и refresh-токены — следующий вход попросит код
-- (auth.login → email_not_verified, клиент покажет экран кода):
--   psql "$DATABASE_URL" -v apply=1 -v revoke=1 -f tools/sql/unverified_accounts.sql
\set ON_ERROR_STOP on

CREATE TEMP TABLE affected_accounts AS
SELECT u.id, u.email, u.display_name, u.created_at, u.last_seen_at
FROM users u
WHERE u.email_verified
  AND EXISTS (SELECT 1 FROM audit_logs a WHERE a.user_id = u.id AND a.kind = 'register')
  AND NOT EXISTS (SELECT 1 FROM audit_logs a WHERE a.user_id = u.id AND a.kind = 'email_verified');

\echo 'Аккаунты без подтверждённого email (email_verified = true без кода):'
SELECT id, email, display_name, created_at, last_seen_at FROM affected_accounts ORDER BY id;
SELECT count(*) AS affected FROM affected_accounts;

\if :{?apply}
BEGIN;
UPDATE users SET email_verified = false WHERE id IN (SELECT id FROM affected_accounts);
INSERT INTO audit_logs (user_id, kind, detail)
SELECT id, 'email_unverified_reset', '{"reason":"pg_default_bug"}'::jsonb FROM affected_accounts;
\if :{?revoke}
UPDATE sessions SET revoked_at = now() WHERE user_id IN (SELECT id FROM affected_accounts) AND revoked_at IS NULL;
UPDATE refresh_tokens SET revoked_at = now() WHERE user_id IN (SELECT id FROM affected_accounts) AND revoked_at IS NULL;
\echo 'Сессии и refresh-токены затронутых аккаунтов отозваны.'
\endif
COMMIT;
\echo 'Готово: затронутым аккаунтам нужно подтвердить email при следующем входе.'
\else
\echo 'Только отчёт. Чтобы сбросить подтверждение, запустите с -v apply=1 (и -v revoke=1, чтобы разлогинить).'
\endif
