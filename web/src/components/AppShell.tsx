import { CheckSquare, Handshake, Home, LogOut, MessageCircle, Settings, Users } from 'lucide-react';
import { NavLink, Navigate, Outlet, useNavigate } from 'react-router-dom';
import { useAura } from '../state/store';
import { AuraRing, Avatar } from './ui';

const NAV = [
  { to: '/', label: 'Главная', icon: Home, end: true },
  { to: '/aura', label: 'Моя Аура', icon: MessageCircle },
  { to: '/negotiations', label: 'Переговоры', icon: Handshake },
  { to: '/tasks', label: 'Задачи', icon: CheckSquare },
  { to: '/contacts', label: 'Контакты', icon: Users },
  { to: '/settings', label: 'Настройки', icon: Settings },
];

export function AppShell() {
  const { state, logout } = useAura();
  const navigate = useNavigate();
  if (!state.user) return <Navigate to="/login" replace />;

  const live = state.negotiations.filter((n) => n.status === 'running' || n.status === 'awaiting').length;

  return (
    <div className="shell">
      <aside className="sidebar glass edge">
        <div className="brand">
          <AuraRing />
          Aura
        </div>
        <nav className="nav" aria-label="Основная навигация">
          {NAV.map(({ to, label, icon: Icon, end }) => (
            <NavLink key={to} to={to} end={end}>
              <Icon size={18} strokeWidth={1.8} />
              {label}
              {to === '/negotiations' && live > 0 && <span className="count">{live}</span>}
            </NavLink>
          ))}
        </nav>
        <div className="sidebar-foot">
          <NavLink to="/settings" className="me-row">
            <Avatar name={state.user.name} size={34} hue={280} />
            <div style={{ minWidth: 0 }}>
              <div style={{ fontWeight: 600, fontSize: 14 }}>{state.user.name}</div>
              <div className="faint tiny" style={{ overflow: 'hidden', textOverflow: 'ellipsis' }}>
                {state.user.email}
              </div>
            </div>
          </NavLink>
          <button
            className="me-row muted small"
            onClick={() => {
              logout();
              navigate('/login');
            }}
          >
            <LogOut size={16} /> Выйти
          </button>
        </div>
      </aside>

      <main className="main glass edge">
        <Outlet />
      </main>

      <nav className="mobile-nav glass" aria-label="Навигация">
        {NAV.slice(0, 5).map(({ to, label, icon: Icon, end }) => (
          <NavLink key={to} to={to} end={end}>
            <Icon size={20} strokeWidth={1.8} />
            {label.replace('Моя ', '')}
          </NavLink>
        ))}
      </nav>
    </div>
  );
}
