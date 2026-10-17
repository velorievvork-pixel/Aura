// SettingsView.swift — профиль (имя и «о себе», profile.update), что разрешено
// Selfvoy (allow / ask / deny по инструментам), приватность в переговорах
// (prefs.privacy), голос, безопасность (2FA), интеграции Google, Back Tap,
// адрес сервера и выход. Кнопки делают только то, что умеет сервер.
import SelfvoyKit
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
                GlassButton(title: "Telegram, ЮKassa и календарь", systemImage: "link", height: 44) {
                    store.selectTab(.integrations)
                }
                section("Пригласить друзей") { referralSection }
                permissionsSection
                section("Приватность ИИ") { privacy }
                section("Голос") { voice }
                section("Безопасность") { security }
                section("Интеграции") { integrations }
                section("Сервер") {
                    VStack(alignment: .leading, spacing: 10) {
                        SelfvoyField(text: $serverURL, placeholder: "ws:// или wss://", keyboard: .URL)
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

    /// Реферальный код и ссылка: друг заводит Selfvoy — засчитывается после подтверждения email.
    @ViewBuilder
    private var referralSection: some View {
        if let info = store.referral {
            VStack(alignment: .leading, spacing: 10) {
                Muted("Друг заведёт свой Selfvoy — и ваши Selfvoy смогут договариваться о встречах напрямую. Регистрации со страниц записи тоже засчитываются вам.")
                Text(info.rewardText).font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.warning)
                HStack {
                    Muted("Ваш код")
                    Spacer()
                    Text(info.code).font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary).textSelection(.enabled)
                }
                if info.hasLink {
                    Text(info.link).font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.textPrimary).textSelection(.enabled)
                } else {
                    Muted("Сервер не знает свой публичный адрес (SELFVOY_PUBLIC_URL) — делимся только кодом: друг введёт его при регистрации.")
                }
                HStack(spacing: 8) {
                    GlassButton(title: "Копировать", systemImage: "doc.on.doc", height: 40) {
                        UIPasteboard.general.string = info.link
                        store.setStatus(info.hasLink ? "Ссылка скопирована" : "Код скопирован")
                    }
                    ShareLink(item: info.shareText) {
                        Label("Поделиться", systemImage: "square.and.arrow.up")
                            .font(SelfvoyFont.small)
                            .frame(maxWidth: .infinity, minHeight: 40)
                    }
                    .foregroundStyle(SelfvoyColor.textPrimary)
                }
                Muted("Зарегистрировались: \(info.joined) · подтвердили email: \(info.verified)")
                ForEach(info.referrals, id: \.self) { item in
                    HStack {
                        Text(item.displayName.isEmpty ? "Без имени" : item.displayName)
                            .font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.textPrimary)
                        Spacer()
                        Muted(item.verified ? "С Selfvoy" : "Ждёт подтверждения")
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
            SectionTitle(text: "Что может ваш Selfvoy")
            Muted("Selfvoy только формирует намерение, исполняет сервер. Опасные действия по умолчанию ждут вашего подтверждения.")
            ForEach(store.permissions) { row in
                VStack(alignment: .leading, spacing: 10) {
                    HStack(spacing: 8) {
                        Text(Format.toolLabel(row.tool)).font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
                        if row.dangerous {
                            Text("опасный")
                                .font(SelfvoyFont.micro)
                                .foregroundStyle(SelfvoyColor.warning)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 2)
                                .overlay(Capsule().strokeBorder(SelfvoyColor.warning.opacity(0.5), lineWidth: 1))
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
                SelfvoyAvatar(name: store.userName, size: 52, online: store.connected)
                VStack(alignment: .leading, spacing: 2) {
                    Text(store.userName).font(SelfvoyFont.manrope(20, .bold)).foregroundStyle(SelfvoyColor.textPrimary)
                    Text(store.userEmail).font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.textSecondary)
                    if let plan = store.userPlan {
                        HStack(spacing: 6) {
                            PlanBadge(plan: plan)
                            Text(plan.hint()).font(SelfvoyFont.micro).foregroundStyle(SelfvoyColor.textMuted)
                        }
                        .padding(.top, 2)
                    }
                }
                Spacer(minLength: 0)
                if !editingProfile {
                    RoundIconButton(systemImage: "pencil", label: "Изменить профиль", size: 38) { startEditing() }
                }
            }
            if editingProfile {
                profileEditor
            } else if !store.userBio.isEmpty {
                Text(store.userBio).font(SelfvoyFont.body).foregroundStyle(SelfvoyColor.textSecondary)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard()
    }

    /// Имя 1–80 символов, «о себе» до 300; ошибка сервера — под полями.
    private var profileEditor: some View {
        let nameCount = nameDraft.trimmingCharacters(in: .whitespacesAndNewlines).count
        let bioCount = bioDraft.trimmingCharacters(in: .whitespacesAndNewlines).count
        let problem = SelfvoyProfileRules.validate(displayName: nameDraft, bio: bioDraft)
        let message = problem ?? store.profileError
        return VStack(alignment: .leading, spacing: 10) {
            SelfvoyField(text: $nameDraft, placeholder: "Как к вам обращаться", label: "Имя")
            Muted("\(nameCount)/\(SelfvoyProfileRules.displayNameMax)")
            SelfvoyField(text: $bioDraft, placeholder: "Пара слов о себе", label: "О себе")
            Muted("\(bioCount)/\(SelfvoyProfileRules.bioMax)")
            if !message.isEmpty {
                Text(message).font(SelfvoyFont.small).foregroundStyle(SelfvoyColor.danger)
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
            Muted("Что ваш Selfvoy раскрывает чужому Selfvoy, когда договаривается о встрече.")
            privacyToggle(.shareFreeSlotsOnly, title: "Делиться только свободными слотами",
                          hint: "Другому Selfvoy уходят только занятость и настройки, нужные для времени и места. Факты из памяти, день рождения и аллергии не передаются.")
            privacyToggle(.allowPlaceChoice, title: "Разрешить выбор локаций",
                          hint: "Selfvoy предлагает место встречи. Если выключено у вас или у собеседника, место не предлагается — выберете сами.")
            privacyToggle(.hideEventTitles, title: "Скрывать названия других событий",
                          hint: "Названия событий вашего календаря заменяются на «занято».")
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .glassCard()
    }

    private func privacyToggle(_ flag: SelfvoyPrivacy.Flag, title: String, hint: String) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Toggle(isOn: Binding(get: { store.privacy[flag] }, set: { store.setPrivacy(flag, $0) })) {
                Text(title).font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
            }
            .tint(SelfvoyColor.accent)
            Muted(hint)
        }
    }

    private var voice: some View {
        VStack(alignment: .leading, spacing: 12) {
            Toggle(isOn: $store.ttsEnabled) {
                Text("Озвучивать ответы Selfvoy").font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
            }
            .tint(SelfvoyColor.accent)
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
            Text("Двухфакторная аутентификация").font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
            Text(store.twoFaEnabled ? "Включена" : "Выключена")
                .font(SelfvoyFont.small)
                .foregroundStyle(store.twoFaEnabled ? SelfvoyColor.accentSoft : SelfvoyColor.textSecondary)
            if !store.twoFaEnabled && store.twoFaSecret.isEmpty {
                GradientButton(title: "Включить 2FA", height: 44) { store.setup2fa() }
            }
            if !store.twoFaSecret.isEmpty {
                Muted("Добавьте секрет в приложение-аутентификатор:")
                Text(store.twoFaSecret)
                    .font(SelfvoyFont.small)
                    .foregroundStyle(SelfvoyColor.accentSoft)
                    .textSelection(.enabled)
                SelfvoyField(text: $totpCode, placeholder: "Код из приложения", keyboard: .numberPad)
                GradientButton(title: "Подтвердить и включить", height: 44) {
                    store.confirm2fa(code: totpCode)
                    totpCode = ""
                }
            }
            if !store.recoveryCodes.isEmpty {
                Muted("Резервные коды (сохраните, они показываются один раз):")
                ForEach(store.recoveryCodes, id: \.self) { code in
                    Text(code).font(SelfvoyFont.labelMedium).foregroundStyle(SelfvoyColor.accentSoft).textSelection(.enabled)
                }
            }
            if store.twoFaEnabled {
                SelfvoyField(text: $disablePassword, placeholder: "Пароль", secure: true)
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
                    Text(provider.name).font(SelfvoyFont.titleSmall).foregroundStyle(SelfvoyColor.textPrimary)
                    if !provider.description.isEmpty { Muted(provider.description) }
                    if let connection {
                        Text(connection.status == "active"
                             ? "подключено\(connection.account.isEmpty ? "" : " · \(connection.account)")"
                             : connection.status)
                            .font(SelfvoyFont.small)
                            .foregroundStyle(connection.status == "active" ? SelfvoyColor.success : SelfvoyColor.warning)
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

/// Три режима инструмента: сам / спрашивать / запретить.
private struct ModePicker: View {
    let mode: String
    let onSelect: (String) -> Void

    private let modes = [("allow", "Сам"), ("ask", "Спрашивать"), ("deny", "Запретить")]

    var body: some View {
        HStack(spacing: 0) {
            ForEach(modes.indices, id: \.self) { index in
                let value = modes[index].0
                let active = mode == value
                Button {
                    if !active { onSelect(value) }
                } label: {
                    Text(modes[index].1)
                        .font(SelfvoyFont.labelMedium)
                        .foregroundStyle(active ? SelfvoyColor.textPrimary : SelfvoyColor.textSecondary)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 8)
                        .background(Capsule().fill(active
                            ? (value == "deny" ? SelfvoyColor.danger.opacity(0.3) : SelfvoyColor.accent.opacity(0.35))
                            : Color.clear))
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
            }
        }
        .padding(3)
        .background(Capsule().fill(SelfvoyColor.well.opacity(0.45)))
        .overlay(Capsule().strokeBorder(.white.opacity(0.14), lineWidth: 1))
    }
}
