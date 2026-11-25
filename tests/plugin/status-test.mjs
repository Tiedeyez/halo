// Tests for plugin/HaloStatus.js, the bar widget's reply parser.
// Run: node tests/plugin/status-test.mjs
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import assert from "node:assert/strict";

const source = readFileSync(fileURLToPath(new URL("../../plugin/HaloStatus.js", import.meta.url)), "utf8")
  .replace(/^\.pragma library\s*$/m, "");
const H = {};
vm.runInNewContext(source + "\nfor (const n of ['parse','parseLyrics','clock','argv','replyKind','emptyState','emptyLyrics','withChange','nextSleep','nextRepeat','coverPath','MaxReplyBytes','MaxField','MaxPath']) this[n] = eval(n);", H);

let passed = 0;
function test(name, fn) {
  fn();
  passed++;
  console.log("PASS " + name);
}

const reply = (status) => JSON.stringify({ ok: true, status }) + "\n";

test("a playing song", () => {
  const s = H.parse(reply({ playing: true, path: "/m/a.flac", title: "Song", artist: "Artist", album: "Album",
    position: 61.5, duration: 200, next: { title: "Next one", artist: "B", path: "/m/b.flac" } }), 0);
  assert.equal(s.kind, "playing");
  assert.equal(s.title, "Song");
  assert.equal(s.artist, "Artist");
  assert.equal(s.album, "Album");
  assert.equal(s.position, 61.5);
  assert.equal(s.duration, 200);
  assert.equal(s.hasNext, true);
  assert.equal(s.nextTitle, "Next one");
});

test("paused, and a title falls back to the file name", () => {
  const s = H.parse(reply({ playing: false, path: "/m/x/track 01.mp3", title: "" }), 0);
  assert.equal(s.kind, "paused");
  assert.equal(s.title, "track 01.mp3");
});

test("running with nothing loaded is idle", () => {
  assert.equal(H.parse(reply({ playing: false, path: "" }), 0).kind, "idle");
  assert.equal(H.parse(JSON.stringify({ ok: true }), 0).kind, "idle");
});

