import QtQuick
import Halo

// The window's background, alive: square pixels on a grid, stars that
// breathe, columns of pixels raining down and a ring sweeping out from the
// cover on every bass hit (see shaders/pixelfield.frag). It moves only while
// music plays, so a paused Halo costs no frames, and calm (C) stills it.
// The brightest a pixel gets is worked out from the theme's accent so the
// faintest text over it still reads at 7:1 (WCAG AAA).
Item {
    id: root
    property point origin: Qt.point(width * 0.1, height)
    property int cell: 6
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && root.Window.window
                                   && root.Window.window.visibility !== Window.Minimized

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

    ShaderEffect {
        id: field
        anchors.fill: parent
        visible: root.supported
        property real time: 0
        property real pulse: Style.motion ? spectrumSource.pulse : 0
        property real level: Style.motion ? spectrumSource.level : 0
        property real cell: root.cell
        property real strength: root.strength
        property size size: Qt.size(width, height)
        property point origin: root.origin
        property color accent: Style.accent
        fragmentShader: "qrc:/shaders/pixelfield.frag.qsb"

        // It steps with the spectrum (30 times a second while music plays),
        // so its frames are the same ones the halo already draws instead of
        // doubling them; real elapsed time keeps the speed even.
        property double last: 0
        Connections {
            target: spectrumSource
            enabled: root.moving && root.supported
            function onUpdated() {
                const now = Date.now()
                if (field.last > 0)
                    field.time += Math.min(0.1, (now - field.last) / 1000)
                field.last = now
            }
        }
        onVisibleChanged: last = 0
    }
}
