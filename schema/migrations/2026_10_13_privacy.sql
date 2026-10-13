-- Приватность Ауры в переговорах (prefs.privacy): что раскрывать чужой Ауре.
--   share_free_slots_only — отдавать только занятость/свободные окна, без прочих фактов памяти;
--   hide_event_titles     — названия событий календаря заменяются на «занято»;
--   allow_place_choice    — разрешить Ауре предлагать место встречи.
-- Для свежих БД колонка уже есть в schema.sql.
ALTER TABLE user_preferences ADD COLUMN IF NOT EXISTS privacy JSONB NOT NULL DEFAULT
    '{"share_free_slots_only": true, "hide_event_titles": true, "allow_place_choice": true}'::jsonb;
