# What Halo does on your computer

Audited against the source of Halo 0.1.0. A change that adds a path, a
process or a network call updates this file in the same commit.

## What it is and what runs it

Halo is a desktop app: one binary, `/usr/bin/halo`, installed by its Arch
package. It runs as you, with your user's access, like any program you
start. It is not sandboxed.

## What it needs

Qt 6 (base, declarative, multimedia, wayland), TagLib, and the `ffmpeg`
program. It talks to PipeWire through Qt to play sound.

## What it writes

| Path | What | When |
| --- | --- | --- |
| `~/.local/state/halo/state.json` | The queue, position, volume, settings, equalizer, moments | As they change, and on quit |
| `~/.local/state/halo/stats.json` | Play counts and last-played times per file | After you hear half a song |
| `~/.local/state/halo/headphones.json` | Outputs you turned headphone correction off for, by name | When you turn it off or on |
| Your music files' tags (title, artist, album, album artist, comment) | The fix shown in the Library Doctor | Only when you press Fix, Fix all or U (undo) there |
| `~/.local/state/halo/doctor.json` | Each Library Doctor fix: the file, the field, and the old and new value, so it can be undone | When you fix or undo |
| `~/.cache/halo/library.json` | The list of music files with their tags and durations | After each library scan |
| `~/.cache/halo/analysis.json` | Tempo, loudness, true peak and silent edges per file | As songs are analysed |
| `~/.cache/halo/covers/*.jpg` | Album covers, cut square and scaled | When a cover is first shown |
| `~/.cache/halo/lyrics/*.json` | Lyrics found on lrclib.net, or a note that none were, one file per song | Only with online lyrics on, after a lookup |
| `~/.local/state/halo/lyrics.json` | Whether online lyrics is on, whether lyrics are shown, and per-song timing nudges | When you change them |
| `$XDG_RUNTIME_DIR/halo.sock` | The local socket the `halo` command talks to | While Halo runs |
| `~/.agents/skills/halo/SKILL.md` and the link `~/.claude/skills/halo` | A skill for coding agents | Only when you run `halo skill install` |
| A `.png` path you name | A picture of the window | Only on the socket command `grab`, and never over an existing file |

Every file is written beside its target and renamed into place, so a crash
never leaves half a file.

**Halo never writes, moves or deletes a music file**, and never edits your
Omarchy or Hyprland configuration.

## What it reads

- Every audio file under `~/Music` and the folders listed in
  `~/.config/halo/folders` (or `$HALO_MUSIC`): their tags, durations and
  embedded covers through TagLib, and their audio through ffmpeg. Image
  files in those folders, for covers. Files named `.haloignore` or
  `.nomedia`, to skip a folder.
- For the song playing: a `.lrc` file beside it with the same name, and
  lyrics in its tags, for the lyrics view.
- `~/.local/state/omarchy/current/theme/colors.toml` for the accent colour,
  `$OMARCHY_LANGUAGE_FILE` or `~/.config/omarchy/language` (under
  `$XDG_CONFIG_HOME` when set) for the language, and
  `$OMARCHY_PATH/default/i18n/locales.json` for number formats.

It reads nothing else in your home folder.

## Processes it starts

| Process | When |
| --- | --- |
| `ffmpeg` | One per song being played or prepared (at most two at a time, plus a seek), decoding to a pipe |
| `nice -n 19 ffmpeg` | Once per song in your library, one at a time, to analyse it; at the lowest CPU priority |
| `hyprctl -j getoption decoration:rounding` | Once a second while Halo is open, to match Hyprland's window corners |
| `hyprctl dispatch` (focus Halo) | When a second `halo open` or an MPRIS Raise asks to bring the window forward |

The uncomfortable part: the first time Halo sees a large library, it decodes
every song once to analyse it. On a big library that is hours of background
work at the lowest priority; it uses CPU (and battery) while it runs, and it
is what Blend and Drift learn from. Results are cached, so it happens once
per file.

## Network

Off unless you turn on online lyrics. With it off (the default) Halo opens
no network connection. There is no telemetry, no analytics, no accounts and
no automatic updates.

**Online lyrics** (the "Turn on online lyrics" button, or
`halo lyrics online on`): when a song starts and no lyrics for it are on
this computer or already cached, Halo asks lrclib.net, a free lyrics
database, over HTTPS:

- `GET https://lrclib.net/api/get` with the song's title, its first artist,
  album, and length in whole seconds; if that misses,
- `GET https://lrclib.net/api/search` with the title and first artist.

The request carries the User-Agent `Halo/0.1.0 (Omarchy music player)` and
nothing else about you: no file paths, no account, no identifier. LRCLIB
sees your IP address, as any website would, and which songs you play. Each
song is looked up once: the answer, or the fact that there was none, is
cached (a miss is asked again after a week). When LRCLIB asks clients to
slow down, Halo waits as long as it says. `halo lyrics online off` stops
all of it; the cache stays until you delete `~/.cache/halo/lyrics`.

Locally, Halo listens only on a socket that only your user can open, and it
registers `org.mpris.MediaPlayer2.halo` on your D-Bus session bus so media
keys and bar widgets can control it.

Anything running as your user can send commands on that socket or over
MPRIS: play, pause, queue files, change settings (online lyrics included),
or save a picture of the window to a new `.png` file.

## How to check this yourself

```sh
grep -rnE "QNetwork|QTcp|QUdp|connectToHost" src/        # network code: src/lyrics.cpp only
grep -rnE "QProcess|posix_spawn|startDetached" src/       # every process it starts
grep -rnE "QSaveFile|mkpath|QFile::link|\.save\(" src/    # every place it writes
```

While Halo runs, `ls -l /proc/$(pgrep -x halo)/fd` shows what it has open,
and `ss -xp | grep halo` shows its one local socket.

## Maintainer and licence

Tiedeyez. MIT licence.
