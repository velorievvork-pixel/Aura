/*
  Хранение токенов (access JWT + refresh).

  - Desktop (Electron): шифрование ОС через safeStorage (Keychain / DPAPI /
    libsecret) — preload отдаёт window.auraSecure, токены лежат в userData
    зашифрованными.
  - Телефон (Capacitor) и браузер: localStorage WebView — он изолирован в
    песочнице приложения; на рутованном устройстве или в общем браузере это
    слабее Keychain/Keystore (см. web/README.md).

  Пароль не хранится нигде и никогда.
*/

export interface SecureBridge {
  get(key: string): Promise<string | null>;
  set(key: string, value: string): Promise<void>;
  remove(key: string): Promise<void>;
  /** true, если ОС действительно шифрует значения. */
  encrypted(): Promise<boolean>;
}

declare global {
  interface Window {
    auraSecure?: SecureBridge;
  }
}

const PREFIX = 'aura.secure.';

const localBridge: SecureBridge = {
  async get(key) {
    try {
      return localStorage.getItem(PREFIX + key);
    } catch {
      return null;
    }
  },
  async set(key, value) {
    try {
      localStorage.setItem(PREFIX + key, value);
    } catch {
      /* хранилище недоступно (приватный режим) — токен живёт до перезапуска */
    }
  },
  async remove(key) {
    try {
      localStorage.removeItem(PREFIX + key);
    } catch {
      /* нечего удалять */
    }
  },
  async encrypted() {
    return false;
  },
};

export const secureStore: SecureBridge = typeof window !== 'undefined' && window.auraSecure ? window.auraSecure : localBridge;

export interface Tokens {
  access: string;
  refresh: string;
}

export async function loadTokens(): Promise<Tokens | null> {
  const [access, refresh] = await Promise.all([secureStore.get('access'), secureStore.get('refresh')]);
  if (!access && !refresh) return null;
  return { access: access ?? '', refresh: refresh ?? '' };
}

export async function saveTokens(tokens: Tokens): Promise<void> {
  await Promise.all([secureStore.set('access', tokens.access), secureStore.set('refresh', tokens.refresh)]);
}

export async function clearTokens(): Promise<void> {
  await Promise.all([secureStore.remove('access'), secureStore.remove('refresh')]);
}

/** Стабильный id устройства — для «доверенных устройств» 2FA. Не секрет. */
export function deviceId(): string {
  const key = 'aura.deviceId';
  try {
    let id = localStorage.getItem(key);
    if (!id) {
      id = crypto.randomUUID();
      localStorage.setItem(key, id);
    }
    return id;
  } catch {
    return 'web-ephemeral';
  }
}
