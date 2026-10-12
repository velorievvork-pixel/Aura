import { createContext, useCallback, useContext, useEffect, useMemo, useReducer, type ReactNode } from 'react';
import { CONTACTS, VENUES, makeMe } from '../data/mock';
import { WINDOW_LABEL, parseIntent } from '../lib/intent';
import { negotiate } from '../lib/negotiation';
import { addDays, dayPhrase, hhmm, rub } from '../lib/time';
import type { Activity, ChatMessage, Contact, MeetingRequest, Negotiation, Task, User } from '../lib/types';

interface State {
  user: User | null;
  tasks: Task[];
  activity: Activity[];
  negotiations: Negotiation[];
  chat: ChatMessage[];
  /** A meeting request waiting for the user to pick a budget in chat. */
  pending: MeetingRequest | null;
  favorites: string[];
  invited: string[];
}

type Action =
  | { type: 'login'; user: User }
  | { type: 'logout' }
  | { type: 'chat'; messages: ChatMessage[] }
  | { type: 'pending'; request: MeetingRequest | null }
  | { type: 'negotiation'; negotiation: Negotiation }
  | { type: 'reveal'; id: string }
  | { type: 'status'; id: string; status: Negotiation['status'] }
  | { type: 'task'; task: Task }
  | { type: 'toggleTask'; id: string }
  | { type: 'activity'; verb: string; object: string }
  | { type: 'favorite'; id: string }
  | { type: 'invite'; id: string };

const STORAGE_KEY = 'aura.state.v1';

let seq = 0;
export const uid = (p: string) => `${p}-${Date.now().toString(36)}-${(seq++).toString(36)}`;

function seed(user: User | null): State {
  const now = new Date();
  const at = (minAgo: number) => new Date(now.getTime() - minAgo * 60_000).toISOString();
  const day = (n: number, h: number) => {
    const d = addDays(now, n);
    d.setHours(h, 0, 0, 0);
    return d.toISOString();
  };
  return {
    user,
    tasks: [
      { id: 't1', title: 'Подготовить презентацию к ревью Q3', due: day(0, 20), state: 'today', tone: 'green' },
      { id: 't2', title: 'Ответить на письмо инвестора', due: day(0, 21), state: 'today', tone: 'amber' },
      { id: 't3', title: 'Разослать приглашения на демо-день', due: day(2, 12), state: 'upcoming', tone: 'violet' },
      { id: 't4', title: 'Посмотреть метрики маркетинговой кампании', due: day(3, 15), state: 'upcoming', tone: 'violet' },
      { id: 't5', title: 'Ревью дизайна питч-дека', due: day(-1, 16), state: 'done', tone: 'green' },
    ],
    activity: [
      { id: 'a1', at: at(4), verb: 'начала задачу', object: 'Черновик маркетингового отчёта Q3' },
      { id: 'a2', at: at(9), verb: 'отправила напоминание', object: 'Планёрка в 9:00' },
      { id: 'a3', at: at(18), verb: 'назначила встречу', object: 'Звонок с клиентом в 10:00' },
      { id: 'a4', at: at(33), verb: 'проанализировала данные', object: 'Отчёт по продажам' },
    ],
    negotiations: [],
    chat: [],
    pending: null,
    favorites: CONTACTS.filter((c) => c.favorite).map((c) => c.id),
    invited: [],
  };
}

function load(): State {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw) return { ...seed(null), ...JSON.parse(raw) };
  } catch {
    /* storage unavailable — start fresh */
  }
  return seed(null);
}

function reducer(s: State, a: Action): State {
  switch (a.type) {
    case 'login':
      return { ...seed(a.user), chat: [welcome(a.user)] };
    case 'logout':
      return seed(null);
    case 'chat':
      return { ...s, chat: [...s.chat, ...a.messages] };
    case 'pending':
      return { ...s, pending: a.request };
    case 'negotiation':
      return { ...s, negotiations: [a.negotiation, ...s.negotiations] };
    case 'reveal':
      return {
        ...s,
        negotiations: s.negotiations.map((n) => {
          if (n.id !== a.id) return n;
          const revealed = n.revealed + 1;
          const done = revealed >= n.steps.length;
          return { ...n, revealed, status: done ? (n.outcome ? 'awaiting' : 'failed') : n.status };
        }),
      };
    case 'status':
      return { ...s, negotiations: s.negotiations.map((n) => (n.id === a.id ? { ...n, status: a.status } : n)) };
    case 'task':
      return { ...s, tasks: [a.task, ...s.tasks] };
    case 'toggleTask':
      return {
        ...s,
        tasks: s.tasks.map((t) => (t.id === a.id ? { ...t, state: t.state === 'done' ? (isToday(t.due) ? 'today' : 'upcoming') : 'done' } : t)),
      };
    case 'activity':
      return { ...s, activity: [{ id: uid('a'), at: new Date().toISOString(), verb: a.verb, object: a.object }, ...s.activity].slice(0, 30) };
    case 'favorite':
      return { ...s, favorites: s.favorites.includes(a.id) ? s.favorites.filter((f) => f !== a.id) : [...s.favorites, a.id] };
    case 'invite':
      return { ...s, invited: [...s.invited, a.id] };
  }
}

