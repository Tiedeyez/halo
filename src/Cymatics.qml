import QtQuick
import QtQuick.Window
import CymtX

// Cymatics: sand on a round plate that the music shakes, worked out from the
// plate's own physics (SandPlate, src/sandplate.h): the plate's modes ring
// to the spectrum, the grains walk to where it stays still, and they are
// drawn with every mirror image, so each moment of a song is a mandala of
// square pixels. The song picks how many times it repeats round the centre.
//
// It is used twice. As the window's background (`vivid` false) one plate
// larger than the window lies behind the library, sand only, dim, in the
// accent: the brightest a cell gets is worked out from the theme so the
// faintest text over it still reads at 7:1 (WCAG AAA), lifting for the
// moment of a beat. As the Cymatics visual (`vivid` true) it is one bright
// plate, `plate` pixels across and centred on `origin`, lit like LEDs, the
// plate's own motion glowing under the sand.
//
// A click anywhere knocks the plate there. It moves while music plays or
// sand is still settling, so a quiet CymtX costs no frames, and calm (C)
// stills it.
Item {
    id: root
    property bool vivid: false
    property point origin: Qt.point(width * 0.5, height * 0.5)
    // The plate's width in pixels; 0 covers the item corner to corner.
    property real plate: 0
    property int cell: 6
    readonly property real span: plate > 0 ? plate : Math.hypot(width, height) * 1.04
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool shown: visible && root.Window.window && root.Window.window.visibility !== Window.Minimized
    // Seconds of `clock.time` until the sand has settled.
    property real stillAt: 0
    readonly property bool moving: Style.motion && shown && (player.playing || clock.time < stillAt)

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

    function loadSong() { sand.setSong(player.currentPath, player.title) }
    Connections {
        target: player
        function onCurrentChanged() { root.loadSong() }
        // Paused: the sand in the air comes down, then everything holds.
        function onPlayingChanged() {
            if (!player.playing)
                root.stillAt = Math.max(root.stillAt, clock.time + 0.6)
        }
    }
    Component.onCompleted: loadSong()

    Connections {
        target: pointer
        function onPressed(x, y) {
            const p = root.mapFromItem(null, x, y)
            if (!root.contains(p) || !Style.motion || !root.shown)
                return
            sand.tap(p.x / root.cell, p.y / root.cell, 1)
            root.stillAt = Math.max(root.stillAt, clock.time + 0.4)
        }
    }

    MotionClock {
        id: clock
        running: root.moving
        // The beat as the spectrum has it this instant, not the eased copy:
        // the sand answers at once.
        onTicked: dt => sand.step(dt, player.playing ? spectrumSource.pulse : 0, player.playing ? clock.level : 0)
    }

    // The plate, one cell per square pixel...
    SandPlate {
        id: sand
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        spectrum: spectrumSource
        vivid: root.vivid
        strength: root.strength
        flash: 0.42
        centre: Qt.point(root.origin.x / root.cell, root.origin.y / root.cell)
        radius: root.span / 2 / root.cell
        // About one grain for every two cells of the plate, before its
        // mirror images multiply it.
        grains: Math.max(2500, Math.min(root.vivid ? 9000 : 6000, Math.round(radius * radius * 0.4)))
        palette: [Style.paletteColour(0), Style.paletteColour(1), Style.paletteColour(2),
                  Style.paletteColour(3), Style.paletteColour(4), Style.paletteColour(5)]
        visible: !root.supported
        scale: root.supported ? 1 : root.cell
        transformOrigin: Item.TopLeft
    }
    ShaderEffectSource {
        id: texture
        sourceItem: sand
        hideSource: root.supported
        // The visual's LEDs bloom from the texture's mipmaps; cells are
        // sampled at texel centres either way, so they stay exact.
        smooth: root.vivid
        mipmap: root.vivid
        visible: false
        live: true
        textureSize: Qt.size(sand.width, sand.height)
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
            width: sand.width * root.cell
            height: sand.height * root.cell
            property variant source: texture
            property size cells: Qt.size(sand.width, sand.height)
            property real cell: root.cell
            property color gapColour: "transparent"
            fragmentShader: "qrc:/shaders/pixelgrid.frag.qsb"
        }
    }
    Component {
        id: leds
        ShaderEffect {
            width: sand.width * root.cell
            height: sand.height * root.cell
            property variant source: texture
            property size cells: Qt.size(sand.width, sand.height)
            property real cell: root.cell
            property color gapColour: Qt.rgba(0, 0, 0, 0)
            property real pulse: root.moving ? clock.pulse : 0
            property real glow: 0.9
            fragmentShader: "qrc:/shaders/ledgrid.frag.qsb"
        }
    }
}
