// RootView.swift — каркас приложения: вход до авторизации, анкета после
// регистрации, затем разделы с плавающей стеклянной панелью вкладок
// «Сегодня · Запись · ИИ · Ещё» и отдельной стеклянной кнопкой «+» (дизайн 2026).
import SwiftUI

struct RootView: View {

    @EnvironmentObject private var store: AppStore

    /// Экраны v3 (вход, анкета, чат, переговоры…) рисуются тёмным стеклом;
    /// экраны 2026 следуют системной светлой/тёмной теме.
    private var legacy: Bool {
        store.authState != .loggedIn || store.needOnboarding || store.tab.usesLegacyTheme
    }

    var body: some View {
        ZStack {
            if legacy {
                SelfvoyBackground()
            } else {
                Ink.bg.ignoresSafeArea()
            }
            if store.authState == .loggedIn {
                if store.needOnboarding {
                    OnboardingView()
                } else {
                    MainShell()
                }
            } else {
                LoginView()
            }
        }
        .overlay(alignment: .top) { ToastView() }
        .preferredColorScheme(legacy ? .dark : nil)
        .tint(legacy ? SelfvoyColor.lilac : Ink.accent)
    }
}

/// Разделы: четыре во вкладках, остальные — из «Ещё» и шапки «Сегодня».
struct MainShell: View {

    @EnvironmentObject private var store: AppStore
    /// Пока открыта клавиатура, панель вкладок прячется и не висит над ней.
    @State private var keyboardShown = false

