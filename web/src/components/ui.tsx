import { Eye, EyeOff } from 'lucide-react';
import { useState, type InputHTMLAttributes, type ReactNode } from 'react';

/** Знак Aura: тонкое кольцо без свечения. */
export function Logo({ size = 22 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" aria-hidden="true">
      <circle cx="12" cy="12" r="8.5" stroke="currentColor" strokeWidth="2" opacity="0.35" />
      <path d="M12 3.5a8.5 8.5 0 0 1 8.5 8.5" stroke="var(--accent)" strokeWidth="2" strokeLinecap="round" />
      <circle cx="12" cy="12" r="2.5" fill="currentColor" />
    </svg>
  );
}

// Приглушённые оттенки для аватаров без фото — по имени, стабильно.
const HUES = ['#5b5fc7', '#3f8f7a', '#a0673d', '#8a4f8f', '#4f7aa8', '#7d7a3e', '#a14f5f'];

export function initials(name: string): string {
  const parts = name.trim().split(/\s+/).filter(Boolean);
  if (!parts.length) return '·';
  return (parts[0][0] + (parts[1]?.[0] ?? '')).toUpperCase();
}

export function Avatar({ name, src, size = 36 }: { name: string; src?: string; size?: number }) {
  const hue = HUES[[...name].reduce((sum, ch) => sum + ch.charCodeAt(0), 0) % HUES.length];
  return (
    <span className="avatar" style={{ width: size, height: size, fontSize: size * 0.38, background: src ? undefined : hue }} aria-hidden="true">
      {src ? <img src={src} alt="" /> : initials(name)}
    </span>
  );
}

export function Spinner({ small }: { small?: boolean }) {
  return <span className={`spinner ${small ? 'sm' : ''}`} role="status" aria-label="Загрузка" />;
}

export function Field({ label, error, hint, children }: { label: string; error?: string | null; hint?: ReactNode; children: ReactNode }) {
  return (
    <label className="field">
      <span>{label}</span>
      {children}
      {error ? <span className="field-error">{error}</span> : hint ? <span className="hint">{hint}</span> : null}
    </label>
  );
}

export function PasswordInput(props: InputHTMLAttributes<HTMLInputElement> & { invalid?: boolean }) {
  const [shown, setShown] = useState(false);
  const { invalid, className, ...rest } = props;
  return (
    <span className="input-wrap">
      <input {...rest} type={shown ? 'text' : 'password'} className={`input ${invalid ? 'invalid' : ''} ${className ?? ''}`} />
      <button type="button" className="icon-btn" onClick={() => setShown((v) => !v)} aria-label={shown ? 'Скрыть пароль' : 'Показать пароль'}>
        {shown ? <EyeOff size={16} /> : <Eye size={16} />}
      </button>
    </span>
  );
}

export function Switch({ on, onChange, label, disabled }: { on: boolean; onChange: (v: boolean) => void; label: string; disabled?: boolean }) {
  return <button type="button" role="switch" aria-checked={on} aria-label={label} disabled={disabled} className={`switch ${on ? 'on' : ''}`} onClick={() => onChange(!on)} />;
}

export function Segmented<T extends string>({ value, options, onChange, label }: { value: T; options: { value: T; label: string }[]; onChange: (v: T) => void; label: string }) {
  return (
    <div className="segmented" role="radiogroup" aria-label={label}>
      {options.map((o) => (
        <button key={o.value} type="button" role="radio" aria-checked={value === o.value} className={`${value === o.value ? 'on' : ''} ${o.value}`} onClick={() => value !== o.value && onChange(o.value)}>
          {o.label}
        </button>
      ))}
    </div>
  );
}

export function Empty({ children }: { children: ReactNode }) {
  return <div className="empty">{children}</div>;
}
