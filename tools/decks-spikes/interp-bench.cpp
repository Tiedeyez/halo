// Decks spike: variable-rate interpolation for scratching and the tempo fader,
// quality against cost, on this CPU.
//
// Interpolators: linear, 4-point cubic Hermite, and a Kaiser-windowed sinc
// read from a 512-phase table with linear interpolation between phases, at
// 16, 32 and 64 taps. Above unit rate the sinc's cutoff falls to 1/rate and
// its span grows to match (taps x rate), so a record sped up past normal
// does not fold its top octave back down as alias; the cost grows with the
// rate, which is why the bench reports it at 1x, 2x and 4x.
//
// Quality: a pure tone through each interpolator at a constant rate; the
// output's spectrum (Kaiser-20 window, double precision, floor below
// -200 dB) gives the tone's level against its ideal (passband error) and the
// largest spur anywhere else (SFDR, dBc; or, when the ideal output is above
// Nyquist and should vanish, the alias left behind, in dB under the input).
// Cost: ns per stereo output frame, single thread, best of 5.
//
// Build (the same flags Halo ships with: plain x86-64, -O2):
//   g++ -O2 -std=c++17 tools/decks-spikes/interp-bench.cpp -o /tmp/interp-bench && /tmp/interp-bench
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

namespace {
constexpr double Pi = 3.14159265358979323846;
constexpr double Fs = 48000;

double besselI0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 64; ++k) {
        term *= (x / (2 * k)) * (x / (2 * k));
        sum += term;
        if (term < 1e-20 * sum)
            break;
    }
    return sum;
}

// A windowed-sinc kernel, tabulated at `Phases` points per input sample over
// its whole span, plus one guard point so phase interpolation never reads
// past the end.
struct SincTable {
    int taps;
    static constexpr int Phases = 512;
    double cutoff; // fraction of Nyquist at unit rate
    std::vector<float> table;

    SincTable(int n, double cut, double beta) : taps(n), cutoff(cut) {
        const int points = n * Phases + 1;
        table.resize(size_t(points) + 1);
        const double half = n / 2.0;
        for (int i = 0; i <= points; ++i) {
            const double t = double(i) / Phases - half; // in input samples
            const double x = t / half;
            const double w = std::fabs(x) <= 1 ? besselI0(beta * std::sqrt(1 - x * x)) / besselI0(beta) : 0;
            const double a = Pi * cut * t;
            const double s = std::fabs(t) < 1e-12 ? 1 : std::sin(a) / a;
            table[size_t(i)] = float(cut * s * w);
        }
    }
    // Kernel value at t input samples from the centre, cutoff scaled by `scale` (<= 1).
    float at(double t, double scale) const {
        const double pos = (t * scale + taps / 2.0) * Phases;
        if (pos < 0 || pos >= double(taps * Phases))
            return 0;
        const int i = int(pos);
        const float f = float(pos - i);
        return (table[size_t(i)] + f * (table[size_t(i) + 1] - table[size_t(i)])) * float(scale);
    }
};

// The shipping layout: phase-major polyphase tables, one per speed band.
// Band k serves speeds up to k with a cutoff of 1/k and taps x k, so the
// inner loop is two contiguous rows blended by the phase fraction and one dot
// product per channel, all vectorisable. A speed between bands uses the next
// band up (its cutoff is then at most one band step low: a slightly duller
// top octave during a fast scratch, never alias).
struct PolyBank {
    struct Band {
        double speed;
        int taps, phases;
        std::vector<float> rows; // (phases + 1) x taps
    };
    std::vector<Band> bands;
    PolyBank(int taps, double cut, double beta, const std::vector<double> &speeds) {
        for (double k : speeds) {
            Band b;
            b.speed = k;
            b.taps = int(std::ceil(taps * k / 2)) * 2;
            b.phases = k <= 1 ? 512 : 256;
            b.rows.resize(size_t(b.phases + 1) * size_t(b.taps));
            const double half = b.taps / 2.0;
            for (int p = 0; p <= b.phases; ++p)
                for (int j = 0; j < b.taps; ++j) {
                    // Tap j of the row for fraction p/phases sits this far from the read point.
                    const double t = double(j) - (half - 1) - double(p) / b.phases;
                    const double x = t / half;
                    const double w = std::fabs(x) <= 1 ? besselI0(beta * std::sqrt(1 - x * x)) / besselI0(beta) : 0;
                    const double c = cut / k;
                    const double a = Pi * c * t;
                    b.rows[size_t(p) * size_t(b.taps) + size_t(j)] =
                        float(c * (std::fabs(t) < 1e-12 ? 1 : std::sin(a) / a) * w);
                }
            bands.push_back(std::move(b));
        }
    }
    const Band &pick(double speed) const {
        for (const Band &b : bands)
            if (speed <= b.speed + 1e-9)
                return b;
        return bands.back();
    }
};

