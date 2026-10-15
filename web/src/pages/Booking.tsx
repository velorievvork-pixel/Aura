/*
  «Запись» — страницы записи владельца (booking.*).

  Постоянная публичная ссылка #/b/<slug>: гость без Selfvoy сам выбирает свободное
  время (Selfvoy подбирает окна в рабочих часах владельца), при желании — сразу
  еженедельную серию. Здесь владелец заводит и настраивает страницы, ставит их
  на паузу, видит записи гостей и может их отменить. Список обновляется по
  уведомлениям booking.* (гость записался, перенёс, отменил).
*/
import { CalendarClock, Copy, Pause, Pencil, Play, Share2, Trash2, X } from 'lucide-react';
import { useCallback, useEffect, useMemo, useRef, useState, type FormEvent } from 'react';
import { Empty, Field, Segmented, Spinner, Switch } from '../components/ui';
import { plural, when } from '../lib/format';
import {
  bookingOwnerErrorText,
  bookingStatus,
  DESCRIPTION_MAX,
  PAGE_DEFAULTS,
  PAGE_STATUS_LABEL,
  pageFields,
  pageFormErrors,
  pagePath,
  publicLink,
  sortedOccurrences,
  TITLE_MAX,
  type PageFormErrors,
} from '../lib/booking';
import { hasErrors } from '../lib/invite';
import { makeBookingApi, type Booking, type BookingApi, type BookingPage as Page, type PageFields } from '../net/bookingApi';
import { isNetworkError } from '../net/socket';
import { useData } from '../state/data';
import { useSession } from '../state/session';
import { canShare, copyText } from '../lib/clipboard';

const DURATIONS = [
  { value: '15', label: '15 мин' },
  { value: '30', label: '30 мин' },
  { value: '45', label: '45 мин' },
  { value: '60', label: '1 час' },
  { value: '90', label: '1,5 часа' },
];

const HORIZONS = [
  { value: '7', label: 'Неделя' },
  { value: '14', label: '2 недели' },
  { value: '30', label: 'Месяц' },
  { value: '60', label: '2 месяца' },
];

const here = () => (typeof location === 'undefined' ? null : location);

export function BookingPage() {
  const { socket, conn } = useSession();
  const { notify } = useData();
  const api = useMemo(() => makeBookingApi(socket), [socket]);
  const [pages, setPages] = useState<Page[] | null>(null);
  const [bookings, setBookings] = useState<Booking[] | null>(null);
  const [error, setError] = useState('');
  const [editing, setEditing] = useState<Page | 'new' | null>(null);
  const seq = useRef(0);

  const load = useCallback(async () => {
    const mine = ++seq.current;
    try {
      const [p, b] = await Promise.all([api.listPages(), api.list(100)]);
      if (mine !== seq.current) return;
      setPages(p.pages ?? []);
      setBookings(b.bookings ?? []);
      setError('');
    } catch (e) {
      if (mine !== seq.current) return;
      // Сетевые ошибки не шумим: есть баннер «нет связи», список перезагрузится при переподключении.
      if (!isNetworkError(e)) setError(bookingOwnerErrorText(e));
    }
  }, [api]);

  useEffect(() => {
    if (conn === 'open') void load();
  }, [conn, load]);

  useEffect(
    () =>
      socket.onEvent((event) => {
        if (event.event !== 'notification.new') return;
        const kind = String((event.payload as { kind?: unknown }).kind ?? '');
        if (kind.startsWith('booking.')) void load();
      }),
    [socket, load],
  );

  /*
    После изменения: ответ сервера показываем сразу, а затем перечитываем оба списка.
    load() сам отбрасывает ответ загрузки, начатой раньше (её данные могли быть до
    изменения), — но та загрузка могла быть начата уведомлением booking.created, и
    без повторного запроса новая запись так и не появилась бы в списке.
  */
  const refresh = () => void load();

  const saved = (page: Page) => {
    setPages((list) => {
      const rest = (list ?? []).filter((x) => x.id !== page.id);
      return editing === 'new' ? [page, ...rest] : (list ?? []).map((x) => (x.id === page.id ? page : x));
    });
    setEditing(null);
    refresh();
  };

  const setStatus = async (page: Page, status: 'active' | 'paused') => {
    try {
      const reply = await api.updatePage(page.id, { status });
      setPages((list) => (list ?? []).map((x) => (x.id === page.id ? reply.page : x)));
      refresh();
      notify(status === 'paused' ? 'Запись на паузе — новые гости не смогут записаться' : 'Запись снова открыта');
    } catch (e) {
      notify(bookingOwnerErrorText(e), 'error');
    }
  };

  const remove = async (page: Page) => {
    try {
      await api.deletePage(page.id);
      setPages((list) => (list ?? []).filter((x) => x.id !== page.id));
      refresh();
      notify('Страница удалена. Уже сделанные записи остаются в силе.');
    } catch (e) {
      notify(bookingOwnerErrorText(e), 'error');
    }
  };

  const cancelBooking = async (booking: Booking) => {
    try {
      const reply = await api.ownerCancel(booking.id);
      setBookings((list) => (list ?? []).map((x) => (x.id === booking.id ? reply.booking : x)));
      refresh();
    } catch (e) {
      notify(bookingOwnerErrorText(e), 'error');
      refresh();
    }
  };

  return (
    <div className="page">
      <header className="page-head">
        <div>
          <h1 className="title-xl">Запись</h1>
          <div className="sub">Постоянная ссылка: люди без Selfvoy сами выбирают свободное время в вашем расписании.</div>
        </div>
        {editing === null && pages !== null && (
          <button className="btn btn-primary" onClick={() => setEditing('new')}>
            <CalendarClock size={16} /> Новая страница
          </button>
        )}
      </header>

      <div className="stack-lg">
        {error && <div className="notice error">{error}</div>}
        {editing !== null && <PageForm api={api} page={editing === 'new' ? null : editing} onSaved={saved} onClose={() => setEditing(null)} />}

        <section className="stack">
          <h2 className="title-sm section-title">Страницы</h2>
          {pages === null && !error && (
            <div className="card empty">
              <Spinner small />
            </div>
          )}
          {pages?.length === 0 && editing === null && (
            <div className="card">
              <Empty>Создайте первую страницу — например, «Консультация, 30 минут» — и отправьте ссылку клиентам.</Empty>
            </div>
          )}
          {pages?.map((page) => (
            <PageCard key={page.id} page={page} onEdit={() => setEditing(page)} onStatus={setStatus} onDelete={remove} />
          ))}
        </section>

        <section className="stack">
          <h2 className="title-sm section-title">Записи гостей</h2>
          <div className="card flush list">
            {bookings === null && !error && (
              <div className="empty">
                <Spinner small />
              </div>
            )}
            {bookings?.length === 0 && <Empty>Здесь появятся люди, которые записались по вашим ссылкам.</Empty>}
            {bookings?.map((b) => <BookingRow key={b.id} booking={b} onCancel={cancelBooking} />)}
          </div>
        </section>
      </div>
    </div>
  );
}

