import QtQuick
import Aura

// LinkText — текстовая ссылка («Все задачи», «Создать аккаунт»).
Text {
    id: root
    signal clicked()
    property color normalColor: AuraTheme.textSecondary
    font.family: AuraTheme.fontFamily
    font.pixelSize: AuraTheme.fontSmall
    font.weight: Font.Medium
    color: area.containsMouse ? AuraTheme.textPrimary : normalColor
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
