import { ArrowRight, Brain, HelpCircle, LogOut, MessageCircle, Mic, SendHorizontal, Settings, Shield, X } from 'lucide-react';
import { useEffect, useRef, useState, type FormEvent } from 'react';
import { Link, NavLink, useNavigate } from 'react-router-dom';
import { Orb } from '../components/ui';
import { makeMe } from '../data/mock';
import { useSpeech } from '../lib/useSpeech';
import { tap } from '../platform/native';
import { useAura } from '../state/store';

const SUGGESTIONS = ['Хочу встретиться с Димой в эти выходные и обсудить стартап', 'Кофе с Марией завтра на 1 час до 1500', 'Что у меня сегодня?'];

export function Chat() {
  const { state, command, choose, logout, contacts } = useAura();
  const navigate = useNavigate();
  const [text, setText] = useState('');
  const [thinking, setThinking] = useState(false);
  const [visible, setVisible] = useState(state.chat.length);
  const [memory, setMemory] = useState(false);
  const speech = useSpeech(setText);
  const end = useRef<HTMLDivElement>(null);

  // Hold Aura's reply back for a moment so the answer feels considered, not instant.
  useEffect(() => {
    if (state.chat.length <= visible) return;
    const last = state.chat[visible];
    if (last?.from === 'user') {
      setVisible(visible + 1);
      setThinking(true);
      return;
    }
    const t = setTimeout(() => {
      setThinking(false);
      setVisible(state.chat.length);
    }, thinking ? 900 : 250);
    return () => clearTimeout(t);
  }, [state.chat.length, visible, thinking, state.chat]);

  useEffect(() => {
    end.current?.scrollIntoView({ behavior: 'smooth', block: 'end' });
  }, [visible, thinking]);

  const send = (t: string) => {
    if (!t.trim()) return;
    tap();
    command(t.trim());
    setText('');
  };
  const submit = (e: FormEvent) => {
    e.preventDefault();
    send(text);
  };
  const negotiating = state.negotiations.some((n) => n.status === 'running');
  const lastWithChoices = [...state.chat.slice(0, visible)].reverse().find((m) => m.from === 'aura')?.id;

  return (
    <div className="chat-shell">
      <header className="chat-head">
        <Orb size={70} />
        <div style={{ flex: 1 }}>
          <h1 style={{ fontSize: 26 }}>Aura</h1>
          <div className="muted small">
            {negotiating ? 'Ведёт переговоры' : 'В сети'} · Персональный ассистент
          </div>
        </div>
        <button className="btn btn-ghost btn-pill" onClick={() => setMemory(!memory)} aria-expanded={memory}>
          <span className="hide-sm">Память</span> <Brain size={18} aria-label="Память" />
        </button>
      </header>

      {memory && <Memory onClose={() => setMemory(false)} />}

      <div className="chat-body">
        <aside className="chat-side">
          <nav className="nav" style={{ marginTop: 0 }}>
            <NavLink to="/aura">
              <MessageCircle size={18} /> Диалоги
            </NavLink>
            <NavLink to="/settings">
              <Settings size={18} /> Настройки
            </NavLink>
            <NavLink to="/settings#privacy">
              <Shield size={18} /> Приватность
            </NavLink>
          </nav>
          <div className="nav" style={{ marginTop: 'auto' }}>
            <a href="#help" onClick={(e) => (e.preventDefault(), send('Что ты умеешь?'))}>
              <HelpCircle size={18} /> Помощь
            </a>
            <a
              href="#logout"
              onClick={(e) => {
                e.preventDefault();
                logout();
                navigate('/login');
              }}
            >
              <LogOut size={18} /> Выйти
            </a>
          </div>
        </aside>

        <div className="chat-main">
          <div className="messages" aria-live="polite">
            {state.chat.slice(0, visible).map((m) => (
              <div key={m.id} className={`bubble ${m.from}`}>
                {m.text}
                {m.choices && m.id === lastWithChoices && (
                  <div className="choices">
                    {m.choices.map((c) => (
                      <button
                        key={c.action}
                        className="chip"
                        onClick={() => {
                          const route = choose(c.action);
                          if (route && c.action.startsWith('budget')) setTimeout(() => navigate(route), 1200);
                        }}
                      >
                        {c.label}
                      </button>
                    ))}
                  </div>
                )}
                {m.link && (
                  <div>
                    <Link className="link" to={m.link.to.replace('#result', '')}>
                      {m.link.label} <ArrowRight size={14} />
                    </Link>
                  </div>
                )}
              </div>
            ))}
            {visible <= 1 && (
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
                {SUGGESTIONS.map((s) => (
                  <button key={s} className="chip" onClick={() => send(s)}>
                    {s}
                  </button>
                ))}
              </div>
            )}
            {(thinking || negotiating) && (
              <div className="thinking">
                {thinking ? 'Аура думает' : `Аура договаривается с Аурой ${contacts.find((c) => c.id === state.negotiations.find((n) => n.status === 'running')?.request.contactId)?.nameGen ?? ''}`}
                <i />
                <i />
                <i />
              </div>
            )}
            <div ref={end} />
          </div>

          <form className="composer" onSubmit={submit}>
            <button
              type="button"
              className="icon-btn"
              style={{ width: 50, height: 50, color: speech.listening ? 'var(--pink)' : undefined }}
              aria-label="Голосовой ввод"
              disabled={!speech.supported}
              title={speech.supported ? 'Сказать голосом' : 'Голосовой ввод не поддерживается этим браузером'}
              onClick={speech.toggle}
            >
              <Mic size={20} />
            </button>
            <div className="bar">
              <input value={text} onChange={(e) => setText(e.target.value)} placeholder="Напишите что-нибудь…" aria-label="Сообщение" />
            </div>
            <button className="icon-btn accent send" aria-label="Отправить" disabled={!text.trim()}>
              <SendHorizontal size={22} />
            </button>
          </form>
        </div>
      </div>
    </div>
  );
}

