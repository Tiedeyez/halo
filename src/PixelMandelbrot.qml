import QtQuick
import QtQuick.Window
import Halo

// The Mandelbrot visual (V cycles to it): the set in square pixels, in the
// Omarchy theme's colours with a pearl's sheen, on a slow voyage into a
// famous place and back (shaders/mandelbrot.frag). It steps with the
// spectrum, 30 times a second, only while music plays; calm (C) or reduced
// motion holds it on a still frame.
Item {
    id: root
    property int cell: 6
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && !player.calm && player.playing && visible && root.Window.window
                                   && root.Window.window.visibility !== Window.Minimized

    ShaderEffect {
        id: set
        anchors.fill: parent
        visible: root.supported
        property real time: 0
        property real pulse: root.moving ? spectrumSource.pulse : 0
        property real level: root.moving ? spectrumSource.level : 0.4
        property real low: root.moving ? spectrumSource.low : 0
        property real high: root.moving ? spectrumSource.high : 0.3
        property real cell: root.cell
        property size size: Qt.size(width, height)
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        property color c4: Style.paletteColour(4)
        property color c5: Style.paletteColour(5)
        property color window: Style.window
        fragmentShader: "qrc:/shaders/mandelbrot.frag.qsb"

        property double last: 0
        Connections {
            target: spectrumSource
            enabled: root.moving && root.supported
            function onUpdated() {
                const now = Date.now()
                if (set.last > 0)
                    set.time += Math.min(0.1, (now - set.last) / 1000)
                set.last = now
            }
        }
        onVisibleChanged: last = 0
    }
}
