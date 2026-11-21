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
      w.request("toggle")
    }],
    [1500, function() {}, function() {
      check("toggle ran `halo toggle --json`", lastCall.indexOf("toggle --json") >= 0, lastCall)
      check("toggle's own reply updates the state (paused)", w.kind === "paused", w.kind)
      w.request("next")
    }],
    [1500, function() {}, function() {
      check("next ran `halo next --json`", lastCall.indexOf("next --json") >= 0, lastCall)
      writeMode("markup")
    }],
    [300, function() { w.request("status") }, null],
    [1500, function() {}, function() {
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
    [1500, function() {}, function() {
      var grabbing = backdrop.grabToImage(function(result) {
        result.saveToFile(shell.dir + "/bar.png")
        console.log("GRAB " + shell.dir + "/bar.png")
        console.log("DONE " + shell.checks + " " + shell.failures)
      })
      if (!grabbing) console.log("DONE " + shell.checks + " " + shell.failures)
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
