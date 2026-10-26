/* Selfvoy — дизайн-превью: экраны, тема, переговоры на пружинах и живой запрос
   к AI-сервису (POST /v1/agent/run). Данные экранов — пример для превью. */

/* --------------------------------------------------------------- иконки (Lucide, ISC) */
const ICONS = {
  home: '<path d="M3 10.5 12 3l9 7.5V20a1 1 0 0 1-1 1h-5v-6h-6v6H4a1 1 0 0 1-1-1z"/>',
  message: '<path d="M7.9 20A9 9 0 1 0 4 16.1L2 22Z"/>',
  handshake: '<path d="m11 17 2 2a1 1 0 1 0 3-3"/><path d="m14 14 2.5 2.5a1 1 0 1 0 3-3l-3.88-3.88a3 3 0 0 0-4.24 0l-.88.88a1 1 0 1 1-3-3l2.81-2.81a5.79 5.79 0 0 1 7.06-.87l.47.28a2 2 0 0 0 1.42.25L21 4"/><path d="m21 3 1 11h-2"/><path d="M3 3 2 14l6.5 6.5a1 1 0 1 0 3-3"/><path d="M3 4h8"/>',
  "check-square": '<path d="M21 10.5V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h12.5"/><path d="m9 11 3 3L22 4"/>',
  users: '<path d="M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.87"/><path d="M16 3.13a4 4 0 0 1 0 7.75"/>',
  sparkle: '<path d="M12 3l1.9 5.6 5.6 1.9-5.6 1.9L12 18l-1.9-5.6-5.6-1.9 5.6-1.9z"/>',
  "log-in": '<path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4"/><path d="m10 17 5-5-5-5"/><path d="M15 12H3"/>',
  grid: '<rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/>',
  moon: '<path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z"/>',
  sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
  settings: '<path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/>',
  mic: '<rect x="9" y="2" width="6" height="12" rx="3"/><path d="M19 10v1a7 7 0 0 1-14 0v-1"/><path d="M12 18v4"/>',
  "arrow-up": '<path d="m5 12 7-7 7 7"/><path d="M12 19V5"/>',
  check: '<path d="M20 6 9 17l-5-5"/>',
  "check-circle": '<circle cx="12" cy="12" r="10"/><path d="m9 12 2 2 4-4"/>',
  mail: '<rect x="2" y="4" width="20" height="16" rx="2"/><path d="m22 7-10 6L2 7"/>',
  bell: '<path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9"/><path d="M10.3 21a1.94 1.94 0 0 0 3.4 0"/>',
  shield: '<path d="M20 13c0 5-3.5 7.5-7.7 9a1 1 0 0 1-.6 0C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.2-2.7a1.2 1.2 0 0 1 1.6 0C14.5 3.8 17 5 19 5a1 1 0 0 1 1 1z"/>',
  brain: '<path d="M4 19.5V5a2 2 0 0 1 2-2h14v18H6.5a2.5 2.5 0 0 1 0-5H20"/>',
  play: '<path d="M7 4l13 8-13 8z"/>',
  rotate: '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5"/>',
  link: '<path d="M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71"/><path d="M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"/>',
  plus: '<path d="M12 5v14M5 12h14"/>',
  search: '<circle cx="11" cy="11" r="8"/><path d="m21 21-4.3-4.3"/>',
  calendar: '<rect x="3" y="4" width="18" height="18" rx="2"/><path d="M16 2v4M8 2v4M3 10h18"/>',
  "map-pin": '<path d="M20 10c0 6-8 12-8 12S4 16 4 10a8 8 0 0 1 16 0Z"/><circle cx="12" cy="10" r="3"/>',
  card: '<rect x="2" y="5" width="20" height="14" rx="2"/><path d="M2 10h20"/>',
  clock: '<circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/>',
};
const svgIcon = (name) =>
  `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name] || ""}</svg>`;
function paintIcons(root = document) {
  root.querySelectorAll("i[data-icon]").forEach((i) => { i.outerHTML = svgIcon(i.dataset.icon); });
}

