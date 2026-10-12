import { ChevronDown, Eye, EyeOff, Lock, Mail } from 'lucide-react';
import { useState, type FormEvent } from 'react';
import { Link, Navigate, useNavigate } from 'react-router-dom';
import { AuraMark, GoogleIcon } from '../components/ui';
import { useAura } from '../state/store';

const EMAIL_RE = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;

function nameFromEmail(email: string) {
  const raw = email.split('@')[0].replace(/[._-]+/g, ' ').trim();
  return raw ? raw[0].toUpperCase() + raw.slice(1) : 'Гость';
}

export function Login() {
  const { state, login } = useAura();
  const navigate = useNavigate();
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  if (state.user) return <Navigate to="/" replace />;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (!EMAIL_RE.test(email)) return setError('Введите корректный email');
    if (password.length < 6) return setError('Пароль — не короче 6 символов');
    setError('');
    setBusy(true);
    setTimeout(() => {
      login({ name: nameFromEmail(email), email, timezone: Intl.DateTimeFormat().resolvedOptions().timeZone, focus: '' });
      navigate('/');
    }, 600);
  };

  const demo = () => {
    login({ name: 'Алекс Морган', email: 'alex@aura.app', timezone: Intl.DateTimeFormat().resolvedOptions().timeZone, focus: 'стартап, встречи' });
    navigate('/');
  };

  return (
    <div className="auth">
      <div className="auth-card glass edge">
        <div className="glow" />
        <div className="auth-logo">
          <AuraMark size={82} />
        </div>
        <h1 className="auth-title">Добро пожаловать в Aura</h1>
        <p className="auth-sub">Ваша персональная сеть ИИ-агентов</p>

        <form className="auth-form" onSubmit={submit} noValidate>
          <label className="field">
            <span>Email</span>
            <div className="input-wrap">
              <input className="input" type="email" autoComplete="email" placeholder="Введите email" value={email} onChange={(e) => setEmail(e.target.value)} />
              <span className="input-icon">
                <Mail size={18} />
              </span>
            </div>
          </label>
          <label className="field">
            <span>Пароль</span>
            <div className="input-wrap">
              <input className="input" type="password" autoComplete="current-password" placeholder="••••••••" value={password} onChange={(e) => setPassword(e.target.value)} />
              <span className="input-icon">
                <Lock size={17} />
              </span>
            </div>
          </label>
          <div className="row-between" style={{ marginTop: -6 }}>
            {error ? <span className="error">{error}</span> : <span />}
            <button type="button" className="btn btn-text small" style={{ fontWeight: 600 }}>
              Забыли пароль?
            </button>
          </div>
          <button className="btn btn-primary btn-block" style={{ marginTop: 10 }} disabled={busy}>
            {busy ? 'Входим…' : 'Войти'}
          </button>
          <button type="button" className="btn btn-ghost btn-block" style={{ marginTop: 8 }} onClick={demo}>
            <GoogleIcon /> Продолжить с Google
          </button>
          <div className="row-between small">
            <button type="button" className="btn btn-text small" onClick={demo}>
              Демо-вход
            </button>
            <Link to="/signup" style={{ fontWeight: 600 }}>
              Создать аккаунт
            </Link>
          </div>
        </form>
      </div>
    </div>
  );
}

const TIMEZONES = ['UTC +2:00', 'UTC +3:00', 'UTC +4:00', 'UTC +5:00', 'UTC +6:00', 'UTC +7:00', 'UTC +8:00', 'UTC +9:00'];

function currentOffset() {
  const m = -new Date().getTimezoneOffset();
  return `UTC ${m >= 0 ? '+' : '-'}${Math.floor(Math.abs(m) / 60)}:${String(Math.abs(m) % 60).padStart(2, '0')}`;
}

