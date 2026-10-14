/*
  Страница записи глазами гостя (#/b/<slug>) и ссылка управления записью (#/m/<токен>).

  Обе открываются без входа (и со входом тоже) вне оболочки приложения: booking.get /
  book / manage / reschedule / cancel — публичные методы. Время показывается по дням
  в часовом поясе браузера. После записи гость получает ссылку управления (сервер
  отдаёт токен один раз) — по ней можно перенести ближайшую встречу или отменить
  одну/все встречи серии. Файл .ics собирается здесь же, в браузере.
*/
import { CalendarPlus, Check, Clock, Copy, RefreshCw, Repeat, WifiOff } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { Link, useParams } from 'react-router-dom';
import { Field, Spinner } from '../components/ui';
import {
  bookingIcs,
  isPlausibleSlug,
  isUpcoming,
  managePath,
  nextOccurrence,
  pagePath,
  publicLink,
  seriesLabel,
  sortedOccurrences,
  upcomingCount,
} from '../lib/booking';
import { genitive } from '../lib/format';
import {
  browserTimeZone,
  COMMENT_MAX,
  EMAIL_MAX,
  GUEST_NAME_MAX,
  groupSlotsByDay,
  guestErrorKind,
  guestErrorText,
  guestFormErrors,
  hasErrors,
  icsUtc,
  isPlausibleToken,
  slotPhrase,
  timeIn,
  timeZoneLabel,
  type DayGroup,
  type GuestFormErrors,
} from '../lib/invite';
import { makeBookingApi, type GuestBooking, type GuestPage } from '../net/bookingApi';
import type { InviteSlot } from '../net/inviteApi';
import { useSession } from '../state/session';
import { copyText } from './Invites';
import { GuestLayout, Message, OwnAura } from './InviteGuest';

const here = () => (typeof location === 'undefined' ? null : location);