/* знак «Точка встречи»: левый круг — вы, правый — собеседник, пересечение — чернила */
let markSeq = 0;
function paintMarks(root = document) {
  root.querySelectorAll("svg[data-mark]").forEach((svg) => {
    const id = `sv-clip-${++markSeq}`;
    svg.setAttribute("viewBox", "0 0 64 40");
    svg.setAttribute("aria-hidden", "true");
    svg.innerHTML = `<defs><clipPath id="${id}"><circle cx="22" cy="20" r="16"/></clipPath></defs>
      <circle class="m-l" cx="22" cy="20" r="16"/><circle class="m-r" cx="42" cy="20" r="16"/>
      <circle class="m-x" cx="42" cy="20" r="16" clip-path="url(#${id})"/>`;
    svg.removeAttribute("data-mark");
  });
}
paintIcons();
paintMarks();

/* --------------------------------------------------------------- навигация */
const screens = document.querySelectorAll(".screen");
const navButtons = document.querySelectorAll("[data-screen]");
const screenSelect = document.getElementById("screen-select");
function show(name) {
  screens.forEach((s) => s.classList.toggle("is-active", s.id === `screen-${name}`));
  navButtons.forEach((b) => b.classList.toggle("is-active", b.dataset.screen === name));
  if (screenSelect) screenSelect.value = name;
  if (location.hash !== `#${name}`) history.replaceState(null, "", `#${name}`);
  window.scrollTo({ top: 0 });
  if (name === "negotiation" && !negotiation.played) negotiation.play();
  if (name === "chat") requestAnimationFrame(() => window.scrollTo({ top: document.body.scrollHeight }));
}
navButtons.forEach((b) => b.addEventListener("click", () => show(b.dataset.screen)));
document.querySelectorAll("[data-goto]").forEach((el) => el.addEventListener("click", () => show(el.dataset.goto)));
screenSelect?.addEventListener("change", () => show(screenSelect.value));

