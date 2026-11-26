import QtQuick
import CymtX

// Runs one glitch: kick() jumps to full strength and settles back to clean
// in about a third of a second, re-rolling the torn slices as it goes.
// Calm (C) makes kick() do nothing.
Item {
    id: driver
    visible: false
    property real amount: 0
    property real seed: 0
    property int duration: 360
    // Qt's software renderer can't run shaders and would draw nothing while
    // the layer is on, so there the glitch never starts.
    readonly property bool supported: GraphicsInfo.api !== GraphicsInfo.Software && GraphicsInfo.api !== GraphicsInfo.Unknown
    readonly property bool active: supported && amount > 0.001

    function kick(strength) {
        if (!Style.motion || !supported)
            return
        seed = Math.random() * 100
        settle.stop()
        settle.from = strength === undefined ? 1 : strength
        settle.start()
    }

    NumberAnimation {
        id: settle
        target: driver
        property: "amount"
        to: 0
        duration: driver.duration
        easing.type: Easing.OutCubic
    }
    Timer {
        interval: 50
        repeat: true
        running: settle.running
        onTriggered: driver.seed = Math.random() * 100
    }
}
