import QtQuick
import QtQuick.Layouts
import Halo

// What a page shows when it has nothing to list: a row of pixels (sweeping
// while Halo is busy), a headline that decrypts in, and a line on what to do.
ColumnLayout {
    id: root
    property string headline
    property string body
    property bool busy: false
    // An optional button under the words, for the one thing to do next.
    property string action: ""
    signal acted()

    width: Math.min(parent ? parent.width - 48 : 440, 460)
    spacing: 14

    PixelLoader {
        Layout.alignment: Qt.AlignHCenter
        count: root.busy ? 18 : 9
        fraction: root.busy ? -1 : 0
    }
    ScrambleText {
        Layout.fillWidth: true
        Layout.topMargin: 6
        target: root.headline
        horizontalAlignment: Text.AlignHCenter
        color: Style.text
        font.family: Style.mono
        font.pixelSize: Style.fontLarge
        font.weight: Font.Bold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 2
        wrapMode: Text.Wrap
    }
    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.body
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Style.muted
        font.pixelSize: Style.fontBody
        lineHeight: 1.15
    }
    AccentButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 6
        visible: root.action.length > 0
        text: root.action
        onClicked: root.acted()
    }
}
