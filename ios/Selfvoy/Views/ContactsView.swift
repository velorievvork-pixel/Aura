// ContactsView.swift — контакты (макет Contacts): люди из чатов, поиск новых
// (users.search → chat.open), карточка человека с «Начать переговоры» /
// «Открыть чат» и сама переписка.
import SwiftUI

struct ContactsView: View {

    @EnvironmentObject private var store: AppStore
    @State private var query = ""
    @State private var recentOnly = false
    @State private var selected: Contact?
    @FocusState private var searchFocused: Bool

    var body: some View {
        if store.openChatId != 0 {
            ChatView(chatId: store.openChatId,
                     title: store.contacts.first { $0.chatId == store.openChatId }?.name
                        ?? store.chats.first { $0.id == store.openChatId }?.title ?? "")
        } else if let contact = selected {
            ContactCard(contact: contact) { selected = nil }
        } else {
            list
        }
    }

    private var list: some View {
        let visible = store.contacts.filter { contact in
            (!recentOnly || !contact.lastAt.isEmpty)
                && (query.isEmpty || contact.name.localizedCaseInsensitiveContains(query)
                    || contact.email.localizedCaseInsensitiveContains(query))
        }
        let newPeople = store.foundUsers.filter { found in !store.contacts.contains { $0.userId == found.id } }

        return ScrollView {
            VStack(alignment: .leading, spacing: 6) {
                ScreenHeader("Контакты")
                VStack(spacing: 12) {
                    HStack(spacing: 10) {
                        Image(systemName: "magnifyingglass").foregroundStyle(SelfvoyColor.textSecondary)
                        ZStack(alignment: .leading) {
                            if query.isEmpty {
                                Text("Искать людей или Selfvoy").font(SelfvoyFont.manrope(17)).foregroundStyle(SelfvoyColor.textMuted)
                            }
                            TextField("", text: $query)
                                .font(SelfvoyFont.manrope(17))
                                .foregroundStyle(SelfvoyColor.textPrimary)
                                .textInputAutocapitalization(.never)
                                .autocorrectionDisabled()
                                .focused($searchFocused)
                        }
                    }
                    Rectangle().fill(.white.opacity(0.18)).frame(height: 1)
                }
                .padding(.top, 10)
                HStack(spacing: 10) {
                    SelfvoyChip(title: "Все", active: !recentOnly) { recentOnly = false }
                    SelfvoyChip(title: "Недавние", active: recentOnly) { recentOnly = true }
                }
                .padding(.vertical, 12)
                if visible.isEmpty && newPeople.isEmpty {
                    Muted(query.isEmpty ? "Пока нет собеседников. Найдите человека по имени или email."
                                        : "Никого не нашли")
                }
                ForEach(visible) { contact in
                    Button {
                        selected = contact
                    } label: {
                        HStack(spacing: 14) {
                            SelfvoyAvatar(name: contact.name, size: 46)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(contact.name).font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
                                Text(contact.lastBody.isEmpty ? "Selfvoy доступен" : contact.lastBody)
                                    .font(SelfvoyFont.small)
                                    .foregroundStyle(SelfvoyColor.textSecondary)
                                    .lineLimit(1)
                            }
                            Spacer()
                            Circle().fill(SelfvoyColor.accentSoft).frame(width: 10, height: 10)
                        }
                        .padding(.vertical, 10)
                        .padding(.horizontal, 6)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                }
                if !newPeople.isEmpty {
                    Text("Найдено в Selfvoy").font(SelfvoyFont.labelMedium).foregroundStyle(SelfvoyColor.textSecondary)
                        .padding(.top, 12)
                    ForEach(newPeople) { user in
                        HStack(spacing: 12) {
                            SelfvoyAvatar(name: user.name, size: 38)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(user.name).font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
                                Muted(user.online ? "в сети" : "есть Selfvoy")
                            }
                            Spacer()
                            GlassButton(title: "Добавить", systemImage: "person.badge.plus", height: 36,
                                        fullWidth: false) {
                                // chat.open принимает id пользователя строкой.
                                store.openChat(contact: String(user.id))
                                query = ""
                            }
                        }
                        .padding(.vertical, 8)
                    }
                }
                GlassButton(title: "Пригласить в Selfvoy", systemImage: "person.badge.plus", height: 48,
                            fullWidth: false) {
                    searchFocused = true
                }
                .padding(.top, 20)
                Muted("Найдите человека по email: если у него есть Selfvoy, он появится в списке.")
                    .padding(.top, 8)
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 16)
        }
        .scrollDismissesKeyboard(.interactively)
        .task(id: query) {
            try? await Task.sleep(nanoseconds: 300_000_000)
            if !Task.isCancelled { store.searchUsers(query) }
        }
    }
}

/// Карточка человека (макет Contacts, правая часть).
private struct ContactCard: View {

    @EnvironmentObject private var store: AppStore
    let contact: Contact
    let onBack: () -> Void