function Memory({ onClose }: { onClose: () => void }) {
  const { state, contacts } = useAura();
  const me = makeMe(state.user!.name);
  const days = ['Вс', 'Пн', 'Вт', 'Ср', 'Чт', 'Пт', 'Сб'];
  const fmt = (h: number) => `${Math.floor(h)}:${h % 1 ? '30' : '00'}`;
  return (
    <div className="memory glass edge">
      <div className="row-between" style={{ marginBottom: 14 }}>
        <strong>Что Аура знает о вас</strong>
        <button className="icon-btn" style={{ width: 30, height: 30 }} onClick={onClose} aria-label="Закрыть">
          <X size={14} />
        </button>
      </div>
      <div className="stack small" style={{ gap: 12 }}>
        <div>
          <div className="faint tiny">Имя и пояс</div>
          {state.user!.name} · {state.user!.timezone}
        </div>
        {state.user!.focus && (
          <div>
            <div className="faint tiny">Помогать с</div>
            {state.user!.focus}
          </div>
        )}
        <div>
          <div className="faint tiny">Занятость</div>
          {me.busy.map((b) => (
            <div key={b.label}>
              {b.label}: {b.days.map((d) => days[d]).join(', ')} {fmt(b.from)}–{fmt(b.to)}
            </div>
          ))}
        </div>
        <div>
          <div className="faint tiny">Бюджет на встречу по умолчанию</div>до 1 000 ₽ на двоих
        </div>
        <div>
          <div className="faint tiny">Доверенные Ауры</div>
          {contacts
            .filter((c) => state.favorites.includes(c.id))
            .map((c) => c.fullName)
            .join(', ') || '—'}
        </div>
        <div className="faint tiny">Ауры других людей видят только, свободны ли вы в предложенное время — не ваш календарь целиком.</div>
      </div>
    </div>
  );
}
