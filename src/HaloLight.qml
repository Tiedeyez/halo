import QtQuick
import QtQuick.Window
import Halo

// The Halo visual: a ring of light in 3D, seen through a camera, driven by
// the music through a little physics (shaders/halolight.frag draws it).
//
// The ring is a body on springs: each kick strikes it, so it swells and
// bounces back past its size and settles, and it rocks on its tilt. The
// bass drives its brightness through an envelope with a fast attack and a
// slow release. Every beat sends out radiation: a wavefront across the
// ring's plane and a shell of light round it, both slowing as they spread
// and fading as their energy thins out; and sparks are flung off the rim,
// pushed by the kick and coasting out under drag.
//
// The ring sits at `centre` (0..1 of the item), `ringRadius` pixels across
// its half width. The ring, sparks and motes are drawn per pixel at the
// screen's resolution, sharp at 4K; the soft light round them at a third.
// Motion holds when paused or calm (C).
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

        // ---- the body: springs ----
        // Size: 1 at rest, held a little larger by the bass; a kick gives it
        // a push outward, and it overshoots and rings down (underdamped).
        property real size: 1
        property real sizeV: 0
        // Tilt: the camera's height above the ring's plane (radians), swaying
        // slowly; kicks knock it, and it wobbles back.
        property real tilt: 0.42
        property real tiltV: 0
        property real roll: 0
        property real rollV: 0
        // The plasma's flow round the ring, faster when loud.
        property real spin: 0
        // Brightness: fast attack, slow release, from the bass and loudness.
        property real energy: 0.3
        // How far the sparks have been carried: they all move together, a
        // kick pushes them and drag slows them.
        property real travel: 0
        property real travelV: 0

        // ---- radiation: the last four beats ----
        property vector4d beatAt: Qt.vector4d(-10, -10, -10, -10)
        property vector4d beatHit: Qt.vector4d(0, 0, 0, 0)
        property bool armed: true

        onTicked: dt => {
            const s = spectrumSource
            clock.lo = Qt.vector4d(s.bandAverage(0, 4), s.bandAverage(4, 10), s.bandAverage(10, 18), s.bandAverage(18, 26))
            clock.hi = Qt.vector4d(s.bandAverage(26, 34), s.bandAverage(34, 44), s.bandAverage(44, 54), s.bandAverage(54, 64))
            const bass = (clock.lo.x + clock.lo.y) * 0.5

            // A beat: the pulse crossing up through 0.55, re-armed once it
            // falls under 0.3, never twice within 0.18 s. Its strength is
            // how hard the pulse hit.
            const raw = s.pulse
            if (clock.armed && raw > 0.55 && clock.time - clock.beatAt.x > 0.18) {
                const hit = Math.min(1, 0.35 + raw * 0.75)
                clock.beatAt = Qt.vector4d(clock.time, clock.beatAt.x, clock.beatAt.y, clock.beatAt.z)
                clock.beatHit = Qt.vector4d(hit, clock.beatHit.x, clock.beatHit.y, clock.beatHit.z)
                clock.armed = false
                clock.sizeV += 1.6 * hit
                clock.tiltV += (Math.random() < 0.5 ? -1 : 1) * 0.55 * hit
                clock.rollV += (Math.random() - 0.5) * 0.35 * hit
                clock.travelV += 2.2 * hit
            } else if (!clock.armed && raw < 0.3) {
                clock.armed = true
            }

            // Springs, integrated semi-implicitly (stable at any frame rate).
            const sizeRest = 1 + 0.07 * bass
            clock.sizeV += (-70 * (clock.size - sizeRest) - 7 * clock.sizeV) * dt
            clock.size += clock.sizeV * dt
            const tiltRest = 0.42 + 0.06 * Math.sin(clock.time * 0.23) + 0.03 * Math.sin(clock.time * 0.61 + 1.3)
            clock.tiltV += (-14 * (clock.tilt - tiltRest) - 2.2 * clock.tiltV) * dt
            clock.tilt = Math.max(0.2, Math.min(0.75, clock.tilt + clock.tiltV * dt))
            const rollRest = 0.04 * Math.sin(clock.time * 0.17)
            clock.rollV += (-10 * (clock.roll - rollRest) - 2.0 * clock.rollV) * dt
            clock.roll += clock.rollV * dt

            clock.spin += dt * (0.5 + 2.2 * clock.level)
            const target = 0.25 + 0.9 * bass + 0.5 * clock.level
            clock.energy += (target - clock.energy) * (1 - Math.exp(-dt * (target > clock.energy ? 28 : 3.2)))
            clock.travelV -= clock.travelV * 2.6 * dt
            clock.travel += (0.12 + 0.25 * clock.level + clock.travelV) * dt
        }
    }

    // One pass of the light: `sharp` picks the ring, sparks and motes, or
    // the soft light round them.
    component LightPass: ShaderEffect {
        property real sharp: 1
        visible: root.supported
        property real time: clock.time
        property real pulse: root.moving ? clock.pulse : 0
        property real level: root.moving ? clock.level : 0.25
        property real energy: root.moving ? clock.energy : 0.45
        property real aspect: width / Math.max(1, height)
        property real radius: root.ringRadius / Math.max(1, height)
        property real size: clock.size
        property real tilt: clock.tilt
        property real roll: clock.roll
        property real spin: clock.spin
        property real travel: clock.travel
        property real pixel: 1 / Math.max(1, root.ringRadius)
        property point centre: root.centre
        property vector4d beats: Qt.vector4d(clock.time - clock.beatAt.x, clock.time - clock.beatAt.y,
                                             clock.time - clock.beatAt.z, clock.time - clock.beatAt.w)
        property vector4d hits: clock.beatHit
        property vector4d bandsLo: root.moving ? clock.lo : Qt.vector4d(0.3, 0.26, 0.22, 0.18)
        property vector4d bandsHi: root.moving ? clock.hi : Qt.vector4d(0.15, 0.12, 0.1, 0.08)
        property color c0: Style.paletteColour(0)
        property color c1: Style.paletteColour(1)
        property color c2: Style.paletteColour(2)
        property color c3: Style.paletteColour(3)
        fragmentShader: "qrc:/shaders/halolight.frag.qsb"
    }

    // The soft light has no fine detail: drawn at a third of the size and
    // scaled up smoothly, a ninth of the cost; it matters on integrated GPUs.
    LightPass {
        anchors.fill: parent
        sharp: 0
        layer.enabled: visible
        layer.smooth: true
        layer.textureSize: Qt.size(Math.max(1, Math.ceil(width / 3)), Math.max(1, Math.ceil(height / 3)))
    }
    // The ring, the sparks and the motes, at the screen's full resolution.
    LightPass {
        anchors.fill: parent
        sharp: 1
    }

    // Without a GPU: a plain ring, so the visual is still there.
    Rectangle {
        visible: !root.supported
        x: root.centre.x * root.width - root.ringRadius
        y: root.centre.y * root.height - root.ringRadius * 0.4
        width: root.ringRadius * 2
        height: root.ringRadius * 0.8
        radius: height / 2
        color: "transparent"
        border.color: Style.accent
        border.width: 3
    }
}
