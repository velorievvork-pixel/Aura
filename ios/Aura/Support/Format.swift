// Format.swift — даты и подписи для экранов (как android/.../ui/Format.kt).
//
// Даты с сервера — ISO 8601 (UTC "...Z", с долями секунды или без зоны);
// показываем в локальном времени, подписи — по-русски.
import Foundation

enum Format {
    private static let ru = Locale(identifier: "ru_RU")

    private static let isoPlain: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime]
        return formatter
    }()

    private static let isoFractional: ISO8601DateFormatter = {
        let formatter = ISO8601DateFormatter()
        formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
        return formatter
    }()

    private static let localNoZone: DateFormatter = {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd'T'HH:mm:ss"
        return formatter
    }()

    private static func formatter(_ pattern: String) -> DateFormatter {
        let formatter = DateFormatter()
        formatter.locale = ru
        formatter.dateFormat = pattern
        return formatter
    }

    private static let time = formatter("HH:mm")
    private static let weekday = formatter("EEEE")
    private static let weekdayStandalone = formatter("cccc")
    private static let weekdayShort = formatter("ccc")
    private static let dayMonth = formatter("d MMMM")

    static func parseISO(_ text: String) -> Date? {
        guard !text.isEmpty else { return nil }
        return isoPlain.date(from: text) ?? isoFractional.date(from: text) ?? localNoZone.date(from: text)
    }

    /// Дата в UTC для полей протокола (due_at, remind_at).
    static func isoString(_ date: Date) -> String {
        isoPlain.string(from: date)
    }

    static func timeOf(_ iso: String) -> String {
        parseISO(iso).map { time.string(from: $0) } ?? ""
    }

    /// «сегодня», «завтра», день недели в пределах шести дней или «12 октября».
    static func dayPhrase(_ iso: String) -> String {
        guard let date = parseISO(iso) else { return "" }
        let calendar = Calendar.current
        if calendar.isDateInToday(date) { return "сегодня" }
        if calendar.isDateInTomorrow(date) { return "завтра" }
        let today = calendar.startOfDay(for: Date())
        let days = calendar.dateComponents([.day], from: today, to: calendar.startOfDay(for: date)).day ?? 0
        if days > 0 && days < 6 { return weekday.string(from: date) }
        return dayMonth.string(from: date)
    }

    /// «Понедельник» — заголовок карточки итога переговоров.
    static func dayTitle(_ iso: String) -> String {
        guard let date = parseISO(iso) else { return "" }
        return weekdayStandalone.string(from: date).capitalized(with: ru)
    }

    /// «Пн», «Вт» — полоса недели в задачах.
    static func dayShort(_ date: Date) -> String {
        let name = weekdayShort.string(from: date).replacingOccurrences(of: ".", with: "")
        return name.prefix(1).uppercased() + name.dropFirst()
    }

    static func dateLabel(_ date: Date) -> String {
        dayMonth.string(from: date)
    }

    static func plural(_ n: Int, _ one: String, _ few: String, _ many: String) -> String {
        let m10 = n % 10
        let m100 = n % 100
        if m10 == 1 && m100 != 11 { return one }
        if (2...4).contains(m10) && (m100 < 12 || m100 > 14) { return few }
        return many
    }

    static func untilText(_ iso: String) -> String {
        guard let date = parseISO(iso) else { return "" }
        let minutes = Int(date.timeIntervalSinceNow / 60)
        if minutes < 0 { return "уже началось" }
        if minutes < 60 { return "через \(minutes) \(plural(minutes, "минуту", "минуты", "минут"))" }
        if minutes < 24 * 60 {
            let hours = minutes / 60
            return "через \(hours) \(plural(hours, "час", "часа", "часов"))"
        }
        let days = minutes / 1440
        return "через \(days) \(plural(days, "день", "дня", "дней"))"
    }

    static func greeting(_ date: Date = Date()) -> String {
        switch Calendar.current.component(.hour, from: date) {
        case 0..<5: return "Доброй ночи"
        case 5..<12: return "Доброе утро"
        case 12..<18: return "Добрый день"
        default: return "Добрый вечер"
        }
    }

    /// Родительный падеж имени: «Аура Алекса», «Аура Марии», «Аура Димы».
    static func genitive(_ name: String) -> String {
        let n = name.trimmingCharacters(in: .whitespaces)
        guard let last = n.last.map({ Character($0.lowercased()) }) else { return n }
        let prev = n.count > 1 ? Character(n[n.index(n.endIndex, offsetBy: -2)].lowercased()) : " "
        let stem = String(n.dropLast())
        switch last {
        case "я": return stem + "и"
        case "а": return stem + ("гкхжчшщ".contains(prev) ? "и" : "ы")
        case "й", "ь": return stem + "я"
        default:
            return "бвгджзклмнпрстфхцчшщ".contains(last) ? n + "а" : n
        }
    }

    static func firstName(_ full: String) -> String {
        full.split(whereSeparator: { $0.isWhitespace }).first.map(String.init) ?? ""
    }

    /// Окно переговоров в часах: до конца воскресенья, двое суток или неделя.
    static func windowHours(_ key: String) -> Int {
        switch key {
        case "weekend":
            let calendar = Calendar(identifier: .iso8601)
            let now = Date()
            // Начало следующего понедельника (если сегодня понедельник — через неделю).
            let next = calendar.nextDate(after: now, matching: DateComponents(hour: 0, minute: 0, weekday: 2),
                                         matchingPolicy: .nextTime) ?? now.addingTimeInterval(7 * 86_400)
            return max(12, Int(next.timeIntervalSince(now) / 3600))
        case "tomorrow":
            return 48
        default:
            return 168
        }
    }

    /// Названия инструментов для подтверждений и разрешений.
    static func toolLabel(_ tool: String) -> String {
        switch tool {
        case "send_message": return "Сообщения"
        case "create_note": return "Заметки"
        case "create_reminder": return "Напоминания"
        case "send_email": return "Письма"
        case "find_cafe": return "Подбор мест"
        case "book_table": return "Бронирование"
        case "check_calendar": return "Календарь"
        case "suggest_time": return "Подбор времени"
        default: return tool
        }
    }
}
