import QtQuick
import QtQuick.Window
import Halo

// The Bars visual: HaloView's LED bars, finished by shaders/barsfx.frag:
// coloured up their height, bloomed like lit LEDs, and reflected in a glossy
// floor below. `barsHeight` is the bars' own height; the floor adds about
// half again below it. Motion holds when paused or calm (C).
Item {
    id: root
    property real barsHeight: height / 1.5
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    MotionClock {
        id: clock
        running: root.moving
    }

    HaloView {
        id: barsView
        width: root.width
        height: root.barsHeight
        spectrum: spectrumSource
        color: Style.accent
        calm: player.calm
        style: HaloView.Bars
    }
    ShaderEffectSource {
        id: barsTexture
        sourceItem: barsView
        hideSource: root.supported
        smooth: true
        mipmap: true
        visible: false
    }
    ShaderEffect {
        anchors.fill: parent
        visible: root.supported
        property variant source: barsTexture
        property real split: root.barsHeight / Math.max(1, root.height)
        property real pulse: root.moving ? clock.pulse : 0
        property real time: clock.time
        property color c0: Style.accent
        property color c1: Style.paletteColour(1)
        fragmentShader: "qrc:/shaders/barsfx.frag.qsb"
    }
}
