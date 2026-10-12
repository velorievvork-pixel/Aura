package ai.aura.agent

import ai.aura.agent.core.ApiClient
import ai.aura.agent.core.ApiException
import ai.aura.agent.core.BillingDto
import ai.aura.agent.core.ChatResponse
import ai.aura.agent.core.MemoryDto
import ai.aura.agent.core.NegotiationDto
import ai.aura.agent.core.OptionsDto
import ai.aura.agent.core.ProfileIn
import ai.aura.agent.core.SettingsIn
import ai.aura.agent.core.StatsDto
import ai.aura.agent.core.TaskDto
import ai.aura.agent.core.TonInvoiceDto
import ai.aura.agent.core.Usage
import ai.aura.agent.core.UserDto
import ai.aura.agent.core.Validation
import android.app.Application
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.serialization.SerializationException
import java.io.File
import java.util.TimeZone

enum class Screen { Loading, Offline, Login, Onboarding, Main }
enum class Tab { Chat, Tasks, Memory, Settings }
data class ChatMsg(val mine: Boolean, val text: String)

/** Состояние приложения и все обращения к API (одна Activity — один ViewModel). */
class AppViewModel(app: Application) : AndroidViewModel(app) {
    private val prefs = Prefs(app)
    private var api = newApi()
    private val speaker = Speaker(app)
    private val recorder = Recorder(app)
    private var events: Events? = null

    var screen by mutableStateOf(Screen.Loading); private set
    var tab by mutableStateOf(Tab.Chat)
    var user by mutableStateOf<UserDto?>(null); private set
    var options by mutableStateOf(OptionsDto()); private set
    val messages = mutableStateListOf<ChatMsg>()
    var tasks by mutableStateOf(listOf<TaskDto>()); private set
    var negotiations by mutableStateOf(listOf<NegotiationDto>()); private set
    var memories by mutableStateOf(listOf<MemoryDto>()); private set
    var billing by mutableStateOf<BillingDto?>(null); private set
    var stats by mutableStateOf<StatsDto?>(null); private set
    var tonInvoice by mutableStateOf<TonInvoiceDto?>(null)
    var usage by mutableStateOf<Usage?>(null); private set
    var busy by mutableStateOf(false); private set
    var thinking by mutableStateOf(false); private set
    var recording by mutableStateOf(false); private set
    var toast by mutableStateOf<String?>(null)
    var serverUrl by mutableStateOf(prefs.serverUrl)
    var speakReplies by mutableStateOf(prefs.speakReplies); private set

    val deviceTimezone: String = TimeZone.getDefault().id

    init {
        start()
    }

    private fun newApi() = ApiClient(prefs.serverUrl, { prefs.token })

    /** Запуск корутины с единым разбором ошибок: текст ошибки сервера — в snackbar. */
    private fun io(showBusy: Boolean = true, block: suspend () -> Unit): Job = viewModelScope.launch {
        if (showBusy) busy = true
        try {
            block()
        } catch (e: ApiException) {
            if (e.status == 401 && prefs.token != null) dropSession()
            toast = e.message
        } catch (e: SerializationException) {
            toast = "Неожиданный ответ сервера — проверьте адрес"
        } catch (e: IllegalArgumentException) {
            toast = e.message
        } finally {
            if (showBusy) busy = false
        }
    }

    fun start() {
        if (prefs.token == null) {
            screen = Screen.Login
            return
        }
        screen = Screen.Loading
        viewModelScope.launch {
            try {
                options = api.options()
                enter(api.me())
            } catch (e: ApiException) {
                if (e.status == 401) dropSession() else screen = Screen.Offline
                toast = e.message
            } catch (e: SerializationException) {
                screen = Screen.Offline
            }
        }
    }

    private suspend fun enter(u: UserDto) {
        user = u
        if (!u.onboarded) {
            screen = Screen.Onboarding
            return
        }
        screen = Screen.Main
        val history = api.history()
        messages.clear()
        messages.addAll(history.map { ChatMsg(it.role == "user", it.text) })
        refreshTasks()
        connectEvents()
        registerPush()
    }

    // ---------- вход ----------

    fun auth(email: String, password: String, register: Boolean) {
        Validation.emailProblem(email)?.let { toast = it; return }
        if (register) Validation.passwordProblem(password)?.let { toast = it; return }
        prefs.serverUrl = serverUrl
        api = newApi()
        io {
            val res = if (register) api.register(email, password, deviceTimezone) else api.login(email, password)
            prefs.token = res.token
            options = api.options()
            enter(res.user)
        }
    }

    fun saveProfile(birthday: String, allergies: String, diets: List<String>, habits: String, tz: String) = io {
        val p = ProfileIn(
            birthday = Validation.birthdayToIso(birthday),
            allergies = Validation.splitList(allergies),
            diets = diets,
            habits = Validation.splitList(habits),
            timezone = tz,
        )
        enter(api.putProfile(p))
        toast = "Анкета сохранена — учитываю её в каждом ответе"
    }

