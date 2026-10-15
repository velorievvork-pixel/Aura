import QtQuick
import QtQuick.Controls
import Selfvoy
import "../icons/Icons.js" as Icons

/*
  GlassField — поле ввода v3: тёмное стекло с тонкой рамкой, сиреневое кольцо
  в фокусе, иконка справа (конверт, замок) или кликабельное действие (глаз).

  label — плавающая подпись внутри поля (экраны настроек); на экранах входа
  подпись стоит над полем отдельным Text, а label пустой.
  icon/action принимают имя иконки Lucide или старый текстовый глиф.
*/
TextField {
    id: control

    property string label: ""
    property bool invalid: false
    property string icon: ""
    property string action: ""
    property bool filled: false   // светлая заливка без рамки (поле «С чем помогать»)
    signal actionClicked()

    readonly property string rightIcon: {
        const name = action.length > 0 ? action : icon
        return Icons.paths[name] !== undefined ? name : Icons.iconFor(name)
    }

    implicitHeight: label.length > 0 ? 54 : 48
    implicitWidth: 240
    leftPadding: SelfvoyTheme.spaceMd
    rightPadding: rightIcon.length > 0 ? SelfvoyTheme.spaceMd + 26 : SelfvoyTheme.spaceMd
    topPadding: label.length > 0 ? 20 : 0
    bottomPadding: label.length > 0 ? 6 : 0
    font.family: SelfvoyTheme.fontFamily
    font.pixelSize: SelfvoyTheme.fontBody
    color: SelfvoyTheme.textPrimary
    selectionColor: SelfvoyTheme.accentA(0.5)
    selectedTextColor: SelfvoyTheme.textPrimary
    placeholderTextColor: SelfvoyTheme.textMuted
    verticalAlignment: TextInput.AlignVCenter
    focusPolicy: Qt.StrongFocus

    background: Item {
        // Кольцо фокуса
        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: SelfvoyTheme.radiusField + 4
            color: "transparent"
            border.width: 4
            border.color: SelfvoyTheme.accentA(control.activeFocus ? 0.18 : 0)
            Behavior on border.color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
        }
        Rectangle {
            anchors.fill: parent
            radius: SelfvoyTheme.radiusField
            color: control.filled ? Qt.rgba(1, 1, 1, control.activeFocus ? 0.12 : 0.09)
                                  : Qt.rgba(0.04, 0.03, 0.07, control.activeFocus ? 0.6 : 0.45)
            border.width: control.filled && !control.activeFocus && !control.invalid ? 0 : 1
            border.color: control.invalid ? SelfvoyTheme.danger
                          : control.activeFocus ? SelfvoyTheme.accentA(0.8)
                          : Qt.rgba(1, 1, 1, 0.16)
            Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
            Behavior on border.color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
        }
    }

    // Плавающая подпись внутри поля
    Text {
        x: control.leftPadding
        y: 8
        text: control.label
        font.family: SelfvoyTheme.fontFamily
        font.pixelSize: SelfvoyTheme.fontMicro
        font.weight: Font.DemiBold
        color: control.activeFocus ? SelfvoyTheme.lilac : SelfvoyTheme.textMuted
        visible: control.label.length > 0
        Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
    }

    // Иконка или действие справа
    Icon {
        id: trailing
        x: control.width - SelfvoyTheme.spaceMd - size
        anchors.verticalCenter: parent.verticalCenter
        name: control.rightIcon
        size: 18
        color: control.action.length > 0 && hit.containsMouse ? SelfvoyTheme.textPrimary : SelfvoyTheme.textSecondary
        visible: control.rightIcon.length > 0

        MouseArea {
            id: hit
            anchors.fill: parent
            anchors.margins: -8
            enabled: control.action.length > 0
            hoverEnabled: true
            cursorShape: enabled ? Qt.PointingHandCursor : Qt.IBeamCursor
            onClicked: control.actionClicked()
        }
    }
}
