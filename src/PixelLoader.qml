import QtQuick
import CymtX

// A row of square pixels for work in progress. With a known fraction the
// pixels fill left to right; without one a short bright run sweeps across,
// trailing block shades behind it. Calm shows the fill without the sweep.
Item {
    id: root
    property int count: 24
    property int cell: 8
    property int gap: 3
    property real fraction: -1 // below 0: unknown, so it sweeps
    property int head: 0

    implicitWidth: count * cell + (count - 1) * gap
    implicitHeight: cell
    Accessible.role: Accessible.ProgressBar
    Accessible.name: fraction >= 0 ? qsTr("%1 percent").arg(Math.round(fraction * 100)) : qsTr("Working")

    Repeater {
        model: root.count
        delegate: Rectangle {
            required property int index
            x: index * (root.cell + root.gap)
            width: root.cell
            height: root.cell
            readonly property int behind: (root.head - index + root.count) % root.count
            color: {
                if (root.fraction >= 0)
                    return index < Math.round(root.fraction * root.count) ? Style.accent : Style.line
                if (!Style.motion)
                    return Style.line
                return behind === 0 ? Style.accent
                     : behind < 4 ? Qt.alpha(Style.accent, 0.75 - behind * 0.2)
                     : Style.line
            }
        }
    }

    Timer {
        interval: 55
        repeat: true
        running: root.visible && root.fraction < 0 && Style.motion
        onTriggered: root.head = (root.head + 1) % root.count
    }
}
