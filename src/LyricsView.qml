import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The lyrics of the song playing. Timed lyrics scroll on their own, the
// line being sung held a third of the way down; scroll or flick to read
// ahead and they catch up again a few seconds later. Click a line to jump
// there. Untimed lyrics are a page to read. When there are none, the view
// says why and what would find them.
Item {
    id: root

    readonly property bool synced: lyrics.status === Lyrics.Found &&
                                   (lyrics.kind === Lyrics.KindSynced || lyrics.kind === Lyrics.KindWords)
    readonly property bool plain: lyrics.status === Lyrics.Found && lyrics.kind === Lyrics.KindPlain
    readonly property bool hasLines: synced || plain
    // Reading ahead: following pauses until the list has been still a while.
    property bool browsing: false
    // The line being sung, sized to the room there is.
    readonly property int lineSize: Math.round(Math.max(20, Math.min(36, Math.min(height / 10, width / 18))))
    // Siege, the lyrics game, has the view while it runs.
    readonly property bool siege: Secrets.siege

    // Up Up Down Down Left Right Left Right, with timed lyrics showing,
    // starts Siege. Nothing on screen mentions it.
    property var code: []
    // A slider or text field with focus keeps its arrow keys.
    readonly property bool arrowsFree: {
        const f = Window.activeFocusItem
        return !(f instanceof Slider || f instanceof TextInput || f instanceof TextEdit)
    }
    readonly property bool listening: visible && synced && !siege && arrowsFree
    function press(key) {
        const want = [Qt.Key_Up, Qt.Key_Up, Qt.Key_Down, Qt.Key_Down, Qt.Key_Left, Qt.Key_Right, Qt.Key_Left, Qt.Key_Right]
        const now = Date.now()
        code = code.filter(k => now - k.at < 3000).concat([{ key: key, at: now }]).slice(-want.length)
        if (code.length === want.length && code.every((k, i) => k.key === want[i])) {
            code = []
            Secrets.siege = true
        }
    }
    Shortcut { sequence: "Up"; enabled: root.listening; onActivated: root.press(Qt.Key_Up) }
    Shortcut { sequence: "Down"; enabled: root.listening; onActivated: root.press(Qt.Key_Down) }
    Shortcut { sequence: "Left"; enabled: root.listening; onActivated: root.press(Qt.Key_Left) }
    Shortcut { sequence: "Right"; enabled: root.listening; onActivated: root.press(Qt.Key_Right) }

    Loader {
        id: siegeGame
        anchors.fill: parent
        anchors.bottomMargin: footer.visible ? footer.height + 4 : 0
        z: 5
        active: root.siege && root.visible
        sourceComponent: SiegeGame {}
    }

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Lyrics")

    ListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: footer.height + 8
        visible: root.hasLines && !root.siege
        clip: true
        model: root.hasLines ? lyrics.lines : []
        spacing: root.synced ? 14 : 6
        boundsBehavior: Flickable.StopAtBounds
        topMargin: root.synced ? height * 0.34 : 12
        bottomMargin: root.synced ? height * 0.55 : 40
        currentIndex: root.synced ? Math.max(0, lyrics.current) : -1
        // ApplyRange (not strict) so flicking never rewrites currentIndex.
        highlightRangeMode: root.synced && !root.browsing ? ListView.ApplyRange : ListView.NoHighlightRange
        preferredHighlightBegin: height * 0.34
        preferredHighlightEnd: height * 0.34 + 40
        highlightMoveDuration: Style.motion ? 520 : 0
        highlightMoveVelocity: -1
        highlightFollowsCurrentItem: true
        highlight: Item {}
        ScrollBar.vertical: PixelScrollBar { policy: root.plain ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }

        delegate: MouseArea {
            id: row
            required property var modelData
            required property int index
            width: list.width - 24
            x: 8
            height: lineItem.implicitHeight
            hoverEnabled: root.synced
            cursorShape: root.synced ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: if (root.synced) player.seek(Math.max(0, modelData.start - lyrics.offset))
            Accessible.role: root.synced ? Accessible.Button : Accessible.StaticText
            Accessible.name: modelData.text

            // Hovering a line to come shows where a click would jump.
            Rectangle {
                visible: row.containsMouse && lyrics.current !== row.index
                x: -8
                width: 3
                height: parent.height
                color: Style.accent
                opacity: 0.6
            }

            LyricLine {
                id: lineItem
                width: parent.width
                line: row.modelData
                index: row.index
                current: lyrics.current
                synced: root.synced
                time: lyrics.time
                size: root.synced ? root.lineSize : Math.max(17, root.lineSize - 10)
            }
        }

        onMovementStarted: if (root.synced) { root.browsing = true; resume.stop() }
        onMovementEnded: if (root.synced) resume.restart()
        Timer {
            id: resume
            interval: 3500
            onTriggered: {
                root.browsing = false
                if (!list.currentItem)
                    return
                // Glide back to the line being sung.
                catchUp.to = Math.max(list.originY - list.topMargin,
                                      list.currentItem.y - list.preferredHighlightBegin)
                catchUp.restart()
            }
        }
        NumberAnimation { id: catchUp; target: list; property: "contentY"; duration: Style.motion ? 600 : 0; easing.type: Easing.InOutCubic }
        onCurrentIndexChanged: if (!root.browsing && !Style.motion) positionViewAtIndex(currentIndex, ListView.Beginning)
    }

    // Soft top and bottom edges, so lines arrive and leave instead of
    // being cut by the frame.
    Rectangle {
        visible: list.visible
        anchors.top: list.top
        width: parent.width
        height: 72
        gradient: Gradient {
            GradientStop { position: 0; color: Style.window }
            GradientStop { position: 1; color: Qt.alpha(Style.window, 0) }
        }
    }
    Rectangle {
        visible: list.visible
        anchors.bottom: list.bottom
        width: parent.width
        height: 96
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.alpha(Style.window, 0) }
            GradientStop { position: 1; color: Style.window }
        }
    }

    // A break in the singing: three pixels fill as the next line nears,
    // pulsing with the beat, the screensaver's blocks for a rest.
    Row {
        id: rest
        visible: root.synced && lyrics.gap > 0 && !root.browsing && !root.siege
        spacing: 8
        x: 8
        readonly property Item anchorItem: list.currentItem
        y: {
            if (!anchorItem)
                return list.height * 0.34
            const top = anchorItem.y - list.contentY
            return lyrics.current < 0 ? top - 34 : top + anchorItem.height + 14
        }
        Repeater {
            model: 3
            Rectangle {
                required property int index
                readonly property bool lit: (1 - lyrics.gap) * 3 > index
                width: 12
                height: 12
                color: lit ? Style.accent : Style.line
                scale: lit && Style.motion ? 1 + spectrumSource.pulse * 0.35 : 1
            }
        }
    }

    // Nothing to show: why, and what would help.
    ColumnLayout {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -footer.height / 2
        width: Math.min(parent.width - 32, 460)
        visible: !root.hasLines && !root.siege
        spacing: 18

        EmptyState {
            Layout.fillWidth: true
            busy: lyrics.status === Lyrics.Searching
            headline: {
                if (!player.hasTrack) return qsTr("Nothing playing")
                switch (lyrics.status) {
                case Lyrics.Searching: return qsTr("Searching lyrics")
                case Lyrics.Offline: return qsTr("Lyrics online is off")
                case Lyrics.NotFound: return qsTr("No lyrics found")
                case Lyrics.Failed: return qsTr("Couldn't reach lrclib.net")
                }
                return lyrics.kind === Lyrics.KindInstrumental ? qsTr("Instrumental") : ""
            }
            body: {
                if (!player.hasTrack) return qsTr("Play something and its words show here.")
                switch (lyrics.status) {
                case Lyrics.Searching: return qsTr("Asking lrclib.net, the free lyrics database.")
                case Lyrics.Offline: return qsTr("There are no lyrics for this song on this computer. Halo can look them up on lrclib.net, sending the title, artist, album and length of each song you play. Nothing else leaves this computer.")
                case Lyrics.NotFound: return qsTr("LRCLIB doesn't have this one yet. Freestyles and DJ drops often aren't there. A .lrc file beside the song, named like it, works too.")
                case Lyrics.Failed: return qsTr("Check the connection. Halo tries again on the next song.")
                }
                return lyrics.kind === Lyrics.KindInstrumental ? qsTr("No words in this one.") : ""
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 10
            visible: player.hasTrack && (lyrics.status === Lyrics.Offline || lyrics.status === Lyrics.NotFound
                                         || lyrics.status === Lyrics.Failed)
            Button {
                text: lyrics.status === Lyrics.Offline ? qsTr("Turn on online lyrics") : qsTr("Look again")
                highlighted: true
                Material.accent: Style.accent
                Material.foreground: Style.onAccent
                Material.roundedScale: Style.radius
                implicitHeight: Style.target
                onClicked: lyrics.status === Lyrics.Offline ? lyrics.online = true : lyrics.retry()
            }
        }
    }

    // Where the words came from, the timing, and the keys that adjust it.
    RowLayout {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        height: 28
        spacing: 14
        visible: player.hasTrack && lyrics.status === Lyrics.Found
        Text {
            text: {
                const from = lyrics.source === "file" ? qsTr(".lrc file") : lyrics.source === "tags" ? qsTr("song tags") : "LRCLIB"
                const timing = lyrics.kind === Lyrics.KindWords ? qsTr("timed by word")
                             : lyrics.kind === Lyrics.KindSynced ? qsTr("timed by line")
                             : lyrics.kind === Lyrics.KindPlain ? qsTr("not timed") : ""
                return (from + (timing ? " · " + timing : "")).toUpperCase()
            }
            color: Style.faint
            font.family: Style.mono
            font.pixelSize: Style.fontSmall
            font.letterSpacing: 1
        }
        Text {
            visible: Math.abs(lyrics.offset) > 0.001
            text: (lyrics.offset > 0 ? "+" : "") + lyrics.offset.toLocaleString(Qt.locale(translator.language), 'f', 2) + " s"
            color: Style.accentText
            font.family: Style.mono
            font.pixelSize: Style.fontSmall
        }
        Item { Layout.fillWidth: true }
        Text {
            visible: root.synced
            text: qsTr(", . earlier or later")
            color: Style.faint
            font.family: Style.mono
            font.pixelSize: Style.fontSmall
        }
    }
}
