import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura

/*
  DesktopShell — широкая раскладка (v3).

  Рельс навигации как в макетах: эмблема Aura + Home / My Aura / Переговоры /
  Задачи / Контакты / Настройки; активный пункт — lilac-индикатор слева.
  Подтверждения и уведомления — компактные кнопки с бейджами внизу рельса
  (барьер безопасности виден из любого раздела). Чаты — master-detail.
*/
Item {
    id: shell

    // Выбранный чат (общий с Main.qml — переживает смену раскладки).
    property int selectedChatId: 0
    property int currentTab: 0

    signal chatSelected(int chatId)

    readonly property var backdropRef: shell.Window.window ? shell.Window.window.contentItem : null

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------- рельс навигации
        GlassPanel {
            Layout.preferredWidth: AuraTheme.railWidth
            Layout.fillHeight: true
            Layout.margins: AuraTheme.spaceMd
            Layout.rightMargin: AuraTheme.spaceSm
            padding: AuraTheme.spaceSm
            backdrop: shell.backdropRef

            ColumnLayout {
                anchors.fill: parent
                spacing: AuraTheme.spaceSm

                // Эмблема бренда (v3)
                AuraEmblem {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: AuraTheme.spaceXs
                    size: 40
                }

                Repeater {
                    model: [
                        { glyph: "⌂", label: "Home" },
                        { glyph: "💬", label: "My Aura" },
                        { glyph: "⇄", label: "Переговоры" },
                        { glyph: "✓", label: "Задачи" },
                        { glyph: "👥", label: "Контакты" },
                        { glyph: "⚙", label: "Ещё" }
                    ]

                    delegate: Item {
                        id: railItem
                        required property int index
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 58

                        readonly property bool active: shell.currentTab === railItem.index

                        Rectangle {
                            anchors.fill: parent
                            radius: AuraTheme.radiusField
                            color: railItem.active ? AuraTheme.accentA(0.18)
                                                   : (railArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08)
                                                                             : "transparent")
                            Behavior on color { ColorAnimation { duration: AuraTheme.pressMs } }

                            // Lilac-индикатор активного пункта (v3)
                            Rectangle {
                                visible: railItem.active
                                width: 3
                                height: parent.height * 0.55
                                radius: 2
                                anchors.left: parent.left
                                anchors.leftMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                color: AuraTheme.lilac
                            }

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: railItem.modelData.glyph
                                    font.pixelSize: 18
                                    color: railItem.active ? AuraTheme.lilac : AuraTheme.textSecondary
                                }
                                Text {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: railItem.modelData.label
                                    font.family: AuraTheme.fontFamily
                                    font.pixelSize: 9
                                    color: railItem.active ? AuraTheme.lilac : AuraTheme.textMuted
                                }
                            }

                            // Бейдж подтверждений на «Задачах» не нужен:
                            // опасные операции — иконкой ниже.
                        }

                        MouseArea {
                            id: railArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                shell.currentTab = railItem.index
                                if (railItem.index === 0 && App) App.loadTasks()
                                if (railItem.index === 3 && App) App.loadTasks()
                                if (railItem.index === 4 && App) App.loadChats()
                                if (railItem.index === 5 && App) {
                                    App.loadPermissions()
                                    App.loadIntegrations()
                                }
                            }
                        }

                        ToolTip.visible: railArea.containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: railItem.modelData.label
                    }
                }

                Item { Layout.fillHeight: true }

                // ------------------------- барьер безопасности: бейджи внизу
                Repeater {
                    model: [
                        { tab: 6, glyph: "⚠", label: "Подтверждения", warn: true },
                        { tab: 7, glyph: "🔔", label: "Уведомления", warn: false }
                    ]

                    delegate: Item {
                        id: badgeItem
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 44

                        readonly property int badgeCount: badgeItem.modelData.warn && App
                            ? App.confirmations.length
                            : (App ? App.unreadNotifications : 0)

                        Rectangle {
                            anchors.fill: parent
                            radius: AuraTheme.radiusField
                            color: badgeArea.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                            border.width: 1
                            border.color: badgeItem.badgeCount > 0
                                          ? (badgeItem.modelData.warn ? AuraTheme.warning : AuraTheme.accent)
                                          : "transparent"
                            border.opacity: 0.5

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 4
                                Text {
                                    text: badgeItem.modelData.glyph
                                    font.pixelSize: 15
                                    color: badgeItem.badgeCount > 0
                                           ? (badgeItem.modelData.warn ? AuraTheme.warning : AuraTheme.accent)
                                           : AuraTheme.textMuted
                                }
                                Text {
                                    visible: badgeItem.badgeCount > 0
                                    text: String(badgeItem.badgeCount)
                                    font.family: AuraTheme.fontFamily
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                    color: badgeItem.badgeCount > 0
                                           ? (badgeItem.modelData.warn ? "#1A1400" : "#04121F")
                                           : AuraTheme.textSecondary
                                }
                            }
                        }

                        MouseArea {
                            id: badgeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                shell.currentTab = badgeItem.modelData.tab
                                if (badgeItem.modelData.warn && App) App.loadConfirmations()
                                if (!badgeItem.modelData.warn && App) {
                                    App.loadNotifications()
                                    App.loadPushDevices()
                                }
                            }
                        }

                        ToolTip.visible: badgeArea.containsMouse
                        ToolTip.delay: 500
                        ToolTip.text: badgeItem.modelData.label
                    }
                }

                AuraAvatar {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.bottomMargin: AuraTheme.spaceXs
                    name: App ? App.userName : ""
                    online: App ? App.connected : false
                    size: 40
                }
            }
        }

        // ------------------------------------------------------- контент
        StackLayout {
            id: content
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: shell.currentTab

            // 0 — Home
            HomePage {
            }

            // 1 — My Aura: чаты master-detail
            RowLayout {
                spacing: 0

                ColumnLayout {
                    Layout.preferredWidth: AuraTheme.masterWidth
                    Layout.fillHeight: true
                    Layout.margins: AuraTheme.spaceMd
                    Layout.rightMargin: AuraTheme.spaceSm
                    spacing: AuraTheme.spaceMd

                    // Новый собеседник
                    GlassPanel {
                        Layout.fillWidth: true
                        padding: AuraTheme.spaceMd
                        backdrop: shell.backdropRef

                        RowLayout {
                            anchors.fill: parent
                            spacing: AuraTheme.spaceSm

                            GlassField {
                                id: contactField
                                Layout.fillWidth: true
                                implicitHeight: 46
                                placeholderText: "email собеседника"
                                icon: "✉"
                                onAccepted: startChatButton.clicked()
                            }

                            GlassButton {
                                id: startChatButton
                                variant: "accent"
                                text: ""
                                glyph: "＋"
                                implicitWidth: 52
                                implicitHeight: 46
                                busy: App ? App.busy : false
                                onClicked: {
                                    App.openChat(contactField.text)
                                    contactField.text = ""
                                }
                            }
                        }
                    }

                    ChatList {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        selectedChatId: shell.selectedChatId
                        backdrop: shell.backdropRef
                        // Только сигнал: selectedChatId придёт обратно биндингом
                        // из Main.qml (императивное присваивание сломало бы его).
                        onOpenChat: function(chatId) { shell.chatSelected(chatId) }
                    }
                }

                Loader {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.topMargin: AuraTheme.spaceMd
                    Layout.bottomMargin: AuraTheme.spaceMd
                    Layout.rightMargin: AuraTheme.spaceMd
                    sourceComponent: shell.selectedChatId > 0 ? chatDetail : chatPlaceholder
                }
            }

            // 2 — Переговоры (v3)
            NegotiationsPage {
                id: negotiations
            }

            // 3 — Задачи
            Item {
                Flickable {
                    anchors.fill: parent
                    anchors.margins: AuraTheme.spaceLg
                    contentHeight: tasksColumn.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: tasksColumn
                        width: parent.width
                        spacing: AuraTheme.spaceMd

                        Text {
                            text: "Задачи и напоминания"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontTitle
                            font.weight: Font.DemiBold
                            color: AuraTheme.textPrimary
                        }

                        TasksPanel {
                            Layout.fillWidth: true
                            backdrop: shell.backdropRef
                        }
                    }
                }
            }

            // 4 — Контакты (v3)
            ContactsPage {
                id: contacts
                onOpenChat: function(chatId) {
                    shell.chatSelected(chatId)
                    shell.currentTab = 1
                }
                onStartNegotiation: function(target) {
                    negotiations.initialTarget = target
                    shell.currentTab = 2
                }
            }

            // 5 — Настройки
            SettingsPage {
                embedded: true
            }

            // 6 — Подтверждения опасных операций
            Item {
                Flickable {
                    anchors.fill: parent
                    anchors.margins: AuraTheme.spaceLg
                    contentHeight: confirmColumn.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: confirmColumn
                        width: parent.width
                        spacing: AuraTheme.spaceMd

                        Text {
                            text: "Подтверждения"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontTitle
                            font.weight: Font.DemiBold
                            color: AuraTheme.textPrimary
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: App && App.confirmations.length === 0
                            text: "Нет операций, ожидающих подтверждения. Опасные действия "
                                  + "(сообщения, письма, бронирование) Аура выполняет только "
                                  + "после вашего «Разрешить»."
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontSmall
                            color: AuraTheme.textMuted
                            wrapMode: Text.WordWrap
                        }

                        ConfirmationPanel {
                            Layout.fillWidth: true
                            backdrop: shell.backdropRef
                        }
                    }
                }
            }

            // 7 — Уведомления
            Item {
                Flickable {
                    anchors.fill: parent
                    anchors.margins: AuraTheme.spaceLg
                    contentHeight: notifyColumn.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds

                    ColumnLayout {
                        id: notifyColumn
                        width: parent.width
                        spacing: AuraTheme.spaceMd

                        Text {
                            text: "Уведомления"
                            font.family: AuraTheme.fontFamily
                            font.pixelSize: AuraTheme.fontTitle
                            font.weight: Font.DemiBold
                            color: AuraTheme.textPrimary
                        }

                        NotificationsPanel {
                            Layout.fillWidth: true
                            backdrop: shell.backdropRef
                        }
                    }
                }
            }
        }
    }

    Component {
        id: chatDetail
        ChatPage {
            embedded: true
            chatId: shell.selectedChatId
        }
    }

    Component {
        id: chatPlaceholder
        GlassPanel {
            backdrop: shell.backdropRef

            ColumnLayout {
                anchors.centerIn: parent
                spacing: AuraTheme.spaceSm

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "✦"
                    font.pixelSize: 34
                    color: AuraTheme.accent
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Выберите чат слева\nили начните новый по email"
                    horizontalAlignment: Text.AlignHCenter
                    font.family: AuraTheme.fontFamily
                    font.pixelSize: AuraTheme.fontSmall
                    color: AuraTheme.textMuted
                    lineHeight: 1.4
                }
            }
        }
    }
}
