import { describe, expect, it, vi } from 'vitest';
import { makeBookingApi } from './bookingApi';
import type { AuraSocket } from './socket';

const PAGE = { title: 'Консультация', description: '', duration_minutes: 30, days_ahead: 14, max_per_day: 4, notice_hours: 2, max_series: 1, weekends: false };

describe('makeBookingApi', () => {
  it('шлёт booking.* и referral.get с полями ровно как в протоколе', async () => {
    const request = vi.fn().mockResolvedValue({});
    const api = makeBookingApi({ request } as unknown as AuraSocket);
    await api.createPage(PAGE);
    await api.listPages();
    await api.updatePage(3, { status: 'paused' });
    await api.deletePage(3);
    await api.list();
    await api.ownerCancel(9);
    await api.ownerCancel(9, '2026-10-20T07:00:00Z');
    await api.referral();
    await api.get('abcdefghij');
    await api.book('abcdefghij', { start: '2026-10-20T07:00:00Z', guest_name: 'Аня' });
    await api.book('abcdefghij', { start: '2026-10-20T07:00:00Z', guest_name: 'Аня', guest_email: 'a@b.ru', comment: 'Привет', count: 3 });
    await api.manage('tok');
    await api.reschedule('tok', '2026-10-21T07:00:00Z');
    await api.reschedule('tok', '2026-10-21T07:00:00Z', '2026-10-20T07:00:00Z');
    await api.cancel('tok');
    await api.cancel('tok', { occurrence: '2026-10-27T07:00:00Z', reason: 'Заболела' });
    await api.resendLink('abcdefghij', 'a@b.ru');
    expect(request.mock.calls).toEqual([
      ['booking.pageCreate', PAGE],
      ['booking.pageList', {}],
      ['booking.pageUpdate', { page_id: 3, status: 'paused' }],
      ['booking.pageDelete', { page_id: 3 }],
      ['booking.list', { limit: 50 }],
      ['booking.ownerCancel', { booking_id: 9 }],
      ['booking.ownerCancel', { booking_id: 9, occurrence: '2026-10-20T07:00:00Z' }],
      ['referral.get', {}],
      ['booking.get', { slug: 'abcdefghij' }, 60_000],
      ['booking.book', { slug: 'abcdefghij', start: '2026-10-20T07:00:00Z', guest_name: 'Аня' }, 60_000],
      ['booking.book', { slug: 'abcdefghij', start: '2026-10-20T07:00:00Z', guest_name: 'Аня', guest_email: 'a@b.ru', comment: 'Привет', count: 3 }, 60_000],
      ['booking.manage', { token: 'tok' }, 60_000],
      ['booking.reschedule', { token: 'tok', start: '2026-10-21T07:00:00Z' }, 60_000],
      ['booking.reschedule', { token: 'tok', start: '2026-10-21T07:00:00Z', occurrence: '2026-10-20T07:00:00Z' }, 60_000],
      ['booking.cancel', { token: 'tok' }],
      ['booking.cancel', { token: 'tok', occurrence: '2026-10-27T07:00:00Z', reason: 'Заболела' }],
      ['booking.resendLink', { slug: 'abcdefghij', email: 'a@b.ru' }],
    ]);
  });
});
