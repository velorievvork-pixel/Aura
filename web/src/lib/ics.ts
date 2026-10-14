/*
  Файлы календаря .ics (RFC 5545) без зависимостей: одно событие (приглашение,
  разовая запись) или еженедельная серия (RRULE:FREQ=WEEKLY;COUNT=N, EXDATE для
  отменённых занятий, отдельный VEVENT с RECURRENCE-ID для перенесённого).
  Всё время — в UTC (…Z), строки — через CRLF, длинные — с переносом по 75 октетов.
  Проверяется юнит-тестами (invite.test.ts, booking.test.ts).
*/

export interface IcsEvent {
  uid: string;
  start: string;
  end: string;
  summary: string;
  description?: string;
  /** Момент создания файла (DTSTAMP). */
  now?: Date;
  /** Серия: правило без префикса, например `FREQ=WEEKLY;COUNT=8`. */
  rrule?: string;
  /** Исключённые из серии начала занятий (ISO). */
  exdates?: string[];
  /** Замена одного занятия серии: исходное начало этого занятия (ISO). */
  recurrenceId?: string;
  /** CANCELLED — отменённое событие (например, замена, которую потом отменили). */
  status?: 'CONFIRMED' | 'CANCELLED';
}

/** 2026-10-15T07:00:00Z → 20261015T070000Z (всегда UTC). */
export function icsUtc(date: Date): string {
  return date.toISOString().replace(/\.\d{3}Z$/, 'Z').replace(/[-:]/g, '');
}

/** Экранирование TEXT по RFC 5545: \ ; , и переводы строк. */
export function icsEscape(text: string): string {
  return text
    .replace(/\\/g, '\\\\')
    .replace(/;/g, '\\;')
    .replace(/,/g, '\\,')
    .replace(/\r\n|\r|\n/g, '\\n');
}

/** Перенос строк длиннее 75 октетов (UTF-8), не разрывая символы; продолжение — с пробела. */
export function foldIcsLine(line: string): string {
  const encoder = new TextEncoder();
  const out: string[] = [];
  let current = '';
  let bytes = 0;
  for (const ch of line) {
    const size = encoder.encode(ch).length;
    if (bytes + size > 75) {
      out.push(current);
      current = ' ';
      bytes = 1;
    }
    current += ch;
    bytes += size;
  }
  out.push(current);
  return out.join('\r\n');
}

function utcOf(iso: string): string {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) throw new Error('Неверное время встречи');
  return icsUtc(d);
}

function eventLines(event: IcsEvent): string[] {
  const start = utcOf(event.start);
  const end = utcOf(event.end);
  const exdates = [...new Set((event.exdates ?? []).map(utcOf))].sort();
  return [
    'BEGIN:VEVENT',
    `UID:${icsEscape(event.uid)}`,
    `DTSTAMP:${icsUtc(event.now ?? new Date())}`,
    ...(event.recurrenceId ? [`RECURRENCE-ID:${utcOf(event.recurrenceId)}`] : []),
    `DTSTART:${start}`,
    `DTEND:${end}`,
    ...(event.rrule ? [`RRULE:${event.rrule}`] : []),
    ...(exdates.length ? [`EXDATE:${exdates.join(',')}`] : []),
    `SUMMARY:${icsEscape(event.summary)}`,
    ...(event.description ? [`DESCRIPTION:${icsEscape(event.description)}`] : []),
    ...(event.status ? [`STATUS:${event.status}`] : []),
    'END:VEVENT',
  ];
}

/** Файл календаря с одним или несколькими событиями; строки через CRLF, время — в UTC. */
export function buildIcs(events: IcsEvent | IcsEvent[]): string {
  const list = Array.isArray(events) ? events : [events];
  if (!list.length) throw new Error('Нет событий для календаря');
  const lines = ['BEGIN:VCALENDAR', 'VERSION:2.0', 'PRODID:-//Aura//Aura//RU', 'CALSCALE:GREGORIAN', 'METHOD:PUBLISH', ...list.flatMap(eventLines), 'END:VCALENDAR'];
  return lines.map(foldIcsLine).join('\r\n') + '\r\n';
}

/**
  Стабильный UID без секретов (токены в файл не попадают): FNV-1a от частей,
  например темы, владельца и времени. `prefix-<начало UTC>-<хэш>@aura`.
*/
export function stableUid(prefix: string, start: string, ...parts: string[]): string {
  let hash = 0x811c9dc5;
  for (const ch of [...parts, start].join('\u0000')) {
    hash ^= ch.codePointAt(0) ?? 0;
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  const at = Number.isNaN(Date.parse(start)) ? 'x' : icsUtc(new Date(start));
  return `${prefix}-${at}-${hash.toString(16).padStart(8, '0')}@aura`;
}

/** `aura-<prefix>-ГГГГММДД.ics` по началу события. */
export function icsFileNameFor(prefix: string, start: string): string {
  const d = new Date(start);
  return Number.isNaN(d.getTime()) ? `aura-${prefix}.ics` : `aura-${prefix}-${icsUtc(d).slice(0, 8)}.ics`;
}
