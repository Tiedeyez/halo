import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// Every album as a cover. Arrow keys move, Enter opens, Space still plays.
FocusScope {
    id: root
    signal openAlbum(string albumId)

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.bottomMargin: 12
            ScrambleText {
                target: qsTr("Albums")
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.Bold
            }
            Text {
                Layout.leftMargin: 8
                Layout.alignment: Qt.AlignBaseline
                text: albums.query.length ? qsTr("%n matching", "", albums.count) : ""
                color: Style.muted
                font.pixelSize: Style.fontBody
            }
            Item { Layout.fillWidth: true }
            ComboBox {
                Layout.preferredWidth: 200
                model: [qsTr("Recently added"), qsTr("Artist"), qsTr("Title")]
                currentIndex: albums.sort
                onActivated: i => albums.sort = i
                focusPolicy: Qt.NoFocus
                Material.roundedScale: Style.radius
                Accessible.name: qsTr("Sort albums")
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 16
            clip: true
            focus: true
            model: albums
            readonly property int columns: Math.max(2, Math.floor((width - 8) / 196))
            cellWidth: Math.floor((width - 8) / columns)
            cellHeight: cellWidth + 64
            keyNavigationWraps: false
            highlightFollowsCurrentItem: true
            currentIndex: 0
            ScrollBar.vertical: ScrollBar {}
            Keys.onReturnPressed: root.openAlbum(albums.idAt(currentIndex))
            Keys.onEnterPressed: root.openAlbum(albums.idAt(currentIndex))

            delegate: Item {
                id: cell
                required property int index
                required property string albumId
                required property string title
                required property string artist
                required property int year
                required property int tracks
                width: grid.cellWidth
                height: grid.cellHeight
                readonly property bool current: grid.currentIndex === index && grid.activeFocus
                readonly property bool playingHere: player.albumId === albumId

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 6
                    radius: Style.small
                    color: hover.hovered || cell.current ? Style.raised : "transparent"
                    border.color: cell.current ? Style.accent : "transparent"
                    border.width: 2
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 4
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: width
                        CoverArt {
                            anchors.fill: parent
                            albumId: cell.albumId
                            title: cell.title
                            size: 256
                        }
                        // Play straight from the grid.
                        IconButton {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 8
                            primary: true
                            visible: hover.hovered
                            glyph: "play"
                            tip: qsTr("Play %1").arg(cell.title)
                            onClicked: player.playAlbum(cell.albumId, 0)
                        }
                        Glyph {
                            visible: cell.playingHere && !hover.hovered
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 10
                            name: "halo"
                            color: Style.accent
                            width: 26
                            height: 26
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        text: cell.title
                        color: cell.playingHere ? Style.accentText : Style.text
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Style.artistName(cell.artist) + (cell.year > 0 ? " · " + cell.year : "")
                        color: Style.muted
                        font.pixelSize: Style.fontSmall + 1
                        elide: Text.ElideRight
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: { grid.currentIndex = cell.index; grid.forceActiveFocus(); root.openAlbum(cell.albumId) }
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: cell.title + ", " + Style.artistName(cell.artist)
            }

            EmptyState {
                anchors.centerIn: parent
                visible: grid.count === 0
                busy: library.scanning
                headline: library.scanning ? qsTr("Reading your music")
                        : albums.query.length ? qsTr("No matches") : qsTr("No music yet")
                body: library.scanning ? qsTr("%n files so far. Albums appear when the first pass is done.", "", library.scanned)
                    : albums.query.length ? qsTr("No albums match “%1”.").arg(albums.query)
                    : qsTr("No music found in %1. Put music there, list more folders in ~/.config/halo/folders, or drop files on this window.")
                          .arg(library.folders.join(", "))
            }
        }
    }
}
