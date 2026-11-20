pragma Singleton
import QtQuick

// How pages ask the window for playlist dialogs, and say what happened:
// the window owns the dialogs and the toast, the pages only ask.
QtObject {
    signal newPlaylist(var paths)
    signal newSmart()
    signal editRules(string id)
    signal rename(string id)
    signal open(string id)
    signal deleted(string id, string name)
    signal toast(string text)

    // Adds songs and says how it went: "Added to Road Trip", "Already in
    // Road Trip", "Added 3 to Road Trip (2 were already in it)".
    function add(id, name, paths) {
        const list = (paths || []).filter(p => p && p.length)
        if (!list.length)
            return
        const added = playlists.add(id, list)
        const skipped = list.length - added
        if (added === 0)
            toast(list.length === 1 ? qsTr("Already in %1").arg(name) : qsTr("All of these are already in %1").arg(name))
        else if (skipped > 0)
            toast(qsTr("Added %1 to %2 (%3 were already in it)").arg(added).arg(name).arg(skipped))
        else
            toast(list.length === 1 ? qsTr("Added to %1").arg(name) : qsTr("Added %1 songs to %2").arg(added).arg(name))
    }
}
