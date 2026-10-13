/// <reference types="vite/client" />

interface ImportMetaEnv {
  /** Основной адрес сервера Aura (ws:// или wss://), задаётся при сборке. */
  readonly VITE_AURA_SERVER_URL?: string;
  /** Запасной адрес: клиент переключается на него, если основной не отвечает. */
  readonly VITE_AURA_FALLBACK_URL?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
