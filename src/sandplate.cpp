#include "sandplate.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

#include "kaleidopilot.h"
#include "visual.h"

namespace Plate {

namespace {
constexpr double Pi = 3.14159265358979323846;

double besselJ(int n, double x) { return std::cyl_bessel_j(double(n), x); }
double besselI(int n, double x) { return std::cyl_bessel_i(double(n), x); }
double besselJPrime(int n, double x) { return n == 0 ? -besselJ(1, x) : 0.5 * (besselJ(n - 1, x) - besselJ(n + 1, x)); }
double besselIPrime(int n, double x) { return n == 0 ? besselI(1, x) : 0.5 * (besselI(n - 1, x) + besselI(n + 1, x)); }

// The free edge at r = 1, for the J part and the I part of a mode: the
// bending moment and the shear each leaves there. The I part is divided by
// I_n(L), which grows like e^L, so the numbers stay ordinary.
struct Edge {
    double momentJ, shearJ, momentI, shearI;
};
Edge edge(int n, double l, double nu) {
    const double j = besselJ(n, l), jp = besselJPrime(n, l);
    const double ip = besselIPrime(n, l) / besselI(n, l);   // I'/I
    const double n2 = double(n) * n, k = 1 - nu;
    return {-l * l * j - k * (l * jp - n2 * j), -l * l * l * jp - k * n2 * (l * jp - j),
            l * l - k * (l * ip - n2), l * l * l * ip - k * n2 * (l * ip - 1)};
}
}

double characteristic(int n, double lambda, double nu) {
    const Edge e = edge(n, lambda, nu);
    return e.momentJ * e.shearI - e.momentI * e.shearJ;
}

std::vector<double> eigenvalues(int n, int count, double nu) {
    std::vector<double> found;
    // Roots are about pi apart; a step of 0.05 cannot miss one. Starting
    // above zero leaves out the motions that are not vibration.
    const double step = 0.05;
    double a = 1.0, fa = characteristic(n, a, nu);
    while (int(found.size()) < count && a < 400) {
        const double b = a + step, fb = characteristic(n, b, nu);
        if ((fa < 0) != (fb < 0)) {
            double lo = a, hi = b, flo = fa;
            for (int i = 0; i < 60; ++i) {
                const double mid = 0.5 * (lo + hi), fm = characteristic(n, mid, nu);
                if ((fm < 0) == (flo < 0)) {
                    lo = mid;
                    flo = fm;
                } else {
                    hi = mid;
                }
            }
            found.push_back(0.5 * (lo + hi));
        }
        a = b;
        fa = fb;
    }
    return found;
}

Mode mode(int n, int ring, double lambda, double nu) {
    Mode m;
    m.n = n;
    m.ring = ring;
    m.lambda = lambda;
    const Edge e = edge(n, lambda, nu);
    // No bending moment at the edge fixes how much of the I part there is.
    const double mix = -e.momentJ / e.momentI;
    const double atEdge = besselI(n, lambda);
    m.shape.resize(Samples);
    double largest = 1e-12;
    for (int i = 0; i < Samples; ++i) {
        const double r = double(i) / (Samples - 1);
        const double w = besselJ(n, lambda * r) + mix * besselI(n, lambda * r) / atEdge;
        m.shape[size_t(i)] = float(w);
        largest = std::max(largest, std::abs(w));
    }
    for (float &w : m.shape)
        w = float(w / largest);
    m.slope.resize(Samples);
    for (int i = 0; i < Samples; ++i) {
        const int a = std::max(0, i - 1), b = std::min(Samples - 1, i + 1);
        m.slope[size_t(i)] = (m.shape[size_t(b)] - m.shape[size_t(a)]) * float(Samples - 1) / float(b - a);
    }
    return m;
}

Sand::Sand() {
    setFold(6);
    setGrains(6000);
}

void Sand::setFold(int fold) {
    fold = std::clamp(fold, 3, 12);
    if (fold == m_fold && !m_modes.empty())
        return;
    m_fold = fold;
    m_modes.clear();
    // The lowest ring mode sets the plate's pitch; every other mode's
    // follows from its eigenvalue squared.
    const double lowest = eigenvalues(0, 1).front();
    const double hertzPerLambda2 = LowestRingHertz / (lowest * lowest);
    for (int harmonic = 0; harmonic < 3; ++harmonic) {
        const int n = harmonic * fold;
        const auto roots = eigenvalues(n, harmonic == 0 ? 9 : harmonic == 1 ? 8 : 6);
        for (size_t ring = 0; ring < roots.size(); ++ring) {
            const double hertz = roots[ring] * roots[ring] * hertzPerLambda2;
            if (hertz > 16000)
                break;   // above what the spectrum hears
            Mode m = mode(n, int(ring), roots[ring]);
            m.harmonic = harmonic;
            m.hertz = hertz;
            m_modes.push_back(std::move(m));
        }
    }
    m_ringing.assign(m_modes.size(), 0.0);
    m_usual.assign(m_modes.size(), 0.3);
    m_spread.assign(m_modes.size(), 0.1);
    m_weight.assign(m_modes.size(), 0.0);
    buildProfiles();
    countSand();
}

float Sand::random01() {
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return float(m_seed & 0xFFFFFF) / float(0x1000000);
}

// Near enough to a bell curve for a grain's hop: the sum of four uniforms.
float Sand::randomNormal() { return (random01() + random01() + random01() + random01() - 2.0f) * 1.732f; }

void Sand::setGrains(int count) {
    count = std::clamp(count, 100, 40000);
    if (count == int(m_grains.size()))
        return;
    // Sand strewn evenly over the plate.
    m_grains.resize(size_t(count));
    for (Grain &g : m_grains) {
        const double r = std::sqrt(random01()) * 0.98, a = random01() * 2 * Pi;
        g = {float(r * std::cos(a)), float(r * std::sin(a)), 0};
    }
    countSand();
}

void Sand::buildProfiles() {
    for (int harmonic = 0; harmonic < 3; ++harmonic) {
        auto &profile = m_profile[size_t(harmonic)];
        auto &slope = m_profileSlope[size_t(harmonic)];
        profile.assign(Samples, 0.0f);
        slope.assign(Samples, 0.0f);
        for (size_t k = 0; k < m_modes.size(); ++k) {
            const Mode &m = m_modes[k];
            const float w = float(m_weight[k]);
            if (m.harmonic != harmonic || w < 1e-4f)
                continue;
            for (int i = 0; i < Samples; ++i) {
                profile[size_t(i)] += w * m.shape[size_t(i)] * m.shape[size_t(i)];
                slope[size_t(i)] += w * 2 * m.shape[size_t(i)] * m.slope[size_t(i)];
            }
        }
    }
}

double Sand::fullCount(int ring) const {
    // Sand lying evenly puts N * 2 (ring + 1/2) / (Rings^2 Wedge) grains in
    // a bin of that ring (bins grow with the radius). Full is eight times
    // that, the sand covering about an eighth of the plate; near the centre
    // the bins are too small to count by, so a couple of grains fill one.
    const double even = double(m_grains.size()) * 2 * (ring + 0.5) / (double(Rings) * Rings * Wedge);
    return std::max(2.0, even * 8.0);
}

int Sand::bin(double r, double theta, int *turn) const {
    // F theta / pi, folded: 0..1 from a spoke to the next mirror line and
    // back again.
    double a = m_fold * theta / Pi;
    a -= 2 * std::floor(a * 0.5);
    const bool back = a > 1;
    if (turn)
        *turn = back ? -1 : 1;
    const int wedge = std::min(Wedge - 1, int((back ? 2 - a : a) * Wedge));
    const int ring = std::min(Rings - 1, int(std::max(0.0, r) * Rings));
    return ring * Wedge + wedge;
}

void Sand::countSand() {
    const size_t bins = size_t(Rings) * Wedge;
    m_count.assign(bins, 0.0f);
    m_heat.assign(bins, 0.0f);
    m_packed.resize(bins);
    m_scratch.resize(bins);
    for (const Grain &g : m_grains) {
        const size_t i = size_t(bin(std::sqrt(double(g.x) * g.x + double(g.y) * g.y), std::atan2(double(g.y), double(g.x))));
        m_count[i] += 1;
        m_heat[i] += g.heat;
    }
    pack();
}

void Sand::pack() {
    // How full, blurred 3 x 3 (single grains are noise). The wedge's two
    // edges are mirror lines, so the blur reflects there.
    for (int ring = 0; ring < Rings; ++ring) {
        const float full = float(fullCount(ring));
        for (int w = 0; w < Wedge; ++w) {
            const size_t i = size_t(ring) * Wedge + size_t(w);
            m_scratch[i] = (m_count[i - (w > 0)] + m_count[i] + m_count[i + (w < Wedge - 1)]) / (3 * full);
        }
    }
    for (int ring = 0; ring < Rings; ++ring)
        for (int w = 0; w < Wedge; ++w) {
            const size_t i = size_t(ring) * Wedge + size_t(w);
            const float f = (m_scratch[i - (ring > 0 ? Wedge : 0)] + m_scratch[i] + m_scratch[i + (ring < Rings - 1 ? Wedge : 0)]) / 3;
            // The pressure of heaped sand: nothing until it is full, steep after.
            m_packed[i] = f * f * f * f;
        }
}

void Sand::field(double r, double cosF, double sinF, double &e, double &dr, double &dtheta) const {
    // cos^2 of (harmonic F theta) and its derivative in theta, for the
    // three harmonics: 0, F, 2F.
    const double c2 = 2 * cosF * cosF - 1, s2 = 2 * sinF * cosF;
    const double square[3] = {1.0, cosF * cosF, c2 * c2};
    const double turn[3] = {0.0, -2.0 * m_fold * sinF * cosF, -4.0 * m_fold * s2 * c2};
    const double at = std::clamp(r, 0.0, 1.0) * (Samples - 1);
    const int i = std::min(int(at), Samples - 2);
    const double f = at - i;
    e = dr = dtheta = 0;
    for (int harmonic = 0; harmonic < 3; ++harmonic) {
        const auto &profile = m_profile[size_t(harmonic)];
        const auto &slope = m_profileSlope[size_t(harmonic)];
        const double motion = profile[size_t(i)] * (1 - f) + profile[size_t(i + 1)] * f;
        e += motion * square[harmonic];
        dr += (slope[size_t(i)] * (1 - f) + slope[size_t(i + 1)] * f) * square[harmonic];
        dtheta += motion * turn[harmonic];
    }
}

double Sand::motion(double r, double cosF, double sinF) const {
    double e, dr, dtheta;
    field(r, cosF, sinF, e, dr, dtheta);
    return e;
}

void Sand::step(double dt, const std::vector<float> &bands, double pulse, double level) {
    dt = std::clamp(dt, 0.0, 0.05);
    if (dt <= 0 || bands.empty())
        return;
    // The beat: a blow to the whole plate.
    m_kick *= std::exp(-dt / 0.11);
    if (m_armed && pulse > 0.5) {
        m_armed = false;
        m_kick = 1;
    } else if (!m_armed && pulse < 0.3) {
        m_armed = true;
    }

    // A loud master holds every band near the top of the decibel scale;
    // the floor under them follows the music's own average.
    double mean = 0;
    for (float b : bands)
        mean += b;
    mean /= double(bands.size());
    m_floor += (0.6 * mean - m_floor) * (1 - std::exp(-dt / 0.8));

    // Each mode answers the spectrum at its own pitch, more for a sound
    // that has just arrived than for one that has been there all along, and
    // rings up and down in its own time (Q of 12).
    const double span = std::log(16000.0 / 40.0);
    double loudest = 1e-6;
    for (size_t k = 0; k < m_modes.size(); ++k) {
        const double at = std::clamp(std::log(m_modes[k].hertz / 40.0) / span, 0.0, 1.0) * double(bands.size() - 1);
        const size_t i = std::min(size_t(at), bands.size() - 2);
        const double heard = bands[i] * (1 - (at - double(i))) + bands[i + 1] * (at - double(i));
        const double shaped = m_floor >= 0.99 ? 0.0 : std::clamp((heard - m_floor) / (1 - m_floor), 0.0, 1.0);
        // Measured against what this mode usually hears and how much that
        // varies, so every mode has its turn: the kick's ring between the
        // hi-hat's lattice, not the bass over everything.
        const double above = (heard - m_usual[k]) / (m_spread[k] + 0.03);
        m_spread[k] += (std::abs(heard - m_usual[k]) - m_spread[k]) * (1 - std::exp(-dt / 1.2));
        m_usual[k] += (heard - m_usual[k]) * (1 - std::exp(-dt / 0.6));
        const double force = std::sqrt(shaped) * std::clamp(0.45 + 0.55 * above, 0.0, 1.6);
        const double ringTime = std::clamp(12.0 / (Pi * m_modes[k].hertz), 0.006, 0.09);
        m_ringing[k] += (force - m_ringing[k]) * (1 - std::exp(-dt / ringTime));
        loudest = std::max(loudest, m_ringing[k]);
    }
    // The sand answers the plate's speed squared, so the modes ringing
    // hardest shape the figure and the rest only blur it.
    double total = 0;
    for (size_t k = 0; k < m_modes.size(); ++k) {
        const double a = m_ringing[k] / loudest;
        m_weight[k] = a * a * a * a;
        total += m_weight[k];
    }
    for (double &w : m_weight)
        w /= std::max(total, 1e-9);
    buildProfiles();

    // Below the loudness that lifts a grain off the plate, nothing moves.
    const double lifted = std::clamp((level - 0.03) / 0.12, 0.0, 1.0);
    m_drive = lifted * std::min(1.0, 0.35 + 0.65 * loudest) + 0.6 * m_kick * lifted;
    if (m_drive < 1e-3)
        return;

    // Counted as the grains are walked, for the next frame's pressure and
    // for the picture.
    const size_t bins = size_t(Rings) * Wedge;
    m_nextCount.assign(bins, 0.0f);
    m_nextHeat.assign(bins, 0.0f);
    const float cooling = float(std::exp(-dt / 0.10));
    const double pull = 0.007 * m_drive * dt * 60;        // down the slope of the motion
    const double hop = 0.0075 * std::sqrt(m_drive * dt * 60);   // thrown about, where it moves
    const double blow = 0.011 * m_kick * std::sqrt(dt * 60);    // a beat throws even the sand lying still
    const double wedgeAngle = Pi / m_fold;                // one wedge, in radians
    const auto packed = [this](int ring, int wedge) {
        return double(m_packed[size_t(std::clamp(ring, 0, Rings - 1)) * Wedge + size_t(std::clamp(wedge, 0, Wedge - 1))]);
    };
    for (Grain &g : m_grains) {
        double r = std::sqrt(double(g.x) * g.x + double(g.y) * g.y);
        if (r < 0.015) {
            // The very centre has no direction; nudge off it.
            g.x += 0.02f * (random01() - 0.5f);
            g.y += 0.02f * (random01() - 0.5f);
            m_nextCount[0] += 1;
            continue;
        }
        const double theta = std::atan2(double(g.y), double(g.x));
        const double cosF = std::cos(m_fold * theta), sinF = std::sin(m_fold * theta);
        double e, dr, dtheta;
        field(r, cosF, sinF, e, dr, dtheta);
        const double ux = g.x / r, uy = g.y / r;
        // ...and away from where the sand is already heaped.
        int turn = 1;
        const int at = bin(r, theta, &turn);
        const int ring = at / Wedge, wedge = at % Wedge;
        m_nextCount[size_t(at)] += 1;
        m_nextHeat[size_t(at)] += g.heat;
        dr += 0.06 * (packed(ring + 1, wedge) - packed(ring - 1, wedge)) * Rings * 0.5;
        dtheta += 0.06 * turn * (packed(ring, wedge + 1) - packed(ring, wedge - 1)) * Wedge / (2 * wedgeAngle);
        const double gx = dr * ux - dtheta * uy / r, gy = dr * uy + dtheta * ux / r;
        const double thrown = hop * std::sqrt(std::min(1.0, e)) + blow;
        double dx = -pull * gx + thrown * randomNormal();
        double dy = -pull * gy + thrown * randomNormal();
        const double far = std::sqrt(dx * dx + dy * dy);
        if (far > 0.03) {
            dx *= 0.03 / far;
            dy *= 0.03 / far;
        }
        g.x += float(dx);
        g.y += float(dy);
        // In the air: thrown by a beat (or a knock), for a moment.
        g.heat = std::max(g.heat * cooling, float(m_kick));
        // The rim: a grain that reaches it comes back onto the plate.
        r = std::sqrt(double(g.x) * g.x + double(g.y) * g.y);
        if (r > 0.985) {
            const double back = (1.97 - r) / r;
            g.x = float(g.x * back);
            g.y = float(g.y * back);
        }
    }
    m_count.swap(m_nextCount);
    m_heat.swap(m_nextHeat);
    pack();
}

void Sand::tap(double x, double y, double force) {
    for (Grain &g : m_grains) {
        const double dx = g.x - x, dy = g.y - y;
        const double d = std::hypot(dx, dy);
        if (d > 0.3)
            continue;
        const double push = force * 0.09 * (1 - d / 0.3);
        const double a = d > 1e-4 ? std::atan2(dy, dx) + (random01() - 0.5) * 1.6 : random01() * 2 * Pi;
        g.x += float(std::cos(a) * push * (0.4 + random01()));
        g.y += float(std::sin(a) * push * (0.4 + random01()));
        g.heat = 1;
        const double r = std::hypot(double(g.x), double(g.y));
        if (r > 0.985) {
            g.x = float(g.x * 0.98 / r);
            g.y = float(g.y * 0.98 / r);
        }
    }
    countSand();
}

}

