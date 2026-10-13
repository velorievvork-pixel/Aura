package ai.aura.kit

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

// AuraClientTests — типизированные методы AuraClient без сети: транспорт
// подменён записывающим фейком (WebSocketTransport открыт ради этого).
// Сверяется форма запроса (type + payload) и разбор ответа сервера.

/** Фейковый транспорт: запоминает запросы и отвечает заготовкой или ошибкой. */
private class RecordingTransport(
    private val reply: (type: String, payload: JsonObject) -> JsonObject?,
) : WebSocketTransport() {
    val sent = mutableListOf<Pair<String, JsonObject>>()

    override suspend fun request(type: String, payload: JsonObject): JsonObject? {
        sent += type to payload
        return reply(type, payload)
    }
}

private fun json(text: String): JsonObject = Json.parseToJsonElement(text) as JsonObject

class ProfileUpdateTests {

    @Test
    fun `sends only passed fields and parses user with bio`() = runBlocking {
        val transport = RecordingTransport { _, _ ->
            json("""{"user":{"id":7,"email":"a@b.c","display_name":"Аня","bio":"Люблю кофе","avatar_url":""}}""")
        }
        val user = AuraClient(transport).profileUpdate(displayName = "Аня", bio = "Люблю кофе")

        val (type, payload) = transport.sent.single()
        assertEquals("profile.update", type)
        assertEquals(setOf("display_name", "bio"), payload.keys)
        assertEquals("Аня", payload.str("display_name"))
        assertEquals("Люблю кофе", payload.str("bio"))
        assertEquals(AuraUser(7, "a@b.c", "Аня", "Люблю кофе", ""), user)
    }

    @Test
    fun `bio only and empty avatar are sent as is`() = runBlocking {
        val transport = RecordingTransport { _, _ -> json("""{"user":{"id":1}}""") }
        AuraClient(transport).profileUpdate(bio = "", avatarUrl = "")
        val payload = transport.sent.single().second
        assertEquals(setOf("bio", "avatar_url"), payload.keys)
        assertEquals("", payload.str("bio"))
    }

    @Test
    fun `empty update fails locally without a request`() = runBlocking {
        val transport = RecordingTransport { _, _ -> fail("запрос не должен уходить"); null }
        try {
            AuraClient(transport).profileUpdate()
            fail("ожидалась AuraError")
        } catch (error: AuraError) {
            assertEquals("bad_request", error.code)
        }
        assertTrue(transport.sent.isEmpty())
    }

    @Test
    fun `server bad_request propagates`() = runBlocking {
        val transport = RecordingTransport { _, _ -> throw AuraError("bad_request", "имя — от 1 до 80 символов") }
        try {
            AuraClient(transport).profileUpdate(displayName = "x".repeat(81))
            fail("ожидалась AuraError")
        } catch (error: AuraError) {
            assertEquals("bad_request", error.code)
            assertEquals("имя — от 1 до 80 символов", error.serverMessage)
        }
    }

    @Test
    fun `user without bio parses to empty bio`() {
        val user = AuraUser.from(json("""{"id":3,"email":"x@y.z","display_name":"Икс"}"""))
        assertEquals("", user.bio)
        assertEquals("Икс", user.displayName)
        assertEquals(3L, user.id)
    }
}

class PrivacyTests {

    @Test
    fun `defaults are true when prefs have no privacy`() {
        assertEquals(Privacy(true, true, true), Privacy.fromPrefs(json("""{"theme":"dark"}""")))
        assertEquals(Privacy(), Privacy.fromPrefs(null))
    }

    @Test
    fun `reads flags and fills missing ones with true`() {
        val privacy = Privacy.fromPrefs(json("""{"privacy":{"share_free_slots_only":false,"allow_place_choice":false}}"""))
        assertFalse(privacy.shareFreeSlotsOnly)
        assertTrue(privacy.hideEventTitles)
        assertFalse(privacy.allowPlaceChoice)
    }

    @Test
    fun `privacyGet reads prefs get`() = runBlocking {
        val transport = RecordingTransport { _, _ ->
            json("""{"privacy":{"share_free_slots_only":true,"hide_event_titles":false,"allow_place_choice":true}}""")
        }
        val privacy = AuraClient(transport).privacyGet()
        assertEquals("prefs.get", transport.sent.single().first)
        assertEquals(Privacy(shareFreeSlotsOnly = true, hideEventTitles = false, allowPlaceChoice = true), privacy)
    }

    @Test
    fun `privacySet sends subset at payload root without preferences wrapper`() = runBlocking {
        val transport = RecordingTransport { _, _ ->
            json("""{"onboarded":true,"privacy":{"share_free_slots_only":true,"hide_event_titles":false,"allow_place_choice":true}}""")
        }
        val result = AuraClient(transport).privacySet(hideEventTitles = false)

        val (type, payload) = transport.sent.single()
        assertEquals("prefs.set", type)
        assertEquals(setOf("privacy"), payload.keys)
        val flags = payload.obj("privacy")!!
        assertEquals(setOf("hide_event_titles"), flags.keys)
        assertEquals(JsonPrimitive(false), flags["hide_event_titles"])
        assertEquals(Privacy(true, false, true), result)
    }

    @Test
    fun `privacySet with full model sends all three flags`() = runBlocking {
        val transport = RecordingTransport { _, payload -> buildPrefs(payload) }
        val wanted = Privacy(shareFreeSlotsOnly = false, hideEventTitles = true, allowPlaceChoice = false)
        assertEquals(wanted, AuraClient(transport).privacySet(wanted))
        assertEquals(
            setOf(Privacy.SHARE_FREE_SLOTS_ONLY, Privacy.HIDE_EVENT_TITLES, Privacy.ALLOW_PLACE_CHOICE),
            transport.sent.single().second.obj("privacy")!!.keys,
        )
    }

    /** Эхо сервера: prefs.set возвращает обновлённые настройки целиком. */
    private fun buildPrefs(payload: JsonObject): JsonObject = JsonObject(mapOf("privacy" to payload.obj("privacy")!!))
}
