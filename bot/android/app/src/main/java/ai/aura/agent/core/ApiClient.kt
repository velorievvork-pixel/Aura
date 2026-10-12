package ai.aura.agent.core

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.encodeToString
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.MultipartBody
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.asRequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.File
import java.io.IOException
import java.util.concurrent.TimeUnit

/** HTTP-клиент API Aura. Токен хранит вызывающая сторона (зашифрованно), здесь — только использование. */
class ApiClient(
    baseUrl: String,
    private val token: () -> String?,
    val http: OkHttpClient = defaultHttp(),
) {
    val base: String = baseUrl.trim().trimEnd('/')

    private val jsonType = "application/json; charset=utf-8".toMediaType()

    suspend fun raw(method: String, path: String, body: RequestBody? = null): String = withContext(Dispatchers.IO) {
        val req = Request.Builder().url(base + path).method(method, body ?: if (method == "POST") EMPTY else null)
        token()?.let { req.header("Authorization", "Bearer $it") }
        try {
            http.newCall(req.build()).execute().use { r ->
                val text = r.body?.string().orEmpty()
                if (!r.isSuccessful) throw parseApiError(r.code, text)
                text
            }
        } catch (e: IOException) {
            throw ApiException(0, "network", "Нет связи с сервером ($base)")
        }
    }

    private fun json(s: String): RequestBody = s.toRequestBody(jsonType)

    private suspend inline fun <reified T> get(path: String): T = AuraJson.decodeFromString(raw("GET", path))

    private suspend inline fun <reified I, reified T> send(method: String, path: String, body: I): T =
        AuraJson.decodeFromString(raw(method, path, json(AuraJson.encodeToString(body))))

    // --- аккаунт ---
    suspend fun register(email: String, password: String, tz: String): AuthResponse =
        send("POST", "/api/auth/register", CredentialsIn(email.trim(), password, tz))

    suspend fun login(email: String, password: String): AuthResponse =
        send("POST", "/api/auth/login", CredentialsIn(email.trim(), password))

    suspend fun logout() { raw("POST", "/api/auth/logout") }
    suspend fun me(): UserDto = get("/api/me")
    suspend fun options(): OptionsDto = get("/api/options")
    suspend fun putProfile(p: ProfileIn): UserDto = send("PUT", "/api/profile", p)
    suspend fun patchSettings(s: SettingsIn): UserDto = send("PATCH", "/api/settings", s)
    suspend fun linkTelegram(code: String): UserDto = send("POST", "/api/link/telegram", CodeIn(code.trim()))

    // --- чат и голос ---
    suspend fun chat(text: String): ChatResponse = send("POST", "/api/chat", TextIn(text))

    suspend fun voice(file: File): ChatResponse {
        val body = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("file", file.name, file.asRequestBody("audio/mp4".toMediaType()))
            .build()
        return AuraJson.decodeFromString(raw("POST", "/api/voice", body))
    }

    suspend fun history(limit: Int = 100): List<HistoryItem> = get("/api/chat/history?limit=$limit")

    // --- память ---
    suspend fun memories(): List<MemoryDto> = get("/api/memories")
    suspend fun deleteMemory(id: Int) { raw("DELETE", "/api/memories/$id") }
    suspend fun wipeMemories() { raw("POST", "/api/memories/wipe", json(AuraJson.encodeToString(ConfirmIn("WIPE")))) }

    // --- задачи ---
    suspend fun tasks(): List<TaskDto> = get("/api/tasks")
    suspend fun taskDone(id: Int) { raw("POST", "/api/tasks/$id/done") }
    suspend fun taskSnooze(id: Int) { raw("POST", "/api/tasks/$id/snooze") }
    suspend fun taskCancel(id: Int) { raw("DELETE", "/api/tasks/$id") }

    // --- встречи ---
    suspend fun negotiations(): List<NegotiationDto> = get("/api/negotiations")
    suspend fun startMeeting(withRef: String, topic: String, minutes: Int): NegotiationDto =
        send("POST", "/api/negotiations", MeetingIn(withRef.trim(), topic.trim(), minutes))

    suspend fun confirmMeeting(id: Int, accept: Boolean): NegotiationDto =
        send("POST", "/api/negotiations/$id/confirm", VerdictIn(accept))

    // --- пуши, подписка, статистика ---
    suspend fun registerDevice(fcmToken: String) {
        raw("POST", "/api/devices", json(AuraJson.encodeToString(DeviceIn(fcmToken))))
    }

    suspend fun billing(): BillingDto = get("/api/billing")
    suspend fun tonInvoice(): TonInvoiceDto = AuraJson.decodeFromString(raw("POST", "/api/billing/ton"))
    suspend fun tonCheck(id: Int): TonStatus = AuraJson.decodeFromString(raw("POST", "/api/billing/ton/$id/check"))
    suspend fun stats(): StatsDto = get("/api/stats")

    /** ws(s)://…/api/ws?token=… для событий в реальном времени. */
    fun wsUrl(): String? {
        val t = token() ?: return null
        val ws = when {
            base.startsWith("https://") -> "wss://" + base.removePrefix("https://")
            base.startsWith("http://") -> "ws://" + base.removePrefix("http://")
            else -> return null
        }
        return "$ws/api/ws?token=$t"
    }

    companion object {
        private val EMPTY: RequestBody = ByteArray(0).toRequestBody(null)

        fun defaultHttp(): OkHttpClient = OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(120, TimeUnit.SECONDS)   // ответ LLM и распознавание речи бывают долгими
            .writeTimeout(60, TimeUnit.SECONDS)
            .pingInterval(30, TimeUnit.SECONDS)
            .build()
    }
}