namespace {
constexpr float Bayer[4][4] = {{0 / 16.f, 8 / 16.f, 2 / 16.f, 10 / 16.f},
                               {12 / 16.f, 4 / 16.f, 14 / 16.f, 6 / 16.f},
                               {3 / 16.f, 11 / 16.f, 1 / 16.f, 9 / 16.f},
                               {15 / 16.f, 7 / 16.f, 13 / 16.f, 5 / 16.f}};

QRgb blend(QRgb a, QRgb b, double t) {
    const int w = int(std::clamp(t, 0.0, 1.0) * 256.0 + 0.5);
    const auto m = [w](int x, int y) { return (x * (256 - w) + y * w + 128) >> 8; };
    return qRgb(m(qRed(a), qRed(b)), m(qGreen(a), qGreen(b)), m(qBlue(a), qBlue(b)));
}

// A colour at a strength, premultiplied (the image's format).
QRgb light(QRgb c, double strength) {
    const int a = int(std::clamp(strength, 0.0, 1.0) * 255.0 + 0.5);
    return qRgba(qRed(c) * a / 255, qGreen(c) * a / 255, qBlue(c) * a / 255, a);
}

// A level in a few steps, dithered, like the other pixel visuals.
double stepped(double v, int steps, int x, int y) {
    return std::clamp(std::floor(std::clamp(v, 0.0, 1.0) * steps + Bayer[y & 3][x & 3]) / steps, 0.0, 1.0);
}
}