function PageCard({ page, onEdit, onStatus, onDelete }: { page: Page; onEdit: () => void; onStatus: (page: Page, status: 'active' | 'paused') => Promise<void>; onDelete: (page: Page) => Promise<void> }) {
  const [copied, setCopied] = useState(false);
  const [confirming, setConfirming] = useState(false);
  const [busy, setBusy] = useState(false);
  const link = publicLink(pagePath(page.slug), page.url, here());
  const text = link ?? `#${pagePath(page.slug)}`;

  const copy = async () => {
    const ok = await copyText(text);
    setCopied(ok);
    if (ok) setTimeout(() => setCopied(false), 2000);
  };

  const run = async (action: () => Promise<void>) => {
    setBusy(true);
    try {
      await action();
    } finally {
      setBusy(false);
      setConfirming(false);
    }
  };

  const facts = [
    `${page.duration_minutes} мин`,
    `на ${page.days_ahead} ${plural(page.days_ahead, 'день', 'дня', 'дней')} вперёд`,
    `до ${page.max_per_day} в день`,
    page.max_series > 1 ? `серия до ${page.max_series}` : '',
    page.weekends ? 'и в выходные' : 'только будни',
  ].filter(Boolean);

  return (
    <div className="card stack">
      <div className="card-head" style={{ marginBottom: 0 }}>
        <div style={{ minWidth: 0 }}>
          <h3 className="title-md ellipsis">{page.title}</h3>
          <div className="small muted">{facts.join(' · ')}</div>
        </div>
        <span className={`badge ${page.status === 'active' ? 'green' : 'amber'}`}>{PAGE_STATUS_LABEL[page.status] ?? page.status}</span>
      </div>
      {page.description && <div className="small muted invite-note">{page.description}</div>}
      <div className="invite-link">
        <input className="input mono" readOnly value={text} aria-label="Ссылка на страницу записи" onFocus={(e) => e.currentTarget.select()} />
        <button type="button" className="btn btn-secondary" onClick={copy}>
          <Copy size={15} /> {copied ? 'Скопировано' : 'Скопировать'}
        </button>
        {link && canShare() && (
          <button
            type="button"
            className="btn btn-secondary"
            onClick={() => navigator.share({ title: page.title, text: `${page.title} — запишитесь на удобное время`, url: link }).catch(() => undefined)}
          >
            <Share2 size={15} /> Поделиться
          </button>
        )}
      </div>
      {!link && (
        <div className="notice warn">
          Сервер не знает свой публичный адрес, а это приложение открыто локально — полную ссылку не собрать. Задайте на сервере SELFVOY_PUBLIC_URL (адрес веб-клиента).
        </div>
      )}
      <div className="row wrap" style={{ gap: 8 }}>
        <span className="small muted" style={{ marginRight: 'auto' }}>
          {page.upcoming > 0 ? `Предстоит: ${page.upcoming} ${plural(page.upcoming, 'встреча', 'встречи', 'встреч')}` : 'Пока никто не записался'}
        </span>
        {confirming ? (
          <>
            <span className="small muted">Удалить страницу?</span>
            <button className="btn btn-danger btn-sm" disabled={busy} onClick={() => run(() => onDelete(page))}>
              {busy ? <Spinner small /> : 'Удалить'}
            </button>
            <button className="btn btn-ghost btn-sm" disabled={busy} onClick={() => setConfirming(false)}>
              Нет
            </button>
          </>
        ) : (
          <>
            <button className="btn btn-ghost btn-sm" onClick={onEdit}>
              <Pencil size={14} /> Изменить
            </button>
            {page.status === 'active' ? (
              <button className="btn btn-ghost btn-sm" disabled={busy} onClick={() => run(() => onStatus(page, 'paused'))}>
                <Pause size={14} /> Пауза
              </button>
            ) : (
              <button className="btn btn-ghost btn-sm" disabled={busy} onClick={() => run(() => onStatus(page, 'active'))}>
                <Play size={14} /> Открыть
              </button>
            )}
            <button className="btn btn-ghost btn-sm" onClick={() => setConfirming(true)} aria-label="Удалить страницу">
              <Trash2 size={14} />
            </button>
          </>
        )}
      </div>
    </div>
  );
}

