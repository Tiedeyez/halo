import QtQuick
import CymtX

// A playlist's face: its first album's cover, or four covers in a grid
// once it holds four albums. Empty and smart playlists get a glyph tile.
Item {
    id: root
    property var covers: []
    property bool smart: false
    property real corner: Style.small

    Rectangle {
        anchors.fill: parent
        radius: root.corner
        color: Style.raised
        border.color: Style.line
        visible: root.covers.length === 0
        Glyph {
            anchors.centerIn: parent
            width: parent.width * 0.38
            height: width
            name: root.smart ? "smart" : "playlist"
            color: Style.accentText
        }
    }
    CoverArt {
        anchors.fill: parent
        visible: root.covers.length > 0 && root.covers.length < 4
        albumId: root.covers.length ? root.covers[0] : ""
        size: Math.max(64, root.width * 2)
        corner: root.corner
    }
    Grid {
        anchors.fill: parent
        columns: 2
        visible: root.covers.length >= 4
        Repeater {
            model: root.covers.length >= 4 ? root.covers.slice(0, 4) : []
            CoverArt {
                required property var modelData
                width: root.width / 2
                height: root.height / 2
                albumId: modelData
                size: Math.max(64, root.width)
                corner: 0
            }
        }
    }
    // The smart mark on a cover, so smart playlists read at a glance.
    Rectangle {
        visible: root.smart && root.covers.length > 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Math.max(4, root.width * 0.05)
        width: Math.max(18, root.width * 0.2)
        height: width
        radius: width / 2
        color: Style.accentFill
        Glyph { anchors.centerIn: parent; width: parent.width * 0.62; height: width; name: "smart"; color: Style.accentInk }
    }
}