SandPlate::SandPlate(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    setRenderTarget(QQuickPaintedItem::Image);
    setFillColor(Qt::transparent);
    for (auto &c : m_palette)
        c = QColor("#FFD60A");
    m_quiet.assign(64, 0.0f);
    connect(this, &QQuickItem::widthChanged, this, [this] { ensureSize(); });
    connect(this, &QQuickItem::heightChanged, this, [this] { ensureSize(); });
}

QObject *SandPlate::spectrum() const { return m_spectrum; }

void SandPlate::setSpectrum(QObject *spectrum) {
    auto *s = qobject_cast<Spectrum *>(spectrum);
    if (s == m_spectrum)
        return;
    m_spectrum = s;
    emit changed();
}

void SandPlate::setPalette(const QStringList &palette) {
    if (palette == m_paletteNames)
        return;
    m_paletteNames = palette;
    for (size_t i = 0; i < m_palette.size(); ++i)
        m_palette[i] = QColor(palette.value(int(i), palette.value(0, "#FFD60A")));
    emit changed();
    render();
}

void SandPlate::setVivid(bool vivid) {
    if (vivid == m_vivid)
        return;
    m_vivid = vivid;
    emit changed();
    render();
}

void SandPlate::setStrength(double strength) {
    if (qFuzzyCompare(strength + 1, m_strength + 1))
        return;
    m_strength = strength;
    emit changed();
    render();
}

