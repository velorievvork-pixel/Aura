import QtQuick
import QtQuick.Controls
import Selfvoy

// GlassComboBox — выпадающий список в стиле полей v3 (тёмное стекло, сиреневый фокус).
ComboBox {
    id: control
    implicitHeight: 44
    implicitWidth: 180
    font.family: SelfvoyTheme.fontFamily
    font.pixelSize: SelfvoyTheme.fontSmall + 1

    background: Rectangle {
        radius: SelfvoyTheme.radiusField
        color: Qt.rgba(0.04, 0.03, 0.07, control.pressed ? 0.6 : 0.45)
        border.width: 1
        border.color: control.visualFocus || control.popup.visible ? SelfvoyTheme.accentA(0.8) : Qt.rgba(1, 1, 1, 0.16)
    }
    contentItem: Text {
        leftPadding: 14
        rightPadding: 34
        text: control.displayText
        font: control.font
        color: SelfvoyTheme.textPrimary
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: control.width - width - 12
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        size: 16
    }
    delegate: ItemDelegate {
        id: option
        required property int index
        required property var modelData
        width: control.width
        height: 38
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: control.textRole ? (Array.isArray(control.model) ? option.modelData[control.textRole] : option.modelData)
                                   : option.modelData
            font: control.font
            color: SelfvoyTheme.textPrimary
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 10
            color: option.highlighted ? SelfvoyTheme.accentA(0.25) : "transparent"
        }
    }
    popup: Popup {
        y: control.height + 6
        width: control.width
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + 12, 320)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
        }
        background: Rectangle {
            radius: SelfvoyTheme.radiusField
            color: SelfvoyTheme.bgElevated
            border.width: 1
            border.color: SelfvoyTheme.lilacA(0.3)
        }
    }
}
