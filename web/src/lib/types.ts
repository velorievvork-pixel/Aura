export type Diet = 'gluten-free' | 'vegetarian' | 'vegan' | 'halal';

export interface Point {
  x: number;
  y: number;
}

/** Recurring busy block: weekday 0 (Sun) … 6 (Sat), hours in local time. */
export interface BusyRule {
  days: number[];
  from: number;
  to: number;
  label: string;
}

export interface Person {
  id: string;
  name: string;
  /** Genitive form for "Аура Димы", "Аура Алекса". */
  nameGen: string;
  diet: Diet[];
  home: Point;
  busy: BusyRule[];
}

export type Presence = 'active' | 'available' | 'away' | 'offline';

export interface Contact extends Person {
  fullName: string;
  /** Instrumental form for "встреча с Димой". */
  nameIns: string;
  /** Lower-case stems the intent parser matches: "дим" → Дима, Диме, Димой. */
  stems: string[];
  hasAura: boolean;
  presence: Presence;
  hue: number;
  role: string;
  permissions: string[];
  sharedMeetings: string[];
  favorite?: boolean;
}

export interface Venue {
  id: string;
  name: string;
  kind: string;
  location: Point;
  /** Average bill per person, ₽. */
  avgCheck: number;
  tags: Diet[];
  opens: number;
  closes: number;
}

export type Window = 'today' | 'tomorrow' | 'weekend' | 'week';

export interface MeetingRequest {
  contactId: string;
  window: Window;
  durationMin: number;
  /** Total budget for everyone, ₽. null means no limit. */
  budget: number | null;
  topic: string;
}

export type StepKind = 'propose' | 'check' | 'counter' | 'venue' | 'accept' | 'fail';

export interface Step {
  round: number;
  actor: 'me' | 'them';
  kind: StepKind;
  text: string;
  detail: string;
}

export interface Outcome {
  start: string;
  end: string;
  venueId: string;
  venueName: string;
  people: number;
  cost: number;
  distanceMe: number;
  distanceThem: number;
}

export interface NegotiationResult {
  steps: Step[];
  outcome: Outcome | null;
  failure: string | null;
}

export type NegotiationStatus = 'running' | 'awaiting' | 'booked' | 'stopped' | 'failed';

export interface Negotiation extends NegotiationResult {
  id: string;
  request: MeetingRequest;
  createdAt: string;
  revealed: number;
  status: NegotiationStatus;
}

export type TaskState = 'today' | 'upcoming' | 'done';

export interface Task {
  id: string;
  title: string;
  due: string;
  state: TaskState;
  tone: 'violet' | 'green' | 'amber';
  negotiationId?: string;
}

export interface Activity {
  id: string;
  at: string;
  verb: string;
  object: string;
}

export interface ChatChoice {
  label: string;
  action: string;
}

export interface ChatMessage {
  id: string;
  from: 'user' | 'aura';
  text: string;
  at: string;
  choices?: ChatChoice[];
  link?: { to: string; label: string };
}

export interface User {
  name: string;
  email: string;
  timezone: string;
  focus: string;
}
