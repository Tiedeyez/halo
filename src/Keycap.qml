import QtQuick
import Halo

// A keyboard key, for showing which key does what.
Rectangle {
    property string label
    implicitWidth: Math.max(20, keyText.implicitWidth + 10)
    implicitHeight: 20
    radius: Style.small
    color: "transparent"
    border.color: Style.line
    border.width: 1
    Accessible.ignored: true

    Text {
        textFormat: Text.PlainText
        id: keyText
        anchors.centerIn: parent
        text: parent.label
        color: Style.faint
        font.pixelSize: Style.fontSmall
        font.family: "monospace"
    }
}
