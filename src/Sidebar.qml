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

    PixelRule { anchors.right: parent.right; vertical: true; height: parent.height }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        anchors.rightMargin: 17
        spacing: 6

        PixelWordmark {
            Layout.topMargin: 4
            Layout.leftMargin: 2
            Layout.bottomMargin: 14
            cell: 5
        }

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search")
            leftPadding: 42
            rightPadding: 40
            Material.accent: Style.accent
            Material.roundedScale: Style.radius
            selectByMouse: true
            Accessible.name: qsTr("Search the library")
            // A terminal's block cursor, blinking.
            cursorDelegate: Rectangle {
                width: 8
                color: Style.accent
                opacity: 0.85
                visible: searchField.activeFocus
                SequentialAnimation on opacity {
                    running: searchField.activeFocus && Style.motion
                    loops: Animation.Infinite
                    NumberAnimation { to: 0; duration: 0 }
                    PauseAnimation { duration: 530 }
                    NumberAnimation { to: 0.85; duration: 0 }
                    PauseAnimation { duration: 530 }
                }
            }
            Keys.onEscapePressed: { text = ""; focus = false }
            Keys.onDownPressed: root.picked(root.view === 3 || root.view === 4 ? 0 : root.view)

            Glyph {
                name: "search"
                color: searchField.activeFocus ? Style.accentText : Style.faint
                width: 20
                height: 20
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
            }
            // The shortcut that focuses search, as a key, while it's idle.
            Keycap {
                label: "/"
                visible: !searchField.activeFocus && searchField.text.length === 0
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Item { Layout.preferredHeight: 6 }

        Repeater {
            model: [
                { view: 0, glyph: "album", label: qsTr("Albums"), key: "1" },
                { view: 2, glyph: "song", label: qsTr("Songs"), key: "2" },
                { view: 3, glyph: "queue", label: qsTr("Queue"), key: "3" },
                { view: 4, glyph: "moment", label: qsTr("Moments"), key: "4" },
                { view: 5, glyph: "top", label: qsTr("Most played"), key: "5" },
                { view: 6, glyph: "history", label: qsTr("Lost & Found"), key: "6" },
                { view: 7, glyph: "doctor", label: qsTr("Library Doctor"), key: "7" }
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
                    PixelBrackets { shown: item.hovered && !item.selected; arm: 6; color: Style.muted }
                    // The selected row's marker: three stacked pixels.
                    Column {
                        visible: item.selected
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Repeater {
                            model: 4
                            Rectangle { width: 4; height: 5; color: Style.accent }
                        }
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
                    // How many things are waiting: a pill, so it never reads as the shortcut.
                    Rectangle {
                        readonly property int count: item.modelData.view === 3 ? player.queue.count
                            : item.modelData.view === 4 ? player.moments.count : 0
                        visible: count > 0
                        implicitWidth: Math.max(22, countText.implicitWidth + 12)
                        implicitHeight: 20
                        radius: 10
                        color: item.selected ? Style.hover : Style.raised
                        border.color: Style.line
                        Text {
                            id: countText
                            anchors.centerIn: parent
                            text: parent.count > 999 ? "999+" : String(parent.count)
                            color: Style.text
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                            font.features: { "tnum": 1 }
                        }
                    }
                    // The number key that opens this view, shown as a key on hover.
                    Keycap {
                        label: item.modelData.key
                        opacity: item.hovered ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 120 } }
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
                font.family: Style.mono
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

        PixelRule { Layout.fillWidth: true; Layout.topMargin: 8 }

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
