import { ArrowLeft, Check, Clock, MapPin, Plus, Users, Utensils, Wallet } from 'lucide-react';
import { useMemo, useState } from 'react';
import { Link, useNavigate, useParams } from 'react-router-dom';
import { Avatar } from '../components/ui';
import { DIET_LABEL, makeMe } from '../data/mock';
import { WINDOW_LABEL } from '../lib/intent';
import { dateLabel, dayTitle, hhmm, rub } from '../lib/time';
import type { Contact, Negotiation as N, NegotiationStatus, Person, Window } from '../lib/types';
import { tap } from '../platform/native';
import { useAura } from '../state/store';

const STATUS: Record<NegotiationStatus, { label: string; tone: string }> = {
  running: { label: 'Идут', tone: '' },
  awaiting: { label: 'Ждёт решения', tone: '' },
  booked: { label: 'Забронировано', tone: 'green' },
  stopped: { label: 'Остановлены', tone: 'gray' },
  failed: { label: 'Не договорились', tone: 'red' },
};

const km = (n: number) => `${n.toLocaleString('ru-RU')} км`;

export function NegotiationView() {
  const { id } = useParams();
  const { state, contacts, approve, stop, startNegotiation } = useAura();
  const navigate = useNavigate();
  const n = state.negotiations.find((x) => x.id === id);
  if (!n) {
    return (
      <div className="page">
        <div className="empty">
          Переговоры не найдены. <Link to="/negotiations">К списку</Link>
        </div>
      </div>
    );
  }
  const c = contacts.find((x) => x.id === n.request.contactId)!;
  const me = makeMe(state.user!.name);
  const shown = n.steps.slice(0, n.revealed);
  const st = STATUS[n.status];
  const start = n.outcome ? new Date(n.outcome.start) : null;

  return (
    <div className="page">
      <div className="page-head">
        <div style={{ display: 'flex', alignItems: 'center', gap: 18, flexWrap: 'wrap' }}>
          <Link to="/negotiations" className="icon-btn" aria-label="Назад">
            <ArrowLeft size={18} />
          </Link>
          <h1 style={{ fontSize: 26 }}>Организация встречи</h1>
          <span className={`badge ${st.tone}`}>
            {n.status === 'running' && <i className="dot violet" style={{ width: 6, height: 6 }} />}
            {st.label}
          </span>
        </div>
        <span className="rounds">
          Раунд {Math.max(1, Math.min(n.revealed, n.steps.length))} из {n.steps.length}
        </span>
      </div>

      <div className="progress" style={{ marginBottom: 30 }}>
        <i style={{ width: `${(n.revealed / n.steps.length) * 100}%` }} />
      </div>

      <div className="neg-stage">
        <AgentCard person={me} title={state.user!.name.split(' ')[0]} hue={280} active={n.status === 'running' && shown.at(-1)?.actor !== 'me'} side="me" />

        <div className="steps" aria-live="polite">
          <div className="faint small" style={{ textAlign: 'center' }}>
            Тема: <span className="muted">{n.request.topic}</span> · {WINDOW_LABEL[n.request.window]} · {n.request.durationMin} мин
          </div>
          {shown.map((s) => (
            <div key={s.round + s.kind} className={`step ${s.actor} ${s.kind === 'fail' ? 'fail' : ''}`}>
              <span className="num">{s.kind === 'accept' ? <Check size={16} /> : s.round}</span>
              <span className="msg">
                {s.text}
                <small>{s.detail}</small>
              </span>
            </div>
          ))}
          {n.status === 'running' && (
            <div className={`step pending ${n.steps[n.revealed]?.actor ?? 'me'}`}>
              <span className="num faint">{n.revealed + 1}</span>
              <span className="msg thinking" style={{ alignSelf: 'auto' }}>
                {n.steps[n.revealed]?.actor === 'them' ? `Аура ${c.nameGen}` : `Аура ${me.nameGen}`} отвечает <i /> <i /> <i />
              </span>
            </div>
          )}
        </div>

        <AgentCard person={c} title={c.name} hue={c.hue} active={n.status === 'running' && shown.at(-1)?.actor === 'me'} side="them" />
      </div>

      {n.outcome && start && n.revealed >= n.steps.length && (
        <div className="result" id="result">
          <div style={{ flex: '1 1 260px' }}>
            <h2>
              {dayTitle(start)}, {hhmm(start)}
              <br />
              {n.outcome.venueName}
            </h2>
            <div className="muted small" style={{ display: 'flex', gap: 18, marginTop: 12, flexWrap: 'wrap' }}>
              <span style={{ display: 'inline-flex', gap: 6, alignItems: 'center' }}>
                <Users size={15} /> Двое
              </span>
              <span style={{ display: 'inline-flex', gap: 6, alignItems: 'center' }}>
                <Wallet size={15} /> Примерно {rub(n.outcome.cost)}
              </span>
              <span style={{ display: 'inline-flex', gap: 6, alignItems: 'center' }}>
                <MapPin size={15} /> {km(n.outcome.distanceMe)} от вас · {km(n.outcome.distanceThem)} от {c.nameGen}
              </span>
              <span style={{ display: 'inline-flex', gap: 6, alignItems: 'center' }}>
                <Clock size={15} /> {dateLabel(start)}, до {hhmm(new Date(n.outcome.end))}
              </span>
            </div>
          </div>
          {n.status === 'awaiting' && (
            <div style={{ display: 'flex', gap: 14, alignItems: 'center', flexWrap: 'wrap' }}>
              <button className="btn btn-primary btn-pill" style={{ height: 52, padding: '0 34px' }} onClick={() => {
                  tap('success');
                  approve(n.id);
                }}>
                Подтвердить и забронировать
              </button>
              <button className="btn btn-text" onClick={() => stop(n.id)}>
                Остановить
              </button>
            </div>
          )}
          {n.status === 'booked' && (
            <div style={{ display: 'flex', gap: 12, alignItems: 'center', flexWrap: 'wrap' }}>
              <span className="badge green">
                <Check size={14} /> Столик забронирован
              </span>
              <Link to="/tasks" className="btn btn-ghost btn-pill">
                В задачах
              </Link>
            </div>
          )}
          {n.status === 'stopped' && <span className="badge gray">Вы остановили переговоры</span>}
        </div>
      )}

      {n.status === 'running' && (
        <div style={{ marginTop: 26, textAlign: 'right' }}>
          <button className="btn btn-text" onClick={() => stop(n.id)}>
            Остановить переговоры
          </button>
        </div>
      )}

      {n.status === 'failed' && <Retry n={n} contact={c} onStart={(route) => navigate(route)} startNegotiation={startNegotiation} />}
    </div>
  );
}

