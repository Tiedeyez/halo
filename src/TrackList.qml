import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// A list of songs. Enter plays from the selected song on, A adds it to the
// queue, Shift+Enter plays it next; right-click for the same. In a playlist
// you can edit, Delete removes the selected song and Alt+Up/Down moves it.
FocusScope {
    id: root
    property var model
    property bool showAlbum: true
    property bool showNumber: false
    property bool showPlays: false
    property alias list: view
    property Component header: null
    // Set on a playlist's page; `editable` for manual playlists.
    property string playlistId: ""
    property bool editable: false
    signal openAlbum(string albumId)

    function removeSelected() {
        if (!root.editable || view.currentIndex < 0)
            return
        const row = view.currentIndex
        playlists.removeRows(root.playlistId, [row])
        view.currentIndex = Math.min(row, view.count - 1)
    }
    function moveSelected(by) {
        const from = view.currentIndex
        const to = from + by
        if (!root.editable || from < 0 || to < 0 || to >= view.count)
            return
        if (playlists.move(root.playlistId, from, to))
            view.currentIndex = to
    }

    function playFrom(row) {
        player.playPaths(model.paths(), row)
    }

    function selectedPath() { return model.pathAt(view.currentIndex) }

    Shortcut {
        sequence: "A"
        enabled: view.activeFocus
        onActivated: player.enqueue([root.selectedPath()])
    }
    Shortcut {
        sequences: ["Shift+Return", "Shift+Enter"]
        enabled: view.activeFocus
        onActivated: player.playNext([root.selectedPath()])
    }

    ListView {
        id: view
        SmoothWheel { flickable: view }
        anchors.fill: parent
        clip: true
        focus: true
        model: root.model
        currentIndex: 0
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: PixelScrollBar {}
        header: root.header
        keyNavigationEnabled: true
        highlightMoveDuration: 0
        Keys.onReturnPressed: root.playFrom(currentIndex)
        Keys.onEnterPressed: root.playFrom(currentIndex)
        Keys.onDeletePressed: root.removeSelected()
        Keys.onPressed: e => {
            if (!root.editable)
                return
            if (e.key === Qt.Key_Backspace) { root.removeSelected(); e.accepted = true }
            else if (e.key === Qt.Key_Up && (e.modifiers & Qt.AltModifier)) { root.moveSelected(-1); e.accepted = true }
            else if (e.key === Qt.Key_Down && (e.modifiers & Qt.AltModifier)) { root.moveSelected(1); e.accepted = true }
        }

        delegate: Item {
            id: row
            required property int index
            required property string path
            required property string title
            required property string artist
            required property string album
            required property string albumId
            required property real duration
            required property int number
            required property int plays
            // Only playlists have this role: a song whose file is gone.
            required property var model
            readonly property bool missing: model.missing === true
            opacity: missing ? 0.45 : 1
            width: ListView.view.width
            height: 52
            readonly property bool current: player.currentPath === path
            readonly property bool selected: view.currentIndex === index

            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                radius: Style.small
                color: row.selected && view.activeFocus ? Style.raised : hover.hovered ? Style.hover : "transparent"
                Behavior on color { ColorAnimation { duration: Style.quick; easing.type: Easing.OutCubic } }
                border.color: row.selected && view.activeFocus ? Style.line : "transparent"
                // A block cursor at the row's edge, on hover and on the song playing.
                Rectangle {
                    visible: hover.hovered || row.current
                    width: 3
                    height: parent.height - 12
                    anchors.verticalCenter: parent.verticalCenter
                    color: row.current ? Style.accent : Style.muted
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 28
                spacing: 16

                Item {
                    Layout.preferredWidth: 28
                    Layout.fillHeight: true
                    // The song playing: a live pixel meter.
                    PixelEq {
                        anchors.centerIn: parent
                        visible: row.current
                    }
                    Text {
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        visible: !row.current
                        text: root.showNumber ? (row.number > 0 ? row.number : "") : row.index + 1
                        color: Style.faint
                        font.pixelSize: Style.fontSmall
                        font.features: { "tnum": 1 }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 4
                    spacing: 0
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: row.title
                        color: row.current ? Style.accentText : Style.text
                        font.pixelSize: Style.fontBody
                        font.weight: row.current ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: row.missing ? qsTr("Missing: the file was moved or deleted") : Style.artistName(row.artist)
                        color: Style.muted
                        font.pixelSize: Style.fontSmall + 1
                        elide: Text.ElideRight
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    visible: root.showAlbum && root.width > 760
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    text: row.album
                    color: Style.muted
                    font.pixelSize: Style.fontSmall + 1
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openAlbum(row.albumId)
                    }
                }
                Text {
                    textFormat: Text.PlainText
                    visible: root.showPlays
                    text: qsTr("%n plays", "", row.plays)
                    color: Style.muted
                    font.pixelSize: Style.fontSmall
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.preferredWidth: 52
                    horizontalAlignment: Text.AlignRight
                    text: row.duration > 0 ? Style.clock(row.duration) : ""
                    color: Style.muted
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                }
            }

            HoverHandler { id: hover }
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: { view.currentIndex = row.index; view.forceActiveFocus() }
                onDoubleTapped: root.playFrom(row.index)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: { view.currentIndex = row.index; menu.path = row.path; menu.albumId = row.albumId; menu.row = row.index; addMenu.paths = [row.path]; menu.popup() }
            }
            Accessible.role: Accessible.ListItem
            Accessible.name: row.title + ", " + Style.artistName(row.artist)
        }
    }

    Menu {
        id: menu
        property string path
        property string albumId
        property int row
        Material.roundedScale: Style.radius
        MenuItem { text: qsTr("Play from here"); onTriggered: root.playFrom(menu.row) }
        MenuItem { text: qsTr("Play next"); onTriggered: player.playNext([menu.path]) }
        MenuItem { text: qsTr("Add to queue"); onTriggered: player.enqueue([menu.path]) }
        AddToPlaylistMenu { id: addMenu }
        MenuItem { text: qsTr("Go to album"); enabled: menu.albumId.length > 0; onTriggered: root.openAlbum(menu.albumId) }
        MenuSeparator { visible: root.editable }
        MenuItem {
            text: qsTr("Move up (Alt+Up)")
            visible: root.editable
            height: visible ? implicitHeight : 0
            enabled: menu.row > 0
            onTriggered: { view.currentIndex = menu.row; root.moveSelected(-1) }
        }
        MenuItem {
            text: qsTr("Move down (Alt+Down)")
            visible: root.editable
            height: visible ? implicitHeight : 0
            enabled: menu.row < view.count - 1
            onTriggered: { view.currentIndex = menu.row; root.moveSelected(1) }
        }
        MenuItem {
            text: qsTr("Remove from this playlist (Delete)")
            visible: root.editable
            height: visible ? implicitHeight : 0
            onTriggered: { view.currentIndex = menu.row; root.removeSelected() }
        }
    }
}
