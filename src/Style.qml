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
    // Lifted so it still reads at 7:1 on a hovered row (#20202a).
    readonly property color faint: "#acacb8"
    readonly property color danger: "#ff8a93"

    readonly property color accent: theme.accent
    // One of the theme's six visual colours (accent, cyan, blue, magenta,
    // bright cyan, foreground); the accent when a theme lacks one.
    function paletteColour(i) { return theme.palette.length > i ? theme.palette[i] : theme.accent }

    // The darkest thing text sits on in Now Playing: the window at 0.93
    // over the visual, at worst a white visual behind it.
    readonly property color scrim: Qt.tint("#ffffff", Qt.alpha(window, 0.93))
    // A faint accent wash behind small labels (the moment stamps).
    readonly property color accentWash: Qt.tint(hover, Qt.alpha(theme.accent, 0.08))
    // The lightest surface text is ever drawn on, so a colour that reads
    // there reads everywhere.
    readonly property color lightestSurface: lightest([window, panel, raised, hover, scrim, accentWash])

    // The accent as text: lightened until it reads at 7:1 on every surface.
    readonly property color accentText: readable(theme.accent, lightestSurface)

    // Filled accent controls (Play, highlighted buttons, badges). The fill
    // is the accent, moved just far enough from its text colour that black
    // or white on it reaches 7:1; accentInk is whichever of the two reads
    // better on it, and accentFillHover moves further from that text.
    readonly property color black: "#000000"
    readonly property color white: "#ffffff"
    readonly property color accentFill: fillFor(theme.accent)
    readonly property bool darkOnAccent: contrast(black, accentFill) >= contrast(white, accentFill)
    readonly property color accentInk: darkOnAccent ? black : white
    readonly property color accentFillHover: Qt.tint(accentFill, darkOnAccent ? "#30ffffff" : "#30000000")

    // Omarchy's terminal font, for the pixel-and-terminal touches: labels,
    // numbers, keys and the decrypting text. Falls back to any monospace.
    readonly property string mono: "JetBrainsMono Nerd Font, JetBrains Mono, monospace"
    // What the decrypting text cycles through before it settles, the block
    // shades and symbols Omarchy's screensaver effects scramble with.
    readonly property string noise: "█▓▒░#%&@$*+=<>/\\|"
    // Motion is reducible: Halo's calm setting (C) turns every glitch off.
    readonly property bool motion: !player.calm
    // Durations (ms) for every eased change: hovers and presses, things
    // that appear and move, big surfaces arriving. Eased out, so a change
    // starts at once and settles softly.
    // How fine the pixel art is: 2 = twice the cells across (four times the
    // pixels) of Halo's first visuals. Every visual and the water derive
    // their cell size from it, so the look scales from a laptop to 4K.
    readonly property real pixelDensity: 2
    readonly property int quick: 140
    readonly property int smooth: 280
    readonly property int gentle: 460

    readonly property int radius: theme.radius
    readonly property int small: Math.min(4, theme.radius)
    readonly property int target: 44

    readonly property int fontSmall: 12
    readonly property int fontBody: 14
    readonly property int fontLarge: 17
    readonly property int fontTitle: 24
    readonly property int fontHero: 34

    // Takes colours, not strings: "#000000" has no .r of its own.
    function luminance(c) {
        function lin(v) { return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) }
        return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b)
    }

    function contrast(a, b) {
        const x = luminance(a), y = luminance(b)
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
    }

    function lightest(colours) {
        let best = colours[0]
        for (const c of colours)
            if (luminance(c) > luminance(best))
                best = c
        return best
    }

    // Lightened toward white in small steps (keeping as much of the hue as
    // it can) until it reads at 7:1 on the background.
    function readable(c, background) {
        let colour = Qt.color(c)
        for (let i = 0; i < 40 && contrast(colour, background) < 7; ++i)
            colour = Qt.tint(colour, "#1affffff")
        return colour
    }

    // Moves a fill away from whichever of black or white reads better on
    // it (lighter under black text, darker under white) until that text
    // reaches 7:1.
    function fillFor(c) {
        let colour = Qt.color(c)
        const lighten = contrast(black, colour) >= contrast(white, colour)
        for (let i = 0; i < 40 && Math.max(contrast(black, colour), contrast(white, colour)) < 7; ++i)
            colour = Qt.tint(colour, lighten ? "#1affffff" : "#1a000000")
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

    // "Any Many Miny Moe [Prod. By Drumma Boy]" -> "Any Many Miny Moe" for
    // the big title; credits() gives "Prod. By Drumma Boy" for the line below.
    readonly property var creditPattern: /\s*[\(\[]([^\)\]]*\b(prod|produced|feat|ft|featuring)\b[^\)\]]*)[\)\]]/gi
    function mainTitle(title) {
        const t = (title || "").replace(creditPattern, "").trim()
        return t.length ? t : (title || "")
    }
    function credits(title) {
        const out = []
        const re = new RegExp(creditPattern.source, "gi")
        let m
        while ((m = re.exec(title || "")) !== null)
            out.push(m[1].trim())
        return out.join("  ·  ")
    }

    function artistName(artist) {
        if (artist === "*")
            return qsTr("Various artists")
        return artist && artist.length ? artist : qsTr("Unknown artist")
    }
}
