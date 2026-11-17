import QtQuick
import Halo

// "HALO" in square pixels with a drop shadow, after the pixel OMARCHY
// wordmark on Omarchy's wallpaper and the shaded block letters of its
// screensaver. The pixels pour in when the window opens, and every so often
// a few rows tear sideways for a moment. Calm keeps it still.
Item {
    id: root
    property string word: "HALO"
    property int cell: 4
    property color color: Style.accent
    property color shadow: Qt.darker(Style.accent, 3.2)

    // 5x7 letters, one string per row.
    readonly property var font: ({
        "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
        "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
        "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
        "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"]
    })
    readonly property var pixels: {
        const out = []
        for (let l = 0; l < word.length; ++l) {
            const rows = font[word[l]]
            if (!rows)
                continue
            for (let y = 0; y < 7; ++y)
                for (let x = 0; x < 5; ++x)
                    if (rows[y][x] === "1")
                        out.push({ x: l * 6 + x, y: y })
        }
        return out
    }
    // How far each of the 7 rows is torn sideways, in pixels of the word.
    property var tear: [0, 0, 0, 0, 0, 0, 0]

    implicitWidth: (word.length * 6 - 1) * cell + cell
    implicitHeight: 8 * cell
    Accessible.role: Accessible.Heading
    Accessible.name: word

    Repeater {
        model: root.pixels
        delegate: Item {
            id: pixel
            required property var modelData
            x: (modelData.x + root.tear[modelData.y]) * root.cell
            y: modelData.y * root.cell
            width: root.cell * 2
            height: root.cell * 2
            opacity: 0

            Rectangle { x: root.cell; y: root.cell; width: root.cell; height: root.cell; color: root.shadow }
            Rectangle {
                width: root.cell
                height: root.cell
                color: root.tear[pixel.modelData.y] !== 0 ? Style.text : root.color
            }

            SequentialAnimation {
                id: pour
                PauseAnimation { duration: Math.random() * 520 + pixel.modelData.x * 12 }
                NumberAnimation { target: pixel; property: "opacity"; to: 1; duration: 60 }
            }
            Component.onCompleted: {
                if (Style.motion)
                    pour.start()
                else
                    opacity = 1
            }
        }
    }

    Timer {
        id: idle
        interval: 7000 + Math.random() * 6000
        repeat: true
        running: Style.motion && root.visible
        onTriggered: {
            interval = 7000 + Math.random() * 6000
            const t = [0, 0, 0, 0, 0, 0, 0]
            const rows = 1 + Math.floor(Math.random() * 3)
            for (let i = 0; i < rows; ++i)
                t[Math.floor(Math.random() * 7)] = Math.random() < 0.5 ? -1 : 1
            root.tear = t
            heal.restart()
        }
    }
    Timer {
        id: heal
        interval: 110
        onTriggered: root.tear = [0, 0, 0, 0, 0, 0, 0]
    }
}
