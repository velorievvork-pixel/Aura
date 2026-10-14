import { describe, expect, it } from 'vitest';
import type { Booking, GuestBooking, Occurrence } from '../net/bookingApi';
import { AuraError } from '../net/socket';
import {
  agendaByDay,
  bookingErrorText,
  bookingIcs,
  bookingIcsFileName,
  canCreatePage,
  durationLabel,
  formToInput,
  forgetReferral,
  guestOccurrences,
  hoursSummary,
  isPlausibleSlug,
  manageLink,
  nextOccurrence,
  normalizeReferral,
  NEW_PAGE,
  pageFormErrors,
  pageLink,
  pageToForm,
  pendingReferral,
  planView,
  referralFromSearch,
  referralLink,
  rememberReferral,
  seriesBase,
  signupPath,
  skippedPhrase,
  toggle,
  upcomingCount,
  weeklyLabel,
  weeklyPhrase,
  weekdaysSummary,
} from './booking';
import { publicLink } from './publicLink';

const loc = (href: string) => {
  const u = new URL(href);
  return { protocol: u.protocol, hostname: u.hostname, origin: u.origin, pathname: u.pathname };
};
const TOKEN = 'AbCdEfGhIjKlMnOpQrStUvWxYz0123456789-_abcde';
const now = new Date('2026-10-14T12:00:00Z'); // среда
const H = 3_600_000;
const W = 7 * 24 * H;
const at = (ms: number) => new Date(ms).toISOString().replace('.000Z', 'Z');
const occ = (index: number, startMs: number, status: Occurrence['status'] = 'booked', minutes = 60): Occurrence => ({
  index,
  start: at(startMs),
  end: at(startMs + minutes * 60_000),
  status,
});
const unfold = (ics: string) => ics.replace(/\r\n /g, '').split('\r\n');

describe('ссылки', () => {
  it('страница записи: url от сервера, иначе адрес клиента #/b/, иначе slug', () => {
    expect(pageLink({ slug: 'k7m2q9xw4p', url: 'https://aura.example.com/#/b/k7m2q9xw4p' }, loc('http://localhost:5173/'))).toEqual({
      kind: 'url',
      url: 'https://aura.example.com/#/b/k7m2q9xw4p',
    });
    expect(pageLink({ slug: 'k7m2q9xw4p', url: '' }, loc('https://app.example/web/'))).toEqual({ kind: 'url', url: 'https://app.example/web/#/b/k7m2q9xw4p' });
    expect(pageLink({ slug: 'k7m2q9xw4p', url: '' }, loc('https://localhost/'))).toEqual({ kind: 'token', token: 'k7m2q9xw4p' });
  });

  it('ссылка управления #/m/<токен>', () => {
    expect(manageLink(TOKEN, '', loc('https://app.example/'))).toEqual({ kind: 'url', url: `https://app.example/#/m/${TOKEN}` });
    expect(manageLink(TOKEN, `https://aura.example.com/#/m/${TOKEN}`, null)).toEqual({ kind: 'url', url: `https://aura.example.com/#/m/${TOKEN}` });
    expect(manageLink(TOKEN, '', null)).toEqual({ kind: 'token', token: TOKEN });
  });

  it('publicLink не принимает не-http адрес от сервера', () => {
    expect(publicLink('/b/x', 'x', 'javascript:alert(1)', null)).toEqual({ kind: 'token', token: 'x' });
  });

  it('slug', () => {
    expect(isPlausibleSlug('k7m2q9xw4p')).toBe(true);
    expect(isPlausibleSlug('')).toBe(false);
    expect(isPlausibleSlug(undefined)).toBe(false);
    expect(isPlausibleSlug('../etc')).toBe(false);
  });
});

