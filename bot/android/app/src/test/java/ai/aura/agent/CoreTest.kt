package ai.aura.agent

import ai.aura.agent.core.ApiClient
import ai.aura.agent.core.ApiException
import ai.aura.agent.core.AuraJson
import ai.aura.agent.core.ChatResponse
import ai.aura.agent.core.NegotiationDto
import ai.aura.agent.core.Validation
import ai.aura.agent.core.parseApiError
import kotlinx.coroutines.test.runTest
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

// JVM-тесты протокола клиента: формат JSON совпадает с bot/aura_bot/api.py.
class CoreTest {

    @Test
    fun chatResponseParsesServerJson() {
        val raw = """{"reply":"Ок\n\n⏰ Напомню завтра в 15:00: Хлеб","task":{"id":1,"text":"Хлеб",
            "due_at":"2026-10-13T12:00:00+00:00","repeat":"none","status":"pending","due_human":"завтра в 15:00"},
            "task_error":null,"meeting_error":null,"negotiation_started":false,"usage":{"used":1,"limit":20},"extra":1}"""
        val r = AuraJson.decodeFromString<ChatResponse>(raw)
        assertEquals("завтра в 15:00", r.task?.dueHuman)
        assertEquals(20, r.usage.limit)
        assertNull(r.transcript)
    }

    @Test
    fun negotiationUsesWithField() {
        val n = AuraJson.decodeFromString<NegotiationDto>(
            """{"id":3,"topic":"обед","status":"agreed","with":"Олег","needs_my_confirmation":true}""")
        assertEquals("Олег", n.withName)
        assertTrue(n.needsMyConfirmation)
    }

    @Test
    fun errorBodyMessageShownToUser() {
        val e = parseApiError(429, """{"error":{"code":"limit","message":"Дневной лимит исчерпан (20/20)"}}""")
        assertEquals("limit", e.code)
        assertEquals("Дневной лимит исчерпан (20/20)", e.message)
        assertEquals("Сервер недоступен, попробуйте позже", parseApiError(502, "<html>").message)
    }

    @Test
    fun validationMatchesServerRules() {
        assertNull(Validation.passwordProblem("secret123"))
        assertEquals("Пароль должен содержать буквы и цифры", Validation.passwordProblem("12345678"))
        assertTrue(Validation.passwordProblem("a1") != null)
        assertNull(Validation.emailProblem(" anya@example.com "))
        assertTrue(Validation.emailProblem("anya@") != null)
        assertEquals("1994-03-07", Validation.birthdayToIso("07.03.1994"))
        assertNull(Validation.birthdayToIso(" "))
        assertEquals("07.03.1994", Validation.isoToBirthday("1994-03-07"))
        try {
            Validation.birthdayToIso("31.02.1990"); fail()
        } catch (e: IllegalArgumentException) { /* ожидаемо */ }
        assertEquals(listOf("арахис", "мёд"), Validation.splitList("арахис, мёд; Арахис"))
        assertTrue(Validation.splitList("нет").isEmpty())
    }

    @Test
    fun clientSendsBearerAndParses() = runTest {
        val server = MockWebServer()
        server.enqueue(MockResponse().setBody("""{"reply":"Привет","usage":{"used":2,"limit":20}}"""))
        server.enqueue(MockResponse().setResponseCode(401).setBody("""{"error":{"code":"unauthorized","message":"Сессия истекла"}}"""))
        server.start()
        val api = ApiClient(server.url("/").toString(), { "tok123" })
        assertEquals("Привет", api.chat("hi").reply)
        val req = server.takeRequest()
        assertEquals("Bearer tok123", req.getHeader("Authorization"))
        assertEquals("/api/chat", req.path)
        assertEquals("""{"text":"hi"}""", req.body.readUtf8())
        try {
            api.me(); fail()
        } catch (e: ApiException) {
            assertEquals(401, e.status)
            assertEquals("Сессия истекла", e.message)
        }
        assertTrue(api.wsUrl()!!.startsWith("ws://"))
        assertFalse(api.wsUrl()!!.contains("http"))
        server.shutdown()
    }
}
