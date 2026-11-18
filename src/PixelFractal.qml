import QtQuick
import QtQuick.Window
import Halo

// The Fractal visual (V cycles to it): a Julia set that keeps changing shape,
// in square pixels, in the Omarchy theme's colours with a pearl's sheen.
// FractalPilot flies it; shaders/fractal.frag draws it. It steps once per
// screen refresh while music plays; calm (C) or reduced motion holds a
// still frame.
Item {
    id: root
    property int cell: 6
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && !player.calm && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    FractalPilot { id: pilot }
    MotionClock {
        id: clock
        running: root.moving
        onTicked: dt => pilot.step(dt, clock.pulse, clock.low)
    }

    // The set, one fragment per cell, into a small texture...
    ShaderEffect {
        id: set
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        property real time: clock.time
        property real pulse: root.moving ? clock.pulse : 0
        property real level: root.moving ? clock.level : 0.4
        property real high: root.moving ? clock.high : 0.3
        property size size: Qt.size(width, height)
        property vector4d shape: Qt.vector4d(pilot.cx, pilot.cy, pilot.morph, 0)
        property vector4d view: Qt.vector4d(pilot.zoom, pilot.angle, 0, 0)
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        property color c4: Style.paletteColour(4)
        property color c5: Style.paletteColour(5)
        property color window: Style.window
        fragmentShader: "qrc:/shaders/fractal.frag.qsb"
    }
    ShaderEffectSource {
        id: texture
        sourceItem: set
        hideSource: true
        smooth: false
        visible: false
        textureSize: Qt.size(set.width, set.height)
    }
    // ...enlarged into square screen pixels with gaps between them.
    ShaderEffect {
        width: set.width * root.cell
        height: set.height * root.cell
        anchors.centerIn: parent
        visible: root.supported
        property variant source: texture
        property size cells: Qt.size(set.width, set.height)
        property real cell: root.cell
        property color gapColour: Qt.rgba(Style.window.r * 0.6, Style.window.g * 0.6, Style.window.b * 0.6, 1)
        fragmentShader: "qrc:/shaders/pixelgrid.frag.qsb"
    }
}
