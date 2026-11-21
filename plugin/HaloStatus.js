.pragma library

// Turns what `/usr/bin/halo <command> --json` printed into the small, bounded
// state the bar widget shows. Pure functions, no I/O, so they can be tested
// outside the shell (tests/plugin/status-test.mjs).

// The most of one reply the widget will read. A `halo status --json` line is
// about 1 KiB; anything far bigger is not a status reply.
var MaxReplyBytes = 64 * 1024
// The longest title, artist or album the widget keeps.
var MaxField = 300

function clip(value) {
  var text = typeof value === "string" ? value : ""
  // Control characters (newlines, tabs, escapes) have no place in a bar label.
  text = text.replace(/[\u0000-\u001f\u007f]+/g, " ")
  return text.length > MaxField ? text.slice(0, MaxField - 1) + "…" : text
}

function seconds(value) {
  var n = Number(value)
  return isFinite(n) && n > 0 ? Math.min(n, 864000) : 0
}

// "missing"  - /usr/bin/halo isn't installed
// "offline"  - Halo is installed but no window is running (or it didn't answer)
// "idle"     - Halo is running with nothing loaded
// "paused" / "playing"
function emptyState(kind, message) {
  return {
    kind: kind,
    message: clip(message || ""),
    title: "", artist: "", album: "",
    position: 0, duration: 0,
    hasNext: false, nextTitle: "", nextArtist: ""
  }
}

// text: everything the command wrote to stdout; exitCode: its exit code.
function parse(text, exitCode) {
  var raw = typeof text === "string" ? text : ""
  if (raw.length > MaxReplyBytes)
    return emptyState("offline", "Halo's reply was too large to read")
  var line = raw.trim()
  var reply = null
  if (line.length > 0) {
    try { reply = JSON.parse(line) } catch (e) { reply = null }
  }
  if (!reply || typeof reply !== "object" || Array.isArray(reply))
    return emptyState("offline", exitCode === 0 ? "Halo's reply wasn't readable" : "")
  if (reply.ok !== true)
    return emptyState("offline", typeof reply.error === "string" ? reply.error : "")
  var s = reply.status
  if (!s || typeof s !== "object" || Array.isArray(s))
    return emptyState("idle", "")
  var path = typeof s.path === "string" ? s.path : ""
  if (path === "")
    return emptyState("idle", "")
  var state = emptyState(s.playing === true ? "playing" : "paused", "")
  state.title = clip(s.title) || clip(path.split("/").pop())
  state.artist = clip(s.artist)
  state.album = clip(s.album)
  state.position = seconds(s.position)
  state.duration = seconds(s.duration)
  if (state.duration > 0 && state.position > state.duration) state.position = state.duration
  var next = s.next
  if (next && typeof next === "object" && !Array.isArray(next)) {
    state.hasNext = true
    state.nextTitle = clip(next.title)
    state.nextArtist = clip(next.artist)
  }
  return state
}

// "3:07", or "1:02:03" past an hour.
function clock(value) {
  var total = Math.floor(seconds(value))
  var h = Math.floor(total / 3600)
  var m = Math.floor((total % 3600) / 60)
  var s = total % 60
  var ss = (s < 10 ? "0" : "") + s
  if (h > 0) return h + ":" + (m < 10 ? "0" : "") + m + ":" + ss
  return m + ":" + ss
}

// The commands the widget may run, and nothing else. Each maps to a fixed
// argv: no shell, no user text in it.
var Commands = {
  status: ["status", "--json"],
  toggle: ["toggle", "--json"],
  next: ["next", "--json"],
  previous: ["previous", "--json"]
}

function argv(binary, name) {
  if (typeof name !== "string" || !Object.prototype.hasOwnProperty.call(Commands, name)) return null
  var args = Commands[name]
  return [binary].concat(args)
}
