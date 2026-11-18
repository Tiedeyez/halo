import QtQuick
import QtQuick.Window
import Halo

// The window's background: pixelated water (see shaders/pixelfield.frag).
// Drops ripple it: each beat (from the cover, `origin`, or somewhere on the
// surface), every click anywhere in the window, and a soft wake behind the
// pointer. It moves while music plays or ripples are still spreading, so a
// quiet Halo costs no frames, and calm (C) stills it. The brightest a pixel
// gets is worked out from the theme's accent so the faintest text over it
// still reads at 7:1 (WCAG AAA).
Item {
    id: root
    property point origin: Qt.point(width * 0.1, height)
    property int cell: 6
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool shown: visible && root.Window.window && root.Window.window.visibility !== Window.Minimized
    // Seconds of `clock.time` until the last drop has died away.
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

    // Eight drops, reused oldest first: x, y in cells, when, how hard.
    property int nextDrop: 0
    property var drops: [Qt.vector4d(0, 0, 0, 0), Qt.vector4d(0, 0, 0, 0), Qt.vector4d(0, 0, 0, 0), Qt.vector4d(0, 0, 0, 0),
                         Qt.vector4d(0, 0, 0, 0), Qt.vector4d(0, 0, 0, 0), Qt.vector4d(0, 0, 0, 0), Qt.vector4d(0, 0, 0, 0)]
    function drop(x, y, force) {
        if (!Style.motion || !supported || !shown)
            return
        const d = drops.slice()
        d[nextDrop] = Qt.vector4d(x / cell, y / cell, clock.time, force)
        drops = d
        nextDrop = (nextDrop + 1) % 8
        stillAt = clock.time + 9
    }

    MotionClock {
        id: clock
        running: root.moving
        property bool wasBeat: false
        property real rainAt: 0
        onTicked: dt => {
            // A beat drops from the cover, or every other one somewhere on
            // the surface, harder on louder music.
            const beat = clock.pulse > 0.55
            if (beat && !wasBeat && player.playing) {
                if (Math.random() < 0.5)
                    root.drop(root.origin.x, root.origin.y, 0.55 + 0.6 * clock.level)
                else
                    root.drop(Math.random() * root.width, Math.random() * root.height, 0.35 + 0.4 * clock.level)
            }
            wasBeat = clock.pulse > 0.35 && (wasBeat || beat)
            // Loud music rains a little between beats.
            if (player.playing && clock.time > rainAt && Math.random() < dt * 0.6 * clock.level) {
                root.drop(Math.random() * root.width, Math.random() * root.height, 0.2)
                rainAt = clock.time + 0.4
            }
        }
    }

    // The pointer anywhere in the window: a click drops, moving leaves a wake.
    property point lastWake: Qt.point(-1000, -1000)
    property real wakeAt: 0
    Connections {
        target: pointer
        function onPressed(x, y) {
            const p = root.mapFromItem(null, x, y)
            if (root.contains(p))
                root.drop(p.x, p.y, 0.9)
        }
        function onMoved(x, y) {
            const p = root.mapFromItem(null, x, y)
            const far = Math.hypot(p.x - root.lastWake.x, p.y - root.lastWake.y) > root.cell * 6
            if (!root.contains(p) || !far || clock.time < root.wakeAt)
                return
            root.lastWake = p
            root.wakeAt = clock.time + 0.12
            root.drop(p.x, p.y, 0.22)
        }
    }

    // The surface, one fragment per cell, into a small texture...
    ShaderEffect {
        id: field
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        property real time: clock.time
        property real level: Style.motion ? clock.level : 0
        property real strength: root.strength
        property size size: Qt.size(width, height)
        property color accent: Style.accent
        property vector4d d0: root.drops[0]
        property vector4d d1: root.drops[1]
        property vector4d d2: root.drops[2]
        property vector4d d3: root.drops[3]
        property vector4d d4: root.drops[4]
        property vector4d d5: root.drops[5]
        property vector4d d6: root.drops[6]
        property vector4d d7: root.drops[7]
        fragmentShader: "qrc:/shaders/pixelfield.frag.qsb"
    }
    ShaderEffectSource {
        id: texture
        sourceItem: field
        hideSource: true
        smooth: false
        visible: false
        textureSize: Qt.size(field.width, field.height)
    }
    // ...enlarged into square screen pixels with clear gaps between them.
    ShaderEffect {
        width: field.width * root.cell
        height: field.height * root.cell
        visible: root.supported
        property variant source: texture
        property size cells: Qt.size(field.width, field.height)
        property real cell: root.cell
        property color gapColour: "transparent"
        fragmentShader: "qrc:/shaders/pixelgrid.frag.qsb"
    }
}
