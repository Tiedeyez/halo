// Measures Halo's output limiter, old (instant sample-peak) against new
// (lookahead true-peak), on test signals.
// g++ -O2 -std=c++20 -I src tools/audio-lab/limiter.cpp -o /tmp/limiter && /tmp/limiter
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#include "dsp.h"

constexpr double Rate = 48000, Pi = 3.14159265358979323846;

struct Old {   // engine.cpp before this change
    float env = 0, ceiling = 0.97f, release = std::exp(-1.0f / (48000 * 0.150f));
    int delay() const { return 0; }
    void process(float &l, float &r) {
        const float peak = std::max(std::fabs(l), std::fabs(r));
        env = peak > env ? peak : env * release;
        if (env > ceiling) { const float g = ceiling / env; l *= g; r *= g; }
    }
};
struct New {
    Dsp::TruePeakLimiter lim;
    int delay() const { return Dsp::TruePeakLimiter::Delay; }
    void process(float &l, float &r) { lim.process(l, r); }
};

template <class L> std::vector<double> run(const std::vector<double> &x) {
    L lim;
    std::vector<double> y;
    for (double v : x) { float l = float(v), r = float(v); lim.process(l, r); y.push_back(l); }
    for (int i = 0; i < lim.delay(); ++i) { float l = 0, r = 0; lim.process(l, r); y.push_back(l); }
    return std::vector<double>(y.begin() + lim.delay(), y.end());
}

// True peak by 16x oversampling with a long windowed sinc.
double truePeakDb(const std::vector<double> &y) {
    double peak = 0;
    const int half = 64;
    for (size_t n = half; n + half < y.size(); ++n)
        for (int k = 0; k < 16; ++k) {
            const double frac = k / 16.0;
            double s = 0;
            for (int j = -half + 1; j <= half; ++j) {
                const double t = j - frac;
                const double sinc = t == 0 ? 1 : std::sin(Pi * t) / (Pi * t);
                const double w = 0.42 + 0.5 * std::cos(Pi * t / half) + 0.08 * std::cos(2 * Pi * t / half);
                s += y[n + j] * sinc * w;
            }
            peak = std::max(peak, std::fabs(s));
        }
    return 20 * std::log10(peak);
}

// THD+N of a sine at f over the steady middle: least-squares fit, residual.
double thdnDb(const std::vector<double> &y, double f) {
    const size_t a = y.size() / 4, b = 3 * y.size() / 4;
    double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    for (size_t n = a; n < b; ++n) {
        const double s = std::sin(2 * Pi * f * n / Rate), c = std::cos(2 * Pi * f * n / Rate);
        ss += s * s; cc += c * c; sc += s * c; ys += y[n] * s; yc += y[n] * c;
    }
    const double det = ss * cc - sc * sc, A = (ys * cc - yc * sc) / det, B = (yc * ss - ys * sc) / det;
    double sig = 0, res = 0;
    for (size_t n = a; n < b; ++n) {
        const double fit = A * std::sin(2 * Pi * f * n / Rate) + B * std::cos(2 * Pi * f * n / Rate);
        sig += fit * fit; res += (y[n] - fit) * (y[n] - fit);
    }
    return 10 * std::log10(res / sig);
}

std::vector<double> sine(double f, double amp, double seconds, double phase = 0) {
    std::vector<double> x(size_t(Rate * seconds));
    for (size_t n = 0; n < x.size(); ++n) x[n] = amp * std::sin(2 * Pi * f * n / Rate + phase);
    return x;
}

int main() {
    std::printf("%-44s %10s %10s\n", "signal", "old", "new");
    auto row = [](const char *name, double o, double n, const char *unit) { std::printf("%-44s %8.1f%-2s %8.1f%-2s\n", name, o, unit, n, unit); };

    auto quiet = sine(1000, 0.5, 1);
    auto qo = run<Old>(quiet), qn = run<New>(quiet);
    double diff = 0;
    for (size_t i = 0; i < quiet.size(); ++i) diff = std::max(diff, std::fabs(qn[i] - float(quiet[i])));
    std::printf("%-44s %10s %10s\n", "-6 dBFS 1 kHz sine passes untouched", "yes", diff == 0 ? "yes" : "NO");

    for (double over : {3.0, 6.0}) {
        auto loud = sine(1000, 0.891 * std::pow(10, over / 20), 2);
        char name[64]; std::snprintf(name, sizeof name, "THD+N, 1 kHz sine %g dB over -1 dBTP", over);
        row(name, thdnDb(run<Old>(loud), 1000), thdnDb(run<New>(loud), 1000), "dB");
    }
    auto bass = sine(60, 1.4, 2);
    row("THD+N, 60 Hz sine at +3 dBFS", thdnDb(run<Old>(bass), 60), thdnDb(run<New>(bass), 60), "dB");

    auto overs = sine(12000, 1.0, 1, Pi / 4);
    row("true peak out, 12 kHz full-scale sine", truePeakDb(run<Old>(overs)), truePeakDb(run<New>(overs)), "dB");
    auto q = sine(Rate / 4, 1.0, 1, Pi / 4);
    row("true peak out, fs/4 at 45 deg (0 dBTP)", truePeakDb(run<Old>(q)), truePeakDb(run<New>(q)), "dB");

    // A drum-like hit: 5 ms attack to 2.0 on a 100 Hz tone, then decay.
    std::vector<double> hit(static_cast<size_t>(Rate));
    for (size_t n = 0; n < hit.size(); ++n) {
        const double t = n / Rate, env = t < 0.005 ? t / 0.005 : std::exp(-(t - 0.005) * 8);
        hit[n] = 2.0 * env * std::sin(2 * Pi * 100 * t) + 0.3 * std::sin(2 * Pi * 3000 * t);
    }
    row("true peak out, loud drum hit", truePeakDb(run<Old>(hit)), truePeakDb(run<New>(hit)), "dB");
    return 0;
}
