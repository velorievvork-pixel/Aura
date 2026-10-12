import type { CSSProperties } from 'react';
import type { Presence } from '../lib/types';

/** The swirl of dashes from the sign-in screen. */
export function AuraMark({ size = 72 }: { size?: number }) {
  const n = 12;
  return (
    <svg width={size} height={size} viewBox="0 0 100 100" aria-hidden="true">
      <defs>
        <linearGradient id="aura-mark" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#f6e6ff" />
          <stop offset="1" stopColor="#b18cff" />
        </linearGradient>
      </defs>
      <g fill="none" stroke="url(#aura-mark)" strokeWidth="5" strokeLinecap="round">
        {Array.from({ length: n }, (_, i) => (
          <path key={i} d="M50 9 Q 60 9.5 66 19" transform={`rotate(${(360 / n) * i} 50 50)`} />
        ))}
      </g>
    </svg>
  );
}

/** Small ring logo used in the sidebar. */
export function AuraRing({ size = 26 }: { size?: number }) {
  return (
    <svg width={size} height={size} viewBox="0 0 32 32" aria-hidden="true">
      <circle cx="16" cy="16" r="12" fill="none" stroke="currentColor" strokeWidth="3.5" />
      <circle cx="16" cy="16" r="4.5" fill="currentColor" />
    </svg>
  );
}

export function Orb({ size = 64, style }: { size?: number; style?: CSSProperties }) {
  return <div className="orb" style={{ width: size, height: size, ...style }} aria-hidden="true" />;
}

export function Avatar({ name, hue = 265, size = 44, ring }: { name: string; hue?: number; size?: number; ring?: boolean }) {
  const initials = name
    .split(/\s+/)
    .map((p) => p[0])
    .join('')
    .slice(0, 2)
    .toUpperCase();
  return (
    <div
      className={`avatar${ring ? ' ring' : ''}`}
      style={{
        width: size,
        height: size,
        fontSize: size * 0.36,
        background: `radial-gradient(circle at 30% 25%, hsl(${hue} 90% 78%), hsl(${hue} 70% 52%) 55%, hsl(${(hue + 30) % 360} 70% 28%))`,
      }}
      aria-hidden="true"
    >
      {initials}
    </div>
  );
}

export const PRESENCE: Record<Presence, { label: string; dot: string }> = {
  active: { label: 'Аура активна', dot: 'violet' },
  available: { label: 'Свободен', dot: 'green' },
  away: { label: 'Не на месте', dot: 'amber' },
  offline: { label: 'Нет Ауры', dot: 'gray' },
};

export function Backdrop() {
  return (
    <div className="backdrop" aria-hidden="true">
      <div className="blob b1" />
      <div className="blob b2" />
      <div className="blob b3" />
    </div>
  );
}

export function GoogleIcon() {
  return (
    <svg width="18" height="18" viewBox="0 0 48 48" aria-hidden="true">
      <path fill="#fff" d="M44.5 20H24v8.5h11.8C34.7 33.9 30.1 37 24 37c-7.2 0-13-5.8-13-13s5.8-13 13-13c3.1 0 5.9 1.1 8.1 2.9l6.4-6.4C34.6 4.1 29.6 2 24 2 11.8 2 2 11.8 2 24s9.8 22 22 22c11 0 21-8 21-22 0-1.3-.2-2.7-.5-4z" />
    </svg>
  );
}
