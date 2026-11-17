import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The song, large, inside its halo. F or Esc closes it.
Rectangle {
    id: root
    signal closed()
    color: Style.window

    // The cover's colours wash the background, softly.
    CoverArt {
        anchors.fill: parent
        albumId: player.albumId
        title: player.album
        size: 256
        opacity: 0.10
        corner: 0
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.alpha(Style.window, 0.55) }
            GradientStop { position: 1; color: Style.window }
        }
    }

    IconButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        glyph: "close"
        tip: qsTr("Close (F or Esc)")
        onClicked: root.closed()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 48
        anchors.bottomMargin: 32
        spacing: 48

        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            readonly property real side: Math.min(width, height)

            HaloView {
                anchors.centerIn: parent
                width: stage.side
                height: stage.side
                spectrum: spectrumSource
                color: Style.accent
                inner: 0.56
                calm: player.calm
                style: HaloView.Rays
                visible: player.visualStyle === 0
            }
            CoverArt {
                anchors.centerIn: parent
                glitchOnChange: true
                width: stage.side * (player.visualStyle === 0 ? 0.52 : 0.78)
                height: width
                albumId: player.albumId
                title: player.album
                size: 640
                scale: player.calm || player.visualStyle !== 0 ? 1 : 1 + spectrumSource.pulse * 0.015
            }
            HaloView {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: stage.side * 0.78
                height: 72
                spectrum: spectrumSource
                color: Style.accent
                calm: player.calm
                style: HaloView.Bars
                visible: player.visualStyle === 1
            }
        }

        ColumnLayout {
            Layout.fillHeight: true
            Layout.preferredWidth: 2
            Layout.maximumWidth: 520
            spacing: 8

            Item { Layout.fillHeight: true }

            ScrambleText {
                Layout.fillWidth: true
                target: player.hasTrack ? player.title : qsTr("Nothing playing")
                duration: 560
                color: Style.text
                font.pixelSize: Style.fontHero + 6
                font.weight: Font.Bold
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: Style.artistName(player.artist)
                visible: player.hasTrack
                color: Style.muted
                font.pixelSize: Style.fontLarge + 3
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: player.album
                visible: player.hasTrack && player.album.length > 0
                color: Style.faint
                font.pixelSize: Style.fontBody
                elide: Text.ElideRight
            }

            // What Halo heard in the song.
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: 14
                spacing: 8
                visible: player.hasTrack
                Repeater {
                    model: [
                        player.bpm > 0 ? qsTr("%1 BPM").arg(Math.round(player.bpm)) : "",
                        player.loudness < 0 ? qsTr("%1 LUFS").arg(player.loudness.toLocaleString(Qt.locale(translator.language), 'f', 1)) : "",
                        player.blend ? qsTr("Blend") : qsTr("Gapless"),
                        player.drift ? qsTr("Drift") : "",
                        player.eqEnabled ? qsTr("EQ: %1").arg(qsTr(player.eqPreset)) : "",
                        player.sleepRemaining > 0 ? qsTr("Sleep %1").arg(Style.clock(player.sleepRemaining)) : ""
                    ].filter(s => s.length)
                    Rectangle {
                        required property string modelData
                        width: chip.implicitWidth + 20
                        height: 30
                        radius: Style.small
                        color: Style.raised
                        border.color: Style.line
                        Text {
                            id: chip
                            anchors.centerIn: parent
                            text: parent.modelData
                            color: Style.muted
                            font.pixelSize: Style.fontSmall + 1
                            font.family: Style.mono
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 22
                spacing: 10
                Text {
                    text: Style.clock(player.position)
                    color: Style.muted
                    font.pixelSize: Style.fontSmall + 1
                    font.family: Style.mono
                }
                SeekBar {
                    Layout.fillWidth: true
                    position: player.position
                    duration: player.duration
                    moments: player.currentMoments
                    blendFrom: player.blendIn >= 0 ? player.position + player.blendIn : -1
                }
                Text {
                    text: Style.clock(player.duration)
                    color: Style.muted
                    font.pixelSize: Style.fontSmall + 1
                    font.family: Style.mono
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 12
                IconButton { glyph: "moment"; tip: qsTr("Save a moment (M)"); onClicked: player.addMoment() }
                IconButton { glyph: "previous"; tip: qsTr("Previous (P)"); onClicked: player.previous() }
                IconButton {
                    primary: true
                    glyph: player.playing ? "pause" : "play"
                    tip: player.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                    onClicked: player.togglePlay()
                }
                IconButton { glyph: "next"; tip: qsTr("Next (N)"); onClicked: player.next() }
                IconButton { glyph: "blend"; tip: qsTr("Blend (B)"); active: player.blend; onClicked: player.blend = !player.blend }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 22
                visible: player.nextTitle.length > 0
                spacing: 2
                Text {
                    text: player.blending ? qsTr("Blending from the last song")
                        : player.blendIn >= 0 ? qsTr("Blends in %1").arg(Style.clock(player.blendIn)) : qsTr("Up next")
                    color: Style.faint
                    font.pixelSize: Style.fontSmall
                    font.family: Style.mono
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1
                }
                Text {
                    Layout.fillWidth: true
                    text: player.nextTitle + "  ·  " + Style.artistName(player.nextArtist)
                    color: Style.muted
                    font.pixelSize: Style.fontBody
                    elide: Text.ElideRight
                }
            }

            Item { Layout.fillHeight: true }

            Text {
                Layout.fillWidth: true
                text: qsTr("V switches the visual · C calms the motion")
                font.family: Style.mono
                color: Style.faint
                font.pixelSize: Style.fontSmall
            }
        }
    }
}
