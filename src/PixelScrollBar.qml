import QtQuick
import QtQuick.Controls
import Halo

// A square scrollbar handle, brighter when held.
ScrollBar {
    id: bar
    padding: 2
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        color: bar.pressed ? Style.accent : bar.hovered ? Style.muted : Style.line
        opacity: bar.policy === ScrollBar.AlwaysOn || bar.active ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }
    background: null
}