describe('реферальный код', () => {
  it('нормализация и ref из адреса', () => {
    expect(normalizeReferral(' abcd2345 ')).toBe('ABCD2345');
    expect(normalizeReferral('<script>')).toBe('');
    expect(normalizeReferral(null)).toBe('');
    expect(referralFromSearch('?ref=abcd2345')).toBe('ABCD2345');
    expect(referralFromSearch('?ref=')).toBe('');
    expect(referralFromSearch('')).toBe('');
    expect(signupPath('ABCD2345')).toBe('/signup?ref=ABCD2345');
    expect(signupPath('')).toBe('/signup');
    expect(signupPath('плохой код')).toBe('/signup');
  });

  it('ссылка «Пригласить коллегу»', () => {
    expect(referralLink({ code: 'ABCD2345', url: '' }, loc('https://app.example/'))).toEqual({ kind: 'url', url: 'https://app.example/#/signup?ref=ABCD2345' });
    expect(referralLink({ code: 'ABCD2345', url: 'https://aura.example.com/#/signup?ref=ABCD2345' }, null)).toEqual({
      kind: 'url',
      url: 'https://aura.example.com/#/signup?ref=ABCD2345',
    });
    expect(referralLink({ code: 'ABCD2345', url: '' }, loc('http://localhost:5173/'))).toEqual({ kind: 'token', token: 'ABCD2345' });
  });

  it('хранится до регистрации; недоступное хранилище не роняет страницу', () => {
    const map = new Map<string, string>();
    const store = { getItem: (k: string) => map.get(k) ?? null, setItem: (k: string, v: string) => void map.set(k, v), removeItem: (k: string) => void map.delete(k) };
    expect(pendingReferral(store)).toBe('');
    rememberReferral('abcd2345', store);
    expect(pendingReferral(store)).toBe('ABCD2345');
    rememberReferral('мусор', store);
    expect(pendingReferral(store)).toBe('ABCD2345');
    forgetReferral(store);
    expect(pendingReferral(store)).toBe('');

    const broken = {
      getItem: () => {
        throw new Error('SecurityError');
      },
      setItem: () => {
        throw new Error('QuotaExceeded');
      },
      removeItem: () => {
        throw new Error('SecurityError');
      },
    };
    expect(() => rememberReferral('ABCD2345', broken)).not.toThrow();
    expect(pendingReferral(broken)).toBe('');
    expect(() => forgetReferral(broken)).not.toThrow();
    expect(pendingReferral(null)).toBe('');
  });
});

describe('тариф', () => {
  it('Free и Pro с датой', () => {
    expect(planView({ name: 'free', pro_until: '', max_pages: 1, max_weeks: 8 }, now)).toMatchObject({ pro: false, name: 'Free', until: '', maxPages: 1, maxWeeks: 8 });
    expect(planView({ name: 'pro', pro_until: '2026-11-13T12:00:00Z', max_pages: 5, max_weeks: 26 }, now, 'Europe/Moscow')).toMatchObject({
      pro: true,
      name: 'Pro',
      until: 'до 13 ноября',
      maxPages: 5,
    });
    expect(planView({ name: 'pro', pro_until: '2027-01-13T12:00:00Z', max_pages: 5, max_weeks: 26 }, now).until).toBe('до 13 января 2027');
  });

  it('истёкший Pro — уже Free', () => {
    expect(planView({ name: 'pro', pro_until: '2026-10-01T00:00:00Z', max_pages: 5, max_weeks: 26 }, now).pro).toBe(false);
  });

  it('лимит страниц', () => {
    const free = { name: 'free' as const, pro_until: '', max_pages: 1, max_weeks: 8 };
    expect(canCreatePage([], free, now)).toBe(true);
    expect(canCreatePage([{}], free, now)).toBe(false);
    expect(canCreatePage([{}], { name: 'pro', pro_until: '2026-11-13T12:00:00Z', max_pages: 5, max_weeks: 26 }, now)).toBe(true);
  });
});

