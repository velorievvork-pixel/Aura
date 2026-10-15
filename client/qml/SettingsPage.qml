import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Selfvoy
import "Format.js" as F

/*
  SettingsPage — профиль, предпочтения Selfvoy, долговременная память, выход.
*/
Item {
    id: page

    signal back()
    signal loggedOut()

    // true — страница встроена в desktop-оболочку: кнопка «назад» не нужна.
    property bool embedded: false

    property var prefs: App ? App.preferences : ({})
    property var privacy: App ? App.privacy : ({})

    // Длина в кодовых точках — так же считает сервер (эмодзи — один символ).
    function charCount(text) { return Array.from(text || "").length }

    // Поля профиля заполняются из App; правки пользователя не затираются,
    // пока поле в фокусе.
    function resetProfileFields() {
        if (!nameField.activeFocus) nameField.text = App ? App.userName : ""
        if (!bioField.activeFocus) bioField.text = App ? App.userBio : ""
    }

    Component.onCompleted: resetProfileFields()

    Connections {
        target: App
        function onProfileChanged() { page.resetProfileFields() }
        function onProfileSaved() { profileError.text = "" }
        function onProfileSaveFailed(message) { profileError.text = message }
        function onPrivacySaveFailed(key, message) { privacyError.text = "Не сохранилось: " + message }
    }

    // v3: заголовок секции с lilac-индикатором слева (как в макете Settings)
    // Переключатель в стиле стекла: стандартный Basic-Switch бело-серый и
    // с лишней шириной, из-за чего вставал посреди строки.
    component GlassSwitch: Switch {
        id: sw
        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
        padding: 0
        implicitWidth: 46
        implicitHeight: 26
        indicator: Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: sw.checked ? SelfvoyTheme.accentA(0.85) : Qt.rgba(1, 1, 1, 0.12)
            border.width: 1
            border.color: sw.checked ? SelfvoyTheme.lilacA(0.5) : Qt.rgba(1, 1, 1, 0.18)
            Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
            Rectangle {
                width: 20; height: 20; radius: 10
                y: 3
                x: sw.checked ? parent.width - width - 3 : 3
                color: "#FFFFFF"
                Behavior on x { NumberAnimation { duration: SelfvoyTheme.pressMs; easing.type: SelfvoyTheme.ease } }
            }
        }
        contentItem: null
    }

    // Слайдер в стиле темы (у Basic — серый трек и белый «пятак» без акцента).
    component GlassSlider: Slider {
        id: sl
        implicitHeight: 26
        background: Rectangle {
            x: sl.leftPadding
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: sl.availableWidth
            height: 4
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.12)
            Rectangle {
                width: sl.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: SelfvoyTheme.accentSoft
            }
        }
        handle: Rectangle {
            x: sl.leftPadding + sl.visualPosition * (sl.availableWidth - width)
            y: sl.topPadding + sl.availableHeight / 2 - height / 2
            width: 18; height: 18; radius: 9
            color: "#FFFFFF"
            border.width: sl.pressed ? 3 : 2
            border.color: SelfvoyTheme.accent
        }
    }

    component SectionTitle: RowLayout {
        property string title: ""
        property int titleSize: SelfvoyTheme.fontBody
        spacing: SelfvoyTheme.spaceSm
        Rectangle {
            Layout.preferredWidth: 3
            Layout.preferredHeight: titleSize + 2
            radius: 2
            color: SelfvoyTheme.lilac
        }
        Text {
            text: title
            font.family: SelfvoyTheme.fontFamily
            font.pixelSize: titleSize
            font.weight: Font.DemiBold
            color: SelfvoyTheme.textPrimary
        }
    }

    // Раздел настроек; навигация слева — как в макете (сиреневый индикатор у активного).
    property string section: "profile"
    readonly property var sections: [
        { key: "profile", label: "Профиль и Selfvoy", icon: "user" },
        { key: "permissions", label: "Разрешения", icon: "shield" },
        { key: "security", label: "Безопасность", icon: "lock" },
        { key: "voice", label: "Голос", icon: "mic" },
        { key: "integrations", label: "Интеграции", icon: "calendar-days" },
        { key: "memory", label: "Память", icon: "brain" },
        { key: "referral", label: "Пригласить друзей", icon: "user-plus" }
    ]

    Text {
        visible: page.embedded
        x: 48
        y: 40
        text: "Настройки"
        font.family: SelfvoyTheme.fontFamily
        font.pixelSize: 38
        font.weight: Font.Bold
        color: SelfvoyTheme.textPrimary
    }

    Column {
        id: sectionNav
        visible: page.embedded
        x: 34
        y: 112
        width: 200
        spacing: 4
        Repeater {
            model: page.sections
            delegate: Rectangle {
                required property var modelData
                readonly property bool active: page.section === modelData.key
                width: sectionNav.width
                height: 44
                radius: SelfvoyTheme.radiusSmall
                color: active ? SelfvoyTheme.accentA(0.14) : (navArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                Rectangle {
                    visible: parent.active
                    width: 3; height: 22; radius: 1.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: SelfvoyTheme.lilac
                }
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 16
                    spacing: 11
                    Icon { name: modelData.icon; size: 17; color: parent.parent.active ? SelfvoyTheme.textPrimary : SelfvoyTheme.textSecondary; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        text: modelData.label
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontBody - 1
                        font.weight: parent.parent.active ? Font.Bold : Font.Medium
                        color: parent.parent.active ? SelfvoyTheme.textPrimary : SelfvoyTheme.textSecondary
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                MouseArea {
                    id: navArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        page.section = modelData.key
                        if (modelData.key === "permissions") App.loadPermissions()
                        if (modelData.key === "integrations") App.loadIntegrations()
                        if (modelData.key === "security") { App.loadSessions(); App.loadTwoFactorStatus() }
                        if (modelData.key === "memory") App.refreshMemory()
                    }
                }
            }
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: SelfvoyTheme.spaceXl
        anchors.leftMargin: page.embedded ? sectionNav.x + sectionNav.width + 24 : SelfvoyTheme.spaceXl
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            width: parent.width
            spacing: SelfvoyTheme.spaceMd

            // ---------------------------------------------------- шапка
            RowLayout {
                Layout.fillWidth: true
                spacing: SelfvoyTheme.spaceMd

                GlassButton {
                    visible: !page.embedded
                    variant: "quiet"
                    glyph: "←"
                    text: ""
                    implicitWidth: 44
                    implicitHeight: 40
                    onClicked: page.back()
                }

                Text {
                    Layout.fillWidth: true
                    // Во встроенном режиме заголовок «Настройки» стоит над навигацией слева.
                    text: page.embedded ? "" : "Настройки"
                    font.family: SelfvoyTheme.fontFamily
                    font.pixelSize: 30
                    font.weight: Font.Bold
                    color: SelfvoyTheme.textPrimary
                }

                GlassButton {
                    variant: "danger"
                    text: "Выйти"
                    glyph: "⎋"
                    implicitHeight: 40
                    onClicked: {
                        App.logout()
                        page.loggedOut()
                    }
                }
            }

            // -------------------------------------------------- профиль
            GlassPanel {
                visible: page.section === "profile"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                RowLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceMd

                    SelfvoyAvatar { name: App ? App.userName : ""; size: 52; online: App ? App.connected : false }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: App ? App.userName : ""
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontTitle
                            font.weight: Font.Bold
                            color: SelfvoyTheme.textPrimary
                        }
                        Text {
                            text: App ? App.userEmail : ""
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: App ? App.userBio : ""
                            textFormat: Text.PlainText
                            wrapMode: Text.WordWrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }
                        Text {
                            text: "Сервер: " + (App ? App.serverUrl : "") + " · " + (App && App.connected ? "подключено" : "офлайн")
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            color: App && App.connected ? SelfvoyTheme.textMuted : SelfvoyTheme.warning
                        }
                    }
                }
            }

            // ------------------------------------------ редактирование профиля
            GlassPanel {
                visible: page.section === "profile"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceSm

                    SectionTitle { title: "Профиль" }

                    Text {
                        Layout.fillWidth: true
                        text: "Имя и «о себе» видят ваши контакты и их Selfvoy при переговорах."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textMuted
                        wrapMode: Text.WordWrap
                    }

                    GlassField {
                        id: nameField
                        Layout.fillWidth: true
                        label: "Имя · " + chars + "/80"
                        placeholderText: "Как вас называть"
                        readonly property int chars: page.charCount(text.trim())
                        readonly property bool valid: chars >= 1 && chars <= 80
                        invalid: !valid
                        onTextEdited: profileError.text = ""
                    }

                    GlassField {
                        id: bioField
                        Layout.fillWidth: true
                        label: "О себе · " + chars + "/300"
                        placeholderText: "Пара слов о себе (необязательно)"
                        readonly property int chars: page.charCount(text.trim())
                        readonly property bool valid: chars <= 300
                        invalid: !valid
                        onTextEdited: profileError.text = ""
                    }

                    Text {
                        id: profileHint
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: !nameField.valid
                              ? (nameField.chars === 0 ? "Имя не может быть пустым" : "Имя — не длиннее 80 символов")
                              : (!bioField.valid ? "«О себе» — не длиннее 300 символов" : "")
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.danger
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        id: profileError
                        Layout.fillWidth: true
                        visible: text.length > 0 && !profileHint.visible
                        text: ""
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.danger
                        wrapMode: Text.WordWrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceSm

                        GlassButton {
                            variant: "accent"
                            text: "Сохранить профиль"
                            glyph: "✓"
                            busy: App ? App.busy : false
                            enabled: nameField.valid && bioField.valid
                                     && (nameField.text.trim() !== (App ? App.userName : "")
                                         || bioField.text.trim() !== (App ? App.userBio : ""))
                            onClicked: {
                                nameField.focus = false
                                bioField.focus = false
                                App.updateProfile(nameField.text, bioField.text)
                            }
                        }

                        GlassButton {
                            variant: "quiet"
                            text: "Отменить"
                            visible: nameField.text !== (App ? App.userName : "")
                                     || bioField.text !== (App ? App.userBio : "")
                            onClicked: {
                                nameField.focus = false
                                bioField.focus = false
                                profileError.text = ""
                                page.resetProfileFields()
                            }
                        }
                    }
                }
            }

            // --------------------------------------------- предпочтения
            GlassPanel {
                visible: page.section === "profile"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceMd

                    SectionTitle { title: "Что знает ваш Selfvoy" }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceMd

                        GlassField {
                            id: cityField
                            Layout.fillWidth: true
                            label: "Город"
                            placeholderText: "Керкраде"
                            text: page.prefs && page.prefs.city ? page.prefs.city : ""
                        }

                        GlassField {
                            id: budgetField
                            Layout.fillWidth: true
                            label: "Бюджет, €"
                            placeholderText: "3"
                            validator: DoubleValidator { bottom: 0; top: 1000; decimals: 2 }
                            text: page.prefs && page.prefs.budget_limit ? String(page.prefs.budget_limit) : ""
                        }
                    }

                    // Диета — стеклянные чипы
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceSm

                        Text {
                            text: "Диета"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }

                        Flow {
                            id: dietFlow
                            Layout.fillWidth: true
                            spacing: SelfvoyTheme.spaceSm

                            property var selected: page.prefs && page.prefs.diet ? page.prefs.diet : []

                            Repeater {
                                model: ["vegan", "vegetarian", "gluten_free", "halal", "без кофеина"]
                                delegate: Rectangle {
                                    id: chip
                                    width: chipText.width + SelfvoyTheme.spaceLg
                                    height: 34
                                    radius: SelfvoyTheme.radiusPill
                                    readonly property bool on: dietFlow.selected.indexOf(modelData) >= 0
                                    color: on ? SelfvoyTheme.accentA(0.25) : Qt.rgba(1, 1, 1, 0.06)
                                    border.width: 1
                                    border.color: on ? SelfvoyTheme.accentA(0.55) : Qt.rgba(1, 1, 1, SelfvoyTheme.glassBorder)
                                    scale: chipArea.pressed ? SelfvoyTheme.pressScale : 1
                                    Behavior on scale { NumberAnimation { duration: SelfvoyTheme.pressMs } }

                                    Text {
                                        id: chipText
                                        anchors.centerIn: parent
                                        // Значения — как ждёт AI-сервис, подписи — по-русски.
                                        text: ({ vegan: "веган", vegetarian: "вегетарианец", gluten_free: "без глютена",
                                                 halal: "халяль" })[modelData] || modelData
                                        font.family: SelfvoyTheme.fontFamily
                                        font.pixelSize: SelfvoyTheme.fontSmall
                                        color: chip.on ? SelfvoyTheme.textPrimary : SelfvoyTheme.textSecondary
                                    }

                                    MouseArea {
                                        id: chipArea
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            const list = dietFlow.selected.slice()
                                            const at = list.indexOf(modelData)
                                            if (at >= 0) list.splice(at, 1)
                                            else list.push(modelData)
                                            dietFlow.selected = list
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Удобные часы — чипы времени
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceSm

                        Text {
                            text: "Удобные часы для встреч"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }

                        Flow {
                            id: hoursFlow
                            Layout.fillWidth: true
                            spacing: 6

                            property var selected: page.prefs && page.prefs.preferred_hours
                                                   ? page.prefs.preferred_hours : [10, 11, 18]

                            Repeater {
                                model: [8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21]
                                delegate: Rectangle {
                                    id: hourChip
                                    width: 40
                                    height: 32
                                    radius: SelfvoyTheme.radiusField
                                    readonly property bool on: hoursFlow.selected.indexOf(modelData) >= 0
                                    color: on ? SelfvoyTheme.accentA(0.25) : Qt.rgba(1, 1, 1, 0.05)
                                    border.width: 1
                                    border.color: on ? SelfvoyTheme.accentA(0.5) : Qt.rgba(1, 1, 1, SelfvoyTheme.glassBorder)
                                    scale: hourArea.pressed ? SelfvoyTheme.pressScale : 1
                                    Behavior on scale { NumberAnimation { duration: SelfvoyTheme.pressMs } }

                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData + ":00"
                                        font.family: SelfvoyTheme.fontFamily
                                        font.pixelSize: SelfvoyTheme.fontMicro
                                        color: hourChip.on ? SelfvoyTheme.textPrimary : SelfvoyTheme.textMuted
                                    }

                                    MouseArea {
                                        id: hourArea
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            const list = hoursFlow.selected.slice()
                                            const at = list.indexOf(modelData)
                                            if (at >= 0) list.splice(at, 1)
                                            else list.push(modelData)
                                            hoursFlow.selected = list
                                        }
                                    }
                                }
                            }
                        }
                    }

                    GlassButton {
                        variant: "accent"
                        text: "Сохранить"
                        glyph: "✓"
                        busy: App ? App.busy : false
                        onClicked: App.savePreferences({
                            city: cityField.text,
                            budget_limit: budgetField.text.length > 0 ? parseFloat(budgetField.text) : 0,
                            diet: dietFlow.selected,
                            preferred_hours: hoursFlow.selected
                        })
                    }
                }
            }

            // ------------------------------------------------ приватность ИИ
            GlassPanel {
                visible: page.section === "profile"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceMd

                    SectionTitle { title: "Приватность ИИ" }

                    Text {
                        Layout.fillWidth: true
                        text: "Что ваш Selfvoy раскрывает чужому Selfvoy, когда договаривается о встрече."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: [
                            { key: "share_free_slots_only",
                              title: "Делиться только свободными слотами",
                              hint: "Другому Selfvoy уходят только занятость и настройки, нужные для времени и места. "
                                    + "Факты из памяти, день рождения и аллергии не передаются." },
                            { key: "allow_place_choice",
                              title: "Разрешить выбор локаций",
                              hint: "Selfvoy может предложить место встречи. Если выключено у вас или у собеседника, "
                                    + "место не предлагается — выберете сами." },
                            { key: "hide_event_titles",
                              title: "Скрывать названия других событий",
                              hint: "Названия событий вашего календаря заменяются на «занято»." }
                        ]

                        delegate: RowLayout {
                            id: privacyRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: SelfvoyTheme.spaceMd

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: privacyRow.modelData.title
                                    font.family: SelfvoyTheme.fontFamily
                                    font.pixelSize: SelfvoyTheme.fontSmall
                                    color: SelfvoyTheme.textPrimary
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: privacyRow.modelData.hint
                                    font.family: SelfvoyTheme.fontFamily
                                    font.pixelSize: SelfvoyTheme.fontMicro
                                    color: SelfvoyTheme.textMuted
                                    wrapMode: Text.WordWrap
                                }
                            }

                            GlassSwitch {
                                checked: page.privacy[privacyRow.modelData.key] === true
                                Accessible.name: privacyRow.modelData.title
                                onToggled: {
                                    privacyError.text = ""
                                    App.setPrivacy(privacyRow.modelData.key, checked)
                                    // Переключение рвёт привязку checked — восстанавливаем,
                                    // чтобы откат при ошибке сервера был виден.
                                    checked = Qt.binding(() => page.privacy[privacyRow.modelData.key] === true)
                                }
                            }
                        }
                    }

                    Text {
                        id: privacyError
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: ""
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.danger
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // -------------------------------------------------- память
            GlassPanel {
                visible: page.section === "memory"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceSm

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Долговременная память"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontBody
                            font.weight: Font.DemiBold
                            color: SelfvoyTheme.textPrimary
                        }
                        GlassButton {
                            variant: "quiet"
                            text: "Обновить"
                            glyph: "⟳"
                            implicitHeight: 34
                            onClicked: App.refreshMemory()
                        }
                    }

                    Repeater {
                        model: App ? App.memory : []
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: memoryText.implicitHeight + SelfvoyTheme.spaceMd
                            radius: SelfvoyTheme.radiusField
                            color: Qt.rgba(1, 1, 1, 0.04)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: SelfvoyTheme.spaceSm
                                spacing: SelfvoyTheme.spaceSm

                                Rectangle {
                                    Layout.preferredWidth: 4
                                    Layout.fillHeight: true
                                    radius: 2
                                    color: (modelData && modelData.kind === "preference")
                                           ? SelfvoyTheme.accentSoft : SelfvoyTheme.accent
                                }

                                Text {
                                    id: memoryText
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: modelData ? String(modelData.text || "") : ""
                                    font.family: SelfvoyTheme.fontFamily
                                    font.pixelSize: SelfvoyTheme.fontSmall
                                    color: SelfvoyTheme.textSecondary
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: (App ? App.memory.length : 0) === 0
                        text: "Память пока пуста — Selfvoy запомнит важное из разговоров."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textMuted
                    }
                }
            }

            // ------------------------------------------- Голос и озвучка
            GlassPanel {
                visible: page.section === "voice"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceMd

                    SectionTitle { title: "Голос и озвучка" }

                    // --- Язык распознавания речи ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceMd
                        Text {
                            Layout.fillWidth: true
                            text: "Язык распознавания"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }
                        GlassComboBox {
                            id: micLangBox
                            Layout.preferredWidth: 160
                            textRole: "label"
                            model: [{ value: "auto", label: "Авто" },
                                    { value: "ru", label: "Русский" },
                                    { value: "en", label: "English" }]
                            currentIndex: Math.max(0, model.findIndex(m => m.value === (App ? App.micLanguage : "auto")))
                            onActivated: App.setMicLanguage(model[currentIndex].value)
                        }
                    }

                    // --- Озвучка ответов (TTS) ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceMd
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Озвучивать ответы Selfvoy"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontSmall
                                color: SelfvoyTheme.textPrimary
                            }
                            Text {
                                text: "Синтез речи средствами платформы"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontMicro
                                color: SelfvoyTheme.textMuted
                            }
                        }
                        Item { Layout.fillWidth: true }
                        GlassSwitch {
                            checked: App ? App.ttsEnabled : false
                            onToggled: App.setTtsEnabled(checked)
                        }
                    }

                    // --- Голос ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceMd
                        visible: App ? App.ttsEnabled : false
                        Text {
                            Layout.fillWidth: true
                            text: "Голос"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }
                        GlassComboBox {
                            id: voiceBox
                            Layout.preferredWidth: 220
                            model: App ? App.ttsVoices : []
                            enabled: count > 0
                            displayText: count > 0 ? currentText : "Системный голос"
                            currentIndex: Math.max(0, model.indexOf(App ? App.ttsVoice : ""))
                            onActivated: App.setTtsVoice(model[currentIndex])
                        }
                    }

                    // --- Скорость ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceMd
                        visible: App ? App.ttsEnabled : false
                        Text {
                            Layout.preferredWidth: 90
                            text: "Скорость"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }
                        GlassSlider {
                            Layout.fillWidth: true
                            from: -1.0
                            to: 1.0
                            value: App ? App.ttsRate : 0.0
                            onMoved: App.setTtsRate(value)
                        }
                    }

                    // --- Громкость ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceMd
                        visible: App ? App.ttsEnabled : false
                        Text {
                            Layout.preferredWidth: 90
                            text: "Громкость"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }
                        GlassSlider {
                            Layout.fillWidth: true
                            from: 0.0
                            to: 1.0
                            value: App ? App.ttsVolume : 1.0
                            onMoved: App.setTtsVolume(value)
                        }
                    }

                    // --- Только важное ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceMd
                        visible: App ? App.ttsEnabled : false
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: "Только важное"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontSmall
                                color: SelfvoyTheme.textPrimary
                            }
                            Text {
                                text: "Озвучивать только ответы Selfvoy, не обычные сообщения"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontMicro
                                color: SelfvoyTheme.textMuted
                            }
                        }
                        Item { Layout.fillWidth: true }
                        GlassSwitch {
                            checked: App ? App.ttsImportantOnly : false
                            onToggled: App.setTtsImportantOnly(checked)
                        }
                    }

                    // --- Проверка голоса ---
                    GlassButton {
                        visible: App ? App.ttsEnabled : false
                        variant: "quiet"
                        text: App && App.speaking ? "Остановить" : "Проверить голос"
                        glyph: App && App.speaking ? "■" : "▶"
                        onClicked: (App && App.speaking) ? App.stopSpeaking()
                                                         : App.speakImportant("Привет! Это ваш Selfvoy.")
                    }
                }
            }

            // ------------------------------------------- Активные сессии
            GlassPanel {
                visible: page.section === "security"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                Component.onCompleted: App.loadSessions()

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceSm

                    // В узкой панели кнопки переносятся под заголовок, а не наезжают на него.
                    GridLayout {
                        Layout.fillWidth: true
                        columns: width < 480 ? 1 : 2
                        columnSpacing: SelfvoyTheme.spaceSm
                        rowSpacing: SelfvoyTheme.spaceXs
                        Text {
                            Layout.fillWidth: true
                            text: "Активные сессии"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontBody
                            font.weight: Font.DemiBold
                            color: SelfvoyTheme.textPrimary
                        }
                        RowLayout {
                            Layout.alignment: parent.columns === 1 ? Qt.AlignLeft : Qt.AlignRight
                            spacing: SelfvoyTheme.spaceSm
                            GlassButton {
                                variant: "quiet"
                                text: "Обновить"
                                glyph: "⟳"
                                implicitHeight: 34
                                onClicked: App.loadSessions()
                            }
                            GlassButton {
                                variant: "danger"
                                text: "Завершить все"
                                glyph: "⏻"
                                implicitHeight: 34
                                onClicked: App.revokeAllSessions()
                            }
                        }
                    }

                    Repeater {
                        model: App ? App.sessions : []
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            height: sessionRow.implicitHeight + SelfvoyTheme.spaceMd
                            radius: SelfvoyTheme.radiusField
                            color: Qt.rgba(1, 1, 1, 0.04)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.08)

                            RowLayout {
                                id: sessionRow
                                anchors.fill: parent
                                anchors.margins: SelfvoyTheme.spaceSm
                                spacing: SelfvoyTheme.spaceSm

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: (modelData.device || "устройство") +
                                              (modelData.current ? "  ·  это устройство" : "")
                                        font.family: SelfvoyTheme.fontFamily
                                        font.pixelSize: SelfvoyTheme.fontSmall
                                        font.weight: modelData.current ? Font.DemiBold : Font.Normal
                                        color: modelData.current ? SelfvoyTheme.accentSoft : SelfvoyTheme.textPrimary
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        // Адрес без IP (":порт", старые записи) ничего не говорит — не показываем.
                                        text: [/^:/.test(modelData.remote_addr || "") ? "" : (modelData.remote_addr || ""),
                                               modelData.created_at ? "вход " + F.stamp(modelData.created_at) : ""]
                                              .filter(x => x.length > 0).join("  ·  ")
                                        visible: text.length > 0
                                        font.family: SelfvoyTheme.fontFamily
                                        font.pixelSize: SelfvoyTheme.fontMicro
                                        color: SelfvoyTheme.textMuted
                                    }
                                }

                                GlassButton {
                                    visible: !modelData.current
                                    variant: "quiet"
                                    text: "Завершить"
                                    implicitHeight: 30
                                    onClicked: App.revokeSession(modelData.id)
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: (App ? App.sessions.length : 0) === 0
                        text: "Список пуст — обновите."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textMuted
                    }
                }
            }

            // ---------------------------------------------- Пригласить друзей
            GlassPanel {
                id: referralPanel
                visible: page.section === "referral"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null
                readonly property var info: App ? App.referral : ({})
                // Без SELFVOY_PUBLIC_URL сервер отдаёт пустой url: голый «#/signup?ref=…» никуда
                // не ведёт, поэтому делимся только кодом — друг введёт его при регистрации.
                readonly property bool hasLink: !!info.url
                readonly property string link: hasLink ? info.url : ""
                readonly property string copyValue: hasLink ? info.url : (info.code ? "Код приглашения в Selfvoy: " + info.code : "")

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceSm

                    SectionTitle { title: "Пригласить друзей" }
                    Text {
                        Layout.fillWidth: true
                        text: "Друг заведёт свой Selfvoy — и ваши Selfvoy смогут договариваться о встречах напрямую. Регистрации со страниц записи тоже засчитываются вам."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textMuted
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        visible: !referralPanel.info.code
                        text: "Загружаем реферальный код…"
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontSmall
                        color: SelfvoyTheme.textSecondary
                    }
                    RowLayout {
                        visible: !!referralPanel.info.code
                        spacing: 12
                        Text {
                            text: "Ваш код"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontSmall
                            color: SelfvoyTheme.textSecondary
                        }
                        TextEdit {
                            readOnly: true
                            selectByMouse: true
                            text: referralPanel.info.code || ""
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontTitle
                            font.weight: Font.DemiBold
                            font.letterSpacing: 2
                            color: SelfvoyTheme.textPrimary
                        }
                    }
                    TextEdit {
                        visible: !!referralPanel.info.code && referralPanel.hasLink
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.WrapAnywhere
                        text: referralPanel.link
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontSmall
                        color: SelfvoyTheme.textPrimary
                    }
                    Text {
                        visible: !!referralPanel.info.code && !referralPanel.hasLink
                        Layout.fillWidth: true
                        text: "Сервер не знает свой публичный адрес (SELFVOY_PUBLIC_URL) — делимся только кодом: друг введёт его при регистрации."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textMuted
                        wrapMode: Text.WordWrap
                    }
                    GlassButton {
                        visible: !!referralPanel.info.code
                        text: referralPanel.hasLink ? "Скопировать ссылку" : "Скопировать код"
                        onClicked: App.copyText(referralPanel.copyValue)
                    }
                    Text {
                        visible: !!referralPanel.info.code
                        text: "Зарегистрировались: " + (referralPanel.info.joined || 0) + " · подтвердили email: " + (referralPanel.info.verified || 0)
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontSmall
                        color: SelfvoyTheme.textSecondary
                    }
                    Repeater {
                        model: referralPanel.info.referrals || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            Text {
                                Layout.fillWidth: true
                                text: modelData.display_name || "Без имени"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontSmall
                                color: SelfvoyTheme.textPrimary
                            }
                            Text {
                                text: modelData.verified ? "С Selfvoy" : "Ждёт подтверждения"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontMicro
                                color: modelData.verified ? SelfvoyTheme.textSecondary : SelfvoyTheme.textMuted
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------- Безопасность
            GlassPanel {
                visible: page.section === "security"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceSm

                    SectionTitle { title: "Смена пароля" }

                    Text {
                        Layout.fillWidth: true
                        text: "Пароль хранится как Argon2id. После смены прочие устройства будут отключены."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textMuted
                        wrapMode: Text.WordWrap
                    }

                    GlassField {
                        id: oldPasswordField
                        Layout.fillWidth: true
                        label: "Текущий пароль"
                        echoMode: TextInput.Password
                    }

                    GlassField {
                        id: newPasswordField
                        Layout.fillWidth: true
                        label: "Новый пароль"
                        placeholderText: "минимум 8 символов, буквы и цифры"
                        echoMode: TextInput.Password
                    }

                    GlassButton {
                        variant: "accent"
                        text: "Сменить пароль"
                        glyph: "🔒"
                        busy: App ? App.busy : false
                        onClicked: {
                            App.changePassword(oldPasswordField.text, newPasswordField.text);
                            oldPasswordField.text = "";
                            newPasswordField.text = "";
                        }
                    }
                }
            }

            // ---------------------------------------------- Двухфакторная аутентификация (TOTP)
            GlassPanel {
                visible: page.section === "security"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null
                Component.onCompleted: App.loadTwoFactorStatus()

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceSm

                    SectionTitle { title: "Двухфакторная аутентификация (TOTP)" }

                    Text {
                        Layout.fillWidth: true
                        text: App && App.twoFactorEnabled
                              ? "Включена. При входе с нового устройства потребуется код из приложения-аутентификатора."
                              : "Защитите аккаунт вторым фактором: коды из приложения-аутентификатора (Google Authenticator, 1Password и др.)."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textMuted
                        wrapMode: Text.WordWrap
                    }

                    // --- Состояние: выключена — кнопка включения ---
                    GlassButton {
                        visible: App && !App.twoFactorEnabled && !App.twoFactorPending
                        variant: "accent"
                        text: "Включить 2FA"
                        glyph: "🔑"
                        busy: App ? App.busy : false
                        onClicked: App.setup2fa()
                    }

                    // --- Состояние: настройка (секрет выдан, ждём подтверждения) ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceSm
                        visible: App && App.twoFactorPending && App.twoFactorSecret !== ""

                        Text {
                            Layout.fillWidth: true
                            text: "1. Добавьте секрет в приложение-аутентификатор (вручную или по URI)."
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            color: SelfvoyTheme.textSecondary
                            wrapMode: Text.WordWrap
                        }

                        GlassField {
                            Layout.fillWidth: true
                            label: "Секрет (base32)"
                            text: App ? App.twoFactorSecret : ""
                            readOnly: true
                        }

                        GlassField {
                            Layout.fillWidth: true
                            label: "otpauth:// URI"
                            text: App ? App.twoFactorUri : ""
                            readOnly: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "QR-код будет добавлен позже — пока введите секрет вручную."
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            color: SelfvoyTheme.textMuted
                            wrapMode: Text.WordWrap
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "2. Введите текущий код из приложения, чтобы включить 2FA."
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            color: SelfvoyTheme.textSecondary
                            wrapMode: Text.WordWrap
                        }

                        GlassField {
                            id: confirm2faField
                            Layout.fillWidth: true
                            label: "Код из приложения"
                            placeholderText: "6 цифр"
                            inputMethodHints: Qt.ImhDigitsOnly
                            enabled: !App.busy
                        }

                        GlassButton {
                            Layout.fillWidth: true
                            variant: "accent"
                            text: "Подтвердить и включить"
                            glyph: "✓"
                            busy: App ? App.busy : false
                            onClicked: {
                                App.confirm2fa(confirm2faField.text);
                                confirm2faField.text = "";
                            }
                        }
                    }

                    // --- Резервные коды (показываются один раз после подтверждения) ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceSm
                        visible: App && App.recoveryCodes.length > 0

                        Text {
                            Layout.fillWidth: true
                            text: "Сохраните резервные коды — они показываются только один раз!"
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            font.weight: Font.DemiBold
                            color: SelfvoyTheme.warning
                            wrapMode: Text.WordWrap
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: SelfvoyTheme.spaceMd
                            rowSpacing: 2
                            Repeater {
                                model: App ? App.recoveryCodes : []
                                delegate: Text {
                                    text: modelData
                                    font.family: "monospace"
                                    font.pixelSize: SelfvoyTheme.fontSmall
                                    color: SelfvoyTheme.textPrimary
                                    textFormat: Text.PlainText
                                }
                            }
                        }
                    }

                    // --- Состояние: включена — отключение и доверенные устройства ---
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: SelfvoyTheme.spaceSm
                        visible: App && App.twoFactorEnabled

                        Text {
                            Layout.fillWidth: true
                            text: "Осталось резервных кодов: " + (App ? App.recoveryCodesLeft : 0)
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            color: SelfvoyTheme.textMuted
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: SelfvoyTheme.spaceSm

                            Text {
                                Layout.fillWidth: true
                                text: "Доверенные устройства"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontSmall
                                font.weight: Font.DemiBold
                                color: SelfvoyTheme.textPrimary
                            }

                            GlassButton {
                                variant: "quiet"
                                text: "Обновить"
                                implicitHeight: 30
                                onClicked: App.loadTrustedDevices()
                            }
                        }

                        Repeater {
                            model: App ? App.trustedDevices : []
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                height: deviceRow.implicitHeight + SelfvoyTheme.spaceMd
                                radius: SelfvoyTheme.radiusField
                                color: Qt.rgba(1, 1, 1, 0.04)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.08)

                                RowLayout {
                                    id: deviceRow
                                    anchors.fill: parent
                                    anchors.margins: SelfvoyTheme.spaceSm
                                    spacing: SelfvoyTheme.spaceSm

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: modelData.device || "устройство"
                                            font.family: SelfvoyTheme.fontFamily
                                            font.pixelSize: SelfvoyTheme.fontSmall
                                            color: SelfvoyTheme.textPrimary
                                        }
                                        Text {
                                            text: [/^:/.test(modelData.remote_addr || "") ? "" : (modelData.remote_addr || ""),
                                                   F.parse(modelData.expires_at) ? "доверено до " + F.dateLabel(F.parse(modelData.expires_at)) : ""]
                                                  .filter(x => x.length > 0).join("  ·  ")
                                            font.family: SelfvoyTheme.fontFamily
                                            font.pixelSize: SelfvoyTheme.fontMicro
                                            color: SelfvoyTheme.textMuted
                                        }
                                    }

                                    GlassButton {
                                        variant: "quiet"
                                        text: "Отозвать"
                                        implicitHeight: 30
                                        onClicked: App.revokeTrustedDevice(modelData.id)
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: (App ? App.trustedDevices.length : 0) === 0
                            text: "Нет доверенных устройств."
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            color: SelfvoyTheme.textMuted
                        }

                        GlassButton {
                            Layout.fillWidth: true
                            variant: "quiet"
                            text: "Отозвать все доверенные устройства"
                            onClicked: App.revokeAllTrustedDevices()
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Отключение 2FA требует подтверждения паролем."
                            font.family: SelfvoyTheme.fontFamily
                            font.pixelSize: SelfvoyTheme.fontMicro
                            color: SelfvoyTheme.textMuted
                            wrapMode: Text.WordWrap
                        }

                        GlassField {
                            id: disable2faPassword
                            Layout.fillWidth: true
                            label: "Пароль для отключения"
                            echoMode: TextInput.Password
                        }

                        GlassButton {
                            Layout.fillWidth: true
                            variant: "quiet"
                            text: "Отключить 2FA"
                            glyph: "🔓"
                            busy: App ? App.busy : false
                            onClicked: {
                                App.disable2fa(disable2faPassword.text);
                                disable2faPassword.text = "";
                            }
                        }
                    }
                }
            }

            // --------------------------------------- Разрешения инструментов (этап 8)
            GlassPanel {
                visible: page.section === "permissions"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceMd

                    SectionTitle { title: "Разрешения инструментов"; titleSize: SelfvoyTheme.fontTitle }
                    Text {
                        Layout.fillWidth: true
                        text: "Selfvoy лишь формирует намерение — исполняет сервер. "
                              + "Опасные операции (сообщения, письма, бронирование) по умолчанию "
                              + "требуют вашего подтверждения."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: App ? App.toolPermissions : []

                        delegate: RowLayout {
                            id: toolRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: SelfvoyTheme.spaceMd

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                RowLayout {
                                    spacing: SelfvoyTheme.spaceXs
                                    Text {
                                        text: F.toolTitle(toolRow.modelData.tool)
                                        font.family: SelfvoyTheme.fontFamily
                                        font.pixelSize: SelfvoyTheme.fontSmall
                                        font.weight: Font.DemiBold
                                        color: SelfvoyTheme.textPrimary
                                    }
                                    Rectangle {
                                        visible: toolRow.modelData.dangerous === true
                                        radius: SelfvoyTheme.radiusPill
                                        implicitWidth: dangerLabel.implicitWidth + 14
                                        implicitHeight: 18
                                        color: Qt.rgba(0.98, 0.75, 0.14, 0.12)
                                        border.width: 1
                                        border.color: Qt.rgba(0.98, 0.75, 0.14, 0.40)
                                        Text {
                                            id: dangerLabel
                                            anchors.centerIn: parent
                                            text: "опасный"
                                            font.family: SelfvoyTheme.fontFamily
                                            font.pixelSize: SelfvoyTheme.fontMicro
                                            color: SelfvoyTheme.warning
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: F.toolDescription(toolRow.modelData.tool, toolRow.modelData.description)
                                    font.family: SelfvoyTheme.fontFamily
                                    font.pixelSize: SelfvoyTheme.fontMicro
                                    color: SelfvoyTheme.textMuted
                                    wrapMode: Text.WordWrap
                                }
                            }

                            // Переключатель режима: разрешить / спрашивать / запретить.
                            // Сегментированный трек, как на Android/iOS: яркая акцентная
                            // кнопка в каждой строке слишком шумит в списке из 8 инструментов.
                            Rectangle {
                                implicitWidth: segRow.implicitWidth + 6
                                implicitHeight: 34
                                radius: height / 2
                                color: Qt.rgba(1, 1, 1, SelfvoyTheme.glassFill)
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, SelfvoyTheme.glassBorder)

                                Row {
                                    id: segRow
                                    anchors.centerIn: parent
                                    spacing: 2

                                    Repeater {
                                        model: [
                                            { mode: "allow", label: "Разрешить" },
                                            { mode: "ask", label: "Спрашивать" },
                                            { mode: "deny", label: "Запретить" }
                                        ]

                                        delegate: Rectangle {
                                            id: seg
                                            required property var modelData
                                            readonly property bool active: toolRow.modelData.mode === modelData.mode
                                            readonly property bool isDeny: modelData.mode === "deny"
                                            width: segLabel.implicitWidth + 2 * SelfvoyTheme.spaceMd
                                            height: 28
                                            radius: height / 2
                                            color: active
                                                   ? Qt.rgba(isDeny ? 0.97 : 0.545, isDeny ? 0.44 : 0.36, isDeny ? 0.44 : 0.965,
                                                             isDeny ? 0.30 : 0.35)
                                                   : (segMouse.containsMouse ? Qt.rgba(1, 1, 1, SelfvoyTheme.glassFillHover)
                                                                             : "transparent")
                                            Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }

                                            Text {
                                                id: segLabel
                                                anchors.centerIn: parent
                                                text: seg.modelData.label
                                                font.family: SelfvoyTheme.fontFamily
                                                font.pixelSize: SelfvoyTheme.fontSmall
                                                font.weight: seg.active ? Font.DemiBold : Font.Normal
                                                color: seg.active ? SelfvoyTheme.textPrimary : SelfvoyTheme.textSecondary
                                            }

                                            MouseArea {
                                                id: segMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: if (!seg.active)
                                                               App.setToolPermission(toolRow.modelData.tool, seg.modelData.mode)
                                            }

                                            Accessible.role: Accessible.RadioButton
                                            Accessible.name: seg.modelData.label
                                            Accessible.checked: seg.active
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // --------------------------------------------- Интеграции (этап 9)
            GlassPanel {
                visible: page.section === "integrations"
                Layout.fillWidth: true
                backdrop: page.Window.window ? page.Window.window.contentItem : null

                ColumnLayout {
                    anchors.fill: parent
                    spacing: SelfvoyTheme.spaceMd

                        SectionTitle { title: "Интеграции"; titleSize: SelfvoyTheme.fontTitle }
                    Text {
                        Layout.fillWidth: true
                        text: "Подключите Google Календарь и Gmail через официальный OAuth. "
                              + "Токены хранятся на сервере зашифрованными и не передаются "
                              + "клиенту; доступ можно отозвать в любой момент."
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontMicro
                        color: SelfvoyTheme.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: App ? App.integrationProviders : []

                        delegate: RowLayout {
                            id: providerRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: SelfvoyTheme.spaceMd

                            // Активное подключение этого провайдера (или null).
                            property var connection: {
                                const list = App ? App.integrations : []
                                for (let i = 0; i < list.length; ++i) {
                                    if (list[i].provider === providerRow.modelData.provider)
                                        return list[i]
                                }
                                return null
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                RowLayout {
                                    spacing: SelfvoyTheme.spaceXs
                                    Text {
                                        text: providerRow.modelData.name
                                        font.family: SelfvoyTheme.fontFamily
                                        font.pixelSize: SelfvoyTheme.fontSmall
                                        font.weight: Font.DemiBold
                                        color: SelfvoyTheme.textPrimary
                                    }
                                    Rectangle {
                                        visible: providerRow.connection !== null
                                        radius: SelfvoyTheme.radiusPill
                                        implicitWidth: stateLabel.implicitWidth + 14
                                        implicitHeight: 18
                                        color: providerRow.connection
                                               && providerRow.connection.status === "active"
                                               ? Qt.rgba(0.29, 0.87, 0.50, 0.12)
                                               : Qt.rgba(0.98, 0.75, 0.14, 0.12)
                                        border.width: 1
                                        border.color: providerRow.connection
                                                      && providerRow.connection.status === "active"
                                                      ? Qt.rgba(0.29, 0.87, 0.50, 0.40)
                                                      : Qt.rgba(0.98, 0.75, 0.14, 0.40)
                                        Text {
                                            id: stateLabel
                                            anchors.centerIn: parent
                                            text: providerRow.connection
                                                  ? (providerRow.connection.status === "active"
                                                     ? "подключено" : providerRow.connection.status)
                                                  : ""
                                            font.family: SelfvoyTheme.fontFamily
                                            font.pixelSize: SelfvoyTheme.fontMicro
                                            color: providerRow.connection
                                                   && providerRow.connection.status === "active"
                                                   ? SelfvoyTheme.success : SelfvoyTheme.warning
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: providerRow.connection
                                          && providerRow.connection.account
                                          ? providerRow.modelData.description + " · "
                                            + providerRow.connection.account
                                          : providerRow.modelData.description
                                    font.family: SelfvoyTheme.fontFamily
                                    font.pixelSize: SelfvoyTheme.fontMicro
                                    color: SelfvoyTheme.textMuted
                                    wrapMode: Text.WordWrap
                                }
                            }

                            RowLayout {
                                spacing: SelfvoyTheme.spaceXs

                                GlassButton {
                                    visible: providerRow.connection === null
                                    text: "Подключить"
                                    variant: "accent"
                                    implicitHeight: 32
                                    onClicked: App.beginIntegration(providerRow.modelData.provider)
                                }
                                GlassButton {
                                    visible: providerRow.connection !== null
                                    text: "Синхронизировать"
                                    variant: "quiet"
                                    implicitHeight: 32
                                    onClicked: App.syncIntegration(providerRow.connection.id)
                                }
                                GlassButton {
                                    visible: providerRow.connection !== null
                                    text: "Отключить"
                                    variant: "quiet"
                                    implicitHeight: 32
                                    onClicked: App.revokeIntegration(providerRow.connection.id)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
