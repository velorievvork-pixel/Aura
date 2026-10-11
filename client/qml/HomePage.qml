import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura
import "Format.js" as F

/*
  HomePage — главный экран после входа (макет Home).

  Слева: приветствие, строка «Что нужно сделать?» с микрофоном (agent.ask без
  чата → ответ в «Моей Ауре»), быстрые действия и ближайшие задачи.
  Справа: статус системы, карточка «Ваша Аура активна» со свечением,
  ожидающие подтверждения и лента последних действий (уведомления).
*/
Item {
    id: page

    signal navigate(string to)

    readonly property Item backdropRef: page.Window.window ? page.Window.window.contentItem : null
    property date now: new Date()

    readonly property var openTasks: {
        const list = (App ? App.tasks : []).filter(t => t.status === "pending")
        list.sort((a, b) => (F.taskWhen(a) || "9999").localeCompare(F.taskWhen(b) || "9999"))
        return list
    }
    readonly property int doneToday: (App ? App.tasks : []).filter(t => {
        const d = F.parse(t.completed_at)
        return t.status === "done" && d && F.sameDay(d, page.now)
    }).length

    Timer { interval: 60000; running: true; repeat: true; onTriggered: page.now = new Date() }

    function ask(text) {
        const t = text.trim()
        if (!t || !App) return
        App.askAgentGlobal(t)
        command.text = ""
        page.navigate("aura")
    }

    // Голос: распознанный текст ложится в строку команды.
    Connections {
        target: App
        function onTranscriptChanged() { if (App.transcript) command.text = App.transcript }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: grid.implicitHeight + 96
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        GridLayout {
            id: grid
            x: 48
            y: 44
            width: parent.width - 96
            columns: width > 900 ? 2 : 1
            columnSpacing: 40
            rowSpacing: 28

            // ========================================================== левая колонка
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 13
                Layout.alignment: Qt.AlignTop
                spacing: 26

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    text: AuraTheme.greeting(page.now) + ",\n" + F.firstName(App ? App.userName : "")
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontDisplay
                    font.weight: Font.DemiBold
                    font.letterSpacing: -1.2
                    lineHeightMode: Text.FixedHeight
                    lineHeight: AuraTheme.fontDisplay * 1.08
                    color: AuraTheme.textPrimary
                    wrapMode: Text.WordWrap
                }

                // Строка команды
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    implicitHeight: 76
                    radius: height / 2
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.09) }
                        GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.03) }
                    }
                    border.width: 1
                    border.color: command.activeFocus ? AuraTheme.accentA(0.7) : Qt.rgba(1, 1, 1, 0.18)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 30
                        anchors.rightMargin: 14
                        spacing: 10

                        TextField {
                            id: command
                            Layout.fillWidth: true
                            background: null
                            placeholderText: "Что нужно сделать?"
                            placeholderTextColor: AuraTheme.textPrimary
                            color: AuraTheme.textPrimary
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: 21
                            font.weight: Font.Medium
                            selectionColor: AuraTheme.accentA(0.5)
                            onAccepted: page.ask(text)
                        }

                        GlassButton {
                            variant: "accent"
                            implicitWidth: 50
                            implicitHeight: 50
                            glyphSize: 21
                            symbol: command.text.trim().length > 0 ? "send-horizontal"
                                    : (App && App.voiceState === "listening" ? "square" : "mic")
                            busy: App ? (App.busy || App.voiceState === "processing") : false
                            ToolTip.visible: hovered
                            ToolTip.delay: 500
                            ToolTip.text: command.text.trim().length > 0 ? "Отправить Ауре"
                                          : (App && !App.micAvailable ? "Микрофон не найден" : "Сказать голосом")
                            onClicked: {
                                if (command.text.trim().length > 0) page.ask(command.text)
                                else if (App.voiceState === "listening") App.stopListening()
                                else App.startListening()
                            }
                        }
                    }
                }

                // Быстрые действия
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width > 760 ? 4 : 2
                    columnSpacing: 12
                    rowSpacing: 12

                    Repeater {
                        model: [
                            { label: "Назначить\nвстречу", go: "negotiations" },
                            { label: "Моё\nрасписание", ask: "Что у меня сегодня в расписании?" },
                            { label: "Написать\nписьмо", fill: "Напиши письмо " },
                            { label: "Создать\nнапоминание", fill: "Напомни мне " }
                        ]
                        delegate: GlassButton {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 62
                            radius: 16
                            alignLeft: true
                            leftPadding: 16
                            textSize: 14
                            text: modelData.label
                            onClicked: {
                                if (modelData.go) page.navigate(modelData.go)
                                else if (modelData.ask) page.ask(modelData.ask)
                                else { command.text = modelData.fill; command.forceActiveFocus() }
                            }
                        }
                    }
                }

                // Ближайшие задачи
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.bottomMargin: 8
                        Text {
                            Layout.fillWidth: true
                            text: "Ближайшие задачи"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontLead
                            font.weight: Font.Bold
                            color: AuraTheme.textPrimary
                        }
                        LinkText { text: "Все задачи"; onClicked: page.navigate("tasks") }
                    }

                    Text {
                        visible: page.openTasks.length === 0
                        text: "Задач нет — можно попросить Ауру что-нибудь запланировать."
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textMuted
                    }

                    Repeater {
                        model: page.openTasks.slice(0, 4)
                        delegate: Item {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: 52

                            Rectangle {
                                anchors.fill: parent
                                radius: AuraTheme.radiusSmall
                                color: rowArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 14
                                Rectangle {
                                    implicitWidth: 34; implicitHeight: 34; radius: 17
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Qt.rgba(1, 1, 1, 0.18)
                                    Icon {
                                        anchors.centerIn: parent
                                        size: 16
                                        name: ["user", "file-text", "presentation"][index % 3]
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    font.family: AuraTheme.fontFamily
                                    font.pixelSize: AuraTheme.fontBody
                                    font.weight: Font.DemiBold
                                    color: AuraTheme.textPrimary
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: {
                                        const when = F.taskWhen(modelData)
                                        if (!when) return ""
                                        const d = F.parse(when)
                                        return F.sameDay(d, page.now) ? F.time(when) : F.dayPhrase(when) + ", " + F.time(when)
                                    }
                                    font.family: AuraTheme.fontFamily
                                    font.pixelSize: AuraTheme.fontSmall
                                    color: AuraTheme.textMuted
                                }
                            }
                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: page.navigate("tasks")
                            }
                        }
                    }
                }
            }

            // ========================================================== правая колонка
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 10
                Layout.alignment: Qt.AlignTop
                spacing: 30

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 8
                    Rectangle {
                        implicitWidth: 7; implicitHeight: 7; radius: 3.5
                        color: App && App.connected ? AuraTheme.success : AuraTheme.warning
                    }
                    Text {
                        text: App && App.connected ? "Все системы работают" : "Нет связи с сервером"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall - 1
                        color: AuraTheme.textSecondary
                    }
                }

                // «Ваша Аура активна»
                GlassPanel {
                    Layout.fillWidth: true
                    radius: 26
                    glow: true
                    glowColor: AuraTheme.gradMid
                    tint: "#B4AAFF"
                    fill: 0.26
                    padding: 26
                    backdrop: page.backdropRef

                    ColumnLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 6
                        Text {
                            text: App && App.connected ? "Ваша Аура активна" : "Аура не на связи"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontTitle
                            font.weight: Font.DemiBold
                            color: AuraTheme.textPrimary
                        }
                        Text {
                            text: page.doneToday + " " + F.plural(page.doneToday, "задача выполнена", "задачи выполнены", "задач выполнено") + " сегодня"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontSmall
                            color: AuraTheme.textSecondary
                        }
                        RowLayout {
                            Layout.topMargin: 12
                            spacing: 10
                            GlassButton {
                                implicitWidth: 38; implicitHeight: 38
                                symbol: "user"
                                ToolTip.visible: hovered; ToolTip.text: "Контакты"
                                onClicked: page.navigate("contacts")
                            }
                            GlassButton {
                                implicitWidth: 38; implicitHeight: 38
                                symbol: "message-square"
                                ToolTip.visible: hovered; ToolTip.text: "Моя Аура"
                                onClicked: page.navigate("aura")
                            }
                        }
                    }
                }

                // Ждут подтверждения — барьер безопасности на виду
                GlassButton {
                    Layout.fillWidth: true
                    visible: App && App.confirmations.length > 0
                    implicitHeight: 52
                    radius: 18
                    alignLeft: true
                    leftPadding: 18
                    symbol: "triangle-alert"
                    text: App ? "Ждут вашего подтверждения: " + App.confirmations.length : ""
                    onClicked: page.navigate("confirmations")
                }

                // Последние переговоры
                GlassButton {
                    Layout.fillWidth: true
                    visible: App && App.negotiation && App.negotiation.target !== undefined
                    implicitHeight: 52
                    radius: 18
                    alignLeft: true
                    leftPadding: 18
                    symbol: "handshake"
                    text: !App || !App.negotiation.target ? ""
                          : App.negotiation.accepted
                            ? (App.negotiation.booking ? "Забронировано: " : "Ждёт решения: ")
                              + (App.negotiation.place ? App.negotiation.place.name : "") + ", "
                              + F.dayPhrase(App.negotiation.slot.start) + " " + F.time(App.negotiation.slot.start)
                            : "Переговоры: нужен ваш выбор"
                    onClicked: page.navigate("negotiations")
                }

                // Недавние действия
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 15

                    Text {
                        text: "Недавние действия Ауры"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontLead
                        font.weight: Font.Bold
                        color: AuraTheme.textPrimary
                    }
                    Text {
                        visible: !App || App.notifications.length === 0
                        text: "Здесь появятся действия Ауры: напоминания, переговоры, подтверждения."
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textMuted
                    }
                    Repeater {
                        model: App ? App.notifications.slice(0, 7) : []
                        delegate: Text {
                            required property var modelData
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            textFormat: Text.StyledText
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontSmall
                            color: AuraTheme.textSecondary
                            readonly property string detail: F.notificationDetail(modelData)
                            text: "<font color='" + AuraTheme.textMuted + "'>" + F.time(modelData.created_at) + "</font>&nbsp; "
                                  + F.escape(modelData.title) + (detail ? ": <b><font color='" + AuraTheme.textPrimary + "'>"
                                  + F.escape(detail) + "</font></b>" : "")
                        }
                    }
                    LinkText {
                        text: "Открыть диалог с Аурой  →"
                        onClicked: page.navigate("aura")
                    }
                }
            }
        }
    }
}
