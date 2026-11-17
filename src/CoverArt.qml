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
    // Tear the cover apart for a moment when it changes to another album,
    // as the song changes. Off for grid tiles, which change while scrolling.
    property bool glitchOnChange: false

    GlitchDriver { id: glitch; duration: 420 }
    layer.enabled: glitch.active
    layer.effect: GlitchEffect { amount: glitch.amount; seed: glitch.seed }
    onAlbumIdChanged: if (glitchOnChange && albumId.length) glitch.kick(1)
    function glitchNow(strength) { glitch.kick(strength) }

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
            anchors.centerIn: parent
            text: cover.initials(cover.title)
            color: "#f4f4f7"
            font.family: Style.mono
            font.pixelSize: Math.max(12, cover.width * 0.28)
            font.weight: Font.DemiBold
            opacity: 0.9
        }
    }

    Image {
        id: image
        anchors.fill: parent
        source: cover.albumId.length ? "image://cover/" + cover.albumId : ""
        sourceSize: Qt.size(cover.size, cover.size)
        asynchronous: true
        cache: true
        fillMode: Image.PreserveAspectCrop
        visible: status === Image.Ready
    }
}
