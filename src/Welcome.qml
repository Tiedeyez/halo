import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The first run, when Halo found no music on its own: what Halo is, where
// to point it, and the three things it does that a stranger wouldn't
// guess. Shown once (player.onboarded), and never with a library.
Rectangle {
    id: root
    // Set after a folder was chosen, so the screen can say how it went.
    property string chosen: ""
    property bool rejected: false
    signal chooseFolder()
    signal dismissed()

    // Solid: see-through, the empty library behind it (with its own
    // "Choose your music folder" button) showed under these buttons.
    color: Style.window
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Welcome to Halo")

    // Nothing behind it takes a click or a scroll.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    function shortPath(path) {
        const home = StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace(/^file:\/\//, "")
        return path.startsWith(home + "/") ? "~" + path.substring(home.length) : path
    }
    readonly property string looked: library.folders.map(shortPath).join(", ")

    readonly property string status: {
        if (rejected)
            return qsTr("Halo can't open that folder. Choose another one.")
        if (library.scanning)
            return qsTr("Reading your music. %n files so far.", "", library.scanned)
        if (chosen.length)
            return qsTr("No songs in %1. Halo plays MP3, FLAC, Ogg, Opus, M4A, WAV and most other music files. Choose another folder.").arg(shortPath(chosen))
        return qsTr("Halo looked in %1 and found no music yet.").arg(looked)
    }

    Flickable {
        id: scroll
        SmoothWheel { flickable: scroll }
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + 96)
        clip: true
        ScrollBar.vertical: PixelScrollBar {}

        ColumnLayout {
            id: column
            width: Math.min(scroll.width - 48, 600)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(48, (scroll.height - implicitHeight) / 2)
            spacing: 0

            PixelWordmark {
                Layout.alignment: Qt.AlignHCenter
                cell: 8
                Accessible.ignored: true
            }
            ScrambleText {
                Layout.fillWidth: true
                Layout.topMargin: 28
                target: qsTr("A music player for the songs on this computer, mixed like a DJ would.")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Style.text
                font.pixelSize: Style.fontLarge + 2
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                Layout.topMargin: 12
                text: root.status
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: root.rejected ? Style.danger : Style.muted
                font.pixelSize: Style.fontBody
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            PixelLoader {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 14
                visible: library.scanning
                count: 18
                fraction: -1
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 22
                spacing: 12
                AccentButton {
                    id: choose
                    text: qsTr("Choose your music folder")
                    font.pixelSize: Style.fontBody + 1
                    font.weight: Font.DemiBold
                    enabled: !library.scanning
                    focus: true
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    onClicked: root.chooseFolder()
                }
                Button {
                    text: qsTr("Not now")
                    flat: true
                    implicitHeight: Style.target
                    Material.foreground: Style.muted
                    Accessible.name: qsTr("Not now: close this and look around")
                    onClicked: root.dismissed()
                }
            }

            PixelRule { Layout.fillWidth: true; Layout.topMargin: 36; Layout.bottomMargin: 20 }

            Repeater {
                model: [
                    ["blend", qsTr("Blend"), qsTr("Songs mix into each other on the beat, like a DJ set, not a plain fade. B turns it off for gapless play.")],
                    ["drift", qsTr("Drift"), qsTr("When the queue runs out, Halo keeps going with songs from your library that sound close to the last one. D turns it off.")],
                    ["lyrics", qsTr("Online lyrics"), qsTr("Off until you turn them on. Then Halo asks lrclib.net for the words of the song playing. Nothing else goes online, and there is no account.")]
                ]
                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.bottomMargin: 16
                    spacing: 16
                    Glyph {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 2
                        width: 26
                        height: 26
                        name: modelData[0]
                        color: Style.accentText
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: modelData[1]
                            color: Style.text
                            font.family: Style.mono
                            font.pixelSize: Style.fontBody
                            font.weight: Font.Bold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 1
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData[2]
                            color: Style.muted
                            font.pixelSize: Style.fontBody
                            wrapMode: Text.Wrap
                            lineHeight: 1.1
                        }
                    }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                spacing: 10
                Keycap { label: "?" }
                Text {
                    Layout.maximumWidth: column.width - 40
                    text: qsTr("shows every shortcut. You can also drop songs or folders on this window.")
                    color: Style.faint
                    font.pixelSize: Style.fontBody
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
