import { describe, expect, it } from 'vitest';
import { CONTACTS, VENUES, genitive, makeMe } from '../data/mock';
import { parseIntent } from './intent';
import { candidateSlots, negotiate, rankVenues, windowDays } from './negotiation';

// Wednesday 2026-10-14, 18:00 local.
const WED = new Date(2026, 9, 14, 18, 0);
const me = makeMe('Алекс Морган');
const dima = CONTACTS.find((c) => c.id === 'dima')!;
const kira = CONTACTS.find((c) => c.id === 'kira')!;

describe('windowDays', () => {
  it('weekend from a weekday is the coming Saturday and Sunday', () => {
    const days = windowDays('weekend', WED);
    expect(days.map((d) => d.getDay())).toEqual([6, 0]);
    expect(days[0].getDate()).toBe(17);
  });

  it('weekend on Sunday is just today', () => {
    expect(windowDays('weekend', new Date(2026, 9, 18, 9)).length).toBe(1);
  });
});

describe('candidateSlots', () => {
  it('skips slots starting within the next hour', () => {
    const slots = candidateSlots('today', 60, WED);
    expect(slots[0].getHours()).toBe(19);
  });
});

describe('negotiate', () => {
  it('reproduces the Saturday 14:00 Green Point scenario', () => {
    const r = negotiate({ contactId: 'dima', window: 'weekend', durationMin: 90, budget: 1000, topic: 'стартап' }, me, dima, VENUES, WED);
    expect(r.failure).toBeNull();
    const start = new Date(r.outcome!.start);
    expect(start.getDay()).toBe(6);
    expect(start.getHours()).toBe(14);
    expect(r.outcome!.venueName).toBe('Green Point');
    expect(r.outcome!.cost).toBe(900);
    expect(r.steps.map((s) => s.kind)).toEqual(['propose', 'check', 'counter', 'venue', 'accept']);
    expect(r.steps[0].text).toBe('Аура Алекса предложила субботу в 13:00');
  });

  it('fails when calendars never overlap', () => {
    const r = negotiate({ contactId: 'kira', window: 'weekend', durationMin: 90, budget: null, topic: '' }, me, kira, VENUES, WED);
    expect(r.outcome).toBeNull();
    expect(r.failure).toMatch(/Общего свободного окна/);
  });

  it('fails when the budget is too small for any suitable venue', () => {
    const r = negotiate({ contactId: 'dima', window: 'weekend', durationMin: 90, budget: 500, topic: '' }, me, dima, VENUES, WED);
    expect(r.failure).toMatch(/бюджете до 500/);
  });
});

describe('rankVenues', () => {
  it('respects every diet of both people', () => {
    const sat = new Date(2026, 9, 17, 14);
    const vegan = { ...me, diet: ['vegan' as const] };
    const ids = rankVenues(VENUES, vegan, dima, sat, 90, null).map((v) => v.id);
    expect(ids).toEqual(expect.arrayContaining(['green-point', 'veg-lab']));
    expect(ids).not.toContain('brasserie');
  });
});

describe('parseIntent', () => {
  it('understands the canonical request', () => {
    const i = parseIntent('Хочу встретиться с Димой в эти выходные и обсудить стартап', CONTACTS);
    expect(i).toMatchObject({ kind: 'meet', contactId: 'dima', window: 'weekend', topic: 'стартап', budget: null });
  });

  it('reads a budget and duration', () => {
    const i = parseIntent('Кофе с Машей завтра на 1 час до 2 тыс', CONTACTS);
    expect(i).toMatchObject({ kind: 'meet', contactId: 'maria', window: 'tomorrow', durationMin: 60, budget: 2000 });
  });

  it('recognises reminders', () => {
    expect(parseIntent('Напомни мне позвонить юристу', CONTACTS)).toEqual({ kind: 'reminder', text: 'позвонить юристу' });
  });
});

describe('genitive', () => {
  it.each([
    ['Алекс', 'Алекса'],
    ['Дима', 'Димы'],
    ['Маша', 'Маши'],
    ['Мария', 'Марии'],
    ['Андрей', 'Андрея'],
  ])('%s → %s', (n, g) => expect(genitive(n)).toBe(g));
});