describe('форма страницы', () => {
  it('умолчания как на сервере', () => {
    const input = formToInput({ ...NEW_PAGE, title: '  Математика, 60 минут ' });
    expect(input).toEqual({
      title: 'Математика, 60 минут',
      description: '',
      duration_minutes: 60,
      window_days: 14,
      min_notice_hours: 12,
      buffer_minutes: 0,
      hours: [],
      weekdays: [0, 1, 2, 3, 4, 5],
      allow_weekly: true,
      active: true,
    });
    expect(formToInput({ ...NEW_PAGE, title: 'x', hours: [17, 15, 15, 99] }, 3)).toMatchObject({ id: 3, hours: [15, 17] });
  });

  it('страница → форма → поля сохранения', () => {
    const page = {
      id: 3,
      slug: 'k7m2q9xw4p',
      url: '',
      title: 'Математика',
      description: 'Онлайн',
      duration_minutes: 90,
      window_days: 21,
      min_notice_hours: 24,
      buffer_minutes: 10,
      hours: [16, 15],
      weekdays: [4, 0],
      allow_weekly: false,
      active: false,
      created_at: '',
      updated_at: '',
    };
    expect(formToInput(pageToForm(page), page.id)).toEqual({
      id: 3,
      title: 'Математика',
      description: 'Онлайн',
      duration_minutes: 90,
      window_days: 21,
      min_notice_hours: 24,
      buffer_minutes: 10,
      hours: [15, 16],
      weekdays: [0, 4],
      allow_weekly: false,
      active: false,
    });
  });

  it('ограничения', () => {
    expect(pageFormErrors({ ...NEW_PAGE, title: ' ' }).title).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'a\nb' }).title).toBe('Название — в одну строку');
    expect(pageFormErrors({ ...NEW_PAGE, title: 'x'.repeat(121) }).title).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'x', description: 'y'.repeat(1001) }).description).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'x', duration: 10 }).duration).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'x', windowDays: 61 }).windowDays).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'x', noticeHours: 169 }).noticeHours).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'x', bufferMinutes: 121 }).bufferMinutes).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'x', weekdays: [] }).weekdays).toBeTruthy();
    expect(pageFormErrors({ ...NEW_PAGE, title: 'Математика' })).toEqual({});
  });

  it('подписи дней, часов, длительности', () => {
    expect(weekdaysSummary([0, 1, 2, 3, 4])).toBe('Пн–Пт');
    expect(weekdaysSummary([0, 2, 4])).toBe('Пн, Ср, Пт');
    expect(weekdaysSummary([5, 6])).toBe('Сб, Вс');
    expect(weekdaysSummary([0, 1, 2, 3, 4, 5, 6])).toBe('каждый день');
    expect(hoursSummary([15, 16, 17, 18, 19, 20])).toBe('15:00–21:00');
    expect(hoursSummary([9, 10, 14, 23])).toBe('9:00–11:00, 14:00–15:00, 23:00–24:00');
    expect(hoursSummary([])).toBe('рабочие часы из настроек');
    expect(toggle([1, 3], 2)).toEqual([1, 2, 3]);
    expect(toggle([1, 2, 3], 2)).toEqual([1, 3]);
    expect(durationLabel(45)).toBe('45 мин');
    expect(durationLabel(60)).toBe('1 час');
    expect(durationLabel(90)).toBe('1,5 часа');
    expect(durationLabel(120)).toBe('2 часа');
    expect(weeklyLabel(8)).toBe('Каждую неделю (8 занятий)');
    expect(weeklyLabel(26)).toBe('Каждую неделю (26 занятий)');
    expect(weeklyLabel(1)).toBe('Каждую неделю (1 занятие)');
  });
});

