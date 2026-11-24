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
| `~/.local/state/halo/state.json` | The queue, position, volume, settings, equalizer, moments, and whether the setup was finished or skipped | As they change, and on quit |
| `~/.config/halo/folders` | Your music folders, one per line: exactly the ones ticked in the setup | Only when you press Next on the setup's folders step |
| `~/.local/state/halo/stats.json` | Play counts and last-played times per file | After you hear half a song |
| `~/.local/state/halo/headphones.json` | Outputs you turned headphone correction off for, by name | When you turn it off or on |
| Your music files' tags (title, artist, album, album artist, comment, track number) | The fix shown in the Library Doctor, or the answer you typed there for a folder | Only when you press Fix, Fix all, Fix these, Write (after Type the answer) or U (undo) there, or when `halo doctor fix`, `set`, `apply` or `undo` is run |
| `~/.local/state/halo/doctor.json` | Each Library Doctor fix: the file, the field, and the old and new value, so it can be undone. The newest 50,000 fixes are kept | When you fix or undo |
| `~/.local/state/halo/doctor.json.unreadable` | The Library Doctor log itself, renamed, if Halo could not read it (damaged, or over 64 MiB). Halo sets it aside and starts a new log; it never cuts the old one short or writes over it | At start, only in that case |
| `~/.cache/halo/library.json` | The list of music files with their tags and durations | After each library scan |
| `~/.cache/halo/analysis.json` | Tempo, loudness, true peak and silent edges per file | As songs are analysed |
| `~/.cache/halo/covers/*.jpg` | Album covers, cut square and scaled | When a cover is first shown |
| `~/.cache/halo/lyrics/*.json` | Lyrics found on lrclib.net, or a note that none were, one file per song | Only with online lyrics on, after a lookup |
| `~/.local/state/halo/lyrics.json` | Whether online lyrics is on, whether lyrics are shown, and per-song timing nudges | When you change them |
| `$XDG_RUNTIME_DIR/halo.sock` | The local socket the `halo` command talks to | While Halo runs |
| `~/.agents/skills/halo/SKILL.md` and the link `~/.claude/skills/halo` | A skill for coding agents | Only when you run `halo skill install`. An existing `SKILL.md` that Halo didn't write, or a folder that is a link, is left alone unless you add `--force`; an existing `~/.claude/skills/halo` that isn't a link is never touched |
| A `.png` path you name | A picture of the window | Only on the socket command `grab`, and never over an existing file |

Every file is written beside its target and renamed into place, so a crash
never leaves half a file.

**Everything Halo writes is private to you.** Halo sets its file-creation
mask to `077` first thing at start, so its folders are `0700` and its files
`0600`, and other accounts on the computer can't read your queue, play
history, lyrics or Library Doctor log. Halo's three folders
(`~/.local/state/halo`, `~/.cache/halo`, `~/.config/halo`) are checked at each
start, and anything an older build left readable is set back to `0700` /
`0600`. Only your own regular files and folders are changed; symbolic links
are never followed.

**Halo never moves, renames or deletes a music file.** It changes one only
through the Library Doctor, and only its tags: a fix you press in the
window, or a `halo doctor fix`, `set` or `apply` command that you or a
coding agent working for you runs. Those commands write only to songs in
your music folders, refuse any other path, and log every write to the same
undo log. Halo never edits your Omarchy or Hyprland configuration.

## What it reads

- Every audio file under your music folders: the folders listed in
  `~/.config/halo/folders` (or `$HALO_MUSIC`), or without that file your
  desktop's music folder (`XDG_MUSIC_DIR` in `~/.config/user-dirs.dirs`)
  and `~/Music`: their tags, durations and
  embedded covers through TagLib, and their audio through ffmpeg. Image
  files in those folders, for covers. Files named `.haloignore` or
  `.nomedia`, to skip a folder.
- The answers file you name in `halo doctor apply <file>`: read once, up to
  8 MiB, as a JSON list of tag values. Nothing in it is run.
