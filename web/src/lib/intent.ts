import type { Contact, Window } from './types';

export type Intent =
  | { kind: 'meet'; contactId: string | null; window: Window; budget: number | null; topic: string; durationMin: number }
  | { kind: 'reminder'; text: string }
  | { kind: 'schedule' }
  | { kind: 'email'; contactId: string | null; text: string }
  | { kind: 'unknown'; text: string };

export function findContact(text: string, contacts: Contact[]): Contact | null {
  const words = text.toLowerCase().replace(/ё/g, 'е').split(/[^a-zа-я]+/);
  return contacts.find((c) => c.stems.some((s) => words.some((w) => w.startsWith(s)))) ?? null;
}

function parseWindow(t: string): Window {
  if (/выходн|суббот|воскрес/.test(t)) return 'weekend';
  if (/послезавтра/.test(t)) return 'week';
  if (/завтра/.test(t)) return 'tomorrow';
  if (/сегодня|вечером/.test(t)) return 'today';
  return 'week';
}

function parseBudget(t: string): number | null {
  const m = t.match(/(?:до|бюджет[а-я]*|за)\s*(\d[\d\s]*)\s*(к|тыс)?/);
  if (!m) return null;
  const n = parseInt(m[1].replace(/\s/g, ''), 10);
  return m[2] ? n * 1000 : n;
}

function parseTopic(text: string): string {
  const m = text.match(/(?:обсудить|поговорить о|про|насчет|насчёт)\s+(.+?)[.!?]*$/i);
  return m ? m[1].trim() : 'Встреча';
}

function parseDuration(t: string): number {
  const h = t.match(/(\d+(?:[.,]\d)?)\s*час/);
  if (h) return Math.round(parseFloat(h[1].replace(',', '.')) * 60);
  const m = t.match(/(\d+)\s*мин/);
  if (m) return parseInt(m[1], 10);
  return 90;
}

export function parseIntent(text: string, contacts: Contact[]): Intent {
  const t = text.toLowerCase().replace(/ё/g, 'е');
  const contact = findContact(text, contacts);

  if (/встрет|встреч|увидет|кофе|пообед|поужин|созвон|пересеч/.test(t)) {
    return {
      kind: 'meet',
      contactId: contact?.id ?? null,
      window: parseWindow(t),
      budget: parseBudget(t),
      topic: parseTopic(text),
      durationMin: parseDuration(t),
    };
  }
  if (/напомн|напоминан/.test(t)) {
    const body = text.replace(/^.*?напомни(?:\s+мне)?\s*/i, '').trim();
    return { kind: 'reminder', text: body || text };
  }
  if (/расписан|график|календар|что у меня|свободн/.test(t)) return { kind: 'schedule' };
  if (/письм|email|почт|напиши/.test(t)) return { kind: 'email', contactId: contact?.id ?? null, text };
  return { kind: 'unknown', text };
}

export const WINDOW_LABEL: Record<Window, string> = {
  today: 'сегодня',
  tomorrow: 'завтра',
  weekend: 'в эти выходные',
  week: 'на этой неделе',
};
