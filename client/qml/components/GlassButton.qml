import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Selfvoy
import "../icons/Icons.js" as Icons

/*
  GlassButton — кнопка v3.

  variant: "accent" — градиент сирень → фиолет → индиго со свечением (CTA)
           "glass"  — нейтральное стекло с тонкой рамкой (по умолчанию)
           "quiet"  — почти прозрачная, для второстепенных действий
           "danger" — красноватое стекло
  symbol — имя иконки Lucide (Icon.qml); glyph — текстовый символ (совместимость).
  Отклик: подъём при наведении, «вдавливание» при нажатии, спиннер при busy.
*/
Button {
    id: control

    property string variant: "glass"
    property string glyph: ""
    property string symbol: ""
    property bool busy: false
    property real glyphSize: 16
    property real radius: SelfvoyTheme.radiusPill
    property int textSize: SelfvoyTheme.fontBody
    property bool alignLeft: false

    // Старые экраны задают glyph символом — показываем его иконкой, если она есть.
    readonly property string resolvedIcon: symbol.length > 0 ? symbol : Icons.iconFor(glyph)
    readonly property bool isAccent: variant === "accent"
    readonly property color fg: isAccent ? SelfvoyTheme.textOnAccent
                                         : (variant === "danger" ? SelfvoyTheme.danger : SelfvoyTheme.textPrimary)

    implicitHeight: 46
    implicitWidth: Math.max(implicitHeight, contentItem.implicitWidth + leftPadding + rightPadding)
    leftPadding: text.length > 0 ? SelfvoyTheme.spaceLg : 0
    rightPadding: text.length > 0 ? SelfvoyTheme.spaceLg : 0
    topPadding: 0
    bottomPadding: 0
    focusPolicy: Qt.TabFocus
    hoverEnabled: true

    scale: pressed ? SelfvoyTheme.pressScale : 1.0
    opacity: enabled ? 1.0 : 0.45
    Behavior on scale { NumberAnimation { duration: SelfvoyTheme.pressMs; easing.type: SelfvoyTheme.ease } }
    Behavior on opacity { NumberAnimation { duration: SelfvoyTheme.moveMs } }

    background: Item {
        implicitHeight: control.implicitHeight
        transform: Translate {
            y: control.hovered && !control.pressed ? SelfvoyTheme.hoverLift : 0
            Behavior on y { NumberAnimation { duration: SelfvoyTheme.pressMs; easing.type: SelfvoyTheme.ease } }
        }

        Rectangle {
            id: face
            anchors.fill: parent
            radius: Math.min(control.radius, height / 2)
            gradient: control.isAccent ? accentGradient : null
            color: control.isAccent ? "transparent"
                   : control.variant === "danger"
                     ? Qt.rgba(SelfvoyTheme.danger.r, SelfvoyTheme.danger.g, SelfvoyTheme.danger.b,
                               control.pressed ? 0.24 : (control.hovered ? 0.2 : 0.14))
                     : Qt.rgba(1, 1, 1, control.variant === "quiet"
                               ? (control.pressed ? 0.1 : (control.hovered ? 0.06 : 0.0))
                               : (control.pressed ? 0.14 : (control.hovered ? 0.1 : 0.06)))
            border.width: control.variant === "quiet" ? 0 : 1
            border.color: control.isAccent ? Qt.rgba(1, 1, 1, 0.28)
                                           : Qt.rgba(1, 1, 1, control.hovered ? SelfvoyTheme.glassBorderHover
                                                                              : SelfvoyTheme.glassBorder + 0.04)
            Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }

            Gradient {
                id: accentGradient
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: control.pressed ? SelfvoyTheme.gradMid : SelfvoyTheme.gradStart }
                GradientStop { position: 0.5; color: SelfvoyTheme.gradMid }
                GradientStop { position: 1.0; color: SelfvoyTheme.gradEnd }
            }

            // Верхний блик. Той же формы, что и кнопка, а затухает градиентом:
            // прямоугольник половинной высоты получал радиус не больше h/4 кнопки,
            // и его углы торчали за круг/пилюлю светлым «квадратом».
            Rectangle {
                visible: control.variant !== "quiet" || control.hovered
                anchors.fill: parent
                anchors.margins: 1
                radius: Math.max(0, parent.radius - 1)
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, control.isAccent ? 0.22 : 0.08) }
                    GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0.0) }
                    GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
                }
            }

            // Свечение акцентной кнопки
            layer.enabled: control.isAccent && control.enabled
            layer.effect: MultiEffect {
                autoPaddingEnabled: true
                shadowEnabled: true
                shadowColor: SelfvoyTheme.accentGlow
                shadowBlur: control.hovered ? 1.0 : 0.8
                shadowVerticalOffset: 8
                blurMax: 40
            }
        }

        // Индикатор фокуса с клавиатуры
        Rectangle {
            anchors.fill: face
            anchors.margins: -3
            radius: face.radius + 3
            color: "transparent"
            border.width: control.visualFocus ? 2 : 0
            border.color: SelfvoyTheme.lilacA(0.7)
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            spacing: SelfvoyTheme.spaceSm
            anchors.verticalCenter: parent.verticalCenter
            x: control.alignLeft ? 0 : Math.max(0, (parent.width - width) / 2)

            // Иконка / глиф / спиннер
            Item {
                width: visible ? control.glyphSize + 2 : 0
                height: control.glyphSize + 2
                visible: control.resolvedIcon.length > 0 || control.glyph.length > 0 || control.busy
                anchors.verticalCenter: parent.verticalCenter

                Icon {
                    anchors.centerIn: parent
                    name: control.resolvedIcon
                    size: control.glyphSize
                    color: control.fg
                    visible: control.resolvedIcon.length > 0 && !control.busy
                }
                Text {
                    anchors.centerIn: parent
                    text: control.glyph
                    visible: control.resolvedIcon.length === 0 && !control.busy
                    font.family: SelfvoyTheme.fontFamily
                    font.pixelSize: control.glyphSize
                    color: control.fg
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: control.glyphSize
                    height: control.glyphSize
                    radius: width / 2
                    color: "transparent"
                    border.width: 2
                    border.color: Qt.rgba(control.fg.r, control.fg.g, control.fg.b, 0.3)
                    visible: control.busy
                    Rectangle {
                        width: 5; height: 5; radius: 2.5
                        color: control.fg
                        x: parent.width / 2 - 2.5
                        y: -1.5
                    }
                    RotationAnimation on rotation {
                        from: 0; to: 360; duration: 900
                        loops: Animation.Infinite
                        running: control.busy
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                visible: control.text.length > 0
                font.family: SelfvoyTheme.fontFamily
                font.pixelSize: control.textSize
                font.weight: control.isAccent ? Font.Bold : Font.DemiBold
                color: control.fg
                elide: Text.ElideRight
            }
        }
    }
}
