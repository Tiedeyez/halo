import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// A list of songs. Enter plays from the selected song on, A adds it to the
// queue, Shift+Enter plays it next; right-click for the same.
FocusScope {
    id: root
    property var model
    property bool showAlbum: true
    property bool showNumber: false
    property bool showPlays: false
    property alias list: view
    property Component header: null
    signal openAlbum(string albumId)

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
                        Layout.fillWidth: true
                        text: row.title
                        color: row.current ? Style.accentText : Style.text
                        font.pixelSize: Style.fontBody
                        font.weight: row.current ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Style.artistName(row.artist)
                        color: Style.muted
                        font.pixelSize: Style.fontSmall + 1
                        elide: Text.ElideRight
                    }
                }
                Text {
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
                    visible: root.showPlays
                    text: qsTr("%n plays", "", row.plays)
                    color: Style.muted
                    font.pixelSize: Style.fontSmall
                }
                Text {
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
                onTapped: { view.currentIndex = row.index; menu.path = row.path; menu.albumId = row.albumId; menu.row = row.index; menu.popup() }
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
        MenuItem { text: qsTr("Go to album"); onTriggered: root.openAlbum(menu.albumId) }
    }
}
