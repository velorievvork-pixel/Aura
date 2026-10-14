/*
  Управление записью — сторона гостя (#/m/<токен>).

  Личная ссылка из подтверждения записи: занятия (будущие и за последние сутки)
  в часовом поясе браузера, перенос одного занятия на свободное время страницы
  (slots из booking.manage.get), отмена одного занятия или всей записи, файл .ics
  с актуальным расписанием. Без входа и вне оболочки приложения.
*/
import { CalendarClock, X } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { useParams } from 'react-router-dom';
import { CalendarButton, GuestLayout, GuestLoading, GuestMessage, GuestOffline, OwnAura, RetryButton } from '../components/GuestLayout';
import { Spinner } from '../components/ui';
import { bookingIcs, bookingIcsFileName, durationLabel, guestOccurrences, lessonsCount, seriesPosition, weeklyPhrase, type GuestOccurrence } from '../lib/booking';
import { guestErrorKind, guestErrorText, isPlausibleToken } from '../lib/invite';
import { browserTimeZone, groupSlotsByDay, slotPhrase, timeZoneLabel } from '../lib/slots';
import { makeBookingApi, type GuestBooking, type Slot } from '../net/bookingApi';
import { useSession } from '../state/session';

const NOT_FOUND = 'Запись не найдена';

type Pending = { kind: 'move'; index: number } | { kind: 'cancel'; index: number } | { kind: 'cancelAll' } | null;

