// Booking.swift — страницы записи (booking.*) и рефералы (referral.*):
// модели ответа сервера (docs/PROTOCOL.md → «Страницы записи», «Рефералы»).
// Зеркало Booking.kt (Android).
import Foundation

/// Поля страницы для booking.pageCreate / pageUpdate; пределы — как на сервере.
public struct BookingPageFields: Equatable, Sendable {
    public static let titleMax = 120
    public static let descriptionMax = 1000

    public var title: String
    public var description: String
    public var durationMinutes: Int
    public var daysAhead: Int
    public var maxPerDay: Int
    public var noticeHours: Int
    public var maxSeries: Int
    public var weekends: Bool

    public init(title: String = "", description: String = "", durationMinutes: Int = 30, daysAhead: Int = 14,
                maxPerDay: Int = 4, noticeHours: Int = 2, maxSeries: Int = 1, weekends: Bool = false) {
        self.title = title
        self.description = description
        self.durationMinutes = durationMinutes
        self.daysAhead = daysAhead
        self.maxPerDay = maxPerDay
        self.noticeHours = noticeHours
        self.maxSeries = maxSeries
        self.weekends = weekends
    }

    /// Ошибка для пользователя или nil — те же пределы, что проверяет сервер.
    public func validate() -> String? {
        let name = title.trimmingCharacters(in: .whitespacesAndNewlines)
        if name.isEmpty { return "Назовите страницу — например, «Консультация»" }
        if name.unicodeScalars.count > Self.titleMax || name.contains("\n") {
            return "Название — до \(Self.titleMax) символов в одну строку"
        }
        if description.trimmingCharacters(in: .whitespacesAndNewlines).unicodeScalars.count > Self.descriptionMax {
            return "Описание — до \(Self.descriptionMax) символов"
        }
        if !(15...240).contains(durationMinutes) { return "Длительность — от 15 до 240 минут" }
        if !(1...60).contains(daysAhead) { return "Запись вперёд — от 1 до 60 дней" }
        if !(1...16).contains(maxPerDay) { return "Встреч в день — от 1 до 16" }
        if !(0...168).contains(noticeHours) { return "Запас до встречи — от 0 до 168 часов" }
        if !(1...12).contains(maxSeries) { return "Серия — от 1 до 12 встреч" }
        return nil
    }

    /// Payload booking.pageCreate / pageUpdate (без page_id).
    public var payload: [String: JSONValue] {
        [
            "title": .string(title.trimmingCharacters(in: .whitespacesAndNewlines)),
            "description": .string(description.trimmingCharacters(in: .whitespacesAndNewlines)),
            "duration_minutes": .from(durationMinutes),
            "days_ahead": .from(daysAhead),
            "max_per_day": .from(maxPerDay),
            "notice_hours": .from(noticeHours),
            "max_series": .from(maxSeries),
            "weekends": .from(weekends),
        ]
    }
}

public struct BookingPage: Identifiable, Equatable, Sendable {
    public var id: Int64
    public var slug: String
    /// Готовая ссылка или "" — если на сервере не задан AURA_PUBLIC_URL.
    public var url: String
    public var fields: BookingPageFields
    /// active | paused
    public var status: String
    public var upcoming: Int

    /// Ссылка для гостя: от сервера или путь веб-клиента.
    public var link: String { url.isEmpty ? "#/b/\(slug)" : url }

    public init(json: JSONValue) {
        id = json.int("id")
        slug = json.string("slug")
        url = json.string("url")
        fields = BookingPageFields(
            title: json.string("title"), description: json.string("description"),
            durationMinutes: Int(json.int("duration_minutes")), daysAhead: Int(json.int("days_ahead")),
            maxPerDay: Int(json.int("max_per_day")), noticeHours: Int(json.int("notice_hours")),
            maxSeries: Int(json.int("max_series")), weekends: json.bool("weekends"))
        status = json.string("status")
        upcoming = Int(json.int("upcoming"))
    }
}

public struct BookingOccurrence: Equatable, Sendable {
    public var start: String
    public var end: String
    public var status: String
    /// Местное время владельца «YYYY-MM-DDTHH:MM» (у старых записей — пусто).
    public var local: String
    public var active: Bool { status == "active" }
}

/// Запись гостя глазами владельца.
public struct Booking: Identifiable, Equatable, Sendable {
    public var id: Int64
    public var pageId: Int64
    public var pageTitle: String
    public var guestName: String
    public var guestEmail: String
    public var guestComment: String
    /// active | cancelled
    public var status: String
    /// "" | guest | owner
    public var cancelledBy: String
    public var reschedules: Int
    public var occurrences: [BookingOccurrence]

    public init(json: JSONValue) {
        id = json.int("id")
        pageId = json.int("page_id")
        pageTitle = json.string("page_title")
        guestName = json.string("guest_name")
        guestEmail = json.string("guest_email")
        guestComment = json.string("guest_comment")
        status = json.string("status")
        cancelledBy = json.string("cancelled_by")
        reschedules = Int(json.int("reschedules"))
        occurrences = json.array("occurrences").map {
            BookingOccurrence(start: $0.string("start"), end: $0.string("end"),
                              status: $0.string("status"), local: $0.string("local"))
        }.sorted { $0.start < $1.start }
    }
}

public struct Referral: Hashable, Sendable {
    public var displayName: String
    public var source: String
    public var joinedAt: String
    public var verified: Bool
}

public struct ReferralInfo: Equatable, Sendable {
    public var code: String
    /// Готовая ссылка или "" — если на сервере не задан AURA_PUBLIC_URL.
    public var url: String
    public var joined: Int
    public var verified: Int
    public var referrals: [Referral]

    /// Есть ли полная ссылка; без AURA_PUBLIC_URL делимся только кодом.
    public var hasLink: Bool { !url.isEmpty }

    /// Что показать и скопировать: полная ссылка или (без AURA_PUBLIC_URL) сам код —
    /// голый путь «#/signup?ref=…» без адреса веб-клиента никуда не ведёт.
    public var link: String { url.isEmpty ? code : url }

    /// Текст для «Поделиться»: ссылка или код, который друг введёт при регистрации.
    public var shareText: String { hasLink ? "Заведи свою Ауру: \(url)" : "Код приглашения в Aura: \(code)" }

    public init(json: JSONValue) {
        code = json.string("code")
        url = json.string("url")
        joined = Int(json.int("joined"))
        verified = Int(json.int("verified"))
        referrals = json.array("referrals").map {
            Referral(displayName: $0.string("display_name"), source: $0.string("source"),
                     joinedAt: $0.string("joined_at"), verified: $0.bool("verified"))
        }
    }
}

/// Реферальный код: 8 символов A–Z/0–9 (сервер сравнивает в верхнем регистре).
public enum ReferralCode {
    public static let length = 8
    private static let allowed = CharacterSet(charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")

    /// Нормализованный код или nil — неверный код не отправляем (сервер его всё равно молча проигнорирует).
    public static func normalize(_ raw: String?) -> String? {
        guard let raw else { return nil }
        let code = raw.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        guard code.unicodeScalars.count == length,
              code.unicodeScalars.allSatisfy({ allowed.contains($0) })
        else { return nil }
        return code
    }
}
