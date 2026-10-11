import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Aura

/*
  InboxPage — «Подтверждения» (опасные действия Ауры ждут решения) и
  «Уведомления» (входящая + push-устройства). Содержимое — готовые панели
  ConfirmationPanel/NotificationsPanel, страница даёт заголовок и прокрутку.
*/
Item {
    id: page

    signal navigate(string to)
    property string mode: "confirmations"

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 88
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: column
            x: 48
            y: 44
            width: parent.width - 96
            spacing: 22

            Text {
                text: page.mode === "confirmations" ? "Подтверждения" : "Уведомления"
                font.family: AuraTheme.fontFamily
                font.pixelSize: 38
                font.weight: Font.Bold
                color: AuraTheme.textPrimary
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: page.mode === "confirmations"
                      ? "Письма, сообщения и брони Аура не выполняет сама: каждое такое действие ждёт здесь вашего решения."
                      : "Напоминания, переговоры, новые входы и изменения безопасности."
                font.family: AuraTheme.fontFamily
                font.pixelSize: AuraTheme.fontBody
                color: AuraTheme.textSecondary
            }
            Loader {
                Layout.fillWidth: true
                sourceComponent: page.mode === "confirmations" ? confirmations : notifications
            }
        }
    }

    Component { id: confirmations; ConfirmationPanel { backdrop: null } }
    Component { id: notifications; NotificationsPanel { backdrop: null } }
}
