import QtQuick
import QtQuick.Window
import Halo

// Cymatics: sand on a plate that the music shakes into figures (Chladni's
// experiment). CymaticsPilot picks the figure from the music's pitch and
// changes it on the beat; shaders/cymatics.frag draws the sand, one cell per
// square pixel.
//
// It is used twice. As the window's background (`vivid` false) the plate is
// the whole window (the figure repeats, `plate` pixels a tile), dim, in the accent, changing every eight beats and
// keeping to the broad figures; the brightest a cell gets is worked out
// from the theme so the faintest text over it still reads at 7:1 (WCAG
// AAA), lifting a little on the beat. As the Cymatics visual (`vivid` true)
// it is one bright square plate, `plate` pixels across and centred on
// `origin`, changing every two beats across the whole list of figures.
//
// A click anywhere throws the sand round the pointer. It moves while music
// plays or sand is still settling, so a quiet Halo costs no frames, and
// calm (C) stills it.
Item {
    id: root
    property bool vivid: false
    property point origin: Qt.point(width * 0.5, height * 0.5)
    // The plate's side in pixels; 0 makes it as tall as the item.
    property real plate: 0
    property int cell: 6
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool shown: visible && root.Window.window && root.Window.window.visibility !== Window.Minimized
    // Seconds of `clock.time` until the sand has settled.
    property real stillAt: 0
    readonly property bool moving: Style.motion && supported && shown
                                   && (player.playing || clock.time < stillAt)

    // Highest mix of the accent into the window colour that keeps faint
    // text at 7:1 or better.
    readonly property real strength: {
        let lo = 0, hi = 0.25
        for (let i = 0; i < 12; ++i) {
            const mid = (lo + hi) / 2
            const bg = Qt.rgba(Style.window.r + (Style.accent.r - Style.window.r) * mid,
                               Style.window.g + (Style.accent.g - Style.window.g) * mid,
                               Style.window.b + (Style.accent.b - Style.window.b) * mid, 1)
            if (Style.contrast(Style.faint, bg) >= 7.05) lo = mid
            else hi = mid
        }
        return lo
    }
    // How bright the sand gets for the moment of a beat.
    property real flash: 0.42

    // The last click: x, y in cells, when, how hard.
    property vector4d tap: Qt.vector4d(0, 0, -10, 0)
    function throwSand(x, y, force) {
        if (!Style.motion || !supported || !shown)
            return
        tap = Qt.vector4d(x / cell, y / cell, clock.time, force)
        stillAt = Math.max(stillAt, clock.time + 2.6)
    }
    Connections {
        target: pointer
        function onPressed(x, y) {
            const p = root.mapFromItem(null, x, y)
            if (root.contains(p))
                root.throwSand(p.x, p.y, 1)
        }
    }
    // Paused: the figure being formed finishes, then holds.
    Connections {
        target: player
        function onPlayingChanged() {
            if (!player.playing)
                root.stillAt = Math.max(root.stillAt, clock.time + 1)
        }
    }

    CymaticsPilot {
        id: pilot
        beatsPerChange: root.vivid ? 2 : 8
        reach: root.vivid ? 1 : 0.42
    }

    MotionClock {
        id: clock
        running: root.moving
        onTicked: dt => pilot.step(dt, player.playing ? clock.pulse : 0, player.playing ? clock.level : 0,
                                   spectrumSource.centroid)
    }

    // The plate, one fragment per cell, into a small texture...
    ShaderEffect {
        id: field
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        property real time: clock.time
        property real morph: pilot.morph
        property real agitation: root.moving ? pilot.agitation : 0
        property real gather: pilot.gather
        property real kick: root.moving ? pilot.kick : 0
        property real low: root.moving && player.playing ? clock.low : 0
        property real strength: root.strength
        property real flash: root.flash
        property real vivid: root.vivid ? 1 : 0
        property real unit: (root.plate > 0 ? root.plate : root.height) / root.cell
        property size size: Qt.size(width, height)
        property point centre: Qt.point(root.origin.x / root.cell, root.origin.y / root.cell)
        property vector4d modeA: pilot.from
        property vector4d modeB: pilot.to
        property vector4d tap: root.tap
        property color accent: Style.accent
        property color shine: root.vivid ? Style.paletteColour(5) : Style.paletteColour(4)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        fragmentShader: "qrc:/shaders/cymatics.frag.qsb"
    }
    ShaderEffectSource {
        id: texture
        sourceItem: field
        hideSource: true
        // The visual's LEDs bloom from the texture's mipmaps; cells are
        // sampled at texel centres either way, so they stay exact.
        smooth: root.vivid
        mipmap: root.vivid
        visible: false
        textureSize: Qt.size(field.width, field.height)
    }
    // ...enlarged into square screen pixels with gaps between them: plain
    // for the background, lit like LEDs for the visual.
    Loader {
        active: root.supported
        sourceComponent: root.vivid ? leds : pixels
    }
    Component {
        id: pixels
        ShaderEffect {
            width: field.width * root.cell
            height: field.height * root.cell
            property variant source: texture
            property size cells: Qt.size(field.width, field.height)
            property real cell: root.cell
            property color gapColour: "transparent"
            fragmentShader: "qrc:/shaders/pixelgrid.frag.qsb"
        }
    }
    Component {
        id: leds
        ShaderEffect {
            width: field.width * root.cell
            height: field.height * root.cell
            property variant source: texture
            property size cells: Qt.size(field.width, field.height)
            property real cell: root.cell
            property color gapColour: Qt.rgba(0, 0, 0, 0)
            property real pulse: root.moving ? clock.pulse : 0
            property real glow: 0.9
            fragmentShader: "qrc:/shaders/ledgrid.frag.qsb"
        }
    }
}