export function BookingManage() {
  const { token } = useParams();
  const { socket, conn, connError, retry } = useSession();
  const api = useMemo(() => makeBookingApi(socket), [socket]);
  const zone = useMemo(browserTimeZone, []);

  const [booking, setBooking] = useState<GuestBooking | null>(null);
  const [slots, setSlots] = useState<Slot[]>([]);
  const [missing, setMissing] = useState(!isPlausibleToken(token));
  const [loadError, setLoadError] = useState('');
  const [pending, setPending] = useState<Pending>(null);
  const [moveTo, setMoveTo] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const seq = useRef(0);
  const connRef = useRef(conn);
  connRef.current = conn;

  const load = useCallback(async () => {
    if (!isPlausibleToken(token)) return;
    const mine = ++seq.current;
    try {
      const reply = await api.manage(token);
      if (mine !== seq.current) return;
      setLoadError('');
      setBooking(reply.booking);
      setSlots(reply.slots ?? []);
    } catch (err) {
      if (mine !== seq.current) return;
      const kind = guestErrorKind(err);
      if (kind === 'notFound') setMissing(true);
      else if (kind !== 'network' || connRef.current === 'open') setLoadError(guestErrorText(err, NOT_FOUND));
    }
  }, [api, token]);

  useEffect(() => {
    if (conn === 'open') void load();
  }, [conn, load]);

  const groups = useMemo(() => groupSlotsByDay(slots, zone), [slots, zone]);
  const occurrences = useMemo(() => (booking ? guestOccurrences(booking) : []), [booking]);

  const close = () => {
    setPending(null);
    setMoveTo(null);
  };

  /** Действие гостя: ответ сервера — новая запись; потом свежие слоты для переноса. */
  const act = async (run: () => Promise<{ booking: GuestBooking }>, done: string) => {
    setBusy(true);
    setError('');
    setNotice('');
    try {
      const reply = await run();
      seq.current += 1;
      setBooking(reply.booking);
      setNotice(done);
      close();
      void load();
    } catch (err) {
      const kind = guestErrorKind(err);
      if (kind === 'notFound') setMissing(true);
      else {
        setError(guestErrorText(err, NOT_FOUND));
        // conflict: время заняли или занятие уже отменено — показываем, как есть сейчас.
        if (kind === 'conflict') {
          setMoveTo(null);
          void load();
        }
      }
    } finally {
      setBusy(false);
    }
  };

  if (missing) {
    return <GuestMessage title={NOT_FOUND}>Проверьте ссылку: возможно, она скопирована не полностью. Если запись делали недавно — ссылка была на экране «Готово» и в сохранённом файле.</GuestMessage>;
  }

  if (booking && token) {
    const owner = booking.owner_name;
    const active = booking.status !== 'cancelled';
    const changeable = occurrences.filter((o) => o.changeable);
    const next = changeable[0];

    if (!active) {
      return (
        <GuestMessage title="Запись отменена" action={<OwnAura referralCode={booking.referral_code} pitch="Ведёте занятия или встречи?" />}>
          {`${booking.title || 'Занятие'}${owner ? ` — ${owner}` : ''}. Чтобы записаться снова, откройте страницу записи по ссылке ${owner ? `от ${owner}` : 'владельца'}.`}
        </GuestMessage>
      );
    }

    return (
      <GuestLayout>
        <div>
          <h1 className="title-lg">{booking.title || 'Ваша запись'}</h1>
          <div className="facts" style={{ marginTop: 8 }}>
            {owner && <span>{owner}</span>}
            <span>{durationLabel(booking.duration_minutes)}</span>
            {booking.weekly && next && <span>{weeklyPhrase(next, zone)}</span>}
          </div>
        </div>
        {conn !== 'open' && <div className="notice warn">Нет связи с сервером Aura — переподключаемся…</div>}
        {notice && (
          <div className="notice" role="status">
            {notice}
          </div>
        )}
        {error && <div className="notice error">{error}</div>}

        <div className="stack" style={{ gap: 8 }}>
          <div className="title-sm">{booking.weekly ? `Занятия · осталось ${lessonsCount(changeable.length)}` : 'Занятие'}</div>
          <div className="card flush list">
            {occurrences.length === 0 && <div className="empty">Предстоящих занятий нет.</div>}
            {occurrences.map((o) => (
              <OccurrenceRow
                key={o.index}
                o={o}
                weekly={booking.weekly}
                weeks={booking.weeks}
                zone={zone}
                pending={pending}
                busy={busy}
                canMove={groups.length > 0}
                onMove={() => {
                  setPending({ kind: 'move', index: o.index });
                  setMoveTo(null);
                  setError('');
                }}
                onCancel={() => {
                  setPending({ kind: 'cancel', index: o.index });
                  setError('');
                }}
                onClose={close}
                onConfirmCancel={() =>
                  void act(
                    // Разовая запись — отменяем запись целиком (статус cancelled).
                    () => (booking.weekly ? api.guestCancel(token, o.index) : api.guestCancel(token)),
                    'Занятие отменено.',
                  )
                }
              />
            ))}
          </div>
          <span className="hint">Время — в вашем часовом поясе: {timeZoneLabel(zone)}</span>
        </div>

        {pending?.kind === 'move' && (
          <div className="card stack" aria-label="Перенос занятия">
            <div className="card-head" style={{ marginBottom: 0 }}>
              <h2 className="title-md">Новое время</h2>
              <button type="button" className="icon-btn" onClick={close} aria-label="Закрыть">
                <X size={16} />
              </button>
            </div>
            <div className="stack" style={{ gap: 14 }}>
              {groups.map((g) => (
                <div key={g.key} role="group" aria-label={g.title}>
                  <div className="invite-day-title">{g.title}</div>
                  <div className="invite-slots">
                    {g.slots.map((s) => (
                      <button key={s.start} type="button" className="invite-slot" aria-pressed={moveTo === s.start} onClick={() => setMoveTo(s.start)}>
                        {s.label}
                      </button>
                    ))}
                  </div>
                </div>
              ))}
            </div>
            <button
              className="btn btn-primary btn-block"
              disabled={!moveTo || busy}
              onClick={() => {
                const target = slots.find((s) => s.start === moveTo);
                if (!target) return;
                void act(() => api.reschedule(token, pending.index, target.start), `Занятие перенесено: ${slotPhrase(target, zone)}.`);
              }}
            >
              {busy ? <Spinner small /> : moveTo ? `Перенести на ${slotPhrase(slots.find((s) => s.start === moveTo) ?? { start: moveTo, end: moveTo }, zone)}` : 'Выберите время'}
            </button>
          </div>
        )}

        {changeable.length > 0 && <CalendarButton build={() => bookingIcs(booking)} fileName={bookingIcsFileName(booking)} variant="secondary" />}

        {booking.weekly && changeable.length > 0 && (
          <div className="row wrap" style={{ justifyContent: 'center' }}>
            {pending?.kind === 'cancelAll' ? (
              <>
                <span className="small muted">Отменить все оставшиеся занятия?</span>
                <button type="button" className="btn btn-danger btn-sm" disabled={busy} onClick={() => void act(() => api.guestCancel(token), 'Запись отменена.')}>
                  {busy ? <Spinner small /> : 'Да, отменить всё'}
                </button>
                <button type="button" className="btn btn-ghost btn-sm" disabled={busy} onClick={close}>
                  Назад
                </button>
              </>
            ) : (
              <button type="button" className="btn btn-ghost" onClick={() => setPending({ kind: 'cancelAll' })}>
                Отменить всю запись
              </button>
            )}
          </div>
        )}
        <OwnAura referralCode={booking.referral_code} pitch="Ведёте занятия или встречи?" />
      </GuestLayout>
    );
  }

  if (loadError) {
    return (
      <GuestMessage title="Не удалось открыть запись" action={<RetryButton onClick={() => void load()} />}>
        {loadError}
      </GuestMessage>
    );
  }

  if (conn !== 'open' && (conn === 'offline' || connError)) return <GuestOffline retry={retry} />;

  return <GuestLoading text="Открываем запись…" />;
}