/* --------------------------------------------------------------- тема */
function storeGet(key) { try { return localStorage.getItem(key); } catch { return null; } }
function storeSet(key, value) { try { localStorage.setItem(key, value); } catch { /* приватный режим */ } }
function applyTheme(theme) {
  document.documentElement.dataset.theme = theme;
  document.querySelectorAll("[data-theme-toggle] svg").forEach((svg) => {
    svg.outerHTML = svgIcon(theme === "dark" ? "sun" : "moon");
  });
}
applyTheme(storeGet("selfvoy-theme") || (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light"));
document.querySelectorAll("[data-theme-toggle]").forEach((b) => b.addEventListener("click", () => {
  const next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
  applyTheme(next);
  storeSet("selfvoy-theme", next);
}));
const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

/* --------------------------------------------------------------- тост */
const toast = document.getElementById("toast");
let toastTimer = null;
function notify(message, isError = false) {
  toast.textContent = message;
  toast.classList.toggle("is-error", isError);
  toast.classList.add("is-visible");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove("is-visible"), 3200);
}
document.querySelectorAll("[data-toast]").forEach((el) => el.addEventListener("click", () => notify(el.dataset.toast)));

/* --------------------------------------------------------------- переключатели */
document.addEventListener("click", (e) => {
  const seg = e.target.closest("[data-segmented] button");
  if (seg) seg.parentElement.querySelectorAll("button").forEach((b) => b.classList.toggle("is-on", b === seg));
  const sw = e.target.closest(".switch");
  if (sw) { sw.classList.toggle("is-on"); sw.setAttribute("aria-checked", sw.classList.contains("is-on")); }
  const check = e.target.closest("[data-task]");
  if (check) check.classList.toggle("is-done");
  const fill = e.target.closest("[data-fill]");
  if (fill) { const input = document.getElementById("home-input"); input.value = fill.dataset.fill; input.focus(); }
});

/* --------------------------------------------------------------- приветствие */
(function greet() {
  const now = new Date();
  const h = now.getHours();
  const part = h < 5 ? "Доброй ночи" : h < 12 ? "Доброе утро" : h < 18 ? "Добрый день" : "Добрый вечер";
  document.getElementById("greeting").innerHTML = `${part}, <em>Алекс</em>`;
  const label = now.toLocaleDateString("ru-RU", { weekday: "long", day: "numeric", month: "long" });
  document.getElementById("today-label").textContent = label[0].toUpperCase() + label.slice(1);
})();

/* --------------------------------------------------------------- задачи (пример) */
const TASKS = {
  today: [
    { t: "Подготовить презентацию к ревью Q3", time: "11:00", dot: "warning" },
    { t: "Ответить на письмо инвестора", time: "16:30", dot: "voy", sub: "Черновик готов" },
  ],
  next: [
    { t: "Встреча с Димой — обсудить стартап", time: "чт 14:30", dot: "agreed", sub: "Договорился посланник" },
    { t: "Разослать приглашения на демо-день", time: "сб 12:00" },
    { t: "Позвонить юристу", time: "завтра 10:00", sub: "Напоминание" },
  ],
  done: [{ t: "Забронировать переговорную", time: "вт", done: true }],
};
const taskRow = (task) => `
  <div class="row ${task.done ? "is-done" : ""}" data-task role="checkbox" aria-checked="${!!task.done}" tabindex="0">
    <span class="check">${svgIcon("check")}</span>
    <div class="grow"><span>${task.t}</span>${task.sub ? `<span class="sub">${task.sub}</span>` : ""}</div>
    ${task.dot ? `<span class="dot dot--${task.dot}"></span>` : ""}
    <span class="meta mono">${task.time}</span>
  </div>`;
document.getElementById("tasks-today").innerHTML = TASKS.today.map(taskRow).join("");
document.getElementById("tasks-next").innerHTML = TASKS.next.map(taskRow).join("");
document.getElementById("tasks-done").innerHTML = TASKS.done.map(taskRow).join("");
document.getElementById("home-tasks").innerHTML = [...TASKS.today, ...TASKS.next.slice(0, 2)].map(taskRow).join("");

(function week() {
  const today = new Date();
  const monday = new Date(today);
  monday.setDate(today.getDate() - ((today.getDay() + 6) % 7));
  const pips = [["", ""], ["v"], ["", "v", ""], ["a", ""], [""], ["", ""], []];
  const names = ["Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"];
  document.getElementById("week").innerHTML = names.map((n, i) => {
    const d = new Date(monday); d.setDate(monday.getDate() + i);
    const isToday = d.toDateString() === today.toDateString();
    return `<button class="day ${isToday ? "is-today is-on" : ""}"><span class="dn">${n}</span><span class="dd">${d.getDate()}</span><span class="pips">${pips[i].map((p) => `<i class="${p}"></i>`).join("")}</span></button>`;
  }).join("");
  document.getElementById("week").addEventListener("click", (e) => {
    const day = e.target.closest(".day");
    if (day) document.querySelectorAll(".day").forEach((d) => d.classList.toggle("is-on", d === day));
  });
})();

/* --------------------------------------------------------------- контакты (пример) */
const PEOPLE = [
  { n: "Дима Волков", i: "ДВ", c: "#B83A24", last: "Давай! Пусть наши посланники договорятся.", when: "09:05", can: ["Видит «занят / свободен»", "Без глютена — учтёт при выборе места", "Город: центр"] },
  { n: "Мария Чен", i: "МЧ", c: "#7A5C2E", last: "Посланник предложил пт 10:00", when: "вчера", can: ["Видит названия событий", "Вегетарианка", "Предпочитает утро"] },
  { n: "Олег Рябов", i: "ОР", c: "#2E6A5A", last: "Ещё нет Selfvoy — можно пригласить по ссылке", when: "", can: ["Нет посланника", "Пригласите ссылкой: он выберет время в браузере"] },
];
const contactsList = document.getElementById("contacts-list");
contactsList.innerHTML = PEOPLE.map((p, k) => `
  <button class="row" data-person="${k}">
    <span class="avatar" style="--c:${p.c}">${p.i}</span>
    <div class="grow"><span style="font-weight:600">${p.n}</span><span class="sub">${p.last}</span></div>
    <span class="meta mono">${p.when}</span>
  </button>`).join("");
function showPerson(k) {
  const p = PEOPLE[k];
  document.getElementById("person").innerHTML = `
    <div class="top"><span class="avatar avatar--lg" style="--c:${p.c}">${p.i}</span><div><p class="heading">${p.n}</p><p class="subtle" style="margin:0;font-size:13px">${k < 2 ? "Посланник на связи" : "Без посланника"}</p></div></div>
    <p class="label" style="margin-top:20px">Что знает ваш посланник</p>
    <div class="can">${p.can.map((c) => `<div>${svgIcon("check")}<span>${c}</span></div>`).join("")}</div>
    <button class="btn btn--primary btn--block" data-goto-neg>${svgIcon("handshake")}Договориться о встрече</button>
    <button class="btn btn--quiet btn--block" style="margin-top:6px">Написать</button>`;
  document.querySelector("[data-goto-neg]").addEventListener("click", () => show("negotiation"));
}
contactsList.addEventListener("click", (e) => { const row = e.target.closest("[data-person]"); if (row) showPerson(+row.dataset.person); });
showPerson(0);

/* --------------------------------------------------------------- образцы цветов */
const swatch = ([name, v]) => `<div class="swatch"><i style="background:var(${v})"></i><span>${name}<br><code>${v}</code></span></div>`;
document.getElementById("swatches-brand").innerHTML = [["вы", "--voy"], ["собеседник", "--peer"], ["договорились", "--agreed"], ["ждёт решения", "--warning"]].map(swatch).join("");
document.getElementById("swatches-base").innerHTML = [["бумага", "--paper"], ["карточка", "--card"], ["чернила", "--ink"], ["линия", "--line"]].map(swatch).join("");

/* --------------------------------------------------------------- пружины (MOTION.md) */
// Закрытая форма отклика пружины 0 → 1 (ζ < 1 — крошечный перелёт, ζ = 1 — без него).
function spring(tau, w = 14, z = 0.86) {
  if (tau <= 0) return 0;
  if (z >= 1) { const x = w * tau; return 1 - Math.exp(-x) * (1 + x); }
  const wd = w * Math.sqrt(1 - z * z);
  return 1 - Math.exp(-z * w * tau) * (Math.cos(wd * tau) + (z * w / wd) * Math.sin(wd * tau));
}
class Track {
  constructor(v0) { this.v0 = v0; this.cur = v0; this.ev = []; }
  to(t, v, w, z) { if (v !== this.cur) { this.ev.push([t, v - this.cur, w, z]); this.cur = v; } return this; }
  at(t) { let v = this.v0; for (const [ti, d, w, z] of this.ev) v += d * spring(t - ti, w, z); return v; }
}
const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;

/* --------------------------------------------------------------- переговоры */
const ROUNDS = [
  { side: "voy", who: "Ваш посланник", text: "Предлагаю вт 18:00 или ср 12:00 — 1 час, центр." },
  { side: "peer", who: "Посланник Димы", text: "Вт занят. Ср — встреча до 13:00. Есть чт после 14:00." },
  { side: "voy", who: "Ваш посланник", text: "Чт 14:30 свободно. Место — с безглютеновым меню?" },
  { side: "peer", who: "Посланник Димы", text: "Чт 14:30 подходит. Кофейня «Зерно» на Мира — да." },
  { side: "agreed", who: "Договорились", text: "Чт 14:30 · «Зерно», Мира 12 · 60 мин." },
];
const negotiation = {
  played: false, raf: 0, start: 0, live: null,
  play() {
    this.played = true;
    cancelAnimationFrame(this.raf);
    const thread = document.getElementById("neg-thread");
    thread.innerHTML = "";
    const track = document.getElementById("track");
    track.classList.remove("is-agreed");
    const deal = document.getElementById("deal");
    deal.classList.add("is-waiting");
    document.getElementById("deal-label").textContent = "Ищем общее время…";
    document.getElementById("deal-when").textContent = "—";
    document.getElementById("deal-place").textContent = "Посланники сверяют календари";
    document.getElementById("deal-confirm").disabled = true;
    const badge = document.getElementById("neg-badge");
    badge.className = "badge badge--voy";

    // цели точек по раундам: сходятся к центру, на договорённости — пересекаются
    const gap = reduceMotion ? 0 : 0.8;
    const T0 = 0.3;
    const voy = new Track(0), peer = new Track(100), fill = new Track(0);
    const stops = [[14, 86], [26, 74], [36, 64], [44, 56], [49, 51]];
    stops.forEach(([a, b], k) => { voy.to(T0 + k * gap, a, 12, 0.82); peer.to(T0 + k * gap + 0.08, b, 9, 0.82); });
    const tEnd = T0 + (ROUNDS.length - 1) * gap;
    fill.to(tEnd + 0.15, 1, 10, 1);
    let shown = 0;
    const dv = document.getElementById("dot-voy"), dp = document.getElementById("dot-peer"), df = document.getElementById("track-fill");
    const round = document.getElementById("neg-round");
    this.start = performance.now();
    const frame = (now) => {
      const t = (now - this.start) / 1000;
      dv.style.left = `${voy.at(t)}%`;
      dp.style.left = `${peer.at(t)}%`;
      const f = fill.at(t);
      df.style.left = `${50 - 40 * f}%`;
      df.style.width = `${80 * f}%`;
      while (shown < ROUNDS.length && t >= T0 + shown * gap) {
        const r = ROUNDS[shown];
        const el = document.createElement("div");
        el.className = `offer offer--${r.side}`;
        el.innerHTML = `<span class="dot dot--${r.side}" style="margin-top:6px"></span><div><div class="who">${r.who}</div><p>${r.text}</p></div><span class="r">${r.side === "agreed" ? "✓" : `${Math.floor(shown / 2) + 1}/5`}</span>`;
        thread.appendChild(el);
        shown += 1;
        round.textContent = r.side === "agreed" ? "Договорились · 3 раунда" : `Раунд ${Math.floor((shown - 1) / 2) + 1}/5`;
        document.querySelectorAll(".track-tick").forEach((tick, k) => tick.classList.toggle("is-past", k < Math.ceil(shown / 2)));
        if (r.side === "agreed") this.finish();
      }
      if (t < tEnd + 1.2) this.raf = requestAnimationFrame(frame);
    };
    this.raf = requestAnimationFrame(frame);
  },
  finish() {
    document.getElementById("track").classList.add("is-agreed");
    document.getElementById("neg-badge").className = "badge badge--agreed";
    const deal = document.getElementById("deal");
    deal.classList.remove("is-waiting");
    deal.classList.add("card--agreed");
    const live = this.live;
    document.getElementById("deal-label").textContent = live ? "Договорились · ответ AI-сервиса" : "Договорились";
    document.getElementById("deal-when").innerHTML = live?.when || "<em>Четверг</em>, 14:30";
    document.getElementById("deal-place").textContent = live?.place || "Кофейня «Зерно», ул. Мира, 12";
    document.getElementById("deal-confirm").disabled = false;
  },
};
document.getElementById("neg-replay").addEventListener("click", () => { negotiation.live = null; negotiation.play(); });
document.getElementById("deal-confirm").addEventListener("click", () => notify("Забронировано. Приглашение ушло Диме."));

/* --------------------------------------------------------------- живой запрос к AI-сервису */
const context = {
  user_id: 1, email: "alex@selfvoy.app", display_name: "Алекс",
  preferences: { diet: ["vegan"], city: "Москва", budget_limit: 3, preferred_hours: [10, 11, 12, 14, 15, 16] },
  memory: [{ kind: "preference", text: "Люблю тихие кофейни", weight: 2 }, { kind: "fact", text: "Не ем мясо", weight: 1.5 }],
};
const peer = { user_id: 2, email: "dima@selfvoy.app", display_name: "Дима", preferences: { diet: ["gluten_free"], city: "Москва" } };
const agentButton = document.getElementById("agent-btn");
const agentStatus = document.getElementById("agent-status");

async function askEnvoy(message) {
  if (!message) { notify("Опишите, о чём договориться", true); return null; }
  const response = await fetch("/v1/agent/run", {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ context: { ...context, message }, peers: [peer], execute: true }),
  });
  if (!response.ok) throw new Error(`AI-сервис ответил ${response.status}`);
  return response.json();
}