function Retry({ n, contact, onStart, startNegotiation }: { n: N; contact: Contact; onStart: (r: string) => void; startNegotiation: ReturnType<typeof useAura>['startNegotiation'] }) {
  const [win, setWin] = useState<Window>('week');
  const [budget, setBudget] = useState<number | null>(n.request.budget === null ? null : Math.max(n.request.budget, 2000));
  return (
    <div className="result" style={{ borderColor: 'rgba(248,113,113,.4)' }}>
      <div style={{ flex: '1 1 260px' }}>
        <h2 style={{ fontSize: 22 }}>Не договорились</h2>
        <p className="muted small" style={{ margin: '8px 0 0' }}>
          {n.failure}. Измените условия — Аура попробует ещё раз с Аурой {contact.nameGen}.
        </p>
      </div>
      <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap', alignItems: 'center' }}>
        {(['tomorrow', 'weekend', 'week'] as Window[]).map((w) => (
          <button key={w} className={`chip ${w === win ? 'active' : ''}`} onClick={() => setWin(w)}>
            {WINDOW_LABEL[w]}
          </button>
        ))}
        <button className={`chip ${budget === null ? 'active' : ''}`} onClick={() => setBudget(budget === null ? 2000 : null)}>
          {budget === null ? 'Без лимита' : `до ${rub(budget)}`}
        </button>
        <button className="btn btn-primary btn-pill" onClick={() => onStart(startNegotiation({ ...n.request, window: win, budget }))}>
          Повторить
        </button>
      </div>
    </div>
  );
}

function AgentCard({ person, title, hue, active, side }: { person: Person; title: string; hue: number; active: boolean; side: 'me' | 'them' }) {
  // A deterministic "voice print" per person; it pulses while that agent is talking.
  const bars = useMemo(() => Array.from({ length: 28 }, (_, i) => 25 + Math.abs(Math.sin(i * 1.7 + hue)) * 65), [hue]);
  const diet = person.diet.map((d) => DIET_LABEL[d]).join(', ') || 'без ограничений';
  return (
    <div className={`agent-card ${side}`}>
      <Avatar name={title} hue={hue} size={78} ring />
      <div style={{ fontSize: 22, fontWeight: 700, marginTop: 10 }}>{title}</div>
      <div className="muted small">Аура {person.nameGen}</div>
      <div className="wave" aria-hidden="true">
        {bars.map((h, i) => (
          <i key={i} style={{ height: `${active ? h : h * 0.35}%`, animation: active ? `blink ${0.6 + (i % 5) * 0.15}s infinite` : undefined }} />
        ))}
      </div>
      <div className="facts">
        <div>
          <Utensils size={13} /> {diet}
        </div>
        <div>
          <Clock size={13} /> Делится только свободными окнами
        </div>
      </div>
    </div>
  );
}

export function Negotiations() {
  const { state, contacts } = useAura();
  const list = state.negotiations;
  return (
    <div className="page">
      <div className="page-head">
        <h1 className="page-title">Переговоры</h1>
        <Link to="/contacts" className="btn btn-primary btn-pill">
          <Plus size={18} /> Новые
        </Link>
      </div>
      {list.length === 0 ? (
        <div className="empty tile">
          <p style={{ marginTop: 0 }}>Пока ни одних переговоров.</p>
          <p className="small">
            Скажите Ауре на главной: «Хочу встретиться с Димой в эти выходные и обсудить стартап» — или выберите человека в контактах.
          </p>
          <Link to="/" className="btn btn-ghost btn-pill" style={{ marginTop: 8 }}>
            На главную
          </Link>
        </div>
      ) : (
        <div className="stack" style={{ gap: 12 }}>
          {list.map((n) => {
            const c = contacts.find((x) => x.id === n.request.contactId)!;
            const st = STATUS[n.status];
            const start = n.outcome ? new Date(n.outcome.start) : null;
            return (
              <Link key={n.id} to={`/negotiations/${n.id}`} className="tile neg-mini" style={{ padding: '16px 20px' }}>
                <Avatar name={c.fullName} hue={c.hue} size={44} />
                <div style={{ flex: 1, minWidth: 0 }}>
                  <div style={{ fontWeight: 700 }}>
                    {c.fullName} · {n.request.topic}
                  </div>
                  <div className="faint small">
                    {start && n.revealed >= n.steps.length ? `${dayTitle(start)}, ${hhmm(start)} · ${n.outcome!.venueName} · ${rub(n.outcome!.cost)}` : n.failure && n.status === 'failed' ? n.failure : `Раунд ${Math.max(1, n.revealed)} из ${n.steps.length}`}
                  </div>
                </div>
                <span className={`badge ${st.tone}`}>{st.label}</span>
              </Link>
            );
          })}
        </div>
      )}
    </div>
  );
}
