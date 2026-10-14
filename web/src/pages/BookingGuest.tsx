/*
  Страница записи — сторона гостя (#/b/<slug>).

  Постоянная ссылка владельца (репетитора): гость без входа выбирает время из
  свободных вариантов (по дням, в часовом поясе браузера), по желанию —
  «Каждую неделю (N занятий)», оставляет имя, email и комментарий. После записи —
  «Готово»: личная ссылка управления (показывается один раз — скопировать,
  сохранить), .ics (для серии — RRULE + EXDATE) и «Заведите свою Ауру» с кодом
  владельца. Открывается и без входа, и со входом, вне оболочки приложения.
*/
import { Check, Clock, Download, Repeat } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Link, useParams } from 'react-router-dom';
import { CalendarButton, GuestLayout, GuestLoading, GuestMessage, GuestOffline, OwnAura, RetryButton } from '../components/GuestLayout';
import { downloadFile, LinkBox } from '../components/share';
import { Field, Spinner, Switch } from '../components/ui';
import { bookingIcs, bookingIcsFileName, durationLabel, isPlausibleSlug, lessonsCount, manageLink, skippedPhrase, weeklyLabel, weeklyPhrase } from '../lib/booking';
import { COMMENT_MAX, EMAIL_MAX, GUEST_NAME_MAX, guestErrorKind, guestErrorText, guestFormErrors, hasErrors, type GuestFormErrors } from '../lib/invite';
import { currentLocation, linkText } from '../lib/publicLink';
import { browserTimeZone, groupSlotsByDay, slotPhrase, timeZoneLabel, zoneDiffers } from '../lib/slots';
import { makeBookingApi, type Booked, type PublicPage, type Slot } from '../net/bookingApi';
import { genitive } from '../lib/format';
import { useSession } from '../state/session';

const NOT_FOUND = 'Страница записи не найдена';

