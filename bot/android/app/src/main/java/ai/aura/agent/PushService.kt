package ai.aura.agent

import ai.aura.agent.core.ApiClient
import com.google.firebase.messaging.FirebaseMessagingService
import com.google.firebase.messaging.RemoteMessage
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/** Приём FCM: напоминания и события агента приходят сюда, даже когда приложение закрыто. */
class PushService : FirebaseMessagingService() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    override fun onNewToken(token: String) {
        val prefs = Prefs(this)
        prefs.fcmToken = token
        if (prefs.token == null) return
        scope.launch {
            runCatching { ApiClient(prefs.serverUrl, { prefs.token }).registerDevice(token) }
        }
    }

    override fun onMessageReceived(message: RemoteMessage) {
        // При открытом приложении система уведомление сама не покажет — показываем вручную.
        val title = message.notification?.title ?: message.data["title"] ?: "Aura"
        val body = message.notification?.body ?: message.data["body"] ?: return
        Push.show(this, title, body)
    }
}
