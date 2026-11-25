import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "HaloStatus.js" as HaloStatus

// Halo in the bar: what is playing, and a small player in a popup: the
// cover, a seek bar, play/pause, next and previous, shuffle and repeat,
// Blend and Drift, the sleep timer, a moment, Halo's volume and the line
// being sung. It talks to Halo only by running the `halo` command with one
// of the fixed argument lists in HaloStatus.js, never through a shell, and
// the only values it passes are whole numbers in a range and a few fixed
// words. It reads at most 64 KiB of each reply, gives a command five
// seconds, shows the cover Halo already cached (a local picture), and
// writes no files, edits no configuration and opens no network connection.
BarWidget {
  id: root
  moduleName: "tiedeyez.halo"

  // The installed program. Absolute, so nothing earlier on PATH can stand in
  // for it. Not read from settings; the plugin's own test points it at a stub.
  property string haloBinary: "/usr/bin/halo"

  // What Halo last said, already bounded by HaloStatus.parse().
  property var state: HaloStatus.emptyState("offline", "")
  // When it said it (ms), so the clock can run between replies.
  property real stamp: 0
  property real now: 0
  // The line being sung, bounded by HaloStatus.parseLyrics().
  property var lyrics: HaloStatus.emptyLyrics()
  // Whether asking for this song's lyrics again can show anything new.
  property bool lyricsWanted: true
  // Until the first reply the widget doesn't claim Halo is missing or off.
  property bool checked: false
  property bool popupOpen: false

  readonly property string kind: state.kind
  readonly property bool installed: kind !== "missing"
  readonly property bool reachable: kind === "idle" || kind === "paused" || kind === "playing"
  readonly property bool hasSong: kind === "paused" || kind === "playing"
  readonly property bool playing: kind === "playing"

  // ---- settings (all optional, in the widget's entry in shell.json) -------
  //   showTitle       false hides the song in the bar            (true)
  //   maxTitleLength  the longest bar label, 8 to 80             (32)
  //   equalizer       false keeps the still note while playing   (true)
  //   artwork         false hides the album cover in the bar     (true)
  //   progress        false hides the line under the bar label   (true)
  //   lyrics          false hides the line being sung            (true)
  //   click           "open" makes a click open Halo and a right
  //                   click the player; "player" the reverse     ("player")
  //   scroll          "volume" makes the wheel change Halo's
  //                   volume; "track" skips songs                ("track")
  readonly property bool showTitle: setting("showTitle", true) !== false
  readonly property int maxLabelChars: Math.max(8, Math.min(80, Number(setting("maxTitleLength", 32)) || 32))
  readonly property bool showArtwork: setting("artwork", true) !== false
  readonly property bool showProgress: setting("progress", true) !== false
  readonly property bool showLyrics: setting("lyrics", true) !== false
  readonly property bool clickOpensHalo: setting("click", "player") === "open"
  readonly property bool scrollVolume: setting("scroll", "track") === "volume"

  readonly property color fg: root.bar ? root.bar.foreground : Commons.Color.foreground
  readonly property color fgSoft: Qt.rgba(fg.r, fg.g, fg.b, 0.8)
  readonly property color fgFaint: Qt.rgba(fg.r, fg.g, fg.b, 0.68)
  readonly property string face: root.bar ? root.bar.fontFamily : Style.font.family

  // While a song plays, the note in the bar becomes three pixel bars that
  // keep moving. Decoration only: the widget hears nothing, it knows a song
  // is playing and no more. `"equalizer": false` in the widget's settings
  // keeps the note, and so does Omarchy's reduce-motion setting.
  readonly property bool showEqualizer: setting("equalizer", true) !== false && !Style.reduceMotion
  readonly property bool dancing: playing && showEqualizer
  readonly property var eqLevels: [
    [2, 4, 6, 4, 3, 1, 3, 5, 6, 3, 2, 4, 5, 3, 1, 4],
    [6, 5, 3, 2, 4, 5, 6, 4, 2, 4, 6, 5, 2, 3, 6, 3],
    [1, 3, 4, 6, 5, 3, 2, 3, 5, 6, 4, 2, 6, 4, 3, 2]
  ]
  property int eqStep: 0
  Timer {
    interval: 170
    repeat: true
    running: root.dancing && root.visible
    onTriggered: root.eqStep = (root.eqStep + 1) % 16
  }

  // The clock between replies: Halo said where the song was at `stamp`, and
  // a playing song has moved on since. Only runs while something shows it.
  readonly property real elapsed: Math.max(0, now - stamp) / 1000
  readonly property real shownPosition: {
    var p = state.position + (playing ? elapsed : 0)
    return state.duration > 0 ? Math.min(state.duration, p) : p
  }
  readonly property real fraction: state.duration > 0 ? Math.max(0, Math.min(1, shownPosition / state.duration)) : 0
  readonly property real sleepLeft: state.sleep > 0 ? Math.max(0, state.sleep - elapsed) : 0
  Timer {
    interval: root.popupOpen ? 250 : 1000
    repeat: true
    running: root.visible && (root.playing || root.state.sleep > 0) && (root.popupOpen || root.progressShown)
    onTriggered: root.now = Date.now()
  }

  readonly property string songLine: state.title + (state.artist ? "  ·  " + state.artist : "")
  readonly property string barLabel: {
    if (!showTitle || vertical || !hasSong) return ""
    var text = songLine
    return text.length > maxLabelChars ? text.slice(0, maxLabelChars - 1) + "…" : text
  }
  // The cover Halo cached for the album, as a link to the local file; empty
  // when there is none (or Halo is too old to name it).
  readonly property string coverUrl: hasSong && state.cover !== ""
    ? "file://" + state.cover.split("/").map(encodeURIComponent).join("/") : ""
  // The cover sits before the label in a horizontal bar, once it has loaded.
  readonly property bool artShown: showArtwork && !vertical && coverUrl !== "" && barArt.status === Image.Ready
  readonly property int artSide: Math.max(12, Math.min(barSize - Style.space(6), Style.bar.iconSlot - Style.space(5)))
  readonly property int artRoom: artShown ? artSide + Style.spacing.md : 0
  readonly property bool progressShown: showProgress && hasSong && state.duration > 0 && !vertical

  // Tempo and loudness, when Halo has analysed the song; then the place in
  // the queue.
  readonly property string factsLine: {
    var parts = []
    if (state.bpm > 0) parts.push(strings.t("halo.bpm", "%1 BPM").arg(state.bpm))
    if (state.hasLoudness) parts.push(strings.t("halo.lufs", "%1 LUFS").arg(state.lufs.toFixed(1)))
    if (state.queueSize > 0) parts.push(strings.t("halo.place", "%1 of %2").arg(state.queueIndex).arg(state.queueSize))
    return parts.join("  ·  ")
  }

  Commons.Translations { id: strings; directory: Qt.resolvedUrl("i18n") }

  readonly property string stateLine: {
    if (!checked) return strings.t("halo.checking", "Looking for Halo")
    if (kind === "missing") return strings.t("halo.missing", "Halo isn't installed")
    if (kind === "offline") return strings.t("halo.offline", "Halo isn't running")
    if (kind === "idle") return strings.t("halo.idle", "Nothing is playing")
    return ""
  }

  function close() { popupOpen = false }
  // What the player holds, for the plugin's own test.
  readonly property alias playerContent: column

  // ---- talking to Halo ----------------------------------------------------

  // One command at a time. Something the user asked for during another
  // command runs as soon as that one ends (the latest wins, so dragging the
  // volume sends where the knob is, not every step on the way); the widget's
  // own polls are dropped instead of queued.
  property var queued: null
  property string inFlight: ""

  function request(name, value) {
    var isPoll = name === "status" || name === "lyrics"
    if (!installed && checked && name !== "status") return
    var argv = HaloStatus.argv(root.haloBinary, name, value)
    if (!argv) return
    if (proc.running) {
      if (!isPoll) queued = { name: name, value: value }
      return
    }
    proc.buffer = ""
    proc.overflow = false
    proc.started = false
    inFlight = name
    proc.command = argv
    proc.running = true
    deadline.restart()
  }

  function adopt(next) {
    if (next.key !== root.state.key) {
      root.lyrics = HaloStatus.emptyLyrics()
      root.lyricsWanted = true
    }
    root.state = next
    root.stamp = Date.now()
    root.now = root.stamp
    root.checked = true
  }

  // Shows a change at once; Halo's reply, a moment later, is what stays.
  function expect(changes) {
    var next = HaloStatus.withChange(root.state, changes)
    if (changes.position === undefined) next.position = root.shownPosition
    root.state = next
    root.stamp = Date.now()
    root.now = root.stamp
  }

  function finish(exitCode) {
    deadline.stop()
    var reply = HaloStatus.replyKind(inFlight)
    var text = proc.buffer
    proc.buffer = ""
    inFlight = ""
    if (reply === "status") {
      adopt(proc.overflow ? HaloStatus.emptyState("offline", "Halo's reply was too large to read")
        : HaloStatus.parse(text, exitCode))
    } else if (reply === "lyrics") {
      var words = proc.overflow ? HaloStatus.emptyLyrics() : HaloStatus.parseLyrics(text)
      root.lyrics = words
      root.lyricsWanted = words.timed || words.searching
    }
    runQueued()
  }

  function runQueued() {
    if (queued === null) return
    var next = queued
    queued = null
    Qt.callLater(function() { root.request(next.name, next.value) })
  }

  function openHalo() {
    if (!installed) {
      popupOpen = true
      return
    }
    // Detached and without a shell: Halo starts, or the running window comes
    // forward. The widget doesn't wait for it.
    Quickshell.execDetached([root.haloBinary, "open"])
    pollSoon.restart()
  }

  function seekTo(position) {
    if (!hasSong || state.duration <= 0) return
    var target = Math.max(0, Math.min(Math.floor(state.duration), Math.round(position)))
    expect({ position: target })
    request("seek", target)
  }

  function setVolume(volume) {
    if (!reachable) return
    var target = Math.max(0, Math.min(100, Math.round(volume)))
    if (target === state.volume) return
    expect({ volume: target })
    request("volume", target)
  }

  function setMode(name, on) {
    var change = {}
    change[name] = on
    expect(change)
    request(name, on ? "on" : "off")
  }

  function cycleRepeat() {
    var next = HaloStatus.nextRepeat(state.repeat)
    expect({ repeat: next })
    request("repeat", next)
  }

  function cycleSleep() {
    var next = HaloStatus.nextSleep(sleepLeft)
    expect({ sleep: next === "off" ? 0 : Number(next) * 60 })
    request("sleep", next)
  }

  Process {
    id: proc
    property string buffer: ""
    property bool overflow: false
    property bool started: false

    onStarted: started = true
    stdout: SplitParser {
      onRead: function(data) {
        if (proc.overflow) return
        if (proc.buffer.length + data.length + 1 > HaloStatus.MaxReplyBytes) {
          proc.overflow = true
          proc.signal(15)
          return
        }
        proc.buffer += data + "\n"
      }
    }
    onExited: function(exitCode) { root.finish(exitCode) }
    onRunningChanged: {
      // Quickshell reports a program that couldn't be started (not
      // installed) by stopping without ever starting or exiting.
      if (!running && !started) {
        deadline.stop()
        root.inFlight = ""
        root.state = HaloStatus.emptyState("missing", "")
        root.lyrics = HaloStatus.emptyLyrics()
        root.checked = true
        root.queued = null
      }
    }
  }

  // The `halo` command gives up on its own after three seconds; this is the
  // backstop if it somehow doesn't.
  Timer {
    id: deadline
    interval: 5000
    onTriggered: if (proc.running) proc.signal(9)
  }

  // The clock runs on its own between replies, so the poll only has to catch
  // changes: a new song, a pause, a setting flipped in the Halo window.
  Timer {
    id: poll
    interval: root.popupOpen ? 2000 : root.playing ? 3000 : root.reachable ? 6000 : 15000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: root.request("status")
  }

  // The line being sung, only while the player is open and the song has
  // lyrics with times.
  Timer {
    interval: 1000
    repeat: true
    running: root.popupOpen && root.hasSong && root.showLyrics && root.lyricsWanted
    triggeredOnStart: true
    onTriggered: root.request("lyrics")
  }

  // A moment after `halo open`, so the bar catches the window coming up.
  Timer {
    id: pollSoon
    interval: 1200
    onTriggered: root.request("status")
  }

  onPopupOpenChanged: if (popupOpen) request("status")

  // ---- the bar ------------------------------------------------------------

  visible: true
  implicitWidth: button.implicitWidth + (vertical ? 0 : artRoom)
  implicitHeight: button.implicitHeight

  function pressedWith(mouseButton) {
    if (mouseButton === Qt.MiddleButton) { if (root.hasSong) root.request("toggle") }
    else if ((mouseButton === Qt.RightButton) !== root.clickOpensHalo) root.openHalo()
    else root.popupOpen = !root.popupOpen
  }

  function wheeled(delta) {
    if (delta === 0) return
    if (root.scrollVolume) {
      if (root.reachable && root.state.volume >= 0) root.setVolume(root.state.volume + (delta > 0 ? 5 : -5))
      return
    }
    if (!root.hasSong) return
    if (delta > 0) root.request("previous")
    else root.request("next")
  }

  // The album cover, when Halo has one: a small square before the song. It
  // takes clicks and the wheel the same as the rest of the widget.
  Item {
    id: artBox
    visible: root.artShown
    x: Style.spacing.sm
    width: root.artSide
    height: root.artSide
    anchors.verticalCenter: parent.verticalCenter
    opacity: root.playing ? 1.0 : 0.7

    SizedImage {
      id: barArt
      anchors.fill: parent
      crop: true
      url: root.showArtwork && !root.vertical ? root.coverUrl : ""
    }

    MouseArea {
      anchors.fill: parent
      acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
      cursorShape: Qt.PointingHandCursor
      onPressed: function(mouse) { root.pressedWith(mouse.button) }
      onWheel: function(wheel) { root.wheeled(wheel.angleDelta.y) }
    }
  }

  BarIconButton {
    id: button
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    anchors.right: parent.right
    width: parent.width - (vertical ? 0 : root.artRoom)
    bar: root.bar
    text: "\u{f075a}"   // nf-md-music_note
    iconComponent: root.dancing ? equalizer : null
    labelText: root.barLabel
    // BarIconButton is one icon slot wide unless told otherwise, and a
    // song title would spill out of both sides of it.
    fixedWidth: vertical ? -1
      : root.barLabel === "" ? slotSize
      : Math.ceil(canvasSize + labelMetrics.advanceWidth + Style.space(4) + Style.spacing.controlPaddingX * 2)
    dimmed: !root.reachable
    tooltipText: root.hasSong ? root.songLine : root.stateLine
    onPressed: function(mouseButton) { root.pressedWith(mouseButton) }
    onWheelMoved: function(delta) { root.wheeled(delta) }
  }

  // How far the song is, as a line along the bottom of the widget.
  Rectangle {
    id: progressTrack
    visible: root.progressShown
    anchors.bottom: parent.bottom
    x: Style.spacing.sm
    height: Math.max(1, Math.round(Style.space(2)))
    width: Math.max(0, parent.width - Style.spacing.sm * 2)
    color: Qt.rgba(root.fg.r, root.fg.g, root.fg.b, 0.22)
    antialiasing: false

    Rectangle {
      height: parent.height
      width: Math.round(parent.width * root.fraction)
      color: Commons.Color.accent
      opacity: root.playing ? 1.0 : 0.6
      antialiasing: false
    }
  }

  // Three bars on whole pixels, in a box as big as the note it replaces (the
  // box keeps the icon's measured size steady while the bars move).
  Component {
    id: equalizer
    Rectangle {
      id: box
      readonly property int side: Math.max(5, Math.round(button.visualIconSize))
      readonly property int bw: Math.max(1, Math.round(side / 5))
      readonly property int gap: Math.max(1, Math.floor(bw / 2))
      readonly property int inset: Math.floor((side - bw * 3 - gap * 2) / 2)
      width: side
      height: side
      color: "transparent"
      Repeater {
        model: 3
        Rectangle {
          required property int index
          x: box.inset + index * (box.bw + box.gap)
          width: box.bw
          height: Math.max(box.bw, Math.round(box.side * root.eqLevels[index][root.eqStep] / 6))
          y: box.side - height
          color: button.foreground
          antialiasing: false
        }
      }
    }
  }

  TextMetrics {
    id: labelMetrics
    font.family: button.fontFamily
    font.pixelSize: button.fontSize
    text: root.barLabel
  }


  // ---- the player ---------------------------------------------------------

  // A mode button: lit while the mode is on.
  component ModeButton: Button {
    foreground: root.fg
    fontFamily: root.face
    fontSize: Style.font.bodySmall
    horizontalPadding: Style.spacing.sm
    bordered: true
    opacity: enabled ? 1.0 : 0.4
  }

  PopupCard {
    id: popup
    anchorItem: root
    bar: root.bar
    owner: root
    open: root.popupOpen
    contentWidth: popup.fittedContentWidth(Style.space(340))
    contentHeight: popup.fittedContentHeight(column.implicitHeight)

    Column {
      id: column
      anchors.fill: parent
      spacing: Style.space(10)

      // The cover, and what is playing.
      Row {
        id: header
        spacing: Style.space(12)
        width: parent.width
        readonly property int coverSide: root.hasSong ? Style.space(92) : Style.space(48)

        BorderSurface {
          id: coverBox
          width: header.coverSide
          height: header.coverSide
          radius: Style.spacing.labelGap
          color: Style.normalFillFor(root.fg, Commons.Color.accent)
          borderSpec: Border.controlSpec("normal", root.fg, Commons.Color.accent)
          readonly property bool pictured: root.hasSong && root.state.cover !== "" && cover.status === Image.Ready

          Text {
            anchors.centerIn: parent
            visible: !coverBox.pictured
            textFormat: Text.PlainText
            text: "\u{f075a}"
            color: root.reachable ? Commons.Color.accent : root.fg
            font.family: root.face
            font.pixelSize: root.hasSong ? Style.font.display : Style.font.displayLarge
          }

          // The picture Halo cut and cached for the album: a local file,
          // never a link.
          SizedImage {
            id: cover
            anchors.fill: parent
            anchors.margins: Math.max(1, Style.space(1))
            crop: true
            visible: coverBox.pictured
            url: root.coverUrl
          }
        }

        Column {
          spacing: Style.space(3)
          width: parent.width - header.coverSide - header.spacing
          anchors.verticalCenter: parent.verticalCenter

          Text {
            textFormat: Text.PlainText
            text: root.hasSong ? root.state.title : root.stateLine
            color: root.fg
            font.family: root.face
            font.pixelSize: Style.font.subtitle
            font.bold: true
            elide: Text.ElideRight
            wrapMode: Text.Wrap
            maximumLineCount: 2
            width: parent.width
          }

          Text {
            textFormat: Text.PlainText
            text: root.hasSong ? root.state.artist
              : root.kind === "missing" ? strings.t("halo.missingHint", "Install the Halo app; see its README.")
              : root.kind === "offline" && root.checked ? strings.t("halo.offlineHint", "Click Open Halo to start it.")
              : ""
            color: root.fgSoft
            font.family: root.face
            font.pixelSize: Style.font.bodySmall
            elide: Text.ElideRight
            wrapMode: root.hasSong ? Text.NoWrap : Text.Wrap
            maximumLineCount: 3
            width: parent.width
            visible: text !== ""
          }

          Text {
            textFormat: Text.PlainText
            text: root.hasSong ? root.state.album : ""
            color: root.fgFaint
            font.family: root.face
            font.pixelSize: Style.font.caption
            elide: Text.ElideRight
            width: parent.width
            visible: text !== ""
          }

          Text {
            textFormat: Text.PlainText
            text: root.hasSong ? root.factsLine : ""
            color: root.fgFaint
            font.family: root.face
            font.pixelSize: Style.font.caption
            elide: Text.ElideRight
            width: parent.width
            visible: text !== ""
          }
        }
      }

      // Where the song is. Drag, click or scroll to move through it.
      Column {
        width: parent.width
        spacing: 0
        visible: root.hasSong && root.state.duration > 0

        PanelSlider {
          id: seekSlider
          bar: root.bar
          width: parent.width
          minimum: 0
          maximum: Math.max(1, root.state.duration)
          step: 5
          integer: true
          value: root.shownPosition
          fillColor: Commons.Color.accent
          onReleased: function(v) { root.seekTo(v) }
        }

        Item {
          width: parent.width
          height: elapsedText.implicitHeight
          // The clock reads left to right in every language.
          LayoutMirroring.enabled: false

          Text {
            id: elapsedText
            anchors.left: parent.left
            anchors.leftMargin: seekSlider.knobSize / 2
            textFormat: Text.PlainText
            text: HaloStatus.clock(seekSlider.dragging ? seekSlider.liveValue : root.shownPosition)
            color: seekSlider.dragging ? root.fg : root.fgFaint
            font.family: root.face
            font.pixelSize: Style.font.caption
          }

          Text {
            anchors.right: parent.right
            anchors.rightMargin: seekSlider.knobSize / 2
            textFormat: Text.PlainText
            text: HaloStatus.clock(root.state.duration)
            color: root.fgFaint
            font.family: root.face
            font.pixelSize: Style.font.caption
          }
        }
      }

      Row {
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Style.space(6)
        visible: root.reachable
        // Transport controls keep previous, play, next in every language.
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true

        Button {
          iconText: "\u{f049d}"   // nf-md-shuffle
          tooltipText: root.state.shuffle ? strings.t("halo.shuffleOn", "Shuffle is on") : strings.t("halo.shuffleOff", "Shuffle is off")
          foreground: root.fg
          selected: root.state.shuffle
          anchors.verticalCenter: parent.verticalCenter
          onClicked: root.setMode("shuffle", !root.state.shuffle)
        }

        Button {
          iconText: "\u{f04ae}"   // nf-md-skip_previous
          tooltipText: strings.t("halo.previous", "Previous")
          foreground: root.fg
          enabled: root.hasSong
          opacity: enabled ? 1.0 : 0.4
          anchors.verticalCenter: parent.verticalCenter
          onClicked: root.request("previous")
        }

        Button {
          iconText: root.playing ? "\u{f03e4}" : "\u{f040a}"   // pause / play
          tooltipText: root.playing ? strings.t("halo.pause", "Pause") : strings.t("halo.play", "Play")
          foreground: root.fg
          horizontalPadding: Style.spacing.panelGap
          iconSize: Style.font.iconLarge
          enabled: root.hasSong
          opacity: enabled ? 1.0 : 0.4
          anchors.verticalCenter: parent.verticalCenter
          onClicked: root.request("toggle")
        }

        Button {
          iconText: "\u{f04ad}"   // nf-md-skip_next
          tooltipText: strings.t("halo.next", "Next")
          foreground: root.fg
          enabled: root.hasSong
          opacity: enabled ? 1.0 : 0.4
          anchors.verticalCenter: parent.verticalCenter
          onClicked: root.request("next")
        }

        Button {
          iconText: root.state.repeat === "one" ? "\u{f0458}" : "\u{f0456}"   // repeat once / repeat
          tooltipText: root.state.repeat === "one" ? strings.t("halo.repeatOne", "Repeating this song")
            : root.state.repeat === "all" ? strings.t("halo.repeatAll", "Repeating the queue")
            : strings.t("halo.repeatOff", "Repeat is off")
          foreground: root.fg
          selected: root.state.repeat !== "off"
          anchors.verticalCenter: parent.verticalCenter
          onClicked: root.cycleRepeat()
        }
      }

      // What makes Halo Halo: Blend, Drift, the sleep timer and moments.
      Row {
        id: modes
        width: parent.width
        spacing: Style.space(6)
        visible: root.reachable
        readonly property real each: (width - spacing * 3) / 4

        ModeButton {
          width: modes.each
          text: strings.t("halo.blend", "Blend")
          tooltipText: strings.t("halo.blendTip", "Mix each song into the next on the beat")
          selected: root.state.blend
          onClicked: root.setMode("blend", !root.state.blend)
        }

        ModeButton {
          width: modes.each
          text: strings.t("halo.drift", "Drift")
          tooltipText: strings.t("halo.driftTip", "Keep playing similar music when the queue ends")
          selected: root.state.drift
          onClicked: root.setMode("drift", !root.state.drift)
        }

        ModeButton {
          width: modes.each
          text: root.sleepLeft > 0
            ? strings.t("halo.sleepLeft", "%1 min").arg(Math.max(1, Math.ceil(root.sleepLeft / 60)))
            : strings.t("halo.sleep", "Sleep")
          tooltipText: root.sleepLeft > 0
            ? strings.t("halo.sleepOnTip", "Pauses when the time is up. Click for longer, then off.")
            : strings.t("halo.sleepTip", "Fade out and pause after 15, 30, 60 or 90 minutes")
          selected: root.sleepLeft > 0
          onClicked: root.cycleSleep()
        }

        ModeButton {
          width: modes.each
          text: strings.t("halo.moment", "Moment")
          tooltipText: strings.t("halo.momentTip", "Save this second of the song")
          enabled: root.hasSong
          onClicked: root.request("moment")
        }
      }

      // Halo's own volume, not the system's.
      Row {
        width: parent.width
        spacing: Style.space(6)
        visible: root.reachable && root.state.volume >= 0
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true

        Text {
          id: volumeIcon
          anchors.verticalCenter: parent.verticalCenter
          width: Style.space(20)
          horizontalAlignment: Text.AlignHCenter
          textFormat: Text.PlainText
          readonly property int level: volumeSlider.dragging ? Math.round(volumeSlider.liveValue) : root.state.volume
          text: level <= 0 ? "\u{f0581}" : level < 34 ? "\u{f057f}" : level < 67 ? "\u{f0580}" : "\u{f057e}"
          color: root.fgSoft
          font.family: root.face
          font.pixelSize: Style.font.icon
        }

        PanelSlider {
          id: volumeSlider
          bar: root.bar
          anchors.verticalCenter: parent.verticalCenter
          width: parent.width - volumeIcon.width - volumeText.width - parent.spacing * 2
          minimum: 0
          maximum: 100
          step: 5
          integer: true
          value: Math.max(0, root.state.volume)
          onMoved: function(v) { root.setVolume(v) }
        }

        Text {
          id: volumeText
          anchors.verticalCenter: parent.verticalCenter
          width: Style.space(28)
          horizontalAlignment: Text.AlignRight
          textFormat: Text.PlainText
          text: volumeIcon.level
          color: root.fgSoft
          font.family: root.face
          font.pixelSize: Style.font.caption
        }
      }

      // The line being sung, and the one coming.
      Column {
        width: parent.width
        spacing: Style.space(2)
        visible: root.showLyrics && root.hasSong && (root.lyrics.line !== "" || root.lyrics.next !== "")

        Text {
          textFormat: Text.PlainText
          text: root.lyrics.line
          color: Commons.Color.accent
          font.family: root.face
          font.pixelSize: Style.font.body
          font.bold: true
          wrapMode: Text.Wrap
          maximumLineCount: 2
          elide: Text.ElideRight
          width: parent.width
          visible: text !== ""
        }

        Text {
          textFormat: Text.PlainText
          text: root.lyrics.next
          color: root.fgFaint
          font.family: root.face
          font.pixelSize: Style.font.bodySmall
          elide: Text.ElideRight
          width: parent.width
          visible: text !== ""
        }
      }

      Text {
        textFormat: Text.PlainText
        visible: root.hasSong && root.state.hasNext && root.state.nextTitle !== ""
        text: strings.t("halo.upNext", "Up next: %1").arg(root.state.nextTitle + (root.state.nextArtist ? " · " + root.state.nextArtist : ""))
        color: root.fgSoft
        font.family: root.face
        font.pixelSize: Style.font.caption
        elide: Text.ElideRight
        width: parent.width
      }

      // Something Halo wants said: a file it couldn't play, say.
      Text {
        textFormat: Text.PlainText
        visible: root.reachable && root.state.problem !== ""
        text: root.state.problem
        color: root.bar ? root.bar.urgent : Commons.Color.urgent
        font.family: root.face
        font.pixelSize: Style.font.caption
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
        width: parent.width
      }

      PanelSeparator {
        foreground: root.fg
      }

      Row {
        id: footer
        width: parent.width
        spacing: Style.space(6)

        Button {
          width: footer.width - (visualButton.visible ? visualButton.width + footer.spacing : 0)
          text: strings.t("halo.open", "Open Halo")
          foreground: root.fg
          bordered: true
          enabled: root.installed
          opacity: enabled ? 1.0 : 0.4
          onClicked: {
            root.openHalo()
            root.popupOpen = false
          }
        }

        Button {
          id: visualButton
          visible: root.reachable
          text: strings.t("halo.visual", "Visual")
          tooltipText: strings.t("halo.visualTip", "Switch the visual in the Halo window")
          foreground: root.fg
          bordered: true
          onClicked: root.request("visual", "next")
        }
      }
    }
  }
}
