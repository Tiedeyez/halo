// Decks spike: key lock (tempo change at constant pitch) on this CPU.
//
// Signalsmith Stretch (MIT) against Rubber Band (GPL-2.0+ or commercial; only
// measured here, see docs/DECKS.md for why Halo can't ship it), in real-time
// streaming use: one deck, 48 kHz stereo, the tempo fader at +6%, output
// pulled in audio-callback-sized blocks. Reports the average CPU (% of one
// core), the worst single block (what decides xruns at a small quantum) and
// the latency each adds.
//
// Build (Signalsmith headers from github.com/Signalsmith-Audio/signalsmith-stretch
// and /linear, fetched to a scratch dir; nothing is vendored into Halo yet):
//   g++ -O2 -std=c++17 -DHAVE_SIGNALSMITH -I$SS/signalsmith-stretch/include -I$SS/linear/include \
//       -DHAVE_RUBBERBAND $(pkg-config --cflags --libs rubberband) \
//       tools/decks-spikes/stretch-bench.cpp -o /tmp/stretch-bench
// Run:   /tmp/stretch-bench [input.f32 (48 kHz stereo f32le)] [block=256]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef HAVE_SIGNALSMITH
#include <signalsmith-stretch/signalsmith-stretch.h>
#endif
#ifdef HAVE_RUBBERBAND
#include <rubberband/RubberBandStretcher.h>
#endif

namespace {
constexpr int Rate = 48000;
constexpr double Tempo = 1.06;
using Clock = std::chrono::steady_clock;

struct Result {
    std::string name;
    double cpu = 0, worstUs = 0, p99Us = 0;
    int latency = 0;
};

std::vector<float> loadOrMake(const char *path, int seconds) {
    std::vector<float> lr;
    if (path) {
        if (FILE *f = std::fopen(path, "rb")) {
            lr.resize(size_t(Rate) * 2 * size_t(seconds));
            lr.resize(std::fread(lr.data(), sizeof(float), lr.size(), f));
            std::fclose(f);
            if (!lr.empty())
                return lr;
        }
    }
    // A stand-in for music: a few harmonic notes changing every half second
    // and noise bursts on the beat.
    lr.resize(size_t(Rate) * 2 * size_t(seconds));
    unsigned seed = 7;
    for (size_t i = 0; i < lr.size() / 2; ++i) {
        const double t = double(i) / Rate;
        const double f0 = 110 * std::pow(2, (int(t * 2) % 7) / 12.0);
        double v = 0;
        for (int h = 1; h <= 8; ++h)
            v += std::sin(2 * M_PI * f0 * h * t) / h;
        seed = seed * 1664525u + 1013904223u;
        const double beat = std::exp(-std::fmod(t, 0.5) * 30);
        v = 0.15 * v + 0.3 * beat * (double(seed >> 8) / double(1 << 24) - 0.5);
        lr[i * 2] = float(v);
        lr[i * 2 + 1] = float(v * 0.9);
    }
    return lr;
}

template <class Step> Result timeBlocks(const std::string &name, int block, int blocks, Step step) {
    std::vector<double> us;
    us.reserve(size_t(blocks));
    const auto start = Clock::now();
    for (int b = 0; b < blocks; ++b) {
        const auto t0 = Clock::now();
        step();
        us.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t0).count());
    }
    const double total = std::chrono::duration<double>(Clock::now() - start).count();
    Result r;
    r.name = name;
    r.cpu = total / (double(blocks) * block / Rate) * 100;
    std::sort(us.begin(), us.end());
    r.worstUs = us.back();
    r.p99Us = us[size_t(double(us.size() - 1) * 0.99)];
    return r;
}

