import QtQuick
import QtQuick.Window
import Halo

// The Hologram visual: the record as light, the album cover its label
// (shaders/vinyl.frag). It turns at 33 1/3 while music plays and stops where
// it is when paused or calm (C); the tonearm follows the song's progress.
Item {
    id: root
    property int cell: 5
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    MotionClock {
        id: clock
        running: root.moving
        property real spin: 0
        property vector4d lo: Qt.vector4d(0, 0, 0, 0)
        property vector4d hi: Qt.vector4d(0, 0, 0, 0)
        onTicked: dt => {
            // 33 1/3 revolutions a minute; a beat nudges it like a DJ's hand.
            clock.spin += dt * (Math.PI * 2 * 33.333 / 60) * (1 + 0.04 * clock.pulse)
            const s = spectrumSource
            clock.lo = Qt.vector4d(s.bandAverage(0, 4), s.bandAverage(4, 10), s.bandAverage(10, 18), s.bandAverage(18, 26))
            clock.hi = Qt.vector4d(s.bandAverage(26, 34), s.bandAverage(34, 44), s.bandAverage(44, 54), s.bandAverage(54, 64))
        }
    }

    // The cover, small, as a texture for the label.
    Image {
        id: label
        source: player.albumId.length ? "image://cover/" + player.albumId : ""
        sourceSize: Qt.size(256, 256)
        asynchronous: true
        visible: false
        smooth: true
    }

    ShaderEffect {
        id: disc
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        property size size: Qt.size(width, height)
        property real spin: clock.spin
        property real time: clock.time
        property real pulse: root.moving ? clock.pulse : 0
        property real kick: root.moving ? Math.max(0, clock.pulse * 1.6 - 0.6) : 0
        property real level: root.moving ? clock.level : 0.35
        property real high: root.moving ? clock.high : 0.2
        property real progress: player.duration > 0 ? player.position / player.duration : 0
        property real hasCover: label.status === Image.Ready ? 1 : 0
        property vector4d bandsLo: root.moving ? clock.lo : Qt.vector4d(0.3, 0.3, 0.25, 0.2)
        property vector4d bandsHi: root.moving ? clock.hi : Qt.vector4d(0.18, 0.15, 0.12, 0.1)
        property variant cover: label
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        property color c5: Style.paletteColour(5)
        property color window: Style.window
        fragmentShader: "qrc:/shaders/vinyl.frag.qsb"
    }
    ShaderEffectSource {
        id: texture
        sourceItem: disc
        hideSource: true
        smooth: false
        visible: false
        textureSize: Qt.size(disc.width, disc.height)
    }
    ShaderEffect {
        width: disc.width * root.cell
        height: disc.height * root.cell
        anchors.centerIn: parent
        visible: root.supported
        property variant source: texture
        property size cells: Qt.size(disc.width, disc.height)
        property real cell: root.cell
        property color gapColour: Qt.rgba(0, 0, 0, 0)
        fragmentShader: "qrc:/shaders/pixelgrid.frag.qsb"
    }
}
