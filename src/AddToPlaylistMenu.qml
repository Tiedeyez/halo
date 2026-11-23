import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import Halo

// "Add to playlist": a new playlist, then every playlist you can add to
// (smart ones fill themselves). Works as a submenu or on its own.
Menu {
    id: menu
    property var paths: []
    title: qsTr("Add to playlist")
    Material.roundedScale: Style.radius

    MenuItem {
        text: qsTr("New playlist…")
        onTriggered: PlaylistUi.newPlaylist(menu.paths)
    }
    MenuSeparator { visible: playlists.manual.length > 0 }
    Instantiator {
        model: playlists.manual
        delegate: MenuItem {
            required property var modelData
            id: item
            text: modelData.name
            // A playlist's name can come from a file someone else wrote. The
            // stock label would read markup in it; this shows it as written.
            contentItem: Text {
                textFormat: Text.PlainText
                text: item.text
                font: item.font
                color: item.enabled ? item.Material.foreground : item.Material.hintTextColor
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            onTriggered: PlaylistUi.add(modelData.id, modelData.name, menu.paths)
        }
        onObjectAdded: (index, object) => menu.insertItem(index + 2, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
