pragma Singleton
import QtQuick

// Colours, sizes and small helpers shared by every view. Text colours are
// chosen for WCAG AAA contrast (7:1) on the panels they sit on, and every
// control is at least 44 px tall.
QtObject {
    readonly property color window: "#0a0a0d"
    readonly property color panel: "#111116"
    readonly property color raised: "#18181f"
    readonly property color hover: "#20202a"
    readonly property color line: "#2a2a34"
    readonly property color text: "#f4f4f7"
    readonly property color muted: "#c3c3cd"
    readonly property color faint: "#a6a6b2"
    readonly property color danger: "#ff7a85"

    readonly property color accent: theme.accent
    readonly property color onAccent: theme.accentForeground
    // The accent as text: lightened until it reads at 7:1 on the panel.
    readonly property color accentText: readable(theme.accent, panel)

    // Omarchy's terminal font, for the pixel-and-terminal touches: labels,
    // numbers, keys and the decrypting text. Falls back to any monospace.
    readonly property string mono: "JetBrainsMono Nerd Font, JetBrains Mono, monospace"
    // What the decrypting text cycles through before it settles, the block
    // shades and symbols Omarchy's screensaver effects scramble with.
    readonly property string noise: "█▓▒░#%&@$*+=<>/\\|"
    // Motion is reducible: Halo's calm setting (C) turns every glitch off.
    readonly property bool motion: !player.calm

    readonly property int radius: theme.radius
    readonly property int small: Math.min(4, theme.radius)
    readonly property int target: 44

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontLarge: 17
    readonly property int fontTitle: 24
    readonly property int fontHero: 34

    function luminance(c) {
        function lin(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b)
    }

    function contrast(a, b) {
        const x = luminance(a), y = luminance(b)
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
    }

    function readable(c, background) {
        let colour = Qt.color(c)
        for (let i = 0; i < 12 && contrast(colour, background) < 7; ++i)
            colour = Qt.tint(colour, "#40ffffff")
        return colour
    }

    function clock(seconds) {
        const s = Math.max(0, Math.floor(seconds || 0))
        const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, r = s % 60
        const two = n => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + two(m) + ":" + two(r) : m + ":" + two(r)
    }

    function hours(seconds) {
        const h = Math.floor(seconds / 3600), m = Math.round(seconds / 60) % 60
        return h > 0 ? qsTr("%1 h %2 min").arg(h).arg(m) : qsTr("%n min", "", Math.max(1, Math.round(seconds / 60)))
    }

    function artistName(artist) {
        if (artist === "*")
            return qsTr("Various artists")
        return artist && artist.length ? artist : qsTr("Unknown artist")
    }
}
