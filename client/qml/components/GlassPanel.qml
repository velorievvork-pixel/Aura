import QtQuick
import QtQuick.Effects
import Selfvoy

/*
  GlassPanel — стеклянная карточка (списки, формы, диалоги) поверх GlassSurface:
  мягкая тень, отступы и, при glow, фиолетовое свечение вокруг.
*/
Item {
    id: root

    property alias backdrop: surface.backdrop
    property alias radius: surface.radius
    property alias edge: surface.edge
    property alias base: surface.base
    property real fill: SelfvoyTheme.glassFill
    property alias tint: surface.tint
    property bool hovered: false
    property int padding: SelfvoyTheme.spaceLg
    // Фиолетовое свечение вокруг панели (карточка «Selfvoy активен» и т.п.)
    property bool glow: false
    property color glowColor: SelfvoyTheme.accent
    property bool shadow: true
    // data, а не children: внутрь панели кладут и невизуальные объекты
    // (Connections, Timer) — список children принимает только Item.
    default property alias content: holder.data

    // holder — обычный Item, его собственный implicit-размер всегда 0;
    // панель меряет содержимое (обычно один Layout внутри) по детям.
    implicitWidth: contentImplicit("implicitWidth") + padding * 2
    implicitHeight: contentImplicit("implicitHeight") + padding * 2

    function contentImplicit(prop) {
        let size = 0
        for (let i = 0; i < holder.children.length; ++i) {
            const child = holder.children[i]
            if (child.visible) size = Math.max(size, child[prop])
        }
        return size
    }

    // Тень и свечение: размытая копия формы панели под ней.
    Rectangle {
        id: halo
        anchors.fill: surface
        radius: surface.radius
        color: root.glow ? root.glowColor : "#000000"
        visible: root.glow || root.shadow
        opacity: root.glow ? 0.85 : 0.55
        layer.enabled: visible
        layer.effect: MultiEffect {
            autoPaddingEnabled: true
            blurEnabled: true
            blurMax: root.glow ? 64 : 48
            blur: 1.0
        }
        transform: Translate { y: root.glow ? 0 : 14 }
        Behavior on opacity { NumberAnimation { duration: SelfvoyTheme.slowMs } }
    }

    GlassSurface {
        id: surface
        anchors.fill: parent
        radius: SelfvoyTheme.radiusCard
        fill: root.fill
        hovered: root.hovered

        Item {
            id: holder
            anchors.fill: parent
            anchors.margins: root.padding
        }
    }
}
