import QtQuick
import CymtX

// A tiny meter for the song playing: lows, mids and highs as three columns
// of square pixels. Still (and low) while paused or calm.
Row {
    id: root
    property bool live: player.playing && Style.motion
    property color color: Style.accent
    property int cell: 3
    readonly property var levels: live ? [spectrumSource.low, spectrumSource.mid, spectrumSource.high] : [0.25, 0.5, 0.25]
    spacing: 2
    Accessible.ignored: true

    Repeater {
        model: 3
        Column {
            required property int index
            readonly property int lit: Math.max(1, Math.min(4, Math.round(1 + root.levels[index] * 5)))
            spacing: 1
            Repeater {
                model: 4
                Rectangle {
                    required property int index
                    width: root.cell
                    height: root.cell
                    // Cells fill from the bottom.
                    color: 3 - index < parent.lit ? root.color : Qt.alpha(root.color, 0.18)
                }
            }
        }
    }
}
