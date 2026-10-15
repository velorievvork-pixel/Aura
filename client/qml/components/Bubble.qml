import QtQuick
import QtQuick.Layouts
import Selfvoy

/*
  Bubble — реплика в диалоге с Selfvoy. Пользователь — тёмное стекло слева,
  Selfvoy — светлое стекло справа с фиолетовым свечением (макет Chat).
  Дети (кнопки действий) встают под текстом.
*/
Item {
    id: root
    property bool fromSelfvoy: false
    property bool error: false
    property string text: ""
    property real maxWidth: 560
    // Под репликой есть кнопки действий — пузырь берёт полную ширину, кнопки переносятся.
    property bool wide: false
    default property alias extra: below.data

    // Ширину меряем копией текста без переноса: implicitWidth переносимого Text
    // внутри Layout занижен, и короткие реплики переносились на две строки.
    implicitWidth: wide ? maxWidth : Math.min(maxWidth, Math.ceil(probe.implicitWidth) + 2 + 44)
    implicitHeight: column.implicitHeight + 36
    width: implicitWidth
    height: implicitHeight

    Text {
        id: probe
        visible: false
        text: root.text
        font: label.font
    }

    Rectangle {
        anchors.fill: parent
        radius: SelfvoyTheme.radiusBubble
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.fromSelfvoy ? Qt.rgba(1, 1, 1, 0.2) : Qt.rgba(1, 1, 1, 0.09) }
            GradientStop { position: 1.0; color: root.fromSelfvoy ? Qt.rgba(0.67, 0.63, 0.86, 0.12) : Qt.rgba(1, 1, 1, 0.06) }
        }
        border.width: 1
        border.color: root.error ? Qt.alpha(SelfvoyTheme.danger, 0.6)
                                 : Qt.rgba(1, 1, 1, root.fromSelfvoy ? 0.28 : 0.1)
    }
    ColumnLayout {
        id: column
        x: 22
        y: 18
        width: root.width - 44
        spacing: 14
        Text {
            id: label
            Layout.fillWidth: true
            text: root.text
            wrapMode: Text.WordWrap
            font.family: SelfvoyTheme.fontFamily
            font.pixelSize: SelfvoyTheme.fontBody
            font.weight: Font.DemiBold
            lineHeight: 1.3
            color: root.error ? SelfvoyTheme.danger : SelfvoyTheme.textPrimary
        }
        Item {
            id: below
            Layout.fillWidth: true
            implicitHeight: childrenRect.height
            visible: root.wide
        }
    }
}
