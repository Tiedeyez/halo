# What CymtX does on your computer

Audited against the source of CymtX 0.1.0. A change that adds a path, a
process or a network call updates this file in the same commit.

## What it is and what runs it

CymtX is a desktop app: one binary, `/usr/bin/cymtx`, installed by its Arch
package. It runs as you, with your user's access, like any program you
start. It is not sandboxed.

## Where it came from

CymtX was called Halo until 1.1.0. The first time CymtX starts for a user
who has Halo's folders (`~/.local/state/halo`, `~/.cache/halo`,
`~/.config/halo`, `~/.local/share/halo`), it copies each to the same place
under the name `cymtx`; it does not follow or copy symbolic links, and it
never writes into a `cymtx` folder that already exists.

It then clears away what still carries the old name, each time it starts
until none is left:

- **Halo's folders are deleted**, each one only where the `cymtx` folder
  beside it exists (so a copy that failed loses nothing), only a real
  folder named exactly `halo`, never through a link.
- **Halo's agent skill is replaced.** If `~/.agents/skills/halo/SKILL.md`
  is the file Halo's own command wrote (it opens with `name: halo`), that
  folder and the link `~/.claude/skills/halo` are removed and CymtX's skill
  is installed in their place. A skill there that Halo did not write is
  left alone.
- **`.haloignore` files are renamed** to `.cymtxignore` when a scan of your
  music folders meets one. This is the one case where CymtX renames a file
  in a music folder; it is a marker file of its own, never a song.

Three things keep the old name on purpose. The package installs
`/usr/bin/halo` as a link to `/usr/bin/cymtx`, so key bindings and scripts
that run `halo` keep working. The Omarchy plugin keeps the id
`tiedeyez.halo`, because changing it would stop updates for everyone who
has it. And CymtX does not edit your own files, so a Hyprland rule or a
script of yours that says `halo` is yours to change.

## What it needs

Qt 6 (base, declarative, multimedia, wayland), TagLib, and the `ffmpeg`
program. It talks to PipeWire through Qt to play sound.

## What it writes

| Path | What | When |
| --- | --- | --- |
| `~/.local/state/cymtx/state.json` | The queue, position, volume, settings, equalizer, moments, and whether the setup was finished or skipped | As they change, and on quit |
| `~/.config/cymtx/folders` | Your music folders, one per line: exactly the ones ticked in the setup | Only when you press Next on the setup's folders step |
| `~/.local/state/cymtx/stats.json` | Play counts and last-played times per file | After you hear half a song |
| `~/.local/state/cymtx/headphones.json` | Outputs you turned headphone correction off for, by name | When you turn it off or on |
| Your music files' tags (title, artist, album, album artist, comment, track number, genre, year) | The fix shown in the Library Doctor, or the answer you typed there for a folder | Only when you press Fix, Fix all, Fix these, Write (after Type the answer) or U (undo) there, or when `cymtx doctor fix`, `set`, `apply` or `undo` is run |
| `~/.local/state/cymtx/doctor.json` | Each Library Doctor fix: the file, the field, and the old and new value, so it can be undone. The newest 50,000 fixes are kept | When you fix or undo |
| `~/.local/state/cymtx/doctor.json.unreadable` | The Library Doctor log itself, renamed, if CymtX could not read it (damaged, or over 64 MiB). CymtX sets it aside and starts a new log; it never cuts the old one short or writes over it | At start, only in that case |
| `~/.cache/cymtx/library.json` | The list of music files with their tags and durations | After each library scan |
| `~/.cache/cymtx/analysis.json` | Tempo, loudness, true peak and silent edges per file | As songs are analysed |
| `~/.cache/cymtx/covers/*.jpg` | Album covers, cut square and scaled | When a cover is first shown |
| `~/.cache/cymtx/lyrics/*.json` | Lyrics found on lrclib.net, or a note that none were, one file per song | Only with online lyrics on, after a lookup |
| `~/.local/state/cymtx/lyrics.json` | Whether online lyrics is on, whether lyrics are shown, and per-song timing nudges | When you change them |
| `$XDG_RUNTIME_DIR/cymtx.sock` | The local socket the `cymtx` command talks to | While CymtX runs |
| `~/.agents/skills/cymtx/SKILL.md` and the link `~/.claude/skills/cymtx` | A skill for coding agents | When you run `cymtx skill install`, press an "Ask my agent" button, or start CymtX where Halo's skill was installed. An existing `SKILL.md` that CymtX didn't write, or a folder that is a link, is left alone unless you add `--force`; an existing `~/.claude/skills/cymtx` that isn't a link is never touched |
| A `.png` path you name | A picture of the window | Only on the socket command `grab`, and never over an existing file |

