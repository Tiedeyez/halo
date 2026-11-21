import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// Halo's volume: a mute button and the volume as a row of pixel bars
// rising to the right, like a signal meter (drag or click across it, or
// scroll over it). This is Halo's own volume, not the system's; used in the
// bottom bar and on Now Playing (where the bar is hidden).
RowLayout {
    spacing: 4
    IconButton {
        glyph: player.volume === 0 ? "mute" : "volume"
        tip: qsTr("Mute")
        property real before: 0.8
        onClicked: {
            if (player.volume > 0) { before = player.volume; player.volume = 0 }
            else player.volume = before
        }
    }
    Slider {
        id: volume
        Layout.preferredWidth: 108
        Layout.preferredHeight: Style.target
        from: 0
        to: 1
        value: player.volume
        onMoved: player.volume = value
        Accessible.name: qsTr("Volume")
        focusPolicy: Qt.NoFocus
        background: Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Repeater {
                model: 12
                Rectangle {
                    required property int index
                    anchors.bottom: parent.bottom
                    width: (volume.width - 22) / 12
                    height: 4 + index * 1.2
                    color: (index + 0.5) / 12 <= volume.visualPosition ? Style.accent : Style.line
                }
            }
        }
        handle: Rectangle {
            x: volume.leftPadding + volume.visualPosition * (volume.availableWidth - width)
            anchors.verticalCenter: parent.verticalCenter
            width: volume.hovered || volume.pressed ? 6 : 0
            height: 20
            color: Style.text
        }
        WheelHandler {
            onWheel: e => player.volume = Math.max(0, Math.min(1, player.volume + (e.angleDelta.y > 0 ? 0.05 : -0.05)))
        }
    }
}
