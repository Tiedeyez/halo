import QtQuick
import QtQuick.Window
import Halo
import "qrc:/turntable/manifest.js" as Layers

// The Turntable visual: a deck rendered in Blender (art/turntable), composed
// live from layers (src/turntable, packed by tools/turntable-layers). The
// album cover turns on the label at 33 1/3 while music plays and stops where
// it is when paused or calm (C); the ring and lamps take the theme's accent
// and lift with the beat; the tonearm sweeps in across the song and parks on
// its rest when nothing is loaded.
Item {
    id: root
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    // Where things are in the render: the deck's geometry, the label's
    // homography and the crops of the moving parts.
    readonly property var manifest: Layers.data
    readonly property real px: manifest ? width / manifest.image : 1

    // ---- the tonearm ----
    function heading(progress) {
        const m = manifest
        const r = m.grooveOut + (m.grooveIn - m.grooveOut) * Math.max(0, Math.min(1, progress))
        const dx = m.centre[0] - m.pivot[0], dy = m.centre[1] - m.pivot[1]
        const d = Math.hypot(dx, dy)
        const cosA = (m.armLength * m.armLength + d * d - r * r) / (2 * m.armLength * d)
        return Math.atan2(dy, dx) + Math.acos(Math.max(-1, Math.min(1, cosA)))
    }
    readonly property real targetFrame: {
        if (!manifest)
            return 0
        if (!player.hasTrack)
            return 0
        const p = player.duration > 0 ? player.position / player.duration : 0
        const f = (heading(p) - manifest.rest) / (manifest.runOut - manifest.rest) * (manifest.frames - 1)
        return Math.max(0, Math.min(manifest.frames - 1, f))
    }
    property real frame: 0
    // The arm eases toward where the song is: big moves (a new song, a seek,
    // leaving the rest) swing over in about a second, and the slow creep
    // through a song just follows. Stepped per frame like the other visuals,
    // so it holds while the window is hidden and catches up when shown.
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
            // Eased, but never slower than a few frames a second, so it lands.
            const step = d * (1 - Math.exp(-dt * 5))
            const least = Math.min(Math.abs(d), dt * 3)
            root.frame += Math.sign(d) * Math.max(Math.abs(step), least)
        }
    }

    function armSource(i) {
        return "qrc:/turntable/arm-" + (i < 10 ? "0" : "") + i + ".webp"
    }

    MotionClock {
        id: clock
        running: root.moving
        property real spin: 0
        onTicked: dt => {
            // 33 1/3 revolutions a minute.
            clock.spin += dt * (Math.PI * 2 * 33.333 / 60)
            if (clock.spin > Math.PI * 2) clock.spin -= Math.PI * 2
        }
    }

    Image {
        id: deckImage
        source: "qrc:/turntable/deck.webp"
        anchors.fill: parent
        smooth: true
        mipmap: true
        asynchronous: true
        visible: !root.supported
    }
    Image {
        id: glowImage
        source: "qrc:/turntable/glow.webp"
        smooth: true
        asynchronous: true
        visible: false
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

    ShaderEffect {
        anchors.fill: parent
        visible: root.supported && deckImage.status === Image.Ready && glowImage.status === Image.Ready
        property variant deck: deckImage
        property variant glow: glowImage
        property variant cover: label
        property real spin: clock.spin
        property real glowLevel: root.moving ? 0.75 + clock.pulse * 1.1 + clock.level * 0.25 : 0.8
        property real hasCover: label.status === Image.Ready ? 1 : 0
        readonly property var h: root.manifest ? root.manifest.label.fromImage : [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        property vector4d h0: Qt.vector4d(h[0][0], h[0][1], h[0][2], 0)
        property vector4d h1: Qt.vector4d(h[1][0], h[1][1], h[1][2], 0)
        property vector4d h2: Qt.vector4d(h[2][0], h[2][1], h[2][2], 0)
        property color tint: Style.accent
        fragmentShader: "qrc:/shaders/turntable.frag.qsb"
    }

    Image {
        source: "qrc:/turntable/spindle.webp"
        visible: !!root.manifest
        x: root.manifest ? root.manifest.spindle.x * root.px : 0
        y: root.manifest ? root.manifest.spindle.y * root.px : 0
        width: root.manifest ? root.manifest.spindle.w * root.px : 0
        height: root.manifest ? root.manifest.spindle.h * root.px : 0
        smooth: true
        mipmap: true
    }

    // The tonearm: the two nearest renders of its swing, the upper one
    // faded in by how far between them the arm is.
    Item {
        visible: !!root.manifest
        x: root.manifest ? root.manifest.arm.x * root.px : 0
        y: root.manifest ? root.manifest.arm.y * root.px : 0
        width: root.manifest ? root.manifest.arm.w * root.px : 0
        height: root.manifest ? root.manifest.arm.h * root.px : 0
        readonly property int lower: Math.floor(root.frame)
        readonly property int upper: Math.min(root.manifest ? root.manifest.frames - 1 : 0, lower + 1)
        Image {
            anchors.fill: parent
            source: root.armSource(parent.lower)
            smooth: true
            mipmap: true
        }
        Image {
            anchors.fill: parent
            source: root.armSource(parent.upper)
            // A quick hand-over in the middle, so the arm rarely shows twice.
            opacity: { const f = root.frame - parent.lower; const t = Math.max(0, Math.min(1, (f - 0.35) / 0.3)); return t * t * (3 - 2 * t) }
            visible: root.supported && parent.upper !== parent.lower
            smooth: true
            mipmap: true
        }
    }
}
