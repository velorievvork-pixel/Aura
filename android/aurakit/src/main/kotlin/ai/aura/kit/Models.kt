package ai.aura.kit

import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

// Типизированные модели поверх payload'ов сервера (docs/PROTOCOL.md).
// Разбор терпим к отсутствующим полям: старый сервер без bio/privacy не роняет клиент.

/**
 * Пользователь из `auth.login`/`auth.me`/`profile.update` (`user{...}`).
 * `bio` — «о себе», до 300 символов; на старом сервере поля нет — пустая строка.
 */
data class AuraUser(
    val id: Long,
    val email: String,
    val displayName: String,
    val bio: String = "",
    val avatarUrl: String = "",
) {
    companion object {
        /** Ограничения profile.update — те же, что проверяет сервер. */
        const val DISPLAY_NAME_MAX = 80
        const val BIO_MAX = 300

        fun from(user: JsonObject?): AuraUser = AuraUser(
            id = user.long("id"),
            email = user.str("email"),
            displayName = user.str("display_name"),
            bio = user.str("bio"),
            avatarUrl = user.str("avatar_url"),
        )
    }
}

/**
 * Приватность в переговорах (`prefs.privacy`): что Аура раскрывает чужой Ауре.
 * Все флаги по умолчанию `true`; `prefs.get` всегда отдаёт все три.
 */
data class Privacy(
    /** Респонденту для AI — только занятость и нужные настройки, без фактов памяти, дня рождения, аллергий. */
    val shareFreeSlotsOnly: Boolean = true,
    /** Названия событий календаря заменяются на «занято». */
    val hideEventTitles: Boolean = true,
    /** `false` у любой из сторон — место не предлагается. */
    val allowPlaceChoice: Boolean = true,
) {
    /** Полный объект `privacy` для `prefs.set`. */
    fun toJson(): JsonObject = buildJsonObject {
        put(SHARE_FREE_SLOTS_ONLY, shareFreeSlotsOnly)
        put(HIDE_EVENT_TITLES, hideEventTitles)
        put(ALLOW_PLACE_CHOICE, allowPlaceChoice)
    }

    companion object {
        const val SHARE_FREE_SLOTS_ONLY = "share_free_slots_only"
        const val HIDE_EVENT_TITLES = "hide_event_titles"
        const val ALLOW_PLACE_CHOICE = "allow_place_choice"

        /** Из ответа `prefs.get`/`prefs.set` (или `auth.me.preferences`); нет ключа — `true`. */
        fun fromPrefs(prefs: JsonObject?): Privacy {
            val privacy = prefs.obj("privacy")
            return Privacy(
                shareFreeSlotsOnly = privacy.flag(SHARE_FREE_SLOTS_ONLY),
                hideEventTitles = privacy.flag(HIDE_EVENT_TITLES),
                allowPlaceChoice = privacy.flag(ALLOW_PLACE_CHOICE),
            )
        }

        private fun JsonObject?.flag(key: String): Boolean =
            (this?.get(key) as? JsonPrimitive)?.content?.toBooleanStrictOrNull() ?: true
    }
}
