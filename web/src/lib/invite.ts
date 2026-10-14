/*
  Приглашение по ссылке: чистая логика без React — ссылка для гостя, группировка
  слотов по дням в часовом поясе браузера, файл .ics, подписи статусов, проверка
  форм и тексты ошибок. Общие части (ссылка, время, .ics) — в publicLink.ts,
  slots.ts и ics.ts, здесь реэкспортируются. Проверяется юнит-тестами (invite.test.ts).
*/
import type { GuestInvite, Invite, InviteSlot, InviteStatus } from '../net/inviteApi';
import { AuraError, isNetworkError } from '../net/socket';
import { buildIcs, icsFileNameFor, stableUid } from './ics';
import { publicLink, type LocationLike, type PublicLink } from './publicLink';
import { nameError } from './validate';

// ------------------------------------------------------------------ статусы

export const INVITE_STATUS_LABEL: Record<InviteStatus, string> = {
  open: 'Открыто',
  accepted: 'Время выбрано',
  declined: 'Отказ',
  cancelled: 'Отменено',
  expired: 'Истекло',
};

const STATUS_TONE: Record<InviteStatus, '' | 'accent' | 'green' | 'red'> = {
  open: 'accent',
  accepted: 'green',
  declined: 'red',
  cancelled: '',
  expired: '',
};

const KNOWN_STATUS = new Set<string>(Object.keys(INVITE_STATUS_LABEL));

/**
  Действующий статус: сервер сам вычисляет expired, но список мог устареть, пока
  страница открыта, — открытое приглашение с прошедшим сроком показываем истёкшим.
*/
export function inviteStatus(invite: Pick<Invite | GuestInvite, 'status' | 'expires_at'>, now = new Date()): InviteStatus {
  const status = KNOWN_STATUS.has(invite.status) ? invite.status : 'expired';
  if (status === 'open') {
    const expires = Date.parse(invite.expires_at);
    if (!Number.isNaN(expires) && expires <= now.getTime()) return 'expired';
  }
  return status;
}

export const statusLabel = (status: InviteStatus) => INVITE_STATUS_LABEL[status] ?? status;
export const statusTone = (status: InviteStatus) => STATUS_TONE[status] ?? '';

/** Выбранное время: сервер шлёт `{}`, пока его нет. */
export function chosenSlot(invite: { chosen?: Partial<InviteSlot> | null }): InviteSlot | null {
  const c = invite.chosen;
  if (!c || !c.start || !c.end) return null;
  if (Number.isNaN(Date.parse(c.start)) || Number.isNaN(Date.parse(c.end))) return null;
  return { start: c.start, end: c.end };
}

// ------------------------------------------------------------------ ссылка

export { isPublicWebOrigin, type LocationLike } from './publicLink';

export type InviteLink = PublicLink;

/**
  Ссылка для гостя: готовая от сервера (AURA_PUBLIC_URL), иначе — адрес этого
  веб-клиента, если он публичный; иначе — только токен (страница подскажет, что
  на сервере нужен AURA_PUBLIC_URL).
*/
export function inviteLink(token: string, serverUrl: string, loc: LocationLike | null | undefined): InviteLink {
  return publicLink(`/i/${token}`, token, serverUrl, loc);
}

/** Токен из адреса: base64url; заведомо битый не отправляем на сервер. */
export function isPlausibleToken(token: string | undefined): token is string {
  return Boolean(token && /^[A-Za-z0-9_-]{16,256}$/.test(token));
}

// ------------------------------------------------------------------ время

export { browserTimeZone, dayKey, dayTitleIn, groupSlotsByDay, slotPhrase, timeIn, timeZoneLabel, type DayGroup, type SlotView } from './slots';

// ------------------------------------------------------------------ .ics

export { buildIcs, foldIcsLine, icsEscape, icsUtc, type IcsEvent } from './ics';

/** Стабильный UID без секретов (токен в файл не попадает): FNV-1a от темы, владельца и времени. */
export function inviteUid(topic: string, owner: string, start: string): string {
  return stableUid('invite', start, topic, owner);
}

/** Событие календаря для гостя по принятому приглашению. */
export function guestIcs(invite: Pick<GuestInvite, 'topic' | 'note' | 'owner_name'>, slot: InviteSlot, now = new Date()): string {
  const owner = invite.owner_name.trim();
  const topic = invite.topic.trim() || 'Встреча';
  const description = [invite.note.trim(), owner ? `Организатор: ${owner}. Приглашение через Aura` : 'Приглашение через Aura'].filter(Boolean).join('\n\n');
  return buildIcs({
    uid: inviteUid(topic, owner, slot.start),
    start: slot.start,
    end: slot.end,
    summary: owner ? `${topic} — ${owner}` : topic,
    description,
    now,
  });
}