void SandPlate::setFlash(double flash) {
    if (qFuzzyCompare(flash + 1, m_flash + 1))
        return;
    m_flash = flash;
    emit changed();
}

void SandPlate::setCentre(const QPointF &centre) {
    if (centre == m_centre)
        return;
    m_centre = centre;
    m_cellsStale = true;
    emit changed();
    render();
}

void SandPlate::setRadius(double radius) {
    radius = std::max(4.0, radius);
    if (qFuzzyCompare(radius, m_radius))
        return;
    m_radius = radius;
    m_cellsStale = true;
    emit changed();
    render();
}

void SandPlate::setGrains(int grains) {
    if (grains == int(m_sand.grains().size()))
        return;
    m_sand.setGrains(grains);
    emit changed();
    render();
}

int SandPlate::foldFor(const QString &path, const QString &title) {
    // 5 to 12: fewer reads as a propeller, not a mandala.
    return 5 + int(Kaleido::fingerprint(path + "\n" + title) % 8);
}

void SandPlate::setSong(const QString &path, const QString &title) {
    const int fold = foldFor(path, title);
    if (fold == m_sand.fold())
        return;
    m_sand.setFold(fold);
    m_cellsStale = true;
    emit changed();
    render();
}

void SandPlate::ensureSize() {
    const int w = std::max(1, int(width())), h = std::max(1, int(height()));
    if (m_image.width() == w && m_image.height() == h)
        return;
    m_image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::transparent);
    m_cellsStale = true;
    render();
}

