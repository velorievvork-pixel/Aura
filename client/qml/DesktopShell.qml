import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Selfvoy

/*
  DesktopShell — оболочка после входа (макет Home): стеклянный сайдбар слева
  (логотип, разделы, барьер безопасности, профиль) и большая стеклянная
  панель с текущим разделом справа. В узком окне сайдбар сворачивается в
  рельс из иконок. Разделы грузятся лениво и переключаются сигналом
  navigate(page) любой страницы.
*/
Item {
    id: shell

    property string current: "home"
    readonly property bool wide: width >= SelfvoyTheme.breakpointWide
    readonly property Item backdropRef: shell.Window.window ? shell.Window.window.contentItem : null

    readonly property var sections: [
        { key: "home",          label: "Главная",    icon: "house" },
        { key: "selfvoy",          label: "Мой Selfvoy",   icon: "message-circle" },
        { key: "negotiations",  label: "Переговоры", icon: "handshake" },
        { key: "booking",       label: "Запись",     icon: "calendar-clock" },
        { key: "tasks",         label: "Задачи",     icon: "square-check" },
        { key: "contacts",      label: "Контакты",   icon: "users" },
        { key: "settings",      label: "Настройки",  icon: "settings" }
    ]

    function navigate(page) {
        current = page
        if (!App) return
        if (page === "settings") { App.loadPermissions(); App.loadIntegrations(); App.loadReferral() }
        else if (page === "booking") App.loadBooking()
        else if (page === "confirmations") App.loadConfirmations()
        else if (page === "notifications") { App.loadNotifications(); App.loadPushDevices() }
        else if (page === "tasks") App.loadTasks()
    }

    // ------------------------------------------------------------- пункт меню
    component NavItem: Item {
        id: item
        property string key: ""
        property string label: ""
        property string icon: ""
        property int badge: 0
        property color badgeColor: SelfvoyTheme.accent
        readonly property bool active: shell.current === key

        Layout.fillWidth: true
        implicitHeight: 44

        Rectangle {
            anchors.fill: parent
            radius: SelfvoyTheme.radiusSmall
            color: item.active ? SelfvoyTheme.accentA(0.16)
                               : (area.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
            border.width: item.active ? 1 : 0
            border.color: SelfvoyTheme.lilacA(0.28)
            Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: shell.wide ? 12 : 0
            anchors.rightMargin: shell.wide ? 8 : 0
            spacing: 11

            Item { Layout.fillWidth: !shell.wide }
            Icon {
                name: item.icon
                size: 19
                color: item.active ? SelfvoyTheme.textPrimary
                       : (area.containsMouse ? SelfvoyTheme.textPrimary : SelfvoyTheme.textSecondary)
            }
            Text {
                visible: shell.wide
                Layout.fillWidth: true
                text: item.label
                font.family: SelfvoyTheme.fontFamily
                font.pixelSize: item.badge > 0 ? SelfvoyTheme.fontBody - 1 : SelfvoyTheme.fontBody
                font.weight: item.active ? Font.Bold : Font.Medium
                color: item.active || area.containsMouse ? SelfvoyTheme.textPrimary : SelfvoyTheme.textSecondary
                elide: Text.ElideRight
            }
            Rectangle {
                visible: item.badge > 0 && shell.wide
                implicitWidth: Math.max(20, badgeText.implicitWidth + 12)
                implicitHeight: 20
                radius: 10
                color: item.badgeColor
                Text {
                    id: badgeText
                    anchors.centerIn: parent
                    text: item.badge
                    font.family: SelfvoyTheme.fontFamily
                    font.pixelSize: SelfvoyTheme.fontMicro
                    font.weight: Font.Bold
                    color: item.badgeColor === SelfvoyTheme.warning ? "#1A1400" : "#FFFFFF"
                }
            }
            Item { Layout.fillWidth: !shell.wide }
        }

        // Точка-бейдж в свёрнутом рельсе
        Rectangle {
            visible: item.badge > 0 && !shell.wide
            width: 8; height: 8; radius: 4
            color: item.badgeColor
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 8
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: shell.navigate(item.key)
        }

        ToolTip.visible: !shell.wide && area.containsMouse
        ToolTip.delay: 400
        ToolTip.text: item.label
    }

    // ------------------------------------------------------------- раскладка
    RowLayout {
        anchors.fill: parent
        anchors.margins: SelfvoyTheme.spaceLg
        spacing: 18

        GlassPanel {
            id: sidebar
            Layout.preferredWidth: shell.wide ? SelfvoyTheme.sidebarWidth : 84
            Layout.fillHeight: true
            radius: 30
            padding: shell.wide ? 18 : 12
            backdrop: shell.backdropRef

            ColumnLayout {
                anchors.fill: parent
                spacing: 4

                // Логотип
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    Layout.leftMargin: shell.wide ? 10 : 0
                    spacing: 12
                    Item { Layout.fillWidth: !shell.wide }
                    Rectangle {
                        implicitWidth: 26; implicitHeight: 26; radius: 13
                        color: "transparent"
                        border.width: 3.5
                        border.color: SelfvoyTheme.textPrimary
                        Rectangle { anchors.centerIn: parent; width: 9; height: 9; radius: 4.5; color: SelfvoyTheme.textPrimary }
                    }
                    Text {
                        visible: shell.wide
                        text: "Selfvoy"
                        font.family: SelfvoyTheme.fontFamily
                        font.pixelSize: SelfvoyTheme.fontTitle
                        font.weight: Font.Bold
                        color: SelfvoyTheme.textPrimary
                    }
                    Item { Layout.fillWidth: true }
                }

                Item { Layout.preferredHeight: 52 }

                Repeater {
                    model: shell.sections
                    delegate: NavItem {
                        required property var modelData
                        key: modelData.key
                        label: modelData.label
                        icon: modelData.icon
                        badge: modelData.key === "negotiations" && App && App.negotiation.accepted
                               && !App.negotiation.booking ? 1 : 0
                    }
                }

                Item { Layout.preferredHeight: 18 }

                // Барьер безопасности: подтверждения опасных действий и уведомления
                // видны из любого раздела.
                NavItem {
                    key: "confirmations"
                    label: "Подтверждения"
                    icon: "triangle-alert"
                    badge: App ? App.confirmations.length : 0
                    badgeColor: SelfvoyTheme.warning
                }
                NavItem {
                    key: "notifications"
                    label: "Уведомления"
                    icon: "bell"
                    badge: App ? App.unreadNotifications : 0
                }

                Item { Layout.fillHeight: true }

                // Профиль
                Item {
                    Layout.fillWidth: true
                    implicitHeight: 58

                    Rectangle {
                        anchors.fill: parent
                        radius: SelfvoyTheme.radiusField
                        color: meArea.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: shell.wide ? 8 : 0
                        spacing: 10
                        Item { Layout.fillWidth: !shell.wide }
                        SelfvoyAvatar {
                            name: App ? App.userName : ""
                            size: 36
                            online: App ? App.connected : false
                        }
                        ColumnLayout {
                            visible: shell.wide
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                text: App ? App.userName : ""
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontSmall + 1
                                font.weight: Font.DemiBold
                                color: SelfvoyTheme.textPrimary
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: App && App.connected ? (App.userEmail || "") : "нет связи с сервером"
                                font.family: SelfvoyTheme.fontFamily
                                font.pixelSize: SelfvoyTheme.fontMicro
                                color: App && App.connected ? SelfvoyTheme.textMuted : SelfvoyTheme.warning
                                elide: Text.ElideRight
                            }
                        }
                        Item { Layout.fillWidth: !shell.wide }
                    }
                    MouseArea {
                        id: meArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shell.navigate("settings")
                    }
                }
            }
        }

        GlassPanel {
            id: main
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 34
            padding: 0
            backdrop: shell.backdropRef

            Loader {
                id: page
                anchors.fill: parent
                sourceComponent: {
                    switch (shell.current) {
                    case "selfvoy": return selfvoyPage
                    case "negotiations": return negotiationsPage
                    case "tasks": return tasksPage
                    case "contacts": return contactsPage
                    case "settings": return settingsPage
                    case "booking": return bookingPage
                    case "confirmations": return confirmationsPage
                    case "notifications": return notificationsPage
                    default: return homePage
                    }
                }
                onLoaded: fade.restart()
                NumberAnimation { id: fade; target: page.item; property: "opacity"; from: 0; to: 1; duration: SelfvoyTheme.moveMs }
            }

            Connections {
                target: page.item
                ignoreUnknownSignals: true
                function onNavigate(to) { shell.navigate(to) }
            }
        }
    }

    Component { id: homePage; HomePage {} }
    Component { id: selfvoyPage; MySelfvoyPage {} }
    Component { id: negotiationsPage; NegotiationsPage {} }
    Component { id: tasksPage; TasksPage {} }
    Component { id: contactsPage; ContactsPage {} }
    Component { id: settingsPage; SettingsPage { embedded: true } }
    Component { id: bookingPage; BookingPage {} }
    Component { id: confirmationsPage; InboxPage { mode: "confirmations" } }
    Component { id: notificationsPage; InboxPage { mode: "notifications" } }
}