function OccurrenceRow({
  o,
  weekly,
  weeks,
  zone,
  pending,
  busy,
  canMove,
  onMove,
  onCancel,
  onClose,
  onConfirmCancel,
}: {
  o: GuestOccurrence;
  weekly: boolean;
  weeks: number;
  zone: string;
  pending: Pending;
  busy: boolean;
  canMove: boolean;
  onMove: () => void;
  onCancel: () => void;
  onClose: () => void;
  onConfirmCancel: () => void;
}) {
  const confirming = pending?.kind === 'cancel' && pending.index === o.index;
  const moving = pending?.kind === 'move' && pending.index === o.index;
  const cancelled = o.status === 'cancelled';
  return (
    <div className={`list-row invite-row ${cancelled || o.past ? 'booking-dim' : ''}`} aria-current={moving ? 'true' : undefined}>
      <CalendarClock size={16} className="faint" aria-hidden="true" />
      <div className="body">
        <div className={cancelled ? 'booking-struck' : ''}>{slotPhrase(o, zone)}</div>
        {weekly && <div className="small muted">{seriesPosition(o.index, weeks)}</div>}
      </div>
      {cancelled && <span className="badge">Отменено</span>}
      {!cancelled && o.past && <span className="badge">Прошло</span>}
      {o.changeable &&
        (confirming ? (
          <div className="row" style={{ gap: 6 }}>
            <span className="small muted">Отменить?</span>
            <button type="button" className="btn btn-danger btn-sm" disabled={busy} onClick={onConfirmCancel}>
              {busy ? <Spinner small /> : 'Да'}
            </button>
            <button type="button" className="btn btn-ghost btn-sm" disabled={busy} onClick={onClose}>
              Нет
            </button>
          </div>
        ) : (
          <div className="row" style={{ gap: 6 }}>
            {canMove && (
              <button type="button" className="btn btn-secondary btn-sm" aria-pressed={moving} onClick={onMove}>
                Перенести
              </button>
            )}
            <button type="button" className="btn btn-ghost btn-sm" onClick={onCancel}>
              Отменить
            </button>
          </div>
        ))}
    </div>
  );
}
