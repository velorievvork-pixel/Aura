import QtQuick

/*
  GlassPanel — стеклянная карточка (списки, формы, диалог).
  Тонкая обёртка над GlassSurface с тенью и отступами.
*/
Item {
    id: root

    property alias backdrop: surface.backdrop
    property alias radius: surface.radius
    property real fill: AuraTheme.glassFill
    property bool hovered: false
    property int padding: AuraTheme.spaceLg
    // v3: фиолетовое свечение вокруг панели (карточка «Аура активна» и т.п.)
    property bool glow: false
    property alias content: holder.data
    default property alias children_: holder.children

    implicitWidth: holder.implicitWidth + padding * 2
    implicitHeight: holder.implicitHeight + padding * 2

    // Ambient-подсветка под панелью (v3)
    Rectangle {
        anchors.fill: surface
        anchors.margins: -12
        radius: surface.radius + 12
        color: AuraTheme.accentGlow
        opacity: root.glow ? 0.55 : 0
        visible: root.glow
        Behavior on opacity { NumberAnimation { duration: AuraTheme.slowMs } }
    }

    // Мягкая тень под панелью
    Rectangle {
        anchors.fill: surface
        anchors.margins: -10
        radius: surface.radius + 10
        color: AuraTheme.shadowColor
        opacity: 0.45
    }

    GlassSurface {
        id: surface
        anchors.fill: parent
        radius: AuraTheme.radiusCard
        fill: root.fill
        hovered: root.hovered

        Item {
            id: holder
            anchors.fill: parent
            anchors.margins: root.padding
        }
    }
}