agentButton.addEventListener("click", async () => {
  const message = document.getElementById("agent-input").value.trim();
  agentButton.disabled = true;
  agentButton.innerHTML = `<svg class="mark mark--busy" data-mark></svg>Договариваемся…`;
  paintMarks(agentButton);
  agentStatus.textContent = "Посланник думает…";
  try {
    const data = await askEnvoy(message);
    if (!data) return;
    const slot = data.a2a?.slots?.[0];
    const place = data.a2a?.place?.name;
    negotiation.live = slot ? {
      when: new Date(slot.start).toLocaleString("ru-RU", { weekday: "long", hour: "2-digit", minute: "2-digit" }).replace(/^./, (c) => `<em>${c.toUpperCase()}`).replace(",", "</em>,"),
      place: place || "Место не выбрано",
    } : null;
    agentStatus.textContent = `Намерение: ${data.intent} · действий: ${(data.actions || []).length}`;
    negotiation.play();
    window.scrollTo({ top: 0, behavior: reduceMotion ? "auto" : "smooth" });
  } catch (error) {
    agentStatus.textContent = `AI-сервис недоступен (${error.message}) — показан пример переговоров`;
    negotiation.live = null;
    negotiation.play();
    window.scrollTo({ top: 0, behavior: reduceMotion ? "auto" : "smooth" });
  } finally {
    agentButton.disabled = false;
    agentButton.innerHTML = `${svgIcon("handshake")}Начать переговоры`;
  }
});

