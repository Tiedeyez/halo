# Decks spikes: findings so far

Three throwaway measurements for Halo Decks (scratching, tempo, two decks).
None of them is part of the app.

## Interpolation for scratching (`interp-bench.cpp`, run 2026-11-21, laptop i5 HD 520)

Error / alias level in dB (lower is better) and cost for two decks at 48 kHz:

| Interpolator | Tempo +-8 %, 15 kHz | 4x speed alias | Two decks, 1x to 4x |
| --- | --- | --- | --- |
| linear | -14 dB | -1.5 dB | 0.1-0.2 % of a core |
| hermite4 | -17 dB | -0.3 dB | 0.1-0.2 % |
| sinc32 | -95 to -102 dB | -121 dB | 3-11 % |
| **poly32** | **-96 to -102 dB** | **-121 dB** | **1-4 %** |
| poly64 | -113 to -117 dB | -117 dB | 2-7.5 % |

Linear and Hermite alias badly once the platter speeds up. **poly32** gives
sinc32's quality at about a third of the cost: the default for scratching,
with poly64 as a "best" setting. (2.6x up to 20.8 kHz shows passband droop
for poly32/poly64; worth a look if fast forward-scratches sound dull.)

## Not run yet

- `pw-latency.cpp`: how small a PipeWire quantum this machine holds. It
  makes its own silent null sink and never touches the playing audio, but
  run it when nothing is playing anyway.
- `stretch-bench.cpp`: key lock (tempo change without pitch change) with
  Signalsmith Stretch or Rubber Band. Needs those headers fetched to a
  scratch folder first; nothing is vendored.