Every file is written beside its target and renamed into place, so a crash
never leaves half a file.

**Everything CymtX writes is private to you.** CymtX sets its file-creation
mask to `077` first thing at start, so its folders are `0700` and its files
`0600`, and other accounts on the computer can't read your queue, play
history, lyrics or Library Doctor log. CymtX's three folders
(`~/.local/state/cymtx`, `~/.cache/cymtx`, `~/.config/cymtx`) are checked at each
start, and anything an older build left readable is set back to `0700` /
`0600`. Only your own regular files and folders are changed; symbolic links
are never followed.

**CymtX never moves, renames or deletes a music file.** It changes one only
through the Library Doctor, and only its tags: a fix you press in the
window, or a `cymtx doctor fix`, `set` or `apply` command that you or a
coding agent working for you runs. Those commands write only to songs in
your music folders, refuse any other path, and log every write to the same
undo log. CymtX never edits your Omarchy or Hyprland configuration.

**"Ask my agent" starts another program.** On the Library Doctor page, a
button for each coding agent found on your computer (`claude`, `opencode`,
`codex`) installs the CymtX skill if it is missing and runs
`xdg-terminal-exec` with that agent and one fixed sentence asking it to fix
what is left. Nothing from your songs is put on that command line, no shell
reads it, and CymtX sends nothing anywhere itself. What the agent then
does, including any network use, is that program's own and is covered by
its terms, not this page. "Copy the request" puts the same sentence on the
clipboard and starts nothing.

## What it reads

- Every audio file under your music folders: the folders listed in
  `~/.config/cymtx/folders` (or `$CYMTX_MUSIC`), or without that file your
  desktop's music folder (`XDG_MUSIC_DIR` in `~/.config/user-dirs.dirs`)
  and `~/Music`: their tags, durations and
  embedded covers through TagLib, and their audio through ffmpeg. Image
  files in those folders, for covers. Files named `.cymtxignore` or
  `.nomedia`, to skip a folder.
- The answers file you name in `cymtx doctor apply <file>`: read once, up to
  8 MiB, as a JSON list of tag values. Nothing in it is run.
- For the song playing: a `.lrc` file beside it with the same name, and
  lyrics in its tags, for the lyrics view.
