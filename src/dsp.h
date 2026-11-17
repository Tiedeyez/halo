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

// Double precision: in float, the low bands' poles sit so close to the unit
// circle that rounding moved the response; the EQ's error against an exact
// reference was -49 dB, and is now below -140 (tools/audio-lab/eq.cpp).
struct Coefficients {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

inline Coefficients peaking(double rate, double frequency, double q, double gainDb) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * Pi * frequency / rate;
    const double alpha = std::sin(w) / (2 * q);
    const double a0 = 1 + alpha / a;
    return {((1 + alpha * a) / a0), (-2 * std::cos(w) / a0), ((1 - alpha * a) / a0),
            (-2 * std::cos(w) / a0), ((1 - alpha / a) / a0)};
}

inline Coefficients highPass(double rate, double frequency, double q) {
    const double w = 2 * Pi * frequency / rate;
    const double alpha = std::sin(w) / (2 * q);
    const double c = std::cos(w);
    const double a0 = 1 + alpha;
    return {((1 + c) / 2 / a0), (-(1 + c) / a0), ((1 + c) / 2 / a0), (-2 * c / a0),
            ((1 - alpha) / a0)};
}

inline Coefficients highShelf(double rate, double frequency, double q, double gainDb) {
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w = 2 * Pi * frequency / rate;
    const double alpha = std::sin(w) / (2 * q);
    const double c = std::cos(w);
    const double s = 2 * std::sqrt(a) * alpha;
    const double a0 = (a + 1) - (a - 1) * c + s;
    return {(a * ((a + 1) + (a - 1) * c + s) / a0), (-2 * a * ((a - 1) + (a + 1) * c) / a0),
            (a * ((a + 1) + (a - 1) * c - s) / a0), (2 * ((a - 1) - (a + 1) * c) / a0),
            (((a + 1) - (a - 1) * c - s) / a0)};
}