#ifdef HAVE_SIGNALSMITH
Result runSignalsmith(const std::vector<float> &lr, int block, bool cheaper) {
    signalsmith::stretch::SignalsmithStretch<float> s;
    if (cheaper)
        s.presetCheaper(2, Rate, true);
    else
        s.presetDefault(2, Rate, true);
    const long frames = long(lr.size() / 2);
    std::vector<std::vector<float>> in(2, std::vector<float>(size_t(block) * 2 + 16)), out(2, std::vector<float>(size_t(block)));
    double pos = 0, carry = 0;
    const int blocks = int(double(frames - block * 4) / (block * Tempo));
    Result r = timeBlocks(cheaper ? "signalsmith cheaper" : "signalsmith default", block, blocks, [&] {
        carry += block * Tempo;
        const int want = int(carry);
        carry -= want;
        for (int i = 0; i < want; ++i) {
            const long at = std::min(frames - 1, long(pos) + i);
            in[0][size_t(i)] = lr[size_t(at) * 2];
            in[1][size_t(i)] = lr[size_t(at) * 2 + 1];
        }
        pos += want;
        s.process(in, want, out, block);
    });
    r.latency = s.inputLatency() + s.outputLatency();
    return r;
}
#endif

#ifdef HAVE_RUBBERBAND
Result runRubberband(const std::vector<float> &lr, int block, bool finer) {
    using RB = RubberBand::RubberBandStretcher;
    RB s(Rate, 2, RB::OptionProcessRealTime | (finer ? RB::OptionEngineFiner : RB::OptionEngineFaster), 1.0 / Tempo, 1.0);
    const long frames = long(lr.size() / 2);
    std::vector<float> inL(8192), inR(8192), outL(static_cast<size_t>(block)), outR(static_cast<size_t>(block));
    long pos = 0;
    const int blocks = int(double(frames - 16384) / (block * Tempo));
    // Prime with the pad it asks for, as a real deck would on load.
    {
        const size_t pad = s.getPreferredStartPad();
        std::vector<float> z(pad, 0.0f);
        const float *p[2] = {z.data(), z.data()};
        if (pad)
            s.process(p, pad, false);
    }
    Result r = timeBlocks(finer ? "rubberband R3 finer" : "rubberband R2 faster", block, blocks, [&] {
        while (s.available() < block) {
            const size_t need = std::min<size_t>(std::max<size_t>(s.getSamplesRequired(), 1), inL.size());
            for (size_t i = 0; i < need; ++i) {
                const long at = std::min(frames - 1, pos + long(i));
                inL[i] = lr[size_t(at) * 2];
                inR[i] = lr[size_t(at) * 2 + 1];
            }
            pos += long(need);
            const float *p[2] = {inL.data(), inR.data()};
            s.process(p, need, false);
        }
        float *o[2] = {outL.data(), outR.data()};
        s.retrieve(o, size_t(block));
    });
    r.latency = int(s.getStartDelay());
    return r;
}
#endif
} // namespace

int main(int argc, char **argv) {
    const int block = argc > 2 ? std::max(32, std::atoi(argv[2])) : 256;
    const std::vector<float> lr = loadOrMake(argc > 1 ? argv[1] : nullptr, 60);
    std::printf("key lock at %+.0f%% tempo, %d-frame blocks, %.0f s of %s\n", (Tempo - 1) * 100, block,
                double(lr.size() / 2) / Rate, argc > 1 ? argv[1] : "synthetic music");
    std::printf("%-22s %8s %10s %10s %9s %10s\n", "engine", "cpu", "p99 block", "worst", "budget", "latency");
    std::vector<Result> results;
#ifdef HAVE_SIGNALSMITH
    results.push_back(runSignalsmith(lr, block, false));
    results.push_back(runSignalsmith(lr, block, true));
#endif
#ifdef HAVE_RUBBERBAND
    results.push_back(runRubberband(lr, block, false));
    results.push_back(runRubberband(lr, block, true));
#endif
    const double budget = 1e6 * block / Rate;
    for (const Result &r : results)
        std::printf("%-22s %7.1f%% %8.0fus %8.0fus %7.0fus %6d (%.0f ms)\n", r.name.c_str(), r.cpu, r.p99Us, r.worstUs,
                    budget, r.latency, 1000.0 * r.latency / Rate);
    return 0;
}