function isToday(iso: string) {
  const d = new Date(iso);
  const n = new Date();
  return d.toDateString() === n.toDateString();
}

const auraMsg = (text: string, extra: Partial<ChatMessage> = {}): ChatMessage => ({ id: uid('m'), from: 'aura', text, at: new Date().toISOString(), ...extra });
const userMsg = (text: string): ChatMessage => ({ id: uid('m'), from: 'user', text, at: new Date().toISOString() });

function welcome(user: User): ChatMessage {
  const name = user.name.split(' ')[0];
  return auraMsg(`Привет, ${name}! Я ваша Аура. Скажите, что нужно сделать — например, «Хочу встретиться с Димой в эти выходные и обсудить стартап», — и я договорюсь с Аурами других людей.`);
}

interface Api {
  state: State;
  contacts: Contact[];
  login: (user: User) => void;
  logout: () => void;
  /** Handles a free-form command. Returns a route to open, if the command started something. */
  command: (text: string) => string | null;
  choose: (action: string) => string | null;
  startNegotiation: (req: MeetingRequest) => string;
  approve: (id: string) => void;
  stop: (id: string) => void;
  addTask: (title: string, due?: Date) => void;
  toggleTask: (id: string) => void;
  toggleFavorite: (id: string) => void;
  invite: (id: string) => void;
}

const Ctx = createContext<Api | null>(null);

