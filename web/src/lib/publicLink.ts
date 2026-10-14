/*
  Публичные ссылки веб-клиента: приглашение (#/i/<токен>), страница записи
  (#/b/<slug>), управление записью (#/m/<токен>), регистрация по приглашению
  коллеги (#/signup?ref=<код>).

  Правило одно для всех: готовая ссылка от сервера (AURA_PUBLIC_URL) — как есть;
  иначе — адрес этого веб-клиента, если его откроет другой человек (http(s), не
  localhost); иначе — только идентификатор, а страница подскажет про
  AURA_PUBLIC_URL. Проверяется юнит-тестами (invite.test.ts, booking.test.ts).
*/

export interface LocationLike {
  protocol: string;
  hostname: string;
  origin: string;
  pathname: string;
}

/** Ссылка или — если её не собрать — голый идентификатор (токен, slug или код). */
export type PublicLink = { kind: 'url'; url: string } | { kind: 'token'; token: string };

/** Адрес, который откроется у другого человека: http(s) и не localhost/loopback (Capacitor — https://localhost). */
export function isPublicWebOrigin(loc: LocationLike | null | undefined): boolean {
  if (!loc || (loc.protocol !== 'http:' && loc.protocol !== 'https:')) return false;
  const host = loc.hostname.toLowerCase().replace(/^\[|\]$/g, '');
  if (!host) return false;
  if (host === 'localhost' || host.endsWith('.localhost')) return false;
  if (host === '::1' || host === '0.0.0.0' || /^127\./.test(host)) return false;
  return true;
}

/**
  Ссылка на публичную страницу веб-клиента.
  `route` — путь внутри hash-роутера без «#», например `/b/k7m2q9xw4p`;
  `id` — что показать, если ссылку не собрать.
*/
export function publicLink(route: string, id: string, serverUrl: string, loc: LocationLike | null | undefined): PublicLink {
  const url = (serverUrl ?? '').trim();
  if (/^https?:\/\//i.test(url)) return { kind: 'url', url };
  if (loc && isPublicWebOrigin(loc)) return { kind: 'url', url: `${loc.origin}${loc.pathname}#${route}` };
  return { kind: 'token', token: id };
}

/** Текущий адрес страницы (в тестах и вне браузера — null). */
export const currentLocation = (): LocationLike | null => (typeof location === 'undefined' ? null : location);

/** Текст, который копируется / показывается в поле ссылки. */
export const linkText = (link: PublicLink) => (link.kind === 'url' ? link.url : link.token);