    fun redoOnboarding() {
        screen = Screen.Onboarding
    }

    fun cancelOnboarding() {
        if (user?.onboarded == true) screen = Screen.Main
    }

    fun logout() = io {
        runCatching { api.logout() }
        dropSession()
    }

    private fun dropSession() {
        prefs.token = null
        events?.close()
        events = null
        user = null
        messages.clear()
        screen = Screen.Login
    }

    // ---------- чат и голос ----------

    private fun accept(res: ChatResponse) {
        res.transcript?.let { messages.add(ChatMsg(true, "🎙 $it")) }
        messages.add(ChatMsg(false, res.reply))
        usage = res.usage
        if (speakReplies) speaker.speak(res.reply)
        if (res.task != null || res.negotiationStarted) viewModelScope.launch { runCatching { refreshTasks() } }
    }

    fun send(text: String) {
        val t = text.trim()
        if (t.isEmpty() || thinking) return
        messages.add(ChatMsg(true, t))
        thinking = true
        io(showBusy = false) {
            try {
                accept(api.chat(t))
            } finally {
                thinking = false
            }
        }
    }

    fun toggleRecording() {
        if (recording) {
            recording = false
            val file = recorder.stop() ?: run { toast = "Запись слишком короткая"; return }
            sendVoice(file)
        } else {
            speaker.stop()
            recording = recorder.start()
            if (!recording) toast = "Не удалось начать запись"
        }
    }

    private fun sendVoice(file: File) {
        thinking = true
        io(showBusy = false) {
            try {
                accept(api.voice(file))
            } finally {
                thinking = false
                file.delete()
            }
        }
    }

    fun setSpeak(on: Boolean) {
        speakReplies = on
        prefs.speakReplies = on
        if (!on) speaker.stop()
    }

    // ---------- задачи и встречи ----------

    suspend fun refreshTasks() {
        tasks = api.tasks()
        negotiations = api.negotiations()
    }

    fun reloadTasks() = io { refreshTasks() }

    fun taskDone(id: Int) = io { api.taskDone(id); refreshTasks() }
    fun taskSnooze(id: Int) = io { api.taskSnooze(id); refreshTasks(); toast = "Напомню через час" }
    fun taskCancel(id: Int) = io { api.taskCancel(id); refreshTasks() }

    fun meet(withRef: String, topic: String, minutes: Int) = io {
        api.startMeeting(withRef, topic, minutes)
        toast = "Агенты договариваются — результат придёт на подтверждение"
        delay(1500)
        refreshTasks()
    }

    fun confirmMeeting(id: Int, accept: Boolean) = io {
        api.confirmMeeting(id, accept)
        refreshTasks()
    }

    // ---------- память ----------

    fun reloadMemories() = io { memories = api.memories() }
    fun deleteMemory(id: Int) = io { api.deleteMemory(id); memories = api.memories() }
    fun wipeMemories() = io {
        api.wipeMemories()
        memories = emptyList()
        messages.clear()
        toast = "Память и история очищены"
    }

    // ---------- настройки и подписка ----------

    fun reloadAccount() = io {
        user = api.me()
        billing = api.billing()
        stats = api.stats()
    }

    fun setTimezone(tz: String) = io { user = api.patchSettings(SettingsIn(timezone = tz)) }

    fun linkTelegram(code: String) = io {
        user = api.linkTelegram(code)
        enter(user!!)
        toast = "Telegram привязан: память и задачи теперь общие"
    }

    fun createTonInvoice() = io { tonInvoice = api.tonInvoice() }

    fun checkTon() = io {
        val inv = tonInvoice ?: return@io
        if (api.tonCheck(inv.id).status == "paid") {
            tonInvoice = null
            toast = "Премиум активирован!"
            billing = api.billing()
            user = api.me()
        } else {
            toast = "Перевод пока не найден — проверьте через минуту"
        }
    }

    // ---------- события и пуши ----------

    private fun connectEvents() {
        events?.close()
        events = Events(api, onEvent = { e ->
            viewModelScope.launch(Dispatchers.Main) {
                toast = listOf(e.title, e.body).filter { it.isNotBlank() }.joinToString(" ")
                runCatching { refreshTasks() }
            }
        }, onDrop = {
            viewModelScope.launch(Dispatchers.Main) {
                delay(10_000)
                if (screen == Screen.Main) events?.connect()
            }
        }).also { it.connect() }
    }

    private fun registerPush() {
        if (!Push.init(getApplication())) return
        viewModelScope.launch {
            val token = runCatching { Push.token() }.getOrNull() ?: prefs.fcmToken ?: return@launch
            prefs.fcmToken = token
            runCatching { api.registerDevice(token) }
        }
    }

    val pushConfigured: Boolean get() = Push.configured

    override fun onCleared() {
        events?.close()
        recorder.cancel()
        speaker.shutdown()
    }
}
