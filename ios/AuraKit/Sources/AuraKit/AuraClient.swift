// AuraClient.swift — типизированный API клиента Aura поверх WS-транспорта.
//
// Методы повторяют имена WS-хендлеров сервера (auth.login, chat.send,
// agent.ask, tasks.*, integrations.* …). Ответ — payload кадра "ok";
// ошибки прилетают AuraError с серверным code/message.
import Foundation

public final class AuraClient: @unchecked Sendable {

    private let transport = WebSocketTransport()

    public init() {}

    public var isConnected: Bool { transport.state == .connected }
    public var onStateChange: ((WebSocketTransport.State) -> Void)? {
        get { transport.onStateChange }
        set { transport.onStateChange = newValue }
    }

    public func connect(serverURL: URL) throws {
        try transport.connect(url: serverURL)
    }

    public func disconnect() {
        transport.disconnect()
    }

    public func events() -> AsyncStream<WebSocketTransport.AuraEvent> {
        transport.events()
    }

    /// Универсальный запрос (payload собирается вызывающим кодом).
    public func request(_ type: String, _ payload: [String: JSONValue] = [:]) async throws -> JSONValue {
        try await transport.request(type, .object(payload))
    }

    /// Универсальный запрос с произвольным JSON-payload (например prefs.set,
    /// где поля настроек лежат прямо в корне payload).
    public func request(_ type: String, _ payload: JSONValue) async throws -> JSONValue {
        try await transport.request(type, payload)
    }

    // MARK: - Auth

    /// `ref` — реферальный код из приглашения: уходит (с ref_source "link") только
    /// если это валидный код из 8 символов A–Z/0–9; неверный молча отбрасывается.
    public func register(email: String, password: String, displayName: String,
                         ref: String? = nil) async throws -> JSONValue {
        try await request("auth.register", AuraClient.registerPayload(email: email, password: password,
                                                                       displayName: displayName, ref: ref))
    }

    /// Payload auth.register (отдельно — чтобы проверять без сети).
    public static func registerPayload(email: String, password: String, displayName: String,
                                       ref: String? = nil) -> [String: JSONValue] {
        var payload: [String: JSONValue] = [
            "email": .string(email),
            "password": .string(password),
            "display_name": .string(displayName)
        ]
        if let code = ReferralCode.normalize(ref) {
            payload["ref"] = .string(code)
            payload["ref_source"] = .string("link")
        }
        return payload
    }

    public func verifyEmail(email: String, code: String) async throws -> JSONValue {
        try await request("auth.verifyEmail", [
            "email": .string(email),
            "code": .string(code)
        ])
    }

    public func resendCode(email: String) async throws -> JSONValue {
        try await request("auth.resendCode", ["email": .string(email)])
    }

    public func login(email: String, password: String, device: String,
                      deviceId: String) async throws -> JSONValue {
        try await request("auth.login", [
            "email": .string(email),
            "password": .string(password),
            "device": .string(device),
            "device_id": .string(deviceId)
        ])
    }

    /// Завершение входа с TOTP-кодом (2FA). Сервер повторно проверяет пароль
    /// (docs/PROTOCOL.md: auth.login2fa = email + password + code).
    public func login2fa(email: String, password: String, code: String,
                         trustDevice: Bool, device: String,
                         deviceId: String) async throws -> JSONValue {
        try await request("auth.login2fa", [
            "email": .string(email),
            "password": .string(password),
            "code": .string(code),
            "trust_device": .bool(trustDevice),
            "device": .string(device),
            "device_id": .string(deviceId)
        ])
    }

    /// Повторная аутентификация соединения сохранённым access-токеном.
    public func authenticate(token: String) async throws -> JSONValue {
        try await request("auth.token", ["token": .string(token)])
    }

    public func refresh(refreshToken: String) async throws -> JSONValue {
        try await request("auth.refresh", ["refresh_token": .string(refreshToken)])
    }

    public func me() async throws -> JSONValue {
        try await request("auth.me")
    }

