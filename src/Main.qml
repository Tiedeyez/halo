import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
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

    // 0 albums, 1 one album, 2 songs, 3 queue, 4 moments, 5 most played,
    // 6 lost & found, 7 library doctor, 8 a playlist (playlistId)
    property int view: 0
    property string playlistId: ""

    function openPlaylist(id) {
        playlistId = id
        show(8)
    }
    function showPlaylists() { show(8) }
    function openDialog(which) {
        if (which === "smart") smartRules.openFor("")
        else if (which === "edit" && playlistId.length) smartRules.openFor(playlistId)
        else if (which === "new") newPlaylist([])
        else if (which === "rename" && playlistId.length) PlaylistUi.rename(playlistId)
        // "setup", "setup1", "setup2": the setup screen, at that step.
        else if (which === "setupnext") welcome.next()
        else if (which === "setupdone") welcome.finished()
        else if (which.startsWith("setuptick")) welcome.toggleChoice(Number(which.slice(9)) || 0)
        else if (which.startsWith("setup")) {
            setupOpen = true
            Qt.callLater(() => { welcome.step = Math.max(0, Math.min(2, Number(which.slice(5)) || 0)) })
        }
    }
    function newPlaylist(paths) {
        namePrompt.ask(qsTr("New playlist"), qsTr("Create"), "", name => {
            const id = playlists.create(name, paths || [])
            if (!id.length)
                return
            if (paths && paths.length)
                PlaylistUi.toast(qsTr("Added %n songs to %1", "", paths.length).arg(name))
            else
                win.openPlaylist(id)
        })
    }
    property bool nowPlaying: false
    // Leaving Now Playing or hiding the lyrics ends a game of Siege.
    onNowPlayingChanged: if (!nowPlaying) Secrets.siege = false
    Connections {
        target: lyrics
        function onShownChanged() { if (!lyrics.shown) Secrets.siege = false }
    }
    property bool helpVisible: false

    // ---- first run ----
    // The setup (Welcome.qml) opens the first time Halo runs, and again on
    // Ctrl+, or from an empty library's button. Finishing or skipping it
    // counts as onboarded, so it never opens by itself again.
    property bool setupOpen: false
    readonly property bool welcoming: !player.onboarded || setupOpen
    function chooseMusicFolder() { setupOpen = true }
    function closeSetup() {
        player.onboarded = true
        setupOpen = false
        Qt.callLater(() => { if (pages.currentItem) pages.currentItem.forceActiveFocus() })
    }
    FolderDialog {
        id: folderPicker
        title: qsTr("Choose a music folder")
        currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
        onAccepted: welcome.addChoice(decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, "")))
    }

    readonly property bool typing: sidebar.search.activeFocus
    readonly property bool free: !typing && !helpVisible && !eq.visible && !welcoming

    function show(v) {
        // A playlist page with no playlist opens the first one.
        if (v === 8 && (!playlistId.length || !playlists.info(playlistId).id)) {
            const all = playlists.all
            playlistId = all.length ? all[0].id : ""
        }
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
    Shortcut { sequences: ["/", "Ctrl+F"]; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.welcoming; onActivated: { win.nowPlaying = false; sidebar.search.forceActiveFocus(); sidebar.search.selectAll() } }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(0) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(2) }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(3) }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(4) }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(5) }
    Shortcut { sequence: "6"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(6) }
    Shortcut { sequence: "7"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.show(7) }
    Shortcut { sequence: "8"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.showPlaylists() }
    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; enabled: !win.typing && !namePrompt.visible && !win.welcoming; onActivated: win.newPlaylist([]) }
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
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.typing && !win.welcoming; onActivated: eq.visible ? eq.close() : eq.open() }
    Shortcut { sequence: "Z"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: win.cycleSleep() }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.visualStyle = (player.visualStyle + 1) % 12 }
    Shortcut { sequence: "Shift+V"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.visualStyle = (player.visualStyle + 11) % 12 }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: win.free; onActivated: player.calm = !player.calm }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; onActivated: library.rescan() }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; enabled: !win.welcoming; onActivated: win.setupOpen = true }
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
                    textFormat: Text.PlainText
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
                    playlistId: win.playlistId
                    onPicked: v => win.show(v)
                    onPickedPlaylist: id => win.openPlaylist(id)
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
                        onChooseFolder: win.chooseMusicFolder()
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
                                    textFormat: Text.PlainText
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
                                textFormat: Text.PlainText
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
                                AccentButton {
                                    text: qsTr("Play")
                                    visible: win.view === 6 && tracks.count > 0
                                    implicitHeight: Style.target
                                    onClicked: player.playPaths(tracks.paths(), 0)
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
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
                            "labels": qsTr("Video and upload labels in titles"),
                            "numbered": qsTr("Track numbers or the artist in titles"),
                            "notitle": qsTr("Missing titles"),
                            "noartist": qsTr("Missing artists"),
                            "noalbum": qsTr("Missing albums"),
                            "nonumber": qsTr("Missing track numbers"),
                            "spelling": qsTr("One artist spelled two ways"),
                            "album": qsTr("Album differs within a folder"),
                            "albumartist": qsTr("Album artist differs within an album"),
                            "case": qsTr("All capitals or all small letters"),
                            "duplicate": qsTr("The same song more than once")
                        })
                        readonly property var fields: ({
                            "title": qsTr("Title"),
                            "artist": qsTr("Artist"),
                            "album": qsTr("Album"),
                            "albumartist": qsTr("Album artist"),
                            "comment": qsTr("Comment"),
                            "tracknumber": qsTr("Track number")
                        })
                        // Where a proposal that isn't certain comes from.
                        readonly property var reasons: ({
                            "tag": qsTr("from the file's other tags"),
                            "filename": qsTr("from the file's name"),
                            "title": qsTr("from the title"),
                            "folder-songs": qsTr("from the other songs in the folder"),
                            "folder-name": qsTr("from the folder's name"),
                            "library": qsTr("how most of your songs spell it"),
                            "case": qsTr("ordinary capitals")
                        })
                        // What a typed answer is asked as, by kind.
                        readonly property var questions: ({
                            "noartist": qsTr("Artist for these songs"),
                            "noalbum": qsTr("Album for these songs"),
                            "album": qsTr("Album for these songs"),
                            "albumartist": qsTr("Album artist for these songs")
                        })
                        function folderName(folder) {
                            const parts = folder.split("/")
                            return parts[parts.length - 1]
                        }
                        // "Folder/file.mp3": enough to tell copies apart.
                        function shortPath(path) {
                            const parts = path.split("/")
                            return parts.slice(-2).join("/")
                        }
                        readonly property var counts: doctor.counts
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
                                textFormat: Text.PlainText
                                Layout.leftMargin: 24
                                Layout.rightMargin: 24
                                Layout.bottomMargin: 12
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("Checks your songs' tags on this computer and proposes fixes from the tags, file names and folders. Fix all writes only the fixes that are certain; the rest are for you to check, one song or one folder at a time, or to answer yourself. Nothing is written until you say so, and U undoes the last thing you did. Duplicates are only pointed out. A coding agent can answer what is left: run halo skill install once, then ask it to fix your music library.")
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
                                // A fix gives the list a new model, which would
                                // jump it back to the top: stay where the person was.
                                function act(run) {
                                    const y = contentY
                                    run()
                                    Qt.callLater(() => {
                                        contentY = Math.max(originY, Math.min(y, originY + contentHeight - height))
                                    })
                                }
                                section.property: "kind"
                                section.delegate: RowLayout {
                                    required property string section
                                    width: findingsList.width
                                    height: Style.target + 16
                                    spacing: 10
                                    Text {
                                        textFormat: Text.PlainText
                                        Layout.leftMargin: 24
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        text: (doctorPage.labels[section] || section) + "  ·  " + (doctorPage.counts[section] || 0)
                                        color: Style.text
                                        font.family: Style.mono
                                        font.pixelSize: Style.fontBody
                                        font.capitalization: Font.AllUppercase
                                        font.letterSpacing: 1
                                    }
                                    Button {
                                        Layout.rightMargin: 24
                                        readonly property int sure: doctorPage.counts[section + "/sure"] || 0
                                        visible: sure > 0
                                        text: qsTr("Fix all %1 certain").arg(sure)
                                        flat: true
                                        Material.foreground: Style.accentText
                                        implicitHeight: Style.target
                                        onClicked: findingsList.act(() => doctor.fixAll(section))
                                    }
                                }
                                delegate: ColumnLayout {
                                    id: finding
                                    required property var modelData
                                    required property int index
                                    width: findingsList.width
                                    spacing: 0
                                    // The folder's heading, on its first song: fix
                                    // the folder's proposals together, or answer.
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Layout.topMargin: 8
                                        spacing: 10
                                        visible: finding.modelData.groupFirst
                                                 && (finding.modelData.groupSize > 1 || finding.modelData.answerable)
                                        Text {
                                            Layout.leftMargin: 24
                                            Layout.fillWidth: true
                                            textFormat: Text.PlainText
                                            elide: Text.ElideMiddle
                                            text: doctorPage.folderName(finding.modelData.folder) + "  ·  " + finding.modelData.groupSize
                                            color: Style.text
                                            font.family: Style.mono
                                            font.pixelSize: Style.fontSmall
                                            font.weight: Font.Bold
                                        }
                                        Button {
                                            visible: finding.modelData.answerable
                                            Layout.rightMargin: finding.modelData.groupFixes > 1 ? 0 : 24
                                            text: qsTr("Type the answer")
                                            flat: true
                                            Material.foreground: Style.accentText
                                            implicitHeight: Style.target
                                            Accessible.description: doctorPage.folderName(finding.modelData.folder)
                                            onClicked: {
                                                const kind = finding.modelData.kind, folder = finding.modelData.folder
                                                namePrompt.ask(doctorPage.questions[kind] + " (" + finding.modelData.groupSize + ")",
                                                               qsTr("Write"), finding.modelData.after,
                                                               value => findingsList.act(() => doctor.setGroup(kind, folder, value)))
                                            }
                                        }
                                        Button {
                                            Layout.rightMargin: 24
                                            visible: finding.modelData.groupFixes > 1
                                            text: qsTr("Fix these %1").arg(finding.modelData.groupFixes)
                                            flat: true
                                            Material.foreground: Style.accentText
                                            implicitHeight: Style.target
                                            Accessible.description: doctorPage.folderName(finding.modelData.folder)
                                            onClicked: findingsList.act(() => doctor.fixGroup(finding.modelData.kind, finding.modelData.folder))
                                        }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 12
                                        ColumnLayout {
                                            Layout.leftMargin: 24
                                            Layout.fillWidth: true
                                            Layout.topMargin: 6
                                            Layout.bottomMargin: 6
                                            spacing: 2
                                            Text {
                                                Layout.fillWidth: true
                                                text: finding.modelData.file
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
                                                text: finding.modelData.kind === "duplicate"
                                                      ? qsTr("Also at: %1").arg(finding.modelData.others.map(doctorPage.shortPath).join(", "))
                                                      : finding.modelData.field.length === 0
                                                        ? qsTr("Nothing on this computer says what it should be.")
                                                        : (doctorPage.fields[finding.modelData.field] || finding.modelData.field) + ":  " + (finding.modelData.before.length ? finding.modelData.before : qsTr("(empty)"))
                                                          + "  →  " + (finding.modelData.after.length ? finding.modelData.after : qsTr("(empty)"))
                                            }
                                            Text {
                                                Layout.fillWidth: true
                                                visible: finding.modelData.fixable && !finding.modelData.sure
                                                textFormat: Text.PlainText
                                                wrapMode: Text.Wrap
                                                color: Style.muted
                                                font.pixelSize: Style.fontSmall
                                                text: qsTr("To check: %1.").arg(doctorPage.reasons[finding.modelData.why] || finding.modelData.why)
                                            }
                                        }
                                        Button {
                                            Layout.rightMargin: 24
                                            visible: finding.modelData.fixable
                                            text: qsTr("Fix")
                                            flat: true
                                            Material.foreground: Style.accentText
                                            implicitHeight: Style.target
                                            Accessible.description: finding.modelData.file
                                            onClicked: findingsList.act(() => doctor.fix(finding.index))
                                        }
                                    }
                                }
                                ScrollBar.vertical: PixelScrollBar {}
                            }
                        }
                    }
                    PlaylistPage {
                        focus: win.view === 8
                        playlistId: win.playlistId
                        onOpenAlbum: id => win.openAlbum(id)
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
            Connections {
                target: PlaylistUi
                function onToast(text) { toast.show(text) }
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

        Welcome {
            id: welcome
            anchors.fill: parent
            firstRun: !player.onboarded
            opacity: win.welcoming ? 1 : 0
            visible: opacity > 0
            enabled: win.welcoming
            Behavior on opacity {
                NumberAnimation { duration: Style.motion ? Style.smooth : 0; easing.type: Easing.OutCubic }
            }
            // Fresh choices and counts each time it opens.
            property bool wasOpen: false
            readonly property bool open: win.welcoming
            onOpenChanged: if (open && !wasOpen) { wasOpen = true; begin() } else if (!open) wasOpen = false
            Component.onCompleted: if (open) { wasOpen = true; begin() }
            onPickFolder: folderPicker.open()
            onFinished: win.closeSetup()
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

    // ---- playlists ----
    NamePrompt { id: namePrompt }
    SmartRulesDialog {
        id: smartRules
        onSaved: id => win.openPlaylist(id)
    }
    Connections {
        target: PlaylistUi
        function onNewPlaylist(paths) { win.newPlaylist(paths) }
        function onNewSmart() { smartRules.openFor("") }
        function onEditRules(id) { smartRules.openFor(id) }
        function onOpen(id) { win.openPlaylist(id) }
        function onRename(id) {
            const info = playlists.info(id)
            namePrompt.ask(qsTr("Rename playlist"), qsTr("Rename"), info.name, name => playlists.rename(id, name))
        }
        function onDeleted(id, name) {
            undoDelete.name = name
            undoDelete.shown = true
            undoTimer.restart()
            if (win.playlistId === id)
                win.showPlaylists()
        }
    }
    // A deleted playlist can come back for a few seconds (and later with
    // `halo playlist restore`): its file waits in the folder's .trash.
    Rectangle {
        id: undoDelete
        property string name
        property bool shown: false
        z: 50
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 120
        width: undoRow.implicitWidth + 24
        height: 52
        radius: Style.radius
        color: Style.raised
        border.color: Style.line
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Style.smooth; easing.type: Easing.OutCubic } }
        Timer { id: undoTimer; interval: 8000; onTriggered: undoDelete.shown = false }
        RowLayout {
            id: undoRow
            anchors.centerIn: parent
            spacing: 12
            Text { textFormat: Text.PlainText; text: qsTr("Deleted “%1”").arg(undoDelete.name); color: Style.text; font.pixelSize: Style.fontBody }
            Button {
                text: qsTr("Undo")
                flat: true
                Material.foreground: Style.accentText
                onClicked: {
                    undoDelete.shown = false
                    const id = playlists.restoreLast()
                    if (id.length)
                        win.openPlaylist(id)
                }
            }
        }
    }

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

    Component.onCompleted: {
        // Every tooltip is drawn by one shared item. Some name an album
        // ("Play …"), so it shows text as written, never as markup.
        win.contentItem.ToolTip.toolTip.contentItem.textFormat = Text.PlainText
        Qt.callLater(() => pages.currentItem.forceActiveFocus())
    }
}