- For the song playing: a `.lrc` file beside it with the same name, and
  lyrics in its tags, for the lyrics view.
- `~/.local/state/omarchy/current/theme/colors.toml` for the accent colour
  (or `colors.toml` in the folder `$HALO_THEME_DIR` names, when it is set:
  a way to check a theme without switching to it),
  `$OMARCHY_LANGUAGE_FILE` or `~/.config/omarchy/language` (under
  `$XDG_CONFIG_HOME` when set) for the language, and
  `$OMARCHY_PATH/default/i18n/locales.json` for number formats.

- While the setup is open (the first run, and Ctrl+, after that): the names
  of the files in the folders it offers, to count the songs in each. Those
  are your music folders, `~/Downloads`, a `Music` folder on a drive mounted
  under `/run/media/<you>` or `/media/<you>` (at most 16 drives looked at),
  and any folder you add there. Only names are read, never contents; the
  count stops after 300,000 files; and a folder you don't tick is not read
  again.

It reads nothing else in your home folder. The setup's "Add a folder" window
is a folder picker that lists folders as you browse them; Halo keeps only
the folder you pick.

## Processes it starts

| Process | When |
| --- | --- |
| `ffmpeg` | One per song being played or prepared (at most two at a time, plus a seek), decoding to a pipe |
| `nice -n 19 ffmpeg` | Once per song in your library, one at a time, to analyse it; at the lowest CPU priority |
| `hyprctl -j getoption decoration:rounding` | Once a second while Halo is open, to match Hyprland's window corners |
| `hyprctl dispatch` (focus Halo) | When a second `halo open` or an MPRIS Raise asks to bring the window forward |

Each tool is run from `/usr/bin` (then `/bin`, `/usr/local/bin`), and from
your `PATH` only when it isn't installed there, so an executable placed in a
personal folder such as `~/.local/bin` can't stand in for `ffmpeg`. ffmpeg
gets `-protocol_whitelist file` on every input, so a file that is really a
playlist (HLS, concat) can't make it open a network connection.

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
sees your IP address, as any website would, and which songs you play.
Redirects are followed only within the same site, at most three, and a reply
over 2 MiB is cut off and treated as a failed lookup. The lyrics address is
https only (plain http is accepted just for a server on this computer, which
the tests use). Each
song is looked up once: the answer, or the fact that there was none, is
cached (a miss is asked again after a week). When LRCLIB asks clients to
slow down, Halo waits as long as it says. `halo lyrics online off` stops
all of it; the cache stays until you delete `~/.cache/halo/lyrics`.

Locally, Halo listens only on a socket that only your user can open, in
your private runtime folder (`$XDG_RUNTIME_DIR`). Without one, Halo opens no
socket at all rather than fall back to the shared `/tmp`, and terminal
commands say Halo isn't reachable. One command is at most 64 KiB; a
connection that sends more without a newline is closed. Halo also
registers `org.mpris.MediaPlayer2.halo` on your D-Bus session bus so media
keys and bar widgets can control it.

Anything running as your user can send commands on that socket or over
MPRIS: play, pause, queue files, change settings (online lyrics included),
or save a picture of the window to a new `.png` file.

## The bar widget (Halo Player plugin)

The repository is also an Omarchy plugin, `tiedeyez.halo`, a bar widget.
It runs only `/usr/bin/halo` with one of five fixed argument lists
(`status --json`, `toggle --json`, `next --json`, `previous --json`,
`open`), never through a shell and never with text from a song in them. It
polls `status` every 3 to 15 seconds (every second while its popup is open),
reads at most 64 KiB of a reply, and stops a command after five seconds.
It writes no files, changes no settings, and opens no network connection.
What it shows (title, artist, album) is drawn as plain text. While a song
plays, its bar icon is three small bars that move on a timer; they are
decoration, drawn from a fixed pattern, and the widget reads no audio.

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
