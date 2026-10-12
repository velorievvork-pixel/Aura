package ai.aura.agent.core

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

// DTO HTTP API бэкенда (bot/aura_bot/api.py). Чистый Kotlin — тестируется без Android.

val AuraJson = Json {
    ignoreUnknownKeys = true
    explicitNulls = false
    encodeDefaults = true
}

@Serializable
data class ProfileDto(
    val birthday: String? = null,
    val age: Int? = null,
    val allergies: List<String> = emptyList(),
    val diets: List<String> = emptyList(),
    val habits: List<String> = emptyList(),
)

@Serializable
data class UserDto(
    val id: Int,
    val email: String? = null,
    val name: String = "",
    val timezone: String = "Europe/Moscow",
    @SerialName("telegram_linked") val telegramLinked: Boolean = false,
    @SerialName("voice_replies") val voiceReplies: Boolean = false,
    val onboarded: Boolean = false,
    val premium: Boolean = false,
    @SerialName("premium_until") val premiumUntil: String? = null,
    val profile: ProfileDto? = null,
)

@Serializable
data class AuthResponse(val token: String, val user: UserDto)

@Serializable
data class TaskDto(
    val id: Int,
    val text: String,
    @SerialName("due_at") val dueAt: String,
    val repeat: String = "none",
    val status: String = "pending",
    @SerialName("due_human") val dueHuman: String = "",
)

@Serializable
data class Usage(val used: Int = 0, val limit: Int = 0)

@Serializable
data class ChatResponse(
    val reply: String,
    val task: TaskDto? = null,
    @SerialName("task_error") val taskError: String? = null,
    @SerialName("meeting_error") val meetingError: String? = null,
    @SerialName("negotiation_started") val negotiationStarted: Boolean = false,
    val usage: Usage = Usage(),
    val transcript: String? = null,
)

@Serializable
data class HistoryItem(val role: String, val text: String, @SerialName("created_at") val createdAt: String = "")

@Serializable
data class MemoryDto(val id: Int, val kind: String, val text: String)

@Serializable
data class NegotiationDto(
    val id: Int,
    val topic: String,
    val status: String,
    @SerialName("duration_min") val durationMin: Int = 60,
    @SerialName("with") val withName: String = "",
    val rounds: Int = 0,
    @SerialName("slot_human") val slotHuman: String? = null,
    @SerialName("needs_my_confirmation") val needsMyConfirmation: Boolean = false,
)

@Serializable
data class BillingDto(
    val premium: Boolean = false,
    @SerialName("premium_until") val premiumUntil: String? = null,
    val used: Int = 0,
    val limit: Int = 0,
    @SerialName("price_rub") val priceRub: Int = 0,
    @SerialName("price_stars") val priceStars: Int = 0,
    val days: Int = 30,
    @SerialName("stars_link") val starsLink: String? = null,
    @SerialName("ton_enabled") val tonEnabled: Boolean = false,
    @SerialName("price_ton") val priceTon: String? = null,
)

@Serializable
data class TonInvoiceDto(
    val id: Int,
    val wallet: String,
    @SerialName("amount_ton") val amountTon: String,
    val comment: String,
    val link: String,
)

@Serializable
data class TonStatus(val status: String)

@Serializable
data class StatsDto(
    val today: Int = 0,
    val limit: Int = 0,
    val messages: Int = 0,
    val memories: Int = 0,
    @SerialName("tasks_active") val tasksActive: Int = 0,
    @SerialName("tasks_done") val tasksDone: Int = 0,
)

@Serializable
data class CodeTitle(val code: String, val title: String)

@Serializable
data class NameTitle(val name: String, val title: String)

@Serializable
data class OptionsDto(
    val diets: List<CodeTitle> = emptyList(),
    val timezones: List<NameTitle> = emptyList(),
    @SerialName("memory_kinds") val memoryKinds: Map<String, String> = emptyMap(),
)

// --- тела запросов ---