/* --------------------------------------------------------------- чат */
const chatLog = document.getElementById("chat-log");
const nowTime = () => new Date().toLocaleTimeString("ru-RU", { hour: "2-digit", minute: "2-digit" });
function bubble(text, kind) {
  const el = document.createElement("div");
  el.className = `msg msg--${kind}`;
  el.textContent = text;
  const time = document.createElement("span");
  time.className = "time";
  time.textContent = nowTime();
  el.appendChild(time);
  chatLog.appendChild(el);
  el.scrollIntoView({ block: "end", behavior: reduceMotion ? "auto" : "smooth" });
  return el;
}
async function sendChat(text) {
  if (!text) return;
  bubble(text, "mine");
  const thinking = document.createElement("div");
  thinking.className = "thinking";
  thinking.innerHTML = `<svg class="mark mark--busy" data-mark></svg>Посланник думает…`;
  paintMarks(thinking);
  chatLog.appendChild(thinking);
  try {
    const data = await askEnvoy(text);
    thinking.remove();
    bubble(data?.reply || "Готово.", "envoy");
  } catch {
    thinking.remove();
    bubble("AI-сервис сейчас недоступен — это превью дизайна. Запустите make ai-run, и я отвечу по-настоящему.", "envoy");
  }
}
const composer = document.getElementById("composer");
document.getElementById("send-btn").addEventListener("click", () => { sendChat(composer.value.trim()); composer.value = ""; });
composer.addEventListener("keydown", (e) => { if (e.key === "Enter") { sendChat(composer.value.trim()); composer.value = ""; } });
document.querySelectorAll("[data-reply]").forEach((chip) => chip.addEventListener("click", () => sendChat(chip.dataset.reply)));
const homeInput = document.getElementById("home-input");
const homeSend = () => { const text = homeInput.value.trim(); if (!text) { homeInput.focus(); return; } homeInput.value = ""; show("chat"); sendChat(text); };
document.getElementById("home-ask").addEventListener("click", homeSend);
homeInput.addEventListener("keydown", (e) => { if (e.key === "Enter") homeSend(); });

