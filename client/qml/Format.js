.pragma library

// Formatting helpers shared by the v3 screens (dates arrive from the server as ISO 8601 UTC).

var MONTHS_GEN = ["января", "февраля", "марта", "апреля", "мая", "июня", "июля", "августа",
                  "сентября", "октября", "ноября", "декабря"]
var DAYS = ["воскресенье", "понедельник", "вторник", "среда", "четверг", "пятница", "суббота"]
var DAYS_ACC = ["воскресенье", "понедельник", "вторник", "среду", "четверг", "пятницу", "субботу"]
var DAYS_SHORT = ["Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"]

function parse(iso) {
    if (!iso) return null
    var d = new Date(iso)
    return isNaN(d.getTime()) ? null : d
}

function pad(n) { return (n < 10 ? "0" : "") + n }

function time(iso) {
    var d = parse(iso)
    return d ? pad(d.getHours()) + ":" + pad(d.getMinutes()) : ""
}

function sameDay(a, b) {
    return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate()
}

function startOfDay(d) { var x = new Date(d); x.setHours(0, 0, 0, 0); return x }
function addDays(d, n) { var x = new Date(d); x.setDate(x.getDate() + n); return x }

// "сегодня", "завтра", "в субботу", "14 октября"
function dayPhrase(iso, accusative) {
    var d = parse(iso)
    if (!d) return ""
    var now = new Date()
    if (sameDay(d, now)) return "сегодня"
    if (sameDay(d, addDays(now, 1))) return "завтра"
    if (d - now < 6 * 86400000 && d > now) return (accusative ? "в " + DAYS_ACC[d.getDay()] : DAYS[d.getDay()])
    return d.getDate() + " " + MONTHS_GEN[d.getMonth()]
}

// Отметка времени события в прошлом: "сегодня, 10:10", "вчера, 18:05", "3 октября, 09:00".
function stamp(iso) {
    var d = parse(iso)
    if (!d) return ""
    var now = new Date()
    var day = sameDay(d, now) ? "сегодня"
            : sameDay(d, addDays(now, -1)) ? "вчера"
            : d.getDate() + " " + MONTHS_GEN[d.getMonth()]
    return day + ", " + time(iso)
}

function dayTitle(iso) {
    var d = parse(iso)
    if (!d) return ""
    var s = DAYS[d.getDay()]
    return s.charAt(0).toUpperCase() + s.slice(1)
}

function dateLabel(d) { return d.getDate() + " " + MONTHS_GEN[d.getMonth()] }
function dayShort(d) { return DAYS_SHORT[d.getDay()] }

// Понедельник..воскресенье недели, в которую попадает d.
function weekOf(d) {
    var start = startOfDay(d)
    var shift = (start.getDay() + 6) % 7
    var monday = addDays(start, -shift)
    var out = []
    for (var i = 0; i < 7; ++i) out.push(addDays(monday, i))
    return out
}

function plural(n, one, few, many) {
    var m10 = n % 10, m100 = n % 100
    if (m10 === 1 && m100 !== 11) return one
    if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few
    return many
}

// "через 25 минут", "через 3 часа"
function until(iso) {
    var d = parse(iso)
    if (!d) return ""
    var min = Math.round((d - new Date()) / 60000)
    if (min < 0) return "уже началось"
    if (min < 60) return "через " + min + " " + plural(min, "минуту", "минуты", "минут")
    var h = Math.round(min / 60)
    if (h < 24) return "через " + h + " " + plural(h, "час", "часа", "часов")
    var days = Math.round(h / 24)
    return "через " + days + " " + plural(days, "день", "дня", "дней")
}

function firstName(full) {
    return (full || "").trim().split(/\s+/)[0] || ""
}

// Задача ближайшая по сроку: due_at, иначе remind_at.
function taskWhen(task) {
    return task.due_at || task.remind_at || ""
}

function escape(text) {
    return String(text === undefined || text === null ? "" : text)
        .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
}

// Текст уведомления для ленты: заголовок + деталь, без технических полей.
function notificationDetail(n) {
    if (n.kind === "login.new") return n.payload && n.payload.device ? "устройство " + n.payload.device : ""
    return n.body || ""
}

// Родительный падеж имени для «Аура Алекса», «Аура Марии» — простые правила для частых имён.
function genitive(name) {
    var n = (name || "").trim()
    if (!n) return ""
    var last = n.slice(-1).toLowerCase()
    var prev = n.slice(-2, -1).toLowerCase()
    if (last === "я") return n.slice(0, -1) + "и"
    if (last === "а") return n.slice(0, -1) + ("гкхжчшщ".indexOf(prev) >= 0 ? "и" : "ы")
    if (last === "й" || last === "ь") return n.slice(0, -1) + "я"
    if ("бвгджзклмнпрстфхцчшщ".indexOf(last) >= 0) return n + "а"
    return n
}

// Окно переговоров в часах от текущего момента: двое суток, до конца недели, две недели.
function windowHours(key) {
    var now = new Date()
    if (key === "weekend") {
        var end = addDays(startOfDay(now), (7 - now.getDay()) % 7 + 1)
        return Math.max(12, Math.round((end - now) / 3600000))
    }
    if (key === "tomorrow") return 48
    return 336
}

// «ближайшие 36 часов» / «ближайшие 7 дней» — окно переговоров для людей.
function windowPhrase(hours) {
    if (hours < 48) return "ближайшие " + hours + " " + plural(hours, "час", "часа", "часов")
    var days = Math.floor((hours + 12) / 24)
    return "ближайшие " + days + " " + plural(days, "день", "дня", "дней")
}

// Понятное описание инструмента; серверное — запасной вариант для новых инструментов.
// Короткое название инструмента для заголовков и подтверждений.
function toolTitle(tool) {
    return ({ send_message: "Сообщения", create_note: "Заметки", create_reminder: "Напоминания",
              send_email: "Письма", find_cafe: "Подбор мест", book_table: "Бронирование",
              check_calendar: "Календарь", suggest_time: "Подбор времени" })[tool] || tool || ""
}

function toolDescription(tool, fallback) {
    var known = {
        send_message: "Писать сообщения вашим контактам",
        create_note: "Сохранять заметки в память Ауры",
        create_reminder: "Ставить напоминания",
        send_email: "Отправлять письма от вашего имени",
        find_cafe: "Подбирать место под питание, бюджет и дорогу обоих",
        book_table: "Бронировать столик в выбранном месте",
        check_calendar: "Смотреть, свободны ли вы в нужное время",
        suggest_time: "Предлагать время встречи"
    }
    return known[tool] || fallback || ""
}

// Люди из чатов (кроме меня), без повторов: основа экрана контактов и выбора собеседника.
function contactsFrom(chats, myId) {
    var seen = {}, out = []
    for (var i = 0; i < (chats || []).length; ++i) {
        var chat = chats[i]
        var members = chat.members || []
        for (var j = 0; j < members.length; ++j) {
            var m = members[j]
            if (m.user_id === myId || seen[m.user_id]) continue
            seen[m.user_id] = true
            out.push({ id: m.user_id, name: m.display_name || m.email, email: m.email, chatId: chat.id,
                       lastMessage: chat.last_message_body || "", lastAt: chat.last_message_at || "" })
        }
    }
    return out
}

function nameFor(target, chats, myId) {
    var list = contactsFrom(chats, myId)
    for (var i = 0; i < list.length; ++i)
        if (list[i].email === target || list[i].name === target) return list[i].name
    return target || ""
}
