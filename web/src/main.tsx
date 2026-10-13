import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { AppShell } from './components/AppShell';
import { ConnectionScreen } from './components/ConnectionScreen';
import { Spinner } from './components/ui';
import { NativeBridge } from './platform/NativeBridge';
import { initPlatform } from './platform/native';
import { ForgotPassword, Login, Signup, TwoFactor, VerifyEmail } from './pages/Auth';
import { Chat } from './pages/Chat';
import { ContactChat, Contacts } from './pages/Contacts';
import { Home } from './pages/Home';
import { Inbox } from './pages/Inbox';
import { NegotiationView, Negotiations } from './pages/Negotiation';
import { Settings } from './pages/Settings';
import { Tasks } from './pages/Tasks';
import { DataProvider } from './state/data';
import { SessionProvider, useSession } from './state/session';
import '@fontsource-variable/inter';
import './styles/global.css';

initPlatform();

function Root() {
  const { status, conn, everConnected, connError } = useSession();

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
          <Route index element={<Home />} />
          <Route path="aura" element={<Chat />} />
          <Route path="negotiations" element={<Negotiations />} />
          <Route path="negotiations/:id" element={<NegotiationView />} />
          <Route path="tasks" element={<Tasks />} />
          <Route path="contacts" element={<Contacts />} />
          <Route path="contacts/:chatId" element={<ContactChat />} />
          <Route path="inbox" element={<Inbox />} />
          <Route path="settings" element={<Settings />} />
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
