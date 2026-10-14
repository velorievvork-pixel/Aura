/*
  Запись — сторона владельца (#/booking).

  Постоянная ссылка для профиля (Авито, Профи, Telegram): ученики сами выбирают
  свободное время, могут записаться «каждую неделю», переносят и отменяют по
  своей личной ссылке. Здесь — тариф и лимиты, страницы записи (создать,
  изменить, выключить, удалить, новая ссылка), копирование и «Поделиться»,
  ближайшие занятия по дням с отменой занятия или всей записи. Список
  обновляется при уведомлениях booking.*.
*/
import { CalendarCheck, Pencil, Plus, Repeat, RotateCw, Trash2 } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState, type FormEvent } from 'react';
import { Link } from 'react-router-dom';
import { LinkBox } from '../components/share';
import { Empty, Field, Segmented, Spinner, Switch } from '../components/ui';
import {
  agendaByDay,
  bookingErrorText,
  canCreatePage,
  durationLabel,
  formToInput,
  hoursSummary,
  pageFormErrors,
  pageLink,
  pageToForm,
  PAGE_DESCRIPTION_MAX,
  PAGE_TITLE_MAX,
  planView,
  toggle,
  upcomingCount,
  WEEKDAY_SHORT,
  weekdaysSummary,
  type AgendaItem,
  type PageForm,
  type PageFormErrors,
} from '../lib/booking';
import { hasErrors } from '../lib/invite';
import { plural } from '../lib/format';
import { currentLocation } from '../lib/publicLink';
import { browserTimeZone } from '../lib/slots';
import { makeBookingApi, type Booking as BookingRow, type BookingApi, type BookingPage, type Plan } from '../net/bookingApi';
import { isNetworkError } from '../net/socket';
import { useData } from '../state/data';
import { useSession } from '../state/session';

const RANGES = [
  { value: '7', label: 'Неделя' },
  { value: '30', label: 'Месяц' },
  { value: '90', label: '3 месяца' },
];

const HOURS = Array.from({ length: 24 }, (_, h) => h);

