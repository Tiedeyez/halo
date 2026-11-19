import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

ApplicationWindow {
    id: win
    width: 1320
    height: 860
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    title: player.hasTrack ? player.title + " · Halo" : "Halo"
    color: Style.window

    Material.theme: Material.Dark
    Material.accent: Style.accent
    Material.roundedScale: Style.radius
    Material.background: Style.raised

    // 0 albums, 1 one album, 2 songs, 3 queue, 4 moments, 5 most played
    property int view: 0
    property bool nowPlaying: false
    property bool helpVisible: false
    readonly property bool typing: sidebar.search.activeFocus
    readonly property bool free: !typing && !helpVisible && !eq.visible

    function show(v) {
        view = v
        tracks.mode = v === 5 ? 2 : v === 6 ? 3 : 0
        nowPlaying = false
        Qt.callLater(() => { if (pages.currentItem) pages.currentItem.forceActiveFocus() })
    }

    function openAlbum(id) {
        if (!id)
            return
        albumTracks.albumId = id
        const row = albumRow(id)
        albumPage.albumId = id
        albumPage.title = row.title
        albumPage.artist = row.artist
        albumPage.year = row.year
        show(1)
    }

    function albumRow(id) {
        const info = library.albumInfo(id)
        return info.title !== undefined ? info : { title: player.album, artist: player.artist, year: 0 }
    }

    function cycleSleep() {
        const steps = [0, 15, 30, 60, 90]
        const now = player.sleepRemaining
        let next = 15
        for (let i = 0; i < steps.length; ++i)
            if (steps[i] * 60 > now + 30) { next = steps[i]; break }
        if (now > 0 && next === 15 && now > 15 * 60) next = 0
        if (now > 85 * 60) next = 0
        if (next === 0) player.cancelSleep()
        else player.startSleep(next)
    }

    Binding { target: spectrumSource; property: "active"; value: player.playing && win.visibility !== Window.Minimized }

    // ---- keys ----
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.togglePlay() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.next() }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.previous() }
    Shortcut { sequences: ["Ctrl+Right", "Media Next"]; context: Qt.ApplicationShortcut; onActivated: player.next() }
    Shortcut { sequences: ["Ctrl+Left", "Media Previous"]; context: Qt.ApplicationShortcut; onActivated: player.previous() }
    Shortcut { sequence: "Shift+Right"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.seekBy(10) }
    Shortcut { sequence: "Shift+Left"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.seekBy(-10) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.volume = player.volume - 0.05 }
    Shortcut { sequences: ["=", "+"]; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.volume = player.volume + 0.05 }
    Shortcut { sequences: ["/", "Ctrl+F"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: { win.nowPlaying = false; sidebar.search.forceActiveFocus(); sidebar.search.selectAll() } }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(0) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(2) }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(3) }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(4) }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(5) }
    Shortcut { sequence: "6"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(6) }
    Shortcut { sequence: "7"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(7) }
    Shortcut { sequence: "F"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.nowPlaying = !win.nowPlaying }
    // L opens the lyrics, in Now Playing; again hides them.
    Shortcut {
        sequence: "L"
        context: Qt.ApplicationShortcut
        enabled: win.free
        onActivated: {
            if (!win.nowPlaying) { win.nowPlaying = true; lyrics.shown = true }
            else lyrics.shown = !lyrics.shown
        }
    }
    Shortcut { sequence: ","; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: lyrics.nudge(0.1) }
    Shortcut { sequence: "."; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: lyrics.nudge(-0.1) }
    Shortcut { sequences: ["<", "Shift+,"]; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: lyrics.nudge(0.5) }
    Shortcut { sequences: [">", "Shift+."]; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: lyrics.nudge(-0.5) }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.addMoment() }
    Shortcut { sequence: "["; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.jumpMoment(-1) }
    Shortcut { sequence: "]"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.jumpMoment(1) }
    Shortcut { sequence: "B"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.blend = !player.blend }
    Shortcut { sequence: "D"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.drift = !player.drift }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.shuffle = !player.shuffle }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.cycleRepeat() }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: eq.visible ? eq.close() : eq.open() }
    Shortcut { sequence: "Z"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.cycleSleep() }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.visualStyle = (player.visualStyle + 1) % 10 }
    Shortcut { sequence: "Shift+V"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.visualStyle = (player.visualStyle + 9) % 10 }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.calm = !player.calm }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; onActivated: library.rescan() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: player.redo() }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: win.helpVisible || win.nowPlaying || win.view === 1
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.nowPlaying) win.nowPlaying = false
            else win.show(0)
        }
    }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: Qt.quit() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: shell
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // The window materialises out of big pixels when it opens.
        GlitchDriver { id: bootGlitch; duration: 900 }
        layer.enabled: bootGlitch.active
        layer.effect: GlitchEffect { amount: bootGlitch.amount; seed: bootGlitch.seed }
        Timer { interval: 60; running: true; onTriggered: bootGlitch.kick(1) }

        // The living pixel field behind the library; its main beat ring
        // starts in the centre while smaller drops float elsewhere.
        PixelField {
            anchors.fill: parent
            cell: Math.max(3, Math.round(6 / Style.pixelDensity))
            origin: Qt.point(width * 0.5, height * 0.5)
            visible: nowPlayingView.opacity < 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // A missing tool or device is said plainly, never worked around.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: problemText.implicitHeight + 20
                visible: player.problem.length > 0
                color: "#3a1418"
                Text {
                    id: problemText
                    anchors.fill: parent
                    anchors.margins: 10
                    anchors.leftMargin: 20
                    text: player.problem
                    color: "#ffd9dc"
                    font.pixelSize: Style.fontBody
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignVCenter
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Sidebar {
                    id: sidebar
                    Layout.fillHeight: true
                    view: win.view
                    onPicked: v => win.show(v)
                    search.onTextChanged: {
                        albums.query = search.text
                        tracks.query = search.text
                        albumTracks.query = ""
                        if (search.text.length && (win.view === 1 || win.view >= 3))
                            win.view = win.view === 1 ? 0 : 2
                    }
                    search.Keys.onReturnPressed: { if (pages.currentItem) pages.currentItem.forceActiveFocus() }
                }

                // A new page surfaces: it fades in while lifting into place.
                ParallelAnimation {
                    id: pageIn
                    NumberAnimation { target: pages; property: "opacity"; from: 0; to: 1; duration: Style.smooth; easing.type: Easing.OutCubic }
                    NumberAnimation { target: pageLift; property: "y"; from: Style.motion ? 16 : 0; to: 0; duration: Style.gentle; easing.type: Easing.OutQuint }
                }
                StackLayout {
                    id: pages
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.view
                    onCurrentIndexChanged: if (Style.motion) pageIn.restart()
                    transform: Translate { id: pageLift }
                    readonly property Item currentItem: children[currentIndex] || null

                    AlbumGrid {
                        focus: win.view === 0
                        onOpenAlbum: id => win.openAlbum(id)
                    }
                    AlbumPage {
                        id: albumPage
                        focus: win.view === 1
                        onBack: win.show(0)
                        onOpenAlbum: id => win.openAlbum(id)
                    }
                    FocusScope {
                        focus: win.view === 2
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            RowLayout {
                                Layout.margins: 24
                                Layout.bottomMargin: 12
                                ScrambleText {
                                    target: qsTr("Songs")
                                    color: Style.text
                                    font.pixelSize: Style.fontTitle
                                    font.weight: Font.Bold
                                }
                                Text {
                                    Layout.leftMargin: 8
                                    text: qsTr("%n songs", "", tracks.count)
                                    color: Style.muted
                                    font.pixelSize: Style.fontBody
                                }
                            }
                            TrackList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                focus: true
                                model: win.view === 2 ? tracks : null
                                onOpenAlbum: id => win.openAlbum(id)
                            }
                        }
                    }
                    QueuePage { focus: win.view === 3 }
                    MomentsPage { focus: win.view === 4 }
                    FocusScope {
                        focus: win.view === 5
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            ScrambleText {
                                Layout.margins: 24
                                Layout.bottomMargin: 4
                                target: qsTr("Most played")
                                color: Style.text
                                font.pixelSize: Style.fontTitle
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.leftMargin: 24
                                Layout.bottomMargin: 12
                                text: qsTr("Counted on this computer when you hear half a song (or four minutes). Nothing leaves it.")
                                color: Style.faint
                                font.pixelSize: Style.fontSmall
                            }
                            TrackList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                focus: true
                                showPlays: true
                                model: win.view === 5 ? tracks : null
                                onOpenAlbum: id => win.openAlbum(id)
                            }
                        }
                    }
                    // Lost & Found: what you loved and haven't heard lately.
                    FocusScope {
                        focus: win.view === 6
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.margins: 24
                                Layout.bottomMargin: 4
                                ScrambleText {
                                    target: qsTr("Lost & Found")
                                    color: Style.text
                                    font.pixelSize: Style.fontTitle
                                    font.weight: Font.Bold
                                }
                                Item { Layout.fillWidth: true }
                                Button {
                                    text: qsTr("Play")
                                    visible: win.view === 6 && tracks.count > 0
                                    highlighted: true
                                    Material.accent: Style.accent
                                    Material.foreground: Style.onAccent
                                    Material.roundedScale: Style.radius
                                    implicitHeight: Style.target
                                    onClicked: player.playPaths(tracks.paths(), 0)
                                }
                            }
                            Text {
                                Layout.leftMargin: 24
                                Layout.rightMargin: 24
                                Layout.bottomMargin: 12
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("Songs you played often and haven't heard in a while, new each day. Until there's enough history, songs in your library you've never played here.")
                                color: Style.faint
                                font.pixelSize: Style.fontSmall
                            }
                            TrackList {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                focus: true
                                visible: tracks.count > 0
                                model: win.view === 6 ? tracks : null
                                onOpenAlbum: id => win.openAlbum(id)
                            }
                            EmptyState {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.alignment: Qt.AlignTop
                                visible: win.view === 6 && tracks.count === 0
                                headline: qsTr("Nothing lost yet")
                                body: qsTr("Every song here has been played lately. Songs come back once you've played them a few times and then not for six weeks.")
                            }
                        }
                    }
                    // The Library Doctor (7): tag problems found on this
                    // computer, fixed only when asked, every fix undoable.
                    FocusScope {
                        id: doctorPage
                        focus: win.view === 7
                        readonly property var labels: ({
                            "watermark": qsTr("Site names in tags"),
                            "numbered": qsTr("Track numbers or the artist in titles"),
                            "notitle": qsTr("Missing titles"),
                            "noartist": qsTr("Missing artists"),
                            "noalbum": qsTr("Missing albums"),
                            "albumartist": qsTr("Album artist differs within an album"),
                            "duplicate": qsTr("The same song more than once")
                        })
                        readonly property var fields: ({
                            "title": qsTr("Title"),
                            "artist": qsTr("Artist"),
                            "album": qsTr("Album"),
                            "albumartist": qsTr("Album artist"),
                            "comment": qsTr("Comment")
                        })
                        // "Folder/file.mp3": enough to tell copies apart.
                        function shortPath(path) {
                            const parts = path.split("/")
                            return parts.slice(-2).join("/")
                        }
                        function countOf(kind) {
                            let n = 0
                            for (const f of doctor.findings) if (f.kind === kind) ++n
                            return n
                        }
                        onVisibleChanged: if (visible && doctor.findings.length === 0 && !doctor.scanning) doctor.scan()
                        Shortcut { sequence: "U"; enabled: win.view === 7 && win.free; onActivated: doctor.undoLast() }
                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.margins: 24
                                Layout.bottomMargin: 4
                                spacing: 10
                                ScrambleText {
                                    target: qsTr("Library Doctor")
                                    color: Style.text
                                    font.pixelSize: Style.fontTitle
                                    font.weight: Font.Bold
                                }
                                Item { Layout.fillWidth: true }
                                Button {
                                    text: qsTr("Undo last fix (U)")
                                    visible: doctor.undoable > 0
                                    flat: true
                                    Material.foreground: Style.accentText
                                    implicitHeight: Style.target
                                    onClicked: doctor.undoLast()
                                }
                                Button {
                                    text: doctor.scanning ? qsTr("Checking…") : qsTr("Check again")
                                    enabled: !doctor.scanning
                                    flat: true
                                    Material.foreground: Style.accentText
                                    implicitHeight: Style.target
                                    onClicked: doctor.scan()
                                }
                            }
                            Text {
                                Layout.leftMargin: 24
                                Layout.rightMargin: 24
                                Layout.bottomMargin: 12
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("Checks your songs' tags on this computer and proposes fixes from the tags, file names and folders. Nothing is written until you say so, and every fix can be undone. Duplicates are only pointed out.")
                                color: Style.faint
                                font.pixelSize: Style.fontSmall
                            }
                            EmptyState {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: doctor.scanning || doctor.findings.length === 0
                                busy: doctor.scanning
                                headline: doctor.scanning ? qsTr("Reading your tags") : qsTr("Nothing to fix")
                                body: doctor.scanning ? qsTr("Every song's tags, in the background.") : qsTr("Every tag Halo can check looks right.")
                            }
                            ListView {
                                id: findingsList
                                SmoothWheel { flickable: findingsList }
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: !doctor.scanning && doctor.findings.length > 0
                                clip: true
                                model: doctor.findings
                                section.property: "kind"
                                section.delegate: RowLayout {
                                    required property string section
                                    width: findingsList.width
                                    height: Style.target + 16
                                    spacing: 10
                                    Text {
                                        Layout.leftMargin: 24
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: (doctorPage.labels[section] || section) + "  ·  " + doctorPage.countOf(section)
                                        color: Style.text
                                        font.family: Style.mono
                                        font.pixelSize: Style.fontBody
                                        font.capitalization: Font.AllUppercase
                                        font.letterSpacing: 1
                                    }
                                    Button {
                                        Layout.rightMargin: 24
                                        visible: section !== "duplicate"
                                        text: qsTr("Fix all")
                                        flat: true
                                        Material.foreground: Style.accentText
                                        implicitHeight: Style.target
                                        onClicked: doctor.fixAll(section)
                                    }
                                }
                                delegate: RowLayout {
                                    required property var modelData
                                    required property int index
                                    width: findingsList.width
                                    spacing: 12
                                    ColumnLayout {
                                        Layout.leftMargin: 24
                                        Layout.fillWidth: true
                                        Layout.topMargin: 6
                                        Layout.bottomMargin: 6
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            text: doctorPage.shortPath(modelData.path)
                                            textFormat: Text.PlainText
                                            elide: Text.ElideMiddle
                                            color: Style.muted
                                            font.family: Style.mono
                                            font.pixelSize: Style.fontSmall
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            textFormat: Text.PlainText
                                            wrapMode: Text.Wrap
                                            color: Style.text
                                            font.pixelSize: Style.fontBody
                                            text: modelData.kind === "duplicate"
                                                  ? qsTr("Also at: %1").arg(modelData.others.map(doctorPage.shortPath).join(", "))
                                                  : modelData.field.length === 0
                                                    ? qsTr("Nothing on this computer says what it should be.")
                                                    : (doctorPage.fields[modelData.field] || modelData.field) + ":  " + (modelData.before.length ? modelData.before : qsTr("(empty)"))
                                                      + "  →  " + (modelData.after.length ? modelData.after : qsTr("(empty)"))
                                        }
                                    }
                                    Button {
                                        Layout.rightMargin: 24
                                        visible: modelData.fixable
                                        text: qsTr("Fix")
                                        flat: true
                                        Material.foreground: Style.accentText
                                        implicitHeight: Style.target
                                        onClicked: doctor.fix(index)
                                    }
                                }
                                ScrollBar.vertical: PixelScrollBar {}
                            }
                        }
                    }
                }
            }

            NowPlayingBar {
                Layout.fillWidth: true
                onOpenNowPlaying: win.nowPlaying = true
                onOpenEq: eq.open()
                onOpenSleep: sleepMenu.popup()
            }
        }

        // Now Playing rises over the library and settles back into it.
        NowPlayingView {
            id: nowPlayingView
            anchors.fill: parent
            anchors.bottomMargin: 0
            opacity: win.nowPlaying ? 1 : 0
            visible: opacity > 0
            enabled: win.nowPlaying
            scale: Style.motion ? 0.96 + 0.04 * opacity : 1
            Behavior on opacity {
                NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic }
            }
            onClosed: win.nowPlaying = false
        }

        // Short confirmations ("Moment saved at 1:23").
        Rectangle {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            // Floats up a little as it appears.
            anchors.bottomMargin: 108 + 12 * opacity
            width: toastText.implicitWidth + 32
            height: toastText.implicitHeight + 20
            color: Style.raised
            opacity: 0
            visible: opacity > 0
            PixelBrackets { shown: true; arm: 8 }
            ScrambleText {
                id: toastText
                anchors.centerIn: parent
                color: Style.text
                font.family: Style.mono
                font.pixelSize: Style.fontBody
                duration: 300
            }
            Behavior on opacity { NumberAnimation { duration: Style.smooth; easing.type: Easing.OutCubic } }
            Timer { id: toastTimer; interval: 2200; onTriggered: toast.opacity = 0 }
            function show(text) {
                toastText.target = text
                toast.opacity = 1
                toastTimer.restart()
            }
            Connections {
                target: player
                function onMessage(text) { toast.show(text) }
            }
            Connections {
                target: doctor
                function onMessage(text) { toast.show(text) }
            }
            // Say so when a correction starts on its own (E to turn it off).
            property string tunedFor: ""
            Connections {
                target: headphones
                function onChanged() {
                    const now = headphones.active ? headphones.model : ""
                    if (now.length > 0 && now !== toast.tunedFor)
                        toast.show(qsTr("Tuned for %1 · E to change").arg(now))
                    toast.tunedFor = now
                }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            opacity: win.helpVisible ? 1 : 0
            visible: opacity > 0
            enabled: win.helpVisible
            scale: Style.motion ? 0.98 + 0.02 * opacity : 1
            Behavior on opacity {
                NumberAnimation { duration: Style.motion ? Style.smooth : 0; easing.type: Easing.OutCubic }
            }
            onClosed: win.helpVisible = false
        }

        // Drop music on the window: it plays if nothing does, else joins the queue.
        DropArea {
            anchors.fill: parent
            onDropped: drop => {
                const paths = drop.urls.map(u => decodeURIComponent(String(u).replace(/^file:\/\//, "")))
                if (player.hasTrack) player.enqueue(paths)
                else player.playPaths(paths, 0)
            }
        }
    }

    EqPanel { id: eq }

    Menu {
        id: sleepMenu
        Material.roundedScale: Style.radius
        MenuItem { text: qsTr("In 15 minutes"); onTriggered: player.startSleep(15) }
        MenuItem { text: qsTr("In 30 minutes"); onTriggered: player.startSleep(30) }
        MenuItem { text: qsTr("In 45 minutes"); onTriggered: player.startSleep(45) }
        MenuItem { text: qsTr("In 1 hour"); onTriggered: player.startSleep(60) }
        MenuItem { text: qsTr("In 90 minutes"); onTriggered: player.startSleep(90) }
        MenuItem { text: qsTr("Off"); enabled: player.sleepRemaining > 0; onTriggered: player.cancelSleep() }
    }

    Component.onCompleted: Qt.callLater(() => pages.currentItem.forceActiveFocus())
}
