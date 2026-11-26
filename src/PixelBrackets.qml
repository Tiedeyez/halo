import QtQuick
import CymtX

// Four corner brackets drawn in pixels, a terminal's selection marks. They
// snap in from just outside the item when shown.
Item {
    id: root
    property bool shown: false
    property color color: Style.accent
    property int arm: 10
    property int thickness: 2
    property real spread: shown ? 0 : 5
    anchors.fill: parent
    visible: opacity > 0
    opacity: shown ? 1 : 0
    Behavior on opacity { enabled: Style.motion; NumberAnimation { duration: 90 } }
    Behavior on spread { enabled: Style.motion; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }

    Repeater {
        model: 4
        Item {
            required property int index
            readonly property bool atRight: index % 2 === 1
            readonly property bool atBottom: index > 1
            x: atRight ? root.width - root.arm + root.spread : -root.spread
            y: atBottom ? root.height - root.arm + root.spread : -root.spread
            width: root.arm
            height: root.arm
            Rectangle {
                x: 0
                y: parent.atBottom ? parent.height - root.thickness : 0
                width: parent.width
                height: root.thickness
                color: root.color
            }
            Rectangle {
                x: parent.atRight ? parent.width - root.thickness : 0
                y: 0
                width: root.thickness
                height: parent.height
                color: root.color
            }
        }
    }
}
