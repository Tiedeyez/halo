import QtQuick
import QtQuick.Window
import Halo

// The Mandelbrot visual (V cycles to it): the set in square pixels, in the
// Omarchy theme's colours with a pearl's sheen, on an endless dive that only
// goes in (MandelPilot steers; shaders/mandelbrot.frag draws). It steps with
// the spectrum, 30 times a second, only while music plays; calm (C) or
// reduced motion holds it on a still frame.
Item {
    id: root
    property int cell: 6
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && !player.calm && player.playing && visible && root.Window.window
                                   && root.Window.window.visibility !== Window.Minimized

    MandelPilot { id: pilot }

    // The set, one fragment per cell, into a small texture...
    ShaderEffect {
        id: set
        width: Math.max(1, Math.ceil(root.width / root.cell))
        height: Math.max(1, Math.ceil(root.height / root.cell))
        property real time: 0
        property real pulse: root.moving ? spectrumSource.pulse : 0
        property real level: root.moving ? spectrumSource.level : 0.4
        property real low: root.moving ? spectrumSource.low : 0
        property real high: root.moving ? spectrumSource.high : 0.3
        property size size: Qt.size(width, height)
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        property color c4: Style.paletteColour(4)
        property color c5: Style.paletteColour(5)
        property color window: Style.window
        property vector4d view: Qt.vector4d(pilot.x, pilot.y, pilot.scale, pilot.angle)
        property vector4d next: Qt.vector4d(pilot.nextX, pilot.nextY, pilot.nextScale, pilot.nextAngle)
        property real reveal: pilot.reveal
        fragmentShader: "qrc:/shaders/mandelbrot.frag.qsb"

        property double last: 0
        Connections {
            target: spectrumSource
            enabled: root.moving && root.supported
            function onUpdated() {
                const now = Date.now()
                if (set.last > 0) {
                    const dt = Math.min(0.1, (now - set.last) / 1000)
                    set.time += dt
                    pilot.step(dt, spectrumSource.pulse, spectrumSource.low)
                }
                set.last = now
            }
        }
        onVisibleChanged: last = 0
        Component.onCompleted: last = 0
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
