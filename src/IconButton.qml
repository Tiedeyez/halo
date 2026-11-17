import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Halo

// A 44 px target with one glyph, a tooltip that names it, and an accessible
// name for screen readers.
AbstractButton {
    id: button
    property string glyph
    property string tip
    property bool active: false
    property bool primary: false
    property int glyphSize: 22

    implicitWidth: primary ? 56 : Style.target
    implicitHeight: primary ? 56 : Style.target
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    Accessible.name: tip
    Accessible.role: Accessible.Button

    background: Rectangle {
        radius: button.primary ? width / 2 : Style.small
        color: button.primary ? Style.accent
             : button.down ? Style.line
             : button.hovered ? Style.hover : "transparent"
        scale: button.primary && button.down ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 80 } }
    }

    contentItem: Item {
        Glyph {
            anchors.centerIn: parent
            width: button.primary ? 28 : button.glyphSize
            height: width
            name: button.glyph
            color: button.primary ? Style.onAccent
                 : !button.enabled ? Style.line
                 : button.active ? Style.accentText : Style.muted
        }
        Rectangle {
            visible: button.active && !button.primary
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 4
            width: 4
            height: 4
            radius: 2
            color: Style.accent
        }
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
}
