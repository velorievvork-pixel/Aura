// HomeView.swift — «Главная» (макет Home): приветствие, строка «Что нужно
// сделать?» с микрофоном (agent.ask → «Мой Selfvoy»), быстрые действия, карточка
// «Selfvoy активен», ждущие решения, ближайшие задачи и лента действий Selfvoy.
import SwiftUI

struct HomeView: View {

    @EnvironmentObject private var store: AppStore
    @State private var command = ""
    @FocusState private var commandFocused: Bool

    private var openTasks: [SelfvoyTask] {
        store.tasks.filter { $0.status == "pending" }
            .sorted { ($0.whenAt.isEmpty ? "9999" : $0.whenAt) < ($1.whenAt.isEmpty ? "9999" : $1.whenAt) }
    }

    private var doneToday: Int {
        store.tasks.filter { task in
            task.isDone && Format.parseISO(task.completedAt).map { Calendar.current.isDateInToday($0) } == true
        }.count
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 22) {
                header
                Text("\(Format.greeting()),\n\(Format.firstName(store.userName))")
                    .font(SelfvoyFont.display)
                    .foregroundStyle(SelfvoyColor.textPrimary)
                commandBar
                quickActions
                activeCard
                if !store.confirmations.isEmpty {
                    GlassButton(title: "Ждут вашего подтверждения: \(store.confirmations.count)",
                                systemImage: "exclamationmark.triangle", height: 52) {
                        store.selectTab(.confirmations)
                    }
                }
                if let n = store.negotiation {
                    GlassButton(title: negotiationLine(n), systemImage: "person.2.wave.2", height: 52) {
                        store.selectTab(.negotiations)
                    }
                }
                upcoming
                recent
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 16)
        }
        .scrollDismissesKeyboard(.interactively)
    }

    // MARK: Шапка: статус, подтверждения, уведомления, профиль → настройки

    private var header: some View {
        HStack(spacing: 6) {
            Circle().fill(SelfvoyColor.success).frame(width: 7, height: 7)
            Text(store.connected ? "Все системы работают" : "Нет связи с сервером")
                .font(SelfvoyFont.small)
                .foregroundStyle(SelfvoyColor.textSecondary)
            Spacer()
            HeaderIcon(systemImage: "exclamationmark.triangle", label: "Подтверждения",
                       count: store.confirmations.count, color: SelfvoyColor.warning) {
                store.selectTab(.confirmations)
            }
            .accessibilityIdentifier("home.confirmations")
            HeaderIcon(systemImage: "bell", label: "Уведомления",
                       count: Int(store.unreadNotifications), color: SelfvoyColor.accent) {
                store.selectTab(.notifications)
            }
            .accessibilityIdentifier("home.notifications")
            Button {
                store.selectTab(.settings)
            } label: {
                SelfvoyAvatar(name: store.userName, size: 36, online: store.connected)
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Настройки")
            .accessibilityIdentifier("home.settings")
            .padding(.leading, 4)
        }
    }

    // MARK: Строка команды

    private var commandBar: some View {
        HStack(spacing: 8) {
            ZStack(alignment: .leading) {
                if command.isEmpty {
                    Text("Что нужно сделать?").font(SelfvoyFont.manrope(17)).foregroundStyle(SelfvoyColor.textPrimary)
                }
                TextField("", text: $command)
                    .font(SelfvoyFont.manrope(17))
                    .foregroundStyle(SelfvoyColor.textPrimary)
                    .submitLabel(.send)
                    .focused($commandFocused)
                    .onSubmit { ask(command) }
            }
            let empty = command.trimmingCharacters(in: .whitespaces).isEmpty
            RoundIconButton(systemImage: empty ? "mic.fill" : "paperplane.fill",
                            label: empty ? "Сказать голосом" : "Отправить Selfvoy", accent: true, size: 48) {
                if empty {
                    store.voiceOverlayVisible = true
                } else {
                    ask(command)
                }
            }
        }
        .padding(.leading, 22)
        .padding(.trailing, 8)
        .frame(height: 64)
        .background(Capsule().fill(LinearGradient(colors: [.white.opacity(0.09), .white.opacity(0.03)],
                                                  startPoint: .top, endPoint: .bottom)))
        .overlay(Capsule().strokeBorder(.white.opacity(0.18), lineWidth: 1))
    }

    private func ask(_ text: String) {
        guard !text.trimmingCharacters(in: .whitespaces).isEmpty else { return }
        store.ask(text)
        command = ""
        commandFocused = false
        store.selectTab(.selfvoy)
    }

    // MARK: Быстрые действия 2×2

    private var quickActions: some View {
        let actions: [(String, () -> Void)] = [
            ("Назначить\nвстречу", { store.selectTab(.negotiations) }),
            ("Моё\nрасписание", { ask("Что у меня сегодня в расписании?") }),
            ("Написать\nписьмо", { command = "Напиши письмо "; commandFocused = true }),
            ("Создать\nнапоминание", { command = "Напомни мне "; commandFocused = true })
        ]
        return LazyVGrid(columns: [GridItem(.flexible(), spacing: 12), GridItem(.flexible(), spacing: 12)],
                         spacing: 12) {
            ForEach(actions.indices, id: \.self) { index in
                Button(action: actions[index].1) {
                    Text(actions[index].0)
                        .font(SelfvoyFont.labelMedium)
                        .foregroundStyle(SelfvoyColor.textPrimary)
                        .multilineTextAlignment(.leading)
                        .frame(maxWidth: .infinity, minHeight: 46, alignment: .leading)
                        .glassCard(radius: 16, padding: 10)
                }
                .buttonStyle(PressStyle())
            }
        }
    }

    // MARK: «Ваш Selfvoy активен»

    private var activeCard: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Ваш Selfvoy активен").font(SelfvoyFont.manrope(22, .semibold)).foregroundStyle(SelfvoyColor.textPrimary)
            Text("\(doneToday) \(Format.plural(doneToday, "задача выполнена", "задачи выполнены", "задач выполнено")) сегодня")
                .font(SelfvoyFont.small)
                .foregroundStyle(SelfvoyColor.textSecondary)
            HStack(spacing: 10) {
                RoundIconButton(systemImage: "person", label: "Контакты", size: 38) { store.selectTab(.contacts) }
                RoundIconButton(systemImage: "paperplane", label: "Мой Selfvoy", size: 38) { store.selectTab(.selfvoy) }
            }
            .padding(.top, 10)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard(radius: 26, glow: true, tint: Color(hex: 0xB4AAFF), fill: 0.2)
    }

    private func negotiationLine(_ n: Negotiation) -> String {
        let at = n.slot?.start ?? ""
        let place = n.place?.name ?? ""
        if !n.accepted { return "Переговоры: нужен ваш выбор" }
        if n.booking != nil { return "Забронировано: \(place), \(Format.dayPhrase(at)) \(Format.timeOf(at))" }
        return "Ждёт решения: \(place), \(Format.dayPhrase(at)) \(Format.timeOf(at))"
    }

    // MARK: Ближайшие задачи

    private var upcoming: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text("Ближайшие задачи").font(SelfvoyFont.titleMedium).foregroundStyle(SelfvoyColor.textPrimary)
                Spacer()
                Button("Все задачи") { store.selectTab(.tasks) }
                    .font(SelfvoyFont.small)
                    .foregroundStyle(SelfvoyColor.textSecondary)
            }
            if openTasks.isEmpty {
                Muted("Задач нет. Можно попросить Selfvoy что-нибудь запланировать.")
            }
            ForEach(Array(openTasks.prefix(4).enumerated()), id: \.element.id) { index, task in
                Button {
                    store.selectTab(.tasks)
                } label: {
                    HStack(spacing: 14) {
                        Image(systemName: ["person", "doc.text", "play.rectangle"][index % 3])
                            .font(.system(size: 14))
                            .foregroundStyle(SelfvoyColor.textSecondary)
                            .frame(width: 34, height: 34)
                            .overlay(Circle().strokeBorder(.white.opacity(0.18), lineWidth: 1))
                        Text(task.title)
                            .font(SelfvoyFont.titleSmall)
                            .foregroundStyle(SelfvoyColor.textPrimary)
                            .lineLimit(1)
                        Spacer(minLength: 8)
                        if let date = Format.parseISO(task.whenAt) {
                            Muted(Calendar.current.isDateInToday(date)
                                  ? Format.timeOf(task.whenAt)
                                  : "\(Format.dayPhrase(task.whenAt)), \(Format.timeOf(task.whenAt))")
                        }
                    }
                    .padding(.vertical, 9)
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
    }

    // MARK: Недавние действия Selfvoy

    private var recent: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Недавние действия Selfvoy").font(SelfvoyFont.titleMedium).foregroundStyle(SelfvoyColor.textPrimary)
            if store.notices.isEmpty {
                Muted("Здесь появятся напоминания, переговоры и подтверждения.")
            }
            ForEach(store.notices.prefix(6)) { notice in
                let detail = notice.kind == "login.new" ? "" : notice.body
                (Text(Format.timeOf(notice.createdAt) + "  ").foregroundColor(SelfvoyColor.textMuted)
                 + Text(notice.title + (detail.isEmpty ? "" : ": ")).foregroundColor(SelfvoyColor.textSecondary)
                 + Text(detail).foregroundColor(SelfvoyColor.textPrimary).bold())
                    .font(SelfvoyFont.small)
            }
        }
        .padding(.bottom, 8)
    }
}

/// Иконка шапки со счётчиком (подтверждения, уведомления).
private struct HeaderIcon: View {
    let systemImage: String
    let label: String
    let count: Int
    let color: Color
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            ZStack(alignment: .topTrailing) {
                Image(systemName: systemImage)
                    .font(.system(size: 19))
                    .foregroundStyle(SelfvoyColor.textSecondary)
                    .frame(width: 40, height: 40)
                if count > 0 {
                    Text("\(count)")
                        .font(SelfvoyFont.micro)
                        .foregroundStyle(.black)
                        .padding(.horizontal, 5)
                        .frame(minWidth: 17, minHeight: 17)
                        .background(Capsule().fill(color))
                        .offset(x: -2, y: 4)
                }
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(count > 0 ? "\(label): \(count)" : label)
    }
}
