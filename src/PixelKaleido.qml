import QtQuick
import QtQuick.Window
import Halo

// The Kaleidoscope visual (V cycles to it): a mirrored fractal of the song's
// own, in square pixels and the Omarchy theme's colours. KaleidoPilot works
// it out from the file (mirrors and shape from the song, colours from the
// album, the turn from its tempo, a path across its length, glow from its
// loudness) and turns it with the music; shaders/kaleido.frag draws it. It
// steps once per screen refresh while music plays; calm (C) holds it still.
Item {
    id: root
    property int cell: 6
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    KaleidoPilot { id: pilot }
    function loadSong() {
        pilot.setSong(player.currentPath, player.title, player.artist, player.album,
                      player.bpm, player.duration, player.loudness)
    }
    Connections {
        target: player
        function onCurrentChanged() { root.loadSong(); clock.position = player.position }
        function onAnalysisChanged() { root.loadSong() }
        // The player reports its position about 30 times a second; between
        // reports it runs on the frame clock, and a seek catches up.
        function onPositionChanged() {
            if (Math.abs(player.position - clock.position) > 0.25)
                clock.position = player.position
        }
    }
    Component.onCompleted: { loadSong(); clock.position = player.position }

    MotionClock {
        id: clock
        running: root.moving
        property real position: 0
        onTicked: dt => {
            clock.position += dt
            // Drift back toward the player's clock without a visible step.
            clock.position += (player.position - clock.position) * Math.min(1, dt * 2)
            pilot.step(dt, clock.position, clock.pulse, clock.low, clock.mid)
        }
    }

    // The kaleidoscope, one fragment per cell, into a small texture...
    ShaderEffect {
        id: scope
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        property real pulse: root.moving ? clock.pulse : 0
        property real level: root.moving ? clock.level : 0.4
        property real high: root.moving ? clock.high : 0.3
        property size size: Qt.size(width, height)
        property vector4d fold: Qt.vector4d(pilot.folds, pilot.previousFolds, pilot.foldMix, pilot.spin)
        property vector4d shape: Qt.vector4d(pilot.cx, pilot.cy, pilot.ring, pilot.twist)
        property vector4d view: Qt.vector4d(pilot.zoom, pilot.hue, pilot.glow, root.moving ? pilot.kick : 0)
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        property color c4: Style.paletteColour(4)
        property color c5: Style.paletteColour(5)
        property color window: Style.window
        fragmentShader: "qrc:/shaders/kaleido.frag.qsb"
    }
    ShaderEffectSource {
        id: texture
        sourceItem: scope
        hideSource: true
        smooth: false
        visible: false
        textureSize: Qt.size(scope.width, scope.height)
    }
    // ...enlarged into square screen pixels with gaps between them.
    ShaderEffect {
        width: scope.width * root.cell
        height: scope.height * root.cell
        anchors.centerIn: parent
        visible: root.supported
        property variant source: texture
        property size cells: Qt.size(scope.width, scope.height)
        property real cell: root.cell
        property color gapColour: Qt.rgba(Style.window.r * 0.6, Style.window.g * 0.6, Style.window.b * 0.6, 1)
        fragmentShader: "qrc:/shaders/pixelgrid.frag.qsb"
    }
}
