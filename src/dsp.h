#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <vector>

// Small, allocation-free signal processing used by the engine (on the audio
// thread), the spectrum and the analyser. Filters follow Robert
// Bristow-Johnson's Audio EQ Cookbook.
namespace Dsp {

constexpr double Pi = 3.14159265358979323846;

struct Coefficients {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

inline Coefficients peaking(double rate, double frequency, double q, double gainDb) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * Pi * frequency / rate;
    const double alpha = std::sin(w) / (2 * q);
    const double a0 = 1 + alpha / a;
    return {float((1 + alpha * a) / a0), float(-2 * std::cos(w) / a0), float((1 - alpha * a) / a0),
            float(-2 * std::cos(w) / a0), float((1 - alpha / a) / a0)};
}

inline Coefficients highPass(double rate, double frequency, double q) {
    const double w = 2 * Pi * frequency / rate;
    const double alpha = std::sin(w) / (2 * q);
    const double c = std::cos(w);
    const double a0 = 1 + alpha;
    return {float((1 + c) / 2 / a0), float(-(1 + c) / a0), float((1 + c) / 2 / a0), float(-2 * c / a0),
            float((1 - alpha) / a0)};
}

inline Coefficients highShelf(double rate, double frequency, double q, double gainDb) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * Pi * frequency / rate;
    const double alpha = std::sin(w) / (2 * q);
    const double c = std::cos(w);
    const double s = 2 * std::sqrt(a) * alpha;
    const double a0 = (a + 1) - (a - 1) * c + s;
    return {float(a * ((a + 1) + (a - 1) * c + s) / a0), float(-2 * a * ((a - 1) + (a + 1) * c) / a0),
            float(a * ((a + 1) + (a - 1) * c - s) / a0), float(2 * ((a - 1) - (a + 1) * c) / a0),
            float(((a + 1) - (a - 1) * c - s) / a0)};
}

// Transposed direct form II, one channel.
struct Biquad {
    float z1 = 0, z2 = 0;
    float process(const Coefficients &c, float x) {
        const float y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

// The ten bands of the equaliser, an octave apart.
constexpr std::array<double, 10> EqFrequencies{31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

// In-place radix-2 FFT; size must be a power of two.
inline void fft(std::vector<std::complex<float>> &data) {
    const size_t n = data.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (size_t length = 2; length <= n; length <<= 1) {
        const double angle = -2 * Pi / double(length);
        const std::complex<float> step(float(std::cos(angle)), float(std::sin(angle)));
        for (size_t i = 0; i < n; i += length) {
            std::complex<float> w(1, 0);
            for (size_t k = 0; k < length / 2; ++k) {
                const auto u = data[i + k];
                const auto v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= step;
            }
        }
    }
}

inline float dbToGain(double db) { return float(std::pow(10.0, db / 20.0)); }

}
