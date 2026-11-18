import QtQuick
import Halo

// Steps a visual once per screen refresh (not on a timer, which drifts
// against the display and stutters), and eases the music's beat, loudness,
// bass and treble toward the spectrum's latest values so they glide between
// its updates. `time` only advances while `running`; held, it holds.
Item {
    id: clock
    visible: false
    property bool running: false
    property real time: 0
    property real dt: 0
    // Eased copies of the spectrum: quick to rise, softer to fall.
    property real pulse: 0
    property real level: 0
    property real low: 0
    property real high: 0
    signal ticked(real dt)

    function ease(now, target, dt, rise, fall) {
        const rate = target > now ? rise : fall
        return now + (target - now) * (1 - Math.exp(-dt * rate))
    }

    FrameAnimation {
        running: clock.running
        onTriggered: {
            // A hitch (a window hidden, a slow frame) never becomes a jump.
            const dt = Math.min(frameTime, 1 / 20)
            clock.dt = dt
            clock.time += dt
            clock.pulse = clock.ease(clock.pulse, spectrumSource.pulse, dt, 40, 9)
            clock.level = clock.ease(clock.level, spectrumSource.level, dt, 14, 5)
            clock.low = clock.ease(clock.low, spectrumSource.low, dt, 20, 6)
            clock.high = clock.ease(clock.high, spectrumSource.high, dt, 20, 6)
            clock.ticked(dt)
        }
    }
}
