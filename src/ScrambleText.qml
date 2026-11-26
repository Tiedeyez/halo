import QtQuick
import CymtX

// Text that decrypts into place, left to right, through block shades and
// symbols, the way Omarchy's screensaver reveals its logo. Screen readers get
// the real text at once; with calm on, it just appears.
Text {
    textFormat: Text.PlainText
    id: root
    property string target
    property int duration: 420
    property real progress: 1

    Accessible.role: Accessible.StaticText
    Accessible.name: target

    function scrambled(p) {
        const n = target.length
        const settled = Math.floor(p * n)
        let out = target.substring(0, settled)
        for (let i = settled; i < n; ++i) {
            const ch = target[i]
            out += ch === " " ? " " : Style.noise[Math.floor(Math.random() * Style.noise.length)]
        }
        return out
    }

    function render() {
        text = progress >= 1 ? target : scrambled(progress)
    }

    function replay() {
        reveal.stop()
        progress = Style.motion && visible ? 0 : 1
        render()
        if (progress < 1)
            reveal.start()
    }

    onTargetChanged: replay()
    onVisibleChanged: if (visible) replay()
    onProgressChanged: if (progress >= 1) render()
    Component.onCompleted: replay()

    NumberAnimation {
        id: reveal
        target: root
        property: "progress"
        from: 0
        to: 1
        duration: root.duration
    }
    // The noise changes every frame or two while it decrypts.
    Timer {
        interval: 40
        repeat: true
        running: reveal.running
        onTriggered: root.render()
    }
}
