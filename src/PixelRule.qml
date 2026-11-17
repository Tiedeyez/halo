import QtQuick
import Halo

// A dotted rule of square pixels, for the edges between panels.
Item {
    id: root
    property bool vertical: false
    property color color: Style.line
    property int dot: 2
    property int gap: 3
    implicitWidth: vertical ? dot : 100
    implicitHeight: vertical ? 100 : dot

    Repeater {
        model: Math.ceil((root.vertical ? root.height : root.width) / (root.dot + root.gap))
        Rectangle {
            required property int index
            x: root.vertical ? 0 : index * (root.dot + root.gap)
            y: root.vertical ? index * (root.dot + root.gap) : 0
            width: root.dot
            height: root.dot
            color: root.color
        }
    }
}
