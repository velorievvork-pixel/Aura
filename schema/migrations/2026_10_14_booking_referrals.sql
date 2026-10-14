-- Запись по постоянной ссылке (booking.*), серии занятий и реферальная программа
-- (referral.*). См. docs/PROTOCOL.md.

-- Страницы записи (booking.*): постоянная публичная ссылка владельца (/#/b/<slug>).
-- slug не секрет (его публикуют в профилях); rotate выдаёт новый.
-- hours — локальные часы занятий владельца ([] — рабочие часы из настроек),
-- weekdays — дни недели (0 = понедельник).
CREATE TABLE IF NOT EXISTS booking_pages (
    id               BIGSERIAL     PRIMARY KEY,
    owner_id         BIGINT        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    slug             TEXT          NOT NULL UNIQUE,
    title            TEXT          NOT NULL,
    description      TEXT          NOT NULL DEFAULT '',
    duration_minutes INTEGER       NOT NULL CHECK (duration_minutes BETWEEN 15 AND 240),
    window_days      INTEGER       NOT NULL DEFAULT 14 CHECK (window_days BETWEEN 1 AND 60),
    min_notice_hours INTEGER       NOT NULL DEFAULT 12 CHECK (min_notice_hours BETWEEN 0 AND 168),
    buffer_minutes   INTEGER       NOT NULL DEFAULT 0 CHECK (buffer_minutes BETWEEN 0 AND 120),
    hours            JSONB         NOT NULL DEFAULT '[]'::jsonb,
    weekdays         JSONB         NOT NULL DEFAULT '[0,1,2,3,4,5]'::jsonb,
    allow_weekly     BOOLEAN       NOT NULL DEFAULT TRUE,
    active           BOOLEAN       NOT NULL DEFAULT TRUE,
    created_at       TIMESTAMPTZ   NOT NULL DEFAULT now(),
    updated_at       TIMESTAMPTZ   NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS booking_pages_owner_idx ON booking_pages (owner_id);

-- Записи гостей: одно занятие (weeks = 1) или серия «каждую неделю» (weekly,
-- занятие k — first_start + 7k суток). skipped — отменённые занятия (индексы),
-- moved — перенесённые {"<индекс>": "<новое начало ISO>"}, reminded — индексы, по
-- которым владельцу уже ушло напоминание. Гость управляет записью по своей
-- ссылке (/#/m/<токен>); в БД только sha256 токена.
CREATE TABLE IF NOT EXISTS bookings (
    id                BIGSERIAL     PRIMARY KEY,
    owner_id          BIGINT        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    page_id           BIGINT        REFERENCES booking_pages(id) ON DELETE SET NULL,
    manage_token_hash TEXT          NOT NULL UNIQUE,
    title             TEXT          NOT NULL,
    duration_minutes  INTEGER       NOT NULL CHECK (duration_minutes BETWEEN 15 AND 240),
    guest_name        TEXT          NOT NULL,
    guest_email       TEXT          NOT NULL DEFAULT '',
    guest_comment     TEXT          NOT NULL DEFAULT '',
    first_start       TIMESTAMPTZ   NOT NULL,
    weekly            BOOLEAN       NOT NULL DEFAULT FALSE,
    weeks             INTEGER       NOT NULL DEFAULT 1 CHECK (weeks BETWEEN 1 AND 52),
    skipped           JSONB         NOT NULL DEFAULT '[]'::jsonb,
    moved             JSONB         NOT NULL DEFAULT '{}'::jsonb,
    reminded          JSONB         NOT NULL DEFAULT '[]'::jsonb,
    status            TEXT          NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'cancelled')),
    created_at        TIMESTAMPTZ   NOT NULL DEFAULT now(),
    updated_at        TIMESTAMPTZ   NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS bookings_owner_idx ON bookings (owner_id, status);
CREATE INDEX IF NOT EXISTS bookings_active_idx ON bookings (status) WHERE status = 'active';

-- Реферальная программа: код приглашения пользователя и срок Pro (бонус).
ALTER TABLE users ADD COLUMN IF NOT EXISTS referral_code TEXT;
ALTER TABLE users ADD COLUMN IF NOT EXISTS pro_until TIMESTAMPTZ;
CREATE UNIQUE INDEX IF NOT EXISTS users_referral_code_idx ON users (referral_code)
    WHERE referral_code IS NOT NULL;

-- Кто кого пригласил. qualified_at — когда приглашённый начал пользоваться
-- (первая запись или выбранное время по приглашению): тогда обоим +30 дней Pro.
CREATE TABLE IF NOT EXISTS referrals (
    id           BIGSERIAL     PRIMARY KEY,
    referrer_id  BIGINT        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    invitee_id   BIGINT        NOT NULL UNIQUE REFERENCES users(id) ON DELETE CASCADE,
    created_at   TIMESTAMPTZ   NOT NULL DEFAULT now(),
    qualified_at TIMESTAMPTZ,
    CHECK (referrer_id <> invitee_id)
);

CREATE INDEX IF NOT EXISTS referrals_referrer_idx ON referrals (referrer_id);
