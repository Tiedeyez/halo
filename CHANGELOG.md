# Changelog

Halo follows [semantic versioning](https://semver.org). Each release has
notes on its GitHub release page as well.

## Unreleased

- **The bar icon dances.** While a song plays, the note in the bar becomes
  three small pixel bars that keep moving, and it is the still note again
  when you pause. `"equalizer": false` in the widget's settings keeps the
  note; Omarchy's reduce-motion setting does too. The bars are decoration:
  the widget still runs only the five `halo` commands and reads no audio.

## 1.0.1 (2026-11-23)

Fixes from the Marketplace review of 1.0.0.

- **Library Doctor keeps every credit.** A field with several values (a FLAC
  with two ARTIST credits) had its whole list replaced by the one corrected
  value. A fix now changes only the value it is about, and undo puts back
  the exact list that was there. Undo also refuses if any value in the
  field changed since the fix. Halo now reads the file back after a
  fix and refuses the fix if the format didn't keep it (Windows Media files
  with several values in one field are left alone).
- **Playlists shared with other players keep what Halo doesn't play.** Pin,
  unpin, rename and describe now change only Halo's own line (and the name)
  in the file; every other byte stays. Adding, removing or moving songs
  rewrites the list with radio and online entries, their tags, other
  players' tags, relative paths and anything past Halo's 100,000-song limit
  still in it. The file also keeps its line endings, its byte-order
  mark, its `#EXTM3U` line with any attributes, and a song listed twice
  keeps both places. A playlist that isn't UTF-8 is written back byte for
  byte.
- **Text from tags is never read as markup.** Every label shows text as
  written, as do tooltips and the playlist names in "Add to playlist", so a
  tag or a playlist name can't make Halo fetch an image from the internet.

## 1.0.0 (2026-11-21)

The first public release.

### Playing music

- A native player for the music on your disk: albums, songs, playlists and
  smart playlists, queue, search. Folders are albums; disc folders join
  their parent.
- **Blend** mixes from one song into the next for eight beats at the
  outgoing tempo, with a bass swap, instead of a fixed crossfade. Gapless
  when Blend is off.
- **Drift** keeps going from your library when the queue runs out, choosing
  songs close in tempo and loudness from another album.
- **Moments** (M) pin the second you are hearing; they show as ticks on the
  seek bar and in their own view.
- **Suggestions** on a playlist offer songs that would fit, worked out on
  this computer.
- **Lost & Found** brings back songs you played often and haven't heard in a
  while. **Library Doctor** proposes tag fixes you approve, each undoable.
- **Live lyrics** (L) from sidecar .lrc files and tags. Looking lyrics up
  online (lrclib.net) is off until you turn it on.

### Sound

- Even loudness: every song measured with EBU R128 and true peak, with a
  lookahead true-peak limiter.
- Resampling with soxr at high precision, a double-precision ten-band EQ,
  dithered 16-bit output.
- Automatic headphone correction from bundled OPRA/AutoEQ data when your
  headphones are recognised.
- Beat detection that works on loud, compressed masters as well as quiet
  ones.

### Visuals

Twelve visuals, drawn from the song itself and in your Omarchy theme's
colours (V cycles them; C holds them still):

- **Halo**: a ring of light in 3D, moved by physics, sending out radiation
  on every beat.
- **Turntable** and **Hologram**: a record player seen from above, photoreal
  or made of light. The album turns on the label; the fader, the gain dial,
  start/stop and the tonearm work by hand.
- **Ring**, **Bars**, **Kaleidoscope**, **Skyline**, **Phosphor**,
  **Pulsar**, **Waterfall**, **Warp** and **Aurora**, lit like a wall of
  LEDs.

### First run, contrast and the bar

- A welcome screen when Halo finds no music, with a folder picker; Halo
  also looks in your desktop's music folder (`XDG_MUSIC_DIR`).
- Filled buttons in every Omarchy theme read at 7:1 (WCAG AAA): the fill is
  adjusted until black or white text on it reaches 7:1.
- **Halo Player**, a bar widget (Omarchy plugin `tiedeyez.halo`): the song
  playing, play/pause, next and previous, and a click to open Halo.
- `packaging/PKGBUILD` builds the tagged release and runs the tests.

### Omarchy

- Follows the Omarchy theme and language live; keyboard first (? shows
  every key).
- MPRIS, so media keys and bar widgets control it.
- A command line for scripts and agents (`halo status --json`, `halo add`,
  `halo analyze`, and more).
- Private by default: no account, no network connection unless you turn on
  lyrics lookups; Halo's files are readable only by you. See DISCLOSURE.md.
