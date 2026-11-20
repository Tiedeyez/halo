import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// Every shortcut on one sheet, opened and closed with "?".
Rectangle {
    id: root
    color: "#e00a0a0d"
    signal closed()

    readonly property var shortcuts: [
        ["Space", qsTr("Play or pause")],
        ["N  P", qsTr("Next or previous song")],
        ["Shift+← →", qsTr("Back or forward 10 seconds")],
        ["-  =", qsTr("Volume down or up")],
        ["Enter", qsTr("Play the selected song or album")],
        ["Shift+Enter", qsTr("Play the selected song next")],
        ["A", qsTr("Add the selected song to the queue")],
        ["/  Ctrl+F", qsTr("Search")],
        ["1 2 3 4 5 6 7", qsTr("Albums, Songs, Queue, Moments, Most played, Lost & Found, Library Doctor")],
        ["8  Ctrl+N", qsTr("Playlists, new playlist")],
        ["Alt+↑ ↓  Delete", qsTr("In a playlist: move or remove the selected song")],
        ["F", qsTr("Now Playing, full window")],
        ["L", qsTr("Lyrics, live with the song")],
        [",  .", qsTr("Lyrics earlier or later (Shift: by half a second)")],
        ["M", qsTr("Save a moment at this second")],
        ["[  ]", qsTr("Jump to the previous or next moment")],
        ["B", qsTr("Blend: mix songs into each other")],
        ["D", qsTr("Drift: keep playing similar music")],
        ["S  R", qsTr("Shuffle, repeat")],
        ["E", qsTr("Equalizer")],
        ["Z", qsTr("Sleep timer")],
        ["V  ⇧V  C", qsTr("Next or previous visual (10), calm the motion")],
        ["Ctrl+Z  Ctrl+Shift+Z", qsTr("Undo and redo queue changes")],
        ["Ctrl+R", qsTr("Look for new music")],
        ["?", qsTr("Show or hide this sheet")],
        ["Q", qsTr("Quit")]
    ]

    TapHandler { onTapped: root.closed() }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 64, 760)
        height: Math.min(parent.height - 64, sheet.implicitHeight + 48)
        radius: Math.min(10, Style.radius)
        color: Style.raised
        border.color: Style.line
        PixelBrackets { shown: true; arm: 16; thickness: 3 }

        Flickable {
            id: sheetScroll
            SmoothWheel { flickable: sheetScroll }
            anchors.fill: parent
            anchors.margins: 24
            contentHeight: sheet.implicitHeight
            clip: true

            ColumnLayout {
                id: sheet
                width: parent.width
                spacing: 6

                ScrambleText {
                    target: qsTr("Shortcuts")
                    color: Style.text
                    font.pixelSize: Style.fontTitle - 2
                    font.weight: Font.Bold
                    Layout.bottomMargin: 8
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: root.width > 900 ? 2 : 1
                    columnSpacing: 32
                    rowSpacing: 6
                    Repeater {
                        model: root.shortcuts
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 14
                            Text {
                                Layout.preferredWidth: 150
                                text: modelData[0]
                                color: Style.accentText
                                font.pixelSize: Style.fontBody - 1
                                font.family: "monospace"
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData[1]
                                color: Style.text
                                font.pixelSize: Style.fontBody - 1
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Text {
                    Layout.topMargin: 12
                    Layout.fillWidth: true
                    text: qsTr("Media keys and every MPRIS bar widget control Halo too. From a terminal: halo --help.")
                    color: Style.muted
                    font.pixelSize: Style.fontSmall + 1
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
