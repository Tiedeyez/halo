import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// What plays, in order. Drag a song to move it; Delete removes the
// selected song; Ctrl+Z brings back anything removed.
FocusScope {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.bottomMargin: 12
            ScrambleText {
                target: qsTr("Queue")
                color: Style.text
                font.pixelSize: Style.fontTitle
                font.weight: Font.Bold
            }
            Text {
                Layout.leftMargin: 8
                text: qsTr("%n songs", "", player.queue.count)
                color: Style.muted
                font.pixelSize: Style.fontBody
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Clear")
                flat: true
                enabled: player.queue.count > 1
                focusPolicy: Qt.NoFocus
                Material.roundedScale: Style.radius
                onClicked: player.clearQueue()
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 8
            visible: player.drift
            text: qsTr("Drift adds songs from your library when the queue runs out, close in tempo and loudness to what's playing. They are marked with a wave.")
            color: Style.faint
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }

        ListView {
            id: view
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: player.queue
            currentIndex: Math.max(0, player.currentIndex)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: PixelScrollBar {}
            highlightMoveDuration: 0
            Keys.onReturnPressed: player.playAt(currentIndex)
            Keys.onEnterPressed: player.playAt(currentIndex)
            Keys.onDeletePressed: player.removeAt(currentIndex)
            Keys.onPressed: e => {
                if (e.key === Qt.Key_Backspace) { player.removeAt(currentIndex); e.accepted = true }
                else if (e.key === Qt.Key_Up && (e.modifiers & Qt.AltModifier) && currentIndex > 0) {
                    player.move(currentIndex, currentIndex - 1); currentIndex -= 1; e.accepted = true
                } else if (e.key === Qt.Key_Down && (e.modifiers & Qt.AltModifier) && currentIndex < count - 1) {
                    player.move(currentIndex, currentIndex + 1); currentIndex += 1; e.accepted = true
                }
            }

            delegate: Item {
                id: row
                required property int index
                required property string title
                required property string artist
                required property string albumId
                required property real duration
                required property bool current
                required property bool drift
                width: ListView.view.width
                height: 60
                readonly property bool selected: view.currentIndex === index

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    radius: Style.small
                    color: row.selected && view.activeFocus ? Style.raised : hover.hovered ? Style.hover : "transparent"
                    // The song playing wears pixel brackets; hover gets a block cursor.
                    PixelBrackets { shown: row.current; arm: 10 }
                    Rectangle {
                        visible: hover.hovered && !row.current
                        width: 3
                        height: parent.height - 14
                        anchors.verticalCenter: parent.verticalCenter
                        color: Style.muted
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 28
                    anchors.rightMargin: 20
                    spacing: 14
                    Item {
                        Layout.preferredWidth: 28
                        Layout.fillHeight: true
                        visible: row.current
                        PixelEq { anchors.centerIn: parent }
                    }
                    Text {
                        Layout.preferredWidth: 28
                        visible: !row.current
                        horizontalAlignment: Text.AlignHCenter
                        text: row.index + 1
                        color: row.index < player.currentIndex ? Style.line : Style.faint
                        font.pixelSize: Style.fontSmall
                    }
                    CoverArt {
                        Layout.preferredWidth: 42
                        Layout.preferredHeight: 42
                        albumId: row.albumId
                        title: row.title
                        size: 256
                        opacity: row.index < player.currentIndex ? 0.5 : 1
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: row.title
                            color: row.current ? Style.accentText : row.index < player.currentIndex ? Style.faint : Style.text
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
                    Glyph {
                        visible: row.drift
                        name: "drift"
                        color: Style.accentText
                        width: 18
                        height: 18
                        ToolTip.visible: driftHover.hovered
                        ToolTip.text: qsTr("Added by Drift")
                        HoverHandler { id: driftHover }
                    }
                    Text {
                        text: row.duration > 0 ? Style.clock(row.duration) : ""
                        color: Style.muted
                        font.pixelSize: Style.fontSmall
                        font.features: { "tnum": 1 }
                    }
                    IconButton {
                        glyph: "close"
                        tip: qsTr("Remove from queue (Delete)")
                        glyphSize: 18
                        opacity: hover.hovered || row.selected ? 1 : 0
                        onClicked: player.removeAt(row.index)
                    }
                }

                HoverHandler { id: hover }
                TapHandler {
                    onTapped: { view.currentIndex = row.index; view.forceActiveFocus() }
                    onDoubleTapped: player.playAt(row.index)
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: row.title + ", " + Style.artistName(row.artist)
            }

            EmptyState {
                anchors.centerIn: parent
                visible: view.count === 0
                headline: qsTr("Nothing queued")
                body: qsTr("The queue is empty. Play an album, or select songs and press A to add them.")
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 24
            text: qsTr("Enter plays · Delete removes · Alt+Up/Down moves · Ctrl+Z undoes")
            color: Style.faint
            font.pixelSize: Style.fontSmall
        }
    }
}
