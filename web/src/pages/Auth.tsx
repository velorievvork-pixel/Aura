/*
  Аккаунт: вход, регистрация, код из письма, код 2FA при входе, восстановление
  пароля. Все шаги — реальные вызовы сервера (auth.*), ошибки — под полем.
*/
import { ArrowLeft, Gift, ShieldCheck } from 'lucide-react';
import { useEffect, useState, type FormEvent, type ReactNode } from 'react';
import { Link, Navigate, useLocation, useNavigate } from 'react-router-dom';
import { ServerAddress } from '../components/ServerAddress';
import { Field, Logo, PasswordInput, Spinner } from '../components/ui';
import { forgetReferral, pendingReferral, referralFromSearch, rememberReferral } from '../lib/booking';
import { cleanCode, emailError, nameError, otpError, passwordError, twoFaCodeError } from '../lib/validate';
import { deviceInfo } from '../net/secureStore';
import { errorText, useSession, type LoginStep } from '../state/session';

function AuthLayout({ title, lead, children, foot }: { title: string; lead?: ReactNode; children: ReactNode; foot?: ReactNode }) {
  const { conn } = useSession();
  return (
    <div className="auth">
      <div className="auth-card">
        <span className="logo">
          <Logo />
        </span>
        <div>
          <h1 className="title-lg">{title}</h1>
          {lead && <p className="lead" style={{ margin: '6px 0 0' }}>{lead}</p>}
        </div>
        {conn !== 'open' && <div className="notice warn">Нет связи с сервером Aura — вход заработает, как только он ответит.</div>}
        {children}
        {foot && <div className="auth-foot">{foot}</div>}
        <ServerAddress compact />
      </div>
    </div>
  );
}

function Submit({ busy, children }: { busy: boolean; children: ReactNode }) {
  return (
    <button className="btn btn-primary btn-lg btn-block" type="submit" disabled={busy}>
      {busy ? <Spinner small /> : children}
    </button>
  );
}

/** Куда вести после шага входа. */
function useStepNavigate() {
  const navigate = useNavigate();
  return (step: LoginStep) => {
    if (step.kind === 'twoFactor') navigate('/2fa');
    else if (step.kind === 'verifyEmail') navigate('/verify', { state: { devCode: step.devCode } });
    else navigate('/', { replace: true });
  };
}

export function Login() {
  const { login } = useSession();
  const go = useStepNavigate();
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [errors, setErrors] = useState<{ email?: string | null; password?: string | null; form?: string }>({});
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const next = { email: emailError(email), password: password ? null : 'Введите пароль' };
    setErrors(next);
    if (next.email || next.password) return;
    setBusy(true);
    try {
      go(await login(email, password));
    } catch (error) {
      setErrors({ form: errorText(error) });
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthLayout
      title="Вход в Aura"
      lead="Ваша Аура договаривается о встречах с Аурами других людей."
      foot={
        <>
          Нет аккаунта? <Link className="link" to="/signup">Зарегистрироваться</Link>
        </>
      }
    >
      <form className="stack" onSubmit={submit} noValidate>
        {errors.form && <div className="notice error">{errors.form}</div>}
        <Field label="Email" error={errors.email}>
          <input className={`input ${errors.email ? 'invalid' : ''}`} type="email" autoComplete="email" inputMode="email" value={email} onChange={(e) => setEmail(e.target.value)} placeholder="you@example.com" autoFocus />
        </Field>
        <Field label="Пароль" error={errors.password}>
          <PasswordInput invalid={Boolean(errors.password)} autoComplete="current-password" value={password} onChange={(e) => setPassword(e.target.value)} />
        </Field>
        <div className="row" style={{ justifyContent: 'flex-end', marginTop: -4 }}>
          <Link className="link small" to="/forgot">
            Забыли пароль?
          </Link>
        </div>
        <Submit busy={busy}>Войти</Submit>
      </form>
    </AuthLayout>
  );
}

/**
  Код пригласившего коллеги: из адреса #/signup?ref=<код> (запоминается на время
  вкладки — переход на «Войти» и обратно его не теряет), иначе — запомненный ранее.
*/
function useReferral(): string {
  const { search } = useLocation();
  const [code] = useState(() => {
    const fromUrl = referralFromSearch(search);
    if (fromUrl) rememberReferral(fromUrl);
    return fromUrl || pendingReferral();
  });
  return code;
}

