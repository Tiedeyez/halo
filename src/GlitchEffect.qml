import QtQuick
import CymtX

// The glitch shader as a layer effect, driven by a GlitchDriver:
//   GlitchDriver { id: g }
//   layer.enabled: g.active
//   layer.effect: GlitchEffect { amount: g.amount; seed: g.seed }
// The item renders through the shader only while a glitch runs, so a still
// window costs nothing.
ShaderEffect {
    property variant source
    property real amount: 0
    property real seed: 0
    property size size: Qt.size(width, height)
    property color tint: Style.accent
    fragmentShader: "qrc:/shaders/glitch.frag.qsb"
}
