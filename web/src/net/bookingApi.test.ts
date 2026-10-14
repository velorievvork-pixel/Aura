import { describe, expect, it, vi } from 'vitest';
import { makeBookingApi } from './bookingApi';
import type { AuraSocket } from './socket';

describe('makeBookingApi', () => {
  it('шлёт booking.* и referral.info с полями ровно как в протоколе', async () => {
    const request = vi.fn().mockResolvedValue({});
    const api = makeBookingApi({ request } as unknown as AuraSocket);
    await api.pages();
    await api.savePage({ title: 'Математика', duration_minutes: 60, hours: [15, 16], weekdays: [0, 1], allow_weekly: true, active: true });
    await api.savePage({ id: 3, title: 'Математика', description: undefined });
    await api.deletePage(3);
    await api.rotatePage(3);
    await api.list();
    await api.list(7, true);
    await api.cancel(9);
    await api.cancel(9, 0);
    await api.referral();
    await api.get('k7m2q9xw4p');
    await api.book('k7m2q9xw4p', { start: '2026-10-15T13:00:00Z', guest_name: 'Маша' });
    await api.book('k7m2q9xw4p', { start: '2026-10-15T13:00:00Z', guest_name: 'Маша', guest_email: 'm@x.ru', comment: 'Алгебра', weekly: true });
    await api.manage('tok');
    await api.reschedule('tok', 2, '2026-10-31T13:00:00Z');
    await api.guestCancel('tok', 0);
    await api.guestCancel('tok');
    expect(request.mock.calls).toEqual([
      ['booking.pages.list', {}],
      ['booking.pages.save', { title: 'Математика', duration_minutes: 60, hours: [15, 16], weekdays: [0, 1], allow_weekly: true, active: true }],
      ['booking.pages.save', { id: 3, title: 'Математика' }],
      ['booking.pages.delete', { id: 3 }],
      ['booking.pages.rotate', { id: 3 }],
      ['booking.list', { days: 30, include_cancelled: false }],
      ['booking.list', { days: 7, include_cancelled: true }],
      ['booking.cancel', { booking_id: 9 }],
      ['booking.cancel', { booking_id: 9, index: 0 }],
      ['referral.info', {}],
      ['booking.get', { slug: 'k7m2q9xw4p' }],
      ['booking.book', { slug: 'k7m2q9xw4p', start: '2026-10-15T13:00:00Z', guest_name: 'Маша' }],
      ['booking.book', { slug: 'k7m2q9xw4p', start: '2026-10-15T13:00:00Z', guest_name: 'Маша', guest_email: 'm@x.ru', comment: 'Алгебра', weekly: true }],
      ['booking.manage.get', { token: 'tok' }],
      ['booking.manage.reschedule', { token: 'tok', index: 2, start: '2026-10-31T13:00:00Z' }],
      ['booking.manage.cancel', { token: 'tok', index: 0 }],
      ['booking.manage.cancel', { token: 'tok' }],
    ]);
  });
});
