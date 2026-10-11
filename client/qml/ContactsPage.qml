import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura

/*
  ContactsPage — контакты и их Ауры (v3).

  Слева — список собеседников (те же chat.list данные), справа — детальная
  карточка выбранного контакта: написать, запустить A2A-переговоры.
  В компактной ширине — только список, карточка раскрывается под ним.
*/
Item {
    id: page

    signal openChat(int chatId)
    // Пользователь нажал «Переговоры» — уводим на экран Negotiations с целью.
    signal startNegotiation(string target)

    property int selectedChatId: -1
    readonly property var contacts: App ? App.chats : []
    readonly property var backdropRef: page.Window.window ? page.Window.window.contentItem : null

    readonly property var selected: {
        const list = page.contacts
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === page.selectedChatId) return list[i]
        }
        return null
    }

    Component.onCompleted: if (App && page.contacts.length > 0 && page.selectedChatId < 0)
                               page.selectedChatId = page.contacts[0].id

    RowLayout {
        anchors.fill: parent
        anchors.margins: AuraTheme.spaceLg
        spacing: AuraTheme.spaceMd

        // ------------------------------------------------------- список
        GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumWidth: 420
            padding: AuraTheme.spaceMd
            backdrop: page.backdropRef

            ColumnLayout {
                anchors.fill: parent
                spacing: AuraTheme.spaceSm

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "Контакты"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontTitle
                        font.weight: Font.DemiBold
                        color: AuraTheme.textPrimary
                    }
                    Text {
                        text: page.contacts.length + ""
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontMicro
                        color: AuraTheme.textMuted
                    }
                }

                ListView {
                    id: contactList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: AuraTheme.spaceSm
                    model: page.contacts

                    Text {
                        anchors.centerIn: parent
                        visible: contactList.count === 0
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: "Пока никого. Чаты с собеседниками появляются здесь —\nили начните переговоры по email."
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textMuted
                        lineHeight: 1.4
                    }

                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        width: contactList.width
                        height: 64
                        radius: AuraTheme.radiusField

                        readonly property bool selected: page.selectedChatId === row.modelData.id

                        color: row.selected ? AuraTheme.accentA(0.14)
                                            : (contactArea.containsMouse ? Qt.rgba(1, 1, 1, 0.10)
                                                                         : Qt.rgba(1, 1, 1, 0.05))
                        border.width: 1
                        border.color: row.selected ? AuraTheme.accentA(0.45)
                                                   : Qt.rgba(1, 1, 1, AuraTheme.glassBorder)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: AuraTheme.spaceSm
                            anchors.rightMargin: AuraTheme.spaceSm
                            spacing: AuraTheme.spaceSm

                            AuraAvatar {
                                name: row.modelData ? String(row.modelData.title || "?") : "?"
                                agent: true
                                size: 38
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: row.modelData ? String(row.modelData.title || "Без названия") : ""
                                    font.family: AuraTheme.fontFamily
                                    font.pixelSize: AuraTheme.fontBody
                                    font.weight: Font.DemiBold
                                    color: AuraTheme.textPrimary
                                }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: row.modelData ? String(row.modelData.last_message_body || "Аура на связи") : ""
                                    font.family: AuraTheme.fontFamily
                                    font.pixelSize: AuraTheme.fontMicro
                                    color: AuraTheme.textSecondary
                                }
                            }

                            // Статусная точка (v3): активен/ждёт
                            Rectangle {
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                radius: 4
                                color: row.selected ? AuraTheme.success : AuraTheme.warning
                            }
                        }

                        MouseArea {
                            id: contactArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.selectedChatId = row.modelData.id
                        }
                    }
                }
            }
        }

        // ----------------------------------------------- детальная карточка
        GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 300
            padding: AuraTheme.spaceLg
            backdrop: page.backdropRef

            ColumnLayout {
                anchors.fill: parent
                spacing: AuraTheme.spaceMd

                Item { Layout.fillHeight: page.selected === null }  // пустое состояние — по центру

                AuraAvatar {
                    Layout.alignment: Qt.AlignHCenter
                    visible: page.selected !== null
                    name: page.selected ? String(page.selected.title || "?") : "?"
                    agent: true
                    size: 72
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    visible: page.selected !== null
                    text: page.selected ? String(page.selected.title || "") : ""
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontTitle
                    font.weight: Font.DemiBold
                    color: AuraTheme.textPrimary
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: page.selected !== null
                    text: "Аура доступна"
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    color: AuraTheme.online
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: page.selected !== null
                    spacing: AuraTheme.spaceSm

                    Text {
                        Layout.fillWidth: true
                        text: "Последнее сообщение"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontMicro
                        font.weight: Font.DemiBold
                        color: AuraTheme.textSecondary
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: page.selected
                              ? String(page.selected.last_message_body || "Сообщений пока нет")
                              : ""
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textPrimary
                        lineHeight: 1.3
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: AuraTheme.spaceSm
                    visible: page.selected === null

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: "Выберите контакт слева"
                        font.family: AuraTheme.fontFamily
                        font.pixelSize: AuraTheme.fontSmall
                        color: AuraTheme.textMuted
                    }
                }

                Item { Layout.fillHeight: true }

                GlassButton {
                    Layout.fillWidth: true
                    visible: page.selected !== null
                    variant: "accent"
                    text: "Написать"
                    glyph: "→"
                    onClicked: page.openChat(page.selected ? page.selected.id : 0)
                }

                GlassButton {
                    Layout.fillWidth: true
                    visible: page.selected !== null
                    text: "Начать переговоры"
                    glyph: "✦"
                    onClicked: page.startNegotiation(page.selected ? String(page.selected.title || "") : "")
                }
            }
        }
    }
}
