import QtQuick
import CymtX

// A small light on a deck visual (the start/stop button, the speed light):
// placed by image fractions over the visual it sits in, lit by `strength`
// 0..1 in `tint` (shaders/spot.frag).
ShaderEffect {
    required property Item host
    property point at: Qt.point(-1, -1)    // centre, fractions of the host
    property real across: 0.02            // width, fraction of the host's width
    property real tall: across            // height, fraction of the host's height
    property real bar: 0                  // > 0: a rounded bar, not a dot
    property real strength: 0
    property color tint: Style.accent
    visible: GraphicsInfo.api !== GraphicsInfo.Software && strength > 0.01
    x: at.x * host.width - width / 2
    y: at.y * host.height - height / 2
    width: across * host.width
    height: tall * host.height
    fragmentShader: "qrc:/shaders/spot.frag.qsb"
}