function downloadBookingIcs(booking: Pick<GuestBooking, 'page_title' | 'owner_name' | 'occurrences'>): boolean {
  try {
    const blob = new Blob([bookingIcs(booking)], { type: 'text/calendar;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const first = sortedOccurrences(booking.occurrences).find((o) => o.status === 'active');
    const a = document.createElement('a');
    a.href = url;
    a.download = first ? `aura-booking-${icsUtc(new Date(first.start)).slice(0, 8)}.ics` : 'aura-booking.ics';
    a.rel = 'noopener';
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 10_000);
    return true;
  } catch {
    return false;
  }
}

function CalendarButton({ booking }: { booking: Pick<GuestBooking, 'page_title' | 'owner_name' | 'occurrences'> }) {
  const [failed, setFailed] = useState(false);
  return (
    <>
      <button className="btn btn-secondary btn-block" onClick={() => setFailed(!downloadBookingIcs(booking))}>
        <CalendarPlus size={17} /> Добавить в календарь
      </button>
      {failed && <div className="field-error">Не удалось создать файл календаря.</div>}
    </>
  );
}

const DAYS_AT_ONCE = 4;

/** Выбор времени: слоты по дням в поясе гостя; сначала несколько ближайших дней. */
function SlotPicker({ groups, selected, onSelect }: { groups: DayGroup[]; selected: string | null; onSelect: (start: string) => void }) {
  const [days, setDays] = useState(DAYS_AT_ONCE);
  // Выбранный слот всегда на экране, даже если его день дальше показанных.
  const selectedDay = groups.findIndex((g) => g.slots.some((s) => s.start === selected));
  const shown = groups.slice(0, Math.max(days, selectedDay + 1));
  return (
    <div className="stack" style={{ gap: 14 }}>
      {shown.map((g) => (
        <div key={g.key} role="group" aria-label={g.title}>
          <div className="invite-day-title">{g.title}</div>
          <div className="invite-slots">
            {g.slots.map((s) => (
              <button key={s.start} type="button" className="invite-slot" aria-pressed={selected === s.start} onClick={() => onSelect(s.start)}>
                {s.label}
              </button>
            ))}
          </div>
        </div>
      ))}
      {shown.length < groups.length && (
        <button type="button" className="btn btn-ghost btn-sm" style={{ alignSelf: 'flex-start' }} onClick={() => setDays((d) => d + DAYS_AT_ONCE)}>
          Показать ещё дни ({groups.length - shown.length})
        </button>
      )}
    </div>
  );
}

function ZoneHint({ zone, ownerZone, ownerName }: { zone: string; ownerZone: string; ownerName: string }) {
  const differs = ownerZone && timeIn(new Date(), ownerZone) !== timeIn(new Date(), zone);
  const ofOwner = ownerName ? genitive(ownerName) : 'организатора';
  return (
    <span className="hint">
      Время — в вашем часовом поясе: {timeZoneLabel(zone)}
      {differs ? `. У ${ofOwner} — ${timeZoneLabel(ownerZone)}` : ''}
    </span>
  );
}

/** Нет связи / ошибка загрузки / ожидание — общие экраны обеих страниц. */
function guestScreens(conn: string, connError: unknown, retry: () => void) {
  return (loadError: string, reload: () => void, waiting: string): ReactNode => {
    if (loadError) {
      return (
        <Message
          title="Не удалось открыть страницу"
          action={
            <button className="btn btn-secondary" onClick={reload}>
              <RefreshCw size={15} /> Повторить
            </button>
          }
        >
          {loadError}
        </Message>
      );
    }
    if (conn !== 'open' && (conn === 'offline' || connError)) {
      return (
        <GuestLayout>
          <div className="stack">
            <div className="row">
              <WifiOff size={16} className="faint" />
              <h1 className="title-md">Нет связи с сервером Aura</h1>
            </div>
            <p className="lead" style={{ margin: 0 }}>
              Проверьте интернет — мы пробуем подключиться снова автоматически.
            </p>
            <button className="btn btn-secondary" onClick={retry}>
              <RefreshCw size={15} /> Повторить
            </button>
          </div>
        </GuestLayout>
      );
    }
    return (
      <div className="gate">
        <div className="gate-card">
          <Spinner />
          <span className="muted small">{waiting}</span>
        </div>
      </div>
    );
  };
}

/** Ссылка управления записью: показывается один раз — сразу после записи. */
function ManageLink({ token, url }: { token: string; url: string }) {
  const link = publicLink(managePath(token), url, here());
  const [copied, setCopied] = useState(false);
  if (!link) {
    return (
      <div className="notice">
        Чтобы перенести или отменить встречу, сохраните эту страницу в закладки или напишите организатору.
      </div>
    );
  }
  return (
    <div className="stack" style={{ gap: 6 }}>
      <span className="small muted">Ссылка, чтобы перенести или отменить встречу — сохраните её:</span>
      <div className="invite-link">
        <input className="input mono" readOnly value={link} aria-label="Ссылка управления записью" onFocus={(e) => e.currentTarget.select()} />
        <button
          type="button"
          className="btn btn-secondary"
          onClick={async () => {
            const ok = await copyText(link);
            setCopied(ok);
            if (ok) setTimeout(() => setCopied(false), 2000);
          }}
        >
          <Copy size={15} /> {copied ? 'Скопировано' : 'Скопировать'}
        </button>
      </div>
    </div>
  );
}

function OccurrenceList({ booking, zone }: { booking: Pick<GuestBooking, 'occurrences'>; zone: string }) {
  const list = sortedOccurrences(booking.occurrences);
  return (
    <div className="stack" style={{ gap: 4 }}>
      {list.map((o) => (
        <div key={o.start} className={`small ${o.status === 'active' ? 'muted' : 'faint'}`} style={o.status === 'active' ? undefined : { textDecoration: 'line-through' }}>
          {slotPhrase(o, zone)}
          {o.status !== 'active' ? ' — отменена' : ''}
        </div>
      ))}
    </div>
  );
}

// ================================================================ #/b/<slug>

export function BookingGuest() {
  const { slug } = useParams();
  const { socket, conn, connError, retry } = useSession();
  const api = useMemo(() => makeBookingApi(socket), [socket]);
  const zone = useMemo(browserTimeZone, []);
  const screens = guestScreens(conn, connError, retry);

  const [page, setPage] = useState<GuestPage | null>(null);
  const [missing, setMissing] = useState(!isPlausibleSlug(slug));
  const [loadError, setLoadError] = useState('');
  const [done, setDone] = useState<{ booking: GuestBooking; token: string; url: string } | null>(null);
  const doneRef = useRef(done);
  doneRef.current = done;
  const seq = useRef(0);
  const connRef = useRef(conn);
  connRef.current = conn;

  const [selected, setSelected] = useState<string | null>(null);
  const [count, setCount] = useState(1);
  const [name, setName] = useState('');
  const [email, setEmail] = useState('');
  const [comment, setComment] = useState('');
  const [errors, setErrors] = useState<GuestFormErrors & { form?: string }>({});
  const [busy, setBusy] = useState(false);

  const apply = useCallback((next: GuestPage) => {
    setPage(next);
    setSelected((cur) => (cur && (next.slots ?? []).some((s) => s.start === cur) ? cur : null));
    setCount((c) => Math.min(c, Math.max(1, next.max_series)));
  }, []);

  const load = useCallback(async () => {
    if (!isPlausibleSlug(slug)) return;
    const mine = ++seq.current;
    try {
      const reply = await api.get(slug);
      if (mine !== seq.current || doneRef.current) return;
      setLoadError('');
      apply(reply.page);
    } catch (error) {
      if (mine !== seq.current) return;
      const kind = guestErrorKind(error);
      if (kind === 'notFound') setMissing(true);
      else if (kind !== 'network' || connRef.current === 'open') setLoadError(guestErrorText(error));
    }
  }, [api, slug, apply]);

  useEffect(() => {
    if (conn === 'open' && !doneRef.current) void load();
  }, [conn, load]);

  const groups = useMemo(() => (page ? groupSlotsByDay(page.slots ?? [], zone) : []), [page, zone]);

  const book = async () => {
    if (!page || !slug) return;
    const next = guestFormErrors({ name, email, comment, slot: selected }, 'accept');
    setErrors(next);
    if (hasErrors(next) || !selected) return;
    setBusy(true);
    try {
      const reply = await api.book(slug, {
        start: selected,
        guest_name: name.trim(),
        guest_email: email.trim() || undefined,
        comment: comment.trim() || undefined,
        count,
      });
      seq.current += 1;
      setDone(reply);
    } catch (error) {
      const kind = guestErrorKind(error);
      if (kind === 'notFound') {
        setMissing(true);
      } else if (kind === 'conflict') {
        // Время заняли, серия упёрлась в занятую неделю или запись закрыли — показываем свежее состояние.
        try {
          const fresh = await api.get(slug);
          seq.current += 1;
          apply(fresh.page);
        } catch {
          /* оставляем как есть — текст ошибки ниже */
        }
        setErrors({ form: guestErrorText(error) });
      } else {
        setErrors({ form: guestErrorText(error) });
      }
    } finally {
      setBusy(false);
    }
  };

  if (missing) {
    return <Message title="Страница записи не найдена">Проверьте ссылку: возможно, она скопирована не полностью или запись по ней закрыта.</Message>;
  }

  if (done) {
    const owner = done.booking.owner_name;
    return (
      <GuestLayout>
        <div className="stack">
          <span className="invite-done" aria-hidden="true">
            <Check size={20} strokeWidth={2.5} />
          </span>
          <h1 className="title-lg">Вы записаны</h1>
          <p className="lead" style={{ margin: 0 }}>{owner ? `${owner} получил(а) уведомление.` : 'Организатор получил уведомление.'}</p>
          <div className="card stack" style={{ gap: 6 }}>
            <div className="title-md">{done.booking.page_title || 'Встреча'}</div>
            <OccurrenceList booking={done.booking} zone={zone} />
            <div className="small faint">{timeZoneLabel(zone)}</div>
          </div>
          <CalendarButton booking={done.booking} />
          <ManageLink token={done.token} url={done.url} />
          <OwnAura refCode={done.booking.ref} source="booking" />
        </div>
      </GuestLayout>
    );
  }

  if (page) {
    if (page.status !== 'active') {
      return <Message title="Запись временно закрыта">{`${page.owner_name || 'Организатор'} пока не принимает новые записи. Загляните позже.`}</Message>;
    }
    const seriesOptions = Array.from({ length: Math.max(1, page.max_series) }, (_, i) => i + 1);
    return (
      <GuestLayout>
        <div>
          <h1 className="title-lg">{page.title || 'Запись'}</h1>
          <div className="facts" style={{ marginTop: 8 }}>
            {page.owner_name && <span>{page.owner_name}</span>}
            <span>
              <Clock size={14} /> {page.duration_minutes} мин
            </span>
          </div>
        </div>
        {page.description && <div className="notice invite-note">{page.description}</div>}
        {conn !== 'open' && <div className="notice warn">Нет связи с сервером Aura — переподключаемся…</div>}

        {page.slots_unavailable ? (
          <div className="stack">
            <div className="notice warn">Не удалось загрузить свободное время. Попробуйте ещё раз через минуту.</div>
            <button className="btn btn-secondary" onClick={() => void load()}>
              <RefreshCw size={15} /> Обновить
            </button>
          </div>
        ) : groups.length === 0 ? (
          <div className="notice">Свободного времени сейчас нет — загляните позже или напишите {page.owner_name ? `организатору (${page.owner_name})` : 'организатору'} напрямую.</div>
        ) : (
          <form
            className="stack"
            noValidate
            onSubmit={(e) => {
              e.preventDefault();
              void book();
            }}
          >
            {errors.form && <div className="notice error">{errors.form}</div>}
            <div className="field">
              <span>Выберите время</span>
              <SlotPicker
                groups={groups}
                selected={selected}
                onSelect={(start) => {
                  setSelected(start);
                  setErrors((e) => ({ ...e, slot: undefined, form: undefined }));
                }}
              />
              {errors.slot ? <span className="field-error">{errors.slot}</span> : <ZoneHint zone={zone} ownerZone={page.owner_timezone} ownerName={page.owner_name} />}
            </div>
            {page.max_series > 1 && (
              <Field label="Сколько встреч" hint="Та же встреча каждую неделю в это же время">
                <select className="input" value={count} onChange={(e) => setCount(Number(e.target.value))}>
                  {seriesOptions.map((n) => (
                    <option key={n} value={n}>
                      {seriesLabel(n)}
                    </option>
                  ))}
                </select>
              </Field>
            )}
            <Field label="Ваше имя" error={errors.name}>
              <input className={`input ${errors.name ? 'invalid' : ''}`} autoComplete="name" value={name} maxLength={GUEST_NAME_MAX} onChange={(e) => setName(e.target.value)} />
            </Field>
            <Field label="Email" error={errors.email} hint="Необязательно — чтобы с вами могли связаться">
              <input className={`input ${errors.email ? 'invalid' : ''}`} type="email" inputMode="email" autoComplete="email" placeholder="you@example.com" value={email} maxLength={EMAIL_MAX} onChange={(e) => setEmail(e.target.value)} />
            </Field>
            <Field label="Комментарий" error={errors.comment} hint="Необязательно">
              <textarea className="textarea" style={{ minHeight: 64 }} value={comment} maxLength={COMMENT_MAX} onChange={(e) => setComment(e.target.value)} />
            </Field>
            <button className="btn btn-primary btn-lg btn-block" type="submit" disabled={busy}>
              {busy ? <Spinner small /> : 'Записаться'}
            </button>
          </form>
        )}
        {slug && <LostLink api={api} slug={slug} />}
        <OwnAura refCode={page.ref} source="booking" />
      </GuestLayout>
    );
  }

  return screens(loadError, () => void load(), 'Открываем страницу записи…');
}

/** «Уже записались?» — новая ссылка управления на email, указанный при записи. */
function LostLink({ api, slug }: { api: ReturnType<typeof makeBookingApi>; slug: string }) {
  const [open, setOpen] = useState(false);
  const [email, setEmail] = useState('');
  const [state, setState] = useState<'idle' | 'busy' | 'sent'>('idle');
  const [error, setError] = useState('');
  if (!open) {
    return (
      <button type="button" className="btn btn-ghost btn-sm" style={{ alignSelf: 'center' }} onClick={() => setOpen(true)}>
        Уже записались? Пришлём ссылку для переноса
      </button>
    );
  }
  if (state === 'sent') {
    return <div className="notice">Если вы записывались с этим email, новая ссылка уже в письме. Прежняя ссылка больше не действует.</div>;
  }
  return (
    <form
      className="stack"
      noValidate
      onSubmit={async (e) => {
        e.preventDefault();
        const value = email.trim();
        if (!/^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/.test(value)) {
          setError('Похоже, в email опечатка');
          return;
        }
        setState('busy');
        setError('');
        try {
          await api.resendLink(slug, value);
          setState('sent');
        } catch (err) {
          setState('idle');
          setError(guestErrorText(err));
        }
      }}
    >
      <Field label="Email, указанный при записи" error={error}>
        <input className={`input ${error ? 'invalid' : ''}`} type="email" inputMode="email" autoComplete="email" value={email} maxLength={EMAIL_MAX} onChange={(e) => setEmail(e.target.value)} />
      </Field>
      <button className="btn btn-secondary" type="submit" disabled={state === 'busy'}>
        {state === 'busy' ? <Spinner small /> : 'Прислать ссылку'}
      </button>
    </form>
  );
}

// ================================================================ #/m/<токен>

export function BookingManage() {
  const { token } = useParams();
  const { socket, conn, connError, retry } = useSession();
  const api = useMemo(() => makeBookingApi(socket), [socket]);
  const zone = useMemo(browserTimeZone, []);
  const screens = guestScreens(conn, connError, retry);

  const [booking, setBooking] = useState<GuestBooking | null>(null);
  const [missing, setMissing] = useState(!isPlausibleToken(token));
  const [loadError, setLoadError] = useState('');
  const [mode, setMode] = useState<'view' | 'move' | 'cancel'>('view');
  const [selected, setSelected] = useState<string | null>(null);
  const [cancelTarget, setCancelTarget] = useState<string | null>(null); // start одной встречи или null — все
  const [reason, setReason] = useState('');
  const [error, setError] = useState('');
  const [notice, setNotice] = useState('');
  const [busy, setBusy] = useState(false);
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
      setSelected((cur) => (cur && (reply.booking.slots ?? []).some((s) => s.start === cur) ? cur : null));
    } catch (e) {
      if (mine !== seq.current) return;
      const kind = guestErrorKind(e);
      if (kind === 'notFound') setMissing(true);
      else if (kind !== 'network' || connRef.current === 'open') setLoadError(guestErrorText(e));
    }
  }, [api, token]);

  useEffect(() => {
    if (conn === 'open') void load();
  }, [conn, load]);

  const groups = useMemo(() => (booking ? groupSlotsByDay(booking.slots ?? [], zone) : []), [booking, zone]);

  const act = async (action: () => Promise<{ booking: GuestBooking }>, success: string) => {
    setBusy(true);
    setError('');
    try {
      const reply = await action();
      seq.current += 1;
      setBooking(reply.booking);
      setMode('view');
      setSelected(null);
      setReason('');
      setNotice(success);
    } catch (e) {
      if (guestErrorKind(e) === 'notFound') setMissing(true);
      else setError(guestErrorText(e));
      if (guestErrorKind(e) === 'conflict') void load();
    } finally {
      setBusy(false);
    }
  };

  if (missing) {
    return <Message title="Запись не найдена">Проверьте ссылку: возможно, она скопирована не полностью.</Message>;
  }

  if (booking) {
    const owner = booking.owner_name || 'Организатор';
    const next = nextOccurrence(booking.occurrences);
    const upcoming = upcomingCount(booking.occurrences);
    const againLink = booking.page_slug ? (
      <Link className="btn btn-secondary btn-block" to={pagePath(booking.page_slug)}>
        Записаться снова
      </Link>
    ) : null;

    if (booking.status === 'cancelled') {
      return (
        <GuestLayout>
          <div className="stack">
            <h1 className="title-lg">Запись отменена</h1>
            <p className="lead" style={{ margin: 0 }}>
              {booking.cancelled_by === 'owner' ? `${owner} отменил(а) встречу. Можно выбрать другое время.` : `${owner} получил(а) уведомление об отмене.`}
            </p>
            {notice && <div className="notice">{notice}</div>}
            <div className="card stack" style={{ gap: 6 }}>
              <div className="title-md">{booking.page_title || 'Встреча'}</div>
              <OccurrenceList booking={booking} zone={zone} />
            </div>
            {againLink}
            <OwnAura refCode={booking.ref} source="booking" />
          </div>
        </GuestLayout>
      );
    }

    return (
      <GuestLayout>
        <div>
          <h1 className="title-lg">{booking.page_title || 'Ваша запись'}</h1>
          <div className="facts" style={{ marginTop: 8 }}>
            <span>{owner}</span>
            <span>
              <Clock size={14} /> {booking.duration_minutes} мин
            </span>
            {booking.occurrences.length > 1 && (
              <span>
                <Repeat size={14} /> раз в неделю
              </span>
            )}
          </div>
        </div>
        {conn !== 'open' && <div className="notice warn">Нет связи с сервером Aura — переподключаемся…</div>}
        {notice && <div className="notice">{notice}</div>}
        {error && <div className="notice error">{error}</div>}

        <div className="card stack" style={{ gap: 6 }}>
          <div className="small muted">{booking.guest_name}</div>
          <OccurrenceList booking={booking} zone={zone} />
          <div className="small faint">{timeZoneLabel(zone)}</div>
        </div>

        {upcoming === 0 && <div className="notice">Все встречи этой записи уже прошли.</div>}

        {mode === 'view' && upcoming > 0 && (
          <div className="stack">
            <CalendarButton booking={booking} />
            {booking.can_change && (
              <div className="row wrap" style={{ gap: 8 }}>
                <button className="btn btn-secondary" disabled={booking.reschedules_left <= 0} onClick={() => setMode('move')}>
                  Перенести
                </button>
                <button
                  className="btn btn-ghost"
                  onClick={() => {
                    setCancelTarget(null);
                    setMode('cancel');
                  }}
                >
                  Отменить
                </button>
              </div>
            )}
            {booking.can_change && booking.reschedules_left <= 0 && <span className="hint">Перенести можно не больше трёх раз — дальше только отмена.</span>}
          </div>
        )}

        {mode === 'move' && next && (
          <div className="stack">
            <div className="small muted">
              Переносим встречу {slotPhrase(next, zone)}. Осталось переносов: {booking.reschedules_left}.
            </div>
            {booking.slots_unavailable ? (
              <div className="notice warn">Не удалось загрузить свободное время. Попробуйте ещё раз через минуту.</div>
            ) : groups.length === 0 ? (
              <div className="notice">Свободного времени для переноса сейчас нет.</div>
            ) : (
              <>
                <SlotPicker groups={groups} selected={selected} onSelect={setSelected} />
                <ZoneHint zone={zone} ownerZone={booking.owner_timezone} ownerName={booking.owner_name} />
              </>
            )}
            <div className="row wrap" style={{ gap: 8 }}>
              <button
                className="btn btn-primary"
                disabled={busy || !selected}
                onClick={() => token && selected && void act(() => api.reschedule(token, selected, next.start), 'Встреча перенесена.')}
              >
                {busy ? <Spinner small /> : 'Перенести сюда'}
              </button>
              <button className="btn btn-ghost" disabled={busy} onClick={() => setMode('view')}>
                Назад
              </button>
            </div>
          </div>
        )}

        {mode === 'cancel' && (
          <div className="stack">
            {upcoming > 1 && (
              <Field label="Что отменить">
                <select className="input" value={cancelTarget ?? ''} onChange={(e) => setCancelTarget(e.target.value || null)}>
                  <option value="">Все предстоящие встречи ({upcoming})</option>
                  {sortedOccurrences(booking.occurrences)
                    .filter((o) => isUpcoming(o))
                    .map((o) => (
                      <option key={o.start} value={o.start}>
                        Только {slotPhrase(o as InviteSlot, zone)}
                      </option>
                    ))}
                </select>
              </Field>
            )}
            <Field label="Причина" hint="Необязательно — её увидит организатор">
              <textarea className="textarea" style={{ minHeight: 56 }} value={reason} maxLength={COMMENT_MAX} onChange={(e) => setReason(e.target.value)} />
            </Field>
            <div className="row wrap" style={{ gap: 8 }}>
              <button
                className="btn btn-danger"
                disabled={busy}
                onClick={() =>
                  token &&
                  void act(
                    () => api.cancel(token, { occurrence: cancelTarget ?? undefined, reason: reason.trim() || undefined }),
                    cancelTarget ? 'Встреча отменена.' : 'Запись отменена.',
                  )
                }
              >
                {busy ? <Spinner small /> : 'Да, отменить'}
              </button>
              <button className="btn btn-ghost" disabled={busy} onClick={() => setMode('view')}>
                Назад
              </button>
            </div>
          </div>
        )}
        <OwnAura refCode={booking.ref} source="booking" />
      </GuestLayout>
    );
  }

  return screens(loadError, () => void load(), 'Открываем запись…');
}
