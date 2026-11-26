.pragma library

// Turns what `/usr/bin/cymtx <command> --json` printed into the small, bounded
// state the bar widget shows, and builds the few command lines the widget may
// run. Pure functions, no I/O, so they can be tested outside the shell
// (tests/plugin/status-test.mjs).

// The most of one reply the widget will read. A `cymtx status --json` line is
// about 1 KiB and a song's lyrics a few KiB; anything far bigger is neither.
var MaxReplyBytes = 64 * 1024
// The longest title, artist, album or lyric line the widget keeps.
var MaxField = 300
// The longest file path the widget keeps (the cover CymtX cached).
var MaxPath = 1024

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

// A number inside [low, high], or `fallback` when it isn't a number at all.
function within(value, low, high, fallback) {
  if (typeof value !== "number" || !isFinite(value)) return fallback
  return Math.max(low, Math.min(high, value))
}

// The cover CymtX cut and cached for the album: an absolute path to a JPEG or
// PNG, or nothing. Anything else (a URL, a relative path, another kind of
// file) is dropped, so the widget only ever opens a local picture.
function coverPath(value) {
  if (typeof value !== "string" || value.length < 2 || value.length > MaxPath) return ""
  if (value.charAt(0) !== "/" || value.indexOf("/../") >= 0) return ""
  if (/[\u0000-\u001f\u007f]/.test(value)) return ""
  if (!/\.(jpe?g|png)$/i.test(value)) return ""
  return value
}

// "missing"  - /usr/bin/cymtx isn't installed
// "offline"  - CymtX is installed but no window is running (or it didn't answer)
// "idle"     - CymtX is running with nothing loaded
// "paused" / "playing"
function emptyState(kind, message) {
  return {
    kind: kind,
    message: clip(message || ""),
    // Changes when the song does (the end of its path).
    key: "",
    title: "", artist: "", album: "",
    position: 0, duration: 0,
    // -1 when CymtX didn't say.
    volume: -1,
    shuffle: false, repeat: "off", blend: false, drift: false,
    // Seconds until the sleep timer pauses; 0 when it is off.
    sleep: 0,
    // 0 when the song hasn't been analysed.
    bpm: 0,
    hasLoudness: false, lufs: 0,
    // Place in the queue, from 1; 0 when CymtX didn't say.
    queueIndex: 0, queueSize: 0,
    cover: "",
    problem: "",
    hasNext: false, nextTitle: "", nextArtist: ""
  }
}

// text: everything the command wrote to stdout; exitCode: its exit code.
function parse(text, exitCode) {
  var raw = typeof text === "string" ? text : ""
  if (raw.length > MaxReplyBytes)
    return emptyState("offline", "CymtX's reply was too large to read")
  var line = raw.trim()
  var reply = null
  if (line.length > 0) {
    try { reply = JSON.parse(line) } catch (e) { reply = null }
  }
  if (!reply || typeof reply !== "object" || Array.isArray(reply))
    return emptyState("offline", exitCode === 0 ? "CymtX's reply wasn't readable" : "")
  if (reply.ok !== true)
    return emptyState("offline", typeof reply.error === "string" ? reply.error : "")
  var s = reply.status
  if (!s || typeof s !== "object" || Array.isArray(s))
    return emptyState("idle", "")
  var path = typeof s.path === "string" ? s.path : ""
  var state = emptyState(path === "" ? "idle" : s.playing === true ? "playing" : "paused", "")
  // What CymtX is set to holds whether or not a song is loaded.
  state.volume = Math.round(within(s.volume, 0, 100, -1))
  state.shuffle = s.shuffle === true
  state.repeat = s.repeat === "all" || s.repeat === "one" ? s.repeat : "off"
  state.blend = s.blend === true
  state.drift = s.drift === true
  state.sleep = Math.floor(seconds(s.sleep))
  state.problem = clip(s.problem)
  if (path === "")
    return state
  state.key = path.slice(-MaxField)
  state.title = clip(s.title) || clip(path.split("/").pop())
  state.artist = clip(s.artist)
  state.album = clip(s.album)
  state.position = seconds(s.position)
  state.duration = seconds(s.duration)
  if (state.duration > 0 && state.position > state.duration) state.position = state.duration
  state.cover = coverPath(s.cover)
  var size = Math.floor(within(s.queue, 0, 10000000, 0))
  var index = Math.floor(within(s.index, -1, 10000000, -1))
  if (size > 0 && index >= 0 && index < size) {
    state.queueIndex = index + 1
    state.queueSize = size
  }
  var a = s.analysis
  if (a && typeof a === "object" && !Array.isArray(a)) {
    state.bpm = Math.round(within(a.bpm, 0, 999, 0))
    if (typeof a.lufs === "number" && isFinite(a.lufs) && a.lufs < 0 && a.lufs > -100) {
      state.hasLoudness = true
      state.lufs = Math.round(a.lufs * 10) / 10
    }
  }
  var next = s.next
  if (next && typeof next === "object" && !Array.isArray(next)) {
    state.hasNext = true
    state.nextTitle = clip(next.title)
    state.nextArtist = clip(next.artist)
  }
  return state
}

