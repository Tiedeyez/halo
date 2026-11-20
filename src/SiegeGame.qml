import QtQuick
import Halo

// Siege: the lyrics as a fixed shooter. Every line still to be sung comes
// down as a row of letters, timed so it reaches the dashed line the moment
// it is sung, and the whole formation steps sideways on the beat. Shoot
// letters before they land; a whole word or a whole line pays more. The
// lowest rows drop bombs, more of them when the song is loud. Three hits
// and the siege is over; the song plays on either way.
//
// Opened with Up Up Down Down Left Right Left Right in the lyrics (see
// LyricsView). Keys while playing: Left/Right or A/D move, Space fires,
// P plays or pauses, Esc leaves. The mouse steers and clicks fire. The game
// only runs while the music plays and keeps its time from the lyrics clock,
// so seeking moves the rows with the song.
Item {
    id: game
    focus: true

    readonly property bool ready: lyrics.status === Lyrics.Found &&
                                  (lyrics.kind === Lyrics.KindSynced || lyrics.kind === Lyrics.KindWords)
    readonly property bool live: ready && player.playing

    property int score: 0
    property int shields: 3
    property int combo: 0
    readonly property int multiplier: Math.min(8, 1 + Math.floor(combo / 12))
    property bool over: false
    // Every line has been sung; the score stands until the next song.
    property bool sung: false
    property real intro: 1.6

    // Playfield state, plain JS so a frame doesn't fire bindings.
    property var rows: []
    property var bullets: []
    property var bombs: []
    property var sparks: []
    property var floats: []
    property real t: 0
    property int lastBeat: -1
    property real marchX: 0
    property real pps: 80
    property real fontPx: 24
    property real charW: 14.4
    property real measuredFor: 0
    property real shipX: -1
    property real targetX: -1
    property bool leftHeld: false
    property bool rightHeld: false
    property bool fireHeld: false
    property real cooldown: 0
    property real invuln: 0
    property real clock: 0

    readonly property real px: Math.max(3, Math.round(fontPx / 7))
    readonly property real shipW: 11 * px
    readonly property real shipH: 7 * px
    readonly property real shipY: height - 24 - shipH / 2
    readonly property real singY: height - shipH - 64
    readonly property string fontName: "JetBrainsMono Nerd Font"

    // The ship: a ring with a cannon, the halo itself.
    readonly property var shipArt: [
        ".....#.....",
        "....###....",
        "..#######..",
        ".##.....##.",
        "##..###..##",
        ".##.....##.",
        "..#######.."
    ]

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Siege, a lyrics game. Score %1.").arg(score)

    function songTime() { return lyrics.time - lyrics.offset }
    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

    // Rows from the lyrics: each letter is a target, spaces are not.
    function build() {
        const src = ready ? lyrics.lines : []
        const out = []
        for (let i = 0; i < src.length; ++i) {
            const l = src[i]
            const text = String(l.text || "")
            if (l.start < 0 || text.trim().length === 0)
                continue
            const chars = []
            const wordOf = []
            const words = []
            let alive = 0
            for (let c = 0; c < text.length; ++c) {
                const target = text[c].trim().length > 0
                chars.push(target)
                if (target) {
                    if (c === 0 || !chars[c - 1])
                        words.push({ left: 0 })
                    words[words.length - 1].left++
                    wordOf.push(words.length - 1)
                    ++alive
                } else {
                    wordOf.push(-1)
                }
            }
            out.push({ text: text, start: l.start, chars: chars, wordOf: wordOf, words: words,
                       alive: alive, total: alive, passed: l.start <= songTime() })
        }
        rows = out
        layout()
    }

    // Letter size from the longest line; fall speed so lines don't pile up.
    function layout() {
        let longest = 12
        const gaps = []
        for (let i = 0; i < rows.length; ++i) {
            longest = Math.max(longest, rows[i].text.length)
            if (i > 0 && rows[i].start - rows[i - 1].start > 0.25)
                gaps.push(rows[i].start - rows[i - 1].start)
        }
        fontPx = clamp(Math.floor(width * 0.88 / (longest * 0.6)), 12, 30)
        gaps.sort((a, b) => a - b)
        const median = gaps.length ? gaps[Math.floor(gaps.length / 2)] : 3
        pps = clamp(fontPx * 2.4 / median, fontPx * 1.2, Math.max(fontPx * 1.2, singY / 3))
    }

    function start() {
        score = 0
        shields = 3
        combo = 0
        over = false
        intro = 1.6
        bullets = []
        bombs = []
        sparks = []
        floats = []
        t = songTime()
        lastBeat = -1
        shipX = width / 2
        targetX = -1
        build()
        forceActiveFocus()
    }

    // Another go, same song: start() rebuilds the rows, so the lines still
    // to come are whole again.
    function again() { start() }

    function rowY(r) { return singY - (r.start - t) * pps }
    function rowX(r) { return (width - r.chars.length * charW) / 2 + marchX }

    function burst(x, y, n, color) {
        for (let i = 0; i < n; ++i) {
            const a = Math.random() * Math.PI * 2
            const v = 60 + Math.random() * 160
            sparks.push({ x: x, y: y, vx: Math.cos(a) * v, vy: Math.sin(a) * v - 60,
                          life: 0.45 + Math.random() * 0.35, color: color })
        }
    }

    function kill(r, c, x, y) {
        r.chars[c] = false
        r.alive--
        combo++
        const m = multiplier
        score += 10 * m
        burst(x, y, 7, Math.random() < 0.5 ? Style.accent : Style.text)
        const word = r.words[r.wordOf[c]]
        if (--word.left === 0) {
            score += 40 * m
            floats.push({ x: x, y: y - fontPx, text: "+" + (50 * m), life: 0.8 })
        }
        if (r.alive === 0) {
            score += 200 * m
            floats.push({ x: width / 2, y: y - fontPx * 1.6, text: qsTr("CLEAR") + " +" + (200 * m), life: 1.2 })
            burst(x, y, 18, Style.accent)
        }
        if (score > Secrets.best)
            Secrets.best = score
    }

    function fire() {
        if (over || intro > 0 || cooldown > 0 || bullets.length >= 3)
            return
        bullets.push({ x: shipX, y: shipY - shipH / 2 })
        cooldown = 0.16
    }

    // On a beat, maybe a bomb from a live letter in the lowest rows.
    function maybeBomb() {
        if (bombs.length >= 6)
            return
        const low = []
        for (const r of rows) {
            if (r.passed || r.alive === 0)
                continue
            const y = rowY(r)
            if (y > fontPx && y < singY - fontPx * 2.5)
                low.push(r)
        }
        if (!low.length)
            return
        low.sort((a, b) => a.start - b.start)
        const pick = low[Math.floor(Math.random() * Math.min(2, low.length))]
        const chance = clamp(0.28 + spectrumSource.level * 0.5 + Math.min(score, 6000) / 30000, 0, 0.8)
        if (Math.random() > chance)
            return
        const cols = []
        for (let c = 0; c < pick.chars.length; ++c)
            if (pick.chars[c]) cols.push(c)
        const c = cols[Math.floor(Math.random() * cols.length)]
        bombs.push({ x: rowX(pick) + (c + 0.5) * charW, y: rowY(pick),
                     vy: height * (0.32 + Math.random() * 0.14), phase: 0 })
    }

    function hitShip() {
        shields--
        combo = 0
        invuln = 1.6
        burst(shipX, shipY, 22, Style.danger)
        if (shields <= 0) {
            over = true
            burst(shipX, shipY, 40, Style.accent)
        }
    }

    function tick(dt) {
        dt = Math.min(dt, 0.05)
        clock += dt
        // The lyrics clock moves in steps; glide between them.
        const target = songTime()
        if (!live || Math.abs(target - t) > 0.4)
            t = target
        else
            t += dt + (target - t) * Math.min(1, dt * 4)
        if (!live) {
            canvas.requestPaint()
            return
        }
        intro = Math.max(0, intro - dt)
        cooldown -= dt
        invuln -= dt

        // Ship.
        if (shipX < 0)
            shipX = width / 2
        const dir = (rightHeld ? 1 : 0) - (leftHeld ? 1 : 0)
        const speed = Math.max(320, width * 0.8)
        if (dir !== 0) {
            targetX = -1
            shipX += dir * speed * dt
        } else if (targetX >= 0) {
            shipX += clamp(targetX - shipX, -speed * 1.4 * dt, speed * 1.4 * dt)
        }
        shipX = clamp(shipX, shipW, width - shipW)
        if (fireHeld)
            fire()

        // The march: one step a beat, eight steps out and back.
        let bpm = player.bpm > 0 ? player.bpm : 96
        while (bpm > 150) bpm /= 2
        while (bpm < 70) bpm *= 2
        const beat = Math.floor(t * bpm / 60)
        if (beat !== lastBeat) {
            if (beat === lastBeat + 1 && !over && intro <= 0)
                maybeBomb()
            lastBeat = beat
        }
        const step = ((beat % 16) + 16) % 16
        const tri = step < 8 ? step : 16 - step
        marchX = (tri - 4) * Math.max(4, width * 0.012)

        // Lines being sung now: whatever is left of them got through.
        for (const r of rows) {
            const due = r.start <= t
            if (due && !r.passed) {
                r.passed = true
                if (r.alive > 0 && !over)
                    combo = 0
            } else if (!due && r.passed) {
                r.passed = false // seeked back
            }
        }

        sung = rows.length > 0 && rows.every(r => r.passed)

        // Shots.
        const keep = []
        for (const b of bullets) {
            b.y -= height * 1.5 * dt
            let hit = false
            for (const r of rows) {
                if (r.passed || r.alive === 0)
                    continue
                const y = rowY(r)
                if (Math.abs(b.y - (y - fontPx * 0.35)) > fontPx * 0.55)
                    continue
                const x0 = rowX(r)
                const c = Math.floor((b.x - x0) / charW)
                if (c >= 0 && c < r.chars.length && r.chars[c]) {
                    kill(r, c, x0 + (c + 0.5) * charW, y - fontPx * 0.35)
                    hit = true
                    break
                }
            }
            if (!hit) {
                for (let i = 0; i < bombs.length; ++i) {
                    if (Math.abs(bombs[i].x - b.x) < 7 && Math.abs(bombs[i].y - b.y) < 12) {
                        burst(bombs[i].x, bombs[i].y, 6, Style.danger)
                        bombs.splice(i, 1)
                        score += 5
                        hit = true
                        break
                    }
                }
            }
            if (!hit && b.y > -12)
                keep.push(b)
        }
        bullets = keep

        // Bombs.
        const falling = []
        for (const m of bombs) {
            m.y += m.vy * dt
            m.phase += dt * 10
            if (!over && invuln <= 0 && Math.abs(m.x - shipX) < shipW / 2 + 2
                    && m.y > shipY - shipH / 2 && m.y < shipY + shipH / 2) {
                hitShip()
                continue
            }
            if (m.y < height + 12)
                falling.push(m)
        }
        bombs = falling

        // Debris and score pops.
        sparks = sparks.filter(p => {
            p.x += p.vx * dt
            p.y += p.vy * dt
            p.vy += 520 * dt
            p.life -= dt
            return p.life > 0
        })
        floats = floats.filter(f => {
            f.y -= 34 * dt
            f.life -= dt
            return f.life > 0
        })
        canvas.requestPaint()
    }

    Connections {
        target: lyrics
        function onChanged() { game.build() }
    }
    onWidthChanged: layout()
    onHeightChanged: layout()
    Component.onCompleted: start()
    onActiveFocusChanged: if (!activeFocus) { leftHeld = false; rightHeld = false; fireHeld = false }

    FrameAnimation {
        running: game.visible
        onTriggered: game.tick(frameTime)
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.font = "bold " + game.fontPx + "px '" + game.fontName + "'"
            ctx.textBaseline = "alphabetic"
            if (game.measuredFor !== game.fontPx) {
                game.charW = ctx.measureText("MMMMMMMMMM").width / 10
                game.measuredFor = game.fontPx
            }
            const cw = game.charW
            const singY = game.singY
            const pulse = Style.motion ? spectrumSource.pulse : 0

            // Where lines are sung: a dashed pixel rule.
            ctx.fillStyle = Style.line
            for (let x = 8; x < width - 8; x += 12)
                ctx.fillRect(x, singY + 6, 6, 2)

            // The rows, nearest to be sung in the accent.
            let next = true
            for (const r of game.rows) {
                const y = game.rowY(r)
                if (y < -game.fontPx || y > height + game.fontPx)
                    continue
                const x0 = game.rowX(r)
                const fade = r.passed ? game.clamp(1 - (y - singY) / Math.max(1, height - singY), 0, 1) : 1
                if (fade <= 0)
                    continue
                // What was shot stays faintly, so the words can still be read.
                if (r.alive < r.total || r.passed) {
                    ctx.globalAlpha = 0.5 * fade
                    ctx.fillStyle = Style.line
                    ctx.fillText(r.text, x0, y)
                }
                if (r.alive > 0) {
                    const isNext = !r.passed && next
                    if (!r.passed) next = false
                    ctx.globalAlpha = (r.passed ? 0.55 : 1) * fade
                    ctx.fillStyle = r.passed ? Style.faint : isNext ? Style.accentText : Style.text
                    const hop = !r.passed ? -pulse * 3 : 0
                    let c = 0
                    while (c < r.chars.length) {
                        if (!r.chars[c]) { ++c; continue }
                        let e = c
                        while (e < r.chars.length && r.chars[e])
                            ++e
                        ctx.fillText(r.text.substring(c, e), x0 + c * cw, y + hop)
                        c = e
                    }
                } else if (!r.passed) {
                    next = false
                }
            }
            ctx.globalAlpha = 1

            // Shots and bombs.
            ctx.fillStyle = Style.accent
            for (const b of game.bullets)
                ctx.fillRect(b.x - 1.5, b.y - 9, 3, 11)
            ctx.fillStyle = Style.danger
            for (const m of game.bombs) {
                const s = Math.floor(m.phase) % 2 ? 1 : -1
                for (let k = 0; k < 3; ++k)
                    ctx.fillRect(m.x - 2 + (k % 2 ? s : -s) * 2, m.y - 6 + k * 4, 4, 4)
            }
            for (const p of game.sparks) {
                ctx.globalAlpha = game.clamp(p.life * 2, 0, 1)
                ctx.fillStyle = p.color
                ctx.fillRect(Math.round(p.x) - 1.5, Math.round(p.y) - 1.5, 3, 3)
            }
            ctx.globalAlpha = 1
            ctx.font = "bold " + Math.max(12, Math.round(game.fontPx * 0.6)) + "px '" + game.fontName + "'"
            ctx.textAlign = "center"
            for (const f of game.floats) {
                ctx.globalAlpha = game.clamp(f.life * 2, 0, 1)
                ctx.fillStyle = Style.accentText
                ctx.fillText(f.text, f.x, f.y)
            }
            ctx.textAlign = "start"
            ctx.globalAlpha = 1

            // The ship, blinking while it recovers from a hit.
            if (!game.over && (game.invuln <= 0 || Math.floor(game.clock * 14) % 2 === 0)) {
                const p = game.px
                const left = Math.round(game.shipX - game.shipW / 2)
                const top = Math.round(game.shipY - game.shipH / 2)
                ctx.fillStyle = game.invuln > 0 ? Style.danger : Style.accent
                for (let j = 0; j < game.shipArt.length; ++j)
                    for (let i = 0; i < game.shipArt[j].length; ++i)
                        if (game.shipArt[j][i] === "#")
                            ctx.fillRect(left + i * p, top + j * p, p, p)
            }
        }
    }

    // Steer with the mouse; click to fire.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        onPositionChanged: (mouse) => game.targetX = mouse.x
        onPressed: (mouse) => {
            game.forceActiveFocus()
            game.targetX = mouse.x
            if (game.over) game.again()
            else { game.fireHeld = true; game.fire() }
        }
        onReleased: game.fireHeld = false
    }

    // Keys the game uses go to it, not to Halo's shortcuts.
    function ours(key) {
        return key === Qt.Key_Left || key === Qt.Key_Right || key === Qt.Key_A || key === Qt.Key_D
            || key === Qt.Key_Space || key === Qt.Key_Up || key === Qt.Key_W
            || key === Qt.Key_Escape || key === Qt.Key_P
    }
    Keys.onShortcutOverride: (event) => event.accepted = ours(event.key)
    Keys.onPressed: (event) => {
        if (!ours(event.key))
            return
        event.accepted = true
        switch (event.key) {
        case Qt.Key_Left: case Qt.Key_A: leftHeld = true; break
        case Qt.Key_Right: case Qt.Key_D: rightHeld = true; break
        case Qt.Key_Space: case Qt.Key_Up: case Qt.Key_W:
            if (event.isAutoRepeat) break
            if (over) again()
            else { fireHeld = true; fire() }
            break
        case Qt.Key_P: if (!event.isAutoRepeat) player.togglePlay(); break
        case Qt.Key_Escape: Secrets.siege = false; break
        }
    }
    Keys.onReleased: (event) => {
        if (event.isAutoRepeat)
            return
        switch (event.key) {
        case Qt.Key_Left: case Qt.Key_A: leftHeld = false; break
        case Qt.Key_Right: case Qt.Key_D: rightHeld = false; break
        case Qt.Key_Space: case Qt.Key_Up: case Qt.Key_W: fireHeld = false; break
        }
    }

    // Score, shields, best.
    Row {
        x: 12
        y: 8
        spacing: 14
        Text {
            text: "SIEGE"
            color: Style.accentText
            font.family: Style.mono
            font.pixelSize: Style.fontBody
            font.bold: true
            font.letterSpacing: 3
        }
        Text {
            text: game.score.toLocaleString(Qt.locale(translator.language), 'f', 0)
            color: Style.text
            font.family: Style.mono
            font.pixelSize: Style.fontBody
            font.bold: true
        }
        Text {
            visible: game.multiplier > 1
            text: "x" + game.multiplier
            color: Style.accentText
            font.family: Style.mono
            font.pixelSize: Style.fontBody
        }
    }
    Row {
        anchors.right: parent.right
        anchors.rightMargin: 56 // clear of Now Playing's close button
        y: 10
        spacing: 6
        Text {
            text: qsTr("BEST %1").arg(Secrets.best.toLocaleString(Qt.locale(translator.language), 'f', 0))
            color: Style.faint
            font.family: Style.mono
            font.pixelSize: Style.fontSmall
            anchors.verticalCenter: parent.verticalCenter
        }
        Item { width: 6; height: 1 }
        Repeater {
            model: 3
            Rectangle {
                required property int index
                width: 10
                height: 10
                color: index < game.shields ? Style.accent : Style.line
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // Cards: the start, a pause, the end, or no timed words to play with.
    Column {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -parent.height * 0.08
        spacing: 10
        visible: headline.text.length > 0
        Text {
            id: headline
            anchors.horizontalCenter: parent.horizontalCenter
            text: !game.ready ? qsTr("NO TIMED WORDS")
                 : game.over ? qsTr("SIEGE OVER")
                 : !player.playing ? qsTr("PAUSED")
                 : game.sung ? qsTr("ALL SUNG")
                 : game.intro > 0 ? qsTr("THE WORDS ARE COMING") : ""
            color: game.over ? Style.danger : Style.text
            font.family: Style.mono
            font.pixelSize: Style.fontTitle
            font.bold: true
            font.letterSpacing: 4
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: !game.ready ? qsTr("This song has no timed lyrics. The siege waits for one that does.")
                 : game.over ? qsTr("%1 points · Space plays again · Esc leaves").arg(game.score.toLocaleString(Qt.locale(translator.language), 'f', 0))
                 : !player.playing ? qsTr("P plays · Esc leaves")
                 : game.sung ? qsTr("%1 points · the siege goes on with the next song").arg(game.score.toLocaleString(Qt.locale(translator.language), 'f', 0))
                 : qsTr("← → move · Space fires · Esc leaves")
            color: Style.muted
            font.family: Style.mono
            font.pixelSize: Style.fontSmall
        }
    }
}
