/*
  Страницы записи (docs/PROTOCOL.md → «Страницы записи», booking.*) и
  реферальная программа (referral.*).

  Владелец (нужен вход): booking.pageCreate / pageList / pageUpdate / pageDelete,
  booking.list, booking.ownerCancel, referral.get.
  Гость (без входа): booking.get (по slug страницы), booking.book, booking.manage /
  reschedule / cancel (по токену ссылки управления — сервер отдаёт его один раз,
  в ответе booking.book; в БД хранится только хэш).
*/
import type { SelfvoySocket } from './socket';
import type { InviteSlot } from './inviteApi';

export type BookingPageStatus = 'active' | 'paused' | 'archived';
export type BookingStatus = 'active' | 'cancelled';
export type OccurrenceStatus = 'active' | 'cancelled';

/** Страница записи глазами владельца. `url` пуст, если на сервере не задан SELFVOY_PUBLIC_URL. */
export interface BookingPage {
  id: number;
  slug: string;
  url: string;
  title: string;
  description: string;
  duration_minutes: number;
  days_ahead: number;
  max_per_day: number;
  notice_hours: number;
  max_series: number;
  weekends: boolean;
  status: BookingPageStatus;
  /** Предстоящих встреч по этой странице. */
  upcoming: number;
  created_at: string;
  updated_at: string;
}

export interface Occurrence extends InviteSlot {
  status: OccurrenceStatus;
  /** Местное время владельца «YYYY-MM-DDTHH:MM» (старые записи — пусто). */
  local?: string;
}

/** Запись гостя глазами владельца. */
export interface Booking {
  id: number;
  page_id: number;
  page_title: string;
  guest_name: string;
  guest_email: string;
  guest_comment: string;
  status: BookingStatus;
  cancelled_by: '' | 'guest' | 'owner';
  reschedules: number;
  occurrences: Occurrence[];
  created_at: string;
  updated_at: string;
}

export interface GuestSlot extends InviteSlot {
  /** Местная дата владельца (YYYY-MM-DD). */
  day?: string;
}

/** Страница записи глазами гостя: без email владельца и чужих записей. */
export interface GuestPage {
  slug: string;
  title: string;
  description: string;
  duration_minutes: number;
  max_series: number;
  owner_name: string;
  owner_timezone: string;
  status: 'active' | 'paused';
  slots: GuestSlot[];
  /** Сервер не смог подобрать время (AI-сервис недоступен) — показать «попробуйте позже». */
  slots_unavailable: boolean;
  /** Реферальный код владельца — для ссылки «Заведите свой Selfvoy». */
  ref: string;
}

/** Запись глазами гостя (ссылка управления #/m/<токен>). */
export interface GuestBooking {
  page_title: string;
  /** Адрес страницы записи — пусто, если запись на ней сейчас закрыта. */
  page_slug: string;
  owner_name: string;
  owner_timezone: string;
  duration_minutes: number;
  guest_name: string;
  status: BookingStatus;
  cancelled_by: '' | 'guest' | 'owner';
  occurrences: Occurrence[];
  /** Старое поле: равно can_reschedule (оставлено для совместимости). */
  can_change: boolean;
  /**
    Можно отменить: запись активна и есть предстоящая встреча — даже если страница
    на паузе или удалена. Старый сервер не присылает — тогда смотрим can_change.
  */
  can_cancel?: boolean;
  /** Можно перенести: страница открыта, запись активна, есть предстоящая встреча и переносы. */
  can_reschedule?: boolean;
  reschedules_left: number;
  /** Свободное время для переноса ближайшей встречи. */
  slots: InviteSlot[];
  slots_unavailable: boolean;
  ref: string;
}

export interface PageFields {
  title: string;
  description?: string;
  duration_minutes: number;
  days_ahead: number;
  max_per_day: number;
  notice_hours: number;
  max_series: number;
  weekends: boolean;
}

export interface BookRequest {
  start: string;
  guest_name: string;
  guest_email?: string;
  comment?: string;
  /** Встреч в еженедельной серии (1 — одна встреча). */
  count?: number;
}

export interface Booked {
  booking: GuestBooking;
  /** 43 символа base64url; показывается только сейчас. */
  token: string;
  /** Готовая ссылка управления или "" — если на сервере не задан SELFVOY_PUBLIC_URL. */
  url: string;
}

export interface ReferralInfo {
  code: string;
  url: string;
  joined: number;
  verified: number;
  referrals: { display_name: string; source: 'link' | 'booking' | 'invite'; joined_at: string; verified: boolean }[];
}

/** Подбор свободного времени идёт через ИИ-сервис — гостевым запросам даём больше времени. */
const SLOTS_TIMEOUT_MS = 60_000;

export function makeBookingApi(socket: SelfvoySocket) {
  const call = <T,>(type: string, payload: Record<string, unknown> = {}, timeout?: number) =>
    timeout === undefined ? socket.request<T>(type, payload) : socket.request<T>(type, payload, timeout);
  return {
    // -------- владелец
    createPage: (fields: PageFields) => call<{ page: BookingPage }>('booking.pageCreate', { ...fields }),
    listPages: () => call<{ pages: BookingPage[] }>('booking.pageList'),
    updatePage: (page_id: number, fields: Partial<PageFields> & { status?: 'active' | 'paused' }) =>
      call<{ page: BookingPage }>('booking.pageUpdate', { page_id, ...fields }),
    deletePage: (page_id: number) => call<{ deleted: boolean }>('booking.pageDelete', { page_id }),
    list: (limit = 50) => call<{ bookings: Booking[] }>('booking.list', { limit }),
    ownerCancel: (booking_id: number, occurrence?: string) => {
      const payload: Record<string, unknown> = { booking_id };
      if (occurrence) payload.occurrence = occurrence;
      return call<{ booking: Booking }>('booking.ownerCancel', payload);
    },
    referral: () => call<ReferralInfo>('referral.get'),

    // -------- гость (публичные методы)
    get: (slug: string) => call<{ page: GuestPage }>('booking.get', { slug }, SLOTS_TIMEOUT_MS),
    book: (slug: string, request: BookRequest) => {
      const payload: Record<string, unknown> = { slug, start: request.start, guest_name: request.guest_name };
      if (request.guest_email) payload.guest_email = request.guest_email;
      if (request.comment) payload.comment = request.comment;
      if (request.count && request.count > 1) payload.count = request.count;
      return call<Booked>('booking.book', payload, SLOTS_TIMEOUT_MS);
    },
    manage: (token: string) => call<{ booking: GuestBooking }>('booking.manage', { token }, SLOTS_TIMEOUT_MS),
    reschedule: (token: string, start: string, occurrence?: string) => {
      const payload: Record<string, unknown> = { token, start };
      if (occurrence) payload.occurrence = occurrence;
      return call<{ booking: GuestBooking }>('booking.reschedule', payload, SLOTS_TIMEOUT_MS);
    },
    /** Потерянная ссылка: новая уходит на email гостя (ответ одинаков, есть ли запись). */
    resendLink: (slug: string, email: string) => call<{ sent: boolean }>('booking.resendLink', { slug, email }),
    cancel: (token: string, options: { occurrence?: string; reason?: string } = {}) => {
      const payload: Record<string, unknown> = { token };
      if (options.occurrence) payload.occurrence = options.occurrence;
      if (options.reason) payload.reason = options.reason;
      return call<{ booking: GuestBooking }>('booking.cancel', payload);
    },
  };
}

export type BookingApi = ReturnType<typeof makeBookingApi>;