/* --------------------------------------------------------------- вход: проверка как на сервере */
document.getElementById("login-form").addEventListener("submit", (e) => {
  e.preventDefault();
  const pass = document.getElementById("login-pass");
  const error = document.getElementById("login-error");
  const ok = pass.value.length >= 8 && /\p{L}/u.test(pass.value) && /\d/.test(pass.value);
  pass.classList.toggle("is-error", !ok);
  error.hidden = ok;
  error.textContent = "Минимум 8 символов, буквы и цифры";
  if (ok) { notify("Это превью дизайна: настоящий вход — в приложении"); show("home"); }
});

/* --------------------------------------------------------------- лендинг: точки встречаются */
(function meetCanvas() {
  const canvas = document.getElementById("meet-canvas");
  const ctx = canvas.getContext("2d");
  const LOOP = 6.4;
  function size() {
    const dpr = Math.min(devicePixelRatio || 1, 2);
    canvas.width = canvas.clientWidth * dpr;
    canvas.height = canvas.clientHeight * dpr;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }
  size();
  addEventListener("resize", size);
  // Чистая функция времени: каждый кадр считается из t (как в launch-ролике).
  function draw(t) {
    const W = canvas.clientWidth, H = canvas.clientHeight, cy = H * 0.46;
    const r = Math.min(46, H * 0.17);
    const u = t % LOOP;
    const hop = (k) => spring(u - 0.4 - k * 0.55, 13, 0.85);
    let p = 0; for (let k = 0; k < 5; k++) p += hop(k) / 5;          // 0 → 1 за 5 раундов
    const out = spring(u - 5.4, 9, 1);                                // расход перед петлёй
    const sep = (W * 0.36) * (1 - p) + r * 0.8 * p + out * W * 0.36;
    const cx = W / 2;
    const voy = cssVar("--voy"), peerC = cssVar("--peer"), ink = cssVar("--ink"), line = cssVar("--line"), muted = cssVar("--ink-3");
    ctx.clearRect(0, 0, W, H);
    // дорожка
    ctx.strokeStyle = line; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(cx - W * 0.4, cy); ctx.lineTo(cx + W * 0.4, cy); ctx.stroke();
    for (let k = 0; k < 5; k++) {
      const x = cx - W * 0.4 + (W * 0.8) * (k + 0.5) / 5;
      ctx.fillStyle = k < Math.round(p * 5) ? muted : line;
      ctx.beginPath(); ctx.arc(x, cy, 4, 0, Math.PI * 2); ctx.fill();
    }
    const lx = cx - sep / 2, rx = cx + sep / 2;
    ctx.fillStyle = voy; ctx.beginPath(); ctx.arc(lx, cy, r, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = peerC; ctx.beginPath(); ctx.arc(rx, cy, r, 0, Math.PI * 2); ctx.fill();
    ctx.save(); ctx.beginPath(); ctx.arc(lx, cy, r, 0, Math.PI * 2); ctx.clip();
    ctx.fillStyle = ink; ctx.beginPath(); ctx.arc(rx, cy, r, 0, Math.PI * 2); ctx.fill(); ctx.restore();
    // подписи
    const a = Math.max(0, Math.min(1, spring(u - 3.4, 16, 1) - out * 1.5));
    ctx.globalAlpha = a; ctx.fillStyle = ink; ctx.textAlign = "center";
    ctx.font = `italic ${Math.round(r * 0.7)}px "Literata", Georgia, serif`;
    ctx.fillText("четверг, 14:30", cx, cy + r + r * 0.95);
    ctx.globalAlpha = 1 - a * 0.6; ctx.fillStyle = muted; ctx.font = `500 12px "JetBrains Mono", monospace`;
    ctx.textAlign = "left"; ctx.fillText("ВЫ", cx - W * 0.4, cy - r - 14);
    ctx.textAlign = "right"; ctx.fillText("ДИМА", cx + W * 0.4, cy - r - 14);
    ctx.globalAlpha = 1;
  }
  const t0 = performance.now();
  const loop = (now) => {
    if (document.getElementById("screen-landing").classList.contains("is-active")) {
      if (canvas.width !== Math.round(canvas.clientWidth * Math.min(devicePixelRatio || 1, 2))) size();
      draw(reduceMotion ? 4.2 : (now - t0) / 1000);
    }
    requestAnimationFrame(loop);
  };
  requestAnimationFrame(loop);
})();

/* --------------------------------------------------------------- стартовый экран */
const fromHash = () => { const name = location.hash.slice(1); return document.getElementById(`screen-${name}`) ? name : null; };
addEventListener("hashchange", () => { const name = fromHash(); if (name) show(name); });
show(fromHash() || "home");