// The state with a few fields replaced: what the widget shows between asking
// CymtX for a change and CymtX's reply.
function withChange(state, changes) {
  var copy = {}
  var k
  for (k in state) copy[k] = state[k]
  for (k in changes) copy[k] = changes[k]
  return copy
}

// What `cymtx lyrics --json` printed, cut down to the line being sung and the
// one after it. `timed` is false for plain words with no times (and for no
// lyrics at all): there is no "line being sung" then, and the widget shows
// nothing and stops asking until the song changes.
function emptyLyrics() {
  return { timed: false, searching: false, line: "", next: "" }
}

function parseLyrics(text) {
  var raw = typeof text === "string" ? text : ""
  var out = emptyLyrics()
  if (raw.length > MaxReplyBytes) return out
  var reply = null
  try { reply = JSON.parse(raw.trim()) } catch (e) { reply = null }
  if (!reply || typeof reply !== "object" || Array.isArray(reply) || reply.ok !== true) return out
  var l = reply.lyrics
  if (!l || typeof l !== "object" || Array.isArray(l)) return out
  out.searching = l.status === "searching"
  if (l.status !== "found" || (l.kind !== "synced" && l.kind !== "words")) return out
  var lines = Array.isArray(l.lines) ? l.lines : []
  out.timed = lines.length > 0
  var current = typeof l.current === "number" && isFinite(l.current) ? Math.floor(l.current) : -1
  function lineText(i) {
    var entry = i >= 0 && i < lines.length ? lines[i] : null
    return entry && typeof entry === "object" ? clip(entry.text).trim() : ""
  }
  out.line = lineText(current)
  // The next line with words in it (blank lines are pauses).
  for (var i = Math.max(0, current + 1); i < lines.length && i < current + 6; i++) {
    out.next = lineText(i)
    if (out.next !== "") break
  }
  return out
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

// The sleep timer's steps, in minutes; the button walks through them and
// then turns the timer off.
var SleepSteps = [15, 30, 60, 90]

// The step after the time left (in seconds): the next longer one, or "off".
function nextSleep(remaining) {
  var minutes = seconds(remaining) / 60
  for (var i = 0; i < SleepSteps.length; i++)
    if (SleepSteps[i] > minutes + 0.5) return String(SleepSteps[i])
  return "off"
}

function nextRepeat(repeat) {
  return repeat === "off" ? "all" : repeat === "all" ? "one" : "off"
}

function wholeNumber(value, low, high) {
  var n = typeof value === "number" ? value : typeof value === "string" && /^[0-9]{1,7}$/.test(value) ? Number(value) : NaN
  if (!isFinite(n)) return null
  n = Math.round(n)
  return n >= low && n <= high ? String(n) : null
}

function oneOf(words) {
  return function(value) { return typeof value === "string" && words.indexOf(value) >= 0 ? value : null }
}

var onOff = oneOf(["on", "off"])

// The commands the widget may run, and nothing else. Each maps to a fixed
// argv: no shell. A command that takes a value has a check that turns the
// value into one of a few words or a whole number in a range; anything else
// is refused, so no text from a song, a setting or a reply reaches CymtX.
//   reply: "status" - CymtX answers with its status
//          "lyrics" - with the lyrics
//          "other"  - with something the widget doesn't read
var Commands = {
  status: { args: ["status"], reply: "status" },
  toggle: { args: ["toggle"], reply: "status" },
  next: { args: ["next"], reply: "status" },
  previous: { args: ["previous"], reply: "status" },
  moment: { args: ["moment"], reply: "status" },
  seek: { args: ["seek"], reply: "status", value: function(v) { return wholeNumber(v, 0, 864000) } },
  volume: { args: ["volume"], reply: "status", value: function(v) { return wholeNumber(v, 0, 100) } },
  shuffle: { args: ["shuffle"], reply: "status", value: onOff },
  blend: { args: ["blend"], reply: "status", value: onOff },
  drift: { args: ["drift"], reply: "status", value: onOff },
  repeat: { args: ["repeat"], reply: "status", value: oneOf(["off", "all", "one"]) },
  sleep: { args: ["sleep"], reply: "status",
           value: function(v) { return v === "off" ? "off" : wholeNumber(v, 1, 720) } },
  visual: { args: ["visual"], reply: "other", value: oneOf(["next", "previous"]) },
  lyrics: { args: ["lyrics"], reply: "lyrics" }
}

function known(name) {
  return typeof name === "string" && Object.prototype.hasOwnProperty.call(Commands, name)
}

// What kind of reply the command gets, or "" for a command that isn't one.
function replyKind(name) {
  return known(name) ? Commands[name].reply : ""
}

function argv(binary, name, value) {
  if (!known(name)) return null
  var command = Commands[name]
  var out = [binary].concat(command.args)
  if (command.value) {
    var word = command.value(value)
    if (word === null) return null
    out.push(word)
  } else if (value !== undefined && value !== null && value !== "") {
    return null
  }
  out.push("--json")
  return out
}
