import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// Full-bleed song visual behind the song and lyrics. The layout follows the
// window: a wide window puts the song on the left and the lyrics on the right;
// a small one (a tiled window, a laptop half) keeps a slim header. F or Esc
// closes it.
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
    readonly property var visualNames: [qsTr("RING"), qsTr("BARS"), qsTr("KALEIDOSCOPE"), qsTr("SKYLINE"),
        qsTr("PHOSPHOR"), qsTr("PULSAR"), qsTr("WATERFALL"), qsTr("WARP"), qsTr("AURORA"), qsTr("HALO"), qsTr("TURNTABLE"), qsTr("HOLOGRAM")]
    readonly property real gutter: Math.max(16, Math.min(48, Math.min(width, height) * 0.05))

    // The cover's colours wash the background, softly: only its colours.
    // The cover is drawn 8 pixels square and stretched smoothly, so no
    // shape of it (a face, a title, a placeholder's initials) shows through
    // the dark visuals as a ghost, and no stretched pixels show as blocks.
    CoverArt {
        id: wash
        width: 8
        height: 8
        albumId: player.albumId
        title: player.album
        size: 64
        corner: 0
    }
    ShaderEffectSource {
        anchors.fill: parent
        sourceItem: wash
        hideSource: true      // the 8 px source itself stays off screen
        textureSize: Qt.size(8, 8)
        smooth: true
        live: true
        opacity: 0.10
    }
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.alpha(Style.window, 0.55) }
            GradientStop { position: 1; color: Style.window }
        }
    }
    // ---- pieces ----

    // The active visual is the background. The pixel visuals fill the whole
    // window; the round ones (halo, kaleidoscope, hologram, turntable) and the cover
    // centre on the song's side of a wide window, left of the text column,
    // and are sized from that area so they fill its height and bleed a
    // little past it, rather than being centred on the window and half
    // hidden under the text's shade.
    component Stage: Item {
        id: stage
        // The song's area: left of the text column in a wide window, the
        // whole window otherwise.
        readonly property real focusW: root.wide ? root.width * 0.62 : root.width
        readonly property real focusH: root.height
        readonly property real shortSide: Math.min(focusW, focusH)
        readonly property real haloSide: Math.min(focusH * 1.02, focusW * 1.05)
        readonly property real kaleidoSide: Math.max(focusW, focusH) * 1.12
        Item {
            id: focusPoint
            x: stage.focusW * 0.5
            y: stage.focusH * 0.5
            width: 0
            height: 0
        }
        // The halo: the ring of rays, drawn one cell per pixel like the
        // others (68 x pixelDensity cells across).
        PixelVisualView {
            anchors.centerIn: focusPoint
            width: stage.haloSide
            height: stage.haloSide
            cell: Math.max(3, Math.round(stage.haloSide / (68 * Style.pixelDensity)))
            mode: PixelVisual.Ring
            opacity: player.visualStyle === 0 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic } }
        }
        PixelKaleido {
            anchors.centerIn: focusPoint
            width: stage.kaleidoSide
            height: stage.kaleidoSide
            // About 140 x pixelDensity pixels across: fine enough for the fractal's detail
            // (four samples each), cheap enough for integrated graphics.
            cell: Math.max(3, Math.round(stage.kaleidoSide / (140 * Style.pixelDensity)))
            opacity: player.visualStyle === 2 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic } }
        }
        // The pixel visualisers: Skyline, Phosphor, Pulsar, Waterfall, Warp, Aurora.
        PixelVisualView {
            anchors.fill: parent
            cell: Math.max(3, Math.ceil(Math.max(root.width, root.height) / (128 * Style.pixelDensity)))
            mode: Math.max(0, player.visualStyle - 3)
            opacity: player.visualStyle >= 3 && player.visualStyle <= 8 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic } }
        }
        // The halo: a ring of light over the whole window, its light
        // reaching above and below; the ring centres on the song's side.
        HaloLight {
            anchors.fill: parent
            centre: Qt.point(focusPoint.x / Math.max(1, width), focusPoint.y / Math.max(1, height))
            ringRadius: stage.shortSide * 0.36
            opacity: player.visualStyle === 9 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic } }
        }
        // The hologram: the Turntable's deck made of light, the same square
        // render, so the same size.
        HologramVisual {
            anchors.centerIn: focusPoint
            width: Math.min(stage.focusH * 1.2, stage.focusW * 1.15)
            height: width
            opacity: player.visualStyle === 11 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic } }
        }
        // The turntable: a square render with the deck across about 80% of
        // it, so it is drawn a little larger than the round visuals.
        TurntableVisual {
            anchors.centerIn: focusPoint
            width: Math.min(stage.focusH * 1.2, stage.focusW * 1.15)
            height: width
            opacity: player.visualStyle === 10 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic } }
        }
        CoverArt {
            anchors.centerIn: focusPoint
            glitchOnChange: true
            visible: player.visualStyle <= 2
            // Inside the halo's ring; small in the kaleidoscope so the
            // fractal leads; a little larger over the bars.
            width: player.visualStyle === 0 ? stage.haloSide * 0.46
                 : player.visualStyle === 2 ? stage.shortSide * 0.28 : stage.shortSide * 0.40
            height: width
            albumId: player.albumId
            title: player.album
            size: 640
            scale: player.calm || player.visualStyle !== 0 ? 1 : 1 + spectrumSource.pulse * 0.015
        }
        // The bars, with their floor below: the bars keep their place
        // (centred on the song), the reflection hangs beneath them.
        BarsVisual {
            id: barsVisual
            x: focusPoint.x - width / 2
            y: focusPoint.y - barsHeight / 2
            width: stage.focusW
            barsHeight: Math.min(stage.focusH * 0.52, Math.max(72, stage.focusW * 0.2))
            height: barsHeight * 1.55
            opacity: player.visualStyle === 1 ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Style.motion ? Style.gentle : 0; easing.type: Easing.OutCubic } }
        }
    }

    Stage { anchors.fill: parent }

    // Keep the text column readable over any of the visual palettes. The
    // brightest possible visual pixel is white; at 93% Style.window coverage
    // even Style.faint remains above 7:1. The song layout fades into the
    // text column; lyric layouts place text across the reading area.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: root.showWords ? Gradient.Vertical : Gradient.Horizontal
            GradientStop {
                position: 0
                color: root.showWords ? Qt.alpha(Style.window, 0.93) : Qt.alpha(Style.window, 0)
            }
            GradientStop {
                position: root.showWords ? 0.92 : 0.42
                color: root.showWords ? Qt.alpha(Style.window, 0.93) : Qt.alpha(Style.window, 0)
            }
            // A long, eased ramp so the visual sinks under the text instead
            // of stopping at a hard edge; full strength before the text.
            GradientStop {
                position: root.showWords ? 0.96 : 0.56
                color: Qt.alpha(Style.window, root.showWords ? 0.93 : 0.55)
            }
            GradientStop {
                position: root.showWords ? 1 : 0.66
                color: Qt.alpha(Style.window, 0.93)
            }
            GradientStop { position: 1; color: Qt.alpha(Style.window, 0.95) }
        }
    }

    // The visual's name, for a moment after V switches to it.
    Rectangle {
        anchors.centerIn: visualName
        width: visualName.implicitWidth + 24
        height: visualName.implicitHeight + 12
        color: Qt.alpha(Style.window, 0.93)
        opacity: visualName.opacity
        z: 1
    }
    ScrambleText {
        id: visualName
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: root.gutter * 0.65
        z: 2
        target: root.visualNames[player.visualStyle] || ""
        duration: 420
        color: Style.text
        font.family: Style.mono
        font.pixelSize: Style.fontSmall
        font.letterSpacing: 2
        opacity: 0
        Connections {
            target: player
            function onSettingsChanged() {
                if (visualName.shownStyle !== player.visualStyle) {
                    visualName.shownStyle = player.visualStyle
                    nameShow.restart()
                }
            }
        }
        property int shownStyle: player.visualStyle
        SequentialAnimation {
            id: nameShow
            NumberAnimation { target: visualName; property: "opacity"; to: 1; duration: Style.motion ? 120 : 0 }
            PauseAnimation { duration: 1300 }
            NumberAnimation { target: visualName; property: "opacity"; to: 0; duration: Style.motion ? 500 : 0 }
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
        // The bar (and its volume) is hidden here, so the volume is too.
        VolumeControl {
            Layout.alignment: Qt.AlignHCenter
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
            Material.foreground: Style.accentText
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
            Item {
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
