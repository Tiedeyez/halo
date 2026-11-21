// Tests for plugin/HaloStatus.js, the bar widget's reply parser.
// Run: node tests/plugin/status-test.mjs
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import vm from "node:vm";
import assert from "node:assert/strict";

const source = readFileSync(fileURLToPath(new URL("../../plugin/HaloStatus.js", import.meta.url)), "utf8")
  .replace(/^\.pragma library\s*$/m, "");
const H = {};
vm.runInNewContext(source + "\nthis.parse = parse; this.clock = clock; this.argv = argv; this.emptyState = emptyState; this.MaxReplyBytes = MaxReplyBytes; this.MaxField = MaxField;", H);

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

test("only the four fixed commands map to an argv", () => {
  assert.deepEqual(Array.from(H.argv("/usr/bin/halo", "status")), ["/usr/bin/halo", "status", "--json"]);
  assert.deepEqual(Array.from(H.argv("/usr/bin/halo", "toggle")), ["/usr/bin/halo", "toggle", "--json"]);
  assert.equal(H.argv("/usr/bin/halo", "open ~; rm -rf /"), null);
  assert.equal(H.argv("/usr/bin/halo", "constructor"), null);
  assert.equal(H.argv("/usr/bin/halo", "__proto__"), null);
});

console.log(passed + " passed");