export function BookingGuest() {
  const { slug } = useParams();
  const { socket, conn, connError, retry } = useSession();
  const api = useMemo(() => makeBookingApi(socket), [socket]);
  const zone = useMemo(browserTimeZone, []);

  const [page, setPage] = useState<PublicPage | null>(null);
  const [slots, setSlots] = useState<Slot[]>([]);
  const [missing, setMissing] = useState(!isPlausibleSlug(slug));
  const [loadError, setLoadError] = useState('');
  const [done, setDone] = useState<Booked | null>(null);
  const doneRef = useRef(false);
  doneRef.current = done !== null;
  const seq = useRef(0);
  const connRef = useRef(conn);
  connRef.current = conn;

  const [selected, setSelected] = useState<string | null>(null);
  const [weekly, setWeekly] = useState(false);
  const [name, setName] = useState('');
  const [email, setEmail] = useState('');
  const [comment, setComment] = useState('');
  const [errors, setErrors] = useState<GuestFormErrors & { form?: string }>({});
  const [busy, setBusy] = useState(false);

  const apply = useCallback((next: { page: PublicPage; slots: Slot[] }) => {
    setPage(next.page);
    setSlots(next.slots ?? []);
    if (!next.page.allow_weekly) setWeekly(false);
    // Выбранное время пропало из свежего списка — снимаем выбор.
    setSelected((cur) => (cur && (next.slots ?? []).some((s) => s.start === cur) ? cur : null));
  }, []);

  const load = useCallback(async () => {
    if (!isPlausibleSlug(slug)) return;
    const mine = ++seq.current;
    try {
      const reply = await api.get(slug);
      if (mine !== seq.current || doneRef.current) return;
      setLoadError('');
      apply(reply);
    } catch (error) {
      if (mine !== seq.current) return;
      const kind = guestErrorKind(error);
      if (kind === 'notFound') setMissing(true);
      else if (kind !== 'network' || connRef.current === 'open') setLoadError(guestErrorText(error, NOT_FOUND));
    }
  }, [api, slug, apply]);

  // Первое подключение и каждое переподключение — свежие слоты (пока запись не сделана).
  useEffect(() => {
    if (conn === 'open' && !doneRef.current) void load();
  }, [conn, load]);

  const groups = useMemo(() => groupSlotsByDay(slots, zone), [slots, zone]);

  const submit = async () => {
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
        weekly: page.allow_weekly && weekly,
      });
      seq.current += 1;
      setDone(reply);
    } catch (error) {
      const kind = guestErrorKind(error);
      if (kind === 'notFound') {
        setMissing(true);
      } else if (kind === 'conflict') {
        // Время заняли, пока гость заполнял форму: свежие варианты и просьба выбрать другое.
        setErrors({ form: guestErrorText(error) || 'Это время уже занято — выберите другое.' });
        setSelected(null);
        try {
          apply(await api.get(slug));
          seq.current += 1;
        } catch {
          /* оставим прежний список — кнопка «Записаться» снова спросит сервер */
        }
      } else {
        setErrors({ form: guestErrorText(error, NOT_FOUND) });
      }
    } finally {
      setBusy(false);
    }
  };

  // ------------------------------------------------------------ экраны

  if (missing) {
    return <GuestMessage title={NOT_FOUND}>Проверьте ссылку: возможно, она скопирована не полностью или владелец выключил запись. Попросите актуальную ссылку.</GuestMessage>;
  }

  if (done && page) return <Done booked={done} page={page} zone={zone} />;

  if (page) {
    const owner = page.owner_name;
    const ofOwner = owner ? genitive(owner) : 'преподавателя';
    return (
      <GuestLayout>
        <div>
          <h1 className="title-lg">{page.title || 'Запись'}</h1>
          <div className="facts" style={{ marginTop: 8 }}>
            {owner && <span>{owner}</span>}
            <span>
              <Clock size={14} /> {durationLabel(page.duration_minutes)}
            </span>
          </div>
        </div>
        {page.description && <div className="notice invite-note">{page.description}</div>}
        {conn !== 'open' && <div className="notice warn">Нет связи с сервером Aura — переподключаемся…</div>}

        {groups.length === 0 ? (
          <div className="notice">Свободного времени сейчас нет — загляните позже{owner ? ` или напишите ${owner} напрямую` : ''}.</div>
        ) : (
          <form
            className="stack"
            noValidate
            onSubmit={(e) => {
              e.preventDefault();
              void submit();
            }}
          >
            {errors.form && <div className="notice error">{errors.form}</div>}
            <div className="field">
              <span>Выберите время</span>
              <div className="stack" style={{ gap: 14 }}>
                {groups.map((g) => (
                  <div key={g.key} role="group" aria-label={g.title}>
                    <div className="invite-day-title">{g.title}</div>
                    <div className="invite-slots">
                      {g.slots.map((s) => (
                        <button
                          key={s.start}
                          type="button"
                          className="invite-slot"
                          aria-pressed={selected === s.start}
                          onClick={() => {
                            setSelected(s.start);
                            setErrors((e) => ({ ...e, slot: undefined, form: undefined }));
                          }}
                        >
                          {s.label}
                        </button>
                      ))}
                    </div>
                  </div>
                ))}
              </div>
              {errors.slot ? (
                <span className="field-error">{errors.slot}</span>
              ) : (
                <span className="hint">
                  Время — в вашем часовом поясе: {timeZoneLabel(zone)}
                  {zoneDiffers(page.owner_timezone, zone) ? `. У ${ofOwner} — ${timeZoneLabel(page.owner_timezone)}` : ''}
                </span>
              )}
            </div>
            {page.allow_weekly && page.max_weeks > 1 && (
              <div className="setting booking-weekly">
                <Repeat size={16} className="faint" aria-hidden="true" />
                <div className="body">
                  <div className="label">{weeklyLabel(page.max_weeks)}</div>
                  <div className="desc">То же время каждую неделю. Отдельное занятие можно будет перенести или отменить.</div>
                </div>
                <Switch on={weekly} onChange={setWeekly} label={weeklyLabel(page.max_weeks)} />
              </div>
            )}
            <Field label="Ваше имя" error={errors.name}>
              <input className={`input ${errors.name ? 'invalid' : ''}`} autoComplete="name" value={name} maxLength={GUEST_NAME_MAX} onChange={(e) => setName(e.target.value)} />
            </Field>
            <Field label="Email" error={errors.email} hint="Необязательно — чтобы с вами могли связаться">
              <input className={`input ${errors.email ? 'invalid' : ''}`} type="email" inputMode="email" autoComplete="email" placeholder="you@example.com" value={email} maxLength={EMAIL_MAX} onChange={(e) => setEmail(e.target.value)} />
            </Field>
            <Field label="Комментарий" error={errors.comment} hint="Необязательно — например, тема или класс">
              <textarea className="textarea" style={{ minHeight: 64 }} value={comment} maxLength={COMMENT_MAX} onChange={(e) => setComment(e.target.value)} />
            </Field>
            <button className="btn btn-primary btn-lg btn-block" type="submit" disabled={busy}>
              {busy ? <Spinner small /> : 'Записаться'}
            </button>
          </form>
        )}
        <OwnAura referralCode={page.referral_code} pitch="Ведёте занятия или встречи?" />
      </GuestLayout>
    );
  }

  if (loadError) {
    return (
      <GuestMessage title="Не удалось открыть страницу записи" action={<RetryButton onClick={() => void load()} />}>
        {loadError}
      </GuestMessage>
    );
  }

  if (conn !== 'open' && (conn === 'offline' || connError)) return <GuestOffline retry={retry} />;

  return <GuestLoading text="Открываем страницу записи…" />;
}

