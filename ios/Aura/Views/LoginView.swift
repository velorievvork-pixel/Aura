// LoginView.swift — вход (макет Welcome) и регистрация (макет Create your Aura),
// плюс шаги auth v3: код из письма, 2FA (TOTP или резервный код), сброс пароля.
import AuraKit
import SwiftUI

struct LoginView: View {

    @EnvironmentObject private var store: AppStore
    @State private var register = false
    @State private var email = ""
    @State private var password = ""
    @State private var displayName = ""
    @State private var focus = ""
    @State private var refCode = ""
    @State private var showPassword = false
    @State private var code = ""
    @State private var newPassword = ""
    @State private var trustDevice = true
    @State private var serverURL = ""
    @State private var showServer = false

    private var isRegister: Bool { register && store.authState == .loggedOut }

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                Spacer(minLength: 24)
                VStack(alignment: .leading, spacing: 14) {
                    logo
                    Text(title)
                        .font(AuraFont.headline)
                        .foregroundStyle(AuraColor.textPrimary)
                        .multilineTextAlignment(isRegister ? .leading : .center)
                        .frame(maxWidth: .infinity, alignment: isRegister ? .leading : .center)
                    form
                }
                .glassCard(radius: 34, padding: 26)
                LinkButton(title: showServer ? "Скрыть адрес сервера" : "Адрес сервера") { showServer.toggle() }
                if showServer {
                    VStack(spacing: 10) {
                        AuraField(text: $serverURL, placeholder: "ws:// или wss://", keyboard: .URL)
                        GlassButton(title: "Сохранить адрес") { store.setServerURL(serverURL) }
                        Muted("Сейчас: \(store.serverURLText)", alignment: .center)
                    }
                }
            }
            .padding(20)
            .frame(maxWidth: 520)
            .frame(maxWidth: .infinity)
        }
        .scrollDismissesKeyboard(.interactively)
        .onAppear { serverURL = store.serverURLText }
        // aura://signup?ref=CODE — сразу форма регистрации с подставленным кодом.
        .onChange(of: store.signupRef, initial: true) { _, link in
            guard let link else { return }
            register = true
            if !link.isEmpty { refCode = link }
            store.signupRef = nil
        }
    }

    private var title: String {
        switch store.authState {
        case .needsEmailCode: return "Подтверждение email"
        case .needs2fa: return "Двухфакторный вход"
        case .needsResetCode: return "Новый пароль"
        default: return isRegister ? "Создайте свою Ауру" : "Добро пожаловать в Aura"
        }
    }

    @ViewBuilder
    private var logo: some View {
        if isRegister {
            HStack(spacing: 10) {
                Image(systemName: "triangle.fill").font(.system(size: 22)).foregroundStyle(.white)
                Text("Aura").font(AuraFont.manrope(20, .bold)).foregroundStyle(AuraColor.textPrimary)
            }
            .frame(maxWidth: .infinity)
        } else {
            AuraEmblem(size: 76).frame(maxWidth: .infinity)
        }
    }

    @ViewBuilder
    private var form: some View {
        switch store.authState {
        case .needsEmailCode(let address):
            Muted("Код отправлен на \(address)", alignment: .center).frame(maxWidth: .infinity)
            AuraField(text: $code, placeholder: "6 цифр", keyboard: .numberPad)
            GradientButton(title: "Подтвердить", busy: store.busy) {
                store.verifyEmail(email: address, code: code)
                code = ""
            }
            HStack {
                LinkButton(title: "Отправить код ещё раз") { store.resendCode(email: address) }
                Spacer()
                LinkButton(title: "Назад ко входу") { store.cancelAuthFlow() }
            }
        case .needs2fa:
            Muted("Код из приложения-аутентификатора или резервный код XXXXX-XXXXX", alignment: .center)
                .frame(maxWidth: .infinity)
            AuraField(text: $code, placeholder: "123456", keyboard: .numbersAndPunctuation)
            Toggle(isOn: $trustDevice) {
                Text("Доверять устройству 90 дней").font(AuraFont.small).foregroundStyle(AuraColor.textSecondary)
            }
            .tint(AuraColor.accent)
            GradientButton(title: "Войти", busy: store.busy) {
                store.login2fa(code: code, trustDevice: trustDevice)
                code = ""
            }
            LinkButton(title: "Отмена") { store.cancelAuthFlow() }.frame(maxWidth: .infinity)
        case .needsResetCode(let address):
            Muted("Код сброса отправлен на \(address)", alignment: .center).frame(maxWidth: .infinity)
            AuraField(text: $code, placeholder: "Код из письма", keyboard: .numberPad)
            AuraField(text: $newPassword, placeholder: "Новый пароль: 8+ символов, буквы и цифры",
                      systemImage: "lock", secure: true)
            GradientButton(title: "Сменить пароль", busy: store.busy) {
                store.resetPassword(email: address, code: code, newPassword: newPassword)
                code = ""
                newPassword = ""
            }
            LinkButton(title: "Назад ко входу") { store.cancelAuthFlow() }.frame(maxWidth: .infinity)
        default:
            if isRegister { registerForm } else { loginForm }
        }
    }

    @ViewBuilder
    private var loginForm: some View {
        Muted("Ваша персональная сеть ИИ-агентов", alignment: .center).frame(maxWidth: .infinity)
        AuraField(text: $email, placeholder: "Введите email", label: "Email", systemImage: "envelope",
                  keyboard: .emailAddress, submitLabel: .next)
        AuraField(text: $password, placeholder: "••••••••", label: "Пароль", systemImage: "lock", secure: true,
                  submitLabel: .go, onSubmit: login)
        HStack {
            Spacer()
            LinkButton(title: "Забыли пароль?") { store.forgotPassword(email: email) }
        }
        GradientButton(title: "Войти", busy: store.busy, action: login)
        // Вход через Google сервер пока не поддерживает: кнопка честно неактивна.
        GlassButton(title: "Продолжить с Google", enabled: false) {}
        HStack {
            Spacer()
            LinkButton(title: "Создать аккаунт") { register = true }
        }
    }

    @ViewBuilder
    private var registerForm: some View {
        AuraField(text: $displayName, placeholder: "Введите имя и фамилию", label: "Полное имя")
        AuraField(text: $email, placeholder: "Введите email", label: "Email", keyboard: .emailAddress)
        AuraField(text: $password, placeholder: "Минимум 8 символов, буквы и цифры", label: "Пароль",
                  systemImage: showPassword ? "eye.slash" : "eye", onTrailing: { showPassword.toggle() },
                  secure: !showPassword)
        AuraField(text: .constant("\(TimeZone.current.identifier) · по системе"), placeholder: "",
                  label: "Часовой пояс", systemImage: "clock")
            .disabled(true)
        AuraField(text: $focus, placeholder: "например, встречи, продуктивность, код",
                  label: "С чем должна помогать ваша Аура?", filled: true)
        AuraField(text: $refCode, placeholder: "Например, ABCD2345", label: "Код приглашения (необязательно)",
                  keyboard: .asciiCapable)
            .onChange(of: refCode) { _, value in
                let cleaned = String(value.trimmingCharacters(in: .whitespaces).uppercased().prefix(ReferralCode.length))
                if cleaned != value { refCode = cleaned }
            }
        if !refCode.isEmpty && ReferralCode.normalize(refCode) == nil {
            Muted("Код — \(ReferralCode.length) латинских букв и цифр; с неверным кодом регистрация пройдёт без приглашения")
        }
        GradientButton(title: "Создать аккаунт", busy: store.busy) {
            store.register(displayName: displayName.trimmingCharacters(in: .whitespaces),
                           email: email.trimmingCharacters(in: .whitespaces),
                           password: password, focus: focus, ref: refCode)
        }
        .padding(.top, 6)
        Muted("Пароль хранится как Argon2id, сессия — JWT", alignment: .center).frame(maxWidth: .infinity)
        HStack {
            Spacer()
            LinkButton(title: "Уже есть аккаунт?") { register = false }
        }
    }

    private func login() {
        store.login(email: email.trimmingCharacters(in: .whitespaces), password: password)
    }
}