export function Booking() {
  const { socket, conn } = useSession();
  const { notify } = useData();
  const api = useMemo(() => makeBookingApi(socket), [socket]);
  const zone = useMemo(browserTimeZone, []);

  const [pages, setPages] = useState<BookingPage[] | null>(null);
  const [plan, setPlan] = useState<Plan | null>(null);
  const [bookings, setBookings] = useState<BookingRow[] | null>(null);
  const [error, setError] = useState('');
  const [days, setDays] = useState('30');
  const [showCancelled, setShowCancelled] = useState(false);
  /** null — редактор закрыт; 0 — новая страница; id — правка. */
  const [editing, setEditing] = useState<number | null>(null);
  const pagesSeq = useRef(0);
  const listSeq = useRef(0);

  const loadPages = useCallback(async () => {
    const mine = ++pagesSeq.current;
    try {
      const reply = await api.pages();
      if (mine !== pagesSeq.current) return;
      setPages(reply.pages ?? []);
      setPlan(reply.plan ?? null);
      setError('');
    } catch (err) {
      if (mine === pagesSeq.current && !isNetworkError(err)) setError(bookingErrorText(err));
    }
  }, [api]);

  const loadBookings = useCallback(async () => {
    const mine = ++listSeq.current;
    try {
      const reply = await api.list(Number(days), showCancelled);
      if (mine !== listSeq.current) return;
      setBookings(reply.bookings ?? []);
    } catch (err) {
      if (mine === listSeq.current && !isNetworkError(err)) setError(bookingErrorText(err));
    }
  }, [api, days, showCancelled]);

  // Открытие страницы и каждое переподключение — свежие данные.
  useEffect(() => {
    if (conn === 'open') void loadPages();
  }, [conn, loadPages]);
  useEffect(() => {
    if (conn === 'open') void loadBookings();
  }, [conn, loadBookings]);

  // Гость записался, перенёс или отменил — сервер шлёт уведомление booking.*;
  // бонус Pro (referral.rewarded) меняет лимиты.
  useEffect(
    () =>
      socket.onEvent((event) => {
        if (event.event !== 'notification.new') return;
        const kind = String((event.payload as { kind?: unknown }).kind ?? '');
        if (kind.startsWith('booking.')) void loadBookings();
        if (kind === 'referral.rewarded') void loadPages();
      }),
    [socket, loadBookings, loadPages],
  );

  const view = planView(plan, new Date(), zone);
  const canCreate = pages !== null && canCreatePage(pages, plan);
  const agenda = useMemo(() => agendaByDay(bookings ?? [], zone, new Date(), showCancelled), [bookings, zone, showCancelled]);

  const onSaved = (page: BookingPage) => {
    pagesSeq.current += 1;
    setPages((list) => {
      const rest = (list ?? []).filter((p) => p.id !== page.id);
      return (list ?? []).some((p) => p.id === page.id) ? (list ?? []).map((p) => (p.id === page.id ? page : p)) : [...rest, page];
    });
    setEditing(null);
  };

  const removePage = async (id: number) => {
    try {
      await api.deletePage(id);
      pagesSeq.current += 1;
      setPages((list) => (list ?? []).filter((p) => p.id !== id));
      notify('Страница удалена. Записи остались в списке занятий');
    } catch (err) {
      notify(bookingErrorText(err), 'error');
      void loadPages();
    }
  };

  const replacePage = (page: BookingPage) => {
    pagesSeq.current += 1;
    setPages((list) => (list ?? []).map((p) => (p.id === page.id ? page : p)));
  };

  const cancel = async (item: AgendaItem, whole: boolean) => {
    try {
      const reply = await api.cancel(item.booking.id, whole ? undefined : item.occurrence.index);
      listSeq.current += 1;
      // Ответ — запись целиком; occurrences в нём — по окну сервера, а не только наше окно: просто перечитываем.
      setBookings((list) => (list ?? []).map((b) => (b.id === reply.booking.id ? { ...reply.booking, occurrences: mergeOccurrences(b, reply.booking) } : b)));
      notify(whole ? 'Запись отменена' : 'Занятие отменено');
      void loadBookings();
    } catch (err) {
      notify(bookingErrorText(err), 'error');
      void loadBookings();
    }
  };

  const editingPage = editing ? (pages ?? []).find((p) => p.id === editing) ?? null : null;

  return (
    <div className="page">
      <header className="page-head">
        <div>
          <h1 className="title-xl">Запись</h1>
          <div className="sub">Постоянная ссылка для учеников и клиентов: сами выбирают свободное время, переносят и отменяют.</div>
        </div>
      </header>

      <div className="stack-lg">
        {error && <div className="notice error">{error}</div>}

        <PlanCard pages={pages?.length ?? 0} view={view} loaded={plan !== null} />

        <section className="stack">
          <h2 className="title-sm section-title">
            Страницы записи
            {pages && pages.length > 0 && editing === null && (
              <button className="btn btn-secondary btn-sm" disabled={!canCreate} onClick={() => setEditing(0)} title={canCreate ? undefined : `На тарифе ${view.name} — ${view.maxPages} ${plural(view.maxPages, 'страница', 'страницы', 'страниц')}`}>
                <Plus size={14} /> Новая страница
              </button>
            )}
          </h2>
          {pages === null && !error && (
            <div className="card empty">
              <Spinner small />
            </div>
          )}
          {editing === 0 && <PageEditor api={api} page={null} onSaved={onSaved} onCancel={() => setEditing(null)} />}
          {pages?.length === 0 && editing === null && (
            <div className="card stack">
              <p className="muted" style={{ margin: 0 }}>
                Создайте страницу — получите одну ссылку для профиля. Аура покажет ученикам только свободное время с учётом вашего календаря, занятий и перерывов.
              </p>
              <div>
                <button className="btn btn-primary" onClick={() => setEditing(0)}>
                  <Plus size={16} /> Создать страницу записи
                </button>
              </div>
            </div>
          )}
          {pages?.map((page) =>
            editing === page.id ? (
              <PageEditor key={page.id} api={api} page={editingPage} onSaved={onSaved} onCancel={() => setEditing(null)} />
            ) : (
              <PageCard key={page.id} api={api} page={page} onEdit={() => setEditing(page.id)} onChanged={replacePage} onDelete={removePage} />
            ),
          )}
          {pages && pages.length > 0 && !canCreate && editing === null && (
            <span className="hint">
              На тарифе {view.name} — {view.maxPages} {plural(view.maxPages, 'страница', 'страницы', 'страниц')} записи.
              {!view.pro && ' В Pro — до 5 страниц.'}
            </span>
          )}
        </section>

        <section className="stack">
          <div className="row wrap" style={{ justifyContent: 'space-between' }}>
            <h2 className="title-sm" style={{ margin: '0 2px' }}>
              Ближайшие занятия
            </h2>
            <div className="row wrap" style={{ gap: 12 }}>
              <label className="row small muted" style={{ gap: 8 }}>
                <Switch on={showCancelled} onChange={setShowCancelled} label="Показывать отменённые" />
                Отменённые
              </label>
              <Segmented label="Период" value={days} onChange={setDays} options={RANGES} />
            </div>
          </div>
          <div className="card flush list">
            {bookings === null && (
              <div className="empty">
                <Spinner small />
              </div>
            )}
            {bookings !== null && agenda.length === 0 && <Empty>Занятий пока нет. Отправьте ссылку на страницу записи — новые записи появятся здесь и во «Входящих».</Empty>}
            {agenda.map((day) => (
              <div key={day.key} role="group" aria-label={day.title}>
                <div className="booking-day">{day.title}</div>
                {day.items.map((item) => (
                  <AgendaRow key={`${item.booking.id}-${item.occurrence.index}`} item={item} onCancel={cancel} />
                ))}
              </div>
            ))}
          </div>
          <span className="hint">Время — в часовом поясе этого устройства. Занятия занимают ваше время: Аура не предложит его в приглашениях, на других страницах записи и в переговорах.</span>
        </section>
      </div>
    </div>
  );
}

