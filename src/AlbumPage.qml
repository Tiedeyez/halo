import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// One album: its cover large, what it is, and its songs.
FocusScope {
    id: root
    property string albumId
    property string title
    property string artist
    property int year
    signal back()
    signal openAlbum(string albumId)

    Keys.onEscapePressed: root.back()
    Keys.onBackPressed: root.back()

    TrackList {
        id: songs
        anchors.fill: parent
        focus: true
        model: albumTracks
        showAlbum: false
        showNumber: true
        onOpenAlbum: id => root.openAlbum(id)
        header: Item {
            width: songs.width
            height: 300
            RowLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 28
                CoverArt {
                    Layout.preferredWidth: 240
                    Layout.preferredHeight: 240
                    albumId: root.albumId
                    title: root.title
                    size: 640
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    IconButton {
                        glyph: "back"
                        tip: qsTr("Back to albums (Esc)")
                        onClicked: root.back()
                        Layout.leftMargin: -12
                    }
                    ScrambleText {
                        Layout.fillWidth: true
                        target: root.title
                        color: Style.text
                        font.pixelSize: Style.fontHero
                        font.weight: Font.Bold
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: Style.artistName(root.artist)
                        color: Style.muted
                        font.pixelSize: Style.fontLarge
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: [root.year > 0 ? String(root.year) : "", qsTr("%n songs", "", albumTracks.count),
                               Style.hours(albumTracks.totalDuration)].filter(s => s.length).join(" · ")
                        color: Style.faint
                        font.pixelSize: Style.fontBody
                    }
                    RowLayout {
                        Layout.topMargin: 10
                        spacing: 10
                        Button {
                            text: qsTr("Play")
                            highlighted: true
                            Material.accent: Style.accent
                            Material.foreground: Style.onAccent
                            Material.roundedScale: Style.radius
                            focusPolicy: Qt.NoFocus
                            implicitHeight: 48
                            onClicked: player.playAlbum(root.albumId, 0)
                        }
                        Button {
                            text: qsTr("Shuffle")
                            flat: true
                            Material.roundedScale: Style.radius
                            focusPolicy: Qt.NoFocus
                            implicitHeight: 48
                            onClicked: { player.shuffle = true; player.playAlbum(root.albumId, Math.floor(Math.random() * albumTracks.count)) }
                        }
                        Button {
                            text: qsTr("Add to queue")
                            flat: true
                            Material.roundedScale: Style.radius
                            focusPolicy: Qt.NoFocus
                            implicitHeight: 48
                            onClicked: player.enqueue(albumTracks.paths())
                        }
                    }
                }
            }
        }
    }
}
