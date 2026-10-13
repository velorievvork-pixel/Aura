import { Bell, CheckSquare, Handshake, Home, MessageCircle, Settings, Shield, Users } from 'lucide-react';
import { NavLink, Outlet } from 'react-router-dom';
import { isStaff } from '../net/adminApi';
import { useData } from '../state/data';
import { useSession } from '../state/session';
import { OfflineBanner } from './ConnectionScreen';
import { Avatar, Logo } from './ui';

const MAIN = [
  { to: '/', label: 'Главная', icon: Home, end: true },
  { to: '/aura', label: 'Аура', icon: MessageCircle },
  { to: '/negotiations', label: 'Переговоры', icon: Handshake },
  { to: '/tasks', label: 'Задачи', icon: CheckSquare },
  { to: '/contacts', label: 'Контакты', icon: Users },
];

export function AppShell() {
  const { user } = useSession();
  const { unread, confirmations, toast } = useData();
  const inbox = unread + confirmations.length;

  return (
    <div className="app">
      <aside className="sidebar" aria-label="Навигация">
        <div className="brand">
          <Logo /> Aura
        </div>
        {MAIN.map(({ to, label, icon: Icon, end }) => (
          <NavLink key={to} to={to} end={end} className="nav-item">
            <Icon size={16} strokeWidth={1.8} /> {label}
          </NavLink>
        ))}
        <NavLink to="/inbox" className="nav-item">
          <Bell size={16} strokeWidth={1.8} /> Входящие
          {inbox > 0 && <span className="count">{inbox}</span>}
        </NavLink>
        <div className="sidebar-foot">
          {isStaff(user?.role) && (
            <NavLink to="/admin" className="nav-item">
              <Shield size={16} strokeWidth={1.8} /> Администрирование
            </NavLink>
          )}
          <NavLink to="/settings" className="nav-item">
            <Settings size={16} strokeWidth={1.8} /> Настройки
          </NavLink>
          {user && (
            <NavLink to="/settings" className="me-row">
              <Avatar name={user.display_name || user.email} src={user.avatar_url} size={28} />
              <span style={{ minWidth: 0 }}>
                <span className="small ellipsis" style={{ display: 'block', fontWeight: 500 }}>
                  {user.display_name}
                </span>
                <span className="tiny faint ellipsis" style={{ display: 'block' }}>
                  {user.email}
                </span>
              </span>
            </NavLink>
          )}
        </div>
      </aside>

      <main className="main">
        <OfflineBanner />
        <Outlet />
      </main>

      <nav className="tab-bar" aria-label="Вкладки">
        {MAIN.map(({ to, label, icon: Icon, end }) => (
          <NavLink key={to} to={to} end={end}>
            <Icon size={20} strokeWidth={1.8} />
            {label}
          </NavLink>
        ))}
      </nav>

      {toast && (
        <div key={toast.id} className={`toast ${toast.kind === 'error' ? 'error' : ''}`} role="status">
          {toast.text}
        </div>
      )}
    </div>
  );
}
