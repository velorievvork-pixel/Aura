import { useState } from 'react';
import { useAura } from '../state/store';

const SWITCHES = [
  { id: 'autoBook', title: 'Бронировать без подтверждения', hint: 'Если сумма в пределах бюджета и встреча с человеком из избранного' },
  { id: 'shareSlots', title: 'Показывать другим Аурам свободные окна', hint: 'Только «свободен / занят» на предложенное время, без названий событий' },
  { id: 'diet', title: 'Учитывать питание при выборе места', hint: 'Ваши ограничения и ограничения собеседника' },
  { id: 'night', title: 'Не беспокоить с 22:00 до 9:00', hint: 'Переговоры продолжатся утром' },
];

export function Settings() {
  const { state } = useAura();
  const [on, setOn] = useState<Record<string, boolean>>({ autoBook: false, shareSlots: true, diet: true, night: true });
  const [budget, setBudget] = useState(1000);
  const u = state.user!;

  return (
    <div className="page">
      <div className="page-head">
        <h1 className="page-title">Настройки</h1>
      </div>
      <div className="grid-2" style={{ alignItems: 'start' }}>
        <section className="tile stack" style={{ padding: 24 }}>
          <h2 style={{ fontSize: 18 }}>Профиль</h2>
          <div className="small">
            <div className="faint tiny">Имя</div>
            {u.name}
          </div>
          <div className="small">
            <div className="faint tiny">Email</div>
            {u.email}
          </div>
          <div className="small">
            <div className="faint tiny">Часовой пояс</div>
            {u.timezone}
          </div>
          <label className="small">
            <div className="row-between">
              <span>Бюджет встречи по умолчанию</span>
              <b>до {budget.toLocaleString('ru-RU')} ₽</b>
            </div>
            <input className="range" type="range" min={500} max={5000} step={250} value={budget} onChange={(e) => setBudget(Number(e.target.value))} />
          </label>
        </section>
        <section className="tile stack" style={{ padding: 24 }} id="privacy">
          <h2 style={{ fontSize: 18 }}>Что может Аура</h2>
          {SWITCHES.map((s) => (
            <div key={s.id} className="row-between">
              <div>
                <div style={{ fontWeight: 600 }}>{s.title}</div>
                <div className="faint small">{s.hint}</div>
              </div>
              <button className={`toggle ${on[s.id] ? 'on' : ''}`} role="switch" aria-checked={on[s.id]} aria-label={s.title} onClick={() => setOn({ ...on, [s.id]: !on[s.id] })} />
            </div>
          ))}
        </section>
      </div>
    </div>
  );
}
