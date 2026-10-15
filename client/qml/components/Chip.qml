import QtQuick
import QtQuick.Controls
import Selfvoy

// Chip — пилюля-кнопка: быстрые ответы, фильтры, варианты окна переговоров.
AbstractButton {
    id: control
    property bool active: false
    hoverEnabled: true
    implicitHeight: 34
    implicitWidth: label.implicitWidth + 30
    opacity: enabled ? 1 : 0.55

    background: Rectangle {
        radius: height / 2
        color: control.active ? Qt.rgba(1, 1, 1, 0.14)
               : control.pressed ? Qt.rgba(1, 1, 1, 0.12)
               : control.hovered ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(0.04, 0.03, 0.07, 0.55)
        border.width: 1
        border.color: control.active ? Qt.rgba(1, 1, 1, 0.38) : Qt.rgba(1, 1, 1, 0.2)
        Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
    }
    contentItem: Text {
        id: label
        text: control.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: SelfvoyTheme.fontFamily
        font.pixelSize: SelfvoyTheme.fontSmall
        font.weight: Font.DemiBold
        color: SelfvoyTheme.textPrimary
        elide: Text.ElideRight
    }
}
