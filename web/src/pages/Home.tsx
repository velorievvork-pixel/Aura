import { Bell, Check, ChevronRight, Handshake, ShieldAlert } from 'lucide-react';
import { useCallback } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { CommandBar } from '../components/CommandBar';
import { Avatar, Empty } from '../components/ui';
import { firstName, shortWhen, toolTitle } from '../lib/format';
import { selfvoyOf } from '../lib/negotiationSteps';
import { greeting } from '../lib/time';
import { useData } from '../state/data';
import { useSession } from '../state/session';

const QUICK = ['Что у меня сегодня?', 'Напомни завтра в 10 позвонить юристу', 'Хочу встретиться с Димой в выходные'];

export function Home() {
  const { user } = useSession();
  const { tasks, confirmations, negotiations, unread, toggleTask, ask, thinking } = useData();
  const navigate = useNavigate();

  const run = useCallback(
    (text: string) => {
      navigate('/selfvoy');
      ask(text);
    },
    [navigate, ask],
  );

  const upcoming = tasks
    .filter((t) => t.status === 'pending')
    .sort((a, b) => (a.due_at || a.remind_at || '9').localeCompare(b.due_at || b.remind_at || '9'))
    .slice(0, 5);
  const date = new Date().toLocaleDateString('ru-RU', { weekday: 'long', day: 'numeric', month: 'long' });
  const today = date.charAt(0).toUpperCase() + date.slice(1); // «Вторник, 13 октября»

  return (
    <div className="page">
      <header className="page-head">
        <div>
          <h1 className="title-xl">
            {greeting(new Date())}, {firstName(user?.display_name ?? '')}
          </h1>
          <div className="sub">{today}</div>
        </div>
        <div className="row hide-desktop">
          <Link to="/inbox" className="icon-btn" aria-label="Входящие" style={{ position: 'relative' }}>
            <Bell size={18} />
            {unread + confirmations.length > 0 && <span className="dot accent" style={{ position: 'absolute', top: 8, right: 8 }} />}
          </Link>
          <Link to="/settings" aria-label="Настройки">
            <Avatar name={user?.display_name || user?.email || ''} src={user?.avatar_url} size={32} />
          </Link>
        </div>
      </header>

      <div className="stack-lg">
        <div className="stack">
          <CommandBar onSubmit={run} busy={thinking} />
          <div className="suggestions">
            <button className="chip" onClick={() => navigate('/negotiations?new=1')}>
              <Handshake size={14} /> Договориться о встрече
            </button>
            {QUICK.map((q) => (
              <button key={q} className="chip" onClick={() => run(q)}>
                {q}
              </button>
            ))}
          </div>
        </div>

        {confirmations.length > 0 && (
          <Link to="/inbox" className="card row" style={{ gap: 12 }}>
            <ShieldAlert size={18} style={{ color: 'var(--warning)' }} />
            <div style={{ flex: 1, minWidth: 0 }}>
              <div style={{ fontWeight: 500 }}>Ждут вашего решения: {confirmations.length}</div>
              <div className="small muted ellipsis">
                {toolTitle(confirmations[0].tool)} · {confirmations[0].summary}
              </div>
            </div>
            <ChevronRight size={16} className="faint" />
          </Link>
        )}

        <section>
          <div className="section-title">
            <h2 className="title-sm">Ближайшие задачи</h2>
            <Link to="/tasks" className="link small">
              Все задачи
            </Link>
          </div>
          <div className="card flush list">
            {upcoming.length === 0 && <Empty>Задач нет. Скажите Selfvoy, о чём напомнить.</Empty>}
            {upcoming.map((t) => (
              <div key={t.id} className="list-row">
                <button className="check" onClick={() => toggleTask(t)} aria-label={`Выполнено: ${t.title}`}>
                  <Check size={13} strokeWidth={3} />
                </button>
                <div className="body ellipsis">{t.title}</div>
                <span className="meta">{shortWhen(t.due_at || t.remind_at)}</span>
              </div>
            ))}
          </div>
        </section>

        <section>
          <div className="section-title">
            <h2 className="title-sm">Переговоры</h2>
            <Link to="/negotiations" className="link small">
              Все
            </Link>
          </div>
          <div className="card flush list">
            {negotiations.length === 0 && <Empty>Попросите Selfvoy договориться о встрече — переговоры появятся здесь.</Empty>}
            {negotiations.slice(0, 3).map((n) => (
              <Link key={n.id} to={`/negotiations/${n.id}`} className="list-row">
                <Handshake size={16} className="faint" />
                <div className="body">
                  <div className="ellipsis">{selfvoyOf(n.targetName)}</div>
                  <div className="small muted ellipsis">{n.result.accepted && n.result.slot ? `${shortWhen(n.result.slot.start)}${n.result.place?.name ? ` · ${n.result.place.name}` : ''}` : 'Общего времени не нашлось'}</div>
                </div>
                <NegotiationBadge accepted={n.result.accepted} booked={n.booked} />
              </Link>
            ))}
          </div>
        </section>
      </div>
    </div>
  );
}

export function NegotiationBadge({ accepted, booked }: { accepted: boolean; booked?: boolean }) {
  if (booked) return <span className="badge green">В задачах</span>;
  if (accepted) return <span className="badge accent">Договорились</span>;
  return <span className="badge amber">Нет общего окна</span>;
}