    var body: some View {
        Group {
            switch store.tab {
            case .home: TodayView()
            case .selfvoy: MySelfvoyView()
            case .negotiations: withBack { NegotiationsView() }
            case .tasks: withBack { TasksView() }
            case .contacts:
                if store.openChatId == 0 { withBack { ContactsView() } } else { ContactsView() }
            case .settings: SettingsView()
            case .confirmations: ConfirmationsView()
            case .notifications: NotificationsView()
            case .booking: BookingView()
            case .more: MoreView()
            case .packages: PackagesView()
            case .integrations: IntegrationsView()
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        // Панель плавает поверх контента; прокрутка уходит под неё (запас снизу — safeAreaInset).
        .safeAreaInset(edge: .bottom, spacing: 0) {
            if !keyboardShown {
                TabBar()
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillShowNotification)) { _ in
            keyboardShown = true
        }
        .onReceive(NotificationCenter.default.publisher(for: UIResponder.keyboardWillHideNotification)) { _ in
            keyboardShown = false
        }
        // Затемнение под статус-баром у экранов v3: без него прокручиваемый контент
        // шёл прямо под часами. У экранов 2026 фон сплошной — затемнение не нужно.
        .overlay(alignment: .top) {
            if store.tab.usesLegacyTheme {
                GeometryReader { geo in
                    LinearGradient(stops: [.init(color: SelfvoyColor.bgDeep.opacity(0.97), location: 0),
                                           .init(color: SelfvoyColor.bgDeep.opacity(0.9), location: 0.7),
                                           .init(color: SelfvoyColor.bgDeep.opacity(0), location: 1)],
                                   startPoint: .top, endPoint: .bottom)
                        .frame(height: geo.safeAreaInsets.top)
                        .ignoresSafeArea(edges: .top)
                        .allowsHitTesting(false)
                }
            }
        }
        .overlay(alignment: .bottom) {
            if store.voiceOverlayVisible {
                VoiceOverlay(speech: store.speech)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .animation(.easeOut(duration: 0.25), value: store.voiceOverlayVisible)
        .sheet(isPresented: $store.quickCreateVisible) {
            QuickCreateSheet()
                .inkSheet()
        }
    }

    /// Разделы v3 без своей строки «назад» (раньше были вкладками).
    private func withBack<Content: View>(@ViewBuilder _ content: () -> Content) -> some View {
        VStack(spacing: 0) {
            SubscreenBar()
            content()
        }
    }
}

/// Плавающая стеклянная панель вкладок и отдельная стеклянная «+».
private struct TabBar: View {

    @EnvironmentObject private var store: AppStore

    private struct Item {
        let tab: AppTab
        let title: String
        let symbol: String
    }

    private let items = [
        Item(tab: .home, title: "Сегодня", symbol: "calendar"),
        Item(tab: .booking, title: "Запись", symbol: "calendar.badge.clock"),
        Item(tab: .selfvoy, title: "ИИ", symbol: "sparkles"),
        Item(tab: .more, title: "Ещё", symbol: "ellipsis")
    ]

    /// Подсвеченная вкладка: вложенные разделы подсвечивают ту, из которой открыты.
    private var activeTab: AppTab { store.tab.isRoot ? store.tab : store.rootTab }

    private var inbox: Int { Int(store.unreadNotifications) + store.confirmations.count }

    var body: some View {
        HStack(spacing: 10) {
            HStack(spacing: 2) {
                ForEach(items, id: \.title) { item in
                    let active = activeTab == item.tab
                    Button {
                        store.selectTab(item.tab)
                    } label: {
                        VStack(spacing: 2) {
                            ZStack(alignment: .topTrailing) {
                                Image(systemName: item.symbol).font(.system(size: 18, weight: .medium))
                                    .frame(height: 22)
                                if item.tab == .more && inbox > 0 {
                                    Circle().fill(Ink.amber).frame(width: 8, height: 8).offset(x: 5, y: -1)
                                }
                            }
                            Text(item.title).font(.system(size: 10, weight: .semibold)).lineLimit(1)
                        }
                        .foregroundStyle(active ? Ink.accent : Ink.label)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 7)
                        .background {
                            if active { Capsule().fill(Ink.accentSoft) }
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(item.tab == .more && inbox > 0 ? "\(item.title), входящих: \(inbox)" : item.title)
                    .accessibilityIdentifier("tab.\(item.tab)")
                    .accessibilityAddTraits(active ? .isSelected : [])
                }
            }
            .padding(5)
            .floatingGlass(Capsule())

            Button {
                store.quickCreateVisible = true
            } label: {
                Image(systemName: "plus")
                    .font(.system(size: 24, weight: .semibold))
                    .foregroundStyle(Ink.label)
                    .frame(width: 58, height: 58)
                    .contentShape(Circle())
            }
            .buttonStyle(.plain)
            .floatingGlass(Circle())
            .accessibilityLabel("Создать")
            .accessibilityIdentifier("tab.create")
        }
        .padding(.horizontal, 14)
        .padding(.bottom, 4)
        .padding(.top, 6)
    }
}

/// «+»: быстрые действия владельца.
struct QuickCreateSheet: View {

    @EnvironmentObject private var store: AppStore

    private struct Action: Identifiable {
        let id: String
        let title: String
        let hint: String
        let symbol: String
        let tint: Color
        let run: (AppStore) -> Void
    }

    private let actions: [Action] = [
        Action(id: "page", title: "Новая страница записи", hint: "Ссылка, по которой клиенты записываются сами",
               symbol: "calendar.badge.plus", tint: Ink.accent) { store in
            store.bookingIntent = .newPage
            store.selectTab(.booking)
        },
        Action(id: "post", title: "Пост со свободными окнами", hint: "Готовый текст для Telegram или VK",
               symbol: "megaphone.fill", tint: Ink.amber) { store in
            store.bookingIntent = .post
            store.selectTab(.booking)
        },
        Action(id: "package", title: "Выдать абонемент", hint: "Пакет занятий клиенту — ссылка придёт письмом",
               symbol: "ticket.fill", tint: Ink.violet) { store in
            store.issuePackageRequested = true
            store.selectTab(.packages)
        },
        Action(id: "meet", title: "Договориться о встрече", hint: "Selfvoy согласует время с другим Selfvoy",
               symbol: "person.2.fill", tint: Ink.green) { store in
            store.selectTab(.negotiations)
        },
        Action(id: "task", title: "Новая задача", hint: "Напоминание или дело со сроком",
               symbol: "checkmark.square.fill", tint: Ink.teal) { store in
            store.selectTab(.tasks)
        },
        Action(id: "ask", title: "Спросить ассистента", hint: "Текстом или голосом",
               symbol: "sparkles", tint: Color(light: 0x0B0B0F, dark: 0x3A3A3C)) { store in
            store.selectTab(.selfvoy)
        }
    ]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                Text("Создать").font(InkFont.title).foregroundStyle(Ink.label)
                InkGroup {
                    ForEach(Array(actions.enumerated()), id: \.element.id) { index, action in
                        if index > 0 { InkDivider() }
                        Button {
                            store.quickCreateVisible = false
                            action.run(store)
                        } label: {
                            HStack(spacing: 12) {
                                Image(systemName: action.symbol)
                                    .font(.system(size: 16, weight: .semibold))
                                    .foregroundStyle(.white)
                                    .frame(width: 34, height: 34)
                                    .background(RoundedRectangle(cornerRadius: 9, style: .continuous).fill(action.tint))
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(action.title).font(InkFont.headline).foregroundStyle(Ink.label)
                                    Text(action.hint).font(InkFont.footnote).foregroundStyle(Ink.secondary)
                                }
                                Spacer(minLength: 0)
                            }
                            .padding(.horizontal, 16)
                            .padding(.vertical, 10)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            .padding(20)
        }
        .background(Ink.bg)
    }
}

/// Сообщения стора (ошибки и статусы) — плашка сверху, гаснет сама.
struct ToastView: View {

    @EnvironmentObject private var store: AppStore

    var body: some View {
        let error = store.errorMessage
        let text = error.isEmpty ? store.statusMessage : error
        Group {
            if !text.isEmpty {
                HStack(spacing: 10) {
                    Circle().fill(error.isEmpty ? SelfvoyColor.success : SelfvoyColor.danger).frame(width: 8, height: 8)
                    Text(text).font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.textPrimary)
                        .multilineTextAlignment(.leading)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
                .background(Capsule().fill(SelfvoyColor.bgElevated.opacity(0.96)))
                .overlay(Capsule().strokeBorder(.white.opacity(0.14), lineWidth: 1))
                .shadow(color: .black.opacity(0.4), radius: 16, y: 6)
                .padding(.horizontal, 20)
                .padding(.top, 6)
                .onTapGesture { clear() }
                .transition(.move(edge: .top).combined(with: .opacity))
                .task(id: text) {
                    try? await Task.sleep(nanoseconds: 3_500_000_000)
                    if !Task.isCancelled { clear() }
                }
            }
        }
        .animation(.easeOut(duration: 0.25), value: text)
    }

    private func clear() {
        store.errorMessage = ""
        store.statusMessage = ""
    }
}

/// Кнопка «назад» вложенных разделов: в вкладку, из которой их открыли.
struct BackToHomeButton: View {

    @EnvironmentObject private var store: AppStore

    var body: some View {
        RoundIconButton(systemImage: "chevron.left", label: "Назад", size: 40) {
            store.selectTab(store.rootTab)
        }
        .accessibilityIdentifier("nav.back")
    }
}

/// Строка «назад» вложенного раздела. Стоит над ScrollView, а не внутри него:
/// иначе после прокрутки длинного экрана (настройки) кнопка уезжала за верх
/// и вернуться можно было, только долистав обратно. Справа — действия раздела.
struct SubscreenBar<Trailing: View>: View {

    @ViewBuilder var trailing: () -> Trailing

    init(@ViewBuilder trailing: @escaping () -> Trailing = { EmptyView() }) {
        self.trailing = trailing
    }

    var body: some View {
        HStack(spacing: 10) {
            BackToHomeButton()
            Spacer(minLength: 8)
            trailing()
        }
        .padding(.horizontal, 20)
        .padding(.top, 12)
        .padding(.bottom, 6)
    }
}
