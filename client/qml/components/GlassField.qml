import QtQuick
import QtQuick.Controls

/*
  GlassField — стеклянное поле ввода с плавающей подсказкой.

  v3: icon — глиф слева (например «✉» / «🔒»), action — глиф справа
  (например «👁» показа пароля); клик по action даёт сигнал actionClicked.
*/
TextField {
    id: control

    property string label: ""
    property bool invalid: false
    property string icon: ""
    property string action: ""
    signal actionClicked()

    implicitHeight: 52
    implicitWidth: 240
    leftPadding: control.icon.length > 0 ? AuraTheme.spaceMd + 24 : AuraTheme.spaceMd
    rightPadding: control.action.length > 0 ? AuraTheme.spaceMd + 24 : AuraTheme.spaceMd
    topPadding: label.length > 0 ? 20 : 0
    bottomPadding: label.length > 0 ? 6 : 0
    font.family: AuraTheme.fontFamily
    font.pixelSize: AuraTheme.fontBody
    color: AuraTheme.textPrimary
    selectionColor: AuraTheme.accentA(0.5)
    selectedTextColor: AuraTheme.textPrimary
    placeholderTextColor: AuraTheme.textMuted
    verticalAlignment: TextInput.AlignVCenter
    focusPolicy: Qt.StrongFocus

    background: Rectangle {
        radius: AuraTheme.radiusField
        color: Qt.rgba(1, 1, 1, control.activeFocus ? AuraTheme.glassFillPressed : AuraTheme.glassFill)
        border.width: 1
        border.color: control.invalid
                      ? AuraTheme.danger
                      : (control.activeFocus ? AuraTheme.accentA(0.7) : Qt.rgba(1, 1, 1, AuraTheme.glassBorder))

        Behavior on color { ColorAnimation { duration: AuraTheme.pressMs } }
        Behavior on border.color { ColorAnimation { duration: AuraTheme.pressMs } }

        // Блик сверху
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 1
            height: parent.height * 0.45
            radius: parent.radius
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
            }
        }
    }

    // Плавающая подпись внутри поля
    Text {
        x: control.leftPadding
        y: 8
        text: control.label
        font.family: AuraTheme.fontFamily
        font.pixelSize: AuraTheme.fontMicro
        color: control.activeFocus ? AuraTheme.accent : AuraTheme.textMuted
        visible: control.label.length > 0
        Behavior on color { ColorAnimation { duration: AuraTheme.pressMs } }
    }

    // Иконка слева (конверт, замок…)
    Text {
        x: AuraTheme.spaceSm + 4
        anchors.verticalCenter: parent.verticalCenter
        text: control.icon
        font.pixelSize: 14
        color: control.activeFocus ? AuraTheme.accent : AuraTheme.textMuted
        visible: control.icon.length > 0
        Behavior on color { ColorAnimation { duration: AuraTheme.pressMs } }
    }

    // Действие справа (глаз показа пароля…)
    Text {
        x: control.width - control.rightPadding + 4
        anchors.verticalCenter: parent.verticalCenter
        text: control.action
        font.pixelSize: 14
        color: control.activeFocus ? AuraTheme.lilac : AuraTheme.textMuted
        visible: control.action.length > 0

        MouseArea {
            anchors.fill: parent
            anchors.margins: -8
            cursorShape: Qt.PointingHandCursor
            onClicked: control.actionClicked()
        }
    }
}
