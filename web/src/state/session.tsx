/*
  Сессия: соединение с сервером + аккаунт.

  Жизненный цикл:
  1. старт — читаем токены; если есть, сразу показываем приложение с
     последним известным профилем (офлайн тоже работает), иначе — вход;
  2. при каждом открытии сокета (onOpen) повторно авторизуемся: auth.token,
     при отказе — auth.refresh (ротация refresh-токена), при отказе и его —
     выходим на экран входа;
  3. вход: auth.login → при включённой 2FA сервер отвечает requires_2fa —
     спрашиваем код и шлём auth.login2fa; при неподтверждённом email —
     экран кода из письма (auth.verifyEmail).

  Пароль держится только в памяти на время многошагового входа (2FA, код из
  письма) и нигде не сохраняется.
*/
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { currentEnv, loadOverride, normalizeServerUrl, saveOverride, serverCandidates } from '../net/config';
import { AuraError, AuraSocket, isNetworkError, type ConnState } from '../net/socket';
import { makeApi, type Api, type AuthPayload, type User } from '../net/api';
import { clearTokens, deviceId, loadTokens, saveTokens } from '../net/secureStore';
import { getPlatform } from '../platform';

export type AuthStatus = 'booting' | 'signedOut' | 'signedIn';

/** Итог шага входа: дальше приложение, ввод 2FA-кода или кода из письма. */
export type LoginStep = { kind: 'done' } | { kind: 'twoFactor' } | { kind: 'verifyEmail'; devCode?: string };

interface Session {
  api: Api;
  socket: AuraSocket;
  conn: ConnState;
  /** Было ли хоть одно успешное подключение за этот запуск. */
  everConnected: boolean;
  serverUrl: string;
  connError: string;
  status: AuthStatus;
  user: User | null;
  /** Email, ожидающий кода (регистрация / вход до подтверждения). */
  pendingEmail: string;

  login(email: string, password: string): Promise<LoginStep>;
  login2fa(code: string, trustDevice: boolean): Promise<void>;
  register(name: string, email: string, password: string): Promise<{ devCode?: string }>;
  verifyEmail(code: string): Promise<LoginStep>;
  resendCode(): Promise<{ devCode?: string }>;
  forgotPassword(email: string): Promise<{ devCode?: string }>;
  resetPassword(email: string, code: string, newPassword: string): Promise<LoginStep>;
  logout(): Promise<void>;
  setUser(user: User): void;
  setServer(input: string | null): string | null;
  retry(): void;
}

const Ctx = createContext<Session | null>(null);
const USER_KEY = 'aura.user';

function deviceName(): string {
  const platform = getPlatform();
  if (platform === 'android') return 'Aura для Android';
  if (platform === 'ios') return 'Aura для iPhone';
  if (platform === 'desktop') return 'Aura Desktop';
  return 'Aura в браузере';
}

function cachedUser(): User | null {
  try {
    const raw = localStorage.getItem(USER_KEY);
    return raw ? (JSON.parse(raw) as User) : null;
  } catch {
    return null;
  }
}

function cacheUser(user: User | null) {
  try {
    if (user) localStorage.setItem(USER_KEY, JSON.stringify(user));
    else localStorage.removeItem(USER_KEY);
  } catch {
    /* без кэша профиль просто подгрузится после подключения */
  }
}

