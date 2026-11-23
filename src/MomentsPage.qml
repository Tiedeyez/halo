import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The seconds you pinned with M: a drop, a verse, a song inside a mixtape.
FocusScope {
    id: root

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ScrambleText {
            Layout.margins: 24
            Layout.bottomMargin: 4
            target: qsTr("Moments")
            color: Style.text
            font.pixelSize: Style.fontTitle
            font.weight: Font.Bold
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 12
            text: qsTr("Press M while a song plays to pin that second. Moments show as ticks on the seek bar; [ and ] jump between them.")
            color: Style.faint
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }

        ListView {
            id: view
            SmoothWheel { flickable: view }
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: player.moments
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: PixelScrollBar {}
            highlightMoveDuration: 0
            Keys.onReturnPressed: player.playMoment(currentIndex)
            Keys.onEnterPressed: player.playMoment(currentIndex)
            Keys.onDeletePressed: player.removeMoment(currentIndex)

            delegate: Item {
                id: row
                required property int index
                required property string title
                required property string artist
                required property string albumId
                required property real seconds
                required property var created
                width: ListView.view.width
                height: 64
                readonly property bool selected: view.currentIndex === index

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    radius: Style.small
                    color: row.selected && view.activeFocus ? Style.raised : hover.hovered ? Style.hover : "transparent"
                    Behavior on color { ColorAnimation { duration: Style.quick; easing.type: Easing.OutCubic } }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 28
                    anchors.rightMargin: 20
                    spacing: 14
                    CoverArt {
                        Layout.preferredWidth: 44
                        Layout.preferredHeight: 44
                        albumId: row.albumId
                        title: row.title
                        size: 256
                    }
                    Rectangle {
                        Layout.preferredWidth: stamp.implicitWidth + 16
                        Layout.preferredHeight: 28
                        radius: Style.small
                        color: Style.accentWash
                        Text {
                            textFormat: Text.PlainText
                            id: stamp
                            anchors.centerIn: parent
                            text: Style.clock(row.seconds)
                            color: Style.accentText
                            font.pixelSize: Style.fontBody
                            font.weight: Font.DemiBold
                            font.features: { "tnum": 1 }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: row.title
                            color: Style.text
                            font.pixelSize: Style.fontBody
                            elide: Text.ElideRight
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: Style.artistName(row.artist)
                            color: Style.muted
                            font.pixelSize: Style.fontSmall + 1
                            elide: Text.ElideRight
                        }
                    }
                    Text {
                        textFormat: Text.PlainText
                        text: row.created ? row.created.toLocaleDateString(Qt.locale(translator.language), Locale.ShortFormat) : ""
                        color: Style.faint
                        font.pixelSize: Style.fontSmall
                    }
                    IconButton {
                        glyph: "delete"
                        tip: qsTr("Forget this moment (Delete)")
                        glyphSize: 18
                        opacity: hover.hovered || row.selected ? 1 : 0
                        onClicked: player.removeMoment(row.index)
                    }
                }
                HoverHandler { id: hover }
                TapHandler {
                    onTapped: { view.currentIndex = row.index; view.forceActiveFocus() }
                    onDoubleTapped: player.playMoment(row.index)
                }
                Accessible.role: Accessible.ListItem
                Accessible.name: row.title + ", " + Style.clock(row.seconds)
            }

            EmptyState {
                anchors.centerIn: parent
                visible: view.count === 0
                headline: qsTr("No moments yet")
                body: qsTr("Press M while a song plays to pin that second.")
            }
        }
    }
}
