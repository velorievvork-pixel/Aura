/*
  Оформление публичных страниц для людей без входа: приглашение (#/i/),
  страница записи (#/b/), управление записью (#/m/). Карточка по центру,
  «Заведите свою Ауру» (с кодом пригласившего), «Добавить в календарь»,
  экраны «нет связи» и загрузки.
*/
import { CalendarPlus, RefreshCw, WifiOff } from 'lucide-react';
import { useState, type ReactNode } from 'react';
import { Link } from 'react-router-dom';
import { signupPath } from '../lib/booking';
import { useSession } from '../state/session';
import { downloadFile } from './share';
import { Logo, Spinner } from './ui';

export function GuestLayout({ children, foot = 'Aura — личный ИИ-агент, который договаривается о встречах.' }: { children: ReactNode; foot?: string }) {
  return (
    <div className="auth">
      <div className="auth-card invite-card">
        <span className="logo">
          <Logo />
        </span>
        {children}
        <div className="auth-foot small faint">{foot}</div>
      </div>
    </div>
  );
}

export function GuestMessage({ title, children, action }: { title: string; children?: ReactNode; action?: ReactNode }) {
  return (
    <GuestLayout>
      <div className="stack">
        <h1 className="title-lg">{title}</h1>
        {children && <p className="lead" style={{ margin: 0 }}>{children}</p>}
        {action}
      </div>
    </GuestLayout>
  );
}

/**
  «Заведите свою Ауру» — ведёт на регистрацию с кодом владельца страницы
  (#/signup?ref=<код>), или «Открыть Aura», если человек уже вошёл.
*/
export function OwnAura({ referralCode, pitch = 'Хотите, чтобы встречи согласовывались сами?' }: { referralCode?: string; pitch?: string }) {
  const { status } = useSession();
  if (status === 'signedIn') {
    return (
      <div className="notice">
        <span>
          У вас уже есть Аура —{' '}
          <Link className="link" to="/">
            открыть Aura
          </Link>
          .
        </span>
      </div>
    );
  }
  return (
    <div className="notice">
      <span>
        {pitch}{' '}
        <Link className="link" to={signupPath(referralCode)}>
          Заведите свою Ауру
        </Link>
        .
      </span>
    </div>
  );
}

/** «Добавить в календарь»: файл .ics собирается здесь же, в браузере. */
export function CalendarButton({ build, fileName, label = 'Добавить в календарь', variant = 'primary' }: { build: () => string | null; fileName: string; label?: string; variant?: 'primary' | 'secondary' }) {
  const [failed, setFailed] = useState(false);
  const onClick = () => {
    let text: string | null = null;
    try {
      text = build();
    } catch {
      text = null;
    }
    setFailed(!text || !downloadFile(fileName, text, 'text/calendar;charset=utf-8'));
  };
  return (
    <>
      <button type="button" className={`btn btn-${variant} btn-lg btn-block`} onClick={onClick}>
        <CalendarPlus size={17} /> {label}
      </button>
      {failed && <div className="field-error">Не удалось создать файл календаря.</div>}
    </>
  );
}

export function GuestOffline({ retry }: { retry: () => void }) {
  return (
    <GuestLayout>
      <div className="stack">
        <div className="row">
          <WifiOff size={16} className="faint" />
          <h1 className="title-md">Нет связи с сервером Aura</h1>
        </div>
        <p className="lead" style={{ margin: 0 }}>
          Проверьте интернет — мы пробуем подключиться снова автоматически.
        </p>
        <button className="btn btn-secondary" onClick={retry}>
          <RefreshCw size={15} /> Повторить
        </button>
      </div>
    </GuestLayout>
  );
}

export function GuestLoading({ text }: { text: string }) {
  return (
    <div className="gate">
      <div className="gate-card">
        <Spinner />
        <span className="muted small">{text}</span>
      </div>
    </div>
  );
}

export function RetryButton({ onClick }: { onClick: () => void }) {
  return (
    <button className="btn btn-secondary" onClick={onClick}>
      <RefreshCw size={15} /> Повторить
    </button>
  );
}
