import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// Setting Halo up, in three short steps: which folders the music is in
// (each with a count of the songs Halo finds there), how it should play, and
// the library being read. Shown on the first run (player.onboarded), and
// again whenever it is asked for (Ctrl+,). Nothing changes until a step is
// left with Next: the folders are saved then, and the switches on the second
// step are the same ones the rest of Halo uses.
Rectangle {
    id: root

    // False when it was opened again later: Esc then closes, and says so.
    property bool firstRun: true
    property int step: 0
    readonly property int steps: 3
    // [{path, chosen, kind}], from library.folderChoices() plus any added here.
    property var choices: []
    // path -> {songs, more}; missing while still counting, songs -1 if it isn't a folder.
    property var counts: ({})
    // The row the keyboard is on, in steps 0 and 1.
    property int cursor: 0
    property string note: ""
    signal pickFolder()
    signal finished()

    // Solid: nothing of the window behind shows through or takes a click.
    color: Style.window
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Set up Halo")
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    function shortPath(path) {
        const home = StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace(/^file:\/\//, "")
        return path === home ? "~" : path.startsWith(home + "/") ? "~" + path.substring(home.length) : path
    }
    function number(n) { return Number(n).toLocaleString(Qt.locale(), "f", 0) }

    // ---- the folders ----
    function begin() {
        step = 0
        cursor = 0
        note = ""
        counts = ({})
        choices = library.folderChoices()
        for (const c of choices)
            library.countSongs(c.path)
        forceActiveFocus()
    }
    function addChoice(path) {
        const at = choices.findIndex(c => c.path === path)
        const next = choices.slice()
        if (at >= 0) {
            next[at] = { path: path, chosen: true, kind: next[at].kind }
        } else {
            next.push({ path: path, chosen: true, kind: "yours" })
            library.countSongs(path)
        }
        choices = next
        cursor = at >= 0 ? at : next.length - 1
        note = ""
    }
    function toggleChoice(index) {
        if (library.foldersPinned || index < 0 || index >= choices.length)
            return
        const next = choices.slice()
        next[index] = { path: next[index].path, chosen: !next[index].chosen, kind: next[index].kind }
        choices = next
        note = ""
    }
    readonly property var chosenPaths: choices.filter(c => c.chosen).map(c => c.path)
    readonly property int chosenSongs: {
        let total = 0
        for (const c of choices)
            if (c.chosen && counts[c.path] && counts[c.path].songs > 0)
                total += counts[c.path].songs
        return total
    }
    readonly property bool counting: choices.some(c => c.chosen && counts[c.path] === undefined)
    Connections {
        target: library
        function onSongsCounted(folder, songs, more) {
            const next = Object.assign({}, root.counts)
            next[folder] = { songs: songs, more: more }
            root.counts = next
        }
    }
    function countText(path) {
        const c = counts[path]
        if (c === undefined)
            return qsTr("counting")
        if (c.songs < 0)
            return qsTr("not there now")
        if (c.songs === 0)
            return qsTr("no songs")
        return c.more ? qsTr("over %1 songs").arg(number(c.songs)) : qsTr("%n song(s)", "", c.songs)
    }
    function kindText(kind) {
        return kind === "music" ? qsTr("your music folder")
             : kind === "downloads" ? qsTr("downloads")
             : kind === "drive" ? qsTr("on a drive")
             : ""
    }

    // ---- how it plays ----
    readonly property var options: [
        { key: "blend", glyph: "blend", title: qsTr("Blend"),
          body: qsTr("Songs mix into each other on the beat, like a DJ set. Off, they play one after another with no gap.") },
        { key: "drift", glyph: "drift", title: qsTr("Drift"),
          body: qsTr("When the queue runs out, Halo keeps going with songs from your library that sound close to the last one.") },
        { key: "normalize", glyph: "volume", title: qsTr("Even loudness"),
          body: qsTr("Quiet and loud songs are brought to the same level, so you don't reach for the volume.") },
        { key: "lyrics", glyph: "lyrics", title: qsTr("Online lyrics"),
          body: qsTr("Halo asks lrclib.net for the words of the song playing, sending its title, artist, album and length. Nothing else goes online, and there is no account.") }
    ]
    function optionOn(key) {
        return key === "blend" ? player.blend : key === "drift" ? player.drift
             : key === "normalize" ? player.normalize : lyrics.online
    }
    function toggleOption(key) {
        if (key === "blend") player.blend = !player.blend
        else if (key === "drift") player.drift = !player.drift
        else if (key === "normalize") player.normalize = !player.normalize
        else lyrics.online = !lyrics.online
    }

    // ---- moving through it ----
    readonly property int rows: step === 0 ? choices.length + (library.foldersPinned ? 0 : 1) : step === 1 ? options.length : 0
    function next() {
        if (step === 0) {
            if (!library.foldersPinned) {
                if (chosenPaths.length === 0) {
                    note = qsTr("Choose at least one folder, or add yours.")
                    return
                }
                if (!library.setFolders(chosenPaths)) {
                    note = qsTr("Halo couldn't save those folders. Check that they are still there.")
                    return
                }
            }
            note = ""
            step = 1
            cursor = 0
        } else if (step === 1) {
            step = 2
        } else {
            root.finished()
        }
    }
    function back() {
        if (step > 0) {
            step -= 1
            cursor = 0
            note = ""
        }
    }
    function activate() {
        if (step === 0) {
            if (cursor < choices.length) toggleChoice(cursor)
            else root.pickFolder()
        } else if (step === 1) {
            toggleOption(options[cursor].key)
        }
    }

    focus: true
    Keys.onPressed: event => {
        const k = event.key
        if (k === Qt.Key_Return || k === Qt.Key_Enter) next()
        else if (k === Qt.Key_Escape) root.finished()
        else if (k === Qt.Key_Backspace || k === Qt.Key_Left) back()
        else if (k === Qt.Key_Right) { if (step < steps - 1) next() }
        else if ((k === Qt.Key_Down || k === Qt.Key_J) && rows > 0) cursor = Math.min(rows - 1, cursor + 1)
        else if ((k === Qt.Key_Up || k === Qt.Key_K) && rows > 0) cursor = Math.max(0, cursor - 1)
        else if (k === Qt.Key_Space) activate()
        else if (k === Qt.Key_A && step === 0 && !library.foldersPinned) root.pickFolder()
        else return
        event.accepted = true
    }

    // A square that fills when its row is on, in whole pixels like the rest.
    component Tick: Rectangle {
        property bool on: false
        implicitWidth: 18
        implicitHeight: 18
        color: "transparent"
        border.width: 2
        border.color: on ? Style.accentText : Style.faint
        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            color: Style.accentText
            visible: parent.on
        }
    }

    // A row to choose: lit under the pointer or the keyboard's cursor.
    component Row_: Rectangle {
        id: row
        property int at: 0
        property bool current: root.cursor === at
        default property alias content: inner.data
        signal chosen()
        Layout.fillWidth: true
        implicitHeight: Math.max(Style.target + 8, inner.implicitHeight + 20)
        radius: Style.small
        color: current ? Style.hover : hover.hovered ? Style.raised : "transparent"
        border.width: current ? 1 : 0
        border.color: Style.line
        RowLayout {
            id: inner
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 14
        }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: { root.cursor = row.at; root.forceActiveFocus(); row.chosen() } }
    }

    Flickable {
        id: scroll
        SmoothWheel { flickable: scroll }
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + 96)
        clip: true
        ScrollBar.vertical: PixelScrollBar {}

        ColumnLayout {
            id: column
            width: Math.min(scroll.width - 48, 640)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(40, (scroll.height - implicitHeight) / 2)
            spacing: 0

            PixelWordmark {
                Layout.alignment: Qt.AlignHCenter
                cell: 6
                Accessible.ignored: true
            }

            // Where you are: three pixels, the current one wide.
            Row {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 22
                spacing: 6
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Step %1 of %2").arg(root.step + 1).arg(root.steps)
                Repeater {
                    model: root.steps
                    Rectangle {
                        required property int index
                        width: index === root.step ? 28 : 8
                        height: 8
                        color: index <= root.step ? Style.accent : Style.line
                        Behavior on width { NumberAnimation { duration: Style.motion ? Style.quick : 0; easing.type: Easing.OutCubic } }
                    }
                }
            }

            ScrambleText {
                Layout.fillWidth: true
                Layout.topMargin: 22
                target: root.step === 0 ? qsTr("Where is your music?")
                      : root.step === 1 ? qsTr("How should it play?")
                      : library.scanning ? qsTr("Reading your music")
                      : library.trackCount > 0 ? qsTr("Ready") : qsTr("No songs yet")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Style.text
                font.family: Style.mono
                font.pixelSize: Style.fontLarge + 5
                font.weight: Font.Bold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 2
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 10
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Style.muted
                font.pixelSize: Style.fontBody
                lineHeight: 1.15
                text: root.step === 0
                        ? (library.foldersPinned
                            ? qsTr("These folders are set by the HALO_MUSIC variable Halo was started with, so they can't be changed here.")
                            : qsTr("Halo plays the songs in the folders you tick, and in every folder inside them. It only reads them: nothing is moved, copied or sent anywhere."))
                    : root.step === 1 ? qsTr("Four things Halo does that you might not expect. Each has a key to change it later.")
                    : library.scanning ? qsTr("%n file(s) so far. You can start now; albums appear as Halo reads them.", "", library.scanned)
                    : library.trackCount > 0
                        ? qsTr("%1 in %2.").arg(qsTr("%n song(s)", "", library.trackCount)).arg(qsTr("%n album(s)", "", library.albumCount))
                        : qsTr("Halo found no songs in the folders you chose. It plays MP3, FLAC, Ogg, Opus, M4A, WAV and most other music files. Go back and choose another folder, or drop songs on the window.")
            }

            // ---- step 1: folders ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 22
                spacing: 4
                visible: root.step === 0

                Repeater {
                    model: root.choices
                    Row_ {
                        id: folderRow
                        required property var modelData
                        required property int index
                        at: folderRow.index
                        onChosen: root.toggleChoice(folderRow.index)
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: root.shortPath(modelData.path) + ", " + root.countText(modelData.path)
                        Accessible.checked: modelData.chosen

                        Tick { on: folderRow.modelData.chosen; Layout.alignment: Qt.AlignVCenter }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: root.shortPath(folderRow.modelData.path)
                                color: folderRow.modelData.chosen ? Style.text : Style.muted
                                font.family: Style.mono
                                font.pixelSize: Style.fontBody
                                font.weight: Font.DemiBold
                                elide: Text.ElideMiddle
                            }
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                visible: text.length > 0
                                text: root.kindText(folderRow.modelData.kind)
                                color: Style.faint
                                font.pixelSize: Style.fontSmall
                            }
                        }
                        Text {
                            textFormat: Text.PlainText
                            text: root.countText(folderRow.modelData.path)
                            color: root.counts[folderRow.modelData.path] !== undefined && root.counts[folderRow.modelData.path].songs > 0
                                   ? Style.accentText : Style.faint
                            font.family: Style.mono
                            font.pixelSize: Style.fontBody
                        }
                    }
                }
                Row_ {
                    at: root.choices.length
                    visible: !library.foldersPinned
                    onChosen: root.pickFolder()
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Add a folder")
                    Glyph { width: 18; height: 18; name: "add"; color: Style.accentText; Layout.alignment: Qt.AlignVCenter }
                    Text {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        text: qsTr("Add a folder")
                        color: Style.text
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                    }
                    Keycap { label: "A" }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: root.note.length ? Style.danger : Style.muted
                    font.pixelSize: Style.fontBody
                    text: root.note.length ? root.note
                        : root.chosenPaths.length === 0 ? qsTr("No folder ticked yet.")
                        : root.counting ? qsTr("Counting the songs in %n folder(s).", "", root.chosenPaths.length)
                        : qsTr("%1 in %2.").arg(qsTr("%n song(s)", "", root.chosenSongs)).arg(qsTr("%n folder(s)", "", root.chosenPaths.length))
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }

            // ---- step 2: how it plays ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 22
                spacing: 4
                visible: root.step === 1

                Repeater {
                    model: root.options
                    Row_ {
                        id: optionRow
                        required property var modelData
                        required property int index
                        at: optionRow.index
                        readonly property bool on: root.optionOn(modelData.key)
                        onChosen: root.toggleOption(modelData.key)
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: modelData.title + ". " + modelData.body
                        Accessible.checked: on

                        Glyph {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 12
                            width: 24
                            height: 24
                            name: optionRow.modelData.glyph
                            color: optionRow.on ? Style.accentText : Style.faint
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            Layout.bottomMargin: 10
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: optionRow.modelData.title
                                color: Style.text
                                font.family: Style.mono
                                font.pixelSize: Style.fontBody
                                font.weight: Font.Bold
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: 1
                            }
                            Text {
                                Layout.fillWidth: true
                                textFormat: Text.PlainText
                                text: optionRow.modelData.body
                                color: Style.muted
                                font.pixelSize: Style.fontBody
                                wrapMode: Text.Wrap
                                lineHeight: 1.1
                            }
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.alignment: Qt.AlignVCenter
                            text: optionRow.on ? qsTr("On") : qsTr("Off")
                            color: optionRow.on ? Style.accentText : Style.faint
                            font.family: Style.mono
                            font.pixelSize: Style.fontBody
                            font.weight: Font.Bold
                            font.capitalization: Font.AllUppercase
                        }
                        Tick { on: optionRow.on; Layout.alignment: Qt.AlignVCenter }
                    }
                }
            }

            // ---- step 3: reading, and the keys worth knowing ----
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 22
                spacing: 0
                visible: root.step === 2

                PixelLoader {
                    Layout.alignment: Qt.AlignHCenter
                    visible: library.scanning
                    count: 18
                    fraction: -1
                }
                PixelRule { Layout.fillWidth: true; Layout.topMargin: 18; Layout.bottomMargin: 16 }
                GridLayout {
                    Layout.alignment: Qt.AlignHCenter
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 10
                    Repeater {
                        model: [
                            ["Space", qsTr("Play or pause")],
                            ["/", qsTr("Search your library")],
                            ["F", qsTr("Now Playing, with the visuals")],
                            ["L", qsTr("Lyrics, live with the song")],
                            ["Ctrl+,", qsTr("Come back to this setup")],
                            ["?", qsTr("Every other shortcut")]
                        ]
                        RowLayout {
                            required property var modelData
                            spacing: 10
                            Item {
                                implicitWidth: 64
                                implicitHeight: 20
                                Keycap { anchors.right: parent.right; label: modelData[0] }
                            }
                            Text {
                                textFormat: Text.PlainText
                                text: modelData[1]
                                color: Style.muted
                                font.pixelSize: Style.fontBody
                            }
                        }
                    }
                }
            }

            // ---- back, next ----
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 26
                spacing: 12
                Button {
                    text: qsTr("Back")
                    flat: true
                    visible: root.step > 0
                    implicitHeight: Style.target
                    focusPolicy: Qt.NoFocus
                    Material.foreground: Style.muted
                    onClicked: { root.back(); root.forceActiveFocus() }
                }
                AccentButton {
                    text: root.step === 0 ? qsTr("Next")
                        : root.step === 1 ? qsTr("Next")
                        : library.trackCount > 0 || library.scanning ? qsTr("Start listening") : qsTr("Close")
                    font.pixelSize: Style.fontBody + 1
                    font.weight: Font.DemiBold
                    focusPolicy: Qt.NoFocus
                    onClicked: { root.next(); root.forceActiveFocus() }
                }
                Button {
                    text: root.firstRun ? qsTr("Skip") : qsTr("Close")
                    flat: true
                    visible: root.step < 2
                    implicitHeight: Style.target
                    focusPolicy: Qt.NoFocus
                    Material.foreground: Style.muted
                    Accessible.name: root.firstRun ? qsTr("Skip the setup and look around") : qsTr("Close the setup")
                    onClicked: root.finished()
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 16
                spacing: 8
                Accessible.ignored: true
                Keycap { label: "↑ ↓"; visible: root.step < 2 }
                Keycap { label: "Space"; visible: root.step < 2 }
                Text { textFormat: Text.PlainText; text: qsTr("choose"); color: Style.faint; font.pixelSize: Style.fontSmall; visible: root.step < 2 }
                Keycap { label: "Enter" }
                Text { textFormat: Text.PlainText; text: root.step < 2 ? qsTr("next") : qsTr("start"); color: Style.faint; font.pixelSize: Style.fontSmall }
                Keycap { label: "Esc"; visible: root.step < 2 }
                Text { textFormat: Text.PlainText; text: root.firstRun ? qsTr("skip") : qsTr("close"); color: Style.faint; font.pixelSize: Style.fontSmall; visible: root.step < 2 }
            }
        }
    }
}