test("Halo not running: the CLI's own error", () => {
  const s = H.parse(JSON.stringify({ ok: false, error: "Halo isn't running. Start it with `halo open`." }) + "\n", 1);
  assert.equal(s.kind, "offline");
  assert.match(s.message, /isn't running/);
});

test("nothing, garbage, arrays and wrong types are offline, never a crash", () => {
  for (const text of ["", "\n", "not json", "[1,2]", "null", "42", "{\"ok\":\"yes\"}", undefined, null, 7])
    assert.equal(H.parse(text, 1).kind, "offline", String(text));
  const s = H.parse(reply({ playing: "yes", path: 5, title: { x: 1 }, position: "NaN", duration: -4 }), 0);
  assert.equal(s.kind, "idle");
  const t = H.parse(reply({ playing: true, path: "/a", title: ["x"], artist: 3, position: Infinity, duration: "12" }), 0);
  assert.equal(t.kind, "playing");
  assert.equal(t.title, "a");
  assert.equal(t.artist, "");
  assert.equal(t.position, 0);
  assert.equal(t.duration, 12);
});

test("an oversize reply is refused before it is parsed", () => {
  const big = reply({ playing: true, path: "/a", title: "x".repeat(H.MaxReplyBytes) });
  const s = H.parse(big, 0);
  assert.equal(s.kind, "offline");
  assert.equal(s.title, "");
});

test("long fields are cut and control characters flattened", () => {
  const s = H.parse(reply({ playing: true, path: "/a", title: "t".repeat(5000), artist: "a\nb\u001b[31mc" }), 0);
  assert.equal(s.title.length, H.MaxField);
  assert.ok(s.title.endsWith("…"));
  assert.equal(s.artist, "a b [31mc");
});

test("markup-shaped titles are kept as text (the widget shows them as plain text)", () => {
  const s = H.parse(reply({ playing: true, path: "/a", title: "<img src=\"https://example.invalid/t\">" }), 0);
  assert.equal(s.title, "<img src=\"https://example.invalid/t\">");
});

test("position never runs past the end", () => {
  const s = H.parse(reply({ playing: true, path: "/a", position: 500, duration: 100 }), 0);
  assert.equal(s.position, 100);
});

test("clock", () => {
  assert.equal(H.clock(0), "0:00");
  assert.equal(H.clock(65.9), "1:05");
  assert.equal(H.clock(3723), "1:02:03");
  assert.equal(H.clock(-5), "0:00");
  assert.equal(H.clock("junk"), "0:00");
});

test("what Halo is set to: volume, modes, sleep, tempo, loudness, place in the queue", () => {
  const s = H.parse(reply({ playing: true, path: "/m/a.flac", title: "Song", position: 5, duration: 100,
    volume: 62, shuffle: true, repeat: "one", blend: true, drift: false, sleep: 840.9, index: 2, queue: 9,
    analysis: { bpm: 99.3, lufs: -11.72 }, problem: "couldn't play b.flac" }), 0);
  assert.equal(s.volume, 62);
  assert.equal(s.shuffle, true);
  assert.equal(s.repeat, "one");
  assert.equal(s.blend, true);
  assert.equal(s.drift, false);
  assert.equal(s.sleep, 840);
  assert.equal(s.bpm, 99);
  assert.equal(s.hasLoudness, true);
  assert.equal(s.lufs, -11.7);
  assert.equal(s.queueIndex, 3);
  assert.equal(s.queueSize, 9);
  assert.equal(s.problem, "couldn't play b.flac");
  assert.equal(s.key, "/m/a.flac");
});

test("an older Halo, or wrong types: those fields fall back, never a crash", () => {
  const s = H.parse(reply({ playing: true, path: "/a", volume: "loud", shuffle: "yes", repeat: "forever",
    sleep: -3, index: 12, queue: 4, analysis: { bpm: "fast", lufs: 3 }, problem: 5 }), 0);
  assert.equal(s.volume, -1);
  assert.equal(s.shuffle, false);
  assert.equal(s.repeat, "off");
  assert.equal(s.sleep, 0);
  assert.equal(s.bpm, 0);
  assert.equal(s.hasLoudness, false);
  assert.equal(s.queueIndex, 0);
  assert.equal(s.problem, "");
  assert.equal(H.parse(reply({ playing: true, path: "/a", volume: 400 }), 0).volume, 100);
  assert.equal(H.parse(reply({ playing: true, path: "/a", analysis: [1] }), 0).bpm, 0);
});

test("idle keeps the settings (the controls still work with nothing loaded)", () => {
  const s = H.parse(reply({ playing: false, path: "", volume: 30, shuffle: true, drift: true }), 0);
  assert.equal(s.kind, "idle");
  assert.equal(s.volume, 30);
  assert.equal(s.shuffle, true);
  assert.equal(s.drift, true);
  assert.equal(s.title, "");
});

test("the cover is a local picture or nothing", () => {
  assert.equal(H.coverPath("/home/u/.cache/halo/covers/ab12-512.jpg"), "/home/u/.cache/halo/covers/ab12-512.jpg");
  assert.equal(H.coverPath("/c/x.PNG"), "/c/x.PNG");
  for (const bad of ["https://example.invalid/x.jpg", "file:///c/x.jpg", "covers/x.jpg", "/c/../etc/x.jpg",
    "/c/x.svg", "/c/x.jpg\n", "/c/x", "", 7, null, undefined, { a: 1 }, "/" + "x".repeat(2000) + ".jpg"])
    assert.equal(H.coverPath(bad), "", String(bad).slice(0, 40));
  assert.equal(H.parse(reply({ playing: true, path: "/a", cover: "/c/a-512.jpg" }), 0).cover, "/c/a-512.jpg");
  assert.equal(H.parse(reply({ playing: true, path: "/a", cover: "http://x/a.jpg" }), 0).cover, "");
});

test("a change shown before Halo answers leaves the old state alone", () => {
  const s = H.parse(reply({ playing: true, path: "/a", volume: 10 }), 0);
  const t = H.withChange(s, { volume: 55 });
  assert.equal(s.volume, 10);
  assert.equal(t.volume, 55);
  assert.equal(t.kind, "playing");
});

const lyrics = (l) => JSON.stringify({ ok: true, lyrics: l });

test("lyrics: the line being sung and the next one with words", () => {
  const l = H.parseLyrics(lyrics({ status: "found", kind: "synced", current: 1,
    lines: [{ text: "one", start: 0 }, { text: "two", start: 4 }, { text: "", start: 8 }, { text: "three", start: 9 }] }));
  assert.equal(l.timed, true);
  assert.equal(l.line, "two");
  assert.equal(l.next, "three");
  const before = H.parseLyrics(lyrics({ status: "found", kind: "words", current: -1, lines: [{ text: "first" }] }));
  assert.equal(before.timed, true);
  assert.equal(before.line, "");
  assert.equal(before.next, "first");
});

test("lyrics: plain words, none, garbage and floods show nothing", () => {
  const plain = H.parseLyrics(lyrics({ status: "found", kind: "plain", current: -1, lines: [{ text: "a" }] }));
  assert.equal(plain.timed, false);
  assert.equal(plain.line + plain.next, "");
  assert.equal(H.parseLyrics(lyrics({ status: "searching", kind: "none", lines: [] })).searching, true);
  for (const text of ["", "junk", "[]", "null", JSON.stringify({ ok: false, error: "x" }), JSON.stringify({ ok: true }),
    lyrics({ status: "found", kind: "synced", current: "2", lines: "nope" }),
    lyrics({ status: "found", kind: "synced", current: 99, lines: [7, null, { text: 5 }] }), undefined, null]) {
    const l = H.parseLyrics(text);
    assert.equal(l.line, "", String(text).slice(0, 40));
    assert.equal(l.next, "");
  }
  assert.equal(H.parseLyrics(lyrics({ status: "found", kind: "synced", current: 0,
    lines: [{ text: "x".repeat(H.MaxReplyBytes) }] })).timed, false);
  const long = H.parseLyrics(lyrics({ status: "found", kind: "synced", current: 0, lines: [{ text: "y".repeat(5000) }] }));
  assert.equal(long.line.length, H.MaxField);
});

test("the sleep button walks 15, 30, 60, 90, off; repeat walks off, all, one", () => {
  assert.equal(H.nextSleep(0), "15");
  assert.equal(H.nextSleep(14 * 60 + 50), "30");
  assert.equal(H.nextSleep(29 * 60), "30");
  assert.equal(H.nextSleep(30 * 60), "60");
  assert.equal(H.nextSleep(89 * 60), "90");
  assert.equal(H.nextSleep(90 * 60), "off");
  assert.equal(H.nextSleep("junk"), "15");
  assert.equal(H.nextRepeat("off"), "all");
  assert.equal(H.nextRepeat("all"), "one");
  assert.equal(H.nextRepeat("one"), "off");
  assert.equal(H.nextRepeat("junk"), "off");
});

test("only the fixed commands map to an argv", () => {
  const a = (...args) => { const v = H.argv("/usr/bin/halo", ...args); return v === null ? null : Array.from(v); };
  assert.deepEqual(a("status"), ["/usr/bin/halo", "status", "--json"]);
  assert.deepEqual(a("toggle"), ["/usr/bin/halo", "toggle", "--json"]);
  assert.deepEqual(a("moment"), ["/usr/bin/halo", "moment", "--json"]);
  assert.deepEqual(a("lyrics"), ["/usr/bin/halo", "lyrics", "--json"]);
  assert.equal(a("open ~; rm -rf /"), null);
  assert.equal(a("constructor"), null);
  assert.equal(a("__proto__"), null);
  for (const name of ["add", "playnext", "clear", "playlist", "doctor", "grab", "ui", "rescan", "open", "normalize"])
    assert.equal(a(name), null, name);
  // A command that takes no value refuses one.
  assert.equal(a("status", "--help"), null);
  assert.equal(a("lyrics", "online"), null);
});

test("values are whole numbers in range or one of a few words, nothing else", () => {
  const a = (...args) => { const v = H.argv("/usr/bin/halo", ...args); return v === null ? null : Array.from(v); };
  assert.deepEqual(a("seek", 61.4), ["/usr/bin/halo", "seek", "61", "--json"]);
  assert.deepEqual(a("seek", 0), ["/usr/bin/halo", "seek", "0", "--json"]);
  assert.deepEqual(a("volume", 55), ["/usr/bin/halo", "volume", "55", "--json"]);
  assert.deepEqual(a("volume", "40"), ["/usr/bin/halo", "volume", "40", "--json"]);
  assert.deepEqual(a("shuffle", "on"), ["/usr/bin/halo", "shuffle", "on", "--json"]);
  assert.deepEqual(a("blend", "off"), ["/usr/bin/halo", "blend", "off", "--json"]);
  assert.deepEqual(a("repeat", "one"), ["/usr/bin/halo", "repeat", "one", "--json"]);
  assert.deepEqual(a("sleep", "30"), ["/usr/bin/halo", "sleep", "30", "--json"]);
  assert.deepEqual(a("sleep", "off"), ["/usr/bin/halo", "sleep", "off", "--json"]);
  assert.deepEqual(a("visual", "next"), ["/usr/bin/halo", "visual", "next", "--json"]);
  for (const [name, value] of [["seek", -1], ["seek", "+10"], ["seek", "1e3"], ["seek", NaN], ["seek", 1e9],
    ["seek", undefined], ["volume", 101], ["volume", -5], ["volume", "50; rm"], ["volume", "--json"],
    ["volume", null], ["volume", [50]], ["shuffle", "yes"], ["shuffle", true], ["drift", ""], ["repeat", "two"],
    ["sleep", 0], ["sleep", 721], ["sleep", "soon"], ["visual", "turntable"], ["visual", "/etc/passwd"],
    ["visual", undefined]])
    assert.equal(a(name, value), null, name + " " + String(value));
});

test("which reply each command gets", () => {
  assert.equal(H.replyKind("status"), "status");
  assert.equal(H.replyKind("volume"), "status");
  assert.equal(H.replyKind("lyrics"), "lyrics");
  assert.equal(H.replyKind("visual"), "other");
  assert.equal(H.replyKind("toString"), "");
});

console.log(passed + " passed");
