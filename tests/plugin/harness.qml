import QtQuick
import Quickshell
import Quickshell.Io

// Loads the real bar widget (plugin/HaloWidget.qml) with Omarchy's own
// qs.Commons and qs.Ui, a stand-in bar, and a stub `halo`, then walks it
// through every state. run-widget-test.sh builds the folder this runs from.
// Each check prints "CHECK PASS|FAIL <name>"; the last line is "DONE <n> <failures>".
ShellRoot {
  id: shell

  property string dir: Quickshell.env("HALO_WIDGET_TEST_DIR")
  property string realHalo: Quickshell.env("HALO_WIDGET_TEST_REAL") === "1" ? "/usr/bin/halo" : ""
  property int failures: 0
  property int checks: 0

  function check(name, ok, detail) {
    checks++
    if (!ok) failures++
    console.log("CHECK " + (ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  (" + detail + ")"))
  }

  QtObject {
    id: fakeBar
    property color foreground: "#e0def4"
    property color background: "#191724"
    property color barForeground: "#e0def4"
    property color urgent: "#eb6f92"
    property string fontFamily: "monospace"
    property string position: "top"
    property bool vertical: false
    property int barSize: 26
    property bool barHidden: false
    property bool foregroundAnimationEnabled: true
    property var activePopout: null
    function run(command) { console.log("UNEXPECTED bar.run " + command) }
    function showTooltip(target, text) {}
    function hideTooltip(target) {}
    function requestPopout(owner) { activePopout = owner }
    function releasePopout(owner) { if (activePopout === owner) activePopout = null }
    function registerClickTarget(t) {}
    function unregisterClickTarget(t) {}
    function moduleWidgets(name) { return [] }
  }

  FloatingWindow {
    id: window
    implicitWidth: 420
    implicitHeight: 40
    color: fakeBar.background

    // What the screenshot grabs: a window's own contentItem has no QML
    // engine of its own under Quickshell, so grabToImage refuses it.
    Rectangle {
      id: backdrop
      anchors.fill: parent
      color: fakeBar.background

    Loader {
      id: loader
      anchors.verticalCenter: parent.verticalCenter
      x: 8
      source: Qt.resolvedUrl("plugin/HaloWidget.qml")
      onLoaded: {
        item.bar = fakeBar
        item.settings = ({})
        item.haloBinary = shell.dir + "/no-such-halo"
        steps.start()
      }
      onStatusChanged: if (status === Loader.Error) console.log("CHECK FAIL load  (" + sourceComponent + ")")
    }
    }
  }

  property var w: loader.item
  property int step: 0
  property var plan: [
    // [delay ms before the check, action, check]
    [1500, function() {}, function() {
      check("missing binary shows 'missing'", w.kind === "missing" && w.checked, w.kind)
      check("missing: label is empty", w.barLabel === "", w.barLabel)
      w.haloBinary = shell.dir + "/halo"
      writeMode("offline")
    }],
    [300, function() { w.request("status") }, null],
    [1500, function() {}, function() {
      check("not running shows 'offline'", w.kind === "offline", w.kind)
      check("offline: dimmed text says so", w.stateLine.indexOf("running") >= 0, w.stateLine)
      writeMode("playing")
    }],
    [300, function() { w.request("status") }, null],
    [1500, function() {}, function() {
      check("playing", w.kind === "playing", w.kind)
      check("bar label is title and artist", w.barLabel === "Night Drive  ·  Tape Deck", w.barLabel)
      check("glyph escapes decode", String.fromCodePoint(0xf075a) === "\u{f075a}", "")
      check("the bar icon dances while a song plays", w.dancing === true, String(w.dancing))
      check("status carries volume, tempo, place and cover", w.state.volume === 50 && w.state.bpm === 99
            && w.state.queueIndex === 3 && w.state.cover === shell.dir + "/cover.png", JSON.stringify(w.state))
      check("the progress line shows under the label", w.progressShown === true, String(w.progressShown))
      check("the album cover shows in the bar", w.artShown === true && w.implicitWidth > w.artSide, String(w.artShown))
      w.setVolume(70)
    }],
    [1500, function() {}, function() {
      check("volume ran `halo volume 70 --json`", lastCall.indexOf("volume 70 --json") >= 0, lastCall)
      check("the reply holds the new volume", w.state.volume === 70, w.state.volume)
      check("the clock runs between replies", w.shownPosition > w.state.position, w.shownPosition + " vs " + w.state.position)
      w.seekTo(100.4)
    }],
    [1500, function() {}, function() {
      check("seek ran `halo seek 100 --json`", lastCall.indexOf("seek 100 --json") >= 0, lastCall)
      check("the song is where it was sent", w.state.position === 100, w.state.position)
      w.setMode("shuffle", true)
    }],
    [1500, function() {}, function() {
      check("shuffle ran `halo shuffle on --json`", lastCall.indexOf("shuffle on --json") >= 0, lastCall)
      check("shuffle is on", w.state.shuffle === true, String(w.state.shuffle))
      w.cycleRepeat()
    }],
    [1500, function() {}, function() {
      check("repeat ran `halo repeat all --json`", lastCall.indexOf("repeat all --json") >= 0 && w.state.repeat === "all", lastCall)
      w.cycleSleep()
    }],
    [1500, function() {}, function() {
      check("sleep ran `halo sleep 15 --json`", lastCall.indexOf("sleep 15 --json") >= 0, lastCall)
      check("the sleep timer counts down", w.sleepLeft > 800 && w.sleepLeft <= 900, w.sleepLeft)
      w.request("visual", "next")
    }],
    [1500, function() {}, function() {
      check("visual ran `halo visual next --json`", lastCall.indexOf("visual next --json") >= 0, lastCall)
      check("a reply without a status leaves the song showing", w.kind === "playing" && w.state.title === "Night Drive", w.kind)
      w.request("lyrics")
    }],
    [1500, function() {}, function() {
      check("lyrics: the line being sung and the next", w.lyrics.line === "Second line" && w.lyrics.next === "Third line", JSON.stringify(w.lyrics))
      w.request("volume", "50; rm -rf /")
      w.request("playlist", "delete")
      w.request("seek", "+1e9")
    }],
    [1500, function() {}, function() {
      check("values and commands off the list never run", lastCall.indexOf("rm") < 0 && lastCall.indexOf("playlist") < 0
            && lastCall.indexOf("e9") < 0 && w.state.volume === 70, lastCall)
      w.request("toggle")
    }],
    [1500, function() {}, function() {
      check("toggle ran `halo toggle --json`", lastCall.indexOf("toggle --json") >= 0, lastCall)
      check("toggle's own reply updates the state (paused)", w.kind === "paused", w.kind)
      check("paused: the bar icon is the still note", w.dancing === false, String(w.dancing))
      w.settings = ({ artwork: false })
      check("\"artwork\": false takes the cover out of the bar", w.artShown === false && w.artRoom === 0, String(w.artShown))
      w.settings = ({})
      w.request("next")
    }],
    [1500, function() {}, function() {
      check("next ran `halo next --json`", lastCall.indexOf("next --json") >= 0, lastCall)
      writeMode("markup")
    }],
    [300, function() { w.request("status") }, null],
    [1500, function() {}, function() {
      check("a song with no cover has none in the bar", w.artShown === false, String(w.artShown))
      check("markup-shaped title stays literal text", w.state.title === "<b>bold</b><img src=\"https://example.invalid/x\">", w.state.title)
      check("long label is cut to 32 characters", w.barLabel.length <= 32, w.barLabel.length)
      writeMode("flood")
    }],
    [300, function() { w.request("status") }, null],
    [2500, function() {}, function() {
      check("a 200 KiB reply is refused (offline), not parsed", w.kind === "offline" && w.state.title === "", w.kind)
      writeMode("hang")
    }],
    [300, function() { w.request("status") }, null],
    [7000, function() {}, function() {
      check("a hung command is killed after 5 s (offline)", w.kind === "offline", w.kind)
      writeMode("playing")
      w.request("status")
      w.request("toggle")   // asked for during the poll: queued, then run
    }],
    [2500, function() {}, function() {
      check("an action during a poll is queued and runs after it", lastCall.indexOf("toggle --json") >= 0, lastCall)
      if (shell.realHalo !== "") {
        w.haloBinary = shell.realHalo
        w.request("status")
      }
    }],
    [2500, function() {}, function() {
      if (shell.realHalo !== "")
        check("the real /usr/bin/halo status --json reads (kind " + w.kind + ")",
              ["idle", "paused", "playing", "offline"].indexOf(w.kind) >= 0, w.kind)
      w.haloBinary = shell.dir + "/halo"
      writeMode("playing")
      w.request("status")
    }],
    [1500, function() { w.popupOpen = true }, function() {
      check("the player opens", w.popupOpen === true, String(w.popupOpen))
      check("the player has a height", w.playerContent.implicitHeight > 100, w.playerContent.implicitHeight)
      // The player's own window may not draw offscreen; the picture is a bonus.
      var done = function() {
        var grabbing = backdrop.grabToImage(function(result) {
          result.saveToFile(shell.dir + "/bar.png")
          console.log("GRAB " + shell.dir + "/bar.png")
          console.log("DONE " + shell.checks + " " + shell.failures)
        })
        if (!grabbing) console.log("DONE " + shell.checks + " " + shell.failures)
      }
      var player = false
      try {
        player = w.playerContent.grabToImage(function(result) {
          result.saveToFile(shell.dir + "/player.png")
          done()
        })
      } catch (e) { player = false }
      if (!player) done()
    }]
  ]

  // The last few `halo` calls, newest last. Not only the very last one: the
  // widget's own status poll can land just after an action.
  property string lastCall: ""
  property var recent: []

  function writeMode(mode) {
    writer.command = ["/bin/sh", "-c", "printf %s \"$1\" > \"$2/mode\"", "sh", mode, shell.dir]
    writer.running = true
  }

  Process {
    id: writer
  }
  Process {
    id: reader
    command: ["/bin/sh", "-c", "tail -n 3 \"$1/calls\" 2>/dev/null", "sh", shell.dir]
    onStarted: shell.recent = []
    stdout: SplitParser { onRead: function(line) { shell.recent = shell.recent.concat([line]); shell.lastCall = shell.recent.join(" | ") } }
  }

  Timer {
    id: steps
    interval: 10
    onTriggered: {
      if (shell.step >= shell.plan.length) return
      var entry = shell.plan[shell.step]
      entry[1]()
      interval = entry[0]
      checkTimer.entry = entry
      checkTimer.interval = entry[0]
      checkTimer.restart()
      reader.running = true
    }
  }
  Timer {
    id: checkTimer
    property var entry: null
    onTriggered: {
      reader.running = false
      reader.running = true
      settle.restart()
    }
  }
  Timer {
    id: settle
    interval: 150
    onTriggered: {
      if (checkTimer.entry && checkTimer.entry[2]) checkTimer.entry[2]()
      shell.step++
      steps.restart()
    }
  }
}
