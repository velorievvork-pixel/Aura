import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura

/*
  HomePage — главный экран после входа (v3, макет «Good evening, Alex»).

  Приветствие по времени суток, карточка «Аура активна» со свечением,
  строка запроса с круглой кнопкой микрофона, чипы быстрых действий,
  ближайшие задачи и свежая активность (уведомления). Запрос уходит
  агенту без привязки к чату (agent.ask, chat_id=0) — ответ App.homeReply.
*/
Item {
    id: page

    // Навигация из компактной раскладки (в desktop её заменяет рельс).
    signal openChats()
    signal openTasks()
    signal openSettings()

    readonly property var backdropRef: page.Window.window ? page.Window.window.contentItem : null

    // Приветствие по времени суток
    readonly property string greeting: {
        const h = new Date().getHours()
        if (h >= 5 && h < 12) return "Доброе утро"
        if (h >= 12 && h < 18) return "Добрый день"
        if (h >= 18 && h < 23) return "Добрый вечер"
        return "Доброй ночи"
    }

    property var activeTasks: App ? (App.tasks || []).filter(function (t) {
        return t.status === "pending"
    }) : []

    Component.onCompleted: if (App) App.loadTasks()

    Connections {
        target: App
        function onTranscriptChanged() {
            // Голосовой ввод — черновик в строку запроса, отправка вручную.
            if (App && App.transcript.length > 0) promptField.text = App.transcript
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: AuraTheme.spaceLg
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            width: parent.width
            spacing: AuraTheme.spaceMd

            // ------------------------------------------------------ шапка
            RowLayout {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceMd

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: page.greeting + (App && App.userName ? ", " + App.userName : "")
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: 30
                        font.weight: Font.DemiBold
                        color: AuraTheme.textPrimary
                    }
                    Text {
                        text: App && App.connected ? "Все системы работают"
                                                    : "Нет соединения с сервером"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: App && App.connected ? AuraTheme.textSecondary : AuraTheme.warning
                    }
                }

                // Компактная раскладка: быстрая навигация вместо рельса.
                RowLayout {
                    visible: !(page.Window.window && page.Window.window.width >= AuraTheme.breakpointWide)
                    spacing: AuraTheme.spaceSm
                    GlassButton {
                        variant: "quiet"; text: ""; glyph: "💬"; implicitWidth: 44; implicitHeight: 40
                        onClicked: page.openChats()
                    }
                    GlassButton {
                        variant: "quiet"; text: ""; glyph: "✓"; implicitWidth: 44; implicitHeight: 40
                        onClicked: page.openTasks()
                    }
                    GlassButton {
                        variant: "quiet"; text: ""; glyph: "⚙"; implicitWidth: 44; implicitHeight: 40
                        onClicked: page.openSettings()
                    }
                }
            }

            // ------------------------------------- карточка «Аура активна»
            GlassPanel {
                Layout.fillWidth: true
                padding: AuraTheme.spaceMd
                glow: true
                fill: AuraTheme.accentFill * 0.4
                backdrop: page.backdropRef

                RowLayout {
                    anchors.fill: parent
                    spacing: AuraTheme.spaceMd

                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 20
                        color: AuraTheme.accentA(0.2)
                        border.width: 1
                        border.color: AuraTheme.accentA(0.45)
                        Text {
                            anchors.centerIn: parent
                            text: "✦"
                            color: AuraTheme.lilac
                            font.pixelSize: 17
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: "Ваша Аура активна"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontBody
                            font.weight: Font.DemiBold
                            color: AuraTheme.textPrimary
                        }
                        Text {
                            text: App && App.confirmations && App.confirmations.length > 0
                                  ? "Ждут подтверждения: " + App.confirmations.length
                                  : "Аура готова помочь — просто спросите"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontSmall
                            color: App && App.confirmations && App.confirmations.length > 0
                                   ? AuraTheme.warning : AuraTheme.textSecondary
                        }
                    }
                }
            }

            // --------------------------------------- строка запроса к Ауре
            GlassPanel {
                Layout.fillWidth: true
                padding: AuraTheme.spaceSm
                backdrop: page.backdropRef

                RowLayout {
                    anchors.fill: parent
                    spacing: AuraTheme.spaceSm

                    GlassField {
                        id: promptField
                        Layout.fillWidth: true
                        implicitHeight: 52
                        placeholderText: "Чем помочь? Например: договориться о встрече с Аней"
                        enabled: !(App && App.busy)
                        onAccepted: askButton.clicked()
                    }

                    // Круглая кнопка микрофона (v3)
                    GlassButton {
                        variant: App && App.voiceState === "listening" ? "danger" : "accent"
                        text: ""
                        glyph: App && App.voiceState === "listening" ? "■" : "🎙"
                        implicitWidth: 52
                        implicitHeight: 52
                        enabled: App && App.micAvailable && !(App && App.busy)
                        ToolTip.visible: hovered
                        ToolTip.text: App && App.micAvailable ? "Голосовой ввод" : "Микрофон недоступен"
                        onClicked: {
                            if (App) {
                                if (App.voiceState === "listening") App.stopListening()
                                else App.startListening()
                            }
                        }
                    }

                    GlassButton {
                        id: askButton
                        variant: "accent"
                        text: "Спросить Ауру"
                        glyph: "✦"
                        implicitHeight: 52
                        busy: App ? App.busy : false
                        enabled: promptField.text.trim().length > 0 && !(App && App.busy)
                        onClicked: {
                            App.askAgentGlobal(promptField.text)
                            promptField.text = ""
                        }
                    }
                }

                // Уровень записи / распознавание — тонкая полоса под полем
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 2
                    height: 3
                    radius: 2
                    visible: App && (App.voiceState === "listening" || App.voiceState === "processing")
                    color: "transparent"

                    Rectangle {
                        anchors.fill: parent
                        color: AuraTheme.accentA(0.25)
                        radius: 3
                    }
                    Rectangle {
                        // Пульсирующий индикатор «Аура слушает/думает»
                        property real lvl: App ? App.voiceLevel : 0
                        width: parent.width * (App && App.voiceState === "listening"
                                               ? 0.25 + lvl * 0.7 : 0.5)
                        height: parent.height
                        radius: 3
                        color: AuraTheme.accent
                        SequentialAnimation on opacity {
                            running: parent.visible
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.4; duration: 700; easing.type: Easing.InOutSine }
                            NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutSine }
                        }
                    }
                }
            }

            // ----------------------------------------- чипы быстрых действий
            Flow {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceSm

                Repeater {
                    model: [
                        "Спланировать встречу",
                        "Проверить расписание",
                        "Отправить письмо",
                        "Создать напоминание"
                    ]
                    delegate: GlassButton {
                        required property var modelData
                        text: modelData
                        variant: "glass"
                        implicitHeight: 36
                        onClicked: promptField.text = modelData.toLowerCase() + ": "
                    }
                }
            }

            // Последний ответ Ауры (chat_id=0)
            GlassPanel {
                Layout.fillWidth: true
                padding: AuraTheme.spaceMd
                visible: App && App.homeReply.length > 0
                backdrop: page.backdropRef

                ColumnLayout {
                    anchors.fill: parent
                    spacing: AuraTheme.spaceXs
                    Text {
                        text: "Аура ответила"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontMicro
                        font.weight: Font.DemiBold
                        color: AuraTheme.accentSoft
                    }
                    Text {
                        Layout.fillWidth: true
                        text: App ? App.homeReply : ""
                        wrapMode: Text.WordWrap
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textPrimary
                        lineHeight: 1.3
                    }
                }
            }

            // ------------------------------------------- ближайшие задачи
            GlassPanel {
                Layout.fillWidth: true
                padding: AuraTheme.spaceMd
                backdrop: page.backdropRef

                ColumnLayout {
                    anchors.fill: parent
                    spacing: AuraTheme.spaceSm

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: "Ближайшие задачи"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontSmall
                            font.weight: Font.DemiBold
                            color: AuraTheme.textSecondary
                        }
                        Text {
                            text: page.activeTasks.length + " активных"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontMicro
                            color: AuraTheme.textMuted
                        }
                    }

                    Repeater {
                        model: page.activeTasks.slice(0, 5)

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: AuraTheme.spaceSm

                            Rectangle {
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                radius: 4
                                color: modelData.remind_at ? AuraTheme.warning : AuraTheme.accent
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.title
                                elide: Text.ElideRight
                                font.family: AuraTheme.fontFamily
                                font.pixelSize: AuraTheme.fontSmall
                                color: AuraTheme.textPrimary
                            }
                            Text {
                                visible: modelData.remind_at && modelData.remind_at.length > 0
                                text: (modelData.remind_at || "").replace("T", " ").substring(5, 16)
                                font.family: AuraTheme.fontFamily
                                font.pixelSize: AuraTheme.fontMicro
                                color: AuraTheme.textMuted
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: page.activeTasks.length === 0
                        text: "Пока пусто — попросите Ауру создать напоминание"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontMicro
                        color: AuraTheme.textMuted
                    }
                }
            }

            // ---------------------------------------- свежая активность
            GlassPanel {
                Layout.fillWidth: true
                padding: AuraTheme.spaceMd
                backdrop: page.backdropRef

                ColumnLayout {
                    anchors.fill: parent
                    spacing: AuraTheme.spaceSm

                    Text {
                        Layout.fillWidth: true
                        text: "Последняя активность"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        font.weight: Font.DemiBold
                        color: AuraTheme.textSecondary
                    }

                    Repeater {
                        model: App ? App.notifications.slice(0, 5) : []

                        delegate: RowLayout {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: AuraTheme.spaceSm

                            Rectangle {
                                Layout.preferredWidth: 4
                                Layout.preferredHeight: 16
                                radius: 2
                                color: index % 2 === 0 ? AuraTheme.accent : AuraTheme.accentSoft
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (modelData.title || "") + (modelData.body ? " — " + modelData.body : "")
                                elide: Text.ElideRight
                                font.family: AuraTheme.fontFamily
                                font.pixelSize: AuraTheme.fontSmall
                                color: AuraTheme.textSecondary
                            }
                            Text {
                                visible: (modelData.created_at || "").length > 0
                                text: (modelData.created_at || "").replace("T", " ").substring(5, 16)
                                font.family: AuraTheme.fontFamily
                                font.pixelSize: AuraTheme.fontMicro
                                color: AuraTheme.textMuted
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: App && App.notifications.length === 0
                        text: "Уведомлений пока нет"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontMicro
                        color: AuraTheme.textMuted
                    }
                }
            }
        }
    }
}
