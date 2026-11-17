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
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.visualStyle = (player.visualStyle + 1) % 3 }
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

        // The living pixel field behind the library; its beat ring starts
        // at the cover in the bar.
        PixelField {
            anchors.fill: parent
            origin: Qt.point(58, height - 52)
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

                GlitchDriver { id: pageGlitch; duration: 300 }
                StackLayout {
                    id: pages
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.view
                    onCurrentIndexChanged: pageGlitch.kick(0.7)
                    layer.enabled: pageGlitch.active
                    layer.effect: GlitchEffect { amount: pageGlitch.amount; seed: pageGlitch.seed }
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
                }
            }

            NowPlayingBar {
                Layout.fillWidth: true
                onOpenNowPlaying: win.nowPlaying = true
                onOpenEq: eq.open()
                onOpenSleep: sleepMenu.popup()
            }
        }

        NowPlayingView {
            anchors.fill: parent
            anchors.bottomMargin: 0
            visible: win.nowPlaying
            onClosed: win.nowPlaying = false
        }

        // Short confirmations ("Moment saved at 1:23").
        Rectangle {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 120
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
            Behavior on opacity { NumberAnimation { duration: 160 } }
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
            visible: win.helpVisible
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
