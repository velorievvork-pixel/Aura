import QtQuick
import Selfvoy

// LinkText — текстовая ссылка («Все задачи», «Создать аккаунт»).
Text {
    id: root
    signal clicked()
    property color normalColor: SelfvoyTheme.textSecondary
    font.family: SelfvoyTheme.fontFamily
    font.pixelSize: SelfvoyTheme.fontSmall
    font.weight: Font.Medium
    color: area.containsMouse ? SelfvoyTheme.textPrimary : normalColor
    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
