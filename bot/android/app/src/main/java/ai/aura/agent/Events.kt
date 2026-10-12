package ai.aura.agent

import ai.aura.agent.core.ApiClient
import ai.aura.agent.core.AuraJson
import ai.aura.agent.core.EventDto
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener

/** События агента в реальном времени, пока приложение открыто (WebSocket /api/ws). */
class Events(
    private val api: ApiClient,
    private val onEvent: (EventDto) -> Unit,
    private val onDrop: () -> Unit,
) {
    private var socket: WebSocket? = null

    fun connect() {
        if (socket != null) return
        val url = api.wsUrl() ?: return
        socket = api.http.newWebSocket(Request.Builder().url(url).build(), object : WebSocketListener() {
            override fun onMessage(webSocket: WebSocket, text: String) {
                runCatching { AuraJson.decodeFromString<EventDto>(text) }.getOrNull()
                    ?.takeIf { it.type == "event" }?.let(onEvent)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                socket = null
                onDrop()
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                socket = null
                if (code != 1000) onDrop()
            }
        })
    }

    fun close() {
        socket?.close(1000, null)
        socket = null
    }
}