/** Свежие занятия из ответа booking.cancel поверх тех, что были в нашем окне. */
function mergeOccurrences(old: BookingRow, fresh: BookingRow) {
  const byIndex = new Map((fresh.occurrences ?? []).map((o) => [o.index, o]));
  return (old.occurrences ?? []).map((o) => byIndex.get(o.index) ?? (fresh.status === 'cancelled' ? { ...o, status: 'cancelled' as const } : o));
}

function PlanCard({ pages, view, loaded }: { pages: number; view: ReturnType<typeof planView>; loaded: boolean }) {
  if (!loaded) return null;
  return (
    <div className="card row wrap booking-plan">
      <span className={`badge ${view.pro ? 'accent' : ''}`}>{view.name}</span>
      <div className="body" style={{ flex: 1, minWidth: 200 }}>
        <div>
          {view.pro ? `Pro ${view.until}` : 'Бесплатный тариф'}
          <span className="muted">
            {' '}
            · страниц: {pages} из {view.maxPages} · серия — до {view.maxWeeks} {plural(view.maxWeeks, 'занятия', 'занятий', 'занятий')}
          </span>
        </div>
        <div className="small muted">
          {view.pro ? 'Продлевается за каждого коллегу, который начнёт пользоваться Аурой.' : 'Pro — 5 страниц и серии до 26 занятий. Даётся на 30 дней вам и коллеге, которого вы пригласите.'}
        </div>
      </div>
      <Link className="btn btn-ghost btn-sm" to="/settings">
        Пригласить коллегу
      </Link>
    </div>
  );
}