export function Signup() {
  const { state, login } = useAura();
  const [form, setForm] = useState({ name: '', email: '', password: '', timezone: currentOffset(), focus: '' });
  const [show, setShow] = useState(false);
  const [error, setError] = useState('');
  const tzs = TIMEZONES.includes(form.timezone) ? TIMEZONES : [form.timezone, ...TIMEZONES];

  // A new account lands in the chat, where Aura introduces itself.
  if (state.user) return <Navigate to="/aura" replace />;

  const set = (k: keyof typeof form) => (e: { target: { value: string } }) => setForm({ ...form, [k]: e.target.value });

  const submit = (e: FormEvent) => {
    e.preventDefault();
    if (form.name.trim().length < 2) return setError('Как вас зовут?');
    if (!EMAIL_RE.test(form.email)) return setError('Введите корректный email');
    if (form.password.length < 6) return setError('Пароль — не короче 6 символов');
    login({ name: form.name.trim(), email: form.email, timezone: form.timezone, focus: form.focus });
  };

  return (
    <div className="auth">
      <div className="signup-wrap">
        <svg className="orbit" viewBox="0 0 600 600" aria-hidden="true">
          <ellipse cx="300" cy="300" rx="290" ry="120" fill="none" stroke="rgba(190,160,255,.45)" transform="rotate(-25 300 300)" />
          <ellipse cx="300" cy="300" rx="280" ry="140" fill="none" stroke="rgba(160,170,255,.35)" transform="rotate(30 300 300)" />
        </svg>
        <span className="signup-glow g1" />
        <span className="signup-glow g2" />
      <div className="auth-card narrow glass edge">
        <div className="window-dots" aria-hidden="true">
          <i />
          <i />
          <i />
        </div>
        <div className="brand" style={{ justifyContent: 'center', marginBottom: 26 }}>
          <svg width="30" height="30" viewBox="0 0 32 32" aria-hidden="true">
            <path d="M16 3 L29 28 H21 L16 18 L11 28 H3 Z" fill="#fff" />
          </svg>
          Aura
        </div>
        <h1 className="h2" style={{ fontSize: 26, marginBottom: 18 }}>
          Создайте свою Ауру
        </h1>
        <form className="auth-form" onSubmit={submit} noValidate style={{ gap: 14 }}>
          <label className="field">
            <span>Полное имя</span>
            <input className="input" placeholder="Введите имя и фамилию" value={form.name} onChange={set('name')} autoComplete="name" />
          </label>
          <label className="field">
            <span>Email</span>
            <input className="input" type="email" placeholder="Введите email" value={form.email} onChange={set('email')} autoComplete="email" />
          </label>
          <label className="field">
            <span>Пароль</span>
            <div className="input-wrap">
              <input className="input" type={show ? 'text' : 'password'} placeholder="Придумайте пароль" value={form.password} onChange={set('password')} autoComplete="new-password" />
              <button type="button" className="input-icon" onClick={() => setShow(!show)} aria-label={show ? 'Скрыть пароль' : 'Показать пароль'}>
                {show ? <EyeOff size={18} /> : <Eye size={18} />}
              </button>
            </div>
          </label>
          <label className="field">
            <span>Часовой пояс</span>
            <div className="input-wrap">
              <select className="input" value={form.timezone} onChange={set('timezone')}>
                {tzs.map((t) => (
                  <option key={t}>{t}</option>
                ))}
              </select>
              <span className="input-icon">
                <ChevronDown size={18} />
              </span>
            </div>
          </label>
          <label className="field">
            <span>С чем должна помогать ваша Аура?</span>
            <input className="input filled" placeholder="например, встречи, продуктивность, код" value={form.focus} onChange={set('focus')} />
          </label>
          {error && <span className="error">{error}</span>}
          <button className="btn btn-primary btn-pill btn-block" style={{ marginTop: 8, background: 'linear-gradient(90deg,#7c6cf6,#b7a2ff)' }}>
            Создать аккаунт
          </button>
          <div className="small muted" style={{ display: 'flex', justifyContent: 'center', gap: 14 }}>
            <a href="#terms">Условия</a>
            <span className="faint">|</span>
            <a href="#privacy">Конфиденциальность</a>
          </div>
          <Link to="/login" className="small" style={{ justifySelf: 'end', fontWeight: 600 }}>
            Уже есть аккаунт?
          </Link>
        </form>
      </div>
      </div>
    </div>
  );
}