describe('расписание владельца', () => {
  const t0 = Date.parse('2026-10-15T13:00:00Z'); // четверг, 16:00 по Москве
  const base: Omit<Booking, 'id' | 'occurrences'> = {
    page_id: 3,
    title: 'Математика',
    guest_name: 'Маша',
    guest_email: '',
    guest_comment: '',
    duration_minutes: 60,
    weekly: true,
    weeks: 8,
    status: 'active',
    created_at: '',
  };
  const series: Booking = { ...base, id: 9, occurrences: [occ(1, t0 + W), occ(0, t0), occ(2, t0 + 2 * W, 'cancelled')] };
  const single: Booking = { ...base, id: 10, guest_name: 'Петя', weekly: false, weeks: 1, occurrences: [occ(0, t0 - 2 * H)] };
  const past: Booking = { ...base, id: 11, weekly: false, weeks: 1, occurrences: [occ(0, Date.parse('2026-10-14T09:00:00Z'))] };

  it('по дням в поясе владельца, хронологически, без отменённых и прошедших', () => {
    const days = agendaByDay([series, single, past], 'Europe/Moscow', now);
    expect(days.map((d) => d.title)).toEqual(['Завтра, 15 октября', 'Четверг, 22 октября']);
    expect(days[0].items.map((i) => [i.booking.guest_name, i.time, i.series])).toEqual([
      ['Петя', '14:00–15:00', ''],
      ['Маша', '16:00–17:00', 'занятие 1 из 8'],
    ]);
    expect(days[1].items[0].series).toBe('занятие 2 из 8');
    expect(days.flatMap((d) => d.items).every((i) => i.canCancel)).toBe(true);
  });

  it('с отменёнными', () => {
    const days = agendaByDay([series], 'Europe/Moscow', now, true);
    const cancelled = days.flatMap((d) => d.items).filter((i) => i.cancelled);
    expect(cancelled.map((i) => i.occurrence.index)).toEqual([2]);
    expect(cancelled[0].canCancel).toBe(false);
    expect(agendaByDay([{ ...series, status: 'cancelled' }], 'UTC', now)).toEqual([]);
  });

  it('счётчик будущих занятий', () => {
    expect(upcomingCount(series, now)).toBe(2);
    expect(upcomingCount({ ...series, status: 'cancelled' }, now)).toBe(0);
  });
});

describe('занятия гостя', () => {
  const t0 = Date.parse('2026-10-15T13:00:00Z');
  const booking: Pick<GuestBooking, 'occurrences' | 'status'> = {
    status: 'active',
    occurrences: [occ(1, t0 + W), occ(0, Date.parse('2026-10-14T11:00:00Z')), occ(2, t0 + 2 * W, 'cancelled')],
  };

  it('прошедшее и отменённое не меняются', () => {
    const list = guestOccurrences(booking, now);
    expect(list.map((o) => [o.index, o.past, o.changeable])).toEqual([
      [0, true, false],
      [1, false, true],
      [2, false, false],
    ]);
    expect(nextOccurrence(booking, now)?.index).toBe(1);
    expect(guestOccurrences({ ...booking, status: 'cancelled' }, now).some((o) => o.changeable)).toBe(false);
  });

  it('повторение и пропущенные недели — в поясе гостя', () => {
    expect(weeklyPhrase({ start: '2026-10-15T13:00:00Z', end: '' }, 'Europe/Moscow')).toBe('по четвергам в 16:00');
    expect(weeklyPhrase({ start: '2026-10-15T22:30:00Z', end: '' }, 'Europe/Moscow')).toBe('по пятницам в 01:30');
    expect(skippedPhrase(['2026-10-29T13:00:00Z', '2026-10-22T13:00:00Z', 'x'], 'Europe/Moscow')).toBe('22 октября, 29 октября');
  });
});