// Transposed direct form II, one channel.
struct Biquad {
    double z1 = 0, z2 = 0;
    float process(const Coefficients &c, float in) {
        const double x = in;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return float(y);
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

// A lookahead true-peak limiter for the output, allocation-free on the
// audio thread.
//
// Detection: each new sample is interpolated at three points between it and
// its neighbours (4x oversampling, 12-tap windowed sinc per phase), so peaks
// between samples are caught as well as the samples themselves.
// Gain: the gain each moment needs is held as a sliding minimum over the
// lookahead window and then averaged over the same window. The average of
// minima that all include a peak is no higher than that peak needs, so by
// the time the peak leaves the delay line the gain has already come down to
// it, smoothly, with no sample-level clipping. Rising gain waits
// `holdSeconds` (longer than half a cycle of 20 Hz bass, so the gain doesn't
// wobble between a bass note's peaks), then is released over
// `releaseSeconds`.
// Below the ceiling the gain is exactly 1, so quiet music passes through
// bit for bit, only delayed.
class TruePeakLimiter {
public:
    static constexpr int Taps = 12;          // per interpolation phase
    static constexpr int Window = 72;        // 1.5 ms at 48 kHz
    static constexpr int Delay = Window - 1 + Taps / 2;

    explicit TruePeakLimiter(double rate = 48000, double ceilingDb = -1.0, double releaseSeconds = 0.15,
                             double holdSeconds = 0.04)
        : m_ceiling(float(std::pow(10.0, ceilingDb / 20.0))),
          m_release(float(1.0 - std::exp(-1.0 / (rate * releaseSeconds)))),
          m_holdFrames(int(rate * holdSeconds)) {
        // Phase k/4 of a windowed sinc centred between taps 5 and 6.
        for (int k = 1; k < 4; ++k) {
            const double frac = k / 4.0;
            double sum = 0;
            for (int j = 0; j < Taps; ++j) {
                const double t = j - (Taps / 2 - 1) - frac;   // distance from the point being estimated
                const double sinc = t == 0 ? 1 : std::sin(Pi * t) / (Pi * t);
                const double w = 0.42 + 0.5 * std::cos(Pi * t / (Taps / 2.0)) + 0.08 * std::cos(2 * Pi * t / (Taps / 2.0));
                m_phase[k - 1][j] = float(sinc * w);
                sum += sinc * w;
            }
            for (float &c : m_phase[k - 1])
                c = float(c / sum);
        }
        reset();
    }

    void reset() {
        m_history.fill({});
        m_delay.fill({});
        m_need.fill(1.0f);
        m_head = m_minHead = m_minTail = 0;
        m_boxSum = Window;
        m_gain = 1;
        m_pos = 0;
        m_holding = 0;
        m_delayPos = 0;
    }

    float gain() const { return m_gain; }

    void process(float &l, float &r) {
        // Detection, on the input as it arrives.
        m_history[m_pos] = {l, r};
        float peak = 0;
        // The sample at the centre of the interpolators, and the three
        // points between it and the next.
        const int centre = (m_pos - Taps / 2 + HistorySize) % HistorySize;
        peak = std::max(std::fabs(m_history[centre][0]), std::fabs(m_history[centre][1]));
        for (const auto &phase : m_phase) {
            float il = 0, ir = 0;
            for (int j = 0; j < Taps; ++j) {
                const auto &h = m_history[(m_pos - (Taps - 1) + j + HistorySize) % HistorySize];
                il += phase[j] * h[0];
                ir += phase[j] * h[1];
            }
            peak = std::max(peak, std::max(std::fabs(il), std::fabs(ir)));
        }
        m_pos = (m_pos + 1) % HistorySize;

        // The gain that moment needs, held as a sliding minimum (monotonic
        // queue of indices into m_need).
        const float need = peak > m_ceiling ? m_ceiling / peak : 1.0f;
        const unsigned now = m_counter++;
        while (m_minTail != m_minHead && m_minValue[(m_minTail - 1) & QueueMask] >= need)
            --m_minTail;
        m_minValue[m_minTail & QueueMask] = need;
        m_minTime[m_minTail & QueueMask] = now;
        ++m_minTail;
        // One longer than the average, so a peak between two samples is
        // covered on both of them.
        while (now - m_minTime[m_minHead & QueueMask] >= unsigned(Window + 1))
            ++m_minHead;
        const float held = m_minValue[m_minHead & QueueMask];

        // Averaged over the window.
        m_boxSum += double(held) - m_need[m_head];
        m_need[m_head] = held;
        m_head = (m_head + 1) % Window;
        const float target = std::min(1.0f, float(m_boxSum / Window));

        // Down at once (the average already arrives on time); up only after
        // the hold, and gently.
        if (target < m_gain) {
            m_gain = target;
            m_holding = m_holdFrames;
        } else if (m_holding > 0) {
            --m_holding;
        } else {
            m_gain += (target - m_gain) * m_release;
        }
        if (m_gain > 0.9999999f && target >= 1.0f)
            m_gain = 1.0f;

        // Out of the delay line.
        const std::array<float, 2> delayed = m_delay[m_delayPos];
        m_delay[m_delayPos] = {l, r};
        m_delayPos = (m_delayPos + 1) % Delay;
        l = delayed[0] * m_gain;
        r = delayed[1] * m_gain;
    }

private:
    static constexpr int HistorySize = Taps;
    static constexpr unsigned QueueMask = 127;   // > Window
    float m_ceiling, m_release;
    int m_holdFrames, m_holding = 0;
    std::array<std::array<float, Taps>, 3> m_phase{};
    std::array<std::array<float, 2>, HistorySize> m_history{};
    int m_pos = 0;
    std::array<std::array<float, 2>, Delay> m_delay{};
    int m_delayPos = 0;
    std::array<float, QueueMask + 1> m_minValue{};
    std::array<unsigned, QueueMask + 1> m_minTime{};
    unsigned m_minHead = 0, m_minTail = 0, m_counter = 0;
    std::array<float, Window> m_need{};
    int m_head = 0;
    double m_boxSum = Window;
    float m_gain = 1;
};

}
