import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { AuraError } from '../net/socket';
import type { Occurrence } from '../net/bookingApi';
import {
  bookingIcs,
  bookingOwnerErrorText,
  bookingStatus,
  forgetRef,
  isPlausibleSlug,
  nextOccurrence,
  PAGE_DEFAULTS,
  pageFormErrors,
  parseRef,
  publicLink,
  recalledRef,
  rememberRef,
  seriesLabel,
  signupPath,
  sortedOccurrences,
  upcomingCount,
} from './booking';

const loc = (href: string) => {
  const u = new URL(href);
  return { protocol: u.protocol, hostname: u.hostname, origin: u.origin, pathname: u.pathname };
};
const NOW = new Date('2026-10-14T12:00:00Z');
const occ = (start: string, status: Occurrence['status'] = 'active'): Occurrence => ({
  start,
  end: new Date(Date.parse(start) + 3_600_000).toISOString(),
  status,
});

describe('ссылки', () => {
  it('slug — 10 символов [a-z0-9]', () => {
    expect(isPlausibleSlug('abc123def4')).toBe(true);
    for (const bad of [undefined, '', 'ABC123DEF4', 'abc123def', 'abc123def45', '../etc/pas']) expect(isPlausibleSlug(bad)).toBe(false);
  });

  it('публичная ссылка: от сервера, иначе публичный адрес клиента, иначе null', () => {
    expect(publicLink('/b/abc123def4', 'https://aura.example.com/#/b/abc123def4', loc('http://localhost:5173/'))).toBe('https://aura.example.com/#/b/abc123def4');
    expect(publicLink('/b/abc123def4', '', loc('https://app.example/web/'))).toBe('https://app.example/web/#/b/abc123def4');
    expect(publicLink('/m/tok', '', loc('http://localhost:5173/'))).toBeNull();
    expect(publicLink('/m/tok', 'javascript:alert(1)', null)).toBeNull();
  });

  it('путь регистрации с кодом и источником', () => {
    expect(signupPath('ABCD2345')).toBe('/signup?ref=ABCD2345');
    expect(signupPath('ABCD2345', 'booking')).toBe('/signup?ref=ABCD2345&src=booking');
    expect(signupPath('', 'booking')).toBe('/signup');
  });
});

describe('реферальный код', () => {
  beforeEach(() => {
    const store = new Map<string, string>();
    vi.stubGlobal('sessionStorage', {
      getItem: (k: string) => store.get(k) ?? null,
      setItem: (k: string, v: string) => void store.set(k, v),
      removeItem: (k: string) => void store.delete(k),
    });
  });
  afterEach(() => {
    forgetRef();
    vi.unstubAllGlobals();
  });

  it('разбор: 8 букв/цифр, регистр не важен, мусор — null', () => {
    expect(parseRef(new URLSearchParams('ref=abcd2345&src=booking'))).toEqual({ code: 'ABCD2345', source: 'booking' });
    expect(parseRef(new URLSearchParams('ref=ABCD2345&src=evil'))).toEqual({ code: 'ABCD2345', source: 'link' });
    expect(parseRef(new URLSearchParams('ref=<script>'))).toBeNull();
    expect(parseRef(new URLSearchParams(''))).toBeNull();
  });

  it('переживает переход внутри вкладки', () => {
    expect(recalledRef()).toBeNull();
    rememberRef({ code: 'ABCD2345', source: 'invite' });
    expect(recalledRef()).toEqual({ code: 'ABCD2345', source: 'invite' });
    forgetRef();
    expect(recalledRef()).toBeNull();
  });

  it('без хранилища (приватный режим) — молча без кода', () => {
    vi.stubGlobal('sessionStorage', undefined);
    rememberRef({ code: 'ABCD2345', source: 'link' });
    expect(recalledRef()).toBeNull();
  });
});

