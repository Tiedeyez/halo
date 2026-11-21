import QtQuick
import QtQuick.Window
import Halo

// The Halo visual: a ring of light with light pouring off it, above and
// below, all of it moving with the music (shaders/halolight.frag). It fills
// the area it is given; the ring sits at `centre` (0..1 of the item) with
// the radius `ringRadius` in pixels. The ring and motes are drawn per pixel
// at the screen's resolution, so they stay sharp at 4K; the soft light round
// them at a third. Motion holds when paused or calm (C).
Item {
    id: root
    property point centre: Qt.point(0.5, 0.5)
    property real ringRadius: Math.min(width, height) * 0.3

    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool moving: Style.motion && player.playing && visible && supported
                                   && root.Window.window && root.Window.window.visibility !== Window.Minimized

    MotionClock {
        id: clock
        running: root.moving
        property vector4d lo: Qt.vector4d(0, 0, 0, 0)
        property vector4d hi: Qt.vector4d(0, 0, 0, 0)
        // The time of the last four beats, newest first, for the shockwaves.
        property vector4d beatAt: Qt.vector4d(-10, -10, -10, -10)
        property bool armed: true
        property real tiltPhase: 0
        onTicked: dt => {
            const s = spectrumSource
            clock.lo = Qt.vector4d(s.bandAverage(0, 4), s.bandAverage(4, 10), s.bandAverage(10, 18), s.bandAverage(18, 26))
            clock.hi = Qt.vector4d(s.bandAverage(26, 34), s.bandAverage(34, 44), s.bandAverage(44, 54), s.bandAverage(54, 64))
            // A beat: the pulse crossing up through 0.55, re-armed once it
            // falls back under 0.3, and never twice within 0.18 s.
            const raw = s.pulse
            if (clock.armed && raw > 0.55 && clock.time - clock.beatAt.x > 0.18) {
                clock.beatAt = Qt.vector4d(clock.time, clock.beatAt.x, clock.beatAt.y, clock.beatAt.z)
                clock.armed = false
            } else if (!clock.armed && raw < 0.3) {
                clock.armed = true
            }
            // The ring rocks slowly, as if hanging in the air.
            clock.tiltPhase += dt * (0.21 + 0.1 * clock.level)
        }
    }

    // One pass of the light (shaders/halolight.frag): `sharp` picks the
    // ring, motes and flare, or the soft light round them.
    component LightPass: ShaderEffect {
        property real sharp: 1
        visible: root.supported
        property real time: clock.time
        property real pulse: root.moving ? clock.pulse : 0
        property real level: root.moving ? clock.level : 0.25
        property real low: root.moving ? clock.low : 0.2
        property real high: root.moving ? clock.high : 0.1
        property real aspect: width / Math.max(1, height)
        property real radius: root.ringRadius / Math.max(1, height)
        property real tilt: 0.3 + 0.035 * Math.sin(clock.tiltPhase) + 0.015 * Math.sin(clock.tiltPhase * 2.7 + 1.0)
        property real pixel: 1 / Math.max(1, root.ringRadius)
        property point centre: root.centre
        property vector4d beats: Qt.vector4d(clock.time - clock.beatAt.x, clock.time - clock.beatAt.y,
                                             clock.time - clock.beatAt.z, clock.time - clock.beatAt.w)
        property vector4d bandsLo: root.moving ? clock.lo : Qt.vector4d(0.3, 0.26, 0.22, 0.18)
        property vector4d bandsHi: root.moving ? clock.hi : Qt.vector4d(0.15, 0.12, 0.1, 0.08)
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        fragmentShader: "qrc:/shaders/halolight.frag.qsb"
    }

    // The soft light (beams, haze, wide bloom, rays, shockwaves) has no fine
    // detail: drawn at a third of the size and scaled up smoothly, a ninth of the
    // cost; it matters on integrated GPUs.
    LightPass {
        anchors.fill: parent
        sharp: 0
        layer.enabled: visible
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(1, Math.ceil(width / 3)), Math.max(1, Math.ceil(height / 3)))
    }
    // The ring, the motes and the flare, at the screen's full resolution.
    LightPass {
        anchors.fill: parent
        sharp: 1
    }

    // Without a GPU: a plain ring, so the visual is still there.
    Rectangle {
        visible: !root.supported
        x: root.centre.x * root.width - root.ringRadius
        y: root.centre.y * root.height - root.ringRadius * 0.3
        width: root.ringRadius * 2
        height: root.ringRadius * 0.6
        radius: height / 2
        color: "transparent"
        border.color: Style.accent
        border.width: 3
    }
}
