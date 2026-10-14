/*
  Запись по постоянной ссылке и реферальная программа: чистая логика без React —
  ссылки (#/b/<slug>, #/m/<токен>, #/signup?ref=<код>), форма страницы записи,
  подписи тарифа, расписание владельца по дням, занятия гостя, .ics для серии,
  реферальный код до регистрации, тексты ошибок. Проверяется юнит-тестами
  (booking.test.ts).
*/
import type { Booking, BookingPage, GuestBooking, Occurrence, PageInput, Plan, ReferralInfo } from '../net/bookingApi';
import { AuraError, isNetworkError } from '../net/socket';
import { plural } from './format';
import { buildIcs, icsFileNameFor, stableUid, type IcsEvent } from './ics';
import { publicLink, type LocationLike, type PublicLink } from './publicLink';
import { dayKey, dayMonthIn, dayTitleIn, rangeIn, safeZone, timeIn, type TimeSlot } from './slots';

const DAY_MS = 86_400_000;
const WEEK_MS = 7 * DAY_MS;
const length = (s: string) => [...s].length;
const sentence = (text: string) => (text ? text.charAt(0).toUpperCase() + text.slice(1) : text);

// ------------------------------------------------------------------ ссылки

/** Slug страницы записи: сервер выдаёт 10 символов [a-z0-9]; заведомо битый не отправляем. */
export function isPlausibleSlug(slug: string | undefined): slug is string {
  return Boolean(slug && /^[A-Za-z0-9]{4,64}$/.test(slug));
}

/** Постоянная ссылка страницы записи: `url` от сервера или `<адрес клиента>#/b/<slug>`. */
export function pageLink(page: Pick<BookingPage, 'slug' | 'url'>, loc: LocationLike | null | undefined): PublicLink {
  return publicLink(`/b/${page.slug}`, page.slug, page.url, loc);
}

/** Личная ссылка гостя для переноса и отмены: `manage_url` или `<адрес клиента>#/m/<токен>`. */
export function manageLink(token: string, manageUrl: string, loc: LocationLike | null | undefined): PublicLink {
  return publicLink(`/m/${token}`, token, manageUrl, loc);
}

// ------------------------------------------------------------------ реферальный код

export const REFERRAL_STORAGE_KEY = 'aura.referral';

/** Код коллеги: сервер выдаёт 8 символов [A-Z2-9]; принимаем и строчные, мусор отбрасываем. */
export function normalizeReferral(raw: string | null | undefined): string {
  const code = (raw ?? '').trim().toUpperCase();
  return /^[A-Z0-9]{4,32}$/.test(code) ? code : '';
}

/** `ref` из query hash-адреса: `?ref=ABCD2345` (react-router отдаёт его как location.search). */
export function referralFromSearch(search: string): string {
  try {
    return normalizeReferral(new URLSearchParams(search).get('ref'));
  } catch {
    return '';
  }
}

/** Путь регистрации (внутри hash-роутера): с кодом пригласившего, если он есть. */
export function signupPath(code: string | null | undefined): string {
  const ref = normalizeReferral(code);
  return ref ? `/signup?ref=${encodeURIComponent(ref)}` : '/signup';
}

type StorageLike = Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>;

function sessionStore(): StorageLike | null {
  try {
    return typeof sessionStorage === 'undefined' ? null : sessionStorage;
  } catch {
    return null; // доступ к хранилищу запрещён (приватный режим, политика сайта)
  }
}

/** Запомнить код до регистрации (на время вкладки): переход на вход и обратно его не теряет. */
export function rememberReferral(code: string, store: StorageLike | null = sessionStore()): void {
  const ref = normalizeReferral(code);
  if (!ref || !store) return;
  try {
    store.setItem(REFERRAL_STORAGE_KEY, ref);
  } catch {
    /* хранилище переполнено или недоступно — код останется только в адресе */
  }
}

export function pendingReferral(store: StorageLike | null = sessionStore()): string {
  if (!store) return '';
  try {
    return normalizeReferral(store.getItem(REFERRAL_STORAGE_KEY));
  } catch {
    return '';
  }
}

export function forgetReferral(store: StorageLike | null = sessionStore()): void {
  try {
    store?.removeItem(REFERRAL_STORAGE_KEY);
  } catch {
    /* нечего чистить */
  }
}

/** Ссылка «Пригласить коллегу»: `url` от сервера или `<адрес клиента>#/signup?ref=<код>`; иначе — сам код. */
export function referralLink(info: Pick<ReferralInfo, 'code' | 'url'>, loc: LocationLike | null | undefined): PublicLink {
  return publicLink(signupPath(info.code), info.code, info.url, loc);
}

