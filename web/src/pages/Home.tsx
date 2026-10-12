import { ArrowRight, CalendarClock, FileText, MessageSquare, Mic, Presentation, Send, User } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { Link, useNavigate } from 'react-router-dom';
import { Avatar } from '../components/ui';
import { greeting, hhmm } from '../lib/time';
import { useSpeech } from '../lib/useSpeech';
import { tap } from '../platform/native';
import { useAura, plural } from '../state/store';

const QUICK = [
  { label: 'Назначить встречу', text: 'Хочу встретиться с Димой в эти выходные и обсудить стартап' },
  { label: 'Моё расписание', text: 'Что у меня сегодня в расписании?' },
  { label: 'Написать письмо', text: 'Напиши письмо Кире про демо-день' },
  { label: 'Создать напоминание', text: 'Напомни мне позвонить юристу' },
];

const TASK_ICONS = [User, FileText, Presentation];

export function Home() {
  const { state, command, contacts } = useAura();
  const navigate = useNavigate();
  const [text, setText] = useState('');
  const speech = useSpeech(setText);
  const now = new Date();
  const first = state.user!.name.split(' ')[0];

  const run = (t: string) => {
    if (!t.trim()) return;
    tap();
    const route = command(t.trim());
    setText('');
    navigate(route ?? '/aura');
  };
  const submit = (e: FormEvent) => {
    e.preventDefault();
    run(text);
  };

  const doneToday = state.tasks.filter((t) => t.state === 'done').length + state.negotiations.filter((n) => n.status === 'booked').length;
  const upcoming = state.tasks.filter((t) => t.state !== 'done').sort((a, b) => a.due.localeCompare(b.due)).slice(0, 4);
  const live = state.negotiations.filter((n) => n.status === 'running' || n.status === 'awaiting');

  return (
    <div className="page">
      <div className="page-head" style={{ justifyContent: 'flex-end', marginBottom: 6 }}>
        <span className="status-line">
          <i className="dot green" style={{ width: 7, height: 7 }} /> Все системы работают
        </span>
      </div>

      <div className="home-grid">
        <div className="stack" style={{ gap: 26 }}>
          <h1 className="display">
            {greeting(now)},
            <br />
            {first}
          </h1>

          <form className="command" onSubmit={submit} style={{ marginTop: 34 }}>
            <input value={text} onChange={(e) => setText(e.target.value)} placeholder="Что нужно сделать?" aria-label="Команда для Ауры" />
            {text ? (
              <button className="icon-btn accent" aria-label="Отправить" style={{ width: 48, height: 48 }}>
                <Send size={20} />
              </button>
            ) : (
              <button
                type="button"
                className="icon-btn accent"
                aria-label={speech.listening ? 'Остановить запись' : 'Голосовой ввод'}
                title={speech.supported ? 'Сказать голосом' : 'Голосовой ввод не поддерживается этим браузером'}
                disabled={!speech.supported}
                style={{ width: 48, height: 48, opacity: speech.listening ? 0.7 : 1 }}
                onClick={speech.toggle}
              >
                <Mic size={20} />
              </button>
            )}
          </form>

          <div className="quick">
            {QUICK.map((q) => (
              <button key={q.label} onClick={() => run(q.text)} title={q.text}>
                {q.label}
              </button>
            ))}
          </div>

          <section style={{ marginTop: 10 }}>
            <div className="section-title">
              Ближайшие задачи
              <Link to="/tasks" className="small muted" style={{ fontWeight: 500 }}>
                Все задачи
              </Link>
            </div>
            <div className="list">
              {upcoming.map((t, i) => {
                const Icon = TASK_ICONS[i % TASK_ICONS.length];
                return (
                  <Link key={t.id} to="/tasks" className="row">
                    <span className="row-icon">
                      <Icon size={16} />
                    </span>
                    <span style={{ flex: 1 }}>{t.title}</span>
                    <span className="faint small">{hhmm(new Date(t.due))}</span>
                  </Link>
                );
              })}
            </div>
          </section>
        </div>

        <div className="stack" style={{ gap: 30, alignContent: 'start' }}>
          <div className="active-card">
            <h2 style={{ fontSize: 22, fontWeight: 600 }}>Ваша Аура активна</h2>
            <p className="small" style={{ margin: '4px 0 18px', opacity: 0.85 }}>
              {doneToday} {plural(doneToday, 'задача выполнена', 'задачи выполнены', 'задач выполнено')} сегодня
            </p>
            <div style={{ display: 'flex', gap: 10 }}>
              <Link to="/contacts" className="icon-btn" aria-label="Контакты" style={{ width: 38, height: 38 }}>
                <User size={16} />
              </Link>
              <Link to="/aura" className="icon-btn" aria-label="Чат с Аурой" style={{ width: 38, height: 38 }}>
                <MessageSquare size={16} />
              </Link>
            </div>
          </div>

          {live.length > 0 && (
            <section>
              <div className="section-title">Идут переговоры</div>
              <div className="stack" style={{ gap: 10 }}>
                {live.map((n) => {
                  const c = contacts.find((x) => x.id === n.request.contactId)!;
                  return (
                    <Link key={n.id} to={`/negotiations/${n.id}`} className="tile neg-mini">
                      <Avatar name={c.fullName} hue={c.hue} size={36} />
                      <div style={{ flex: 1, minWidth: 0 }}>
                        <div style={{ fontWeight: 700, fontSize: 14 }}>Аура {c.nameGen}</div>
                        <div className="faint tiny">{n.status === 'running' ? `Раунд ${Math.max(1, n.revealed)} из ${n.steps.length}` : 'Ждёт вашего решения'}</div>
                      </div>
                      {n.status === 'awaiting' ? <span className="badge">Подтвердить</span> : <CalendarClock size={18} className="muted" />}
                    </Link>
                  );
                })}
              </div>
            </section>
          )}

          <section>
            <div className="section-title">Недавние действия Ауры</div>
            <div className="feed">
              {state.activity.slice(0, 7).map((a) => (
                <div key={a.id}>
                  <span className="t">{hhmm(new Date(a.at))}</span> Аура {a.verb}: <b>{a.object}</b>
                </div>
              ))}
            </div>
            <Link to="/aura" className="small muted" style={{ display: 'inline-flex', alignItems: 'center', gap: 6, marginTop: 18 }}>
              Открыть диалог <ArrowRight size={14} />
            </Link>
          </section>
        </div>
      </div>
    </div>
  );
}