// Source: interleaved stereo float, read at a fractional position.
struct Source {
    std::vector<float> lr;
    long frames() const { return long(lr.size() / 2); }
    float l(long i) const { return (i >= 0 && i < frames()) ? lr[size_t(i) * 2] : 0.0f; }
    float r(long i) const { return (i >= 0 && i < frames()) ? lr[size_t(i) * 2 + 1] : 0.0f; }
};

enum class Kind { Linear, Hermite, Sinc, Poly };

struct Interp {
    std::string name;
    Kind kind;
    const SincTable *sinc = nullptr;
    const PolyBank *poly = nullptr;
    std::vector<float> coef; // scratch, sized for the widest kernel

    // One stereo frame at position `pos` (input samples) for playback rate
    // `rate` (sign ignored for the filter's cutoff).
    void read(const Source &s, double pos, double rate, float &outL, float &outR) {
        const long i = long(std::floor(pos));
        const float f = float(pos - double(i));
        if (kind == Kind::Linear) {
            outL = s.l(i) + f * (s.l(i + 1) - s.l(i));
            outR = s.r(i) + f * (s.r(i + 1) - s.r(i));
            return;
        }
        if (kind == Kind::Hermite) {
            auto h = [f](float xm1, float x0, float x1, float x2) {
                const float c1 = 0.5f * (x1 - xm1);
                const float c2 = xm1 - 2.5f * x0 + 2 * x1 - 0.5f * x2;
                const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            };
            outL = h(s.l(i - 1), s.l(i), s.l(i + 1), s.l(i + 2));
            outR = h(s.r(i - 1), s.r(i), s.r(i + 1), s.r(i + 2));
            return;
        }
        if (kind == Kind::Poly) {
            const PolyBank::Band &b = poly->pick(std::fabs(rate));
            const int n = b.taps;
            const long first = i - n / 2 + 1;
            const float ph = f * float(b.phases);
            const int p = std::min(int(ph), b.phases - 1);
            const float a = ph - float(p);
            const float *r0 = b.rows.data() + size_t(p) * size_t(n);
            const float *r1 = r0 + n;
            float *c = coef.data();
            for (int k = 0; k < n; ++k)
                c[k] = r0[k] + a * (r1[k] - r0[k]);
            float accL = 0, accR = 0;
            if (first >= 0 && first + n <= s.frames()) {
                const float *x = s.lr.data() + first * 2;
                for (int k = 0; k < n; ++k) {
                    accL += c[k] * x[k * 2];
                    accR += c[k] * x[k * 2 + 1];
                }
            } else {
                for (int k = 0; k < n; ++k) {
                    accL += c[k] * s.l(first + k);
                    accR += c[k] * s.r(first + k);
                }
            }
            outL = accL;
            outR = accR;
            return;
        }
        const double speed = std::fabs(rate);
        const double scale = speed > 1 ? 1 / speed : 1;
        const int half = int(std::ceil(sinc->taps / 2.0 / scale));
        const long first = i - half + 1;
        const int n = half * 2;
        for (int k = 0; k < n; ++k)
            coef[size_t(k)] = sinc->at(double(first + k) - pos, scale);
        float accL = 0, accR = 0;
        if (first >= 0 && first + n <= s.frames()) {
            const float *x = s.lr.data() + first * 2;
            for (int k = 0; k < n; ++k) {
                accL += coef[size_t(k)] * x[k * 2];
                accR += coef[size_t(k)] * x[k * 2 + 1];
            }
        } else {
            for (int k = 0; k < n; ++k) {
                accL += coef[size_t(k)] * s.l(first + k);
                accR += coef[size_t(k)] * s.r(first + k);
            }
        }
        outL = accL;
        outR = accR;
    }
};