function NumberField({ label, value, min, max, onChange, hint }: { label: string; value: number; min: number; max: number; onChange: (v: number) => void; hint?: string }) {
  return (
    <Field label={label} hint={hint}>
      <input
        className="input"
        type="number"
        inputMode="numeric"
        min={min}
        max={max}
        step={1}
        value={Number.isNaN(value) ? '' : value}
        onChange={(e) => onChange(e.target.value === '' ? Number.NaN : Number(e.target.value))}
      />
    </Field>
  );
}

function PageForm({ api, page, onSaved, onClose }: { api: BookingApi; page: Page | null; onSaved: (page: Page) => void; onClose: () => void }) {
  const [form, setForm] = useState<PageFields>(page ? pageFields(page) : PAGE_DEFAULTS);
  const [errors, setErrors] = useState<PageFormErrors & { form?: string }>({});
  const [busy, setBusy] = useState(false);
  const set = <K extends keyof PageFields>(key: K, value: PageFields[K]) => {
    setForm((f) => ({ ...f, [key]: value }));
    // Исправленное поле больше не подсвечиваем ошибкой.
    const field: keyof PageFormErrors = key === 'title' ? 'title' : key === 'description' ? 'description' : 'numbers';
    setErrors((e) => (e[field] ? { ...e, [field]: undefined } : e));
  };
  const durationOptions = DURATIONS.some((d) => d.value === String(form.duration_minutes))
    ? DURATIONS
    : [...DURATIONS, { value: String(form.duration_minutes), label: `${form.duration_minutes} мин` }];
  const horizonOptions = HORIZONS.some((h) => h.value === String(form.days_ahead))
    ? HORIZONS
    : [...HORIZONS, { value: String(form.days_ahead), label: `${form.days_ahead} дн.` }];

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    const next = pageFormErrors(form);
    setErrors(next);
    if (hasErrors(next)) return;
    setBusy(true);
    try {
      const fields = { ...form, title: form.title.trim(), description: (form.description ?? '').trim() };
      const reply = page ? await api.updatePage(page.id, fields) : await api.createPage(fields);
      onSaved(reply.page);
    } catch (error) {
      setErrors({ form: bookingOwnerErrorText(error) });
    } finally {
      setBusy(false);
    }
  };

  return (
    <form className="card stack" onSubmit={submit} noValidate>
      <div className="card-head" style={{ marginBottom: 0 }}>
        <h2 className="title-md">{page ? 'Изменить страницу' : 'Новая страница записи'}</h2>
        <button type="button" className="icon-btn" onClick={onClose} aria-label="Закрыть">
          <X size={16} />
        </button>
      </div>
      {errors.form && <div className="notice error">{errors.form}</div>}
      <Field label="Название" error={errors.title}>
        <input className={`input ${errors.title ? 'invalid' : ''}`} placeholder="Например, консультация" value={form.title} maxLength={TITLE_MAX} onChange={(e) => set('title', e.target.value)} />
      </Field>
      <Field label="Описание для гостей" error={errors.description} hint="Необязательно — что будет на встрече, где и как">
        <textarea className="textarea" style={{ minHeight: 64 }} value={form.description ?? ''} maxLength={DESCRIPTION_MAX} onChange={(e) => set('description', e.target.value)} />
      </Field>
      <div className="row wrap" style={{ gap: 16 }}>
        <div className="field">
          <span>Длительность</span>
          <Segmented label="Длительность" value={String(form.duration_minutes)} onChange={(v) => set('duration_minutes', Number(v))} options={durationOptions} />
        </div>
        <div className="field">
          <span>Запись открыта на</span>
          <Segmented label="Запись открыта на" value={String(form.days_ahead)} onChange={(v) => set('days_ahead', Number(v))} options={horizonOptions} />
        </div>
      </div>
      <div className="grid-2">
        <NumberField label="Встреч в день, не больше" value={form.max_per_day} min={1} max={16} onChange={(v) => set('max_per_day', v)} />
        <NumberField label="Запас до встречи, часов" value={form.notice_hours} min={0} max={168} onChange={(v) => set('notice_hours', v)} hint="Записаться можно не позже чем за столько часов" />
        <NumberField label="Серия, встреч" value={form.max_series} min={1} max={12} onChange={(v) => set('max_series', v)} hint="Больше 1 — гость может взять встречи раз в неделю" />
        <div className="field">
          <span>Выходные</span>
          <div className="row" style={{ gap: 10, minHeight: 40 }}>
            <Switch on={form.weekends} onChange={(v) => set('weekends', v)} label="Принимать запись на выходные" />
            <span className="small muted">{form.weekends ? 'Будни и выходные' : 'Только будни'}</span>
          </div>
        </div>
      </div>
      {errors.numbers && <div className="field-error">{errors.numbers}</div>}
      <div className="row wrap">
        <button className="btn btn-primary" type="submit" disabled={busy}>
          {busy ? <Spinner small /> : page ? 'Сохранить' : 'Создать страницу'}
        </button>
        <span className="hint">Гости видят только свободное время в ваших рабочих часах — без названий событий.</span>
      </div>
    </form>
  );
}

