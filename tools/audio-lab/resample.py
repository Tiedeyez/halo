#!/usr/bin/env python3
"""Measures the decode/resample step Halo uses (ffmpeg -> f32le 48 kHz).

For each source rate, plays test tones through the same ffmpeg arguments
Halo uses, with and without a resampler filter, and reports:
  thdn_1k   THD+N of a -1 dBFS 1 kHz tone (dB, lower is better)
  spur_19k  largest spurious component next to a -1 dBFS 19 kHz tone,
            i.e. imaging/aliasing (dB below the tone, lower is better)
  hf_20k    level change of a 20 kHz tone (dB, 0 is flat)
usage: resample.py [ffmpeg]"""
import subprocess, sys, numpy as np

FF = sys.argv[1] if len(sys.argv) > 1 else "/usr/bin/ffmpeg"
OUT = 48000
VARIANTS = {
    "current (swr default)": [],
    "swr, high quality": ["-af", "aresample=48000:filter_size=64:phase_shift=10:linear_interp=1:cutoff=0.97"],
    "soxr, precision 28": ["-af", "aresample=48000:resampler=soxr:precision=28"],
    "soxr, precision 33": ["-af", "aresample=48000:resampler=soxr:precision=33"],
}

def render(src_rate, freq, variant):
    # 4 s tone at -1 dBFS, written as 64-bit float WAV so the source is exact.
    t = np.arange(int(src_rate * 4)) / src_rate
    x = (10 ** (-1 / 20)) * np.sin(2 * np.pi * freq * t)
    stereo = np.repeat(x[:, None], 2, axis=1).astype("<f8")
    wav = subprocess.run([FF, "-v", "error", "-f", "f64le", "-ar", str(src_rate), "-ac", "2", "-i", "pipe:0",
                          "-c:a", "pcm_f64le", "-f", "wav", "pipe:1"], input=stereo.tobytes(), capture_output=True).stdout
    # Halo's arguments (src/stream.cpp), plus the variant's filter.
    args = [FF, "-nostdin", "-v", "error", "-i", "pipe:0", "-map", "0:a:0", "-vn"] + VARIANTS[variant] + \
           ["-f", "f32le", "-ac", "2", "-ar", str(OUT), "pipe:1"]
    y = np.frombuffer(subprocess.run(args, input=wav, capture_output=True).stdout, "<f4").reshape(-1, 2)[:, 0].astype(np.float64)
    return y[OUT:OUT * 3]  # steady 2 s from the middle

def spectrum(y):
    w = np.blackman(len(y))
    s = np.abs(np.fft.rfft(y * w)) / (np.sum(w) / 2)
    return np.fft.rfftfreq(len(y), 1 / OUT), s

def thdn(y, f0):
    f, s = spectrum(y)
    p = s ** 2
    fund = (f > f0 - 20) & (f < f0 + 20)
    band = (f > 20) & (f < 20000)
    return 10 * np.log10(p[band & ~fund].sum() / p[fund].sum())

def spur(y, f0):
    f, s = spectrum(y)
    tone = s[(f > f0 - 20) & (f < f0 + 20)].max()
    other = s[((f < f0 - 50) | (f > f0 + 50)) & (f > 20)].max()
    return 20 * np.log10(other / tone)

def level(y):
    return 20 * np.log10(np.sqrt(np.mean(y ** 2)) * np.sqrt(2) / 10 ** (-1 / 20))

print(f"{'source':>8}  {'variant':<24} {'thdn_1k':>8} {'spur_19k':>9} {'hf_20k':>7}")
for rate in (44100, 48000, 88200, 96000):
    for v in VARIANTS:
        if rate == OUT and v != "current (swr default)":
            continue
        a = thdn(render(rate, 1000, v), 1000)
        b = spur(render(rate, 19000, v), 19000)
        c = level(render(rate, 20000, v))
        print(f"{rate:>8}  {v:<24} {a:8.1f} {b:9.1f} {c:7.2f}")
