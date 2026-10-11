import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura

/*
  LoginPage — вход, регистрация, подтверждение email и восстановление пароля.

  Потоки (auth v3):
    Вход / Регистрация → (регистрация) Код подтверждения → Вход
    Вход с неподтверждённым email → Код подтверждения → Вход
    «Забыли пароль?» → Код сброса → Новый пароль → Вход
*/
Item {
    id: page

    signal loggedIn()

    readonly property bool busy: App ? App.busy : false
    // flow: "auth" (вход/регистрация) | "verify" (код email) | "forgot" | "reset"
    property string flow: "auth"
    readonly property bool registerMode: modeSwitch.currentIndex === 1

    function backToAuth() {
        flow = "auth";
    }

    // Центрируем карточку и мягко двигаем её при переключении режима
    GlassPanel {
        id: card
        anchors.centerIn: parent
        width: Math.min(430, parent.width - AuraTheme.spaceXl * 2)
        backdrop: page.Window.window ? page.Window.window.contentItem : null
        padding: AuraTheme.spaceXl

        ColumnLayout {
            anchors.fill: parent
            spacing: AuraTheme.spaceMd

            // Эмблема v3: ядро + кольцо лучей со свечением
            AuraEmblem {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: AuraTheme.spaceSm
                size: 72
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: page.flow === "auth"
                      ? (page.registerMode ? "Create your Aura" : "Welcome to Aura")
                      : (page.flow === "verify" ? "Подтверждение email"
                        : page.flow === "forgot" ? "Восстановление пароля"
                        : page.flow === "twofactor" ? "Двухфакторный вход"
                        : "Новый пароль")
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontHero
                font.weight: Font.DemiBold
                color: AuraTheme.textPrimary
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: page.flow === "auth"
                      ? (page.registerMode ? "Ваша личная сеть ИИ-агентов"
                                           : "Ваша личная сеть ИИ-агентов")
                      : (page.flow === "verify" ? "Введите код из письма"
                        : page.flow === "forgot" ? "Отправим код сброса на email"
                        : page.flow === "twofactor" ? "Код из приложения-аутентификатора"
                        : "Придумайте новый пароль")
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontSmall
                color: AuraTheme.textSecondary
                lineHeight: 1.3
            }

            Item { Layout.preferredHeight: AuraTheme.spaceXs; visible: page.flow === "auth" }

            // ============================ Поток «вход / регистрация» ============================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceMd
                visible: page.flow === "auth"

                // Скрытый хранитель индекса режима: 0 — вход, 1 — регистрация.
                // Переключение — ссылкой внизу карточки (v3, без пилюли).
                QtObject {
                    id: modeSwitch
                    property int currentIndex: 0
                }

                GlassField {
                    id: nameField
                    Layout.fillWidth: true
                    label: "Имя"
                    icon: "👤"
                    placeholderText: "Как вас зовут"
                    visible: page.registerMode
                    enabled: !page.busy
                }

                GlassField {
                    id: emailField
                    Layout.fillWidth: true
                    label: "Email"
                    icon: "✉"
                    placeholderText: "you@example.com"
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                    enabled: !page.busy
                }

                GlassField {
                    id: passwordField
                    Layout.fillWidth: true
                    label: "Пароль"
                    icon: "🔒"
                    action: "👁"
                    placeholderText: page.registerMode ? "минимум 8 символов, буквы и цифры" : "••••••••"
                    echoMode: revealPassword.checked ? TextInput.Normal : TextInput.Password
                    enabled: !page.busy
                    onAccepted: submitButton.clicked()
                    onActionClicked: revealPassword.toggle()

                    // «Глаз»: показать/скрыть пароль (v3)
                    CheckBox {
                        id: revealPassword
                        visible: false
                        function toggle() { checked = !checked }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: page.registerMode
                    text: "Пароль хранится как Argon2id (RFC 9106), сессия — JWT + refresh-ротация."
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontMicro
                    color: AuraTheme.textMuted
                    wrapMode: Text.WordWrap
                }

                GlassButton {
                    id: submitButton
                    Layout.fillWidth: true
                    variant: "accent"
                    text: page.registerMode ? "Создать аккаунт" : "Войти"
                    glyph: page.registerMode ? "✦" : "→"
                    busy: page.busy
                    enabled: !page.busy
                    onClicked: {
                        if (page.registerMode) {
                            App.registerUser(nameField.text, emailField.text, passwordField.text)
                        } else {
                            App.login(emailField.text, passwordField.text)
                        }
                    }
                }

                // Google OAuth: сервер ещё не отдаёт вход через Google
                // (только интеграции) — кнопка честно неактивна.
                GlassButton {
                    Layout.fillWidth: true
                    text: "Continue with Google"
                    glyph: "G"
                    enabled: false
                    ToolTip.visible: hovered
                    ToolTip.delay: 400
                    ToolTip.text: "Появится после настройки Google OAuth на сервере"
                }

                // Забыли пароль? · Сменить режим (v3: вместо пилюли)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: AuraTheme.spaceSm

                    Text {
                        text: "Забыли пароль?"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.accentSoft
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                forgotEmailField.text = emailField.text;
                                page.flow = "forgot";
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: page.registerMode ? "Уже есть аккаунт? Войти"
                                                : "Нет аккаунта? Создать Ауру"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.accentSoft
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: modeSwitch.currentIndex = page.registerMode ? 0 : 1
                        }
                    }
                }
            }

            // ============================ Поток «код подтверждения» ============================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceMd
                visible: page.flow === "verify"

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: "Код отправлен на " + (App ? App.pendingEmail : "")
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    color: AuraTheme.textSecondary
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    visible: App && App.devEmailCode !== ""
                    text: "Режим разработки — код: " + (App ? App.devEmailCode : "")
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    font.weight: Font.DemiBold
                    color: AuraTheme.warning
                }

                GlassField {
                    id: codeField
                    Layout.fillWidth: true
                    label: "Код из письма"
                    placeholderText: "6 цифр"
                    inputMethodHints: Qt.ImhDigitsOnly
                    enabled: !page.busy
                    onAccepted: verifyButton.clicked()
                }

                GlassButton {
                    id: verifyButton
                    Layout.fillWidth: true
                    variant: "accent"
                    text: "Подтвердить"
                    glyph: "✓"
                    busy: page.busy
                    enabled: !page.busy
                    onClicked: App.verifyEmail(App.pendingEmail, codeField.text)
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Отправить код ещё раз"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.accentSoft
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.resendCode(App.pendingEmail)
                        }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: "Назад"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textSecondary
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.backToAuth()
                        }
                    }
                }
            }

            // ============================ Поток «двухфакторная аутентификация» ============================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceMd
                visible: page.flow === "twofactor"

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: "Двухфакторная аутентификация"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontBody
                    font.weight: Font.DemiBold
                    color: AuraTheme.textPrimary
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: "Введите 6-значный код из приложения-аутентификатора\nили резервный код вида XXXXX-XXXXX"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    color: AuraTheme.textSecondary
                }

                GlassField {
                    id: twoFactorCodeField
                    Layout.fillWidth: true
                    label: "Код подтверждения"
                    placeholderText: "123456 или XXXXX-XXXXX"
                    enabled: !page.busy
                    onAccepted: twoFactorButton.clicked()
                }

                CheckBox {
                    id: trustDeviceCheck
                    Layout.fillWidth: true
                    text: "Запомнить это устройство (90 дней)"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    contentItem: Text {
                        leftPadding: trustDeviceCheck.indicator.width + AuraTheme.spaceSm
                        text: trustDeviceCheck.text
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textSecondary
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                GlassButton {
                    id: twoFactorButton
                    Layout.fillWidth: true
                    variant: "accent"
                    text: "Подтвердить"
                    glyph: "✓"
                    busy: page.busy
                    enabled: !page.busy
                    onClicked: App.login2fa(twoFactorCodeField.text, trustDeviceCheck.checked)
                }

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: "Назад"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    color: AuraTheme.textSecondary
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            App.cancelTwoFactor();
                            page.backToAuth();
                        }
                    }
                }
            }

            // ============================ Поток «забыли пароль» ============================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceMd
                visible: page.flow === "forgot"

                GlassField {
                    id: forgotEmailField
                    Layout.fillWidth: true
                    label: "Email аккаунта"
                    placeholderText: "you@example.com"
                    inputMethodHints: Qt.ImhEmailCharactersOnly
                    enabled: !page.busy
                    onAccepted: forgotButton.clicked()
                }

                GlassButton {
                    id: forgotButton
                    Layout.fillWidth: true
                    variant: "accent"
                    text: "Отправить код сброса"
                    glyph: "→"
                    busy: page.busy
                    enabled: !page.busy
                    onClicked: App.forgotPassword(forgotEmailField.text)
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Назад ко входу"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    color: AuraTheme.textSecondary
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.backToAuth()
                    }
                }
            }

            // ============================ Поток «новый пароль» ============================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceMd
                visible: page.flow === "reset"

                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    visible: App && App.devResetCode !== ""
                    text: "Режим разработки — код: " + (App ? App.devResetCode : "")
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    font.weight: Font.DemiBold
                    color: AuraTheme.warning
                    wrapMode: Text.WordWrap
                }

                GlassField {
                    id: resetCodeField
                    Layout.fillWidth: true
                    label: "Код сброса"
                    placeholderText: "код из письма"
                    enabled: !page.busy
                }

                GlassField {
                    id: newPasswordField
                    Layout.fillWidth: true
                    label: "Новый пароль"
                    placeholderText: "минимум 8 символов, буквы и цифры"
                    echoMode: TextInput.Password
                    enabled: !page.busy
                    onAccepted: resetButton.clicked()
                }

                GlassButton {
                    id: resetButton
                    Layout.fillWidth: true
                    variant: "accent"
                    text: "Установить пароль"
                    glyph: "✓"
                    busy: page.busy
                    enabled: !page.busy
                    onClicked: App.resetPassword(App.pendingEmail, resetCodeField.text, newPasswordField.text)
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Назад ко входу"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    color: AuraTheme.textSecondary
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.backToAuth()
                    }
                }
            }

            // Строка состояния/подсказки
            Text {
                id: statusLine
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: App ? App.statusMessage : ""
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontMicro
                color: AuraTheme.textSecondary
                wrapMode: Text.WordWrap
                visible: text !== ""
            }

            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: App ? App.connectionLabel : ""
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontMicro
                color: App && App.connected ? AuraTheme.textSecondary : AuraTheme.warning
            }
        }
    }

    Connections {
        target: App
        function onAuthenticatedChanged() {
            if (App.authenticated) page.loggedIn()
        }
        function onVerificationRequested() {
            page.flow = "verify";
        }
        function onResetFlowRequested() {
            page.flow = "reset";
        }
        function onTwoFactorRequiredChanged() {
            // Вход потребовал код 2FA — переключаемся на шаг ввода кода.
            if (App.twoFactorRequired) {
                page.flow = "twofactor";
            } else if (page.flow === "twofactor") {
                page.flow = "auth";
            }
        }
        function onStatusMessageChanged() {
            // После удачного подтверждения/сброса возвращаемся ко входу.
            if (App.statusMessage.indexOf("подтверждён") !== -1 ||
                App.statusMessage.indexOf("Пароль изменён — войдите") !== -1) {
                if (page.flow !== "auth") {
                    page.flow = "auth";
                    modeSwitch.currentIndex = 0;
                }
            }
        }
    }
}