- `~/.local/state/omarchy/current/theme/colors.toml` for the accent colour
  (or `colors.toml` in the folder `$CYMTX_THEME_DIR` names, when it is set:
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
is a folder picker that lists folders as you browse them; CymtX keeps only
the folder you pick.

## Processes it starts

| Process | When |
| --- | --- |
| `ffmpeg` | One per song being played or prepared (at most two at a time, plus a seek), decoding to a pipe |
| `nice -n 19 ffmpeg` | Once per song in your library, one at a time, to analyse it; at the lowest CPU priority |
| `hyprctl -j getoption decoration:rounding` | Once a second while CymtX is open, to match Hyprland's window corners |
| `hyprctl dispatch` (focus CymtX) | When a second `cymtx open` or an MPRIS Raise asks to bring the window forward |

Each tool is run from `/usr/bin` (then `/bin`, `/usr/local/bin`), and from
your `PATH` only when it isn't installed there, so an executable placed in a
personal folder such as `~/.local/bin` can't stand in for `ffmpeg`. ffmpeg
gets `-protocol_whitelist file` on every input, so a file that is really a
playlist (HLS, concat) can't make it open a network connection.

The uncomfortable part: the first time CymtX sees a large library, it decodes
every song once to analyse it. On a big library that is hours of background
work at the lowest priority; it uses CPU (and battery) while it runs, and it
is what Blend and Drift learn from. Results are cached, so it happens once
per file.

## Network

Off unless you turn on online lyrics. With it off (the default) CymtX opens
no network connection. There is no telemetry, no analytics, no accounts and
no automatic updates.

**Online lyrics** (the "Turn on online lyrics" button, or
`cymtx lyrics online on`): when a song starts and no lyrics for it are on
this computer or already cached, CymtX asks lrclib.net, a free lyrics
database, over HTTPS:

- `GET https://lrclib.net/api/get` with the song's title, its first artist,
  album, and length in whole seconds; if that misses,
- `GET https://lrclib.net/api/search` with the title and first artist.

The request carries the User-Agent `CymtX/0.1.0 (Omarchy music player)` and
nothing else about you: no file paths, no account, no identifier. LRCLIB
sees your IP address, as any website would, and which songs you play.
Redirects are followed only within the same site, at most three, and a reply
over 2 MiB is cut off and treated as a failed lookup. The lyrics address is
https only (plain http is accepted just for a server on this computer, which
the tests use). Each
song is looked up once: the answer, or the fact that there was none, is
cached (a miss is asked again after a week). When LRCLIB asks clients to
slow down, CymtX waits as long as it says. `cymtx lyrics online off` stops
all of it; the cache stays until you delete `~/.cache/cymtx/lyrics`.

Locally, CymtX listens only on a socket that only your user can open, in
your private runtime folder (`$XDG_RUNTIME_DIR`). Without one, CymtX opens no
socket at all rather than fall back to the shared `/tmp`, and terminal
commands say CymtX isn't reachable. One command is at most 64 KiB; a
connection that sends more without a newline is closed. CymtX also
registers `org.mpris.MediaPlayer2.cymtx` on your D-Bus session bus so media
keys and bar widgets can control it.

Anything running as your user can send commands on that socket or over
MPRIS: play, pause, queue files, change settings (online lyrics included),
or save a picture of the window to a new `.png` file.

## The bar widget (CymtX Player plugin)

The repository is also an Omarchy plugin, `tiedeyez.halo`, a bar widget
with a small player in a popup. It runs only `/usr/bin/cymtx`, never through
a shell, with one of a fixed list of commands: `status`, `toggle`, `next`,
`previous`, `moment`, `lyrics`, `seek`, `volume`, `shuffle`, `blend`,
`drift`, `repeat`, `sleep`, `visual` (each with `--json`) and `open`. The
only values it passes are whole numbers in a range (a second to seek to, a
volume, sleep minutes) and a few fixed words (`on`, `off`, `all`, `one`,
`next`, `previous`); text from a song, a setting or a reply is never part
of a command. So from the bar you can do what those commands do: play,
pause, skip, seek, change CymtX's volume and modes, start or stop the sleep
timer, save a moment and switch the visual. It cannot add or remove songs,
touch playlists, write tags or change any other setting.

It polls `status` every 3 to 15 seconds (every 2 while the player is open),
asks for the lyrics once a second only while the player is open and the
song has lyrics with times, reads at most 64 KiB of a reply, and stops a
command after five seconds. Asking for the lyrics reads what CymtX already
has; it does not turn online lyrics on or start a lookup of its own. It
shows the cover CymtX already cached in `~/.cache/cymtx`, and only opens it
if it is a local `.jpg` or `.png`. It writes no files, changes no
settings, and opens no network connection. What it shows (title, artist,
album, lyric lines) is drawn as plain text. While a song plays, its bar
icon is three small bars that move on a timer; they are decoration, drawn
from a fixed pattern, and the widget reads no audio.

## How to check this yourself

```sh
grep -rnE "QNetwork|QTcp|QUdp|connectToHost" src/        # network code: src/lyrics.cpp only
grep -rnE "QProcess|posix_spawn|startDetached" src/       # every process it starts
grep -rnE "QSaveFile|mkpath|QFile::link|\.save\(" src/    # every place it writes
```

While CymtX runs, `ls -l /proc/$(pgrep -x cymtx)/fd` shows what it has open,
and `ss -xp | grep cymtx` shows its one local socket.

## Maintainer and licence

Tiedeyez. MIT licence.
