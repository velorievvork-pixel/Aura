import { CalendarDays, Lock, Search, Star, UserPlus } from 'lucide-react';
import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Avatar, PRESENCE } from '../components/ui';
import { DIET_LABEL } from '../data/mock';
import { WINDOW_LABEL } from '../lib/intent';
import { rub } from '../lib/time';
import type { Contact, Window } from '../lib/types';
import { useAura } from '../state/store';

type Tab = 'all' | 'favorites' | 'recent';

export function Contacts() {
  const { state, contacts, toggleFavorite, invite } = useAura();
  const [q, setQ] = useState('');
  const [tab, setTab] = useState<Tab>('all');
  const [selId, setSelId] = useState(contacts[0].id);

  const recent = new Set(state.negotiations.map((n) => n.request.contactId));
  const list = contacts
    .filter((c) => (tab === 'favorites' ? state.favorites.includes(c.id) : tab === 'recent' ? recent.has(c.id) : true))
    .filter((c) => !q || c.fullName.toLowerCase().includes(q.toLowerCase()) || c.role.toLowerCase().includes(q.toLowerCase()));
  const sel = contacts.find((c) => c.id === selId)!;

  return (
    <div className="page" style={{ position: 'relative' }}>
      <span className="bubble-deco" style={{ width: 120, height: 120, right: 300, top: 30 }} />
      <span className="bubble-deco" style={{ width: 26, height: 26, right: 380, bottom: 60 }} />
      <div className="contacts-grid">
        <div>
          <h1 className="page-title" style={{ marginBottom: 18 }}>
            Контакты
          </h1>
          <label className="search">
            <Search size={18} />
            <input value={q} onChange={(e) => setQ(e.target.value)} placeholder="Искать людей или Ауры" aria-label="Поиск контактов" />
          </label>
          <div style={{ display: 'flex', gap: 10, margin: '20px 0 22px' }}>
            {(
              [
                ['all', 'Все'],
                ['favorites', 'Избранные'],
                ['recent', 'Недавние'],
              ] as [Tab, string][]
            ).map(([k, l]) => (
              <button key={k} className={`chip ${tab === k ? 'active' : ''}`} onClick={() => setTab(k)}>
                {l}
              </button>
            ))}
          </div>

          <div className="stack" style={{ gap: 4 }}>
            {list.length === 0 && <div className="faint small">Никого не нашли</div>}
            {list.map((c) => {
              const p = c.hasAura || !state.invited.includes(c.id) ? PRESENCE[c.presence] : { label: 'Приглашён', dot: 'amber' };
              return (
                <button key={c.id} className={`contact-row ${c.id === selId ? 'sel' : ''}`} onClick={() => setSelId(c.id)}>
                  <Avatar name={c.fullName} hue={c.hue} size={46} />
                  <span style={{ fontWeight: 700 }}>{c.fullName}</span>
                  <span className="sep" />
                  <span className="muted small" style={{ flex: 1 }}>
                    {p.label}
                  </span>
                  <i className={`dot ${p.dot}`} />
                </button>
              );
            })}
          </div>

          <button className="btn btn-ghost btn-pill" style={{ marginTop: 36, height: 50, padding: '0 34px' }} onClick={() => setSelId('ildar')}>
            <UserPlus size={18} /> Пригласить в Aura
          </button>
        </div>

        <Profile contact={sel} favorite={state.favorites.includes(sel.id)} invited={state.invited.includes(sel.id)} onFavorite={() => toggleFavorite(sel.id)} onInvite={() => invite(sel.id)} />
      </div>
    </div>
  );
}

function Profile({ contact: c, favorite, invited, onFavorite, onInvite }: { contact: Contact; favorite: boolean; invited: boolean; onFavorite: () => void; onInvite: () => void }) {
  const { startNegotiation } = useAura();
  const navigate = useNavigate();
  const [planning, setPlanning] = useState(false);
  const [win, setWin] = useState<Window>('weekend');
  const [budget, setBudget] = useState<number | null>(1000);
  const [topic, setTopic] = useState('');

  return (
    <aside className="profile" key={c.id}>
      <button className="icon-btn" style={{ position: 'absolute', right: 18, top: 18, width: 36, height: 36, color: favorite ? 'var(--amber)' : undefined }} onClick={onFavorite} aria-pressed={favorite} aria-label="В избранное">
        <Star size={16} fill={favorite ? 'currentColor' : 'none'} />
      </button>
      <div style={{ display: 'grid', justifyItems: 'center', gap: 4, textAlign: 'center' }}>
        <Avatar name={c.fullName} hue={c.hue} size={104} ring />
        <h2 style={{ fontSize: 22, marginTop: 12 }}>{c.fullName}</h2>
        <div className="small" style={{ opacity: 0.8 }}>
          {c.hasAura ? 'Аура доступна' : invited ? 'Приглашение отправлено' : 'Ауры пока нет'} · {c.role}
        </div>
      </div>

      {c.hasAura ? (
        <>
          <div>
            <h4>
              <span className="icon-dot">
                <CalendarDays size={14} />
              </span>
              Общие встречи
            </h4>
            <ul>{c.sharedMeetings.length ? c.sharedMeetings.map((m) => <li key={m}>{m}</li>) : <li className="faint">Пока нет</li>}</ul>
          </div>
          <div>
            <h4>
              <span className="icon-dot">
                <Lock size={14} />
              </span>
              Разрешения для вашей Ауры
            </h4>
            <ul>
              {c.permissions.map((p) => (
                <li key={p}>{p}</li>
              ))}
              {c.diet.length > 0 && <li className="faint">Питание: {c.diet.map((d) => DIET_LABEL[d]).join(', ')}</li>}
            </ul>
          </div>

          {planning ? (
            <form
              className="stack"
              style={{ gap: 12 }}
              onSubmit={(e) => {
                e.preventDefault();
                navigate(startNegotiation({ contactId: c.id, window: win, durationMin: 90, budget, topic: topic.trim() || 'Встреча' }));
              }}
            >
              <input className="input" value={topic} onChange={(e) => setTopic(e.target.value)} placeholder="Тема, например «обсудить стартап»" aria-label="Тема встречи" />
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
                {(['tomorrow', 'weekend', 'week'] as Window[]).map((w) => (
                  <button type="button" key={w} className={`chip ${win === w ? 'active' : ''}`} onClick={() => setWin(w)}>
                    {WINDOW_LABEL[w]}
                  </button>
                ))}
              </div>
              <label className="small">
                <div className="row-between">
                  <span>Бюджет</span>
                  <b>{budget === null ? 'без лимита' : `до ${rub(budget)}`}</b>
                </div>
                <input className="range" type="range" min={500} max={5500} step={500} value={budget ?? 5500} onChange={(e) => setBudget(Number(e.target.value) > 5000 ? null : Number(e.target.value))} />
              </label>
              <button className="btn btn-primary btn-pill">Начать переговоры</button>
            </form>
          ) : (
            <button className="btn btn-ghost btn-pill btn-block" style={{ height: 52, background: 'rgba(0,0,0,.18)' }} onClick={() => setPlanning(true)}>
              Начать переговоры
            </button>
          )}
        </>
      ) : (
        <div className="stack">
          <p className="small" style={{ margin: 0, opacity: 0.85 }}>
            Чтобы ваши Ауры договаривались напрямую, у {c.nameGen} тоже должна быть Аура.
          </p>
          <button className="btn btn-primary btn-pill" disabled={invited} onClick={onInvite}>
            <UserPlus size={18} /> {invited ? 'Приглашение отправлено' : 'Пригласить в Aura'}
          </button>
        </div>
      )}
    </aside>
  );
}