export function StoreProvider({ children }: { children: ReactNode }) {
  const [state, dispatch] = useReducer(reducer, undefined, load);

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
    } catch {
      /* ignore */
    }
  }, [state]);

  // Play running negotiations step by step, as the agents exchange messages.
  const running = state.negotiations.find((n) => n.status === 'running');
  useEffect(() => {
    if (!running) return;
    const t = setTimeout(() => dispatch({ type: 'reveal', id: running.id }), running.revealed === 0 ? 700 : 1300);
    return () => clearTimeout(t);
  }, [running?.id, running?.revealed]); // eslint-disable-line react-hooks/exhaustive-deps

  // When a negotiation finishes, report back in the chat.
  const finished = state.negotiations.filter((n) => n.status === 'awaiting' || n.status === 'failed');
  const reported = useMemo(() => new Set(state.chat.map((m) => m.link?.to)), [state.chat]);
  useEffect(() => {
    for (const n of finished) {
      const to = `/negotiations/${n.id}`;
      if (reported.has(to + '#result')) continue;
      const c = CONTACTS.find((x) => x.id === n.request.contactId)!;
      const text = n.outcome
        ? `Договорилась с Аурой ${c.nameGen}: ${dayPhrase(new Date(n.outcome.start), new Date())} в ${hhmm(new Date(n.outcome.start))}, ${n.outcome.venueName}, около ${rub(n.outcome.cost)} на двоих. Подтвердите — и я забронирую столик.`
        : `Не получилось договориться с Аурой ${c.nameGen}: ${n.failure}.`;
      dispatch({ type: 'chat', messages: [auraMsg(text, { link: { to: to + '#result', label: n.outcome ? 'Открыть и подтвердить' : 'Подробнее' } })] });
      dispatch({ type: 'activity', verb: n.outcome ? 'договорилась о встрече' : 'не смогла договориться', object: `${c.fullName}` });
    }
  }, [finished.map((n) => n.id).join(), reported]); // eslint-disable-line react-hooks/exhaustive-deps

  const contacts = CONTACTS;

  const startNegotiation = useCallback(
    (req: MeetingRequest) => {
      const c = CONTACTS.find((x) => x.id === req.contactId)!;
      const me = makeMe(state.user?.name ?? 'Вы');
      const result = negotiate(req, me, c, VENUES, new Date());
      const n: Negotiation = { id: uid('n'), request: req, createdAt: new Date().toISOString(), revealed: 0, status: 'running', ...result };
      dispatch({ type: 'negotiation', negotiation: n });
      dispatch({ type: 'pending', request: null });
      dispatch({ type: 'activity', verb: 'начала переговоры', object: `${c.fullName} · ${req.topic}` });
      return `/negotiations/${n.id}`;
    },
    [state.user],
  );

  const command = useCallback(
    (text: string): string | null => {
      const intent = parseIntent(text, CONTACTS);
      const out: ChatMessage[] = [userMsg(text)];
      let route: string | null = null;

      switch (intent.kind) {
        case 'meet': {
          const c = CONTACTS.find((x) => x.id === intent.contactId);
          if (!c) {
            out.push(auraMsg('С кем встречаемся? Выберите человека:', { choices: CONTACTS.filter((x) => x.hasAura).map((x) => ({ label: x.fullName, action: `meet:${x.id}:${intent.window}` })) }));
            break;
          }
          if (!c.hasAura) {
            out.push(auraMsg(`У ${c.nameGen} пока нет Ауры, поэтому договориться автоматически не получится. Отправить приглашение?`, { choices: [{ label: 'Пригласить в Aura', action: `invite:${c.id}` }] }));
            break;
          }
          const req: MeetingRequest = { contactId: c.id, window: intent.window, durationMin: intent.durationMin, budget: intent.budget, topic: intent.topic };
          if (intent.budget === null) {
            dispatch({ type: 'pending', request: req });
            out.push(
              auraMsg(`Я проверю ваши расписания и свяжусь с Аурой ${c.nameGen} — ${WINDOW_LABEL[intent.window]}. Бюджет до 1000 ₽?`, {
                choices: [
                  { label: 'Да, утвердить', action: 'budget:1000' },
                  { label: 'Без ограничений', action: 'budget:none' },
                  { label: 'Изменить условия', action: 'budget:edit' },
                ],
              }),
            );
          } else {
            route = startNegotiation(req);
            out.push(auraMsg(`Связываюсь с Аурой ${c.nameGen}: ${WINDOW_LABEL[intent.window]}, бюджет до ${rub(intent.budget)}.`, { link: { to: route, label: 'Смотреть переговоры' } }));
          }
          break;
        }
        case 'reminder': {
          const due = new Date(Date.now() + 60 * 60_000);
          dispatch({ type: 'task', task: { id: uid('t'), title: intent.text[0].toUpperCase() + intent.text.slice(1), due: due.toISOString(), state: 'today', tone: 'amber' } });
          dispatch({ type: 'activity', verb: 'создала напоминание', object: intent.text });
          out.push(auraMsg(`Готово, напомню в ${hhmm(due)}: «${intent.text}».`, { link: { to: '/tasks', label: 'Задачи' } }));
          break;
        }
        case 'schedule': {
          const today = state.tasks.filter((t) => t.state === 'today');
          out.push(auraMsg(today.length ? `Сегодня у вас ${today.length} ${plural(today.length, 'задача', 'задачи', 'задач')}: ${today.map((t) => `«${t.title}» в ${hhmm(new Date(t.due))}`).join(', ')}.` : 'На сегодня задач нет — свободный вечер.', { link: { to: '/tasks', label: 'Открыть задачи' } }));
          break;
        }
        case 'email': {
          const c = CONTACTS.find((x) => x.id === intent.contactId);
          dispatch({ type: 'activity', verb: 'подготовила черновик письма', object: c ? c.fullName : 'Новое письмо' });
          out.push(auraMsg(`Подготовила черновик письма${c ? ` для ${c.nameGen}` : ''}. Отправлю после вашего подтверждения.`, { choices: [{ label: 'Отправить', action: 'email:send' }, { label: 'Не отправлять', action: 'email:cancel' }] }));
          break;
        }
        default:
          out.push(auraMsg('Пока я умею договариваться о встречах, ставить напоминания, рассказывать о расписании и готовить письма. Попробуйте: «Кофе с Марией завтра до 1500 ₽».'));
      }
      dispatch({ type: 'chat', messages: out });
      return route;
    },
    [startNegotiation, state.tasks],
  );

  const choose = useCallback(
    (action: string): string | null => {
      const [kind, arg, extra] = action.split(':');
      if (kind === 'budget' && state.pending) {
        if (arg === 'edit') {
          dispatch({ type: 'chat', messages: [userMsg('Изменить условия'), auraMsg('Напишите новые условия, например: «до 2000 ₽, в воскресенье».')] });
          return null;
        }
        const req = { ...state.pending, budget: arg === 'none' ? null : Number(arg) };
        const route = startNegotiation(req);
        dispatch({ type: 'chat', messages: [userMsg(arg === 'none' ? 'Без ограничений' : 'Да, утвердить'), auraMsg('Начала переговоры. Можно наблюдать в реальном времени.', { link: { to: route, label: 'Смотреть переговоры' } })] });
        return route;
      }
      if (kind === 'meet') {
        const c = CONTACTS.find((x) => x.id === arg)!;
        return command(`Встретиться с ${c.nameIns} ${extra === 'weekend' ? 'в выходные' : extra === 'tomorrow' ? 'завтра' : extra === 'today' ? 'сегодня' : 'на неделе'}`);
      }
      if (kind === 'invite') {
        dispatch({ type: 'invite', id: arg });
        dispatch({ type: 'chat', messages: [auraMsg('Приглашение отправлено.')] });
        return null;
      }
      if (kind === 'email') {
        dispatch({ type: 'chat', messages: [userMsg(arg === 'send' ? 'Отправить' : 'Не отправлять'), auraMsg(arg === 'send' ? 'Письмо отправлено.' : 'Хорошо, черновик сохранён.')] });
        if (arg === 'send') dispatch({ type: 'activity', verb: 'отправила письмо', object: 'по вашему подтверждению' });
      }
      return null;
    },
    [state.pending, startNegotiation, command],
  );

  const api: Api = {
    state,
    contacts,
    login: (user) => dispatch({ type: 'login', user }),
    logout: () => dispatch({ type: 'logout' }),
    command,
    choose,
    startNegotiation,
    approve: (id) => {
      const n = state.negotiations.find((x) => x.id === id);
      if (!n?.outcome) return;
      const c = CONTACTS.find((x) => x.id === n.request.contactId)!;
      const start = new Date(n.outcome.start);
      dispatch({ type: 'status', id, status: 'booked' });
      dispatch({ type: 'task', task: { id: uid('t'), title: `Встреча с ${c.nameIns} в ${n.outcome.venueName}`, due: n.outcome.start, state: isToday(n.outcome.start) ? 'today' : 'upcoming', tone: 'violet', negotiationId: id } });
      dispatch({ type: 'activity', verb: 'забронировала столик', object: `${n.outcome.venueName}, ${dayPhrase(start, new Date())} ${hhmm(start)}` });
      dispatch({ type: 'chat', messages: [auraMsg(`Забронировала ${n.outcome.venueName} на ${hhmm(start)} и добавила встречу в задачи. Аура ${c.nameGen} получила подтверждение.`)] });
    },
    stop: (id) => {
      dispatch({ type: 'status', id, status: 'stopped' });
      dispatch({ type: 'activity', verb: 'остановила переговоры', object: 'по вашему решению' });
    },
    addTask: (title, due) => {
      const d = due ?? new Date(Date.now() + 2 * 3600_000);
      dispatch({ type: 'task', task: { id: uid('t'), title, due: d.toISOString(), state: isToday(d.toISOString()) ? 'today' : 'upcoming', tone: 'violet' } });
    },
    toggleTask: (id) => dispatch({ type: 'toggleTask', id }),
    toggleFavorite: (id) => dispatch({ type: 'favorite', id }),
    invite: (id) => {
      dispatch({ type: 'invite', id });
      dispatch({ type: 'activity', verb: 'отправила приглашение в Aura', object: CONTACTS.find((c) => c.id === id)?.fullName ?? '' });
    },
  };

  return <Ctx.Provider value={api}>{children}</Ctx.Provider>;
}

export function useAura(): Api {
  const v = useContext(Ctx);
  if (!v) throw new Error('useAura outside StoreProvider');
  return v;
}

export function plural(n: number, one: string, few: string, many: string): string {
  const m10 = n % 10;
  const m100 = n % 100;
  if (m10 === 1 && m100 !== 11) return one;
  if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
  return many;
}
