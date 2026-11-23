import QtQuick
import Halo

// An album cover, or when there is none, a tile in a colour of its own
// with the album's initials, so a library of untagged mixtapes still has
// faces.
Item {
    id: cover
    property string albumId
    property string title
    property int size: 256
    property real corner: Style.small

    readonly property bool hasImage: image.status === Image.Ready
    // When it changes to another album (as the song changes), the old cover
    // stays underneath while the new one fades up over it. Off for grid
    // tiles, which change while scrolling.
    property bool glitchOnChange: false
    property url shownSource   // the last cover that finished fading up
    onAlbumIdChanged: {
        previous.source = glitchOnChange && Style.motion ? shownSource : ""
        fadeIn.stop()
        image.opacity = Style.motion ? 0 : 1
        if (image.status === Image.Ready)
            fadeIn.restart()
    }

    function hue(text) {
        let h = 0
        for (let i = 0; i < text.length; ++i)
            h = (h * 31 + text.charCodeAt(i)) % 360
        return h / 360
    }

    function initials(text) {
        const words = (text || "").replace(/[^\p{L}\p{N} ]/gu, " ").split(" ").filter(w => w.length)
        return words.slice(0, 2).map(w => w[0].toUpperCase()).join("")
    }

    Rectangle {
        anchors.fill: parent
        radius: cover.corner
        visible: !cover.hasImage
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.hsla(cover.hue(cover.albumId), 0.45, 0.30, 1) }
            GradientStop { position: 1; color: Qt.hsla((cover.hue(cover.albumId) + 0.12) % 1, 0.50, 0.14, 1) }
        }
        Text {
            textFormat: Text.PlainText
            anchors.centerIn: parent
            text: cover.initials(cover.title)
            color: "#f4f4f7"
            font.family: Style.mono
            font.pixelSize: Math.max(12, cover.width * 0.28)
            font.weight: Font.DemiBold
            opacity: 0.9
        }
    }

    // The outgoing cover, held under the new one while it fades up.
    Image {
        id: previous
        anchors.fill: parent
        sourceSize: Qt.size(cover.size, cover.size)
        cache: true
        fillMode: Image.PreserveAspectCrop
        visible: source.toString().length > 0 && image.opacity < 1
    }
    // Covers fade up when they've loaded instead of popping in.
    Image {
        id: image
        anchors.fill: parent
        source: cover.albumId.length ? "image://cover/" + cover.albumId : ""
        sourceSize: Qt.size(cover.size, cover.size)
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectCrop
        visible: status === Image.Ready
        opacity: Style.motion ? 0 : 1
        onStatusChanged: if (status === Image.Ready) fadeIn.restart()
        Component.onCompleted: if (status === Image.Ready) fadeIn.restart()
        NumberAnimation {
            id: fadeIn
            target: image
            property: "opacity"
            to: 1
            duration: Style.motion ? Style.gentle : 0
            easing.type: Easing.OutCubic
            onFinished: {
                cover.shownSource = image.source
                previous.source = ""
            }
        }
    }
}
