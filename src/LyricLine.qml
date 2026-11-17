import QtQuick
import Halo

// One line of lyrics. The line being sung fills word by word in the accent,
// with a block cursor at the edge like a terminal's; it decrypts into place
// as it becomes the current line, the way Omarchy's screensaver reveals its
// logo. Lines already sung dim, the ones to come wait in grey.
Item {
    id: root
    required property var line     // {start, end, text, words: [{start, text}]}
    required property int index
    property int current: -1
    property bool synced: true
    property real time: 0
    property int size: 30

    readonly property bool isCurrent: synced && index === current
    readonly property bool isPast: synced && current >= 0 && index < current
    readonly property int distance: synced ? Math.abs(index - Math.max(current, 0)) : 0

    // The words with their start and end times. Lines timed only as a whole
    // spread their words over 90% of the line by length, a fair guess at
    // how a rapper moves through it.
    readonly property var words: {
        const out = []
        const given = line.words || []
        if (given.length) {
            for (let i = 0; i < given.length; ++i) {
                const end = i + 1 < given.length ? given[i + 1].start : Math.max(line.end, given[i].start + 0.4)
                out.push({ text: given[i].text, start: given[i].start, end: end })
            }
            return out
        }
        const parts = (line.text || "").match(/\S+\s*/g) || []
        const total = parts.reduce((n, p) => n + p.length, 0) || 1
        const span = Math.max(0.4, (line.end - line.start) * 0.9)
        let done = 0
        for (const p of parts) {
            const s = line.start + span * done / total
            done += p.length
            out.push({ text: p, start: s, end: line.start + span * done / total })
        }
        return out
    }

    // 0..1 while the line decrypts on becoming current.
    property real reveal: 1
    onIsCurrentChanged: {
        if (isCurrent && Style.motion) {
            decrypt.restart()
            glitch.kick(0.32)
        } else if (!isCurrent) {
            decrypt.stop()
            reveal = 1
        }
    }
    NumberAnimation { id: decrypt; target: root; property: "reveal"; from: 0; to: 1; duration: 260 }
    // Re-rolls the noise while it decrypts.
    property int noiseSeed: 0
    Timer { interval: 40; repeat: true; running: decrypt.running; onTriggered: root.noiseSeed++ }

    function noise(text, seed) {
        let out = ""
        for (let i = 0; i < text.length; ++i)
            out += text[i] === " " ? " " : Style.noise[(seed * 7 + i * 13 + text.charCodeAt(i)) % Style.noise.length]
        return out
    }

    implicitHeight: flow.implicitHeight + 4
    opacity: !synced ? 1 : isCurrent ? 1 : isPast ? Math.max(0.22, 0.5 - distance * 0.08) : Math.max(0.28, 0.75 - distance * 0.1)
    Behavior on opacity { enabled: Style.motion; NumberAnimation { duration: 280 } }
    // The current line grows from the left edge it reads from.
    scale: isCurrent || !synced ? 1 : 0.86
    transformOrigin: Item.Left
    Behavior on scale { enabled: Style.motion; NumberAnimation { duration: 320; easing.type: Easing.OutCubic } }

    Accessible.role: Accessible.StaticText
    Accessible.name: line.text

    GlitchDriver { id: glitch; duration: 260 }
    layer.enabled: glitch.active
    layer.effect: GlitchEffect { amount: glitch.amount; seed: glitch.seed }

    Flow {
        id: flow
        width: root.width
        spacing: 0

        Repeater {
            model: root.words
            delegate: Item {
                id: word
                required property var modelData
                required property int index
                readonly property real fill: !root.isCurrent ? (root.isPast ? 1 : 0)
                    : Math.max(0, Math.min(1, (root.time - modelData.start) / Math.max(0.05, modelData.end - modelData.start)))
                readonly property bool scrambled: root.isCurrent && root.reveal < 1 &&
                                                  index / Math.max(1, root.words.length) >= root.reveal
                readonly property string shown: scrambled ? root.noise(modelData.text, root.noiseSeed + index) : modelData.text

                width: base.implicitWidth
                height: base.implicitHeight

                Text {
                    id: base
                    text: word.shown
                    color: root.isCurrent ? Qt.alpha(Style.text, 0.38) : root.isPast ? Style.faint : Style.muted
                    font.family: Style.mono
                    font.pixelSize: root.size
                    font.weight: root.isCurrent ? Font.Bold : Font.DemiBold
                }
                // The sung part, in the accent, clipped to how far the word has got.
                Item {
                    visible: root.isCurrent && word.fill > 0
                    width: base.width * word.fill
                    height: base.height
                    clip: true
                    Text {
                        text: word.shown
                        color: Style.accentText
                        font: base.font
                    }
                }
                // The block cursor rides the edge of the word being sung.
                Rectangle {
                    visible: root.isCurrent && word.fill > 0 && word.fill < 1 && word.modelData.text.trim().length > 0
                    x: Math.min(base.width - 3, base.width * word.fill)
                    y: base.height * 0.18
                    width: Math.max(3, Math.round(base.font.pixelSize * 0.16))
                    height: base.height * 0.68
                    color: Style.accent
                }
            }
        }
    }
}