export function SessionProvider({ children }: { children: ReactNode }) {
  const [conn, setConn] = useState<ConnState>('connecting');
  const [everConnected, setEverConnected] = useState(false);
  const [status, setStatus] = useState<AuthStatus>('booting');
  const [user, setUserState] = useState<User | null>(null);
  const [pendingEmail, setPendingEmail] = useState('');
  const [serverUrl, setServerUrl] = useState('');
  const [connError, setConnError] = useState('');
  const [override, setOverride] = useState<string | null>(() => loadOverride());

  const overrideRef = useRef(override);
  overrideRef.current = override;
  // Учётные данные многошагового входа — только в памяти.
  const pending = useRef<{ email: string; password: string } | null>(null);

  const applyUser = useCallback((next: User | null) => {
    setUserState(next);
    cacheUser(next);
  }, []);

  const signedIn = useCallback(
    async (payload: AuthPayload) => {
      await saveTokens({ access: payload.token, refresh: payload.refresh_token });
      applyUser(payload.user);
      pending.current = null;
      setPendingEmail('');
      setStatus('signedIn');
    },
    [applyUser],
  );

  const signedOut = useCallback(async () => {
    await clearTokens();
    applyUser(null);
    setStatus('signedOut');
  }, [applyUser]);

  // Один сокет на всё время жизни приложения.
  const socket = useMemo(
    () =>
      new AuraSocket({
        candidates: () => serverCandidates(overrideRef.current, currentEnv()),
        onOpen: async (sock) => {
          const tokens = await loadTokens();
          if (!tokens) {
            setStatus((s) => (s === 'signedIn' ? s : 'signedOut'));
            return;
          }
          try {
            const reply = await sock.requestNow<{ user?: User }>('auth.token', { token: tokens.access });
            // Старые серверы на свежем соединении отдают пустой ответ — профиль берём из auth.me.
            applyUser(reply.user ?? (await sock.requestNow<User>('auth.me')));
            setStatus('signedIn');
            return;
          } catch (error) {
            if (isNetworkError(error)) throw error;
          }
          if (tokens.refresh) {
            try {
              const renewed = await sock.requestNow<AuthPayload>('auth.refresh', { refresh_token: tokens.refresh, device: deviceName() });
              await saveTokens({ access: renewed.token, refresh: renewed.refresh_token });
              applyUser(renewed.user);
              setStatus('signedIn');
              return;
            } catch (error) {
              if (isNetworkError(error)) throw error;
            }
          }
          // Токены отозваны или истекли — вход заново.
          await clearTokens();
          applyUser(null);
          setStatus('signedOut');
        },
      }),
    [applyUser],
  );
  const api = useMemo(() => makeApi(socket), [socket]);

  useEffect(() => {
    // Старт: с токенами сразу открываем приложение (последний профиль из кэша),
    // сеть подтянет актуальное; без токенов — экран входа.
    loadTokens().then((tokens) => {
      const cached = cachedUser();
      if (tokens && cached) {
        setUserState(cached);
        setStatus('signedIn');
      } else if (!tokens) {
        setStatus('signedOut');
      }
    });
    const offState = socket.onState((state) => {
      setConn(state);
      setServerUrl(socket.url);
      setConnError(socket.lastError);
      if (state === 'open') setEverConnected(true);
    });
    socket.start();
    return () => {
      offState();
      socket.stop();
    };
  }, [socket]);

  const login = useCallback(
    async (email: string, password: string): Promise<LoginStep> => {
      const address = email.trim().toLowerCase();
      try {
        const payload = await api.login(address, password, deviceName(), deviceId());
        await signedIn(payload);
        return { kind: 'done' };
      } catch (error) {
        if (error instanceof AuraError && error.code === 'requires_2fa') {
          pending.current = { email: address, password };
          return { kind: 'twoFactor' };
        }
        if (error instanceof AuraError && error.code === 'email_not_verified') {
          pending.current = { email: address, password };
          setPendingEmail(address);
          const sent = await api.resendCode(address).catch(() => ({ email_code: undefined }));
          return { kind: 'verifyEmail', devCode: sent.email_code };
        }
        throw error;
      }
    },
    [api, signedIn],
  );

  const login2fa = useCallback(
    async (code: string, trustDevice: boolean) => {
      const creds = pending.current;
      if (!creds) throw new AuraError('bad_request', 'Начните вход заново');
      const payload = await api.login2fa(creds.email, creds.password, code.trim().toUpperCase(), trustDevice, deviceName(), deviceId());
      await signedIn(payload);
    },
    [api, signedIn],
  );

  const register = useCallback(
    async (name: string, email: string, password: string) => {
      const address = email.trim().toLowerCase();
      const result = await api.register(name.trim(), address, password);
      pending.current = { email: address, password };
      setPendingEmail(address);
      return { devCode: result.email_code };
    },
    [api],
  );

  const verifyEmail = useCallback(
    async (code: string): Promise<LoginStep> => {
      const creds = pending.current;
      const email = creds?.email ?? pendingEmail;
      await api.verifyEmail(email, code);
      setPendingEmail('');
      // Пароль ещё в памяти — сразу входим, без повторного ввода.
      if (creds) return login(creds.email, creds.password);
      return { kind: 'done' };
    },
    [api, login, pendingEmail],
  );

  const resendCode = useCallback(async () => {
    const email = pending.current?.email ?? pendingEmail;
    const result = await api.resendCode(email);
    return { devCode: result.email_code };
  }, [api, pendingEmail]);

  const forgotPassword = useCallback(
    async (email: string) => {
      const result = await api.forgotPassword(email.trim().toLowerCase());
      return { devCode: result.reset_code };
    },
    [api],
  );

  const resetPassword = useCallback(
    async (email: string, code: string, newPassword: string) => {
      await api.resetPassword(email.trim().toLowerCase(), code, newPassword);
      return login(email, newPassword);
    },
    [api, login],
  );

  const logout = useCallback(async () => {
    try {
      await api.logout();
    } catch {
      /* офлайн — токен всё равно удаляем с устройства */
    }
    await signedOut();
  }, [api, signedOut]);

  const setServer = useCallback(
    (input: string | null) => {
      const url = input ? normalizeServerUrl(input) : null;
      if (input && !url) return null;
      saveOverride(url);
      setOverride(url);
      overrideRef.current = url;
      socket.reconnect();
      return url ?? serverCandidates(null, currentEnv())[0];
    },
    [socket],
  );

  const value: Session = {
    api,
    socket,
    conn,
    everConnected,
    serverUrl,
    connError,
    status,
    user,
    pendingEmail,
    login,
    login2fa,
    register,
    verifyEmail,
    resendCode,
    forgotPassword,
    resetPassword,
    logout,
    setUser: applyUser,
    setServer,
    retry: () => socket.retryNow(),
  };
  return <Ctx.Provider value={value}>{children}</Ctx.Provider>;
}

export function useSession(): Session {
  const value = useContext(Ctx);
  if (!value) throw new Error('useSession outside SessionProvider');
  return value;
}

/** Понятный текст ошибки для формы: сетевые — своими словами, остальное — как сказал сервер. */
export function errorText(error: unknown): string {
  if (error instanceof AuraError) {
    switch (error.code) {
      case 'offline':
      case 'disconnected':
        return 'Нет связи с сервером Aura';
      case 'timeout':
        return 'Сервер не ответил вовремя — попробуйте ещё раз';
      case 'conflict':
        return 'Этот email уже зарегистрирован';
      default: {
        const text = error.message || 'Что-то пошло не так';
        return text.charAt(0).toUpperCase() + text.slice(1);
      }
    }
  }
  return 'Что-то пошло не так';
}
