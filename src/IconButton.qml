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

    background: Item {
        // The main button is a pixel-art circle; it pushes in when pressed.
        PixelCircle {
            anchors.fill: parent
            visible: button.primary
            pixel: 4
            color: button.hovered ? Qt.lighter(Style.accent, 1.12) : Style.accent
            scale: button.down ? 0.92 : button.hovered ? 1.04 : 1
            Behavior on scale { NumberAnimation { duration: Style.quick; easing.type: Easing.OutBack; easing.overshoot: 2.2 } }
        }
        Rectangle {
            anchors.fill: parent
            visible: !button.primary
            radius: Style.small
            color: button.down ? Style.line : button.hovered ? Style.hover : "transparent"
            Behavior on color { ColorAnimation { duration: Style.quick; easing.type: Easing.OutCubic } }
        }
        // Pixel corner marks on the button under the pointer.
        PixelBrackets {
            shown: !button.primary && button.hovered && button.enabled
            arm: 6
            color: button.active ? Style.accent : Style.muted
        }
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
            color: Style.accent
        }
    }

    ToolTip.visible: hovered && tip.length > 0
    ToolTip.delay: 600
    ToolTip.text: tip
}