    public func logout() async throws -> JSONValue {
        try await request("auth.logout")
    }

    public func status2fa() async throws -> JSONValue {
        try await request("auth.status2fa")
    }

    public func setup2fa() async throws -> JSONValue {
        try await request("auth.setup2fa")
    }

    public func confirm2fa(code: String) async throws -> JSONValue {
        try await request("auth.confirm2fa", ["code": .string(code)])
    }

    public func disable2fa(password: String) async throws -> JSONValue {
        try await request("auth.disable2fa", ["password": .string(password)])
    }

    /// Сброс пароля: код уходит на почту (ответ одинаковый, есть ли аккаунт).
    public func forgotPassword(email: String) async throws -> JSONValue {
        try await request("auth.forgotPassword", ["email": .string(email)])
    }

    public func resetPassword(email: String, code: String, newPassword: String) async throws -> JSONValue {
        try await request("auth.resetPassword", [
            "email": .string(email),
            "code": .string(code),
            "new_password": .string(newPassword)
        ])
    }

    // MARK: - Профиль

    /// profile.update: меняются только переданные (не nil) поля. Сервер проверяет
    /// имя 1–80, «о себе» ≤300 и аватар (пусто, https://… или data:image/…;base64);
    /// неверное значение или пустой payload — AuraError(code: "bad_request").
    public func updateProfile(displayName: String? = nil, bio: String? = nil,
                              avatarUrl: String? = nil) async throws -> AuraUser {
        let response = try await request("profile.update",
                                         Self.profileUpdatePayload(displayName: displayName, bio: bio,
                                                                   avatarUrl: avatarUrl))
        return AuraUser(json: response["user"] ?? response)
    }

    /// Payload profile.update: только переданные поля, ключи как в протоколе.
    public static func profileUpdatePayload(displayName: String? = nil, bio: String? = nil,
                                            avatarUrl: String? = nil) -> [String: JSONValue] {
        var payload: [String: JSONValue] = [:]
        if let displayName { payload["display_name"] = .string(displayName) }
        if let bio { payload["bio"] = .string(bio) }
        if let avatarUrl { payload["avatar_url"] = .string(avatarUrl) }
        return payload
    }

    // MARK: - Чаты и сообщения

    public func chatList() async throws -> JSONValue {
        try await request("chat.list")
    }

    public func chatOpen(contact: String) async throws -> JSONValue {
        try await request("chat.open", ["contact": .string(contact)])
    }

    public func chatHistory(chatId: Int64, limit: Int = 50) async throws -> JSONValue {
        try await request("chat.history", [
            "chat_id": .from(chatId),
            "limit": .from(limit)
        ])
    }

    public func chatRead(chatId: Int64) async throws -> JSONValue {
        try await request("chat.read", ["chat_id": .from(chatId)])
    }

    public func chatSend(chatId: Int64, text: String) async throws -> JSONValue {
        // Поле протокола — "body" (docs/PROTOCOL.md); сервер читает именно его.
        try await request("chat.send", [
            "chat_id": .from(chatId),
            "body": .string(text)
        ])
    }

    // MARK: - Аура (агент)

    /// Запрос к Ауре. chatId=0 — без контекста чата (Siri/Shortcuts).
    public func agentAsk(chatId: Int64, message: String) async throws -> JSONValue {
        try await request("agent.ask", [
            "chat_id": .from(chatId),
            "message": .string(message)
        ])
    }

    /// A2A-переговоры, один раунд: target — email или имя собеседника, окно в часах.
    /// Ответ: accepted, slot{start,end}, place{name,city,tags,rating}, counter_slots[], message.
    public func agentNegotiate(target: String, topic: String = "", durationMinutes: Int = 60,
                               windowHours: Int = 96) async throws -> JSONValue {
        var payload: [String: JSONValue] = [
            "target": .string(target),
            "duration_minutes": .from(durationMinutes),
            "window_hours": .from(windowHours)
        ]
        if !topic.isEmpty { payload["topic"] = .string(topic) }
        return try await request("agent.negotiate", payload)
    }

