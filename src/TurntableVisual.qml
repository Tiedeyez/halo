import QtQuick
import QtQuick.Window
import Halo
import "qrc:/turntable/manifest.js" as Layers
import "qrc:/hologram/manifest.js" as Energy

// The Turntable visual: a deck rendered in Blender (art/turntable), seen
// from straight above, composed live from layers (src/turntable, packed by
// tools/turntable-layers). The album cover (or Halo's tile for it) turns on
// the label at 33 1/3 while music plays and stops where it is when paused or
// calm (C), with specks of dust riding the vinyl through the light; the
// ring and lamps take the theme's accent and lift with the beat, the stage
// light flashes in the record's sheen on the kick, and an underglow
// breathes with the bass. The tonearm sweeps in across the song and parks
// on its rest when nothing is loaded; the fader follows the volume and the
// dial the EQ's gain.
Item {
    id: root
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized
    // The platter has weight: it spins up when play is pressed and coasts
    // down when stopped, so the clock runs on until it has stopped.
    readonly property bool turning: Style.motion && visible && supported && (player.playing || clock.speed > 0.002)
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
        running: root.turning
        property real spin: 0
        // 0 stopped .. 1 at speed: up in about a second, down in about two.
        property real speed: player.playing ? 1 : 0
        onTicked: dt => {
            const target = player.playing ? 1 : 0
            clock.speed += (target - clock.speed) * (1 - Math.exp(-dt * (target > clock.speed ? 3.2 : 1.6)))
            if (clock.speed < 0.002 && !player.playing)
                clock.speed = 0
            // 33 1/3 revolutions a minute at speed.
            clock.spin += dt * (Math.PI * 2 * 33.333 / 60) * clock.speed
            if (clock.spin > Math.PI * 2) clock.spin -= Math.PI * 2
        }
    }

    // Under the deck, a light of the theme's accent that breathes with the
    // bass and flares on the kick, hugging the plinth's outline.
    ShaderEffect {
        anchors.fill: parent
        visible: root.supported && !!root.manifest && !!root.manifest.plinth
        readonly property var box: root.manifest && root.manifest.plinth ? root.manifest.plinth : ({ x: 0.1, y: 0.2, w: 0.8, h: 0.6 })
        property vector4d plinth: Qt.vector4d(box.x, box.y, box.w, box.h)
        property real strength: root.moving ? 0.35 + clock.low * 0.9 + clock.pulse * 0.9 : 0.3
        property real time: clock.time
        property color tint: Style.accent
        property color tint2: Style.paletteColour(1)
        fragmentShader: "qrc:/shaders/underglow.frag.qsb"
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
    // The label: the album's cover, or Halo's own tile for it (its colour
    // and initials) when it has none, so the record is always seen turning.
    CoverArt {
        id: labelArt
        width: 512
        height: 512
        albumId: player.albumId
        title: player.album
        size: 512
        corner: 0
        // Visible, so the source below can draw it; hideSource keeps it
        // off the screen itself.
    }
    ShaderEffectSource {
        id: label
        sourceItem: labelArt
        hideSource: true
        // The shader samples it like an Image (top row at v = 0).
        textureMirroring: ShaderEffectSource.NoMirroring
        smooth: true
        mipmap: true
        visible: false
    }

    ShaderEffect {
        anchors.fill: parent
        visible: root.supported && deckImage.status === Image.Ready && glowImage.status === Image.Ready
        property variant deck: deckImage
        property variant glow: glowImage
        property variant cover: label
        property real spin: clock.spin
        // The lamps dim while the deck is stopped and come up as it spins.
        property real glowLevel: root.moving ? (0.35 + 0.4 * clock.speed) + clock.pulse * 1.1 + clock.level * 0.25 : 0.3 + 0.45 * clock.speed
        property real hasCover: player.albumId.length ? 1 : 0
        property real pulse: root.moving ? clock.pulse : 0
        property real level: root.moving ? clock.level : 0.2
        // The record and its music, in label radii (from the same Blender scene).
        property real recordR: Energy.data.recordRadius
        property real grooveOut: Energy.data.grooveOutLabel
        property real grooveIn: Energy.data.grooveInLabel
        // Gaps between the album's songs, at least 3, from the album, so a
        // record keeps its look.
        property real tracks: player.albumId.length ? 4 + (Qt.md5(player.albumId).charCodeAt(0) % 6) : 6
        readonly property var h: root.manifest ? root.manifest.label.fromImage : [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
        property vector4d h0: Qt.vector4d(h[0][0], h[0][1], h[0][2], 0)
        property vector4d h1: Qt.vector4d(h[1][0], h[1][1], h[1][2], 0)
        property vector4d h2: Qt.vector4d(h[2][0], h[2][1], h[2][2], 0)
        property color tint: Style.accent
        fragmentShader: "qrc:/shaders/turntable.frag.qsb"
    }

    // ---- the controls follow Halo's own ----
    // Start/stop lights while playing, and the 33 speed light beside it.
    DeckSpot {
        host: root
        readonly property var m: root.manifest && root.manifest.start ? root.manifest.start : null
        at: m ? Qt.point(m.x, m.y) : Qt.point(-1, -1)
        across: m ? m.w * 1.5 : 0
        tall: m ? m.h * 2.2 : 0
        bar: 0.55
        strength: m ? clock.speed * (0.55 + 0.25 * clock.pulse) : 0
    }
    DeckSpot {
        host: root
        readonly property var m: root.manifest && root.manifest.pip ? root.manifest.pip : null
        at: m ? Qt.point(m.x, m.y) : Qt.point(-1, -1)
        across: 0.03
        strength: m ? clock.speed : 0
    }
    // The fader is the volume: its cap slides along the slot, from the near
    // end (silent) to the far end (full).
    Image {
        id: fader
        readonly property var m: root.manifest ? root.manifest.fader : null
        property real level: player.volume
        Behavior on level { NumberAnimation { duration: Style.motion ? 160 : 0; easing.type: Easing.OutCubic } }
        source: m ? "qrc:/turntable/fader.webp" : ""
        visible: !!m
        x: m ? m.x * root.px : 0
        y: m ? (m.y + (m.low + (m.high - m.low) * level) - m.rest) * root.px : 0
        width: m ? m.w * root.px : 0
        height: m ? m.h * root.px : 0
        smooth: true
        mipmap: true
    }
    // The round dial is the EQ's gain: its lit index turns from the first
    // tick (-12 dB) through the middle (0) to the last (+12 dB), and rests in
    // the middle while the EQ is off.
    Image {
        id: dialIndex
        readonly property var m: root.manifest ? root.manifest.dial : null
        property real gain: player.eqEnabled ? player.eqPreamp : 0
        Behavior on gain { NumberAnimation { duration: Style.motion ? 200 : 0; easing.type: Easing.OutCubic } }
        source: m ? "qrc:/turntable/dial.webp" : ""
        visible: !!m
        x: m ? m.x * root.px : 0
        y: m ? m.y * root.px : 0
        width: m ? m.w * root.px : 0
        height: m ? m.h * root.px : 0
        smooth: true
        mipmap: true
        transform: Rotation {
            origin.x: dialIndex.m ? (dialIndex.m.cx - dialIndex.m.x) * root.px : 0
            origin.y: dialIndex.m ? (dialIndex.m.cy - dialIndex.m.y) * root.px : 0
            // Clockwise is up: the ticks run from up-left round to down-left.
            angle: dialIndex.gain / 12 * 120
        }
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