    private func mode(_ tool: String) -> String {
        switch store.permissions.first(where: { $0.tool == tool })?.mode {
        case "allow": return "сам"
        case "ask": return "с вашего подтверждения"
        case "deny": return "запрещено"
        default: return "—"
        }
    }

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: 30, style: .continuous)
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                RoundIconButton(systemImage: "chevron.left", label: "Назад", size: 40, action: onBack)
                    .accessibilityIdentifier("nav.back")
                VStack(alignment: .leading, spacing: 20) {
                    VStack(spacing: 6) {
                        SelfvoyAvatar(name: contact.name, size: 96, ring: true).padding(.bottom, 6)
                        Text(contact.name).font(SelfvoyFont.title).foregroundStyle(SelfvoyColor.textPrimary)
                        Text("Selfvoy доступен · \(contact.email)").font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.textSecondary)
                    }
                    .frame(maxWidth: .infinity)
                    InfoBlock(systemImage: "calendar", title: "Последнее в переписке",
                              lines: contact.lastBody.isEmpty ? ["Пока ничего"]
                                : ["«\(contact.lastBody)»",
                                   "\(Format.dayPhrase(contact.lastAt)), \(Format.timeOf(contact.lastAt))"])
                    InfoBlock(systemImage: "lock", title: "Что может ваш Selfvoy",
                              lines: ["Писать: \(mode("send_message"))", "Бронировать: \(mode("book_table"))",
                                      "Делиться: только свободными окнами"])
                    GlassButton(title: "Начать переговоры", systemImage: "person.2.wave.2", enabled: !store.busy,
                                height: 50) {
                        store.negotiate(target: contact.email, topic: "", durationMinutes: 60,
                                        windowHours: Format.windowHours("weekend"))
                    }
                    GlassButton(title: "Открыть чат", systemImage: "bubble.left", quiet: true) {
                        store.openChat(id: contact.chatId)
                    }
                }
                .padding(24)
                .background(shape.fill(LinearGradient(colors: [Color(hex: 0x6E6E78, alpha: 0.55),
                                                               Color(hex: 0x3D3B45, alpha: 0.6)],
                                                      startPoint: .top, endPoint: .bottom)))
                .overlay(shape.strokeBorder(.white.opacity(0.28), lineWidth: 1))
            }
            .padding(20)
        }
    }
}

private struct InfoBlock: View {
    let systemImage: String
    let title: String
    let lines: [String]

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 10) {
                Image(systemName: systemImage)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(.white)
                    .frame(width: 28, height: 28)
                    .background(Circle().fill(SelfvoyColor.accent))
                Text(title).font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
            }
            ForEach(lines, id: \.self) { line in
                Text(line).font(SelfvoyFont.labelMedium).foregroundStyle(SelfvoyColor.textPrimary).padding(.leading, 38)
            }
        }
    }
}

/// Переписка с человеком; режим «Спросить Selfvoy» отправляет agent.ask в этот чат.
private struct ChatView: View {

    @EnvironmentObject private var store: AppStore
    let chatId: Int64
    let title: String
    @State private var draft = ""
    @State private var toSelfvoy = false

    init(chatId: Int64, title: String) {
        self.chatId = chatId
        self.title = title
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                RoundIconButton(systemImage: "chevron.left", label: "Назад", size: 38) { store.closeChat() }
                    .accessibilityIdentifier("nav.back")
                SelfvoyAvatar(name: title, size: 40)
                Text(title).font(SelfvoyFont.titleMedium).foregroundStyle(SelfvoyColor.textPrimary).lineLimit(1)
                Spacer()
            }
            .padding(16)
            Rectangle().fill(.white.opacity(0.08)).frame(height: 1)
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(spacing: 12) {
                        if store.messages.isEmpty {
                            Muted("Сообщений пока нет. Напишите или попросите Selfvoy договориться о встрече.")
                        }
                        ForEach(store.messages) { message in
                            let mine = message.senderId == store.userId
                            let agent = message.kind.hasPrefix("agent")
                            VStack(alignment: mine ? .trailing : .leading, spacing: 4) {
                                if agent {
                                    Text("Selfvoy").font(SelfvoyFont.micro).foregroundStyle(SelfvoyColor.accentSoft)
                                }
                                Bubble(fromSelfvoy: mine || agent, text: message.body)
                                Muted("\(message.senderName.isEmpty ? (mine ? "Вы" : title) : message.senderName) · \(Format.timeOf(message.createdAt))")
                            }
                            .frame(maxWidth: .infinity, alignment: mine ? .trailing : .leading)
                            .id(message.id)
                        }
                    }
                    .padding(16)
                }
                .scrollDismissesKeyboard(.interactively)
                .onChange(of: store.messages.count) {
                    if let last = store.messages.last {
                        withAnimation { proxy.scrollTo(last.id, anchor: .bottom) }
                    }
                }
            }
            HStack(spacing: 8) {
                SelfvoyChip(title: "Обычное", active: !toSelfvoy) { toSelfvoy = false }
                SelfvoyChip(title: "Спросить Selfvoy", active: toSelfvoy) { toSelfvoy = true }
                Spacer()
            }
            .padding(.horizontal, 16)
            HStack(spacing: 10) {
                SelfvoyField(text: $draft, placeholder: toSelfvoy ? "Например: договорись о встрече" : "Сообщение…",
                          submitLabel: .send, onSubmit: send)
                RoundIconButton(systemImage: toSelfvoy ? "sparkles" : "paperplane.fill", label: "Отправить",
                                accent: true, size: 48,
                                enabled: !draft.trimmingCharacters(in: .whitespaces).isEmpty && !store.busy,
                                action: send)
            }
            .padding(16)
        }
    }

    private func send() {
        guard !draft.trimmingCharacters(in: .whitespaces).isEmpty, !store.busy else { return }
        if toSelfvoy {
            store.ask(draft, chatId: chatId)
        } else {
            store.sendMessage(draft)
        }
        draft = ""
    }
}