export function Signup() {
  const { register } = useSession();
  const navigate = useNavigate();
  const referral = useReferral();
  const [form, setForm] = useState({ name: '', email: '', password: '' });
  const [errors, setErrors] = useState<{ name?: string | null; email?: string | null; password?: string | null; form?: string }>({});
  const [busy, setBusy] = useState(false);
  const set = (key: keyof typeof form) => (e: React.ChangeEvent<HTMLInputElement>) => setForm((f) => ({ ...f, [key]: e.target.value }));

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const next = { name: nameError(form.name), email: emailError(form.email), password: passwordError(form.password) };
    setErrors(next);
    if (next.name || next.email || next.password) return;
    setBusy(true);
    try {
      const { devCode } = await register(form.name, form.email, form.password, referral);
      // Код ушёл на сервер вместе с регистрацией — больше не нужен.
      forgetReferral();
      navigate('/verify', { state: { devCode } });
    } catch (error) {
      setErrors({ form: errorText(error) });
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthLayout
      title="Создать аккаунт"
      lead="Пришлём код подтверждения на email."
      foot={
        <>
          Уже есть аккаунт? <Link className="link" to="/login">Войти</Link>
        </>
      }
    >
      <form className="stack" onSubmit={submit} noValidate>
        {referral && (
          <div className="notice referral-note">
            <Gift size={15} aria-hidden="true" />
            <span>Вас пригласили в Aura. Когда к вам впервые запишутся по вашей ссылке или выберут время по вашему приглашению, вы и пригласивший получите по 30 дней Pro.</span>
          </div>
        )}
        {errors.form && <div className="notice error">{errors.form}</div>}
        <Field label="Имя" error={errors.name}>
          <input className={`input ${errors.name ? 'invalid' : ''}`} autoComplete="name" value={form.name} onChange={set('name')} placeholder="Алекс Морган" autoFocus />
        </Field>
        <Field label="Email" error={errors.email}>
          <input className={`input ${errors.email ? 'invalid' : ''}`} type="email" autoComplete="email" inputMode="email" value={form.email} onChange={set('email')} placeholder="you@example.com" />
        </Field>
        <Field label="Пароль" error={errors.password} hint="Не короче 8 символов, буквы и цифры">
          <PasswordInput invalid={Boolean(errors.password)} autoComplete="new-password" value={form.password} onChange={set('password')} />
        </Field>
        <Submit busy={busy}>Продолжить</Submit>
      </form>
    </AuthLayout>
  );
}

function DevCode({ code }: { code?: string }) {
  if (!code) return null;
  return (
    <div className="notice">
      Сервер в режиме разработки и не отправляет письма. Код: <b className="mono">{code}</b>
    </div>
  );
}

export function VerifyEmail() {
  const { pendingEmail, verifyEmail, resendCode } = useSession();
  const go = useStepNavigate();
  const location = useLocation();
  const [devCode, setDevCode] = useState<string | undefined>((location.state as { devCode?: string } | null)?.devCode);
  const [code, setCode] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);
  const [cooldown, setCooldown] = useState(30);

  useEffect(() => {
    if (cooldown <= 0) return;
    const t = setTimeout(() => setCooldown((c) => c - 1), 1000);
    return () => clearTimeout(t);
  }, [cooldown]);

  if (!pendingEmail) return <Navigate to="/login" replace />;

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const problem = otpError(code);
    if (problem) return setError(problem);
    setBusy(true);
    try {
      go(await verifyEmail(code));
    } catch (err) {
      setError(errorText(err));
    } finally {
      setBusy(false);
    }
  };

  const resend = async () => {
    setCooldown(30);
    try {
      setDevCode((await resendCode()).devCode);
    } catch (err) {
      setError(errorText(err));
    }
  };

  return (
    <AuthLayout
      title="Подтвердите email"
      lead={
        <>
          Мы отправили 6-значный код на <b>{pendingEmail}</b>.
        </>
      }
      foot={
        <Link className="link" to="/login">
          <ArrowLeft size={14} style={{ verticalAlign: -2 }} /> Ко входу
        </Link>
      }
    >
      <form className="stack" onSubmit={submit} noValidate>
        <DevCode code={devCode} />
        <Field label="Код из письма" error={error}>
          <input className={`input code-input mono ${error ? 'invalid' : ''}`} inputMode="numeric" autoComplete="one-time-code" value={code} onChange={(e) => setCode(cleanCode(e.target.value))} placeholder="000000" autoFocus />
        </Field>
        <Submit busy={busy}>Подтвердить</Submit>
        <button type="button" className="btn btn-ghost" onClick={resend} disabled={cooldown > 0}>
          {cooldown > 0 ? `Отправить код ещё раз через ${cooldown} с` : 'Отправить код ещё раз'}
        </button>
      </form>
    </AuthLayout>
  );
}