    /// Прямой запуск инструмента по кнопке пользователя (например book_table).
    public func toolRun(tool: String, args: [String: JSONValue]) async throws -> JSONValue {
        try await request("tool.run", ["tool": .string(tool), "args": .object(args)])
    }

    /// Поиск людей по имени или email, чтобы начать с ними чат (chat.open).
    public func usersSearch(query: String, limit: Int = 10) async throws -> JSONValue {
        try await request("users.search", ["query": .string(query), "limit": .from(limit)])
    }

    /// Серверный STT: аудио в base64 (формат согласуется с AI-сервисом).
    public func speechTranscribe(audioBase64: String, language: String,
                                 format: String) async throws -> JSONValue {
        try await request("speech.transcribe", [
            "audio": .string(audioBase64),
            "language": .string(language),
            "format": .string(format)
        ])
    }

    // MARK: - Память и настройки

    public func memoryList() async throws -> JSONValue {
        try await request("memory.list")
    }

    public func memoryAdd(text: String, kind: String = "fact") async throws -> JSONValue {
        try await request("memory.add", ["text": .string(text), "kind": .string(kind)])
    }

    public func prefsGet() async throws -> JSONValue {
        try await request("prefs.get")
    }

    public func prefsSet(_ preferences: JSONValue) async throws -> JSONValue {
        // Сервер читает поля настроек прямо из payload (docs/PROTOCOL.md):
        // prefs.set { "theme": ..., "notifications": {...} } — без обёртки.
        try await request("prefs.set", preferences)
    }

    /// Приватность переговоров из prefs.get (сервер всегда отдаёт все три флага).
    public func privacyGet() async throws -> AuraPrivacy {
        AuraPrivacy(preferences: try await prefsGet())
    }

    /// prefs.set {"privacy": {...}} — в корне payload, без обёртки; сервер сливает
    /// переданные флаги с текущими. Ответ — обновлённые настройки целиком.
    public func privacySet(_ changes: [AuraPrivacy.Flag: Bool]) async throws -> JSONValue {
        try await prefsSet(Self.privacyPayload(changes))
    }

    /// Payload prefs.set для частичного обновления приватности.
    public static func privacyPayload(_ changes: [AuraPrivacy.Flag: Bool]) -> JSONValue {
        var flags: [String: JSONValue] = [:]
        for (flag, value) in changes { flags[flag.rawValue] = .bool(value) }
        return .object(["privacy": .object(flags)])
    }

    // MARK: - Задачи (этап 8)

    public func tasksList(status: String = "") async throws -> JSONValue {
        var payload: [String: JSONValue] = [:]
        if !status.isEmpty { payload["status"] = .string(status) }
        return try await request("tasks.list", payload)
    }

    public func taskCreate(title: String, notes: String = "", remindAt: String = "",
                           dueAt: String = "") async throws -> JSONValue {
        var payload: [String: JSONValue] = ["title": .string(title)]
        if !notes.isEmpty { payload["notes"] = .string(notes) }
        if !remindAt.isEmpty { payload["remind_at"] = .string(remindAt) }
        if !dueAt.isEmpty { payload["due_at"] = .string(dueAt) }
        return try await request("tasks.create", payload)
    }

    public func taskSetStatus(id: Int64, status: String) async throws -> JSONValue {
        try await request("tasks." + status, ["id": .from(id)])
    }

    public func taskDelete(id: Int64) async throws -> JSONValue {
        try await request("tasks.delete", ["id": .from(id)])
    }

    // MARK: - Разрешения и подтверждения (этап 8)

    // MARK: - Страницы записи и рефералы

    public func bookingPageCreate(_ fields: BookingPageFields) async throws -> BookingPage {
        if let problem = fields.validate() { throw AuraError(code: "bad_request", message: problem) }
        return BookingPage(json: try await request("booking.pageCreate", fields.payload)["page"] ?? .null)
    }