describe('.ics записи', () => {
  const t0 = Date.parse('2026-10-15T13:00:00Z');
  const guest: GuestBooking = {
    title: 'Математика, 60 минут',
    owner_name: 'Анна',
    owner_timezone: 'Europe/Moscow',
    duration_minutes: 60,
    weekly: true,
    weeks: 8,
    status: 'active',
    guest_name: 'Маша',
    referral_code: '',
    occurrences: Array.from({ length: 8 }, (_, k) => occ(k, t0 + k * W)),
  };

  it('серия: RRULE COUNT=N, UTC, EXDATE для отменённых', () => {
    const b = { ...guest, occurrences: guest.occurrences.map((o) => (o.index === 2 || o.index === 5 ? { ...o, status: 'cancelled' as const } : o)) };
    const lines = unfold(bookingIcs(b, now)!);
    expect(lines.filter((l) => l === 'BEGIN:VEVENT')).toHaveLength(1);
    expect(lines).toContain('DTSTART:20261015T130000Z');
    expect(lines).toContain('DTEND:20261015T140000Z');
    expect(lines).toContain('RRULE:FREQ=WEEKLY;COUNT=8');
    expect(lines).toContain('EXDATE:20261029T130000Z,20261119T130000Z');
    expect(lines).toContain('SUMMARY:Математика\\, 60 минут — Анна');
    expect(lines.join('\n')).not.toContain(TOKEN);
  });

  it('перенесённое занятие — VEVENT с RECURRENCE-ID и тем же UID', () => {
    const moved = { ...occ(3, t0 + 3 * W + 2 * 24 * H) }; // суббота той же недели
    const b = { ...guest, occurrences: guest.occurrences.map((o) => (o.index === 3 ? moved : o)) };
    expect(seriesBase(b.occurrences)).toBe(t0);
    const lines = unfold(bookingIcs(b, now)!);
    expect(lines.filter((l) => l === 'BEGIN:VEVENT')).toHaveLength(2);
    expect(lines).toContain('RECURRENCE-ID:20261105T130000Z');
    expect(lines).toContain('DTSTART:20261107T130000Z');
    const uids = lines.filter((l) => l.startsWith('UID:'));
    expect(new Set(uids).size).toBe(1);
    expect(lines.some((l) => l.startsWith('EXDATE'))).toBe(false);
  });

  it('прошедшие занятия не пришли с сервера — серия с первого известного', () => {
    const b = { ...guest, occurrences: guest.occurrences.filter((o) => o.index >= 3) };
    const lines = unfold(bookingIcs(b, now)!);
    expect(lines).toContain('DTSTART:20261105T130000Z');
    expect(lines).toContain('RRULE:FREQ=WEEKLY;COUNT=5');
  });

  it('недели, которые сервер пропустил (skipped), — отменённые, в EXDATE', () => {
    const b = { ...guest, weeks: 3, occurrences: [occ(0, t0), occ(1, t0 + W, 'cancelled'), occ(2, t0 + 2 * W)] };
    const lines = unfold(bookingIcs(b, now)!);
    expect(lines).toContain('RRULE:FREQ=WEEKLY;COUNT=3');
    expect(lines).toContain('EXDATE:20261022T130000Z');
  });

  it('разовая запись — одно событие без RRULE', () => {
    const b = { ...guest, weekly: false, weeks: 1, occurrences: [occ(0, t0)] };
    const lines = unfold(bookingIcs(b, now)!);
    expect(lines).toContain('DTSTART:20261015T130000Z');
    expect(lines.some((l) => l.startsWith('RRULE'))).toBe(false);
    expect(bookingIcsFileName(b)).toBe('aura-booking-20261015.ics');
  });

  it('отменённая запись — нечего добавлять', () => {
    expect(bookingIcs({ ...guest, status: 'cancelled' }, now)).toBeNull();
    expect(bookingIcs({ ...guest, occurrences: guest.occurrences.map((o) => ({ ...o, status: 'cancelled' as const })) }, now)).toBeNull();
  });

  it('UID стабилен между скачиваниями', () => {
    const a = unfold(bookingIcs(guest, now)!).find((l) => l.startsWith('UID:'));
    const b = unfold(bookingIcs({ ...guest, occurrences: guest.occurrences.slice(2) }, new Date('2026-10-30T00:00:00Z'))!).find((l) => l.startsWith('UID:'));
    expect(a).toBe(b);
    expect(a).toMatch(/^UID:booking-20261015T130000Z-[0-9a-f]{8}@aura$/);
  });
});

describe('ошибки владельца', () => {
  it('сеть, сервер', () => {
    expect(bookingErrorText(new AuraError('offline', ''))).toBe('Нет связи с сервером Aura');
    expect(bookingErrorText(new AuraError('timeout', ''))).toContain('не ответил');
    expect(bookingErrorText(new AuraError('forbidden', 'на тарифе Free — 1 страница записи'))).toBe('На тарифе Free — 1 страница записи');
    expect(bookingErrorText(new AuraError('upstream_error', 'ai: connection refused'))).toContain('Аура сейчас не может');
    expect(bookingErrorText(new Error('x'))).toBe('Что-то пошло не так');
  });
});