export function TwoFactor() {
  const { login2fa, status } = useSession();
  const navigate = useNavigate();
  const [code, setCode] = useState('');
  const [trust, setTrust] = useState(true);
  // «Доверять устройству» — только если id устройства переживёт перезапуск.
  const [trustable, setTrustable] = useState(false);
  const [recovery, setRecovery] = useState(false);
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let alive = true;
    deviceInfo().then(
      (d) => alive && setTrustable(d.persistent),
      () => alive && setTrustable(false),
    );
    return () => {
      alive = false;
    };
  }, []);

  if (status === 'signedIn') return <Navigate to="/" replace />;

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const problem = twoFaCodeError(code);
    if (problem) return setError(problem);
    setBusy(true);
    try {
      await login2fa(code, trust && trustable);
      navigate('/', { replace: true });
    } catch (err) {
      const text = errorText(err);
      setError(text);
      // Страница открыта без пароля в памяти (перезапуск) — вход с начала.
      if (text === 'Начните вход заново') navigate('/login', { replace: true });
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthLayout
      title="Двухфакторная защита"
      lead={recovery ? 'Введите один из резервных кодов, сохранённых при включении 2FA.' : 'Откройте Google Authenticator или Яндекс Ключ и введите код для Aura.'}
      foot={
        <Link className="link" to="/login">
          <ArrowLeft size={14} style={{ verticalAlign: -2 }} /> Другой аккаунт
        </Link>
      }
    >
      <form className="stack" onSubmit={submit} noValidate>
        <Field label={recovery ? 'Резервный код' : 'Код из приложения'} error={error}>
          {recovery ? (
            <input className={`input mono ${error ? 'invalid' : ''}`} value={code} onChange={(e) => setCode(e.target.value.toUpperCase().slice(0, 11))} placeholder="XXXXX-XXXXX" autoCapitalize="characters" autoFocus />
          ) : (
            <input className={`input code-input mono ${error ? 'invalid' : ''}`} inputMode="numeric" autoComplete="one-time-code" value={code} onChange={(e) => setCode(cleanCode(e.target.value))} placeholder="000000" autoFocus />
          )}
        </Field>
        {trustable && (
          <label className="row small muted" style={{ cursor: 'pointer' }}>
            <input type="checkbox" checked={trust} onChange={(e) => setTrust(e.target.checked)} />
            Доверять этому устройству 90 дней
          </label>
        )}
        <Submit busy={busy}>
          <ShieldCheck size={16} /> Войти
        </Submit>
        <button
          type="button"
          className="btn btn-ghost"
          onClick={() => {
            setRecovery((v) => !v);
            setCode('');
            setError('');
          }}
        >
          {recovery ? 'Ввести код из приложения' : 'Нет доступа к приложению? Резервный код'}
        </button>
      </form>
    </AuthLayout>
  );
}

export function ForgotPassword() {
  const { forgotPassword, resetPassword } = useSession();
  const go = useStepNavigate();
  const [step, setStep] = useState<'email' | 'reset'>('email');
  const [email, setEmail] = useState('');
  const [code, setCode] = useState('');
  const [password, setPassword] = useState('');
  const [devCode, setDevCode] = useState<string | undefined>();
  const [errors, setErrors] = useState<{ email?: string | null; code?: string | null; password?: string | null; form?: string }>({});
  const [busy, setBusy] = useState(false);

  const requestCode = async (e: FormEvent) => {
    e.preventDefault();
    const problem = emailError(email);
    setErrors({ email: problem });
    if (problem) return;
    setBusy(true);
    try {
      setDevCode((await forgotPassword(email)).devCode);
      setStep('reset');
    } catch (err) {
      setErrors({ form: errorText(err) });
    } finally {
      setBusy(false);
    }
  };

  const reset = async (e: FormEvent) => {
    e.preventDefault();
    const next = { code: otpError(code), password: passwordError(password) };
    setErrors(next);
    if (next.code || next.password) return;
    setBusy(true);
    try {
      go(await resetPassword(email, code, password));
    } catch (err) {
      setErrors({ form: errorText(err) });
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthLayout
      title={step === 'email' ? 'Восстановление пароля' : 'Новый пароль'}
      lead={step === 'email' ? 'Пришлём код для сброса пароля.' : <>Код отправлен на <b>{email}</b>, если такой аккаунт есть.</>}
      foot={
        <Link className="link" to="/login">
          <ArrowLeft size={14} style={{ verticalAlign: -2 }} /> Ко входу
        </Link>
      }
    >
      {step === 'email' ? (
        <form className="stack" onSubmit={requestCode} noValidate>
          {errors.form && <div className="notice error">{errors.form}</div>}
          <Field label="Email" error={errors.email}>
            <input className={`input ${errors.email ? 'invalid' : ''}`} type="email" autoComplete="email" inputMode="email" value={email} onChange={(e) => setEmail(e.target.value)} autoFocus />
          </Field>
          <Submit busy={busy}>Получить код</Submit>
        </form>
      ) : (
        <form className="stack" onSubmit={reset} noValidate>
          {errors.form && <div className="notice error">{errors.form}</div>}
          <DevCode code={devCode} />
          <Field label="Код из письма" error={errors.code}>
            <input className={`input code-input mono ${errors.code ? 'invalid' : ''}`} inputMode="numeric" autoComplete="one-time-code" value={code} onChange={(e) => setCode(cleanCode(e.target.value))} placeholder="000000" autoFocus />
          </Field>
          <Field label="Новый пароль" error={errors.password} hint="Не короче 8 символов, буквы и цифры. Все устройства выйдут из аккаунта.">
            <PasswordInput invalid={Boolean(errors.password)} autoComplete="new-password" value={password} onChange={(e) => setPassword(e.target.value)} />
          </Field>
          <Submit busy={busy}>Сменить пароль и войти</Submit>
        </form>
      )}
    </AuthLayout>
  );
}