void SandPlate::step(double dt, double pulse, double level) {
    ensureSize();
    m_sand.step(dt, m_spectrum ? m_spectrum->fresh() : m_quiet, pulse, level);
    render();
}

void SandPlate::tap(double x, double y, double force) {
    m_sand.tap((x - m_centre.x()) / m_radius, (y - m_centre.y()) / m_radius, force);
    render();
}

void SandPlate::render() {
    if (m_image.isNull())
        return;
    const int w = m_image.width(), h = m_image.height();
    if (m_cellsStale)
        placeCells();
    const double cx = m_centre.x(), cy = m_centre.y();
    const QRgb accent = m_palette[0].rgb(), bright = m_palette[5].rgb(), shine = m_palette[4].rgb();
    const QRgb cool = m_palette[2].rgb(), warm = m_palette[3].rgb(), airborne = m_palette[1].rgb();
    const double kick = m_sand.kick(), drive = m_sand.drive();
    const double ceiling = m_strength + (m_flash - m_strength) * std::min(1.0, 0.4 * kick);

    // The colours a cell can take, worked out once a frame: by how much
    // sand (8 steps), whether it is in the air, and the cell's place in the
    // dither pattern (16); and the bare plate's glow by how much it moves.
    QRgb sandColour[2][9][16], glowColour[17][16];
    for (int level = 0; level <= 8; ++level)
        for (int d = 0; d < 16; ++d) {
            const double sand = level / 8.0, threshold = Bayer[d >> 2][d & 3];
            for (int hot = 0; hot < 2; ++hot) {
                if (!m_vivid) {
                    // The background: sand only, in a few steps, never
                    // brighter than text can be read over.
                    const double stepsOf = std::min(1.0, std::floor(sand * 4 + threshold) / 4);
                    sandColour[hot][level][d] = stepsOf > 0 ? light(blend(accent, shine, stepsOf > 0.99 ? 0.35 : 0), stepsOf * ceiling) : 0;
                } else {
                    // Sand: the accent where it lies thin, near white where
                    // it is heaped; grains in the air take the theme's cyan.
                    const double stepsOf = std::min(1.0, std::floor((0.45 + 0.55 * sand) * 5 + threshold) / 5);
                    const QRgb lying = blend(accent, bright, sand * 0.8 + 0.15 * kick);
                    sandColour[hot][level][d] = level == 0 ? 0 : (light(hot ? blend(lying, airborne, 0.65) : lying, stepsOf) | 0xFF000000u);
                }
            }
        }
    for (int level = 0; level <= 16; ++level)
        for (int d = 0; d < 16; ++d) {
            // Bare plate: its own motion glows, dim, cool where it moves a
            // little and warm where it moves most.
            const double e = level / 16.0;
            const double glow = std::min(1.0, std::floor(std::sqrt(e) * (0.06 + 0.24 * drive + 0.2 * kick) * 6 + Bayer[d >> 2][d & 3]) / 6);
            glowColour[level][d] = glow > 0 ? light(blend(cool, warm, e), glow) : 0;
        }
    const QRgb rim = light(accent, 0.3);

    // What each bin of the mandala's wedge shows: sand (how much, and
    // whether in the air) or the bare plate's glow. Every cell then only
    // looks its bin up.
    const int bins = Plate::Sand::Rings * Plate::Sand::Wedge;
    m_binLook.resize(size_t(bins));
    static const std::array<std::pair<float, float>, Plate::Sand::Wedge> wedgeAngle = [] {
        std::array<std::pair<float, float>, Plate::Sand::Wedge> out;
        for (int k = 0; k < Plate::Sand::Wedge; ++k)
            out[size_t(k)] = {float(std::cos(M_PI * (k + 0.5) / Plate::Sand::Wedge)), float(std::sin(M_PI * (k + 0.5) / Plate::Sand::Wedge))};
        return out;
    }();
    for (int bin = 0; bin < bins; ++bin) {
        const double sand = m_sand.sandIn(bin);
        if (sand > 0) {
            const int level = std::clamp(int(std::ceil((1 - std::exp(-1.6 * sand)) * 8)), 1, 8);
            m_binLook[size_t(bin)] = quint8(level | (m_sand.heatIn(bin) > 0.45 ? 0x10 : 0));
        } else if (!m_vivid) {
            m_binLook[size_t(bin)] = 0;
        } else if (bin / Plate::Sand::Wedge >= Plate::Sand::Rings - 2) {
            m_binLook[size_t(bin)] = 0x80;   // the rim
        } else {
            const auto &[c, sn] = wedgeAngle[size_t(bin % Plate::Sand::Wedge)];
            const double e = m_sand.motion((bin / Plate::Sand::Wedge + 0.5) / Plate::Sand::Rings, c, sn);
            m_binLook[size_t(bin)] = quint8(0x40 | std::clamp(int(e * 16 + 0.5), 0, 16));
        }
    }

    // Only the cells the plate covers; the rest stay clear.
    m_image.fill(Qt::transparent);
    const int x0 = std::max(0, int(cx - m_radius) - 1), x1 = std::min(w, int(cx + m_radius) + 2);
    const int y0 = std::max(0, int(cy - m_radius) - 1), y1 = std::min(h, int(cy + m_radius) + 2);
    for (int y = y0; y < y1; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(m_image.scanLine(y));
        const int *cellBin = m_cellBin.data() + size_t(y) * size_t(w);
        for (int x = x0; x < x1; ++x) {
            if (cellBin[x] < 0)
                continue;   // off the plate
            const quint8 look = m_binLook[size_t(cellBin[x])];
            if (!look)
                continue;
            const int d = ((y & 3) << 2) | (x & 3);
            row[x] = look & 0x80 ? rim : look & 0x40 ? glowColour[look & 0x1F][d] : sandColour[look >> 4][look & 0x0F][d];
        }
    }
    update();
}

void SandPlate::placeCells() {
    const int w = m_image.width(), h = m_image.height();
    m_cellBin.resize(size_t(w) * size_t(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double dx = (x + 0.5 - m_centre.x()) / m_radius, dy = (y + 0.5 - m_centre.y()) / m_radius;
            const double r = std::sqrt(dx * dx + dy * dy);
            m_cellBin[size_t(y) * size_t(w) + size_t(x)] = r <= 1.0 ? m_sand.bin(r, std::atan2(dy, dx)) : -1;
        }
    m_cellsStale = false;
}

void SandPlate::paint(QPainter *painter) {
    if (!m_image.isNull())
        painter->drawImage(QRectF(0, 0, width(), height()), m_image);
}
