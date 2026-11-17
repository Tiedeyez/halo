# Roadmap to a public release

Decided 2026-11-17. The bar is "triple A": every claim measured, every
screen at WCAG 2.2 AAA, nothing personal or borrowed in what ships.

## 1. Audio quality: the best Halo can do

Measure first, with test signals and real files, then change.

- [ ] Measurement harness: render the engine offline and run ffmpeg's
      decode path on test signals (sweeps, 1 kHz tones, near-0 dBFS
      material, 44.1 / 48 / 88.2 / 96 kHz sources). Report THD+N,
      aliasing, the limiter's gain reduction and its distortion.
- [ ] Resampling: ffmpeg's soxr resampler at high precision instead of
      the default, when the source rate isn't 48 kHz.
- [ ] Loudness: cap each song's boost by its measured true peak, so the
      limiter only ever catches blends and EQ boosts.
- [ ] Limiter: lookahead and true-peak (oversampled) detection, so it
      never flattens transients or creates intersample overs.
- [ ] 16-bit output: TPDF dither instead of truncation.
- [ ] EQ: double-precision filter state; check low-band noise and
      stability.
- [ ] Output rate: play at the source's rate when PipeWire allows it
      (bit-perfect when Blend, EQ, loudness and volume are all neutral),
      and say plainly in the window when that's on.

## 2. Pixel and ASCII, everywhere

Each effect switches off in Calm mode and under "reduce motion", and
must hold 60 fps on integrated graphics (Intel HD 520 is the floor).

- [ ] ASCII cover art: covers redrawn as glyphs in the theme accent
      (Now Playing toggle; on hover in the grid).
- [ ] ASCII spectrum from the real audio, in Now Playing and the bar.
- [ ] Pixel block-dissolve transitions between views.
- [ ] Terminal chrome: box-drawing panel borders, pixel-font section
      titles, an ASCII seek bar option, dithered list thumbnails.
- [ ] Performance budget measured on the laptop before and after.

## 3. First run

- [ ] Onboarding for a new user: find music folders (XDG Music, then
      ask), explain Blend, Drift and lyrics lookups in one screen, no
      account and nothing online until asked.
- [ ] Empty, scanning and error states written for strangers.
- [ ] No demo or bundled music.

## 4. Release readiness

- [ ] Security pass on every outside input: the lyrics network code,
      the control socket, tags and files from the library, lyrics files
      (bounded reads, no TOCTOU, plain-text sinks, absolute helper paths).
- [ ] AAA pass: 7:1 text contrast on every Omarchy theme (repair the
      palette where a theme falls short), 44 px targets, nothing
      clipped at 150 % text, every control named, motion reducible.
- [ ] Every Omarchy theme checked on screen.
- [ ] Neutral README, screenshots and reel with nothing personal and no
      commercial music.
- [ ] Versioning, CHANGELOG, AUR package, DISCLOSURE.md re-audited,
      GitHub release notes.