describe('форма страницы', () => {
  it('по умолчанию ошибка только в названии', () => {
    expect(pageFormErrors(PAGE_DEFAULTS)).toEqual({ title: expect.any(String) });
    expect(pageFormErrors({ ...PAGE_DEFAULTS, title: 'Консультация' })).toEqual({});
  });

  it('пределы как на сервере', () => {
    const ok = { ...PAGE_DEFAULTS, title: 'Встреча' };
    expect(pageFormErrors({ ...ok, title: 'а'.repeat(121) }).title).toBeTruthy();
    expect(pageFormErrors({ ...ok, title: 'две\nстроки' }).title).toBeTruthy();
    expect(pageFormErrors({ ...ok, description: 'а'.repeat(1001) }).description).toBeTruthy();
    for (const bad of [{ duration_minutes: 14 }, { days_ahead: 61 }, { max_per_day: 0 }, { notice_hours: 169 }, { max_series: 13 }, { max_per_day: Number.NaN }, { max_series: 1.5 }]) {
      expect(pageFormErrors({ ...ok, ...bad }).numbers).toBeTruthy();
    }
    expect(pageFormErrors({ ...ok, notice_hours: 0, max_series: 12, days_ahead: 60, max_per_day: 16, duration_minutes: 240 })).toEqual({});
  });
});

describe('встречи', () => {
  const list = [occ('2026-10-28T07:00:00Z'), occ('2026-10-14T07:00:00Z'), occ('2026-10-21T07:00:00Z', 'cancelled'), occ('2026-10-15T07:00:00Z')];

  it('порядок, ближайшая и число предстоящих', () => {
    expect(sortedOccurrences(list).map((o) => o.start.slice(0, 10))).toEqual(['2026-10-14', '2026-10-15', '2026-10-21', '2026-10-28']);
    expect(nextOccurrence(list, NOW)?.start).toBe('2026-10-15T07:00:00Z');
    expect(upcomingCount(list, NOW)).toBe(2);
    expect(nextOccurrence([], NOW)).toBeNull();
  });

  it('статус записи для владельца', () => {
    expect(bookingStatus({ status: 'active', cancelled_by: '', occurrences: list }, NOW)).toEqual({ label: 'Предстоит', tone: 'green' });
    expect(bookingStatus({ status: 'active', cancelled_by: '', occurrences: [occ('2026-10-01T07:00:00Z')] }, NOW).label).toBe('Прошла');
    expect(bookingStatus({ status: 'cancelled', cancelled_by: 'owner', occurrences: [] }, NOW).label).toBe('Отменили вы');
    expect(bookingStatus({ status: 'cancelled', cancelled_by: 'guest', occurrences: [] }, NOW).label).toBe('Отменил гость');
  });

  it('подпись серии', () => {
    expect(seriesLabel(1)).toBe('Одна встреча');
    expect(seriesLabel(2)).toBe('Серия: 2 встречи, раз в неделю');
    expect(seriesLabel(5)).toBe('Серия: 5 встреч, раз в неделю');
  });
});

describe('bookingIcs', () => {
  it('одно событие на каждую активную встречу, CRLF', () => {
    const ics = bookingIcs({ page_title: 'Консультация', owner_name: 'Ольга', occurrences: [occ('2026-10-21T07:00:00Z'), occ('2026-10-28T07:00:00Z', 'cancelled'), occ('2026-11-04T07:00:00Z')] }, NOW);
    expect(ics.match(/BEGIN:VEVENT/g)).toHaveLength(2);
    expect(ics).toContain('DTSTART:20261021T070000Z');
    expect(ics).toContain('DTSTART:20261104T070000Z');
    expect(ics).not.toContain('DTSTART:20261028T070000Z');
    expect(ics).toContain('SUMMARY:Консультация — Ольга');
    expect(ics.endsWith('END:VCALENDAR\r\n')).toBe(true);
    expect(ics.split('\r\n').every((line) => !line.includes('\n'))).toBe(true);
  });

  it('без активных встреч — ошибка', () => {
    expect(() => bookingIcs({ page_title: 'X', owner_name: '', occurrences: [occ('2026-10-21T07:00:00Z', 'cancelled')] })).toThrow();
  });
});

describe('ошибки владельца', () => {
  it('сеть и таймаут — своими словами, остальное — текст сервера', () => {
    expect(bookingOwnerErrorText(new AuraError('timeout', ''))).toMatch(/не ответил/);
    expect(bookingOwnerErrorText(new AuraError('forbidden', 'не больше 10 страниц записи'))).toBe('Не больше 10 страниц записи');
    expect(bookingOwnerErrorText(new Error('x'))).toBe('Что-то пошло не так');
  });
});
