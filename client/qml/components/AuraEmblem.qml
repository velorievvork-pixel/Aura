import QtQuick
import QtQuick.Effects
import Aura

/*
  AuraEmblem — логотип Aura: кольцо из 12 изогнутых штрихов-«лепестков»
  с сиреневым свечением (экран входа). Геометрия та же, что у иконки
  приложения; штрихи медленно вращаются, свечение дышит.
*/
Item {
    id: root

    property int size: 72
    property int dashes: 12
    property bool animate: true
    property real glow: 1.0

    implicitWidth: size
    implicitHeight: size

    Canvas {
        id: swirl
        anchors.fill: parent
        antialiasing: true
        onPaint: {
            const ctx = getContext("2d")
            const s = width / 100
            ctx.reset()
            ctx.lineCap = "round"
            ctx.lineWidth = 5 * s
            const grad = ctx.createLinearGradient(0, 0, width, height)
            grad.addColorStop(0, "#F6E6FF")
            grad.addColorStop(1, "#B18CFF")
            ctx.strokeStyle = grad
            for (let i = 0; i < root.dashes; ++i) {
                ctx.save()
                ctx.translate(50 * s, 50 * s)
                ctx.rotate(2 * Math.PI * i / root.dashes)
                ctx.translate(-50 * s, -50 * s)
                ctx.beginPath()
                ctx.moveTo(50 * s, 9 * s)
                ctx.quadraticCurveTo(60 * s, 9.5 * s, 66 * s, 19 * s)
                ctx.stroke()
                ctx.restore()
            }
        }
        onWidthChanged: requestPaint()

        layer.enabled: true
        layer.effect: MultiEffect {
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowColor: AuraTheme.lilac
            shadowBlur: 0.9 * root.glow
            shadowOpacity: breath.value
            shadowHorizontalOffset: 0
            shadowVerticalOffset: 0
            blurMax: 32
        }

        RotationAnimation on rotation {
            running: root.animate
            from: 0; to: 360
            duration: 60000
            loops: Animation.Infinite
        }
    }

    QtObject {
        id: breath
        property real value: 0.9
        SequentialAnimation on value {
            running: root.animate
            loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 2200; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.95; duration: 2200; easing.type: Easing.InOutSine }
        }
    }
}
