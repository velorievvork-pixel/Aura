import QtQuick

/*
  AuraEmblem — круглая светящаяся эмблема-логотип (v3, макеты 30.09.2026).

  Кольцо из коротких лучей вокруг ядра; лучи медленно вращаются, свечение
  пульсирует. Используется на LoginPage (размер 64+) и в рельсе оболочки.
*/
Item {
    id: root

    property int size: 64
    property int rays: 12
    property bool animate: true
    property color coreColor: AuraTheme.accentSoft

    width: size
    height: size

    // Мягкое свечение за эмблемой
    Rectangle {
        anchors.centerIn: parent
        width: root.size * 1.35
        height: width
        radius: width / 2
        color: AuraTheme.accentGlow
        opacity: root.animate ? glowPulse.value : 0.8

        QtObject {
            id: glowPulse
            property real value: 0.8
        }
    }

    SequentialAnimation {
        running: root.animate
        loops: Animation.Infinite
        NumberAnimation { target: glowPulse; property: "value"; to: 0.35; duration: 2400; easing.type: Easing.InOutSine }
        NumberAnimation { target: glowPulse; property: "value"; to: 0.9; duration: 2400; easing.type: Easing.InOutSine }
    }

    // Кольцо лучей
    Canvas {
        id: raysCanvas
        anchors.fill: parent
        rotation: root.animate ? raysSpin.angle : 0

        QtObject {
            id: raysSpin
            property real angle: 0
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const cx = width / 2
            const cy = height / 2
            const outer = width * 0.48
            const inner = width * 0.38
            ctx.strokeStyle = root.coreColor
            ctx.lineWidth = Math.max(1.5, width / 42)
            for (let i = 0; i < root.rays; ++i) {
                const a = (2 * Math.PI * i) / root.rays
                ctx.beginPath()
                ctx.moveTo(cx + Math.cos(a) * inner, cy + Math.sin(a) * inner)
                ctx.lineTo(cx + Math.cos(a) * outer, cy + Math.sin(a) * outer)
                ctx.stroke()
            }
            // Тонкое внешнее кольцо
            ctx.beginPath()
            ctx.arc(cx, cy, width * 0.34, 0, 2 * Math.PI)
            ctx.strokeStyle = Qt.rgba(AuraTheme.accentSoft.r, AuraTheme.accentSoft.g,
                                      AuraTheme.accentSoft.b, 0.35)
            ctx.lineWidth = 1
            ctx.stroke()
        }

        Component.onCompleted: requestPaint()
    }

    SequentialAnimation {
        running: root.animate
        loops: Animation.Infinite
        NumberAnimation { target: raysSpin; property: "angle"; to: 360; duration: 48000; easing.type: Easing.Linear }
    }

    // Ядро
    Rectangle {
        anchors.centerIn: parent
        width: root.size * 0.20
        height: width
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#FFFFFF" }
            GradientStop { position: 1.0; color: root.coreColor }
        }
    }
}
