pragma Singleton
import QtQuick

// Things CymtX doesn't mention. `siege` is the lyrics game (Up Up Down Down
// Left Right Left Right in the lyrics); `best` is this session's top score.
// Nothing here is saved.
QtObject {
    property bool siege: false
    property int best: 0
}
