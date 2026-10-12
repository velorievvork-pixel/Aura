import { DIET_LABEL } from '../data/mock';
import { addDays, dayPhrase, hhmm, hourOf, startOfDay } from './time';
import type { MeetingRequest, NegotiationResult, Person, Point, Step, Venue, Window } from './types';

/** Earliest hour a meeting may start and latest hour it must end. */
const DAY_FROM = 9;
const DAY_TO = 22;
const SLOT_STEP_MIN = 30;
/** A slot must start at least this long from now, so people can get there. */
const LEAD_MIN = 60;

export function windowDays(window: Window, now: Date): Date[] {
  const today = startOfDay(now);
  switch (window) {
    case 'today':
      return [today];
    case 'tomorrow':
      return [addDays(today, 1)];
    case 'weekend': {
      const dow = today.getDay();
      if (dow === 6) return [today, addDays(today, 1)];
      if (dow === 0) return [today];
      return [addDays(today, 6 - dow), addDays(today, 7 - dow)];
    }
    case 'week':
      return Array.from({ length: 7 }, (_, i) => addDays(today, i));
  }
}

export function isFree(person: Person, start: Date, durationMin: number): boolean {
  const from = hourOf(start);
  const to = from + durationMin / 60;
  return !person.busy.some((r) => r.days.includes(start.getDay()) && from < r.to && to > r.from);
}

export function busyReason(person: Person, start: Date, durationMin: number): string | null {
  const from = hourOf(start);
  const to = from + durationMin / 60;
  const rule = person.busy.find((r) => r.days.includes(start.getDay()) && from < r.to && to > r.from);
  return rule ? rule.label : null;
}

/** All candidate starts inside the window, in time order. */
export function candidateSlots(window: Window, durationMin: number, now: Date): Date[] {
  const earliest = now.getTime() + LEAD_MIN * 60_000;
  const out: Date[] = [];
  for (const day of windowDays(window, now)) {
    for (let m = DAY_FROM * 60; m + durationMin <= DAY_TO * 60; m += SLOT_STEP_MIN) {
      const s = new Date(day);
      s.setMinutes(m);
      if (s.getTime() >= earliest) out.push(s);
    }
  }
  return out;
}

export function distance(a: Point, b: Point): number {
  return Math.hypot(a.x - b.x, a.y - b.y);
}

/**
 * Venues both people can eat at, open for the whole slot and within budget, ranked so neither person
 * travels much more than the other (minimise the longer trip, then the price).
 */
export function rankVenues(venues: Venue[], a: Person, b: Person, start: Date, durationMin: number, budget: number | null, people = 2): Venue[] {
  const diets = new Set([...a.diet, ...b.diet]);
  const from = hourOf(start);
  const to = from + durationMin / 60;
  return venues
    .filter((v) => [...diets].every((d) => v.tags.includes(d)))
    .filter((v) => v.opens <= from && v.closes >= to)
    .filter((v) => budget === null || v.avgCheck * people <= budget)
    .map((v) => ({ v, score: Math.max(distance(v.location, a.home), distance(v.location, b.home)) + v.avgCheck / 1000 }))
    .sort((x, y) => x.score - y.score)
    .map((x) => x.v);
}

const round1 = (n: number) => Math.round(n * 10) / 10;

/**
 * One agent-to-agent negotiation. `me` speaks for the user, `them` for the contact. Each agent only
 * reveals whether a slot works for it, never its whole calendar.
 */
export function negotiate(req: MeetingRequest, me: Person, them: Person, venues: Venue[], now: Date): NegotiationResult {
  const steps: Step[] = [];
  let round = 1;
  const meName = `Аура ${me.nameGen}`;
  const themName = `Аура ${them.nameGen}`;
  const fail = (failure: string): NegotiationResult => {
    steps.push({ round, actor: 'them', kind: 'fail', text: failure, detail: 'Переговоры остановлены — нужно ваше решение' });
    return { steps, outcome: null, failure };
  };

  const slots = candidateSlots(req.window, req.durationMin, now);
  const mine = slots.filter((s) => isFree(me, s, req.durationMin));
  if (mine.length === 0) return fail(`У вас нет свободных ${req.durationMin} минут в выбранный период`);

  const first = mine[0];
  steps.push({
    round: round++,
    actor: 'me',
    kind: 'propose',
    text: `${meName} предложила ${dayPhrase(first, now)} в ${hhmm(first)}`,
    detail: `Первое свободное окно у вас · ${req.durationMin} мин`,
  });

  let slot = first;
  const reason = busyReason(them, first, req.durationMin);
  if (reason) {
    const common = mine.find((s) => isFree(them, s, req.durationMin));
    steps.push({
      round: round++,
      actor: 'them',
      kind: 'check',
      text: `${themName} проверила календарь ${them.nameGen}`,
      detail: `В ${hhmm(first)} занято`,
    });
    if (!common) return fail('Общего свободного окна в этот период нет');
    slot = common;
    steps.push({
      round: round++,
      actor: 'them',
      kind: 'counter',
      text: `${themName} предложила ${dayPhrase(slot, now)} в ${hhmm(slot)}`,
      detail: 'Ваш календарь в это время свободен',
    });
  } else {
    steps.push({
      round: round++,
      actor: 'them',
      kind: 'check',
      text: `${themName} проверила календарь ${them.nameGen}`,
      detail: `${hhmm(first)} подходит`,
    });
  }

  const ranked = rankVenues(venues, me, them, slot, req.durationMin, req.budget);
  if (ranked.length === 0) {
    return fail(req.budget !== null ? `Нет места, подходящего обоим по питанию, в бюджете до ${req.budget} ₽` : 'Нет места, подходящего обоим по питанию');
  }
  const venue = ranked[0];
  const diets = [...new Set([...me.diet, ...them.diet])].map((d) => DIET_LABEL[d]);
  steps.push({
    round: round++,
    actor: 'them',
    kind: 'venue',
    text: `${themName} предложила ${venue.name}`,
    detail: [venue.kind, diets.length ? diets.join(', ') : null, 'на полпути'].filter(Boolean).join(' · '),
  });

  const cost = venue.avgCheck * 2;
  steps.push({
    round: round++,
    actor: 'me',
    kind: 'accept',
    text: `${meName} приняла место`,
    detail: req.budget !== null ? `~${cost} ₽ из ${req.budget} ₽` : `~${cost} ₽ на двоих`,
  });

  const end = new Date(slot.getTime() + req.durationMin * 60_000);
  return {
    steps,
    failure: null,
    outcome: {
      start: slot.toISOString(),
      end: end.toISOString(),
      venueId: venue.id,
      venueName: venue.name,
      people: 2,
      cost,
      distanceMe: round1(distance(venue.location, me.home)),
      distanceThem: round1(distance(venue.location, them.home)),
    },
  };
}
