#!/bin/sh
# Runs the bar widget for real in a private, offscreen Quickshell instance:
# Omarchy's own qs.Commons and qs.Ui, a stand-in bar and a stub `halo`, so
# nothing touches your bar, your shell.json or a running Halo. With --real it
# also reads the installed /usr/bin/halo once with `status --json` (read-only).
#
# --grab saves the bar as out.png and the player as out-player.png.
#
#   tests/plugin/run-widget-test.sh [--real] [--grab out.png]
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
SHELL_DIR="${OMARCHY_PATH:-/usr/share/omarchy}/shell"
REAL=0
GRAB=""
while [ $# -gt 0 ]; do
  case "$1" in
    --real) REAL=1 ;;
    --grab) GRAB="$2"; shift ;;
    *) echo "usage: $0 [--real] [--grab out.png]" >&2; exit 2 ;;
  esac
  shift
done

[ -d "$SHELL_DIR/Commons" ] || { echo "Omarchy's shell isn't at $SHELL_DIR" >&2; exit 2; }
command -v qs >/dev/null || { echo "qs (Quickshell) isn't installed" >&2; exit 2; }

# A fresh private folder (mktemp -d is 0700 with an unpredictable name),
# removed on exit.
work="$(mktemp -d)"
pid=""
cleanup() {
  [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT INT TERM

ln -s "$SHELL_DIR/Commons" "$work/Commons"
ln -s "$SHELL_DIR/Ui" "$work/Ui"
ln -s "$SHELL_DIR/services" "$work/services"
cp -R "$ROOT/plugin" "$work/plugin"
cp "$ROOT/tests/plugin/harness.qml" "$work/shell.qml"

# A picture to stand in for the cover Halo caches.
cp "$ROOT/docs/turntable.png" "$work/cover.png"

cat > "$work/halo" <<'STUB'
#!/bin/sh
# Stand-in for /usr/bin/halo. Logs its arguments; answers by the mode file,
# and remembers the few settings the widget changes.
d="$(dirname "$0")"
printf '%s\n' "$*" >> "$d/calls"
mode="$(cat "$d/mode" 2>/dev/null || true)"
get() { cat "$d/set-$1" 2>/dev/null || printf %s "$2"; }
flag() { [ "$(get "$1" off)" = on ] && echo true || echo false; }
case "$1" in
  toggle)
    if [ "$mode" = playing ]; then mode=paused; else mode=playing; fi
    printf %s "$mode" > "$d/mode" ;;
  volume|seek|shuffle|blend|drift|repeat|sleep)
    printf %s "$2" > "$d/set-$1" ;;
esac
case "$mode" in
  offline)
    echo '{"ok":false,"error":"Halo isn'"'"'t running. Start it with `halo open`."}'
    exit 1 ;;
  playing|paused)
    if [ "$1" = visual ]; then echo '{"ok":true,"visual":"bars"}'; exit 0; fi
    if [ "$1" = lyrics ]; then
      echo '{"ok":true,"lyrics":{"status":"found","kind":"synced","current":1,"offset":0,"lines":[{"text":"First line","start":0},{"text":"Second line","start":30},{"text":"","start":50},{"text":"Third line","start":60}]}}'
      exit 0
    fi
    p=true; [ "$mode" = paused ] && p=false
    sleep_left=0; [ "$(get sleep off)" != off ] && sleep_left=$(( $(get sleep 0) * 60 ))
    printf '{"ok":true,"status":{"playing":%s,"path":"/music/a.flac","title":"Night Drive","artist":"Tape Deck","album":"Loops","position":%s,"duration":215,"volume":%s,"index":2,"queue":12,"shuffle":%s,"blend":%s,"drift":%s,"repeat":"%s","sleep":%s,"analysis":{"bpm":99.3,"lufs":-11.7},"cover":"%s","next":{"title":"Morning","artist":"Tape Deck","path":"/music/b.flac"}}}\n' \
      "$p" "$(get seek 42.5)" "$(get volume 50)" "$(flag shuffle)" "$(flag blend)" "$(flag drift)" "$(get repeat off)" "$sleep_left" "$d/cover.png" ;;
  markup)
    printf '%s\n' '{"ok":true,"status":{"playing":true,"path":"/music/m.flac","title":"<b>bold</b><img src=\"https://example.invalid/x\">","artist":"Someone","position":1,"duration":10}}' ;;
  flood)
    head -c 204800 /dev/zero | tr '\0' x
    echo ;;
  hang)
    exec sleep 30 ;;
  *)
    echo '{"ok":true,"status":{"playing":false,"path":""}}' ;;
esac
STUB
chmod 700 "$work/halo"

env -u WAYLAND_DISPLAY -u DISPLAY QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic \
  HALO_WIDGET_TEST_DIR="$work" HALO_WIDGET_TEST_REAL="$REAL" \
  qs -p "$work" > "$work/log" 2>&1 &
pid=$!

# Wait for the harness to finish (about 45 s), at most 120 s.
i=0
while [ $i -lt 240 ] && ! grep -q '^.*DONE ' "$work/log" 2>/dev/null; do
  if ! kill -0 "$pid" 2>/dev/null; then break; fi
  i=$((i + 1))
  sleep 0.5
done

grep -E 'CHECK|ERROR|error|TypeError|ReferenceError|is not a type|not installed' "$work/log" | sed 's/\x1b\[[0-9;]*m//g' || true
if [ -n "$GRAB" ] && [ -f "$work/bar.png" ]; then
  cp "$work/bar.png" "$GRAB" && echo "Saved $GRAB"
  [ -f "$work/player.png" ] && cp "$work/player.png" "${GRAB%.png}-player.png" && echo "Saved ${GRAB%.png}-player.png"
fi

done_line="$(grep -o 'DONE [0-9]* [0-9]*' "$work/log" | tail -n 1 || true)"
if [ -z "$done_line" ]; then
  echo "The harness didn't finish; full log:" >&2
  sed 's/\x1b\[[0-9;]*m//g' "$work/log" >&2
  exit 1
fi
set -- $done_line
echo "$2 checks, $3 failed"
[ "$3" = 0 ]