// ------------------------------------------------------------------ тариф

export interface PlanView {
  pro: boolean;
  /** «Free» / «Pro» */
  name: string;
  /** «до 13 ноября» — для Pro; "" — для Free. */
  until: string;
  maxPages: number;
  maxWeeks: number;
}

/** Действующий тариф: Pro с прошедшим pro_until (страница открыта давно) — уже Free. */
export function planView(plan: Plan | null | undefined, now = new Date(), timeZone = 'UTC'): PlanView {
  const until = Date.parse(plan?.pro_until ?? '');
  const pro = plan?.name === 'pro' && !Number.isNaN(until) && until > now.getTime();
  const untilDate = pro ? new Date(until) : null;
  const year = untilDate && untilDate.getUTCFullYear() !== now.getUTCFullYear() ? ` ${untilDate.getUTCFullYear()}` : '';
  return {
    pro,
    name: pro ? 'Pro' : 'Free',
    until: untilDate ? `до ${dayMonthIn(untilDate, timeZone)}${year}` : '',
    maxPages: plan?.max_pages ?? (pro ? 5 : 1),
    maxWeeks: plan?.max_weeks ?? (pro ? 26 : 8),
  };
}

export function canCreatePage(pages: unknown[] | null | undefined, plan: Plan | null | undefined, now = new Date()): boolean {
  return (pages?.length ?? 0) < planView(plan, now).maxPages;
}

// ------------------------------------------------------------------ форма страницы

export const WEEKDAY_SHORT = ['Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб', 'Вс'];
export const PAGE_TITLE_MAX = 120;
export const PAGE_DESCRIPTION_MAX = 1000;
export const DEFAULT_WEEKDAYS = [0, 1, 2, 3, 4, 5];

export interface PageForm {
  title: string;
  description: string;
  duration: number;
  windowDays: number;
  noticeHours: number;
  bufferMinutes: number;
  /** [] — рабочие часы из настроек. */
  hours: number[];
  weekdays: number[];
  allowWeekly: boolean;
  active: boolean;
}

export const NEW_PAGE: PageForm = {
  title: '',
  description: '',
  duration: 60,
  windowDays: 14,
  noticeHours: 12,
  bufferMinutes: 0,
  hours: [],
  weekdays: DEFAULT_WEEKDAYS,
  allowWeekly: true,
  active: true,
};

export function pageToForm(page: BookingPage | null | undefined): PageForm {
  if (!page) return { ...NEW_PAGE, weekdays: [...DEFAULT_WEEKDAYS] };
  return {
    title: page.title,
    description: page.description ?? '',
    duration: page.duration_minutes,
    windowDays: page.window_days,
    noticeHours: page.min_notice_hours,
    bufferMinutes: page.buffer_minutes,
    hours: [...(page.hours ?? [])].sort((a, b) => a - b),
    weekdays: [...(page.weekdays ?? DEFAULT_WEEKDAYS)].sort((a, b) => a - b),
    allowWeekly: page.allow_weekly,
    active: page.active,
  };
}

const sortedUnique = (values: number[], max: number) => [...new Set(values.filter((v) => Number.isInteger(v) && v >= 0 && v <= max))].sort((a, b) => a - b);

/** Поля booking.pages.save (все, чтобы правка не зависела от умолчаний сервера). */
export function formToInput(form: PageForm, id?: number): PageInput {
  return {
    ...(id ? { id } : {}),
    title: form.title.trim(),
    description: form.description.trim(),
    duration_minutes: form.duration,
    window_days: form.windowDays,
    min_notice_hours: form.noticeHours,
    buffer_minutes: form.bufferMinutes,
    hours: sortedUnique(form.hours, 23),
    weekdays: sortedUnique(form.weekdays, 6),
    allow_weekly: form.allowWeekly,
    active: form.active,
  };
}

export interface PageFormErrors {
  title?: string;
  description?: string;
  duration?: string;
  windowDays?: string;
  noticeHours?: string;
  bufferMinutes?: string;
  weekdays?: string;
}

const inRange = (n: number, min: number, max: number) => Number.isInteger(n) && n >= min && n <= max;

