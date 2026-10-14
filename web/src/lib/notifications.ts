/*
  Подписи уведомлений во «Входящих»: короткая метка вида, строка подробностей
  из payload (без технических полей) и экран, куда ведёт уведомление.
  Проверяется юнит-тестами (notifications.test.ts).
*/
import type { Notice } from '../net/api';
import { plural, when } from './format';
import { dateLabel } from './time';

const KIND_LABELS: Record<string, string> = {
  'task.due': 'Напоминание',
  'confirmation.requested': 'Нужно решение',
  'a2a.proposal': 'Переговоры',
  'login.new': 'Новый вход',
  'twofactor.enabled': '2FA включена',
  'twofactor.disabled': '2FA выключена',
  'invite.accepted': 'Приглашение: время выбрано',
  'invite.declined': 'Приглашение: отказ',
  'booking.created': 'Новая запись',
  'booking.rescheduled': 'Перенос занятия',
  'booking.cancelled': 'Отмена записи',
  'booking.reminder': 'Скоро занятие',
  'referral.rewarded': 'Бонус Pro',
};

/** «Новая запись», «Перенос занятия»…; неизвестный вид — "". */
export const noticeKindLabel = (kind: string) => KIND_LABELS[kind] ?? '';

const str = (v: unknown) => (typeof v === 'string' ? v.trim() : '');
const num = (v: unknown) => (typeof v === 'number' && Number.isFinite(v) ? v : null);

function dateWithYear(iso: string, now: Date): string {
  const d = new Date(iso);
  if (Number.isNaN(d.getTime())) return '';
  return d.getFullYear() === now.getFullYear() ? dateLabel(d) : `${dateLabel(d)} ${d.getFullYear()}`;
}

/** Строка под заголовком: собрана из payload; если собрать нечего — body от сервера. */
export function noticeDetail(n: Pick<Notice, 'kind' | 'body' | 'payload'>, now = new Date()): string {
  const p = (n.payload ?? {}) as Record<string, unknown>;
  const body = (n.body ?? '').trim();
  const join = (...parts: string[]) => parts.filter(Boolean).join(' · ');
  switch (n.kind) {
    case 'login.new': {
      // Устройство, а не сетевой адрес.
      const device = str(p.device);
      return device ? `Устройство: ${device}` : '';
    }
    case 'booking.created': {
      const start = str(p.start);
      return join(body, start ? when(start, now) : '', p.weekly === true ? 'каждую неделю' : '') || body;
    }
    case 'booking.rescheduled': {
      const from = str(p.from);
      const to = str(p.to);
      const guest = str(p.guest_name);
      if (!from || !to) return body;
      return join(guest, `${when(from, now)} → ${when(to, now)}`);
    }
    case 'booking.cancelled': {
      const index = num(p.index);
      const guest = str(p.guest_name);
      if (index === null) return body;
      return join(guest, index < 0 ? 'отменена вся запись' : `отменено занятие №${index + 1}`);
    }
    case 'booking.reminder': {
      const start = str(p.start);
      const guest = str(p.guest_name);
      return start ? join(guest, `начало ${when(start, now)}`) : body;
    }
    case 'referral.rewarded': {
      const days = num(p.days) ?? 30;
      const until = dateWithYear(str(p.pro_until), now);
      const bonus = `+${days} ${plural(days, 'день', 'дня', 'дней')} Pro${until ? `, до ${until}` : ''}`;
      if (p.role === 'referrer') return `Приглашённый коллега начал пользоваться Aura: ${bonus}`;
      if (p.role === 'invitee') return `Вы пришли по приглашению коллеги: ${bonus}`;
      return bonus;
    }
    case 'invite.accepted': {
      const start = str(p.start);
      return start ? join(str(p.guest_name), when(start, now)) : body;
    }
    case 'invite.declined': {
      const comment = str(p.comment);
      const guest = str(p.guest_name);
      if (!comment && !guest) return body;
      return join(guest ? `${guest}: не получится` : 'Не получится', comment ? `«${comment}»` : '');
    }
    default:
      return body;
  }
}

/** Экран, где с уведомлением можно что-то сделать; null — некуда. */
export function noticeTarget(kind: string): string | null {
  if (kind.startsWith('booking.')) return '/booking';
  if (kind === 'referral.rewarded') return '/settings';
  if (kind.startsWith('invite.') || kind === 'a2a.proposal') return '/negotiations';
  if (kind === 'task.due') return '/tasks';
  return null;
}
