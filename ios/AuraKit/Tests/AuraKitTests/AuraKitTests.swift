// AuraKitTests.swift — тесты общего слоя без сети: JSON, кадры протокола,
// deep links. Гоняются `swift test` в ios/AuraKit (или XCTest в Xcode).
import XCTest

@testable import AuraKit

final class JSONValueTests: XCTestCase {

    func testRoundTrip() throws {
        let json = """
        {"id": 12, "ok": true, "text": "привет", "tags": ["a", "b"], "meta": null}
        """
        let value = try JSONDecoder().decode(JSONValue.self, from: Data(json.utf8))
        XCTAssertEqual(value.int("id"), 12)
        XCTAssertEqual(value.bool("ok"), true)
        XCTAssertEqual(value.string("text"), "привет")
        XCTAssertEqual(value.array("tags").count, 2)
        XCTAssertEqual(value["meta"], .null)

        let reEncoded = try JSONEncoder().encode(value)
        let decodedAgain = try JSONDecoder().decode(JSONValue.self, from: reEncoded)
        XCTAssertEqual(decodedAgain, value)
    }

    func testDefaults() {
        let value = JSONValue.object([:])
        XCTAssertEqual(value.string("missing", default: "x"), "x")
        XCTAssertEqual(value.int("missing", default: 7), 7)
        XCTAssertEqual(value.array("missing"), [])
    }
}

final class ProtocolFrameTests: XCTestCase {

    func testRequestFrameShape() throws {
        let frame = AuraRequestFrame(id: "1", type: "auth.login",
                                     payload: .object(["email": .string("a@b.c")]))
        let data = try JSONEncoder().encode(frame)
        let object = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        XCTAssertEqual(object?["id"] as? String, "1")
        XCTAssertEqual(object?["type"] as? String, "auth.login")
        let payload = object?["payload"] as? [String: Any]
        XCTAssertEqual(payload?["email"] as? String, "a@b.c")
    }

    func testErrorFrameDecoding() throws {
        let json = """
        {"id":"1","type":"error","code":"bad_request","message":"нужен email"}
        """
        let frame = try JSONDecoder().decode(AuraResponseFrame.self, from: Data(json.utf8))
        XCTAssertEqual(frame.type, "error")
        XCTAssertEqual(frame.code, "bad_request")
        XCTAssertEqual(frame.message, "нужен email")
    }

    func testEventFrameDecoding() throws {
        let json = """
        {"type":"event","event":"chat.message","payload":{"text":"ок"}}
        """
        let frame = try JSONDecoder().decode(AuraResponseFrame.self, from: Data(json.utf8))
        XCTAssertEqual(frame.type, "event")
        XCTAssertEqual(frame.event, AuraEventName.chatMessage)
        XCTAssertEqual(frame.payload?.string("text"), "ок")
    }
}

final class DeepLinkTests: XCTestCase {

    func testChats() {
        XCTAssertEqual(DeepLink(url: URL(string: "aura://chats/42")!), .chats(id: 42))
        XCTAssertEqual(DeepLink(url: URL(string: "aura://chats")!), .chats(id: nil))
    }

    func testSimpleRoutes() {
        XCTAssertEqual(DeepLink(url: URL(string: "aura://voice")!), .voice)
        XCTAssertEqual(DeepLink(url: URL(string: "aura://tasks")!), .tasks)
        XCTAssertEqual(DeepLink(url: URL(string: "aura://settings")!), .settings)
    }

    func testAsk() {
        let link = DeepLink(url: URL(string: "aura://ask?text=%D0%9F%D1%80%D0%B8%D0%B2%D0%B5%D1%82")!)
        XCTAssertEqual(link, .ask(text: "Привет"))
    }

    func testOAuthCallback() {
        let link = DeepLink(url: URL(string: "aura://oauth?provider=google_gmail&code=abc&state=st1")!)
        XCTAssertEqual(link, .oauth(provider: "google_gmail", code: "abc", state: "st1"))
    }

    func testRejectsForeignSchemeAndUnknownHost() {
        XCTAssertNil(DeepLink(url: URL(string: "https://aura.ai/chats/1")!))
        XCTAssertNil(DeepLink(url: URL(string: "aura://unknown")!))
        XCTAssertNil(DeepLink(url: URL(string: "aura://oauth?provider=x")!))  // нет code/state
    }
}