function PageCard({
  api,
  page,
  onEdit,
  onChanged,
  onDelete,
}: {
  api: BookingApi;
  page: BookingPage;
  onEdit: () => void;
  onChanged: (page: BookingPage) => void;
  onDelete: (id: number) => Promise<void>;
}) {
  const { notify } = useData();
  const [confirm, setConfirm] = useState<'rotate' | 'delete' | null>(null);
  const [busy, setBusy] = useState(false);
  const link = pageLink(page, currentLocation());

  const run = async (fn: () => Promise<void>) => {
    setBusy(true);
    try {
      await fn();
    } finally {
      setBusy(false);
      setConfirm(null);
    }
  };

  const setActive = (active: boolean) =>
    run(async () => {
      try {
        const reply = await api.savePage(formToInput({ ...pageToForm(page), active }, page.id));
        onChanged(reply.page);
        notify(active ? 'Запись включена' : 'Запись выключена — по ссылке её не открыть');
      } catch (err) {
        notify(bookingErrorText(err), 'error');
      }
    });

  const rotate = () =>
    run(async () => {
      try {
        const reply = await api.rotatePage(page.id);
        onChanged(reply.page);
        notify('Новая ссылка готова — старая больше не работает');
      } catch (err) {
        notify(bookingErrorText(err), 'error');
      }
    });

  return (
    <div className={`card stack ${page.active ? '' : 'booking-off'}`}>
      <div className="card-head" style={{ marginBottom: 0, alignItems: 'flex-start' }}>
        <div style={{ minWidth: 0 }}>
          <h3 className="title-md ellipsis">{page.title}</h3>
          <div className="small muted">
            {[durationLabel(page.duration_minutes), weekdaysSummary(page.weekdays), hoursSummary(page.hours), `на ${page.window_days} ${plural(page.window_days, 'день', 'дня', 'дней')} вперёд`].join(' · ')}
          </div>
          <div className="small faint">
            {[
              page.min_notice_hours ? `не позже чем за ${page.min_notice_hours} ч` : 'без минимального срока',
              page.buffer_minutes ? `перерыв ${page.buffer_minutes} мин` : '',
              page.allow_weekly ? 'можно каждую неделю' : '',
            ]
              .filter(Boolean)
              .join(' · ')}
          </div>
        </div>
        <label className="row small muted" style={{ gap: 8, flex: 'none' }}>
          {page.active ? 'Включена' : 'Выключена'}
          <Switch on={page.active} onChange={(v) => void setActive(v)} label={page.active ? 'Выключить запись' : 'Включить запись'} disabled={busy} />
        </label>
      </div>
      {page.description && <div className="small muted invite-comment">{page.description}</div>}
      <LinkBox link={link} label={link.kind === 'url' ? 'Ссылка на страницу записи' : 'Код страницы записи'} share={{ title: page.title, text: `${page.title} — запишитесь на удобное время` }} />
      {link.kind === 'token' && (
        <div className="notice warn">
          Сервер не знает свой публичный адрес, а это приложение открыто локально — готовую ссылку не собрать. Задайте на сервере AURA_PUBLIC_URL (адрес веб-клиента). Пока ученику можно открыть веб-клиент Aura с адресом #/b/ и этим кодом.
        </div>
      )}
      {confirm ? (
        <div className="row wrap" style={{ gap: 8 }}>
          <span className="small muted">{confirm === 'rotate' ? 'Старая ссылка перестанет работать. Записи и личные ссылки учеников останутся.' : 'Удалить страницу? Записи останутся в списке занятий.'}</span>
          <button className="btn btn-danger btn-sm" disabled={busy} onClick={() => void (confirm === 'rotate' ? rotate() : run(() => onDelete(page.id)))}>
            {busy ? <Spinner small /> : confirm === 'rotate' ? 'Сменить ссылку' : 'Удалить'}
          </button>
          <button className="btn btn-ghost btn-sm" disabled={busy} onClick={() => setConfirm(null)}>
            Отмена
          </button>
        </div>
      ) : (
        <div className="row wrap" style={{ gap: 8 }}>
          <button className="btn btn-secondary btn-sm" onClick={onEdit}>
            <Pencil size={14} /> Изменить
          </button>
          <button className="btn btn-ghost btn-sm" onClick={() => setConfirm('rotate')}>
            <RotateCw size={14} /> Новая ссылка
          </button>
          <button className="btn btn-ghost btn-sm" onClick={() => setConfirm('delete')}>
            <Trash2 size={14} /> Удалить
          </button>
        </div>
      )}
    </div>
  );
}