function BookingRow({ booking, onCancel }: { booking: Booking; onCancel: (booking: Booking) => Promise<void> }) {
  const [confirming, setConfirming] = useState(false);
  const [busy, setBusy] = useState(false);
  const status = bookingStatus(booking);
  const occurrences = sortedOccurrences(booking.occurrences);
  const active = occurrences.filter((o) => o.status === 'active');
  const shown = (active.length ? active : occurrences).slice(0, 3);
  const more = (active.length ? active : occurrences).length - shown.length;

  return (
    <div className="list-row invite-row">
      <div className="body">
        <div className="ellipsis">
          {booking.guest_name}
          <span className="muted"> · {booking.page_title || 'Запись'}</span>
        </div>
        <div className="small muted">
          {shown.map((o) => when(o.start)).join(' · ')}
          {more > 0 ? ` · ещё ${more}` : ''}
          {booking.reschedules > 0 ? ` · переносов: ${booking.reschedules}` : ''}
        </div>
        {booking.guest_email && <div className="small faint ellipsis">{booking.guest_email}</div>}
        {booking.guest_comment && <div className="small muted invite-comment">«{booking.guest_comment}»</div>}
      </div>
      {confirming ? (
        <div className="row" style={{ gap: 6 }}>
          <span className="small muted">Отменить все встречи?</span>
          <button
            className="btn btn-danger btn-sm"
            disabled={busy}
            onClick={async () => {
              setBusy(true);
              await onCancel(booking);
              setBusy(false);
              setConfirming(false);
            }}
          >
            {busy ? <Spinner small /> : 'Да'}
          </button>
          <button className="btn btn-ghost btn-sm" disabled={busy} onClick={() => setConfirming(false)}>
            Нет
          </button>
        </div>
      ) : (
        <div className="row" style={{ gap: 8 }}>
          <span className={`badge ${status.tone}`}>{status.label}</span>
          {status.label === 'Предстоит' && (
            <button className="btn btn-ghost btn-sm" onClick={() => setConfirming(true)}>
              Отменить
            </button>
          )}
        </div>
      )}
    </div>
  );
}
