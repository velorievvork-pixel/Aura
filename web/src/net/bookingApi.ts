/*
  Запись по постоянной ссылке и реферальная программа (booking.*, referral.info).

  Владелец (нужен вход): booking.pages.list / save / delete / rotate, booking.list,
  booking.cancel, referral.info.
  Гость (без входа): booking.get / booking.book по slug страницы и
  booking.manage.get / reschedule / cancel по личному токену управления —
  работают и на неавторизованном соединении, и на соединении вошедшего.

  Токен управления сервер отдаёт один раз — в ответе booking.book (в БД — sha256).
*/
import type { AuraSocket } from './socket';

export interface Plan {
  name: 'free' | 'pro';
  /** ISO или "" — Pro не действует. */
  pro_until: string;
  max_pages: number;
  max_weeks: number;
}

export interface BookingPage {
  id: number;
  slug: string;
  /** Готовая ссылка или "" — если на сервере не задан AURA_PUBLIC_URL. */
  url: string;
  title: string;
  description: string;
  duration_minutes: number;
  window_days: number;
  min_notice_hours: number;
  buffer_minutes: number;
  /** Локальные часы владельца; [] — рабочие часы из настроек. */
  hours: number[];
  /** 0 = Пн … 6 = Вс. */
  weekdays: number[];
  allow_weekly: boolean;
  active: boolean;
  created_at: string;
  updated_at: string;
}

/** Поля booking.pages.save: без id — создать, с id — обновить. */
export interface PageInput {
  id?: number;
  title: string;
  description?: string;
  duration_minutes?: number;
  window_days?: number;
  min_notice_hours?: number;
  buffer_minutes?: number;
  hours?: number[];
  weekdays?: number[];
  allow_weekly?: boolean;
  active?: boolean;
}

export type OccurrenceStatus = 'booked' | 'cancelled';

export interface Occurrence {
  index: number;
  start: string;
  end: string;
  status: OccurrenceStatus;
}

export type BookingStatus = 'active' | 'cancelled';

/** Запись глазами владельца; occurrences — только из окна booking.list. */
export interface Booking {
  id: number;
  page_id: number | null;
  title: string;
  guest_name: string;
  guest_email: string;
  guest_comment: string;
  duration_minutes: number;
  weekly: boolean;
  weeks: number;
  status: BookingStatus;
  created_at: string;
  occurrences: Occurrence[];
}

/** Страница записи глазами гостя: без email владельца. */
export interface PublicPage {
  title: string;
  description: string;
  duration_minutes: number;
  owner_name: string;
  owner_timezone: string;
  allow_weekly: boolean;
  max_weeks: number;
  /** Код владельца для «Заведите свою Ауру»; может быть "". */
  referral_code: string;
}

/** Запись глазами гостя: occurrences — все будущие и прошедшие за последние сутки. */
export interface GuestBooking {
  title: string;
  owner_name: string;
  owner_timezone: string;
  duration_minutes: number;
  weekly: boolean;
  weeks: number;
  status: BookingStatus;
  guest_name: string;
  occurrences: Occurrence[];
  referral_code: string;
}

export interface Slot {
  start: string;
  end: string;
}

export interface BookInput {
  /** Ровно start одного из текущих slots. */
  start: string;
  guest_name: string;
  guest_email?: string;
  comment?: string;
  weekly?: boolean;
}

export interface Booked {
  booking: GuestBooking;
  /** 43 символа base64url; показывается только сейчас. */
  manage_token: string;
  /** Готовая ссылка или "". */
  manage_url: string;
  /** Недели серии, где время уже занято (созданы отменёнными). */
  skipped: string[];
}

export interface ReferralInfo {
  code: string;
  /** Готовая ссылка или "" (клиент строит сам). */
  url: string;
  invited: number;
  qualified: number;
  reward_days: number;
  rewards_left: number;
  plan: Plan;
}

export function makeBookingApi(socket: AuraSocket) {
  const call = <T,>(type: string, payload: Record<string, unknown> = {}) => socket.request<T>(type, payload);
  return {
    // -------- владелец
    pages: () => call<{ pages: BookingPage[]; plan: Plan }>('booking.pages.list'),
    savePage: (page: PageInput) => {
      const payload: Record<string, unknown> = {};
      for (const [key, value] of Object.entries(page)) if (value !== undefined) payload[key] = value;
      return call<{ page: BookingPage }>('booking.pages.save', payload);
    },
    deletePage: (id: number) => call<{ deleted: boolean }>('booking.pages.delete', { id }),
    rotatePage: (id: number) => call<{ page: BookingPage }>('booking.pages.rotate', { id }),
    list: (days = 30, include_cancelled = false) => call<{ bookings: Booking[] }>('booking.list', { days, include_cancelled }),
    /** Без index — вся запись. */
    cancel: (booking_id: number, index?: number) => call<{ booking: Booking }>('booking.cancel', index === undefined ? { booking_id } : { booking_id, index }),
    referral: () => call<ReferralInfo>('referral.info'),

    // -------- гость (публичные методы)
    get: (slug: string) => call<{ page: PublicPage; slots: Slot[] }>('booking.get', { slug }),
    book: (slug: string, input: BookInput) => {
      const payload: Record<string, unknown> = { slug, start: input.start, guest_name: input.guest_name };
      if (input.guest_email) payload.guest_email = input.guest_email;
      if (input.comment) payload.comment = input.comment;
      if (input.weekly) payload.weekly = true;
      return call<Booked>('booking.book', payload);
    },
    manage: (token: string) => call<{ booking: GuestBooking; slots: Slot[] }>('booking.manage.get', { token }),
    reschedule: (token: string, index: number, start: string) => call<{ booking: GuestBooking }>('booking.manage.reschedule', { token, index, start }),
    /** Без index — вся запись. */
    guestCancel: (token: string, index?: number) => call<{ booking: GuestBooking }>('booking.manage.cancel', index === undefined ? { token } : { token, index }),
  };
}

export type BookingApi = ReturnType<typeof makeBookingApi>;
