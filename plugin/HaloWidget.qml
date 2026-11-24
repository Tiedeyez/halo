import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Commons as Commons
import qs.Ui
import "HaloStatus.js" as HaloStatus

// Halo in the bar: what is playing, and play/pause, next and previous, with
// a click to bring the Halo window up. It talks to Halo only by running the
// `halo` command (`status`, `toggle`, `next`, `previous`, `open`) with a
// fixed argument list, never through a shell. It reads at most 64 KiB of
// each reply, gives a command five seconds, and writes no files, edits no
// configuration and opens no network connection.
BarWidget {
  id: root
  moduleName: "tiedeyez.halo"

  // The installed program. Absolute, so nothing earlier on PATH can stand in
  // for it. Not read from settings; the plugin's own test points it at a stub.
  property string haloBinary: "/usr/bin/halo"

  // What Halo last said, already bounded by HaloStatus.parse().
  property var state: HaloStatus.emptyState("offline", "")
  // Until the first reply the widget doesn't claim Halo is missing or off.
  property bool checked: false
  property bool popupOpen: false

  readonly property string kind: state.kind
  readonly property bool installed: kind !== "missing"
  readonly property bool reachable: kind === "idle" || kind === "paused" || kind === "playing"
  readonly property bool hasSong: kind === "paused" || kind === "playing"
  readonly property bool playing: kind === "playing"
  readonly property bool showTitle: setting("showTitle", true) !== false
  readonly property int maxLabelChars: Math.max(8, Math.min(80, Number(setting("maxTitleLength", 32)) || 32))

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

  readonly property string songLine: state.title + (state.artist ? "  ·  " + state.artist : "")
  readonly property string barLabel: {
    if (!showTitle || vertical || !hasSong) return ""
    var text = songLine
    return text.length > maxLabelChars ? text.slice(0, maxLabelChars - 1) + "…" : text
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

  // ---- talking to Halo ----------------------------------------------------

  // One command at a time; an action asked for during a status poll runs as
  // soon as the poll ends, and repeated polls are dropped.
  property string queuedCommand: ""

  function request(name) {
    if (!installed && checked && name !== "status") return
    if (proc.running) {
      if (name !== "status") queuedCommand = name
      return
    }
    var argv = HaloStatus.argv(root.haloBinary, name)
    if (!argv) return
    proc.buffer = ""
    proc.overflow = false
    proc.started = false
    proc.command = argv
    proc.running = true
    deadline.restart()
  }

  function finish(exitCode) {
    deadline.stop()
    var next = proc.overflow ? HaloStatus.emptyState("offline", "Halo's reply was too large to read")
      : HaloStatus.parse(proc.buffer, exitCode)
    proc.buffer = ""
    root.state = next
    root.checked = true
    runQueued()
  }

  function runQueued() {
    if (queuedCommand === "") return
    var name = queuedCommand
    queuedCommand = ""
    Qt.callLater(function() { root.request(name) })
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
        root.state = HaloStatus.emptyState("missing", "")
        root.checked = true
        root.queuedCommand = ""
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

  // Poll faster while the popup shows the clock, slower when Halo is off.
  Timer {
    id: poll
    interval: root.popupOpen ? 1000 : root.playing ? 3000 : root.reachable ? 6000 : 15000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: root.request("status")
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
  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  BarIconButton {
    id: button
    anchors.fill: parent
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
    onPressed: function(mouseButton) {
      if (mouseButton === Qt.RightButton) root.popupOpen = !root.popupOpen
      else if (mouseButton === Qt.MiddleButton) { if (root.hasSong) root.request("toggle") }
      else root.openHalo()
    }
    onWheelMoved: function(delta) {
      if (!root.hasSong) return
      if (delta > 0) root.request("previous")
      else if (delta < 0) root.request("next")
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

  // ---- the popup ----------------------------------------------------------

  PopupCard {
    id: popup
    anchorItem: root
    bar: root.bar
    owner: root
    open: root.popupOpen
    contentWidth: popup.fittedContentWidth(Style.space(300))
    contentHeight: popup.fittedContentHeight(column.implicitHeight)

    Column {
      id: column
      anchors.fill: parent
      spacing: Style.space(10)

      Row {
        spacing: Style.space(10)
        width: parent.width

        BorderSurface {
          width: Style.space(48)
          height: Style.space(48)
          radius: Style.spacing.labelGap
          color: Style.normalFillFor(root.bar ? root.bar.foreground : Commons.Color.foreground, Commons.Color.accent)
          borderSpec: Border.controlSpec("normal", root.bar ? root.bar.foreground : Commons.Color.foreground, Commons.Color.accent)

          Text {
            anchors.centerIn: parent
            textFormat: Text.PlainText
            text: "\u{f075a}"
            color: root.reachable ? Commons.Color.accent : (root.bar ? root.bar.foreground : Commons.Color.foreground)
            font.family: root.bar ? root.bar.fontFamily : Style.font.family
            font.pixelSize: Style.font.displayLarge
          }
        }

        Column {
          spacing: Style.space(3)
          width: parent.width - Style.space(58)
          anchors.verticalCenter: parent.verticalCenter

          Text {
            textFormat: Text.PlainText
            text: root.hasSong ? root.state.title : root.stateLine
            color: root.bar ? root.bar.foreground : Commons.Color.foreground
            font.family: root.bar ? root.bar.fontFamily : Style.font.family
            font.pixelSize: Style.font.subtitle
            font.bold: true
            elide: Text.ElideRight
            width: parent.width
          }

          Text {
            textFormat: Text.PlainText
            text: root.hasSong ? root.state.artist
              : root.kind === "missing" ? strings.t("halo.missingHint", "Install the Halo app; see its README.")
              : root.kind === "offline" && root.checked ? strings.t("halo.offlineHint", "Click Open Halo to start it.")
              : ""
            color: Qt.darker(root.bar ? root.bar.foreground : Commons.Color.foreground, 1.3)
            font.family: root.bar ? root.bar.fontFamily : Style.font.family
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
            color: Qt.darker(root.bar ? root.bar.foreground : Commons.Color.foreground, 1.6)
            font.family: root.bar ? root.bar.fontFamily : Style.font.family
            font.pixelSize: Style.font.caption
            elide: Text.ElideRight
            width: parent.width
            visible: text !== ""
          }
        }
      }

      // Where the song is, from the last reply.
      Column {
        width: parent.width
        spacing: Style.space(4)
        visible: root.hasSong && root.state.duration > 0

        Rectangle {
          width: parent.width
          height: Math.max(2, Style.space(3))
          radius: height / 2
          color: Qt.darker(root.bar ? root.bar.foreground : Commons.Color.foreground, 3)

          Rectangle {
            height: parent.height
            radius: parent.radius
            width: root.state.duration > 0 ? parent.width * Math.min(1, root.state.position / root.state.duration) : 0
            color: Commons.Color.accent
          }
        }

        Text {
          textFormat: Text.PlainText
          text: HaloStatus.clock(root.state.position) + " / " + HaloStatus.clock(root.state.duration)
          color: Qt.darker(root.bar ? root.bar.foreground : Commons.Color.foreground, 1.5)
          font.family: root.bar ? root.bar.fontFamily : Style.font.family
          font.pixelSize: Style.font.caption
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
          iconText: "\u{f04ae}"   // nf-md-skip_previous
          tooltipText: strings.t("halo.previous", "Previous")
          foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
          enabled: root.hasSong
          opacity: enabled ? 1.0 : 0.4
          onClicked: root.request("previous")
        }

        Button {
          iconText: root.playing ? "\u{f03e4}" : "\u{f040a}"   // pause / play
          tooltipText: root.playing ? strings.t("halo.pause", "Pause") : strings.t("halo.play", "Play")
          foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
          horizontalPadding: Style.spacing.panelGap
          iconSize: Style.font.iconLarge
          enabled: root.hasSong
          opacity: enabled ? 1.0 : 0.4
          onClicked: root.request("toggle")
        }

        Button {
          iconText: "\u{f04ad}"   // nf-md-skip_next
          tooltipText: strings.t("halo.next", "Next")
          foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
          enabled: root.hasSong
          opacity: enabled ? 1.0 : 0.4
          onClicked: root.request("next")
        }
      }

      Text {
        textFormat: Text.PlainText
        visible: root.hasSong && root.state.hasNext && root.state.nextTitle !== ""
        text: strings.t("halo.upNext", "Up next: %1").arg(root.state.nextTitle + (root.state.nextArtist ? " · " + root.state.nextArtist : ""))
        color: Qt.darker(root.bar ? root.bar.foreground : Commons.Color.foreground, 1.5)
        font.family: root.bar ? root.bar.fontFamily : Style.font.family
        font.pixelSize: Style.font.caption
        elide: Text.ElideRight
        width: parent.width
      }

      PanelSeparator {
        foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
      }

      Button {
        width: parent.width
        text: strings.t("halo.open", "Open Halo")
        foreground: root.bar ? root.bar.foreground : Commons.Color.foreground
        enabled: root.installed
        opacity: enabled ? 1.0 : 0.4
        onClicked: {
          root.openHalo()
          root.popupOpen = false
        }
      }
    }
  }
}
