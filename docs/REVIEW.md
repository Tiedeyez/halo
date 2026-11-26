# Review notes for the Omarchy Marketplace

What a reviewer will want to check in CymtX, theme by theme. Two things ship
from this repository:

- **CymtX**, a native app (C++ and Qt Quick), installed as an Arch package by
  the user (`packaging/PKGBUILD`). The plugin does not install it.
- **CymtX Player** (`tiedeyez.halo`, `manifest.json`), a bar widget in
  `plugin/` that runs the installed `/usr/bin/cymtx`.

DISCLOSURE.md is the full list of what each one reads, writes, starts and
sends. The plugin itself is two QML/JS files and a string table.

| Theme | CymtX app | CymtX Player widget |
| --- | --- | --- |
| **Network** | None by default. Online lyrics (lrclib.net) only after the user turns them on: https only, same-site redirects (max 3), replies over 2 MiB cut off (`src/lyrics.cpp`, `Safety::MaxLyricsReply`). ffmpeg runs with `-protocol_whitelist file` (`src/analysis.cpp`). | None. |
| **Privacy and stored data** | State, stats, lyrics cache and playlists under `~/.local/state`, `~/.cache`, `~/.local/share` in `cymtx/` folders. No command lines, no account, no tokens. Text from tags, file names and playlists is shown as plain text everywhere (`textFormat: Text.PlainText` on every label, tooltips, playlist menu items; a test checks each one), so markup in a tag fetches nothing. | Keeps nothing. Song text and lyric lines from CymtX are shown as plain text (`textFormat: Text.PlainText`). The cover is the file CymtX already cached, opened only if `status` names an absolute path to a `.jpg` or `.png` (`coverPath` in `plugin/CymtXStatus.js`); a URL or any other path is dropped. |
| **Config changes and consent** | Never edits Omarchy, Hyprland or other apps' config. Writes `~/.config/cymtx/folders` only when the user picks a folder on the welcome screen. | Writes no files and changes no settings. |
| **systemd and services** | None: no units, timers or autostart. | None. |
| **Native binaries and builds** | Built from source by `packaging/PKGBUILD` from the `v1.0.3` tag, with the tag's commit pinned in `_commit` and checked in `prepare()`, so a moved tag fails the build. Tests run in `check()`. No downloaded binaries, no cached artifacts. The bundled shaders and turntable images are built from files in this repository (`art/`, `tools/`). | No binaries. |
| **Resource bounds** | Control socket: 64 KiB per command, then the connection closes. `cymtx` commands: one deadline and a 16 MiB cap for the reply (`Remote::send`). The permission walk stops after 50,000 entries; analysis reads at most 8 minutes of a song. | Replies over 64 KiB are refused, commands killed after 5 s, one command at a time (the latest waiting action replaces an older one; polls are dropped, not queued), status polled every 2 to 15 s, lyrics once a second only while the player is open and the song has timed lyrics. Tested in `tests/plugin/`. |
| **Removal** | `pacman -Rns cymtx`; README "Remove" lists CymtX's own folders. Nothing else to undo. | Removing the plugin folder is all; it created nothing. |
| **Symlinks and /tmp** | No `/tmp` use: the socket lives only in the private `$XDG_RUNTIME_DIR`, with no fallback. The permission pass opens folders with `O_NOFOLLOW` (`src/safety.h`). `cymtx skill install` refuses a linked folder and won't overwrite a file it didn't write. | No files. |
| **Supply chain** | Arch packages only (Qt 6, TagLib, ffmpeg). No vendored code beyond the headphone data (OPRA/AutoEQ, CC BY-SA 4.0, licence shipped). | Uses only Omarchy's own `qs.Commons`/`qs.Ui`. |
| **Secrets** | None used or stored. | None. |
| **Privilege** | Runs as the user. Only `makepkg -si` asks for the password, to let pacman install the package the user built. | Runs as the user. |
| **File permissions** | `umask 077` first thing in `main()`; CymtX's own folders corrected to `0700`/`0600` on start (`Safety::makePrivate`). | No files. |

## Processes

| Started by | Program | How |
| --- | --- | --- |
| CymtX | `ffmpeg` (decode, analysis) | `/usr/bin` first, then `PATH` (`Safety::systemTool`), argument list, no shell |
| CymtX | `hyprctl` (focus its window, read rounding) | same, with a timeout |
| Widget | `/usr/bin/cymtx` with `status`, `toggle`, `next`, `previous`, `moment`, `lyrics`, `seek <0-864000>`, `volume <0-100>`, `shuffle\|blend\|drift <on\|off>`, `repeat <off\|all\|one>`, `sleep <1-720\|off>`, `visual <next\|previous>` (each with `--json`), and `cymtx open` | absolute path, no shell; the command list and the check on each value are in `plugin/CymtXStatus.js` (`Commands`, `argv`): a value is a whole number in range or one of the words shown, so no text from a song, a setting or a reply is passed on. Tested in `tests/plugin/status-test.mjs` |

## Checking it

```sh
bin/test                          # 77 Qt tests, offscreen
node tests/plugin/status-test.mjs # the widget's parsing and bounds
tests/plugin/run-widget-test.sh   # the widget in a private, offscreen Quickshell with a stub cymtx
cd packaging && makepkg           # builds the tag, runs the tests, packages
```
