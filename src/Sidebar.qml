import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The library's sections, search, and what Halo is doing in the background.
Rectangle {
    id: root
    property int view: 0
    property alias search: searchField
    signal picked(int view)

    color: Style.panel
    implicitWidth: 248

    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Style.line }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.rightMargin: 17
        spacing: 6

        RowLayout {
            Layout.bottomMargin: 10
            spacing: 10
            Glyph { name: "halo"; color: Style.accent; width: 30; height: 30 }
            Text {
                text: "Halo"
                color: Style.text
                font.pixelSize: 22
                font.weight: Font.Bold
                font.letterSpacing: 0.5
            }
        }

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search  /")
            Material.accent: Style.accent
            Material.roundedScale: Style.radius
            selectByMouse: true
            Accessible.name: qsTr("Search the library")
            Keys.onEscapePressed: { text = ""; focus = false }
            Keys.onDownPressed: root.picked(root.view === 3 || root.view === 4 ? 0 : root.view)
        }

        Item { Layout.preferredHeight: 6 }

        Repeater {
            model: [
                { view: 0, glyph: "album", label: qsTr("Albums"), key: "1" },
                { view: 2, glyph: "song", label: qsTr("Songs"), key: "2" },
                { view: 3, glyph: "queue", label: qsTr("Queue"), key: "3" },
                { view: 4, glyph: "moment", label: qsTr("Moments"), key: "4" },
                { view: 5, glyph: "top", label: qsTr("Most played"), key: "5" }
            ]
            delegate: AbstractButton {
                id: item
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: Style.target
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                readonly property bool selected: root.view === modelData.view || (modelData.view === 0 && root.view === 1)
                Accessible.name: modelData.label
                onClicked: root.picked(modelData.view)
                background: Rectangle {
                    radius: Style.small
                    color: item.selected ? Style.raised : item.hovered ? Style.hover : "transparent"
                    Rectangle {
                        visible: item.selected
                        width: 3
                        height: parent.height - 16
                        anchors.verticalCenter: parent.verticalCenter
                        color: Style.accent
                        radius: 1
                    }
                }
                contentItem: RowLayout {
                    spacing: 12
                    Item { Layout.preferredWidth: 4 }
                    Glyph {
                        name: item.modelData.glyph
                        color: item.selected ? Style.accentText : Style.muted
                        width: 20
                        height: 20
                    }
                    Text {
                        Layout.fillWidth: true
                        text: item.modelData.label
                        color: item.selected ? Style.text : Style.muted
                        font.pixelSize: Style.fontBody
                        font.weight: item.selected ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    Text {
                        text: item.modelData.view === 3 ? String(player.queue.count || "")
                            : item.modelData.view === 4 ? String(player.moments.count || "") : ""
                        color: Style.faint
                        font.pixelSize: Style.fontSmall
                    }
                    Text {
                        text: item.modelData.key
                        color: Style.faint
                        font.pixelSize: Style.fontSmall
                        font.family: "monospace"
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // Up next, so a glance tells what Blend or Drift will do.
        ColumnLayout {
            Layout.fillWidth: true
            visible: player.nextTitle.length > 0
            spacing: 2
            Text {
                text: qsTr("Up next")
                color: Style.faint
                font.pixelSize: Style.fontSmall
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
            }
            Text {
                Layout.fillWidth: true
                text: player.nextTitle
                color: Style.text
                font.pixelSize: Style.fontBody
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: Style.artistName(player.nextArtist)
                color: Style.muted
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.topMargin: 8; height: 1; color: Style.line }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: library.scanning
                  ? qsTr("Reading your music: %1 files").arg(library.scanned.toLocaleString(Qt.locale(translator.language), 'f', 0))
                  : qsTr("%n songs", "", library.trackCount) + " · " + qsTr("%n albums", "", library.albumCount)
            color: Style.faint
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            visible: library.trackCount > 0
            text: player.analyzedCount >= library.trackCount
                  ? qsTr("Every song analysed")
                  : qsTr("Analysed %1 of %2 for Blend and Drift")
                        .arg(player.analyzedCount.toLocaleString(Qt.locale(translator.language), 'f', 0))
                        .arg(library.trackCount.toLocaleString(Qt.locale(translator.language), 'f', 0))
            color: Style.faint
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Press ? for shortcuts")
            color: Style.faint
            font.pixelSize: Style.fontSmall
        }
    }
}
