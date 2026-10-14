import { useEffect, useRef } from 'react';
import { useLocation, useNavigate } from 'react-router-dom';
import { onBackButton } from './native';

const ROOTS = new Set(['/', '/login']);
const PUBLIC = /^\/(i|b|m)\//;

/** react-router хранит номер записи истории в history.state.idx: 0 — страница открыта первой. */
function hasInAppHistory(): boolean {
  try {
    const idx = (window.history.state as { idx?: unknown } | null)?.idx;
    return typeof idx === 'number' && idx > 0;
  } catch {
    return false;
  }
}

/**
 * Router-aware native glue: screens open scrolled to the top, and the Android back button walks
 * history instead of closing the app.
 */
export function NativeBridge() {
  const navigate = useNavigate();
  const location = useLocation();
  const path = useRef(location.pathname);
  path.current = location.pathname;

  // Each screen opens at the top, as in a native app; phones scroll the document itself.
  useEffect(() => {
    window.scrollTo(0, 0);
  }, [location.pathname]);

  useEffect(
    () =>
      onBackButton(
        () => navigate(-1),
        // Публичные страницы (приглашение, запись, управление записью) открываются по
        // ссылке сами по себе — «Назад» с них сворачивает приложение, если перед
        // ними в истории ничего нет (запись → «Управлять записью» — шаг назад).
        () => ROOTS.has(path.current) || (PUBLIC.test(path.current) && !hasInAppHistory()),
      ),
    [navigate],
  );
  return null;
}