/** Те же ограничения, что у сервера (booking.pages.save). */
export function pageFormErrors(form: PageForm): PageFormErrors {
  const errors: PageFormErrors = {};
  const title = form.title.trim();
  if (!title) errors.title = 'Назовите страницу — например, «Математика, 60 минут»';
  else if (/[\r\n]/.test(title)) errors.title = 'Название — в одну строку';
  else if (length(title) > PAGE_TITLE_MAX) errors.title = `Название — до ${PAGE_TITLE_MAX} символов`;
  if (length(form.description.trim()) > PAGE_DESCRIPTION_MAX) errors.description = `Описание — до ${PAGE_DESCRIPTION_MAX} символов`;
  if (!inRange(form.duration, 15, 240)) errors.duration = 'Длительность — от 15 до 240 минут';
  if (!inRange(form.windowDays, 1, 60)) errors.windowDays = 'Запись вперёд — от 1 до 60 дней';
  if (!inRange(form.noticeHours, 0, 168)) errors.noticeHours = 'Не позже чем за 0–168 часов';
  if (!inRange(form.bufferMinutes, 0, 120)) errors.bufferMinutes = 'Перерыв — от 0 до 120 минут';
  if (sortedUnique(form.weekdays, 6).length === 0) errors.weekdays = 'Выберите хотя бы один день';
  return errors;
}

/** Переключить элемент в наборе (дни недели, часы). */
export function toggle(values: number[], value: number): number[] {
  return values.includes(value) ? values.filter((v) => v !== value) : [...values, value].sort((a, b) => a - b);
}

/** [0,1,2,3,4] → «Пн–Пт», [0,2,4] → «Пн, Ср, Пт», все — «каждый день». */
export function weekdaysSummary(weekdays: number[]): string {
  const days = sortedUnique(weekdays ?? [], 6);
  if (days.length === 7) return 'каждый день';
  if (!days.length) return '—';
  return runs(days)
    .map(([a, b]) => (a === b ? WEEKDAY_SHORT[a] : b === a + 1 ? `${WEEKDAY_SHORT[a]}, ${WEEKDAY_SHORT[b]}` : `${WEEKDAY_SHORT[a]}–${WEEKDAY_SHORT[b]}`))
    .join(', ');
}

/** Часы, в которые идёт занятие: [15..20] → «15:00–21:00»; [] — рабочие часы из настроек. */
export function hoursSummary(hours: number[]): string {
  const list = sortedUnique(hours ?? [], 23);
  if (!list.length) return 'рабочие часы из настроек';
  return runs(list)
    .map(([a, b]) => `${a}:00–${b + 1 === 24 ? 24 : b + 1}:00`)
    .join(', ');
}

/** Отрезки подряд идущих чисел: [1,2,3,5] → [[1,3],[5,5]]. */
function runs(sorted: number[]): [number, number][] {
  const out: [number, number][] = [];
  for (const v of sorted) {
    const last = out.at(-1);
    if (last && v === last[1] + 1) last[1] = v;
    else out.push([v, v]);
  }
  return out;
}

/** «60 мин», «1,5 часа», «2 часа» */
export function durationLabel(minutes: number): string {
  if (minutes < 60 || minutes % 30 !== 0) return `${minutes} мин`;
  const hours = minutes / 60;
  if (Number.isInteger(hours)) return `${hours} ${plural(hours, 'час', 'часа', 'часов')}`;
  return `${String(hours).replace('.', ',')} часа`;
}

/** «Каждую неделю (8 занятий)» */
export function weeklyLabel(weeks: number): string {
  return `Каждую неделю (${weeks} ${plural(weeks, 'занятие', 'занятия', 'занятий')})`;
}

// ------------------------------------------------------------------ занятия

const parse = (iso: string) => Date.parse(iso);

function validOccurrences(list: Occurrence[] | null | undefined): Occurrence[] {
  return (list ?? [])
    .filter((o) => o && Number.isInteger(o.index) && !Number.isNaN(parse(o.start)) && !Number.isNaN(parse(o.end)))
    .sort((a, b) => parse(a.start) - parse(b.start));
}

export interface AgendaItem {
  booking: Booking;
  occurrence: Occurrence;
  /** «16:00–17:00» */
  time: string;
  /** «занятие 3 из 8» — для серии. */
  series: string;
  cancelled: boolean;
  /** Отменить можно только будущее активное занятие. */
  canCancel: boolean;
}

export interface AgendaDay {
  key: string;
  title: string;
  items: AgendaItem[];
}

export function seriesPosition(index: number, weeks: number): string {
  return `занятие ${index + 1} из ${weeks}`;
}

