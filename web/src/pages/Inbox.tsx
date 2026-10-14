import { Bell, Check, CheckCheck, ShieldAlert, X } from 'lucide-react';
import { Link } from 'react-router-dom';
import { Empty } from '../components/ui';
import { toolTitle, when } from '../lib/format';
import { noticeDetail, noticeKindLabel, noticeTarget } from '../lib/notifications';
import { useData } from '../state/data';

export function Inbox() {
  const { confirmations, notices, unread, approve, deny, readAll } = useData();
  return (
    <div className="page">
      <header className="page-head">
        <div>
          <h1 className="title-xl">Входящие</h1>
          <div className="sub">Решения, которых ждёт Аура, и уведомления.</div>
        </div>
        {unread > 0 && (
          <button className="btn btn-ghost" onClick={readAll}>
            <CheckCheck size={16} /> Прочитать все
          </button>
        )}
      </header>
      <div className="stack-lg">
        <section>
          <h2 className="title-sm section-title">Ждут решения · {confirmations.length}</h2>
          <div className="card flush list">
            {confirmations.length === 0 && <Empty>Аура ничего не ждёт. Опасные действия — письма, сообщения, бронь — появятся здесь до выполнения.</Empty>}
            {confirmations.map((c) => (
              <div key={c.id} className="list-row" style={{ flexWrap: 'wrap' }}>
                <ShieldAlert size={16} style={{ color: 'var(--warning)' }} />
                <div className="body">
                  <div>{c.summary || toolTitle(c.tool)}</div>
                  <div className="small muted">
                    {toolTitle(c.tool)}
                    {c.created_at ? ` · ${when(c.created_at)}` : ''}
                  </div>
                </div>
                <div className="row">
                  <button className="btn btn-primary btn-sm" onClick={() => approve(c.id)}>
                    <Check size={14} /> Подтвердить
                  </button>
                  <button className="btn btn-ghost btn-sm" onClick={() => deny(c.id)}>
                    <X size={14} /> Отклонить
                  </button>
                </div>
              </div>
            ))}
          </div>
        </section>

        <section>
          <h2 className="title-sm section-title">Уведомления</h2>
          <div className="card flush list">
            {notices.length === 0 && <Empty>Уведомлений нет.</Empty>}
            {notices.map((n) => {
              const label = noticeKindLabel(n.kind);
              const detail = noticeDetail(n);
              const target = noticeTarget(n.kind);
              const content = (
                <>
                  {n.read ? <Bell size={16} className="faint" /> : <span className="dot accent" style={{ margin: '0 4px' }} />}
                  <div className="body">
                    {label && <div className="tiny faint">{label}</div>}
                    <div style={{ fontWeight: n.read ? 400 : 500 }}>{n.title || label}</div>
                    {detail && <div className="small muted">{detail}</div>}
                  </div>
                  <span className="meta">{when(n.created_at)}</span>
                </>
              );
              return target ? (
                <Link key={n.id} to={target} className="list-row">
                  {content}
                </Link>
              ) : (
                <div key={n.id} className="list-row">
                  {content}
                </div>
              );
            })}
          </div>
        </section>
      </div>
    </div>
  );
}
