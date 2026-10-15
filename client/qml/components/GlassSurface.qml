import QtQuick
import QtQuick.Effects
import Selfvoy

/*
  GlassSurface — базовая «стеклянная» поверхность (v3).

  Слои снизу вверх: сиреневый градиентный край (edge) → размытый фон окна под
  панелью (backdrop) либо плотная подложка → полупрозрачная плёнка → блик
  по верхнему краю. Край виден как тонкое кольцо в 1 px: всё остальное
  перекрыто следующими слоями.
*/
Item {
    id: root

    // Что размывать под стеклом (обычно — фон окна).
    property Item backdrop: null
    property real blurStrength: 64
    property color tint: SelfvoyTheme.glassTint
    property real fill: SelfvoyTheme.glassFill
    property real fillHover: fill + 0.03
    property real borderAlpha: SelfvoyTheme.glassBorder
    // Плотность подложки, когда размывать нечего (0 — прозрачно).
    property real base: 0.78
    property bool edge: true          // сиреневый блик по краю, как в макетах
    property real edgeStrength: 0.7
    property bool hovered: false
    property bool pressed: false
    property bool showHighlight: true
    property real radius: SelfvoyTheme.radiusCard

    default property alias content: holder.data

    readonly property bool blurred: backdrop !== null && backdrop.width > 0
    readonly property point origin: backdrop ? mapToItem(backdrop, 0, 0) : Qt.point(0, 0)

    // Подложка непрозрачна (есть размытие или base≈1) — край рисуется градиентом
    // под ней; иначе градиент просвечивал бы сквозь всю панель, и край — рамка плёнки.
    readonly property bool solidBase: blurred || base >= 0.95

    // 1. Край: вертикальный градиент сирени, сверху ярче, в середине гаснет.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        visible: root.edge && root.solidBase
        gradient: Gradient {
            GradientStop { position: 0.0; color: SelfvoyTheme.lilacA(root.edgeStrength) }
            GradientStop { position: 0.35; color: Qt.rgba(1, 1, 1, 0.06) }
            GradientStop { position: 0.7; color: Qt.rgba(1, 1, 1, 0.03) }
            GradientStop { position: 1.0; color: SelfvoyTheme.lilacA(root.edgeStrength * 0.6) }
        }
    }

    // 2. Размытый фон окна под панелью
    ShaderEffectSource {
        id: backdropSource
        anchors.fill: inner
        visible: false
        live: true
        sourceItem: root.backdrop
        sourceRect: root.blurred
                    ? Qt.rect(root.origin.x + 1, root.origin.y + 1, inner.width, inner.height)
                    : Qt.rect(0, 0, 0, 0)
    }

    Item {
        id: cornerMask
        anchors.fill: inner
        visible: false
        layer.enabled: root.blurred
        layer.smooth: true
        Rectangle {
            anchors.fill: parent
            radius: Math.max(0, root.radius - 1)
            color: "#ffffff"
        }
    }

    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: root.edge && root.solidBase ? 1 : 0
    }

    // Плотная подложка (под размытием тоже — чтобы край не просвечивал сквозь стекло)
    Rectangle {
        anchors.fill: inner
        radius: Math.max(0, root.radius - 1)
        color: Qt.rgba(SelfvoyTheme.bgPanel.r, SelfvoyTheme.bgPanel.g, SelfvoyTheme.bgPanel.b,
                       root.blurred ? 1.0 : root.base)
        visible: root.blurred || root.base > 0
    }

    MultiEffect {
        anchors.fill: inner
        visible: root.blurred
        source: backdropSource
        blurEnabled: true
        blur: 1.0
        blurMax: root.blurStrength
        maskEnabled: true
        maskSource: cornerMask
        opacity: 0.92
    }

    // 3. Плёнка стекла: полупрозрачный белый + тонкая рамка.
    Rectangle {
        id: film
        anchors.fill: inner
        radius: Math.max(0, root.radius - 1)
        color: Qt.rgba(root.tint.r, root.tint.g, root.tint.b,
                       root.pressed ? SelfvoyTheme.glassFillPressed
                                    : (root.hovered ? root.fillHover : root.fill))
        border.width: root.edge && root.solidBase ? 0 : 1
        border.color: root.edge ? SelfvoyTheme.lilacA(0.3) : Qt.rgba(1, 1, 1, root.borderAlpha)
        Behavior on color { ColorAnimation { duration: SelfvoyTheme.pressMs } }
    }

    // 4. Блик сверху — ощущение объёма.
    //    Той же формы, что и плёнка: у низких пилюль урезанный по высоте блик
    //    получал меньший радиус, и его углы вылезали за скругление.
    Rectangle {
        anchors.fill: inner
        radius: film.radius
        visible: root.showHighlight
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.07) }
            GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0.0) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    Item {
        id: holder
        anchors.fill: parent
    }
}