final class ProfileTests: XCTestCase {

    func testUserDecodesBioAndOldPayloads() throws {
        let fresh = """
        {"id": 7, "email": "a@b.c", "display_name": "Алекс", "bio": "люблю кофе", "avatar_url": ""}
        """
        let user = try JSONDecoder().decode(AuraUser.self, from: Data(fresh.utf8))
        XCTAssertEqual(user, AuraUser(id: 7, email: "a@b.c", displayName: "Алекс", bio: "люблю кофе"))

        // Старый сервер: ни bio, ни avatar_url — декодируется с пустыми строками.
        let old = """
        {"id": 7, "email": "a@b.c", "display_name": "Алекс"}
        """
        let legacy = try JSONDecoder().decode(AuraUser.self, from: Data(old.utf8))
        XCTAssertEqual(legacy.bio, "")
        XCTAssertEqual(legacy.avatarUrl, "")

        let dynamic = try JSONDecoder().decode(JSONValue.self, from: Data(fresh.utf8))
        XCTAssertEqual(AuraUser(json: dynamic), user)
    }

    func testProfileUpdatePayloadHasOnlyPassedFields() {
        XCTAssertEqual(AuraClient.profileUpdatePayload(displayName: "Алекс"),
                       ["display_name": .string("Алекс")])
        XCTAssertEqual(AuraClient.profileUpdatePayload(displayName: "Алекс", bio: ""),
                       ["display_name": .string("Алекс"), "bio": .string("")])
        XCTAssertEqual(AuraClient.profileUpdatePayload(avatarUrl: "https://x.y/a.png"),
                       ["avatar_url": .string("https://x.y/a.png")])
        XCTAssertEqual(AuraClient.profileUpdatePayload(), [:])
    }

    func testProfileRules() {
        XCTAssertNil(AuraProfileRules.validate(displayName: "Алекс", bio: ""))
        XCTAssertNotNil(AuraProfileRules.validate(displayName: "   ", bio: ""))
        XCTAssertNil(AuraProfileRules.validate(displayName: String(repeating: "я", count: 80), bio: ""))
        XCTAssertNotNil(AuraProfileRules.validate(displayName: String(repeating: "я", count: 81), bio: ""))
        XCTAssertNil(AuraProfileRules.validate(displayName: "А", bio: String(repeating: "б", count: 300)))
        XCTAssertNotNil(AuraProfileRules.validate(displayName: "А", bio: String(repeating: "б", count: 301)))
    }
}

final class PrivacyTests: XCTestCase {

    func testDefaultsAreTrue() throws {
        XCTAssertEqual(AuraPrivacy(json: nil), AuraPrivacy())
        XCTAssertEqual(AuraPrivacy(preferences: .emptyObject),
                       AuraPrivacy(shareFreeSlotsOnly: true, hideEventTitles: true, allowPlaceChoice: true))
        let partial = try JSONDecoder().decode(AuraPrivacy.self, from: Data(#"{"hide_event_titles": false}"#.utf8))
        XCTAssertEqual(partial, AuraPrivacy(hideEventTitles: false))
    }

    func testFromPreferences() throws {
        let json = """
        {"theme": "dark", "privacy": {"share_free_slots_only": false, "hide_event_titles": true,
                                      "allow_place_choice": false}}
        """
        let prefs = try JSONDecoder().decode(JSONValue.self, from: Data(json.utf8))
        let privacy = AuraPrivacy(preferences: prefs)
        XCTAssertFalse(privacy[.shareFreeSlotsOnly])
        XCTAssertTrue(privacy[.hideEventTitles])
        XCTAssertFalse(privacy[.allowPlaceChoice])
        XCTAssertEqual(privacy.json, prefs["privacy"])
    }

    func testSetPayloadIsAtRootWithoutWrapper() {
        let payload = AuraClient.privacyPayload([.allowPlaceChoice: false])
        XCTAssertEqual(payload, .object(["privacy": .object(["allow_place_choice": .bool(false)])]))
        XCTAssertNil(payload["preferences"])
    }
}
