/*
  Адрес сервера Aura (WebSocket). Порядок выбора:

  1. адрес, который пользователь ввёл сам («Адрес сервера» на экране входа
     или в настройках) — хранится на устройстве;
  2. VITE_AURA_SERVER_URL — задаётся при сборке (.env или переменная CI),
     для продакшна это wss://… за TLS-прокси;
  3. VITE_AURA_FALLBACK_URL — запасной адрес: если основной не отвечает,
     клиент сам переключается на него;
  4. платформенный дефолт для разработки: на Android-эмуляторе компьютер
     разработчика — это 10.0.2.2 (localhost там — сам телефон), в браузере,
     открытом с настоящего хоста по https, — тот же хост по wss, иначе
     ws://127.0.0.1:9000.

  Реальному телефону localhost/10.0.2.2 не подходят: нужен IP компьютера в
  той же Wi-Fi сети (ws://192.168.x.x:9000) или публичный wss-адрес.
*/
import { Capacitor } from '@capacitor/core';

const OVERRIDE_KEY = 'aura.server';
export const DEV_PORT = 9000;

export interface ServerEnv {
  primary?: string;
  fallback?: string;
  platform: string;
  location?: { protocol: string; hostname: string; host: string };
}

/**
 * Приводит ввод пользователя к ws(s)-адресу: «192.168.1.5» → ws://192.168.1.5:9000,
 * «https://aura.example.com» → wss://aura.example.com. Неразборчивый ввод → null.
 */
export function normalizeServerUrl(input: string): string | null {
  let value = input.trim();
  if (!value) return null;
  if (/^https?:\/\//i.test(value)) value = value.replace(/^http/i, 'ws');
  else if (/^[a-z][a-z0-9+.-]*:\/\//i.test(value) && !/^wss?:\/\//i.test(value)) return null; // ftp://, file://…
  if (!/^wss?:\/\//i.test(value)) {
    const hasPort = /:\d+$/.test(value.split('/')[0]);
    value = `ws://${value}${hasPort ? '' : `:${DEV_PORT}`}`;
  }
  try {
    const url = new URL(value);
    if (url.protocol !== 'ws:' && url.protocol !== 'wss:') return null;
    if (!url.hostname) return null;
    // URL добавляет «/» к пустому пути — для наглядности убираем его.
    return url.toString().replace(/\/$/, '');
  } catch {
    return null;
  }
}

/** Платформенный адрес по умолчанию (без пользовательских настроек и переменных сборки). */
export function platformDefault(env: ServerEnv): string {
  if (env.platform === 'android') return `ws://10.0.2.2:${DEV_PORT}`;
  const loc = env.location;
  if (loc && loc.protocol === 'https:' && loc.hostname && !['localhost', '127.0.0.1'].includes(loc.hostname)) {
    // Веб-версия на настоящем хосте: сервер за тем же TLS-прокси.
    return `wss://${loc.host}`;
  }
  return `ws://127.0.0.1:${DEV_PORT}`;
}

/** Кандидаты по порядку; без повторов и без пустых значений. */
export function serverCandidates(override: string | null, env: ServerEnv): string[] {
  // Если пользователь задал адрес сам — подключаемся только к нему: молча уходить
  // на другой сервер было бы сюрпризом. Неразборчивый адрес игнорируется.
  const own = override ? normalizeServerUrl(override) : null;
  if (own) return [own];
  const list = [env.primary, env.fallback]
    .map((v) => (v ? normalizeServerUrl(v) : null))
    .filter((v): v is string => Boolean(v));
  list.push(platformDefault(env));
  return [...new Set(list)];
}

export function currentEnv(): ServerEnv {
  return {
    primary: import.meta.env.VITE_AURA_SERVER_URL,
    fallback: import.meta.env.VITE_AURA_FALLBACK_URL,
    platform: Capacitor.getPlatform(),
    location: typeof location === 'undefined' ? undefined : { protocol: location.protocol, hostname: location.hostname, host: location.host },
  };
}

export function loadOverride(): string | null {
  try {
    return localStorage.getItem(OVERRIDE_KEY);
  } catch {
    return null;
  }
}

export function saveOverride(url: string | null): void {
  try {
    if (url) localStorage.setItem(OVERRIDE_KEY, url);
    else localStorage.removeItem(OVERRIDE_KEY);
  } catch {
    /* хранилище недоступно — адрес действует до перезапуска */
  }
}
