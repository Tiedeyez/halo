// Noise added by the ten-band EQ, float state/coefficients against double.
// g++ -O2 -std=c++20 -I src tools/audio-lab/eq.cpp -o /tmp/eq && /tmp/eq
#include <cmath>
#include <cstdio>
#include <vector>
#include "dsp.h"
constexpr double Rate = 48000, Pi = Dsp::Pi;

struct D { double b0, b1, b2, a1, a2; double z1 = 0, z2 = 0;
    double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };

D peakingD(double f, double q, double g) {
    const double a = std::pow(10.0, g / 40), w = 2 * Pi * f / Rate, al = std::sin(w) / (2 * q), a0 = 1 + al / a;
    return {(1 + al * a) / a0, -2 * std::cos(w) / a0, (1 - al * a) / a0, -2 * std::cos(w) / a0, (1 - al / a) / a0};
}

int main(int argc, char **argv) {
    const double q = argc > 1 ? std::atof(argv[1]) : 1.41;
    const double gains[10] = {6, 5, 3, 0, -1, 0, 1, 2, 3, 3};   // a "bass and air" curve
    std::vector<Dsp::Coefficients> cf; std::vector<Dsp::Biquad> sf(10); std::vector<D> sd;
    for (int b = 0; b < 10; ++b) { cf.push_back(Dsp::peaking(Rate, Dsp::EqFrequencies[b], q, gains[b])); sd.push_back(peakingD(Dsp::EqFrequencies[b], q, gains[b])); }
    // Reference: everything in double. Error of the float path against it,
    // for a quiet 1 kHz tone plus a 40 Hz bass line.
    double err = 0, sig = 0;
    const size_t n = size_t(Rate * 6);
    for (size_t i = 0; i < n; ++i) {
        const double x = 0.05 * std::sin(2 * Pi * 1000 * i / Rate) + 0.2 * std::sin(2 * Pi * 40 * i / Rate);
        double yd = x; for (auto &f : sd) yd = f.process(yd);
        float yf = float(x); for (int b = 0; b < 10; ++b) yf = sf[b].process(cf[b], yf);
        if (i > Rate) { err += (yf - yd) * (yf - yd); sig += yd * yd; }
    }
    std::printf("float EQ error against double: %.1f dB below the signal (Q %.2f)\n", 10 * std::log10(err / sig), q);
}
