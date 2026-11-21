import QtQuick
import QtQuick.Controls
import Halo

// The main action on a page, filled with the accent. Its words are black or
// white, whichever reads at 7:1 on the fill (Style.accentInk), and the fill
// is lifted for accents too mid-toned for either. Material's own highlighted
// button always writes white, so light accents made it unreadable.
// filled: false is the same button without the fill (an unchosen option in
// a row of them, like the equalizer's presets).
Button {
    id: control
    property bool filled: true

    implicitHeight: Style.target
    implicitWidth: Math.max(Style.target, implicitContentWidth + leftPadding + rightPadding)
    topInset: 0
    bottomInset: 0
    leftPadding: 18
    rightPadding: 18
    topPadding: 0
    bottomPadding: 0
    Accessible.role: Accessible.Button
    Accessible.name: text

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? Style.muted : control.filled ? Style.accentInk : Style.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Style.radius
        color: !control.enabled ? Style.line
             : control.filled ? (control.down || control.hovered ? Style.accentFillHover : Style.accentFill)
             : control.down ? Style.line : control.hovered ? Style.hover : "transparent"
        // A keyboard focus ring in the text's own colour.
        border.width: control.visualFocus ? 2 : 0
        border.color: control.filled ? Style.accentInk : Style.accentText
        scale: control.down && Style.motion ? 0.97 : 1
        Behavior on color { ColorAnimation { duration: Style.quick; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: Style.quick; easing.type: Easing.OutCubic } }
    }
}
