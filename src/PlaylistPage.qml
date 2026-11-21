import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtCore
import Halo

// One playlist: its face, play / shuffle / queue, the songs, and for a
// playlist you build by hand, songs Halo thinks would fit (by tempo,
// loudness, artists and genres, all worked out on this computer).
FocusScope {
    id: root
    property string playlistId
    // A snapshot, read again whenever the playlist or the library changes.
    property int revision: 0
    readonly property var info: (revision, playlistId.length ? playlists.info(playlistId) : ({}))
    readonly property bool exists: info.id !== undefined
    readonly property bool smart: info.smart === true
    signal openAlbum(string albumId)

    Connections {
        target: playlists
        function onContentsChanged(id) { if (id === root.playlistId) root.revision++ }
        function onChanged() { root.revision++ }
    }
    Connections {
        target: library
        function onChanged() { root.revision++ }
    }

    Binding { target: playlistTracks; property: "playlistId"; value: root.playlistId; when: root.visible }

    property var suggestions: []
    property bool suggesting: false
    function suggest() {
        suggestions = playlists.describe(playlists.suggest(root.playlistId, 8))
        suggesting = true
    }
    // The very top, header included (positionViewAtBeginning stops at the first song).
    function toTop() { list.list.contentY = list.list.originY }
    onPlaylistIdChanged: { suggesting = false; suggestions = []; Qt.callLater(root.toTop) }
    onVisibleChanged: if (visible) Qt.callLater(root.toTop)

    function exportToMusic() {
        const music = StandardPaths.writableLocation(StandardPaths.MusicLocation).toString().replace("file://", "")
        const file = music + "/Playlists/" + root.playlistId + ".m3u8"
        if (playlists.exportFile(root.playlistId, file))
            PlaylistUi.toast(qsTr("Saved a copy to %1").arg(file.replace(StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", ""), "~")))
        else
            PlaylistUi.toast(qsTr("Couldn't write %1").arg(file))
    }

    Shortcut { sequence: "Ctrl+Return"; enabled: root.visible && root.exists; onActivated: player.playPaths(playlistTracks.paths(), 0) }

    EmptyState {
        anchors.fill: parent
        visible: !root.exists
        headline: playlists.count === 0 ? qsTr("No playlists yet") : qsTr("Pick a playlist")
        body: playlists.count === 0
              ? qsTr("Make one with the + next to Playlists in the sidebar, right-click any song and choose Add to playlist, or save the queue as a playlist. Smart playlists fill themselves from rules.")
              : qsTr("Your playlists are in the sidebar.")
    }

    TrackList {
        id: list
        anchors.fill: parent
        visible: root.exists
        focus: true
        model: root.visible ? playlistTracks : null
        playlistId: root.playlistId
        editable: root.exists && !root.smart
        onOpenAlbum: id => root.openAlbum(id)
        header: Item {
            width: list.width
            height: head.implicitHeight + 24
            // The header grows once its details load; stay at the top if we were there.
            property real lastHeight: 0
            onHeightChanged: {
                if (list.list.contentY <= list.list.originY + lastHeight * 0.1 + 1 || lastHeight === 0)
                    Qt.callLater(root.toTop)
                lastHeight = height
            }
            Column {
                id: head
                x: 24
                y: 24
                width: parent.width - 48
                spacing: 16

                RowLayout {
                    width: head.width
                    spacing: 24
                    PlaylistCover {
                        Layout.preferredWidth: 176
                        Layout.preferredHeight: 176
                        covers: root.info.covers || []
                        smart: root.smart
                        corner: Style.radius
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            text: root.smart ? qsTr("Smart playlist") : qsTr("Playlist")
                            color: Style.faint
                            font.pixelSize: Style.fontSmall
                            font.family: Style.mono
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1
                        }
                        ScrambleText {
                            Layout.fillWidth: true
                            target: root.info.name || ""
                            color: Style.text
                            font.pixelSize: Style.fontHero
                            font.weight: Font.Bold
                            elide: Text.ElideRight
                            TapHandler { onDoubleTapped: PlaylistUi.rename(root.playlistId) }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: root.info.description || ""
                            color: Style.muted
                            font.pixelSize: Style.fontBody
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                        Text {
                            text: [qsTr("%n songs", "", root.info.count || 0),
                                   (root.info.duration || 0) > 0 ? Style.hours(root.info.duration) : "",
                                   (root.info.missing || 0) > 0 ? qsTr("%n missing", "", root.info.missing) : ""]
                                  .filter(s => s.length).join(" · ")
                            color: Style.faint
                            font.pixelSize: Style.fontBody
                        }
                        RowLayout {
                            Layout.topMargin: 10
                            spacing: 10
                            AccentButton {
                                text: qsTr("Play")
                                enabled: playlistTracks.count > 0
                                focusPolicy: Qt.NoFocus
                                implicitHeight: 48
                                onClicked: { player.shuffle = false; player.playPaths(playlistTracks.paths(), 0) }
                            }
                            Button {
                                text: qsTr("Shuffle")
                                flat: true
                                enabled: playlistTracks.count > 0
                                Material.roundedScale: Style.radius
                                focusPolicy: Qt.NoFocus
                                implicitHeight: 48
                                onClicked: { player.shuffle = true; player.playPaths(playlistTracks.paths(), Math.floor(Math.random() * playlistTracks.count)) }
                            }
                            Button {
                                text: qsTr("Add to queue")
                                flat: true
                                enabled: playlistTracks.count > 0
                                Material.roundedScale: Style.radius
                                focusPolicy: Qt.NoFocus
                                implicitHeight: 48
                                onClicked: { player.enqueue(playlistTracks.paths()); PlaylistUi.toast(qsTr("Added %n songs to the queue", "", playlistTracks.count)) }
                            }
                            Button {
                                text: root.smart ? qsTr("Edit rules") : qsTr("Suggest songs")
                                flat: true
                                visible: root.smart || playlistTracks.count > 0
                                Material.foreground: Style.accentText
                                Material.roundedScale: Style.radius
                                focusPolicy: Qt.NoFocus
                                implicitHeight: 48
                                onClicked: root.smart ? PlaylistUi.editRules(root.playlistId) : root.suggest()
                            }
                            IconButton {
                                glyph: "more"
                                tip: qsTr("More")
                                onClicked: more.popup()
                                Menu {
                                    id: more
                                    Material.roundedScale: Style.radius
                                    MenuItem { text: qsTr("Rename…"); onTriggered: PlaylistUi.rename(root.playlistId) }
                                    MenuItem {
                                        text: root.info.pinned ? qsTr("Unpin") : qsTr("Pin to the top")
                                        onTriggered: playlists.setPinned(root.playlistId, !root.info.pinned)
                                    }
                                    MenuItem {
                                        text: qsTr("Duplicate")
                                        onTriggered: { const id = playlists.duplicate(root.playlistId); if (id.length) PlaylistUi.open(id) }
                                    }
                                    MenuItem {
                                        text: qsTr("Save the queue into it")
                                        visible: !root.smart
                                        height: visible ? implicitHeight : 0
                                        enabled: player.queue.count > 0
                                        onTriggered: PlaylistUi.add(root.playlistId, root.info.name, player.queueList())
                                    }
                                    MenuItem { text: qsTr("Save a copy to Music/Playlists"); onTriggered: root.exportToMusic() }
                                    MenuItem {
                                        text: qsTr("Show the file")
                                        onTriggered: Qt.openUrlExternally("file://" + playlists.folder)
                                    }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: qsTr("Delete")
                                        Material.foreground: Style.danger
                                        onTriggered: {
                                            const name = root.info.name
                                            const id = root.playlistId
                                            if (playlists.remove(id))
                                                PlaylistUi.deleted(id, name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Suggestions: Drift's ear, offered, never added on its own.
                ColumnLayout {
                    width: head.width
                    visible: root.suggesting && !root.smart
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: root.suggestions.length
                                  ? qsTr("Would fit: close in tempo and loudness, or by artists and genres already here")
                                  : qsTr("Nothing to suggest yet: Halo needs your library analysed, or more songs here.")
                            color: Style.faint
                            font.pixelSize: Style.fontSmall
                            wrapMode: Text.Wrap
                        }
                        Button {
                            text: qsTr("Add all")
                            flat: true
                            visible: root.suggestions.length > 0
                            Material.foreground: Style.accentText
                            focusPolicy: Qt.NoFocus
                            onClicked: {
                                PlaylistUi.add(root.playlistId, root.info.name, root.suggestions.map(s => s.path))
                                root.suggest()
                            }
                        }
                        Button { text: qsTr("More"); flat: true; focusPolicy: Qt.NoFocus; visible: root.suggestions.length > 0; onClicked: root.suggest() }
                        IconButton { glyph: "close"; tip: qsTr("Hide suggestions"); onClicked: root.suggesting = false }
                    }
                    Repeater {
                        model: root.suggestions
                        delegate: Rectangle {
                            id: s
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: 48
                            radius: Style.small
                            color: sh.hovered ? Style.hover : "transparent"
                            HoverHandler { id: sh }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                spacing: 12
                                CoverArt { Layout.preferredWidth: 36; Layout.preferredHeight: 36; albumId: s.modelData.albumId; size: 72 }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { Layout.fillWidth: true; text: s.modelData.title; color: Style.text; font.pixelSize: Style.fontBody; elide: Text.ElideRight }
                                    Text { Layout.fillWidth: true; text: Style.artistName(s.modelData.artist); color: Style.muted; font.pixelSize: Style.fontSmall; elide: Text.ElideRight }
                                }
                                IconButton {
                                    glyph: "play"
                                    tip: qsTr("Play next")
                                    onClicked: player.playNext([s.modelData.path])
                                }
                                IconButton {
                                    glyph: "add"
                                    tip: qsTr("Add to this playlist")
                                    onClicked: {
                                        PlaylistUi.add(root.playlistId, root.info.name, [s.modelData.path])
                                        root.suggestions = root.suggestions.filter(x => x.path !== s.modelData.path)
                                    }
                                }
                            }
                        }
                    }
                    PixelRule { Layout.fillWidth: true; Layout.topMargin: 8 }
                }

                Text {
                    visible: root.smart
                    width: head.width
                    text: qsTr("Fills itself from your library and keeps up as it changes. Edit the rules to change what's in it.")
                    color: Style.faint
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
                EmptyState {
                    width: head.width
                    height: 200
                    visible: root.exists && playlistTracks.count === 0
                    headline: root.smart ? qsTr("Nothing matches yet") : qsTr("An empty playlist")
                    body: root.smart ? qsTr("No song in your library matches these rules. Edit the rules to widen them.")
                                     : qsTr("Right-click any song or album and choose Add to playlist, or press + on the song playing.")
                }
            }
        }
    }
}
