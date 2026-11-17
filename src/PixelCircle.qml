import QtQuick
import QtQuick.Shapes
import Halo

// A circle drawn as pixel art: its edge steps in whole pixels of `pixel`
// size, like a sprite, instead of curving smoothly.
Item {
    id: root
    property color color: Style.accent
    property int pixel: 4

    readonly property string path: {
        const d = Math.min(width, height)
        const p = Math.max(1, pixel)
        const rows = Math.max(2, Math.round(d / p))
        const r = rows / 2
        const cx = width / 2, top = (height - rows * p) / 2
        const half = []
        for (let i = 0; i < rows; ++i) {
            const y = i + 0.5 - r
            half.push(Math.round(Math.sqrt(Math.max(0, r * r - y * y))))
        }
        // Down the right edge, then up the left one, step by step.
        let s = ""
        for (let i = 0; i < rows; ++i) {
            const x = cx + half[i] * p
            s += (i === 0 ? "M " : "L ") + x + " " + (top + i * p) + " L " + x + " " + (top + (i + 1) * p) + " "
        }
        for (let i = rows - 1; i >= 0; --i) {
            const x = cx - half[i] * p
            s += "L " + x + " " + (top + (i + 1) * p) + " L " + x + " " + (top + i * p) + " "
        }
        return s + "Z"
    }

    Shape {
        anchors.fill: parent
        antialiasing: false
        ShapePath {
            fillColor: root.color
            strokeColor: "transparent"
            strokeWidth: 0
            PathSvg { path: root.path }
        }
    }
}
