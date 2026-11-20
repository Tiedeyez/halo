import QtQuick
import QtQuick.Window
import Halo
import "qrc:/hologram/manifest.js" as Layers

// The Hologram visual: the Turntable's deck made of light and energy. The
// same Blender model, drawn as lines of light and a fresnel skin
// (art/turntable/turntable.py --energy, packed by tools/hologram-layers),
// tinted with the theme and brought alive by shaders/hologram.frag: the
// grooves carry the spectrum, a shockwave runs out on every beat, the groove
// under the stylus burns, and the album cover turns on the label as light.
// The tonearm follows the song like the Turntable's; everything is added as
// light over what's behind. Motion stops when paused or calm (C).
Item {
    id: root
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    readonly property var manifest: Layers.data
    readonly property alias clock: clock
    readonly property alias cover: label
    readonly property real px: manifest ? width / manifest.image : 1

    // ---- the tonearm: the same geometry as the Turntable's ----
    function heading(progress) {
        const m = manifest
        const r = m.grooveOut + (m.grooveIn - m.grooveOut) * Math.max(0, Math.min(1, progress))
        const dx = m.centre[0] - m.pivot[0], dy = m.centre[1] - m.pivot[1]
        const d = Math.hypot(dx, dy)
        const cosA = (m.armLength * m.armLength + d * d - r * r) / (2 * m.armLength * d)
        return Math.atan2(dy, dx) + Math.acos(Math.max(-1, Math.min(1, cosA)))
    }
    readonly property real progress: player.duration > 0 ? player.position / player.duration : 0
    readonly property real targetFrame: {
        if (!manifest || !player.hasTrack)
            return 0
        const f = (heading(progress) - manifest.rest) / (manifest.runOut - manifest.rest) * (manifest.frames - 1)
        return Math.max(0, Math.min(manifest.frames - 1, f))
    }
    property real frame: 0
    Component.onCompleted: frame = targetFrame
    FrameAnimation {
        running: root.visible && Math.abs(root.frame - root.targetFrame) > 0.001
        onTriggered: {
            if (!Style.motion) {
                root.frame = root.targetFrame
                return
            }
            const dt = Math.min(frameTime, 1 / 20)
            const d = root.targetFrame - root.frame
            const step = d * (1 - Math.exp(-dt * 5))
            const least = Math.min(Math.abs(d), dt * 3)
            root.frame += Math.sign(d) * Math.max(Math.abs(step), least)
        }
    }
    function layerUrl(name, kind) { return "qrc:/hologram/" + name + "-" + kind + ".webp" }
    function armName(i) { return "arm-" + (i < 10 ? "0" : "") + i }

    MotionClock {
        id: clock
        running: root.moving
        property real spin: 0
        property vector4d lo: Qt.vector4d(0, 0, 0, 0)
        property vector4d hi: Qt.vector4d(0, 0, 0, 0)
        onTicked: dt => {
            // 33 1/3 a minute; a beat nudges it like a DJ's hand.
            clock.spin += dt * (Math.PI * 2 * 33.333 / 60) * (1 + 0.04 * clock.pulse)
            if (clock.spin > Math.PI * 2) clock.spin -= Math.PI * 2
            const s = spectrumSource
            clock.lo = Qt.vector4d(s.bandAverage(0, 4), s.bandAverage(4, 10), s.bandAverage(10, 18), s.bandAverage(18, 26))
            clock.hi = Qt.vector4d(s.bandAverage(26, 34), s.bandAverage(34, 44), s.bandAverage(44, 54), s.bandAverage(54, 64))
        }
    }

    Image {
        id: label
        source: player.albumId.length ? "image://cover/" + player.albumId : ""
        sourceSize: Qt.size(512, 512)
        asynchronous: true
        visible: false
        smooth: true
        mipmap: true
    }

    // One layer of light: lines and body textures through the shader.
    component Light: ShaderEffect {
        id: light
        // Inline components can't see this file's ids: the visual is passed in.
        required property Item host
        property string name
        property real deck: 0
        property real gain: 1
        property alias linesSource: linesImage.source
        property alias bodySource: bodyImage.source
        readonly property bool ready: linesImage.status === Image.Ready && bodyImage.status === Image.Ready
        visible: host.supported && ready && gain > 0.002
        Image { id: linesImage; source: host.layerUrl(light.name, "lines"); visible: false; smooth: true; mipmap: true; asynchronous: true }
        Image { id: bodyImage; source: host.layerUrl(light.name, "body"); visible: false; smooth: true; mipmap: true; asynchronous: true }
        property variant lines: linesImage
        property variant body: bodyImage
        property variant cover: host.cover
        property real spin: host.clock.spin
        property real time: host.clock.time
        property real pulse: host.moving ? host.clock.pulse : 0
        property real level: host.moving ? host.clock.level : 0.3
        property real high: host.moving ? host.clock.high : 0
        property real progress: host.progress
        property real hasCover: host.cover.status === Image.Ready ? 1 : 0
        property real recordR: host.manifest ? host.manifest.recordRadius : 3
        property real grooveOut: host.manifest ? host.manifest.grooveOutLabel : 2.9
        property real grooveIn: host.manifest ? host.manifest.grooveInLabel : 1.2
        // Scanlines about every 3 px of the window, whatever the layer's size.
        property real lineRows: height / 3
        property vector4d bandsLo: host.moving ? host.clock.lo : Qt.vector4d(0.25, 0.22, 0.18, 0.15)
        property vector4d bandsHi: host.moving ? host.clock.hi : Qt.vector4d(0.12, 0.1, 0.08, 0.06)
        readonly property var h: host.manifest ? host.manifest.label.fromImage : [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        property vector4d h0: Qt.vector4d(h[0][0], h[0][1], h[0][2], 0)
        property vector4d h1: Qt.vector4d(h[1][0], h[1][1], h[1][2], 0)
        property vector4d h2: Qt.vector4d(h[2][0], h[2][1], h[2][2], 0)
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        fragmentShader: "qrc:/shaders/hologram.frag.qsb"
    }

    Light {
        host: root
        anchors.fill: parent
        name: "deck"
        deck: 1
    }
    Light {
        host: root
        name: "spindle"
        x: root.manifest ? root.manifest.spindle.x * root.px : 0
        y: root.manifest ? root.manifest.spindle.y * root.px : 0
        width: root.manifest ? root.manifest.spindle.w * root.px : 0
        height: root.manifest ? root.manifest.spindle.h * root.px : 0
    }
    // The tonearm: the two nearest frames of its swing, handed over in the
    // middle of each step so it rarely shows twice.
    Item {
        id: armBox
        x: root.manifest ? root.manifest.arm.x * root.px : 0
        y: root.manifest ? root.manifest.arm.y * root.px : 0
        width: root.manifest ? root.manifest.arm.w * root.px : 0
        height: root.manifest ? root.manifest.arm.h * root.px : 0
        readonly property int lower: Math.floor(root.frame)
        readonly property int upper: Math.min(root.manifest ? root.manifest.frames - 1 : 0, lower + 1)
        readonly property real t: { const f = root.frame - lower; const x = Math.max(0, Math.min(1, (f - 0.35) / 0.3)); return x * x * (3 - 2 * x) }
        Light { host: root; anchors.fill: parent; name: root.armName(armBox.lower); gain: armBox.upper === armBox.lower ? 1 : 1 - armBox.t }
        Light { host: root; anchors.fill: parent; name: root.armName(armBox.upper); gain: armBox.upper === armBox.lower ? 0 : armBox.t }
    }

    // Without a GPU: the lines alone, still recognisably the deck.
    Image {
        anchors.fill: parent
        visible: !root.supported
        source: root.layerUrl("deck", "lines")
        smooth: true
        mipmap: true
    }
}
