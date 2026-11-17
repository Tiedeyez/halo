import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The song, large, inside its halo; with L, its lyrics beside it. The layout
// follows the window: a wide window puts the cover and controls on the left
// and the lyrics on the right; a small one (a tiled window, a laptop half)
// keeps a slim header so the lyrics get the room. F or Esc closes it.
Rectangle {
    id: root
    signal closed()
    color: Style.window

    readonly property bool lyricsOn: lyrics.shown
    // Words to show. Without them (none found, still looking, an
    // instrumental) the song keeps its full layout and a note says why.
    readonly property bool hasWords: lyrics.status === Lyrics.Found && lyrics.count > 0
                                     && lyrics.kind !== Lyrics.KindInstrumental
    readonly property bool showWords: lyricsOn && hasWords
    readonly property bool wide: width >= 1080 && height >= 560
    readonly property real gutter: Math.max(16, Math.min(48, Math.min(width, height) * 0.05))

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
    // The pixel field, its beat ring starting from the cover.
    PixelField {
        anchors.fill: parent
        cell: 7
        origin: Qt.point(width * (root.showWords ? 0.2 : 0.32), height * 0.42)
    }

    // ---- pieces ----

    // The cover inside the halo (or above the bars).
    component Stage: Item {
        id: stage
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
        PixelMandelbrot {
            anchors.centerIn: parent
            width: stage.side
            height: stage.side
            cell: Math.max(4, Math.round(stage.side / 90))
            visible: player.visualStyle === 2
        }
        CoverArt {
            anchors.centerIn: parent
            glitchOnChange: true
            // Smaller in the Mandelbrot, so the set has a wide ring to show.
            width: stage.side * (player.visualStyle === 1 ? 0.78 : player.visualStyle === 2 ? 0.36 : 0.52)
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
            height: Math.min(72, stage.side * 0.14)
            spectrum: spectrumSource
            color: Style.accent
            calm: player.calm
            style: HaloView.Bars
            visible: player.visualStyle === 1
        }
    }

    // Title (decrypting), its credits, artist and album, what Halo heard.
    component Header: ColumnLayout {
        property int titleSize: Style.fontHero + 6
        property bool chips: true
        property int titleLines: 3
        spacing: 6

        ScrambleText {
            Layout.fillWidth: true
            target: player.hasTrack ? Style.mainTitle(player.title) : qsTr("Nothing playing")
            duration: 560
            color: Style.text
            font.pixelSize: parent.titleSize
            font.weight: Font.Bold
            wrapMode: Text.Wrap
            maximumLineCount: parent.titleLines
            elide: Text.ElideRight
        }
        // Producer and feature credits, out of the title's way.
        Text {
            Layout.fillWidth: true
            text: Style.credits(player.title)
            visible: player.hasTrack && text.length > 0
            color: Style.faint
            font.family: Style.mono
            font.pixelSize: Style.fontSmall
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: Style.artistName(player.artist) + (parent.chips || !player.album.length ? "" : "  ·  " + player.album)
            visible: player.hasTrack
            color: Style.muted
            font.pixelSize: parent.chips ? Style.fontLarge + 3 : Style.fontLarge
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: player.album
            visible: parent.chips && player.hasTrack && player.album.length > 0
            color: Style.faint
            font.pixelSize: Style.fontBody
            elide: Text.ElideRight
        }
        // What Halo heard in the song.
        Flow {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8
            visible: parent.chips && player.hasTrack
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
    }

    // Time, seek bar and the buttons.
    component Transport: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
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
            IconButton { glyph: "lyrics"; tip: qsTr("Lyrics (L)"); active: lyrics.shown; onClicked: lyrics.shown = !lyrics.shown }
        }
    }

    // A short note in place of the lyrics when there are none to show.
    component LyricsNote: ColumnLayout {
        spacing: 6
        readonly property bool canAct: lyrics.status === Lyrics.Offline || lyrics.status === Lyrics.NotFound
                                       || lyrics.status === Lyrics.Failed
        Text {
            Layout.fillWidth: true
            text: lyrics.status === Lyrics.Searching ? qsTr("Looking for lyrics")
                : lyrics.status === Lyrics.Offline ? qsTr("No lyrics on this computer")
                : lyrics.status === Lyrics.NotFound ? qsTr("No lyrics found")
                : lyrics.status === Lyrics.Failed ? qsTr("Couldn't reach lrclib.net")
                : lyrics.kind === Lyrics.KindInstrumental ? qsTr("Instrumental") : qsTr("No lyrics")
            color: Style.faint
            font.pixelSize: Style.fontSmall
            font.family: Style.mono
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
        }
        Text {
            Layout.fillWidth: true
            text: lyrics.status === Lyrics.Searching ? qsTr("Asking lrclib.net. The words appear here when they arrive.")
                : lyrics.status === Lyrics.Offline ? qsTr("Looking them up sends the title, artist, album and length to lrclib.net.")
                : lyrics.status === Lyrics.NotFound ? qsTr("LRCLIB doesn't have this one. A .lrc file beside the song works too.")
                : lyrics.status === Lyrics.Failed ? qsTr("Check the connection. Halo tries again on the next song.")
                : lyrics.kind === Lyrics.KindInstrumental ? qsTr("No words in this one.") : ""
            visible: text.length > 0
            color: Style.muted
            font.pixelSize: Style.fontBody
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Button {
            visible: parent.canAct
            text: lyrics.status === Lyrics.Offline ? qsTr("Turn on online lyrics") : qsTr("Look again")
            flat: true
            Material.foreground: Style.accent
            Material.roundedScale: Style.radius
            implicitHeight: Style.target
            leftPadding: 0
            onClicked: lyrics.status === Lyrics.Offline ? lyrics.online = true : lyrics.retry()
        }
    }

    // ---- layouts ----

    // Without lyrics: the halo large, the song beside it.
    Component {
        id: songLayout
        RowLayout {
            spacing: root.gutter
            Stage {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
            }
            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                Layout.maximumWidth: 520
                spacing: 8
                Item { Layout.fillHeight: true }
                Header {
                    Layout.fillWidth: true
                    titleSize: root.height < 560 ? Style.fontTitle + 6 : Style.fontHero + 6
                    titleLines: root.height < 560 ? 2 : 3
                }
                Transport { Layout.fillWidth: true; Layout.topMargin: 14 }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    visible: player.nextTitle.length > 0 && root.height >= 520
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
                LyricsNote {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    visible: root.lyricsOn && player.hasTrack && !root.hasWords
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("V switches the visual · C calms the motion · L shows the lyrics")
                    font.family: Style.mono
                    color: Style.faint
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    // Lyrics, wide: halo and song on the left, the words filling the right.
    Component {
        id: wideLyricsLayout
        RowLayout {
            spacing: root.gutter
            ColumnLayout {
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                Layout.maximumWidth: 520
                spacing: 10
                Stage {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 160
                }
                Header {
                    Layout.fillWidth: true
                    titleSize: Style.fontTitle + 6
                    titleLines: 2
                }
                Transport { Layout.fillWidth: true; Layout.topMargin: 8 }
            }
            LyricsView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
            }
        }
    }

    // Lyrics, small window: a slim header, the words, then the controls.
    Component {
        id: compactLyricsLayout
        ColumnLayout {
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                Layout.rightMargin: 48 // clear of the close button
                spacing: 14
                // The cover and the visualiser stay in view, as large as the
                // window allows while leaving the words most of the room.
                Stage {
                    readonly property real side: Math.max(120, Math.min(260, root.height * 0.30, root.width * 0.42))
                    Layout.preferredWidth: side
                    Layout.preferredHeight: side
                }
                Header {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    titleSize: Style.fontLarge + 5
                    titleLines: 2
                    chips: false
                    spacing: 2
                }
            }
            LyricsView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 90
            }
            Transport { Layout.fillWidth: true }
        }
    }

    Loader {
        anchors.fill: parent
        anchors.margins: root.gutter
        anchors.bottomMargin: Math.max(16, root.gutter * 0.7)
        sourceComponent: !root.showWords ? songLayout : root.wide ? wideLyricsLayout : compactLyricsLayout
    }

    IconButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        glyph: "close"
        tip: qsTr("Close (F or Esc)")
        onClicked: root.closed()
    }
}
