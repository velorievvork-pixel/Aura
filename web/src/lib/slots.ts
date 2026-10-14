/*
  Время для публичных страниц (приглашение, запись): дни и часы в заданном
  часовом поясе (у гостя — пояс браузера), группировка слотов по дням, подписи.
  Без React; проверяется юнит-тестами (invite.test.ts, booking.test.ts).
*/

export interface TimeSlot {
  start: string;
  end: string;
}

export function browserTimeZone(): string {
  try {
    return Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
  } catch {
    return 'UTC';
  }
}

/** Пояс, который понимает Intl; неизвестный — UTC (страница не должна падать). */
export function safeZone(timeZone: string): string {
  try {
    new Intl.DateTimeFormat('ru-RU', { timeZone });
    return timeZone;
  } catch {
    return 'UTC';
  }
}

/** Ключ дня «ГГГГ-ММ-ДД» в часовом поясе. */
export function dayKey(date: Date, timeZone: string): string {
  const parts = new Intl.DateTimeFormat('en-CA', { timeZone: safeZone(timeZone), year: 'numeric', month: '2-digit', day: '2-digit' }).formatToParts(date);
  const get = (type: string) => parts.find((p) => p.type === type)?.value ?? '';
  return `${get('year')}-${get('month')}-${get('day')}`;
}

function nextDayKey(key: string): string {
  const [y, m, d] = key.split('-').map(Number);
  const next = new Date(Date.UTC(y, m - 1, d + 1));
  return `${next.getUTCFullYear()}-${String(next.getUTCMonth() + 1).padStart(2, '0')}-${String(next.getUTCDate()).padStart(2, '0')}`;
}

/** «10:00» в часовом поясе. */
export function timeIn(date: Date, timeZone: string): string {
  return new Intl.DateTimeFormat('ru-RU', { timeZone: safeZone(timeZone), hour: '2-digit', minute: '2-digit', hourCycle: 'h23' }).format(date);
}

/** «четверг, 15 октября» в часовом поясе. */
function dateIn(date: Date, timeZone: string): string {
  return new Intl.DateTimeFormat('ru-RU', { timeZone: safeZone(timeZone), weekday: 'long', day: 'numeric', month: 'long' }).format(date);
}

/** «15 октября» в часовом поясе. */
export function dayMonthIn(date: Date, timeZone: string): string {
  return new Intl.DateTimeFormat('ru-RU', { timeZone: safeZone(timeZone), day: 'numeric', month: 'long' }).format(date);
}

const capitalize = (s: string) => (s ? s[0].toUpperCase() + s.slice(1) : s);

/** «Сегодня, 15 октября» / «Завтра, 16 октября» / «Суббота, 17 октября». */
export function dayTitleIn(date: Date, timeZone: string, now = new Date()): string {
  const key = dayKey(date, timeZone);
  const today = dayKey(now, timeZone);
  const full = dateIn(date, timeZone);
  const dayMonth = full.includes(',') ? full.slice(full.indexOf(',') + 1).trim() : full;
  if (key === today) return `Сегодня, ${dayMonth}`;
  if (key === nextDayKey(today)) return `Завтра, ${dayMonth}`;
  return capitalize(full);
}

export interface SlotView extends TimeSlot {
  /** «10:00–11:00» */
  label: string;
}

export interface DayGroup {
  key: string;
  title: string;
  slots: SlotView[];
}

/** «10:00–11:00» в поясе. */
export function rangeIn(slot: TimeSlot, timeZone: string): string {
  return `${timeIn(new Date(slot.start), timeZone)}–${timeIn(new Date(slot.end), timeZone)}`;
}

/** Слоты по дням (в поясе гостя), хронологически; прошедшие и битые отбрасываются. */
export function groupSlotsByDay(slots: TimeSlot[], timeZone: string, now = new Date()): DayGroup[] {
  const valid = (slots ?? [])
    .map((s) => ({ s, start: Date.parse(s?.start), end: Date.parse(s?.end) }))
    .filter((x) => !Number.isNaN(x.start) && !Number.isNaN(x.end) && x.end > x.start && x.start > now.getTime())
    .sort((a, b) => a.start - b.start);
  const groups: DayGroup[] = [];
  for (const { s, start } of valid) {
    const startDate = new Date(start);
    const key = dayKey(startDate, timeZone);
    let group = groups.find((g) => g.key === key);
    if (!group) {
      group = { key, title: dayTitleIn(startDate, timeZone, now), slots: [] };
      groups.push(group);
    }
    group.slots.push({ start: s.start, end: s.end, label: rangeIn(s, timeZone) });
  }
  return groups;
}

/** «Суббота, 17 октября, 10:00–11:00» — выбранное время целиком. */
export function slotPhrase(slot: TimeSlot, timeZone: string, now = new Date()): string {
  const start = new Date(slot.start);
  const end = new Date(slot.end);
  if (Number.isNaN(start.getTime()) || Number.isNaN(end.getTime())) return '';
  return `${dayTitleIn(start, timeZone, now)}, ${timeIn(start, timeZone)}–${timeIn(end, timeZone)}`;
}

/** «Europe/Moscow (GMT+3)» — подпись часового пояса. */
export function timeZoneLabel(timeZone: string, at = new Date()): string {
  const zone = safeZone(timeZone);
  try {
    const offset = new Intl.DateTimeFormat('en-US', { timeZone: zone, timeZoneName: 'shortOffset' })
      .formatToParts(at)
      .find((p) => p.type === 'timeZoneName')?.value;
    return offset ? `${zone.replace(/_/g, ' ')} (${offset})` : zone.replace(/_/g, ' ');
  } catch {
    return zone;
  }
}

/** Пояс владельца показываем, только если сейчас часы в нём другие, чем у гостя. */
export function zoneDiffers(ownerZone: string, zone: string, now = new Date()): boolean {
  return Boolean(ownerZone) && timeIn(now, ownerZone) !== timeIn(now, zone);
}