function Done({ booked, page, zone }: { booked: Booked; page: PublicPage; zone: string }) {
  const { booking, manage_token: token, manage_url: manageUrl, skipped } = booked;
  const link = manageLink(token, manageUrl ?? '', currentLocation());
  const occurrences = [...(booking.occurrences ?? [])].sort((a, b) => Date.parse(a.start) - Date.parse(b.start));
  const first = occurrences.find((o) => o.status === 'booked') ?? occurrences[0];
  const bookedCount = occurrences.filter((o) => o.status === 'booked').length;
  const owner = booking.owner_name || page.owner_name;
  const missed = skippedPhrase(skipped ?? [], zone);
  const [saveFailed, setSaveFailed] = useState(false);

  const save = () => {
    const text = [`${booking.title || page.title}${owner ? ` — ${owner}` : ''}`, '', 'Перенести или отменить занятие:', linkText(link), ''].join('\r\n');
    setSaveFailed(!downloadFile('aura-booking-link.txt', text, 'text/plain;charset=utf-8'));
  };

  return (
    <GuestLayout>
      <div className="stack">
        <span className="invite-done" aria-hidden="true">
          <Check size={20} strokeWidth={2.5} />
        </span>
        <h1 className="title-lg">Готово</h1>
        <p className="lead" style={{ margin: 0 }}>
          {owner ? `Вы записаны — ${owner} получит уведомление.` : 'Вы записаны.'}
        </p>
        {first && (
          <div className="card stack" style={{ gap: 6 }}>
            <div className="title-md">{booking.title || page.title || 'Занятие'}</div>
            <div className="muted">{slotPhrase(first, zone)}</div>
            {booking.weekly && (
              <div className="small muted">
                {weeklyPhrase(first, zone)}, {lessonsCount(bookedCount)}
              </div>
            )}
            <div className="small faint">{timeZoneLabel(zone, new Date(first.start))}</div>
          </div>
        )}
        {missed && <div className="notice warn">В эти недели время уже занято — занятий не будет: {missed}.</div>}

        <div className="stack" style={{ gap: 8 }}>
          <div className="title-sm">Ссылка для переноса и отмены</div>
          <LinkBox
            link={link}
            label="Ссылка управления записью"
            share={{ title: 'Моя запись', text: `${booking.title || page.title} — перенести или отменить` }}
            extra={
              <button type="button" className="btn btn-secondary" onClick={save}>
                <Download size={15} /> Сохранить
              </button>
            }
          />
          {saveFailed && <div className="field-error">Не удалось сохранить файл — скопируйте ссылку.</div>}
          <span className="hint">
            Ссылка показывается один раз — сохраните её. По ней можно перенести или отменить занятие.{' '}
            <Link className="link" to={`/m/${token}`}>
              Открыть
            </Link>
          </span>
        </div>

        <CalendarButton build={() => bookingIcs(booking)} fileName={bookingIcsFileName(booking)} />
        <OwnAura referralCode={booking.referral_code || page.referral_code} pitch="Ведёте занятия или встречи?" />
      </div>
    </GuestLayout>
  );
}
