import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura

/*
  NegotiationsPage — A2A-переговоры двух Аур (v3).

  Форма запуска agent.negotiate (цель, тема, длительность, окно) и карточка
  результата: accepted / слот / место / встречные предложения. Правило
  продукта: максимум 5 раундов, затем эскалация человеку — подпись внизу.
*/
Item {
    id: page

    // Цель, предзаполненная из ContactsPage.
    property string initialTarget: ""

    readonly property var backdropRef: page.Window.window ? page.Window.window.contentItem : null
    readonly property var result: App ? App.negotiation : ({})

    onInitialTargetChanged: if (initialTarget.length > 0) targetField.text = initialTarget

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

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Переговоры Аур"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: 30
                    font.weight: Font.DemiBold
                    color: AuraTheme.textPrimary
                }
                Rectangle {
                    width: roundLabel.width + 16
                    height: 22
                    radius: AuraTheme.radiusPill
                    color: AuraTheme.accentA(0.18)
                    border.width: 1
                    border.color: AuraTheme.accentA(0.35)
                    Text {
                        id: roundLabel
                        anchors.centerIn: parent
                        text: "до 5 раундов"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        color: AuraTheme.lilac
                    }
                }
            }

            // ------------------------------------------- карточки агентов
            RowLayout {
                Layout.fillWidth: true
                spacing: AuraTheme.spaceMd

                GlassPanel {
                    Layout.fillWidth: true
                    padding: AuraTheme.spaceMd
                    backdrop: page.backdropRef

                    ColumnLayout {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: AuraTheme.spaceSm

                        // «Звуковая волна» агента (v3): пульсирующие столбики
                        Row {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 3
                            Repeater {
                                model: 7
                                Rectangle {
                                    required property int index
                                    width: 3
                                    radius: 2
                                    color: AuraTheme.accent
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 6 + ((index * 5) % 14)
                                    SequentialAnimation on opacity {
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.35; duration: 600 + index * 120; easing.type: Easing.InOutSine }
                                        NumberAnimation { to: 1.0; duration: 600 + index * 120; easing.type: Easing.InOutSine }
                                    }
                                }
                            }
                        }

                        AuraAvatar {
                            Layout.alignment: Qt.AlignHCenter
                            name: App ? App.userName : ""
                            size: 56
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: App ? App.userName : "Вы"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontBody
                            font.weight: Font.DemiBold
                            color: AuraTheme.textPrimary
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "ваша Аура"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontMicro
                            color: AuraTheme.textMuted
                        }
                    }
                }

                Text {
                    text: "⇄"
                    font.pixelSize: 22
                    color: AuraTheme.lilac
                }

                GlassPanel {
                    Layout.fillWidth: true
                    padding: AuraTheme.spaceMd
                    backdrop: page.backdropRef

                    ColumnLayout {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: AuraTheme.spaceSm

                        Row {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 3
                            Repeater {
                                model: 7
                                Rectangle {
                                    required property int index
                                    width: 3
                                    radius: 2
                                    color: AuraTheme.accentSoft
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 6 + ((index * 7) % 14)
                                    SequentialAnimation on opacity {
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.35; duration: 700 + index * 110; easing.type: Easing.InOutSine }
                                        NumberAnimation { to: 1.0; duration: 700 + index * 110; easing.type: Easing.InOutSine }
                                    }
                                }
                            }
                        }

                        AuraAvatar {
                            Layout.alignment: Qt.AlignHCenter
                            name: targetField.text.length > 0 ? targetField.text : "?"
                            agent: true
                            size: 56
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: targetField.text.length > 0 ? targetField.text : "собеседник"
                            elide: Text.ElideRight
                            Layout.maximumWidth: 220
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontBody
                            font.weight: Font.DemiBold
                            color: AuraTheme.textPrimary
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: "Аура собеседника"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontMicro
                            color: AuraTheme.textMuted
                        }
                    }
                }
            }

            // ------------------------------------------------ форма запуска
            GlassPanel {
                Layout.fillWidth: true
                padding: AuraTheme.spaceMd
                glow: true
                fill: AuraTheme.accentFill * 0.4
                backdrop: page.backdropRef

                ColumnLayout {
                    anchors.fill: parent
                    spacing: AuraTheme.spaceSm

                    GlassField {
                        id: targetField
                        Layout.fillWidth: true
                        label: "Собеседник (email или имя)"
                        icon: "👤"
                        placeholderText: "anya@example.com"
                        enabled: !(App && App.busy)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: AuraTheme.spaceSm

                        GlassField {
                            id: topicField
                            Layout.fillWidth: true
                            label: "Тема (необязательно)"
                            placeholderText: "встреча на выходных"
                            enabled: !(App && App.busy)
                        }

                        GlassField {
                            id: durationField
                            Layout.preferredWidth: 120
                            label: "Длит., мин"
                            placeholderText: "60"
                            validator: IntValidator { bottom: 15; top: 480 }
                            enabled: !(App && App.busy)
                        }

                        GlassField {
                            id: windowField
                            Layout.preferredWidth: 110
                            label: "Окно, ч"
                            placeholderText: "48"
                            validator: IntValidator { bottom: 1; top: 336 }
                            enabled: !(App && App.busy)
                        }
                    }

                    GlassButton {
                        Layout.fillWidth: true
                        variant: "accent"
                        text: "Запустить переговоры"
                        glyph: "✦"
                        busy: App ? App.busy : false
                        enabled: targetField.text.trim().length > 0 && !(App && App.busy)
                        onClicked: App.negotiateAgent(targetField.text.trim(),
                                                      topicField.text.trim(),
                                                      parseInt(durationField.text) || 0,
                                                      parseInt(windowField.text) || 0)
                    }
                }
            }

            // ---------------------------------------------- результат (таймлайн)
            GlassPanel {
                Layout.fillWidth: true
                padding: AuraTheme.spaceMd
                visible: page.result && Object.keys(page.result).length > 0
                backdrop: page.backdropRef

                ColumnLayout {
                    anchors.fill: parent
                    spacing: AuraTheme.spaceSm

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: page.result && page.result.accepted === true
                                  ? "Ауры договорились" : "Не сошлись"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontBody
                            font.weight: Font.DemiBold
                            color: page.result && page.result.accepted === true
                                   ? AuraTheme.success : AuraTheme.warning
                        }
                    }

                    // Таймлайн раундов (v3): номер + карточка
                    Repeater {
                        model: {
                            const lines = []
                            const r = page.result
                            if (!r) return lines
                            if (r.slot) {
                                lines.push({ n: 1, text: "Найден общий слот: " + String(r.slot).replace("T", " ").substring(0, 16), done: true })
                            }
                            if (r.place) {
                                lines.push({ n: lines.length + 1, text: "Место: " + String(r.place), done: true })
                            }
                            const counters = r.counter_slots
                            if (counters && counters.length > 0) {
                                lines.push({ n: lines.length + 1, text: "Встречных предложений: " + counters.length, done: true })
                            }
                            if (r.message) {
                                lines.push({ n: lines.length + 1, text: String(r.message), done: r.accepted === true })
                            }
                            return lines
                        }

                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: AuraTheme.spaceSm

                            Rectangle {
                                Layout.preferredWidth: 26
                                Layout.preferredHeight: 26
                                radius: 13
                                color: modelData.done ? AuraTheme.accent : AuraTheme.accentA(0.2)
                                border.width: 1
                                border.color: modelData.done ? "transparent" : AuraTheme.accentA(0.5)
                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.done ? "✓" : modelData.n
                                    font.family: AuraTheme.fontFamily
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                    color: modelData.done ? AuraTheme.textOnAccent : AuraTheme.textPrimary
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: modelData.text
                                font.family: AuraTheme.fontFamily
                                font.pixelSize: AuraTheme.fontSmall
                                color: AuraTheme.textSecondary
                            }
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: "Переговоры ограничены пятью раундами; если Ауры не сошлись —\nвопрос эскалируется вам, а опасные действия требуют подтверждения."
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontMicro
                color: AuraTheme.textMuted
                horizontalAlignment: Text.AlignHCenter
                lineHeight: 1.4
            }
        }
    }
}
