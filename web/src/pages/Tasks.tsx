import { Check, ChevronLeft, ChevronRight, Plus, X } from 'lucide-react';
import { useEffect, useState, type FormEvent } from 'react';
import { Link } from 'react-router-dom';
import { addDays, dateLabel, dayShort, hhmm, sameDay, startOfDay, weekOf } from '../lib/time';
import type { Task } from '../lib/types';
import { plural, useAura } from '../state/store';

export function Tasks() {
  const { state, toggleTask, addTask } = useAura();
  const [now, setNow] = useState(() => new Date());
  const [day, setDay] = useState(() => new Date());
  const [adding, setAdding] = useState(false);
  const week = weekOf(day);

  useEffect(() => {
    const t = setInterval(() => setNow(new Date()), 30_000);
    return () => clearInterval(t);
  }, []);

  const open = state.tasks.filter((t) => t.state !== 'done').sort((a, b) => a.due.localeCompare(b.due));
  const onDay = open.filter((t) => sameDay(new Date(t.due), day));
  const later = open.filter((t) => new Date(t.due) >= startOfDay(addDays(day, 1)));
  const done = state.tasks.filter((t) => t.state === 'done');
  const isToday = sameDay(day, now);

  const next = open.find((t) => new Date(t.due) > now);
  const minutes = next ? Math.round((new Date(next.due).getTime() - now.getTime()) / 60_000) : null;

  return (
    <div className="page">
      <div className="page-head">
        <h1 className="page-title">Задачи</h1>
        <button className="btn btn-primary btn-pill" onClick={() => setDay(new Date())}>
          Сегодня
        </button>
      </div>

      <div className="tasks-grid">
        <div className="stack" style={{ gap: 30 }}>
          <div className="row-between" style={{ flexWrap: 'wrap', gap: 18 }}>
            <div className="tile" style={{ display: 'inline-flex', alignItems: 'center', gap: 6, padding: 6, borderRadius: 999 }}>
              <button className="icon-btn" style={{ width: 32, height: 32, border: 0 }} onClick={() => setDay(addDays(day, -7))} aria-label="Предыдущая неделя">
                <ChevronLeft size={16} />
              </button>
              <span className="small" style={{ fontWeight: 600, minWidth: 140, textAlign: 'center' }}>
                {dateLabel(week[0])} – {dateLabel(week[6])}
              </span>
              <button className="icon-btn" style={{ width: 32, height: 32, border: 0 }} onClick={() => setDay(addDays(day, 7))} aria-label="Следующая неделя">
                <ChevronRight size={16} />
              </button>
            </div>
          </div>

          <div className="week" role="tablist" aria-label="Дни недели">
            {week.map((d) => {
              const has = open.some((t) => sameDay(new Date(t.due), d));
              return (
                <button key={d.toISOString()} role="tab" aria-selected={sameDay(d, day)} className={sameDay(d, day) ? 'sel' : ''} onClick={() => setDay(d)}>
                  <div className="faint small">{dayShort(d)}</div>
                  <div className={`d ${has ? 'has' : ''}`} style={{ color: sameDay(d, now) ? 'var(--lilac)' : undefined }}>
                    {d.getDate()}
                  </div>
                </button>
              );
            })}
          </div>

          <Section title={isToday ? 'Сегодня' : `${dayShort(day)}, ${dateLabel(day)}`} tasks={onDay} onToggle={toggleTask} empty="Свободный день" />
          <Section title="Дальше" tasks={later} onToggle={toggleTask} empty="Больше ничего не запланировано" />
          <Section title="Выполнено" tasks={done} onToggle={toggleTask} empty="Пока ничего" />
        </div>

        <div className="stack" style={{ alignContent: 'start' }}>
          <div className="glass" style={{ padding: 26, borderRadius: 24 }}>
            <h2 style={{ fontSize: 20 }}>Подсказки Ауры</h2>
            {next && minutes !== null ? (
              <>
                <p style={{ fontWeight: 600, margin: '10px 0 22px' }}>
                  {minutes < 120 ? `У вас ${minutes} ${plural(minutes, 'минута', 'минуты', 'минут')} до «${next.title}»` : `Следующее: «${next.title}» — ${dayShort(new Date(next.due))}, ${hhmm(new Date(next.due))}`}
                </p>
                <div className="progress">
                  <i style={{ width: `${Math.max(6, 100 - Math.min(100, (minutes / 120) * 100))}%` }} />
                </div>
                <div className="faint tiny" style={{ marginTop: 8 }}>
                  Время до следующего события
                </div>
              </>
            ) : (
              <p className="muted">Впереди ничего не запланировано.</p>
            )}
          </div>
          <div className="tile" style={{ padding: 20 }}>
            <div className="small muted">Хотите с кем-то встретиться?</div>
            <Link to="/contacts" className="btn btn-ghost btn-pill" style={{ marginTop: 12 }}>
              Начать переговоры
            </Link>
          </div>
        </div>
      </div>

      <button className="icon-btn accent fab" onClick={() => setAdding(true)} aria-label="Новая задача">
        <Plus size={28} />
      </button>
      {adding && (
        <AddTask
          day={day}
          onClose={() => setAdding(false)}
          onAdd={(title, due) => {
            addTask(title, due);
            setAdding(false);
          }}
        />
      )}
    </div>
  );
}