function NumberField({ label, value, onChange, min, max, error, hint }: { label: string; value: number; onChange: (v: number) => void; min: number; max: number; error?: string; hint?: string }) {
  return (
    <Field label={label} error={error} hint={hint}>
      <input
        className={`input ${error ? 'invalid' : ''}`}
        type="number"
        inputMode="numeric"
        min={min}
        max={max}
        step={1}
        value={Number.isNaN(value) ? '' : value}
        onChange={(e) => onChange(e.target.value === '' ? NaN : Number(e.target.value))}
      />
    </Field>
  );
}

function PageEditor({ api, page, onSaved, onCancel }: { api: BookingApi; page: BookingPage | null; onSaved: (page: BookingPage) => void; onCancel: () => void }) {
  const [form, setForm] = useState<PageForm>(() => pageToForm(page));
  const [errors, setErrors] = useState<PageFormErrors & { form?: string }>({});
  const [busy, setBusy] = useState(false);
  const set = <K extends keyof PageForm>(key: K, value: PageForm[K]) => setForm((f) => ({ ...f, [key]: value }));

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const next = pageFormErrors(form);
    setErrors(next);
    if (hasErrors(next)) return;
    setBusy(true);
    try {
      const reply = await api.savePage(formToInput(form, page?.id));
      onSaved(reply.page);
    } catch (err) {
      setErrors({ form: bookingErrorText(err) });
    } finally {
      setBusy(false);
    }
  };

  return (
    <form className="card stack" onSubmit={submit} noValidate aria-label={page ? 'Изменить страницу записи' : 'Новая страница записи'}>
      <h3 className="title-md">{page ? 'Изменить страницу' : 'Новая страница записи'}</h3>
      {errors.form && <div className="notice error">{errors.form}</div>}
      <Field label="Название" error={errors.title} hint="Ученик увидит его на странице записи">
        <input className={`input ${errors.title ? 'invalid' : ''}`} placeholder="Математика, 60 минут" value={form.title} maxLength={PAGE_TITLE_MAX} onChange={(e) => set('title', e.target.value)} />
      </Field>
      <Field label="Описание" error={errors.description} hint="Необязательно — формат, адрес, что подготовить">
        <textarea className="textarea" style={{ minHeight: 64 }} value={form.description} maxLength={PAGE_DESCRIPTION_MAX} onChange={(e) => set('description', e.target.value)} />
      </Field>
      <div className="grid-2">
        <NumberField label="Длительность, мин" value={form.duration} min={15} max={240} onChange={(v) => set('duration', v)} error={errors.duration} />
        <NumberField label="Запись вперёд, дней" value={form.windowDays} min={1} max={60} onChange={(v) => set('windowDays', v)} error={errors.windowDays} />
        <NumberField label="Не позже чем за, часов" value={form.noticeHours} min={0} max={168} onChange={(v) => set('noticeHours', v)} error={errors.noticeHours} hint="0 — можно записаться прямо перед началом" />
        <NumberField label="Перерыв между занятиями, мин" value={form.bufferMinutes} min={0} max={120} onChange={(v) => set('bufferMinutes', v)} error={errors.bufferMinutes} />
      </div>
      <div className="field">
        <span>Дни</span>
        <div className="row wrap" style={{ gap: 6 }} role="group" aria-label="Дни недели">
          {WEEKDAY_SHORT.map((label, day) => (
            <button key={label} type="button" className={`chip ${form.weekdays.includes(day) ? 'active' : ''}`} aria-pressed={form.weekdays.includes(day)} onClick={() => set('weekdays', toggle(form.weekdays, day))}>
              {label}
            </button>
          ))}
        </div>
        {errors.weekdays && <span className="field-error">{errors.weekdays}</span>}
      </div>
      <div className="field">
        <span>Часы занятий</span>
        <div className="booking-hours" role="group" aria-label="Часы занятий">
          {HOURS.map((h) => (
            <button key={h} type="button" className={`chip ${form.hours.includes(h) ? 'active' : ''}`} aria-pressed={form.hours.includes(h)} onClick={() => set('hours', toggle(form.hours, h))}>
              {h}:00
            </button>
          ))}
        </div>
        <span className="hint">
          {form.hours.length ? `Занятие целиком внутри: ${hoursSummary(form.hours)} (ваше местное время). ` : 'Не выбрано — рабочие часы из настроек Ауры. '}
          {form.hours.length > 0 && (
            <button type="button" className="link" onClick={() => set('hours', [])}>
              Сбросить
            </button>
          )}
        </span>
      </div>
      <div>
        <div className="setting">
          <Repeat size={16} className="faint" aria-hidden="true" />
          <div className="body">
            <div className="label">Можно записаться каждую неделю</div>
            <div className="desc">Ученик выберет «Каждую неделю» — Аура создаст серию занятий в то же время.</div>
          </div>
          <Switch on={form.allowWeekly} onChange={(v) => set('allowWeekly', v)} label="Можно записаться каждую неделю" />
        </div>
        <div className="setting">
          <CalendarCheck size={16} className="faint" aria-hidden="true" />
          <div className="body">
            <div className="label">Запись включена</div>
            <div className="desc">Выключенная страница по ссылке не открывается; записи остаются.</div>
          </div>
          <Switch on={form.active} onChange={(v) => set('active', v)} label="Запись включена" />
        </div>
      </div>
      <div className="row wrap">
        <button className="btn btn-primary" type="submit" disabled={busy}>
          {busy ? <Spinner small /> : page ? 'Сохранить' : 'Создать страницу'}
        </button>
        <button className="btn btn-ghost" type="button" onClick={onCancel} disabled={busy}>
          Отмена
        </button>
      </div>
    </form>
  );
}

