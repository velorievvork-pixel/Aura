// SettingsView.swift — профиль (имя и «о себе», profile.update), что разрешено
// Ауре (allow / ask / deny по инструментам), приватность в переговорах
// (prefs.privacy), голос, безопасность (2FA), интеграции Google, Back Tap,
// адрес сервера и выход. Кнопки делают только то, что умеет сервер.
import AuraKit
import SwiftUI
import UIKit

struct SettingsView: View {

    @EnvironmentObject private var store: AppStore
    @State private var serverURL = ""
    @State private var totpCode = ""
    @State private var disablePassword = ""
    @State private var showQuickActions = false
    @State private var editingProfile = false
    @State private var nameDraft = ""
    @State private var bioDraft = ""

    var body: some View {
        VStack(spacing: 0) {
            SubscreenBar()
            content
        }
        .onAppear {
            serverURL = store.serverURLText
            store.loadPrivacy()
            if store.referral == nil { store.loadReferral() }
        }
        .sheet(isPresented: $showQuickActions) {
            NavigationStack { QuickActionsView() }
        }
    }

    private var content: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                ScreenHeader("Настройки")
                profile
                GlassButton(title: "Страницы записи", systemImage: "calendar.badge.plus", height: 44) {
                    store.selectTab(.booking)
                }
                section("Пригласить друзей") { referralSection }
                permissionsSection
                section("Приватность ИИ") { privacy }
                section("Голос") { voice }
                section("Безопасность") { security }
                section("Интеграции") { integrations }
                section("Сервер") {
                    VStack(alignment: .leading, spacing: 10) {
                        AuraField(text: $serverURL, placeholder: "ws:// или wss://", keyboard: .URL)
                        GlassButton(title: "Сохранить и переподключиться") { store.setServerURL(serverURL) }
                        Muted(store.connected ? "Соединение активно" : "Нет соединения")
                    }
                    .glassCard()
                }
                GlassButton(title: "Выйти", systemImage: "rectangle.portrait.and.arrow.right", danger: true, height: 50) {
                    store.logout()
                }
                .padding(.top, 8)
            }
            .padding(.horizontal, 20)
            .padding(.top, 6)
            .padding(.bottom, 20)
        }
        .scrollDismissesKeyboard(.interactively)
    }

    /// Секция с сиреневым заголовком.
    private func section<C: View>(_ title: String, @ViewBuilder content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            SectionTitle(text: title)
            content()
        }
        .padding(.top, 8)
    }

    /// Реферальный код и ссылка: друг заводит Ауру — засчитывается после подтверждения email.
    @ViewBuilder
    private var referralSection: some View {
        if let info = store.referral {
            VStack(alignment: .leading, spacing: 10) {
                Muted("Друг заведёт свою Ауру — и ваши Ауры смогут договариваться о встречах напрямую. Регистрации со страниц записи тоже засчитываются вам.")
                HStack {
                    Muted("Ваш код")
                    Spacer()
                    Text(info.code).font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary).textSelection(.enabled)
                }
                Text(info.link).font(AuraFont.small).foregroundStyle(AuraColor.textPrimary).textSelection(.enabled)
                HStack(spacing: 8) {
                    GlassButton(title: "Копировать", systemImage: "doc.on.doc", height: 40) {
                        UIPasteboard.general.string = info.link
                        store.setStatus("Ссылка скопирована")
                    }
                    ShareLink(item: info.link, message: Text("Заведи свою Ауру — личного ИИ-агента")) {
                        Label("Поделиться", systemImage: "square.and.arrow.up")
                            .font(AuraFont.small)
                            .frame(maxWidth: .infinity, minHeight: 40)
                    }
                    .foregroundStyle(AuraColor.textPrimary)
                }
                Muted("Зарегистрировались: \(info.joined) · подтвердили email: \(info.verified)")
                ForEach(info.referrals, id: \.self) { item in
                    HStack {
                        Text(item.displayName.isEmpty ? "Без имени" : item.displayName)
                            .font(AuraFont.small).foregroundStyle(AuraColor.textPrimary)
                        Spacer()
                        Muted(item.verified ? "С Aura" : "Ждёт подтверждения")
                    }
                }
            }
            .glassCard(padding: 16)
        } else {
            Muted("Загружаем реферальный код…")
        }
    }

    private var permissionsSection: some View {
        VStack(alignment: .leading, spacing: 14) {
            SectionTitle(text: "Что может ваша Аура")
            Muted("Аура только формирует намерение, исполняет сервер. Опасные действия по умолчанию ждут вашего подтверждения.")
            ForEach(store.permissions) { row in
                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 8) {
                        Text(Format.toolLabel(row.tool)).font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
                        if row.dangerous {
                            Text("опасный")
                                .font(AuraFont.micro)
                                .foregroundStyle(AuraColor.warning)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 2)
                                .overlay(Capsule().strokeBorder(AuraColor.warning.opacity(0.5), lineWidth: 1))
                        }
                    }
                    Muted(Format.toolDescription(row.tool, fallback: row.description))
                    ModePicker(mode: row.mode) { store.setPermission(tool: row.tool, mode: $0) }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .glassCard(padding: 16)
            }
        }
        .padding(.top, 8)
    }

    private var profile: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(spacing: 14) {
                AuraAvatar(name: store.userName, size: 52, online: store.connected)
                VStack(alignment: .leading, spacing: 2) {
                    Text(store.userName).font(AuraFont.manrope(20, .bold)).foregroundStyle(AuraColor.textPrimary)
                    Text(store.userEmail).font(AuraFont.small).foregroundStyle(AuraColor.textSecondary)
                }
                Spacer(minLength: 0)
                if !editingProfile {
                    RoundIconButton(systemImage: "pencil", label: "Изменить профиль", size: 38) { startEditing() }
                }
            }
            if editingProfile {
                profileEditor
            } else if !store.userBio.isEmpty {
                Text(store.userBio).font(AuraFont.body).foregroundStyle(AuraColor.textSecondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard()
    }

    /// Имя 1–80 символов, «о себе» до 300; ошибка сервера — под полями.
    private var profileEditor: some View {
        let nameCount = nameDraft.trimmingCharacters(in: .whitespacesAndNewlines).count
        let bioCount = bioDraft.trimmingCharacters(in: .whitespacesAndNewlines).count
        let problem = AuraProfileRules.validate(displayName: nameDraft, bio: bioDraft)
        let message = problem ?? store.profileError
        return VStack(alignment: .leading, spacing: 10) {
            AuraField(text: $nameDraft, placeholder: "Как к вам обращаться", label: "Имя")
            Muted("\(nameCount)/\(AuraProfileRules.displayNameMax)")
            AuraField(text: $bioDraft, placeholder: "Пара слов о себе", label: "О себе")
            Muted("\(bioCount)/\(AuraProfileRules.bioMax)")
            if !message.isEmpty {
                Text(message).font(AuraFont.small).foregroundStyle(AuraColor.danger)
            }
            HStack(spacing: 10) {
                GlassButton(title: "Отмена", quiet: true, height: 44) {
                    editingProfile = false
                    store.profileError = ""
                }
                GradientButton(title: "Сохранить", enabled: problem == nil, busy: store.savingProfile, height: 44) {
                    store.updateProfile(displayName: nameDraft, bio: bioDraft) { editingProfile = false }
                }
            }
        }
    }

    private func startEditing() {
        nameDraft = store.userName
        bioDraft = store.userBio
        store.profileError = ""
        editingProfile = true
    }

    /// Три флага prefs.privacy; переключение сохраняется сразу, при ошибке откатывается.
    private var privacy: some View {
        VStack(alignment: .leading, spacing: 14) {
            Muted("Что ваша Аура раскрывает чужой Ауре, когда договаривается о встрече.")
            privacyToggle(.shareFreeSlotsOnly, title: "Делиться только свободными слотами",
                          hint: "Другой Ауре уходят только занятость и настройки, нужные для времени и места. Факты из памяти, день рождения и аллергии не передаются.")
            privacyToggle(.allowPlaceChoice, title: "Разрешить выбор локаций",
                          hint: "Аура предлагает место встречи. Если выключено у вас или у собеседника, место не предлагается — выберете сами.")
            privacyToggle(.hideEventTitles, title: "Скрывать названия других событий",
                          hint: "Названия событий вашего календаря заменяются на «занято».")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard()
    }

    private func privacyToggle(_ flag: AuraPrivacy.Flag, title: String, hint: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Toggle(isOn: Binding(get: { store.privacy[flag] }, set: { store.setPrivacy(flag, $0) })) {
                Text(title).font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
            }
            .tint(AuraColor.accent)
            Muted(hint)
        }
    }

    private var voice: some View {
        VStack(alignment: .leading, spacing: 12) {
            Toggle(isOn: $store.ttsEnabled) {
                Text("Озвучивать ответы Ауры").font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
            }
            .tint(AuraColor.accent)
            HStack(spacing: 10) {
                GlassButton(title: "Проверить голос", systemImage: "mic") { store.voiceOverlayVisible = true }
                GlassButton(title: "Тишина", systemImage: "speaker.slash", quiet: true) { store.speech.stopSpeaking() }
            }
            GlassButton(title: "Back Tap и Команды", systemImage: "hand.tap", quiet: true) { showQuickActions = true }
        }
        .glassCard()
    }

    private var security: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Двухфакторная аутентификация").font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
            Text(store.twoFaEnabled ? "Включена" : "Выключена")
                .font(AuraFont.small)
                .foregroundStyle(store.twoFaEnabled ? AuraColor.accentSoft : AuraColor.textSecondary)
            if !store.twoFaEnabled && store.twoFaSecret.isEmpty {
                GradientButton(title: "Включить 2FA", height: 44) { store.setup2fa() }
            }
            if !store.twoFaSecret.isEmpty {
                Muted("Добавьте секрет в приложение-аутентификатор:")
                Text(store.twoFaSecret)
                    .font(AuraFont.small)
                    .foregroundStyle(AuraColor.accentSoft)
                    .textSelection(.enabled)
                AuraField(text: $totpCode, placeholder: "Код из приложения", keyboard: .numberPad)
                GradientButton(title: "Подтвердить и включить", height: 44) {
                    store.confirm2fa(code: totpCode)
                    totpCode = ""
                }
            }
            if !store.recoveryCodes.isEmpty {
                Muted("Резервные коды (сохраните, они показываются один раз):")
                ForEach(store.recoveryCodes, id: \.self) { code in
                    Text(code).font(AuraFont.labelMedium).foregroundStyle(AuraColor.accentSoft).textSelection(.enabled)
                }
            }
            if store.twoFaEnabled {
                AuraField(text: $disablePassword, placeholder: "Пароль", secure: true)
                GlassButton(title: "Отключить 2FA", danger: true) {
                    store.disable2fa(password: disablePassword)
                    disablePassword = ""
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard()
    }

    private var integrations: some View {
        VStack(alignment: .leading, spacing: 12) {
            Muted("Google Календарь и Gmail через официальный OAuth. Токены хранятся только на сервере, зашифрованными.")
            if store.providers.isEmpty {
                Muted("Сервер не предлагает интеграций.")
            }
            ForEach(store.providers) { provider in
                let connection = store.connections.first { $0.provider == provider.provider }
                VStack(alignment: .leading, spacing: 6) {
                    Text(provider.name).font(AuraFont.titleSmall).foregroundStyle(AuraColor.textPrimary)
                    if !provider.description.isEmpty { Muted(provider.description) }
                    if let connection {
                        Text(connection.status == "active"
                             ? "подключено\(connection.account.isEmpty ? "" : " · \(connection.account)")"
                             : connection.status)
                            .font(AuraFont.small)
                            .foregroundStyle(connection.status == "active" ? AuraColor.success : AuraColor.warning)
                        HStack(spacing: 10) {
                            GlassButton(title: "Синхронизировать", height: 38) { store.syncIntegration(id: connection.id) }
                            GlassButton(title: "Отключить", danger: true, height: 38) {
                                store.revokeIntegration(id: connection.id)
                            }
                        }
                    } else {
                        GlassButton(title: "Подключить", systemImage: "link", height: 40) {
                            store.beginIntegration(provider: provider.provider)
                        }
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard()
    }
}

/// Три режима инструмента: сама / спрашивать / запретить.
private struct ModePicker: View {
    let mode: String
    let onSelect: (String) -> Void

    private let modes = [("allow", "Сама"), ("ask", "Спрашивать"), ("deny", "Запретить")]

    var body: some View {
        HStack(spacing: 0) {
            ForEach(modes.indices, id: \.self) { index in
                let value = modes[index].0
                let active = mode == value
                Button {
                    if !active { onSelect(value) }
                } label: {
                    Text(modes[index].1)
                        .font(AuraFont.labelMedium)
                        .foregroundStyle(active ? AuraColor.textPrimary : AuraColor.textSecondary)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 8)
                        .background(Capsule().fill(active
                            ? (value == "deny" ? AuraColor.danger.opacity(0.3) : AuraColor.accent.opacity(0.35))
                            : Color.clear))
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(3)
        .background(Capsule().fill(AuraColor.well.opacity(0.45)))
        .overlay(Capsule().strokeBorder(.white.opacity(0.14), lineWidth: 1))
    }
}