/**
  Ближайшие занятия владельца по дням (в поясе устройства): закончившиеся не
  показываем; отменённые — только если includeCancelled.
*/
export function agendaByDay(bookings: Booking[], timeZone: string, now = new Date(), includeCancelled = false): AgendaDay[] {
  const items: AgendaItem[] = [];
  for (const booking of bookings ?? []) {
    for (const occurrence of validOccurrences(booking.occurrences)) {
      if (parse(occurrence.end) <= now.getTime()) continue;
      const cancelled = occurrence.status === 'cancelled' || booking.status === 'cancelled';
      if (cancelled && !includeCancelled) continue;
      items.push({
        booking,
        occurrence,
        time: rangeIn(occurrence, timeZone),
        series: booking.weekly ? seriesPosition(occurrence.index, booking.weeks) : '',
        cancelled,
        canCancel: !cancelled && parse(occurrence.start) > now.getTime(),
      });
    }
  }
  items.sort((a, b) => parse(a.occurrence.start) - parse(b.occurrence.start) || a.booking.id - b.booking.id);
  const days: AgendaDay[] = [];
  for (const item of items) {
    const start = new Date(item.occurrence.start);
    const key = dayKey(start, timeZone);
    let day = days.find((d) => d.key === key);
    if (!day) {
      day = { key, title: dayTitleIn(start, timeZone, now), items: [] };
      days.push(day);
    }
    day.items.push(item);
  }
  return days;
}

/** Будущие активные занятия записи (у владельца — для «отменить всю запись»). */
export function upcomingCount(booking: Pick<Booking, 'occurrences' | 'status'>, now = new Date()): number {
  if (booking.status === 'cancelled') return 0;
  return validOccurrences(booking.occurrences).filter((o) => o.status === 'booked' && parse(o.start) > now.getTime()).length;
}

export interface GuestOccurrence extends Occurrence {
  past: boolean;
  /** Можно перенести или отменить: занятие booked и ещё не началось. */
  changeable: boolean;
}

/** Занятия на странице управления, хронологически. */
export function guestOccurrences(booking: Pick<GuestBooking, 'occurrences' | 'status'>, now = new Date()): GuestOccurrence[] {
  const active = booking.status !== 'cancelled';
  return validOccurrences(booking.occurrences).map((o) => {
    const past = parse(o.start) <= now.getTime();
    return { ...o, past, changeable: active && !past && o.status === 'booked' };
  });
}

/** Ближайшее предстоящее занятие (или null). */
export function nextOccurrence(booking: Pick<GuestBooking, 'occurrences' | 'status'>, now = new Date()): Occurrence | null {
  return guestOccurrences(booking, now).find((o) => o.changeable) ?? null;
}

const WEEKDAY_DATIVE = ['воскресеньям', 'понедельникам', 'вторникам', 'средам', 'четвергам', 'пятницам', 'субботам'];

/** Повторение для гостя: «по четвергам в 16:00» — по занятию, в поясе гостя. */
export function weeklyPhrase(occurrence: TimeSlot, timeZone: string): string {
  const d = new Date(occurrence.start);
  if (Number.isNaN(d.getTime())) return '';
  const weekday = new Intl.DateTimeFormat('en-US', { timeZone: safeZone(timeZone), weekday: 'short' }).format(d);
  const idx = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'].indexOf(weekday);
  return idx < 0 ? '' : `по ${WEEKDAY_DATIVE[idx]} в ${timeIn(d, timeZone)}`;
}

/** «22 октября, 29 октября» — недели серии, где время уже было занято. */
export function skippedPhrase(skipped: string[], timeZone: string): string {
  return (skipped ?? [])
    .map((iso) => new Date(iso))
    .filter((d) => !Number.isNaN(d.getTime()))
    .sort((a, b) => a.getTime() - b.getTime())
    .map((d) => dayMonthIn(d, timeZone))
    .join(', ');
}

// ------------------------------------------------------------------ .ics

/**
  Начало занятия 0 серии: занятие k = first_start + 7·k суток (UTC). Перенесённые
  занятия выбиваются из ряда, поэтому берём самое частое значение start − 7·k.
*/
export function seriesBase(occurrences: Occurrence[]): number | null {
  const votes = new Map<number, { count: number; index: number }>();
  for (const o of validOccurrences(occurrences)) {
    const base = parse(o.start) - o.index * WEEK_MS;
    const v = votes.get(base);
    if (v) v.count += 1;
    else votes.set(base, { count: 1, index: o.index });
  }
  let best: { base: number; count: number; index: number } | null = null;
  for (const [base, v] of votes) {
    if (!best || v.count > best.count || (v.count === best.count && v.index < best.index)) best = { base, ...v };
  }
  return best ? best.base : null;
}