function AgendaRow({ item, onCancel }: { item: AgendaItem; onCancel: (item: AgendaItem, whole: boolean) => Promise<void> }) {
  const [confirming, setConfirming] = useState(false);
  const [busy, setBusy] = useState<'one' | 'all' | null>(null);
  const { booking } = item;
  const many = booking.weekly && upcomingCount(booking) > 1;

  const run = async (whole: boolean, button: 'one' | 'all') => {
    setBusy(button);
    await onCancel(item, whole);
    setBusy(null);
    setConfirming(false);
  };

  return (
    <div className={`list-row invite-row ${item.cancelled ? 'booking-dim' : ''}`}>
      <span className="booking-time">{item.time}</span>
      <div className="body">
        <div className="ellipsis">
          <span className={item.cancelled ? 'booking-struck' : ''}>{booking.guest_name || 'Гость'}</span>
          <span className="muted"> · {booking.title || 'Занятие'}</span>
        </div>
        {(item.series || booking.guest_email) && <div className="small muted ellipsis">{[item.series, booking.guest_email].filter(Boolean).join(' · ')}</div>}
        {booking.guest_comment && <div className="small muted invite-comment">«{booking.guest_comment}»</div>}
      </div>
      {item.cancelled && <span className="badge">Отменено</span>}
      {!item.cancelled && booking.weekly && <span className="badge accent">Серия</span>}
      {item.canCancel &&
        (confirming ? (
          <div className="row wrap" style={{ gap: 6 }}>
            <button className="btn btn-danger btn-sm" disabled={busy !== null} onClick={() => void run(!booking.weekly, 'one')}>
              {busy === 'one' ? <Spinner small /> : booking.weekly ? 'Это занятие' : 'Отменить запись'}
            </button>
            {many && (
              <button className="btn btn-danger btn-sm" disabled={busy !== null} onClick={() => void run(true, 'all')}>
                {busy === 'all' ? <Spinner small /> : 'Всю серию'}
              </button>
            )}
            <button className="btn btn-ghost btn-sm" disabled={busy !== null} onClick={() => setConfirming(false)}>
              Нет
            </button>
          </div>
        ) : (
          <button className="btn btn-ghost btn-sm" onClick={() => setConfirming(true)}>
            Отменить
          </button>
        ))}
    </div>
  );
}
