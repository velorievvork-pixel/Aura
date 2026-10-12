// RootView.swift — каркас приложения v3: вход до авторизации, анкета после
// регистрации, затем разделы с нижней стеклянной панелью вкладок.
import SwiftUI

struct RootView: View {

    @EnvironmentObject private var store: AppStore

    var body: some View {
        ZStack {
            AuraBackground()
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
        .preferredColorScheme(.dark)
        .tint(AuraColor.lilac)
    }
}

/// Пять разделов во вкладках; настройки, подтверждения и уведомления
/// открываются из шапки «Главной» и закрываются кнопкой «назад».
struct MainShell: View {

    @EnvironmentObject private var store: AppStore
    /// Пока открыта клавиатура, панель вкладок прячется и не висит над ней.
    @State private var keyboardShown = false

    var body: some View {
        VStack(spacing: 0) {
            Group {
                switch store.tab {
                case .home: HomeView()
                case .aura: MyAuraView()
                case .negotiations: NegotiationsView()
                case .tasks: TasksView()
                case .contacts: ContactsView()
                case .settings: SettingsView()
                case .confirmations: ConfirmationsView()
                case .notifications: NotificationsView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
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
        // Затемнение под статус-баром: без него прокручиваемый контент шёл прямо
        // под часами и индикаторами.
        .overlay(alignment: .top) {
            GeometryReader { geo in
                LinearGradient(colors: [AuraColor.bgDeep.opacity(0.92), AuraColor.bgDeep.opacity(0)],
                               startPoint: .top, endPoint: .bottom)
                    .frame(height: geo.safeAreaInsets.top + 12)
                    .ignoresSafeArea(edges: .top)
                    .allowsHitTesting(false)
            }
        }
        .overlay(alignment: .bottom) {
            if store.voiceOverlayVisible {
                VoiceOverlay(speech: store.speech)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .animation(.easeOut(duration: 0.25), value: store.voiceOverlayVisible)
    }
}

private struct TabBar: View {

    @EnvironmentObject private var store: AppStore

    private struct Item {
        let tab: AppTab
        let title: String
        let symbol: String
    }

    private let items = [
        Item(tab: .home, title: "Главная", symbol: "house"),
        Item(tab: .aura, title: "Аура", symbol: "bubble.left.and.bubble.right"),
        Item(tab: .negotiations, title: "Переговоры", symbol: "person.2.wave.2"),
        Item(tab: .tasks, title: "Задачи", symbol: "checkmark.square"),
        Item(tab: .contacts, title: "Контакты", symbol: "person.2")
    ]

    var body: some View {
        HStack(spacing: 4) {
            ForEach(items, id: \.title) { item in
                let active = store.tab == item.tab
                Button {
                    store.selectTab(item.tab)
                } label: {
                    VStack(spacing: 3) {
                        Image(systemName: item.symbol).font(.system(size: 19, weight: .regular))
                        Text(item.title).font(AuraFont.micro).lineLimit(1).minimumScaleFactor(0.8)
                    }
                    .foregroundStyle(active ? AuraColor.textPrimary : AuraColor.textMuted)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 7)
                    .background(RoundedRectangle(cornerRadius: 14, style: .continuous)
                        .fill(active ? AuraColor.accent.opacity(0.18) : .clear))
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(active ? .isSelected : [])
            }
        }
        .padding(.horizontal, 6)
        .padding(.top, 8)
        .padding(.bottom, 4)
        .background(
            UnevenRoundedRectangle(topLeadingRadius: 24, topTrailingRadius: 24, style: .continuous)
                .fill(Color(hex: 0x100D18))
                .overlay(UnevenRoundedRectangle(topLeadingRadius: 24, topTrailingRadius: 24, style: .continuous)
                    .stroke(.white.opacity(0.08), lineWidth: 1))
                .ignoresSafeArea(edges: .bottom)
        )
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
                    Circle().fill(error.isEmpty ? AuraColor.success : AuraColor.danger).frame(width: 8, height: 8)
                    Text(text).font(AuraFont.small).foregroundStyle(AuraColor.textPrimary)
                        .multilineTextAlignment(.leading)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
                .background(Capsule().fill(AuraColor.bgElevated.opacity(0.96)))
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

/// Кнопка «назад» вложенных разделов (настройки, подтверждения, уведомления).
struct BackToHomeButton: View {

    @EnvironmentObject private var store: AppStore

    var body: some View {
        RoundIconButton(systemImage: "chevron.left", label: "Назад", size: 40) {
            store.selectTab(.home)
        }
    }
}
