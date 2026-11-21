import QtQuick
import QtQuick.Controls
import Halo

// The record player's controls, working: laid over a deck visual
// (Turntable or Hologram), in the places its manifest gives.
//   - The fader is the volume: drag it, or scroll over it.
//   - The dial is the EQ's gain: drag up to turn it clockwise (more), down
//     to turn it back, or scroll; a double click sets it back to 0 dB.
//     Turning it switches the EQ on, as the gain only acts through it.
//   - Start/stop plays and pauses.
//   - The tonearm: take its headshell and drag it over the record to drop
//     the needle anywhere in the song; the arm follows the hand, and the
//     song seeks there on release.
// `deck` is the visual (its manifest, px and heading()); `geometry` the
// record's measures in label radii (the Hologram's manifest has them).
// While the arm is held, `scrub` is where in the song (0..1), else -1.
Item {
    id: controls
    required property Item deck
    required property var geometry
    property real scrub: -1
    readonly property var m: deck.manifest
    readonly property real px: deck.px
    anchors.fill: parent

    // ---- the record's geometry, for the tonearm ----
    // Label radii per metre (the label's radius is grooveOut / grooveOutLabel).
    readonly property real labelR: geometry ? geometry.grooveOut / geometry.grooveOutLabel : 0.05
    function toImage(u, v) {                // label plane -> item pixels
        const H = m.label.toImage
        const w = H[2][0] * u + H[2][1] * v + H[2][2]
        return Qt.point((H[0][0] * u + H[0][1] * v + H[0][2]) / w * deck.width,
                        (H[1][0] * u + H[1][1] * v + H[1][2]) / w * deck.height)
    }
    function toLabel(x, y) {                // item pixels -> label plane
        const H = m.label.fromImage
        const a = x / deck.width, b = y / deck.height
        const w = H[2][0] * a + H[2][1] * b + H[2][2]
        return Qt.point((H[0][0] * a + H[0][1] * b + H[0][2]) / w, (H[1][0] * a + H[1][1] * b + H[1][2]) / w)
    }
    // Where the stylus is, in item pixels, at `progress` through the song.
    function stylusAt(progress) {
        const h = deck.heading(progress)
        const sx = m.pivot[0] + m.armLength * Math.cos(h), sy = m.pivot[1] + m.armLength * Math.sin(h)
        return toImage((sx - m.centre[0]) / labelR, (sy - m.centre[1]) / labelR)
    }
    // Where in the song the needle lands at a point: by its radius on the
    // record, between the first groove and the last.
    function progressAt(x, y) {
        const q = toLabel(x, y)
        const r = Math.hypot(q.x, q.y)
        return Math.max(0, Math.min(1, (geometry.grooveOutLabel - r) / (geometry.grooveOutLabel - geometry.grooveInLabel)))
    }

    // ---- the tonearm: drop the needle ----
    MouseArea {
        id: armArea
        anchors.fill: parent
        hoverEnabled: true
        Accessible.role: Accessible.Slider
        Accessible.name: qsTr("Tonearm: drag the needle over the record to move through the song")
        preventStealing: true
        // Only the headshell is a handle: within a few percent of the stylus.
        readonly property real reach: controls.deck.width * 0.035
        readonly property point stylus: controls.m && player.hasTrack
                                        ? controls.stylusAt(controls.scrub >= 0 ? controls.scrub : (player.duration > 0 ? player.position / player.duration : 0))
                                        : Qt.point(-1000, -1000)
        readonly property bool overHead: Math.hypot(mouseX - stylus.x, mouseY - stylus.y) < reach
        property bool holding: false
        // Let the clicks that aren't on the headshell through.
        onPressed: mouse => {
            if (!controls.m || !player.hasTrack || !overHead) {
                mouse.accepted = false
                return
            }
            holding = true
            controls.scrub = controls.progressAt(mouse.x, mouse.y)
        }
        onPositionChanged: mouse => { if (holding) controls.scrub = controls.progressAt(mouse.x, mouse.y) }
        onReleased: {
            if (holding && player.duration > 0)
                player.seek(controls.scrub * player.duration)
            holding = false
            controls.scrub = -1
        }
        onCanceled: { holding = false; controls.scrub = -1 }
        cursorShape: holding ? Qt.ClosedHandCursor : overHead ? Qt.OpenHandCursor : Qt.ArrowCursor
        ToolTip.visible: holding || (containsMouse && overHead)
        ToolTip.delay: holding ? 0 : 500
        ToolTip.text: holding ? Style.clock(controls.scrub * player.duration) : qsTr("Drag the arm to move through the song")
    }

    // The small controls sit over it, so they keep their own hover.
    // ---- the fader: volume ----
    MouseArea {
        id: faderArea
        readonly property var f: controls.m ? controls.m.fader : null
        Accessible.role: Accessible.Slider
        Accessible.name: qsTr("Volume %1%").arg(Math.round(player.volume * 100))
        visible: !!f
        x: f ? f.x * controls.px : 0
        width: f ? f.w * controls.px : 0
        // The slot from end to end, with room for the cap at either end.
        y: f ? (f.high - f.h * 0.3) * controls.px : 0
        height: f ? (f.low - f.high + f.h * 0.6) * controls.px : 0
        cursorShape: Qt.SizeVerCursor
        hoverEnabled: true
        preventStealing: true
        function set(mouseY) {
            const at = (y + mouseY) / controls.px
            player.volume = Math.max(0, Math.min(1, (at - f.low) / (f.high - f.low)))
        }
        onPressed: mouse => set(mouse.y)
        onPositionChanged: mouse => { if (pressed) set(mouse.y) }
        onWheel: wheel => player.volume = Math.max(0, Math.min(1, player.volume + (wheel.angleDelta.y > 0 ? 0.05 : -0.05)))
        ToolTip.visible: containsMouse || pressed
        ToolTip.delay: pressed ? 0 : 500
        ToolTip.text: qsTr("Volume %1%").arg(Math.round(player.volume * 100))
    }

    // ---- the dial: the EQ's gain ----
    MouseArea {
        id: dialArea
        readonly property var d: controls.m ? controls.m.dial : null
        Accessible.role: Accessible.Dial
        Accessible.name: ToolTip.text
        readonly property real size: controls.deck.width * 0.045
        visible: !!d
        x: d ? d.cx * controls.px - size / 2 : 0
        y: d ? d.cy * controls.px - size / 2 : 0
        width: size
        height: size
        cursorShape: Qt.SizeVerCursor
        hoverEnabled: true
        preventStealing: true
        property real fromY: 0
        property real fromGain: 0
        function setGain(db) {
            player.eqEnabled = true
            player.eqPreamp = Math.max(-12, Math.min(12, Math.round(db * 2) / 2))
        }
        onPressed: mouse => { fromY = mouse.y; fromGain = player.eqEnabled ? player.eqPreamp : 0 }
        // Up is more: all the way round (24 dB) in 150 px.
        onPositionChanged: mouse => { if (pressed) setGain(fromGain + (fromY - mouse.y) * 24 / 150) }
        onDoubleClicked: setGain(0)
        onWheel: wheel => setGain((player.eqEnabled ? player.eqPreamp : 0) + (wheel.angleDelta.y > 0 ? 0.5 : -0.5))
        ToolTip.visible: containsMouse || pressed
        ToolTip.delay: pressed ? 0 : 500
        ToolTip.text: player.eqEnabled ? qsTr("EQ gain %1 dB").arg((player.eqPreamp > 0 ? "+" : "") + player.eqPreamp.toFixed(1))
                                       : qsTr("EQ gain (EQ off)")
    }

    // ---- start/stop ----
    MouseArea {
        readonly property var s: controls.m ? controls.m.start : null
        visible: !!s
        x: s ? (s.x - s.w * 0.6) * controls.deck.width : 0
        y: s ? (s.y - s.h * 0.7) * controls.deck.height : 0
        width: s ? s.w * 1.2 * controls.deck.width : 0
        height: s ? s.h * 1.4 * controls.deck.height : 0
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: player.togglePlay()
        Accessible.role: Accessible.Button
        Accessible.name: ToolTip.text
        ToolTip.visible: containsMouse
        ToolTip.delay: 500
        ToolTip.text: player.playing ? qsTr("Stop") : qsTr("Start")
    }

}
