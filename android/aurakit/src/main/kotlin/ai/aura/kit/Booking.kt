package ai.aura.kit

import kotlinx.serialization.json.JsonObject

/*
 * Страницы записи (booking.*) и рефералы (referral.*) — модели ответа сервера
 * (docs/PROTOCOL.md → «Страницы записи», «Рефералы»). Зеркало Booking.swift.
 */

/** Поля страницы для booking.pageCreate / pageUpdate; пределы — как на сервере. */
data class BookingPageFields(
    val title: String,
    val description: String = "",
    val durationMinutes: Int = 30,
    val daysAhead: Int = 14,
    val maxPerDay: Int = 4,
    val noticeHours: Int = 2,
    val maxSeries: Int = 1,
    val weekends: Boolean = false,
) {
    /** Ошибка для пользователя или null — те же пределы, что проверяет сервер. */
    fun validate(): String? {
        val name = title.trim()
        return when {
            name.isEmpty() -> "Назовите страницу — например, «Консультация»"
            name.codePointCount(0, name.length) > TITLE_MAX || name.contains('\n') ->
                "Название — до $TITLE_MAX символов в одну строку"
            description.trim().let { it.codePointCount(0, it.length) } > DESCRIPTION_MAX ->
                "Описание — до $DESCRIPTION_MAX символов"
            durationMinutes !in 15..240 -> "Длительность — от 15 до 240 минут"
            daysAhead !in 1..60 -> "Запись вперёд — от 1 до 60 дней"
            maxPerDay !in 1..16 -> "Встреч в день — от 1 до 16"
            noticeHours !in 0..168 -> "Запас до встречи — от 0 до 168 часов"
            maxSeries !in 1..12 -> "Серия — от 1 до 12 встреч"
            else -> null
        }
    }

    companion object {
        const val TITLE_MAX = 120
        const val DESCRIPTION_MAX = 1000
    }
}

data class BookingPage(
    val id: Long,
    val slug: String,
    /** Готовая ссылка или "" — если на сервере не задан AURA_PUBLIC_URL. */
    val url: String,
    val title: String,
    val description: String,
    val durationMinutes: Int,
    val daysAhead: Int,
    val maxPerDay: Int,
    val noticeHours: Int,
    val maxSeries: Int,
    val weekends: Boolean,
    /** active | paused */
    val status: String,
    val upcoming: Int,
) {
    val fields get() = BookingPageFields(title, description, durationMinutes, daysAhead, maxPerDay, noticeHours, maxSeries, weekends)

    /** Ссылка для гостя: от сервера или путь веб-клиента (собрать полный адрес нельзя). */
    val link: String get() = url.ifEmpty { "#/b/$slug" }

    companion object {
        fun from(json: JsonObject?): BookingPage = BookingPage(
            id = json.long("id"),
            slug = json.str("slug"),
            url = json.str("url"),
            title = json.str("title"),
            description = json.str("description"),
            durationMinutes = json.long("duration_minutes").toInt(),
            daysAhead = json.long("days_ahead").toInt(),
            maxPerDay = json.long("max_per_day").toInt(),
            noticeHours = json.long("notice_hours").toInt(),
            maxSeries = json.long("max_series").toInt(),
            weekends = json.bool("weekends"),
            status = json.str("status"),
            upcoming = json.long("upcoming").toInt(),
        )
    }
}

data class BookingOccurrence(val start: String, val end: String, val status: String, val local: String = "") {
    val active get() = status == "active"
}

/** Запись гостя глазами владельца. */
data class Booking(
    val id: Long,
    val pageId: Long,
    val pageTitle: String,
    val guestName: String,
    val guestEmail: String,
    val guestComment: String,
    /** active | cancelled */
    val status: String,
    /** "" | guest | owner */
    val cancelledBy: String,
    val reschedules: Int,
    val occurrences: List<BookingOccurrence>,
) {
    companion object {
        fun from(json: JsonObject?): Booking = Booking(
            id = json.long("id"),
            pageId = json.long("page_id"),
            pageTitle = json.str("page_title"),
            guestName = json.str("guest_name"),
            guestEmail = json.str("guest_email"),
            guestComment = json.str("guest_comment"),
            status = json.str("status"),
            cancelledBy = json.str("cancelled_by"),
            reschedules = json.long("reschedules").toInt(),
            occurrences = json.arr("occurrences").mapNotNull { element ->
                val item = element as? JsonObject ?: return@mapNotNull null
                BookingOccurrence(item.str("start"), item.str("end"), item.str("status"), item.str("local"))
            }.sortedBy { it.start },
        )
    }
}

data class Referral(val displayName: String, val source: String, val joinedAt: String, val verified: Boolean)

data class ReferralInfo(
    val code: String,
    /** Готовая ссылка или "" — если на сервере не задан AURA_PUBLIC_URL. */
    val url: String,
    val joined: Int,
    val verified: Int,
    val referrals: List<Referral>,
) {
    val link: String get() = url.ifEmpty { "#/signup?ref=$code" }

    companion object {
        fun from(json: JsonObject?): ReferralInfo = ReferralInfo(
            code = json.str("code"),
            url = json.str("url"),
            joined = json.long("joined").toInt(),
            verified = json.long("verified").toInt(),
            referrals = json.arr("referrals").mapNotNull { element ->
                val item = element as? JsonObject ?: return@mapNotNull null
                Referral(item.str("display_name"), item.str("source"), item.str("joined_at"), item.bool("verified"))
            },
        )
    }
}
