import { describe, expect, it } from 'vitest';
import { noticeDetail, noticeKindLabel, noticeTarget } from './notifications';

const now = new Date('2026-10-14T12:00:00');
const n = (kind: string, payload: Record<string, unknown> = {}, body = '') => ({ kind, payload, body });

describe('уведомления во «Входящих»', () => {
  it('подписи новых видов', () => {
    expect(noticeKindLabel('booking.created')).toBe('Новая запись');
    expect(noticeKindLabel('booking.rescheduled')).toBe('Перенос занятия');
    expect(noticeKindLabel('booking.cancelled')).toBe('Отмена записи');
    expect(noticeKindLabel('booking.reminder')).toBe('Скоро занятие');
    expect(noticeKindLabel('referral.rewarded')).toBe('Бонус Pro');
    expect(noticeKindLabel('invite.accepted')).toBeTruthy();
    expect(noticeKindLabel('invite.declined')).toBeTruthy();
    expect(noticeKindLabel('что-то.новое')).toBe('');
  });

  it('запись: название страницы, время, серия', () => {
    expect(noticeDetail(n('booking.created', { booking_id: 9, start: '2026-10-15T16:00:00', weekly: true, guest_name: 'Маша' }, 'Математика'), now)).toBe(
      'Математика · завтра, 16:00 · каждую неделю',
    );
    expect(noticeDetail(n('booking.created', { start: '2026-10-14T18:00:00', weekly: false }, 'Математика'), now)).toBe('Математика · сегодня, 18:00');
  });

  it('перенос, отмена, напоминание', () => {
    expect(noticeDetail(n('booking.rescheduled', { index: 2, from: '2026-10-15T16:00:00', to: '2026-10-17T12:00:00', guest_name: 'Маша' }), now)).toBe(
      'Маша · завтра, 16:00 → сб, 17 октября, 12:00',
    );
    expect(noticeDetail(n('booking.cancelled', { index: -1, guest_name: 'Маша' }), now)).toBe('Маша · отменена вся запись');
    expect(noticeDetail(n('booking.cancelled', { index: 0, guest_name: 'Маша' }), now)).toBe('Маша · отменено занятие №1');
    expect(noticeDetail(n('booking.reminder', { index: 0, start: '2026-10-14T12:15:00', guest_name: 'Маша' }), now)).toBe('Маша · начало сегодня, 12:15');
  });

  it('реферальный бонус', () => {
    expect(noticeDetail(n('referral.rewarded', { days: 30, pro_until: '2026-11-13T12:00:00', role: 'referrer' }), now)).toBe(
      'Приглашённый коллега начал пользоваться Aura: +30 дней Pro, до 13 ноября',
    );
    expect(noticeDetail(n('referral.rewarded', { days: 30, pro_until: '2027-01-13T12:00:00', role: 'invitee' }), now)).toBe(
      'Вы пришли по приглашению коллеги: +30 дней Pro, до 13 января 2027',
    );
  });

  it('приглашения и прочее', () => {
    expect(noticeDetail(n('invite.accepted', { start: '2026-10-15T10:00:00', guest_name: 'Аня' }), now)).toBe('Аня · завтра, 10:00');
    expect(noticeDetail(n('invite.declined', { guest_name: 'Аня', comment: 'Уезжаю' }), now)).toBe('Аня: не получится · «Уезжаю»');
    expect(noticeDetail(n('login.new', { device: 'Chrome', ip: '1.2.3.4' }, '1.2.3.4'), now)).toBe('Устройство: Chrome');
    expect(noticeDetail(n('task.due', {}, 'Купить хлеб'), now)).toBe('Купить хлеб');
    // payload без нужных полей — текст от сервера
    expect(noticeDetail(n('booking.rescheduled', {}, 'Маша перенесла занятие'), now)).toBe('Маша перенесла занятие');
    expect(noticeDetail({ kind: 'booking.cancelled', body: 'Отмена' }, now)).toBe('Отмена');
  });

  it('куда ведёт', () => {
    expect(noticeTarget('booking.created')).toBe('/booking');
    expect(noticeTarget('referral.rewarded')).toBe('/settings');
    expect(noticeTarget('invite.accepted')).toBe('/negotiations');
    expect(noticeTarget('login.new')).toBeNull();
  });
});
