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
            text: modelData.name
            onTriggered: PlaylistUi.add(modelData.id, modelData.name, menu.paths)
        }
        onObjectAdded: (index, object) => menu.insertItem(index + 2, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
