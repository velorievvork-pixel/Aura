import QtQuick
import QtQuick.Effects
import Aura

// AuraOrb — светящаяся сфера-аватар Ауры (шапка «Моей Ауры»).
Item {
    id: root
    property int size: 70
    property bool animate: true
    implicitWidth: size
    implicitHeight: size

    Canvas {
        id: sphere
        anchors.fill: parent
        onPaint: {
                if (width <= 0 || height <= 0) return
            const ctx = getContext("2d")
            ctx.reset()
            const r = width / 2
            const g = ctx.createRadialGradient(r * 0.64, r * 0.56, 0, r, r, r)
            g.addColorStop(0.0, "#FFFFFF")
            g.addColorStop(0.1, "#F3D9FF")
            g.addColorStop(0.34, "#B98CFF")
            g.addColorStop(0.62, "#5B3BD6")
            g.addColorStop(0.86, "#2A1A6B")
            g.addColorStop(1.0, "#1A1040")
            ctx.fillStyle = g
            ctx.beginPath()
            ctx.arc(r, r, r, 0, Math.PI * 2)
            ctx.fill()
            // голубой отблеск
            const h = ctx.createLinearGradient(0, height * 0.4, width, height)
            h.addColorStop(0, "rgba(140,220,255,0)")
            h.addColorStop(0.6, "rgba(140,220,255,0.35)")
            h.addColorStop(1, "rgba(140,220,255,0)")
            ctx.fillStyle = h
            ctx.beginPath()
            ctx.arc(r, r, r * 0.92, 0.1 * Math.PI, 0.9 * Math.PI)
            ctx.fill()
        }
        layer.enabled: true
        layer.effect: MultiEffect {
            autoPaddingEnabled: true
            shadowEnabled: true
            shadowColor: AuraTheme.accentSoft
            shadowBlur: 1.0
            shadowOpacity: 0.7
            shadowVerticalOffset: 0
            blurMax: 40
        }
        RotationAnimation on rotation {
            running: root.animate
            from: 0; to: 360; duration: 14000
            loops: Animation.Infinite
        }
    }
}
