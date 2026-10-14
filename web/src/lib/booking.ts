/*
  Страницы записи и рефералы: чистая логика без React (тестируется в booking.test.ts).
  Время, группировка слотов по дням, .ics и тексты ошибок гостя — общие с
  приглашениями (lib/invite.ts).
*/
import type { Booking, BookingPage, GuestBooking, Occurrence, PageFields } from '../net/bookingApi';
import { AuraError, isNetworkError } from '../net/socket';
import { plural } from './format';
import { foldIcsLine, icsEscape, icsUtc, inviteUid, isPublicWebOrigin, type LocationLike } from './invite';

// ------------------------------------------------------------------ ссылки

/** Адрес страницы записи: 10 символов [a-z0-9] (как выдаёт сервер). */
export const isPlausibleSlug = (slug: string | undefined): slug is string => Boolean(slug && /^[a-z0-9]{10}$/.test(slug));

/**
  Публичная ссылка на путь веб-клиента (#/b/<slug>, #/m/<токен>, #/signup?ref=…):
  готовая от сервера (AURA_PUBLIC_URL) — как есть; иначе адрес этого веб-клиента,
  если он публичный; иначе null (Electron/Capacitor/localhost — ссылку не собрать).
*/
export function publicLink(path: string, serverUrl: string, loc: LocationLike | null | undefined): string | null {
  const url = serverUrl.trim();
  if (/^https?:\/\//i.test(url)) return url;
  if (loc && isPublicWebOrigin(loc)) return `${loc.origin}${loc.pathname}#${path}`;
  return null;
}

export const pagePath = (slug: string) => `/b/${slug}`;
export const managePath = (token: string) => `/m/${token}`;
export const signupPath = (ref: string, source: 'link' | 'booking' | 'invite' = 'link') =>
  ref ? `/signup?ref=${encodeURIComponent(ref)}${source === 'link' ? '' : `&src=${source}`}` : '/signup';

// ------------------------------------------------------------------ рефералы

const REF_KEY = 'aura.ref';

/** Код из адреса регистрации: 8 букв/цифр; иначе null (мусор не отправляем на сервер). */
export function parseRef(search: URLSearchParams): { code: string; source: 'link' | 'booking' | 'invite' } | null {
  const code = (search.get('ref') ?? '').trim().toUpperCase();
  if (!/^[A-Z0-9]{8}$/.test(code)) return null;
  const src = search.get('src');
  return { code, source: src === 'booking' || src === 'invite' ? src : 'link' };
}

/** Код переживает переход «Регистрация → Вход → Регистрация» внутри вкладки. */
export function rememberRef(ref: { code: string; source: string } | null): void {
  try {
    if (ref) sessionStorage.setItem(REF_KEY, JSON.stringify(ref));
  } catch {
    /* хранилище недоступно (приватный режим) — код просто не запомнится */
  }
}

export function recalledRef(): { code: string; source: 'link' | 'booking' | 'invite' } | null {
  try {
    const raw = sessionStorage.getItem(REF_KEY);
    if (!raw) return null;
    const parsed = JSON.parse(raw) as { code?: unknown; source?: unknown };
    return parseRef(new URLSearchParams({ ref: String(parsed.code ?? ''), src: String(parsed.source ?? '') }));
  } catch {
    return null;
  }
}

export function forgetRef(): void {
  try {
    sessionStorage.removeItem(REF_KEY);
  } catch {
    /* нечего чистить */
  }
}

// ------------------------------------------------------------------ формы

export const TITLE_MAX = 120;
export const DESCRIPTION_MAX = 1000;

export const PAGE_DEFAULTS: PageFields = {
  title: '',
  description: '',
  duration_minutes: 30,
  days_ahead: 14,
  max_per_day: 4,
  notice_hours: 2,
  max_series: 1,
  weekends: false,
};

export interface PageFormErrors {
  title?: string;
  description?: string;
  numbers?: string;
}

const length = (s: string) => [...s].length;
const inRange = (n: number, min: number, max: number) => Number.isInteger(n) && n >= min && n <= max;

/** Те же пределы, что на сервере (BookingManager::pageCreate). */
export function pageFormErrors(form: PageFields): PageFormErrors {
  const errors: PageFormErrors = {};
  const title = form.title.trim();
  if (!title) errors.title = 'Назовите страницу — например, «Консультация»';
  else if (length(title) > TITLE_MAX || /[\r\n]/.test(title)) errors.title = `Название — до ${TITLE_MAX} символов в одну строку`;
  if (length((form.description ?? '').trim()) > DESCRIPTION_MAX) errors.description = `Описание — до ${DESCRIPTION_MAX} символов`;
  if (
    !inRange(form.duration_minutes, 15, 240) ||
    !inRange(form.days_ahead, 1, 60) ||
    !inRange(form.max_per_day, 1, 16) ||
    !inRange(form.notice_hours, 0, 168) ||
    !inRange(form.max_series, 1, 12)
  ) {
    errors.numbers = 'Проверьте числа: длительность 15–240 мин, запись на 1–60 дней, до 16 встреч в день, запас 0–168 ч, серия до 12 встреч';
  }
  return errors;
}

/** Поля страницы для формы редактирования. */
export const pageFields = (page: BookingPage): PageFields => ({
  title: page.title,
  description: page.description,
  duration_minutes: page.duration_minutes,
  days_ahead: page.days_ahead,
  max_per_day: page.max_per_day,
  notice_hours: page.notice_hours,
  max_series: page.max_series,
  weekends: page.weekends,
});

// ------------------------------------------------------------------ встречи

/** Встречи по порядку; прошедшие помечаются, отменённые остаются (видно историю). */
export function sortedOccurrences(list: Occurrence[] | undefined): Occurrence[] {
  return [...(list ?? [])].filter((o) => !Number.isNaN(Date.parse(o?.start))).sort((a, b) => Date.parse(a.start) - Date.parse(b.start));
}

export const isUpcoming = (o: Occurrence, now = new Date()) => o.status === 'active' && Date.parse(o.start) > now.getTime();

/** Ближайшая предстоящая встреча (её по умолчанию переносит booking.reschedule). */
export function nextOccurrence(list: Occurrence[] | undefined, now = new Date()): Occurrence | null {
  return sortedOccurrences(list).find((o) => isUpcoming(o, now)) ?? null;
}

export function upcomingCount(list: Occurrence[] | undefined, now = new Date()): number {
  return (list ?? []).filter((o) => isUpcoming(o, now)).length;
}

/** «Одна встреча» / «Серия: 4 встречи, раз в неделю». */
export function seriesLabel(count: number): string {
  if (count <= 1) return 'Одна встреча';
  return `Серия: ${count} ${plural(count, 'встреча', 'встречи', 'встреч')}, раз в неделю`;
}

export const PAGE_STATUS_LABEL: Record<string, string> = { active: 'Открыта', paused: 'На паузе', archived: 'Удалена' };

/** Статус записи для владельца: «Предстоит» / «Прошла» / «Отменил гость» / «Отменили вы». */
export function bookingStatus(booking: Pick<Booking, 'status' | 'cancelled_by' | 'occurrences'>, now = new Date()): { label: string; tone: string } {
  if (booking.status === 'cancelled') {
    return { label: booking.cancelled_by === 'owner' ? 'Отменили вы' : 'Отменил гость', tone: 'red' };
  }
  return upcomingCount(booking.occurrences, now) > 0 ? { label: 'Предстоит', tone: 'green' } : { label: 'Прошла', tone: '' };
}

// ------------------------------------------------------------------ .ics

/** Календарь со всеми активными встречами записи (одна или серия). */
export function bookingIcs(booking: Pick<GuestBooking, 'page_title' | 'owner_name' | 'occurrences'>, now = new Date()): string {
  const owner = booking.owner_name.trim();
  const topic = booking.page_title.trim() || 'Встреча';
  const events = sortedOccurrences(booking.occurrences).filter((o) => o.status === 'active');
  if (events.length === 0) throw new Error('Нет встреч для календаря');
  const lines = ['BEGIN:VCALENDAR', 'VERSION:2.0', 'PRODID:-//Aura//Booking//RU', 'CALSCALE:GREGORIAN', 'METHOD:PUBLISH'];
  for (const event of events) {
    const start = new Date(event.start);
    const end = new Date(event.end);
    if (Number.isNaN(start.getTime()) || Number.isNaN(end.getTime())) continue;
    lines.push(
      'BEGIN:VEVENT',
      `UID:${icsEscape(inviteUid(topic, owner, event.start))}`,
      `DTSTAMP:${icsUtc(now)}`,
      `DTSTART:${icsUtc(start)}`,
      `DTEND:${icsUtc(end)}`,
      `SUMMARY:${icsEscape(owner ? `${topic} — ${owner}` : topic)}`,
      `DESCRIPTION:${icsEscape(owner ? `Запись к ${owner} через Aura` : 'Запись через Aura')}`,
      'END:VEVENT',
    );
  }
  lines.push('END:VCALENDAR');
  return lines.map(foldIcsLine).join('\r\n') + '\r\n';
}

// ------------------------------------------------------------------ ошибки

const sentence = (text: string) => (text ? text.charAt(0).toUpperCase() + text.slice(1) : text);

/** Ошибка booking.page* / list / ownerCancel / referral.get для владельца. */
export function bookingOwnerErrorText(error: unknown): string {
  if (!(error instanceof AuraError)) return 'Что-то пошло не так';
  if (error.code === 'timeout') return 'Сервер не ответил вовремя — попробуйте ещё раз';
  if (isNetworkError(error)) return 'Нет связи с сервером Aura';
  return sentence(error.message) || 'Что-то пошло не так';
}