export function icsFileName(slot: InviteSlot): string {
  return icsFileNameFor('invite', slot.start);
}

// ------------------------------------------------------------------ формы

export const TOPIC_MAX = 120;
export const NOTE_MAX = 500;
export const GUEST_NAME_MAX = 80;
export const EMAIL_MAX = 254;
export const COMMENT_MAX = 500;

const length = (s: string) => [...s].length;

export interface InviteFormErrors {
  topic?: string;
  note?: string;
  guestName?: string;
}

/** Форма владельца: тема 1..120, заметка ≤ 500, имя гостя ≤ 80. */
export function inviteFormErrors(form: { topic: string; note: string; guestName: string }): InviteFormErrors {
  const errors: InviteFormErrors = {};
  const topic = form.topic.trim();
  if (!topic) errors.topic = 'Укажите тему встречи';
  else if (length(topic) > TOPIC_MAX) errors.topic = `Тема — до ${TOPIC_MAX} символов`;
  if (length(form.note.trim()) > NOTE_MAX) errors.note = `Заметка — до ${NOTE_MAX} символов`;
  if (length(form.guestName.trim()) > GUEST_NAME_MAX) errors.guestName = `Имя — до ${GUEST_NAME_MAX} символов`;
  return errors;
}

export interface GuestFormErrors {
  name?: string;
  email?: string;
  comment?: string;
  slot?: string;
}

/** Ответ гостя: имя обязательно (1..80), email и комментарий — по желанию; для accept нужен слот. */
export function guestFormErrors(form: { name: string; email: string; comment: string; slot?: string | null }, action: 'accept' | 'decline'): GuestFormErrors {
  const errors: GuestFormErrors = {};
  const name = nameError(form.name);
  if (name) errors.name = name;
  const email = form.email.trim();
  if (email) {
    if (length(email) > EMAIL_MAX || !/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(email)) errors.email = 'Похоже, в email опечатка';
  }
  if (length(form.comment.trim()) > COMMENT_MAX) errors.comment = `Комментарий — до ${COMMENT_MAX} символов`;
  if (action === 'accept' && !form.slot) errors.slot = 'Выберите время';
  return errors;
}

export const hasErrors = (errors: object) => Object.values(errors).some(Boolean);

// ------------------------------------------------------------------ ошибки

const sentence = (text: string) => (text ? text.charAt(0).toUpperCase() + text.slice(1) : text);

/** Ошибка invite.create / list / cancel для владельца. */
export function ownerErrorText(error: unknown): string {
  if (!(error instanceof AuraError)) return 'Что-то пошло не так';
  if (error.code === 'timeout') return 'Сервер не ответил вовремя — попробуйте ещё раз';
  if (isNetworkError(error)) return 'Нет связи с сервером Aura';
  if (error.code === 'upstream' || error.code === 'upstream_error') return 'Аура сейчас не может подобрать время — попробуйте позже';
  if (error.code === 'conflict' && !error.message) return 'Нет свободных окон в выбранный период';
  // forbidden — лимит открытых приглашений (текст от сервера).
  if (error.code === 'forbidden' && !error.message) return 'Слишком много открытых приглашений — отмените ненужные';
  return sentence(error.message) || 'Что-то пошло не так';
}

export type GuestErrorKind = 'network' | 'notFound' | 'conflict' | 'limited' | 'invalid' | 'other';

export function guestErrorKind(error: unknown): GuestErrorKind {
  if (!(error instanceof AuraError)) return 'other';
  if (isNetworkError(error)) return 'network';
  switch (error.code) {
    case 'not_found':
      return 'notFound';
    case 'conflict':
      return 'conflict';
    case 'forbidden':
    case 'rate_limited':
      return 'limited';
    case 'bad_request':
      return 'invalid';
    default:
      return 'other';
  }
}

/** Текст ошибки для гостя: сетевые — своими словами, остальное — как сказал сервер. */
export function guestErrorText(error: unknown, notFound = 'Приглашение не найдено'): string {
  const kind = guestErrorKind(error);
  if (kind === 'network') {
    return error instanceof AuraError && error.code === 'timeout' ? 'Сервер не ответил вовремя — попробуйте ещё раз' : 'Нет связи с сервером Aura — ответ не отправлен';
  }
  const message = error instanceof AuraError ? sentence(error.message) : '';
  if (kind === 'limited') return message || 'Слишком много попыток — подождите минуту';
  if (kind === 'notFound') return notFound;
  return message || 'Что-то пошло не так';
}