function Section({ title, tasks, onToggle, empty }: { title: string; tasks: Task[]; onToggle: (id: string) => void; empty: string }) {
  return (
    <section>
      <div className="section-title" style={{ fontSize: 22 }}>
        {title}
      </div>
      <div className="stack" style={{ gap: 12 }}>
        {tasks.length === 0 && <div className="faint small">{empty}</div>}
        {tasks.map((t) => (
          <div key={t.id} className={`task ${t.state === 'done' ? 'done' : ''}`}>
            <button className={`check ${t.state === 'done' ? 'on' : ''}`} onClick={() => onToggle(t.id)} aria-label={t.state === 'done' ? 'Вернуть в работу' : 'Отметить выполненной'}>
              {t.state === 'done' && <Check size={14} />}
            </button>
            <span className="title">{t.negotiationId ? <Link to={`/negotiations/${t.negotiationId}`}>{t.title}</Link> : t.title}</span>
            <span className="faint small">{hhmm(new Date(t.due))}</span>
            <i className={`dot ${t.tone}`} />
          </div>
        ))}
      </div>
    </section>
  );
}

function AddTask({ day, onClose, onAdd }: { day: Date; onClose: () => void; onAdd: (title: string, due: Date) => void }) {
  const [title, setTitle] = useState('');
  const [time, setTime] = useState('18:00');
  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!title.trim()) return;
    const [h, m] = time.split(':').map(Number);
    const due = new Date(day);
    due.setHours(h, m, 0, 0);
    onAdd(title.trim(), due);
  };
  return (
    <div style={{ position: 'fixed', inset: 0, zIndex: 30, display: 'grid', placeItems: 'center', background: 'rgba(5,4,10,.6)', padding: 16 }} onClick={onClose}>
      <form className="glass edge stack" style={{ width: 'min(100%, 420px)', padding: 26 }} onClick={(e) => e.stopPropagation()} onSubmit={submit}>
        <div className="row-between">
          <h2 style={{ fontSize: 20 }}>Новая задача · {dateLabel(day)}</h2>
          <button type="button" className="icon-btn" style={{ width: 32, height: 32 }} onClick={onClose} aria-label="Закрыть">
            <X size={14} />
          </button>
        </div>
        <label className="field">
          <span>Что сделать</span>
          <input className="input" autoFocus value={title} onChange={(e) => setTitle(e.target.value)} placeholder="Например, отправить питч-дек" />
        </label>
        <label className="field">
          <span>Время</span>
          <input className="input" type="time" value={time} onChange={(e) => setTime(e.target.value)} />
        </label>
        <button className="btn btn-primary btn-pill" disabled={!title.trim()}>
          Добавить
        </button>
      </form>
    </div>
  );
}