    /// status — active | paused; fields == nil — меняется только статус.
    public func bookingPageUpdate(id: Int64, fields: BookingPageFields? = nil,
                                  status: String? = nil) async throws -> BookingPage {
        if let problem = fields?.validate() { throw AuraError(code: "bad_request", message: problem) }
        var payload = fields?.payload ?? [:]
        payload["page_id"] = .from(id)
        if let status { payload["status"] = .string(status) }
        return BookingPage(json: try await request("booking.pageUpdate", payload)["page"] ?? .null)
    }

    public func bookingPageList() async throws -> [BookingPage] {
        try await request("booking.pageList").array("pages").map(BookingPage.init(json:))
    }

    public func bookingPageDelete(id: Int64) async throws -> JSONValue {
        try await request("booking.pageDelete", ["page_id": .from(id)])
    }

    public func bookingList(limit: Int = 50) async throws -> [Booking] {
        try await request("booking.list", ["limit": .from(limit)]).array("bookings").map(Booking.init(json:))
    }

    /// occurrence — start одной встречи серии; nil — все предстоящие.
    public func bookingOwnerCancel(id: Int64, occurrence: String? = nil) async throws -> Booking {
        var payload: [String: JSONValue] = ["booking_id": .from(id)]
        if let occurrence { payload["occurrence"] = .string(occurrence) }
        return Booking(json: try await request("booking.ownerCancel", payload)["booking"] ?? .null)
    }

    public func referralGet() async throws -> ReferralInfo {
        ReferralInfo(json: try await request("referral.get"))
    }

    public func permissionsList() async throws -> JSONValue {
        try await request("permissions.list")
    }

    public func permissionSet(tool: String, mode: String) async throws -> JSONValue {
        try await request("permissions.set", [
            "tool": .string(tool),
            "mode": .string(mode)
        ])
    }

    public func confirmationsList() async throws -> JSONValue {
        try await request("confirmation.list")
    }

    public func confirmationDecide(id: Int64, approve: Bool) async throws -> JSONValue {
        try await request(approve ? "confirmation.approve" : "confirmation.deny",
                          ["id": .from(id)])
    }

    // MARK: - Интеграции (этап 9)

    public func integrationsList() async throws -> JSONValue {
        try await request("integrations.list")
    }

    public func integrationBegin(provider: String, redirectUri: String) async throws -> JSONValue {
        try await request("integrations.begin", [
            "provider": .string(provider),
            "redirect_uri": .string(redirectUri)
        ])
    }

    public func integrationCallback(provider: String, code: String, state: String) async throws -> JSONValue {
        try await request("integrations.callback", [
            "provider": .string(provider),
            "code": .string(code),
            "state": .string(state)
        ])
    }

    public func integrationRevoke(id: Int64) async throws -> JSONValue {
        try await request("integrations.revoke", ["id": .from(id)])
    }

    public func integrationSync(id: Int64) async throws -> JSONValue {
        try await request("integrations.sync", ["id": .from(id)])
    }

    // MARK: - Уведомления и push-устройства (этап 13)

    public func notificationsList(unreadOnly: Bool = false, limit: Int = 50) async throws -> JSONValue {
        try await request("notifications.list", [
            "unread": .bool(unreadOnly),
            "limit": .from(Int64(limit))
        ])
    }

    /// id = 0 (или не передан) — отметить прочитанными все.
    public func notificationsRead(id: Int64 = 0) async throws -> JSONValue {
        try await request("notifications.read", ["id": .from(id)])
    }

    public func devicesPushRegister(platform: String, token: String) async throws -> JSONValue {
        try await request("devices.push.register", [
            "platform": .string(platform),
            "token": .string(token)
        ])
    }

    public func devicesPushList() async throws -> JSONValue {
        try await request("devices.push.list")
    }

    public func devicesPushRevoke(id: Int64) async throws -> JSONValue {
        try await request("devices.push.revoke", ["id": .from(id)])
    }

    // MARK: - Сервер

    public func serverInfo() async throws -> JSONValue {
        try await request("server.info")
    }
}
