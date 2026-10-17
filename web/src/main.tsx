import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { HashRouter, Navigate, Route, Routes, useMatch } from 'react-router-dom';
import { AppShell } from './components/AppShell';
import { ConnectionScreen } from './components/ConnectionScreen';
import { lazyNamed, LazyOutlet } from './components/Lazy';
import { Spinner } from './components/ui';
import { NativeBridge } from './platform/NativeBridge';
import { initPlatform } from './platform/native';
import { ForgotPassword, Login, Signup, TwoFactor, VerifyEmail } from './pages/Auth';
import { DataProvider } from './state/data';
import { SessionProvider, useSession } from './state/session';
import '@fontsource-variable/inter';
import './styles/global.css';

initPlatform();

// Страницы — отдельными чанками: гостевые ссылки (#/i, #/b, #/m) не тянут код приложения,
// а настройки (QR-код) и админка грузятся, только когда их открыли. Вход — в основном чанке.
const Admin = lazyNamed(() => import('./pages/Admin'), 'Admin');
const Chat = lazyNamed(() => import('./pages/Chat'), 'Chat');
const Contacts = lazyNamed(() => import('./pages/Contacts'), 'Contacts');
const ContactChat = lazyNamed(() => import('./pages/Contacts'), 'ContactChat');
const Home = lazyNamed(() => import('./pages/Home'), 'Home');
const Inbox = lazyNamed(() => import('./pages/Inbox'), 'Inbox');
const BookingPage = lazyNamed(() => import('./pages/Booking'), 'BookingPage');
const BookingGuest = lazyNamed(() => import('./pages/BookingGuest'), 'BookingGuest');
const BookingManage = lazyNamed(() => import('./pages/BookingGuest'), 'BookingManage');
const InviteGuest = lazyNamed(() => import('./pages/InviteGuest'), 'InviteGuest');
const Negotiations = lazyNamed(() => import('./pages/Negotiation'), 'Negotiations');
const NegotiationView = lazyNamed(() => import('./pages/Negotiation'), 'NegotiationView');
const Settings = lazyNamed(() => import('./pages/Settings'), 'Settings');
const Tasks = lazyNamed(() => import('./pages/Tasks'), 'Tasks');

function Root() {
  const { status, conn, everConnected, connError } = useSession();
  const invite = useMatch('/i/:token');
  const bookingPage = useMatch('/b/:slug');
  const bookingManage = useMatch('/m/:token');

  // Приглашение по ссылке и страницы записи — публичные страницы гостя: и без входа,
  // и со входом, вне оболочки и без переадресаций на вход; о связи сообщают сами.
  if (invite || bookingPage || bookingManage) {
    return (
      <Routes>
        <Route element={<LazyOutlet full />}>
          <Route path="/i/:token" element={<InviteGuest />} />
          <Route path="/b/:slug" element={<BookingGuest />} />
          <Route path="/m/:token" element={<BookingManage />} />
        </Route>
      </Routes>
    );
  }

  if (status === 'booting') {
    // Токены есть, профиля в кэше нет — ждём сервер. Если он не отвечает, вместо
    // вечной заставки — экран ожидания с «Повторить» и сменой адреса.
    if (conn !== 'open' && connError) return <ConnectionScreen />;
    return (
      <div className="gate">
        <Spinner />
      </div>
    );
  }

  if (status === 'signedOut') {
    // Без сервера войти нельзя: вместо формы, которая упадёт с «Network Error», — экран ожидания.
    if (conn !== 'open' && !everConnected) return <ConnectionScreen />;
    return (
      <Routes>
        <Route path="/login" element={<Login />} />
        <Route path="/signup" element={<Signup />} />
        <Route path="/verify" element={<VerifyEmail />} />
        <Route path="/2fa" element={<TwoFactor />} />
        <Route path="/forgot" element={<ForgotPassword />} />
        <Route path="*" element={<Navigate to="/login" replace />} />
      </Routes>
    );
  }

  return (
    <DataProvider>
      <Routes>
        <Route element={<AppShell />}>
          {/* Меню оболочки на экране сразу, страница подгружается внутри неё. */}
          <Route element={<LazyOutlet />}>
            <Route index element={<Home />} />
            <Route path="aura" element={<Chat />} />
            <Route path="negotiations" element={<Negotiations />} />
            <Route path="negotiations/:id" element={<NegotiationView />} />
            <Route path="booking" element={<BookingPage />} />
            <Route path="tasks" element={<Tasks />} />
            <Route path="contacts" element={<Contacts />} />
            <Route path="contacts/:chatId" element={<ContactChat />} />
            <Route path="inbox" element={<Inbox />} />
            <Route path="settings" element={<Settings />} />
            <Route path="admin" element={<Admin />} />
          </Route>
        </Route>
        <Route path="*" element={<Navigate to="/" replace />} />
      </Routes>
    </DataProvider>
  );
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <SessionProvider>
      <HashRouter>
        <div className="titlebar" aria-hidden="true" />
        <NativeBridge />
        <Root />
      </HashRouter>
    </SessionProvider>
  </StrictMode>,
);
