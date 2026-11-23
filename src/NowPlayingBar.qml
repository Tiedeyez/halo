import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The transport along the bottom: what plays (with a small halo round the
// cover), the controls, time, and the Blend/Drift/EQ/sleep switches.
Rectangle {
    id: root
    signal openNowPlaying()
    signal openEq()
    signal openSleep()
    color: Style.panel
    implicitHeight: 104

    PixelRule { anchors.top: parent.top; width: parent.width }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 16

        // Now playing
        RowLayout {
            Layout.preferredWidth: Math.max(240, root.width * 0.28)
            Layout.maximumWidth: 420
            spacing: 14

            Item {
                Layout.preferredWidth: 84
                Layout.preferredHeight: 84
                HaloView {
                    anchors.fill: parent
                    spectrum: spectrumSource
                    color: Style.accent
                    inner: 0.72
                    calm: player.calm
                    visible: player.hasTrack
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 58
                    height: 58
                    radius: Style.small
                    visible: !player.hasTrack
                    color: Style.raised
                    border.color: Style.line
                    Glyph { anchors.centerIn: parent; name: "halo"; color: Style.faint; width: 26; height: 26 }
                }
                CoverArt {
                    glitchOnChange: true
                    anchors.centerIn: parent
                    width: 58
                    height: 58
                    visible: player.hasTrack
                    albumId: player.albumId
                    title: player.album
                    size: 256
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.openNowPlaying()
                    Accessible.name: qsTr("Open Now Playing")
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: player.hasTrack ? Style.mainTitle(player.title) : qsTr("Nothing playing")
                    color: Style.text
                    font.pixelSize: Style.fontBody + 1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: player.hasTrack ? Style.artistName(player.artist) : qsTr("Pick an album, or drop music here")
                    color: Style.muted
                    font.pixelSize: Style.fontBody
                    elide: Text.ElideRight
                }
                // The line being sung, while the lyrics aren't on screen.
                ScrambleText {
                    readonly property bool singing: lyrics.status === Lyrics.Found && lyrics.current >= 0 &&
                        (lyrics.kind === Lyrics.KindSynced || lyrics.kind === Lyrics.KindWords) && lyrics.gap <= 0
                    Layout.fillWidth: true
                    visible: singing && !(player.blending || player.blendIn >= 0 && player.blendIn < 30)
                    target: singing ? (lyrics.lines[lyrics.current] || {}).text || "" : ""
                    duration: 220
                    color: Style.accentText
                    font.family: Style.mono
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: player.blending || player.blendIn >= 0 && player.blendIn < 30
                    text: player.blending ? qsTr("Blending in")
                                          : qsTr("Blends into %1 in %2").arg(player.nextTitle).arg(Style.clock(player.blendIn))
                    color: Style.accentText
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
            }
        }

        // Transport and time
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 6
                IconButton {
                    glyph: "shuffle"
                    tip: qsTr("Shuffle (S)")
                    active: player.shuffle
                    onClicked: player.shuffle = !player.shuffle
                }
                IconButton {
                    glyph: "previous"
                    tip: qsTr("Previous (P)")
                    enabled: player.hasTrack
                    onClicked: player.previous()
                }
                IconButton {
                    primary: true
                    glyph: player.playing ? "pause" : "play"
                    tip: player.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                    onClicked: player.togglePlay()
                }
                IconButton {
                    glyph: "next"
                    tip: qsTr("Next (N)")
                    enabled: player.hasTrack
                    onClicked: player.next()
                }
                IconButton {
                    glyph: player.repeat === 2 ? "repeatOne" : "repeat"
                    tip: player.repeat === 0 ? qsTr("Repeat off (R)") : player.repeat === 1 ? qsTr("Repeat all (R)") : qsTr("Repeat one (R)")
                    active: player.repeat !== 0
                    onClicked: player.cycleRepeat()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Text {
                    textFormat: Text.PlainText
                    text: Style.clock(player.position)
                    color: Style.muted
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                    Layout.preferredWidth: 52
                    horizontalAlignment: Text.AlignRight
                }
                SeekBar {
                    Layout.fillWidth: true
                    position: player.position
                    duration: player.duration
                    moments: player.currentMoments
                    blendFrom: player.blendIn >= 0 ? player.position + player.blendIn : -1
                }
                Text {
                    textFormat: Text.PlainText
                    text: Style.clock(player.duration)
                    color: Style.muted
                    font.pixelSize: Style.fontSmall
                    font.features: { "tnum": 1 }
                    Layout.preferredWidth: 52
                }
            }
        }

        // Switches and volume
        RowLayout {
            Layout.preferredWidth: Math.max(240, root.width * 0.26)
            Layout.maximumWidth: 400
            Layout.alignment: Qt.AlignRight
            spacing: 2
            Item { Layout.fillWidth: true }
            IconButton {
                glyph: "playlistAdd"
                tip: qsTr("Add this song to a playlist")
                enabled: player.hasTrack
                onClicked: { nowMenu.paths = [player.currentPath]; nowMenu.popup() }
                AddToPlaylistMenu { id: nowMenu }
            }
            IconButton {
                glyph: "blend"
                tip: player.blend ? qsTr("Blend is on: songs mix into each other (B)") : qsTr("Blend is off: gapless (B)")
                active: player.blend
                onClicked: player.blend = !player.blend
            }
            IconButton {
                glyph: "drift"
                tip: player.drift ? qsTr("Drift is on: keeps playing similar music (D)") : qsTr("Drift is off (D)")
                active: player.drift
                onClicked: player.drift = !player.drift
            }
            IconButton {
                glyph: "eq"
                tip: qsTr("Equalizer (E)")
                active: player.eqEnabled
                onClicked: root.openEq()
            }
            IconButton {
                glyph: "sleep"
                tip: player.sleepRemaining > 0 ? qsTr("Sleep in %1 (Z)").arg(Style.clock(player.sleepRemaining)) : qsTr("Sleep timer (Z)")
                active: player.sleepRemaining > 0
                onClicked: root.openSleep()
            }
            VolumeControl {}
        }
    }
}