void fft(std::vector<std::complex<double>> &a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * Pi / double(len);
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> w = std::polar(1.0, ang * double(k));
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
    }
}

struct Quality {
    double toneDb;  // the wanted tone's level re its ideal (0 = exact); NaN if above Nyquist
    double spurDb;  // largest other component, dBc (or dB re input when no tone is wanted)
};

Quality measure(Interp &ip, double toneHz, double rate) {
    constexpr size_t N = 1 << 15;
    const double speed = std::fabs(rate);
    const long inFrames = long(double(N) * speed) + 512;
    Source src;
    src.lr.resize(size_t(inFrames) * 2);
    for (long i = 0; i < inFrames; ++i) {
        const float v = float(0.5 * std::sin(2 * Pi * toneHz * double(i) / Fs));
        src.lr[size_t(i) * 2] = v;
        src.lr[size_t(i) * 2 + 1] = v;
    }
    // Start well inside, on an awkward fraction, and step at `rate`.
    double pos = rate > 0 ? 200.37 : double(inFrames) - 200.37;
    std::vector<std::complex<double>> spec(N);
    const double beta = 20;
    for (size_t n = 0; n < N; ++n) {
        float l, r;
        ip.read(src, pos, rate, l, r);
        pos += rate;
        const double x = 2.0 * double(n) / double(N - 1) - 1;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1 - x * x))) / besselI0(beta);
        spec[n] = {double(l) * w, 0};
    }
    fft(spec);
    // Reference: the same window on an ideal 0.5 tone gives this peak.
    double wsum = 0;
    for (size_t n = 0; n < N; ++n) {
        const double x = 2.0 * double(n) / double(N - 1) - 1;
        wsum += besselI0(beta * std::sqrt(std::max(0.0, 1 - x * x))) / besselI0(beta);
    }
    const double ref = 0.5 * wsum / 2;
    const double outHz = toneHz * speed;
    const bool wanted = outHz < Fs / 2;
    const double bin = outHz / Fs * double(N);
    const int guard = 40; // main lobe of a Kaiser-20 window is ~13 bins wide each side
    double tone = 0, toneEnergy = 0, spur = 0, w2 = 0;
    for (size_t n = 0; n < N; ++n) {
        const double x = 2.0 * double(n) / double(N - 1) - 1;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1 - x * x))) / besselI0(beta);
        w2 += w * w;
    }
    for (size_t k = 1; k < N / 2; ++k) {
        const double m = std::abs(spec[k]);
        if (wanted && std::fabs(double(k) - bin) <= guard) {
            tone = std::max(tone, m);
            toneEnergy += m * m;
        } else if (k > 8)
            spur = std::max(spur, m);
    }
    Quality q;
    // Lobe energy against an ideal 0.5 tone's, N x (0.5 / 2)^2 x sum(w^2)
    // (Parseval, one side): no scalloping, whatever bin the tone lands on.
    q.toneDb = wanted ? 10 * std::log10(toneEnergy / (double(N) * 0.0625 * w2)) : NAN;
    q.spurDb = 20 * std::log10(std::max(spur, 1e-15) / (wanted ? std::max(tone, 1e-15) : ref));
    return q;
}

double costNs(Interp &ip, double rate) {
    constexpr long Frames = 1 << 18;
    Source src;
    const long inFrames = long(double(Frames) * std::fabs(rate)) + 512;
    src.lr.resize(size_t(inFrames) * 2);
    unsigned seed = 1;
    for (auto &v : src.lr) {
        seed = seed * 1664525u + 1013904223u;
        v = float(seed >> 8) / float(1 << 24) - 0.5f;
    }
    double best = 1e30;
    volatile float sink = 0;
    for (int run = 0; run < 5; ++run) {
        double pos = 100.25;
        float acc = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (long n = 0; n < Frames; ++n) {
            float l, r;
            ip.read(src, pos, rate, l, r);
            acc += l + r;
            pos += rate;
        }
        const auto t1 = std::chrono::steady_clock::now();
        sink = acc;
        best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / double(Frames));
    }
    (void)sink;
    return best;
}
} // namespace

