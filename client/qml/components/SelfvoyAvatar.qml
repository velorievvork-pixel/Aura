import QtQuick
import Selfvoy

/*
  SelfvoyAvatar — круглый аватар с инициалами и градиентом «северного сияния».
  agent: true — обводка и значок Selfvoy.
*/
Item {
    id: root

    property string name: ""
    property bool agent: false
    property bool online: false
    property int size: 38

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size

    readonly property string initials: {
        const parts = name.trim().split(/\s+/).filter(part => part.length > 0)
        if (parts.length === 0) return agent ? "AI" : "?"
        if (parts.length === 1) return parts[0].substring(0, 1).toUpperCase()
        return (parts[0].substring(0, 1) + parts[1].substring(0, 1)).toUpperCase()
    }

    // Оттенок — из имени: у каждого человека свой цвет шара, стабильный между запусками.
    property real hue: {
        let h = 0
        for (let i = 0; i < name.length; ++i) h = (h * 31 + name.charCodeAt(i)) % 360
        return agent ? 265 : h
    }
    property bool ring: false

    Rectangle {
        anchors.fill: parent
        anchors.margins: -5
        radius: width / 2
        visible: root.ring
        color: "transparent"
        border.width: 3
        border.color: SelfvoyTheme.lilacA(0.25)
    }

    Rectangle {
        id: circle
        anchors.fill: parent
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.hsla(root.hue / 360, 0.85, 0.74, 1) }
            GradientStop { position: 0.55; color: Qt.hsla(root.hue / 360, 0.68, 0.52, 1) }
            GradientStop { position: 1.0; color: Qt.hsla(((root.hue + 30) % 360) / 360, 0.7, 0.3, 1) }
        }
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, root.ring ? 0.3 : 0.14)
    }

    Text {
        anchors.centerIn: parent
        text: root.initials
        font.family: SelfvoyTheme.fontFamily
        font.pixelSize: Math.max(10, root.size * 0.36)
        font.weight: Font.Bold
        color: "#FFFFFF"
    }

    // Индикатор «онлайн»
    Rectangle {
        width: Math.max(8, root.size * 0.26)
        height: width
        radius: width / 2
        color: SelfvoyTheme.online
        border.width: 2
        border.color: SelfvoyTheme.bgPanel
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.online
    }
}
