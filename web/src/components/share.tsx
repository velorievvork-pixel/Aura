/*
  Поделиться ссылкой: поле со ссылкой (или идентификатором), «Скопировать»,
  «Поделиться» (системное окно, где оно есть) и сохранение файла (.ics, .txt).
  Общие для приглашений, страниц записи и «Пригласить коллегу».
*/
import { Copy, Share2 } from 'lucide-react';
import { useState, type ReactNode } from 'react';
import { linkText, type PublicLink } from '../lib/publicLink';

/** Копирование в буфер: Clipboard API, иначе — через скрытое поле (старые WebView). */
export async function copyText(text: string): Promise<boolean> {
  try {
    await navigator.clipboard.writeText(text);
    return true;
  } catch {
    /* нет доступа к буферу — пробуем по-старому */
  }
  try {
    const area = document.createElement('textarea');
    area.value = text;
    area.setAttribute('readonly', '');
    area.style.position = 'fixed';
    area.style.opacity = '0';
    document.body.appendChild(area);
    area.select();
    const ok = document.execCommand('copy');
    area.remove();
    return ok;
  } catch {
    return false;
  }
}

export const canShare = () => typeof navigator !== 'undefined' && typeof navigator.share === 'function';

/** Скачать текст файлом (в браузере и Electron; в WebView телефонов — системный обработчик). */
export function downloadFile(name: string, text: string, type: string): boolean {
  try {
    const blob = new Blob([text], { type });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
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

export interface ShareText {
  title: string;
  text: string;
}

/**
  Поле со ссылкой и кнопки. Для `kind: 'token'` (ссылку не собрать) показывается
  идентификатор, «Поделиться» прячется. `extra` — дополнительные кнопки в ряд.
*/
export function LinkBox({ link, label, share, extra }: { link: PublicLink; label: string; share?: ShareText; extra?: ReactNode }) {
  const [copied, setCopied] = useState(false);
  const [copyFailed, setCopyFailed] = useState(false);
  const text = linkText(link);

  const copy = async () => {
    const ok = await copyText(text);
    setCopied(ok);
    setCopyFailed(!ok);
    if (ok) setTimeout(() => setCopied(false), 2000);
  };

  const doShare = async () => {
    if (link.kind !== 'url') return;
    try {
      await navigator.share({ title: share?.title, text: share?.text, url: link.url });
    } catch {
      /* пользователь закрыл окно «Поделиться» */
    }
  };

  return (
    <>
      <div className="invite-link">
        <input className="input mono" readOnly value={text} aria-label={label} onFocus={(e) => e.currentTarget.select()} />
        <button type="button" className="btn btn-secondary" onClick={copy}>
          <Copy size={15} /> {copied ? 'Скопировано' : 'Скопировать'}
        </button>
        {link.kind === 'url' && share && canShare() && (
          <button type="button" className="btn btn-secondary" onClick={doShare}>
            <Share2 size={15} /> Поделиться
          </button>
        )}
        {extra}
      </div>
      {copyFailed && <div className="field-error">Не удалось скопировать — выделите текст и скопируйте вручную.</div>}
    </>
  );
}