@Serializable
data class CredentialsIn(val email: String, val password: String, val timezone: String? = null)

@Serializable
data class ProfileIn(
    val birthday: String?,
    val allergies: List<String>,
    val diets: List<String>,
    val habits: List<String>,
    val timezone: String,
)

@Serializable
data class TextIn(val text: String)

@Serializable
data class SettingsIn(
    @SerialName("voice_replies") val voiceReplies: Boolean? = null,
    val timezone: String? = null,
)

@Serializable
data class CodeIn(val code: String)

@Serializable
data class ConfirmIn(val confirm: String)

@Serializable
data class DeviceIn(@SerialName("fcm_token") val fcmToken: String)

@Serializable
data class MeetingIn(
    @SerialName("with") val withRef: String,
    val topic: String,
    @SerialName("duration_min") val durationMin: Int,
)

@Serializable
data class VerdictIn(val accept: Boolean)

@Serializable
private data class ErrorBody(val error: ErrorDetail? = null)

@Serializable
private data class ErrorDetail(val code: String = "error", val message: String = "")

/** Ошибка API: [code] — машинный код (limit, unauthorized…), message — текст для пользователя. */
class ApiException(val status: Int, val code: String, message: String) : Exception(message)

fun parseApiError(status: Int, body: String?): ApiException {
    val detail = runCatching { AuraJson.decodeFromString<ErrorBody>(body ?: "").error }.getOrNull()
    val text = detail?.message?.takeIf { it.isNotBlank() } ?: when (status) {
        401 -> "Нужно войти заново"
        429 -> "Слишком много запросов, попробуйте позже"
        in 500..599 -> "Сервер недоступен, попробуйте позже"
        else -> "Ошибка запроса ($status)"
    }
    return ApiException(status, detail?.code ?: "http_$status", text)
}

/** Клиентские проверки повторяют серверные (сервер всё равно проверит сам). */
object Validation {
    private val email = Regex("^[^@\\s]{1,64}@[^@\\s]+\\.[^@\\s]{2,}$")

    fun emailProblem(value: String): String? =
        if (email.matches(value.trim())) null else "Некорректный email"

    fun passwordProblem(value: String): String? = when {
        value.length < 8 -> "Пароль: минимум 8 символов"
        value.length > 128 -> "Пароль: максимум 128 символов"
        !value.any { it.isLetter() } || !value.any { it.isDigit() } -> "Пароль должен содержать буквы и цифры"
        else -> null
    }

    /** «07.03.1994» -> «1994-03-07»; пусто -> null; неверно -> IllegalArgumentException. */
    fun birthdayToIso(value: String): String? {
        val v = value.trim()
        if (v.isEmpty()) return null
        val m = Regex("^(\\d{1,2})[./-](\\d{1,2})[./-](\\d{4})$").find(v)
            ?: throw IllegalArgumentException("Дата: ДД.ММ.ГГГГ")
        val (d, mo, y) = m.destructured
        val date = runCatching { java.time.LocalDate.of(y.toInt(), mo.toInt(), d.toInt()) }
            .getOrElse { throw IllegalArgumentException("Такой даты нет") }
        require(!date.isAfter(java.time.LocalDate.now()) && date.year >= 1900) { "Дата вне допустимого диапазона" }
        return date.toString()
    }

    fun isoToBirthday(iso: String?): String {
        if (iso.isNullOrBlank()) return ""
        val p = iso.split("-")
        return if (p.size == 3) "${p[2]}.${p[1]}.${p[0]}" else ""
    }

    fun splitList(value: String): List<String> =
        value.split(',', ';', '\n').map { it.trim() }.filter { it.isNotEmpty() }
            .filterNot { it.lowercase() in setOf("нет", "-", "—") }.distinctBy { it.lowercase() }
}

/** Событие из WebSocket /api/ws (то же, что уходит в Telegram и FCM). */
@Serializable
data class EventDto(
    val type: String = "",
    val event: String = "",
    val title: String = "",
    val body: String = "",
)
