import QtQuick
import Selfvoy

/*
  ChatBubble — «пузырь» сообщения.

  Свой текст — справа, акцентное стекло; чужой — слева, нейтральное стекло;
  сообщения Selfvoy помечаются значком и холодным оттенком.
*/
Item {
    id: root

    property string text: ""
    property string senderName: ""
    property string timestamp: ""
    property bool mine: false
    property bool agent: false
    property real maxBubbleWidth: 520

    implicitHeight: column.height + (root.agent ? badge.height + 6 : 0)
    implicitWidth: parent ? parent.width : maxBubbleWidth
    height: implicitHeight

    readonly property color bubbleTint: root.agent ? SelfvoyTheme.accentSoft
                                                 : (root.mine ? SelfvoyTheme.accent : SelfvoyTheme.glassTint)
    readonly property real bubbleFill: root.agent ? 0.16 : (root.mine ? SelfvoyTheme.accentFill : SelfvoyTheme.glassFill)

    // Значок Selfvoy над сообщением агента
    Row {
        id: badge
        anchors.left: root.mine ? undefined : parent.left
        anchors.right: root.mine ? parent.right : undefined
        anchors.leftMargin: SelfvoyTheme.spaceXs
        anchors.rightMargin: SelfvoyTheme.spaceXs
        spacing: 5
        visible: root.agent
        height: visible ? 16 : 0

        Rectangle {
            width: 6
            height: 6
            radius: 3
            anchors.verticalCenter: parent.verticalCenter
            color: SelfvoyTheme.accentSoft
        }
        Text {
            text: "Selfvoy"
            font.family: SelfvoyTheme.fontFamily
            font.pixelSize: SelfvoyTheme.fontMicro
            font.weight: Font.DemiBold
            color: SelfvoyTheme.accentSoft
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Item {
        id: column
        anchors.top: badge.bottom
        anchors.topMargin: root.agent ? 6 : 0
        anchors.left: parent.left
        anchors.right: parent.right
        height: bubble.height

        Rectangle {
            id: bubble
            anchors.right: root.mine ? parent.right : undefined
            anchors.left: root.mine ? undefined : parent.left
            // Ширина по копии текста без переноса (как в Bubble.qml): implicitWidth
            // самого переносимого текста зависит от уже выставленной ширины.
            width: Math.min(root.maxBubbleWidth, Math.max(80, Math.ceil(bubbleProbe.implicitWidth) + 2 + SelfvoyTheme.spaceLg * 2))
            height: bubbleText.implicitHeight + SelfvoyTheme.spaceLg
            radius: SelfvoyTheme.radiusBubble

            Text {
                id: bubbleProbe
                visible: false
                text: root.text
                font: bubbleText.font
            }
            color: Qt.rgba(root.bubbleTint.r, root.bubbleTint.g, root.bubbleTint.b, root.bubbleFill)
            border.width: 1
            border.color: root.agent
                          ? SelfvoyTheme.accentA(0.35)
                          : Qt.rgba(1, 1, 1, root.mine ? 0.22 : SelfvoyTheme.glassBorder)

            // Блик по верхнему краю пузыря (той же формы, что и пузырь, чтобы
            // углы не вылезали за скругление у однострочных сообщений)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: Math.max(0, parent.radius - 1)
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.16) }
                    GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.0) }
                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
                }
            }

            Text {
                id: bubbleText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: SelfvoyTheme.spaceLg
                text: root.text
                wrapMode: Text.WordWrap
                font.family: SelfvoyTheme.fontFamily
                font.pixelSize: SelfvoyTheme.fontBody
                color: SelfvoyTheme.textPrimary
                lineHeight: 1.25
            }
        }
    }

    // Подпись: кто и когда
    Text {
        anchors.top: column.bottom
        anchors.topMargin: 4
        anchors.left: root.mine ? undefined : parent.left
        anchors.right: root.mine ? parent.right : undefined
        anchors.leftMargin: SelfvoyTheme.spaceXs
        anchors.rightMargin: SelfvoyTheme.spaceXs
        text: [root.senderName, root.timestamp].filter(part => part.length > 0).join(" · ")
        font.family: SelfvoyTheme.fontFamily
        font.pixelSize: SelfvoyTheme.fontMicro
        color: SelfvoyTheme.textMuted
        visible: text.length > 0
    }
}
