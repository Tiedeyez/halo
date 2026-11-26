import QtQuick
import QtQuick.Window
import CymtX

// One of the pixel visualisers (Skyline, Phosphor, Pulsar, Waterfall, Warp,
// Aurora, and the Ring): PixelVisual (C++) draws it one pixel per cell, and
// the LED grid (shaders/ledgrid.frag) enlarges it into square screen pixels
// with gaps, each lit like an LED with a bloom that lifts on the beat. It steps once
// per screen refresh while music plays; calm (C) holds the last frame.
Item {
    id: root
    property int mode: 0
    property int cell: 6
    // Where across the item (0..1) a visual with a middle puts it.
    property real middle: 0.5
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    MotionClock {
        id: clock
        running: root.moving
        onTicked: dt => art.step(dt, clock.pulse, clock.level, clock.low, clock.mid, clock.high)
    }

    PixelVisual {
        id: art
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        mode: root.mode
        middle: root.middle
        spectrum: spectrumSource
        window: Style.window
        palette: [Style.paletteColour(0), Style.paletteColour(1), Style.paletteColour(2),
                  Style.paletteColour(3), Style.paletteColour(4), Style.paletteColour(5)]
        visible: !root.supported
        scale: root.supported ? 1 : root.cell
        transformOrigin: Item.TopLeft
        // A first frame, so a held or paused visual isn't empty.
        Component.onCompleted: step(0, 0, 0.3, 0.3, 0.3, 0.3)
        onWidthChanged: if (!root.moving) step(0, 0, 0.3, 0.3, 0.3, 0.3)
    }
    ShaderEffectSource {
        id: texture
        sourceItem: art
        hideSource: root.supported
        // Smooth and mipmapped for the LEDs' bloom; the cells themselves are
        // sampled at texel centres, so they stay exact.
        smooth: true
        mipmap: true
        visible: false
        live: true
        textureSize: Qt.size(art.width, art.height)
    }
    ShaderEffect {
        width: art.width * root.cell
        height: art.height * root.cell
        anchors.centerIn: parent
        visible: root.supported
        property variant source: texture
        property size cells: Qt.size(art.width, art.height)
        property real cell: root.cell
        property color gapColour: Qt.rgba(0, 0, 0, 0)
        property real pulse: root.moving ? clock.pulse : 0
        property real glow: 1
        fragmentShader: "qrc:/shaders/ledgrid.frag.qsb"
    }
}
