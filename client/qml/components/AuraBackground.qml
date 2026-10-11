import QtQuick
import Aura

/*
  AuraBackground — фон окна из макетов: почти чёрный фиолетовый с тремя
  размытыми орбитами (фиолет слева снизу, глубокий фиолет справа сверху,
  индиго снизу), которые медленно дрейфуют. Орбиты — радиальные градиенты
  Canvas: дёшево и без зависимостей от GPU-эффектов.
*/
Item {
    id: root

    property bool animated: true

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: AuraTheme.bgDeep }
            GradientStop { position: 1.0; color: AuraTheme.bgBase }
        }
    }

    component Orb: Canvas {
        id: orb
        property color tint: AuraTheme.glowViolet
        property real strength: 0.55
        property real driftX: 60
        property real driftY: -40
        property int period: 22000
        readonly property real baseX: x
        width: 900
        height: 900
        onTintChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const g = ctx.createRadialGradient(width / 2, height / 2, 0, width / 2, height / 2, width / 2)
            const c = (a) => "rgba(" + Math.round(tint.r * 255) + "," + Math.round(tint.g * 255) + ","
                             + Math.round(tint.b * 255) + "," + a + ")"
            g.addColorStop(0.0, c(strength))
            g.addColorStop(0.45, c(strength * 0.45))
            g.addColorStop(1.0, c(0))
            ctx.fillStyle = g
            ctx.fillRect(0, 0, width, height)
        }
        transform: Translate { id: shift }
        SequentialAnimation {
            running: root.animated
            loops: Animation.Infinite
            ParallelAnimation {
                NumberAnimation { target: shift; property: "x"; to: orb.driftX; duration: orb.period; easing.type: Easing.InOutSine }
                NumberAnimation { target: shift; property: "y"; to: orb.driftY; duration: orb.period; easing.type: Easing.InOutSine }
            }
            ParallelAnimation {
                NumberAnimation { target: shift; property: "x"; to: 0; duration: orb.period; easing.type: Easing.InOutSine }
                NumberAnimation { target: shift; property: "y"; to: 0; duration: orb.period; easing.type: Easing.InOutSine }
            }
        }
    }

    Orb {
        x: -width * 0.45
        y: root.height - height * 0.55
        tint: AuraTheme.glowViolet
        strength: 0.62
    }
    Orb {
        width: 760; height: 760
        x: root.width - width * 0.55
        y: -height * 0.5
        tint: AuraTheme.glowMagenta
        strength: 0.7
        driftX: -50; driftY: 40; period: 26000
    }
    Orb {
        width: 600; height: 600
        x: root.width * 0.62
        y: root.height - height * 0.45
        tint: AuraTheme.glowIndigo
        strength: 0.4
        driftX: 40; driftY: -30; period: 30000
    }
}