const iso = (ms: number) => new Date(ms).toISOString().replace(/\.\d{3}Z$/, 'Z');

/**
  Файл календаря для гостя. Разовая запись — одно событие; серия —
  RRULE:FREQ=WEEKLY;COUNT=N от первого известного занятия, EXDATE для отменённых,
  перенесённое занятие — отдельный VEVENT с RECURRENCE-ID (тот же UID).
  Время — UTC: серия повторяется каждые 7 суток по UTC, как на сервере.
  null — в календарь добавлять нечего (запись отменена целиком).
*/
export function bookingIcs(booking: Pick<GuestBooking, 'title' | 'owner_name' | 'duration_minutes' | 'weekly' | 'weeks' | 'status' | 'occurrences'>, now = new Date()): string | null {
  if (booking.status === 'cancelled') return null;
  const occurrences = validOccurrences(booking.occurrences);
  const booked = occurrences.filter((o) => o.status === 'booked');
  if (!booked.length) return null;
  const owner = (booking.owner_name ?? '').trim();
  const title = (booking.title ?? '').trim() || 'Занятие';
  const summary = owner ? `${title} — ${owner}` : title;
  const description = `${owner ? `Ведёт: ${owner}. ` : ''}Запись через Aura. Перенести или отменить — по личной ссылке из подтверждения записи.`;

  if (!booking.weekly) {
    const o = booked[0];
    return buildIcs({ uid: stableUid('booking', o.start, title, owner), start: o.start, end: o.end, summary, description, now });
  }

  const base = seriesBase(occurrences);
  if (base === null) return null;
  const byIndex = new Map(occurrences.map((o) => [o.index, o]));
  const minIndex = Math.min(...occurrences.map((o) => o.index));
  const maxIndex = Math.max(...occurrences.map((o) => o.index));
  const total = Math.max(booking.weeks || 0, maxIndex + 1);
  const count = total - minIndex;
  // Длительность — по занятию, стоящему на своём месте (у перенесённого она та же).
  const regular = occurrences.find((o) => parse(o.start) === base + o.index * WEEK_MS) ?? occurrences[0];
  const durationMs = parse(regular.end) - parse(regular.start) || booking.duration_minutes * 60_000;
  const first = base + minIndex * WEEK_MS;
  const uid = stableUid('booking', iso(base), title, owner);

  const exdates: string[] = [];
  const overrides: IcsEvent[] = [];
  for (let k = minIndex; k < minIndex + count; k++) {
    const original = iso(base + k * WEEK_MS);
    const o = byIndex.get(k);
    if (!o || o.status === 'cancelled') {
      // Отменённое — исключаем; неизвестное в середине серии тоже (не обещаем лишнего).
      if (!o && k > maxIndex) continue; // хвост серии за окном ответа сервера — остаётся в правиле
      exdates.push(original);
      continue;
    }
    if (parse(o.start) !== base + k * WEEK_MS) {
      overrides.push({ uid, recurrenceId: original, start: o.start, end: o.end, summary, description, now });
    }
  }

  const master: IcsEvent = {
    uid,
    start: iso(first),
    end: iso(first + durationMs),
    summary,
    description,
    now,
    ...(count > 1 ? { rrule: `FREQ=WEEKLY;COUNT=${count}` } : {}),
    exdates,
  };
  return buildIcs([master, ...overrides]);
}

export function bookingIcsFileName(booking: Pick<GuestBooking, 'occurrences'>): string {
  const first = validOccurrences(booking.occurrences)[0];
  return icsFileNameFor('booking', first?.start ?? '');
}

// ------------------------------------------------------------------ ошибки

/** Ошибка методов владельца (booking.*, referral.info): сеть — своими словами, остальное — как сказал сервер. */
export function bookingErrorText(error: unknown): string {
  if (!(error instanceof AuraError)) return 'Что-то пошло не так';
  if (error.code === 'timeout') return 'Сервер не ответил вовремя — попробуйте ещё раз';
  if (isNetworkError(error)) return 'Нет связи с сервером Aura';
  if (error.code === 'not_found' && !error.message) return 'Не найдено — возможно, уже удалено';
  if (error.code === 'conflict' && !error.message) return 'Уже отменено';
  if (error.code === 'upstream_error' || error.code === 'upstream') return 'Аура сейчас не может подобрать время — попробуйте позже';
  return sentence(error.message) || 'Что-то пошло не так';
}

/** Сколько занятий: «8 занятий». */
export const lessonsCount = (n: number) => `${n} ${plural(n, 'занятие', 'занятия', 'занятий')}`;
