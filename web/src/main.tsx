import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { HashRouter, Navigate, Route, Routes } from 'react-router-dom';
import { AppShell } from './components/AppShell';
import { Backdrop } from './components/ui';
import { NativeBridge } from './platform/NativeBridge';
import { initPlatform } from './platform/native';
import { Login, Signup } from './pages/Auth';
import { Chat } from './pages/Chat';
import { Contacts } from './pages/Contacts';
import { Home } from './pages/Home';
import { NegotiationView, Negotiations } from './pages/Negotiation';
import { Settings } from './pages/Settings';
import { Tasks } from './pages/Tasks';
import { StoreProvider } from './state/store';
import '@fontsource-variable/manrope';
import './styles/global.css';

initPlatform();

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <StoreProvider>
      <HashRouter>
        <Backdrop />
        <div className="titlebar" aria-hidden="true" />
        <NativeBridge />
        <Routes>
          <Route path="/login" element={<Login />} />
          <Route path="/signup" element={<Signup />} />
          <Route element={<AppShell />}>
            <Route index element={<Home />} />
            <Route path="aura" element={<Chat />} />
            <Route path="negotiations" element={<Negotiations />} />
            <Route path="negotiations/:id" element={<NegotiationView />} />
            <Route path="tasks" element={<Tasks />} />
            <Route path="contacts" element={<Contacts />} />
            <Route path="settings" element={<Settings />} />
          </Route>
          <Route path="*" element={<Navigate to="/" replace />} />
        </Routes>
      </HashRouter>
    </StoreProvider>
  </StrictMode>,
);