int main() {
    // Cutoffs leave a transition band that ends at Nyquist; beta sets the
    // stopband (beta 6 ~ -63 dB, 8 ~ -80 dB, 10 ~ -100 dB).
    const SincTable s16(16, 0.80, 6.0), s32(32, 0.88, 8.0), s64(64, 0.92, 10.0);
    std::vector<Interp> ips;
    ips.push_back({"linear", Kind::Linear});
    ips.push_back({"hermite4", Kind::Hermite});
    ips.push_back({"sinc16", Kind::Sinc, &s16});
    ips.push_back({"sinc32", Kind::Sinc, &s32});
    ips.push_back({"sinc64", Kind::Sinc, &s64});
    const std::vector<double> speeds{1, 1.25, 1.5, 1.75, 2, 2.5, 3, 4, 5, 6, 8};
    const PolyBank p32(32, 0.88, 8.0, speeds), p64(64, 0.92, 10.0, speeds);
    ips.push_back({"poly32", Kind::Poly, nullptr, &p32});
    ips.push_back({"poly64", Kind::Poly, nullptr, &p64});
    for (auto &ip : ips)
        ip.coef.resize(64 * 8 + 8);
    size_t bytes32 = 0, bytes64 = 0;
    for (auto &b : p32.bands)
        bytes32 += b.rows.size() * sizeof(float);
    for (auto &b : p64.bands)
        bytes64 += b.rows.size() * sizeof(float);
    std::printf("poly32 tables %.0f KB (1x band %.0f KB); poly64 %.0f KB (1x band %.0f KB)\n\n", bytes32 / 1024.0,
                double(p32.bands[0].rows.size()) * 4 / 1024.0, bytes64 / 1024.0,
                double(p64.bands[0].rows.size()) * 4 / 1024.0);

    struct Test {
        const char *what;
        double tone, rate;
    };
    const std::vector<Test> tests{
        {"fractional delay, 1 kHz", 1000, 1.0},  {"fractional delay, 10 kHz", 10000, 1.0},
        {"fractional delay, 18 kHz", 18000, 1.0}, {"tempo +8%, 15 kHz", 15000, 1.08},
        {"tempo -8%, 15 kHz", 15000, 0.92},       {"half speed, 10 kHz", 10000, 0.5},
        {"slow drag 0.1x, 15 kHz", 15000, 0.1},   {"reverse 1x, 10 kHz", 10000, -1.0},
        {"2x, 8 kHz (to 16k)", 8000, 2.0},       {"2x, 15 kHz (alias)", 15000, 2.0},
        {"4x, 3 kHz (to 12k)", 3000, 4.0},       {"4x, 9 kHz (alias)", 9000, 4.0},
        {"2.6x, 8 kHz (to 20.8k)", 8000, 2.6},   {"2.6x, 12 kHz (alias)", 12000, 2.6},
    };

    std::printf("Quality: tone level re ideal (dB) / largest spur (dBc, or alias re input)\n");
    std::printf("%-28s", "case");
    for (auto &ip : ips)
        std::printf(" %16s", ip.name.c_str());
    std::printf("\n");
    for (const Test &t : tests) {
        std::printf("%-28s", t.what);
        for (auto &ip : ips) {
            const Quality q = measure(ip, t.tone, t.rate);
            if (std::isnan(q.toneDb))
                std::printf("     -- / %6.1f", q.spurDb);
            else
                std::printf(" %+6.2f / %6.1f", q.toneDb, q.spurDb);
        }
        std::printf("\n");
        std::fflush(stdout);
    }

    std::printf("\nCost: ns per stereo output frame (one core), and %% of one core for two decks at 48 kHz\n");
    std::printf("%-10s %16s %16s %16s %16s %16s\n", "interp", "1.03x", "-1x", "0.3x", "2x", "4x");
    for (auto &ip : ips) {
        std::printf("%-10s", ip.name.c_str());
        for (double rate : {1.03, -1.0, 0.3, 2.0, 4.0}) {
            const double ns = costNs(ip, rate);
            std::printf(" %6.1f ns %5.2f%%", ns, ns * 2 * Fs / 1e9 * 100);
        }
        std::printf("\n");
        std::fflush(stdout);
    }
    return 0;
}
