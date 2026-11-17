#include "visual.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

#include "dsp.h"
#include "engine.h"

namespace {
constexpr size_t Window = 2048;
constexpr double SampleRate = 48000;
}

Spectrum::Spectrum(Engine *engine, QObject *parent)
    : QObject(parent), m_engine(engine), m_history(Window, 0.0f), m_read(8192),
      m_bands(Bands, 0.0f), m_peaks(Bands, 0.0f), m_peakVelocity(Bands, 0.0f) {
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &Spectrum::tick);
}

void Spectrum::setActive(bool active) {
    if (active == m_active)
        return;
    m_active = active;
    if (active)
        m_timer.start();
    else
        m_timer.stop();
    emit activeChanged();
}

std::vector<float> Spectrum::analyse(const std::vector<float> &samples) {
    std::vector<std::complex<float>> buffer(Window);
    for (size_t i = 0; i < Window; ++i) {
        const float w = float(0.5 - 0.5 * std::cos(2 * Dsp::Pi * double(i) / double(Window - 1)));
        buffer[i] = {i < samples.size() ? samples[i] * w : 0.0f, 0.0f};
    }
    Dsp::fft(buffer);
    std::vector<float> bands(Bands, 0.0f);
    // 40 Hz to 16 kHz, log spaced: each band is as wide as the ear hears it.
    const double low = 40, high = 16000;
    for (int b = 0; b < Bands; ++b) {
        const double f0 = low * std::pow(high / low, double(b) / Bands);
        const double f1 = low * std::pow(high / low, double(b + 1) / Bands);
        int k0 = int(f0 * Window / SampleRate), k1 = int(f1 * Window / SampleRate);
        k1 = std::max(k1, k0 + 1);
        double power = 0;
        for (int k = k0; k < k1 && k < int(Window / 2); ++k)
            power = std::max(power, double(std::norm(buffer[size_t(k)])));
        // Normalised so a full-scale sine reads 0 dB, then -66..-6 dB to 0..1,
        // with a gentle tilt so the highs aren't always flat.
        const double db = 10 * std::log10(power / (double(Window) * Window / 16) + 1e-12) + 3.0 * b / Bands * 3;
        bands[size_t(b)] = float(std::clamp((db + 66) / 60, 0.0, 1.0));
    }
    return bands;
}

void Spectrum::tick() {
    Ring &tap = m_engine->tap();
    size_t got = 0;
    while ((got = tap.read(m_read.data(), m_read.size())) > 0)
        for (size_t i = 0; i < got; ++i) {
            m_history[m_historyPos] = m_read[i];
            m_historyPos = (m_historyPos + 1) % Window;
        }
    std::vector<float> ordered(Window);
    for (size_t i = 0; i < Window; ++i)
        ordered[i] = m_history[(m_historyPos + i) % Window];
    const auto fresh = analyse(ordered);

    double bass = 0, total = 0;
    for (int b = 0; b < Bands; ++b) {
        const float target = fresh[size_t(b)];
        float &band = m_bands[size_t(b)];
        // Quick to rise, slow to fall, like a meter's needle.
        band = target > band ? band + (target - band) * 0.6f : band * 0.88f + target * 0.12f;
        float &peak = m_peaks[size_t(b)];
        float &velocity = m_peakVelocity[size_t(b)];
        if (band >= peak) {
            peak = band;
            velocity = 0;
        } else {
            velocity += 0.0016f;
            peak = std::max(band, peak - velocity);
        }
        if (b < 8)
            bass += band;
        total += band;
    }
    bass /= 8;
    m_level = total / Bands;
    // A beat is the bass rising well above its recent average.
    m_bassAverage = m_bassAverage * 0.93 + bass * 0.07;
    if (bass > m_bassAverage * 1.18 + 0.04 && bass > 0.25)
        m_pulse = std::min(1.0, m_pulse + (bass - m_bassAverage) * 2.5);
    m_pulse *= 0.90;
    emit updated();

    // Stop polling once the music has been quiet for a while; QML turns it
    // back on when playback resumes.
    m_idleFrames = m_level < 0.002 ? m_idleFrames + 1 : 0;
}

HaloView::HaloView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    // Painted on the CPU into an image, then uploaded. Qt's OpenGL paint
    // engine draws the large gradient glow so slowly on Intel's Mesa driver
    // that the render thread fell behind and froze the window.
    setRenderTarget(QQuickPaintedItem::Image);
}

void HaloView::setSpectrum(QObject *spectrum) {
    auto *s = qobject_cast<Spectrum *>(spectrum);
    if (s == m_spectrum)
        return;
    if (m_spectrum)
        disconnect(m_spectrum, nullptr, this, nullptr);
    m_spectrum = s;
    if (m_spectrum)
        connect(m_spectrum, &Spectrum::updated, this, [this] {
            if (isVisible())
                update();
        });
    emit spectrumChanged();
}

void HaloView::setColor(const QColor &color) {
    if (color == m_color)
        return;
    m_color = color;
    emit colorChanged();
    update();
}

void HaloView::setInner(double inner) {
    if (qFuzzyCompare(inner, m_inner))
        return;
    m_inner = inner;
    emit innerChanged();
    update();
}

void HaloView::setCalm(bool calm) {
    if (calm == m_calm)
        return;
    m_calm = calm;
    emit calmChanged();
    update();
}

void HaloView::setStyle(int style) {
    if (style == m_style)
        return;
    m_style = style;
    emit styleChanged();
    update();
}

void HaloView::paint(QPainter *p) {
    // Square pixels on a grid, so no antialiasing: edges stay hard.
    p->setRenderHint(QPainter::Antialiasing, false);
    if (m_style == Bars)
        paintBars(p);
    else
        paintRays(p);
}

namespace {
// Four brightness steps, like the block shades the screensaver draws with
// (light, medium, dark, full), instead of a smooth fade.
QColor shade(QColor c, double v) {
    static constexpr double steps[] = {0.28, 0.5, 0.75, 1.0};
    c.setAlphaF(steps[std::clamp(int(v * 4), 0, 3)]);
    return c;
}
}

// The halo as pixel art: the ring is a grid of square cells around the cover;
// a cell lights when its band reaches out that far. Rays are mirrored left and
// right with the lows at the top, so the ring reads as one shape. On a hard
// beat a few rows of cells tear sideways for a frame, the screensaver's glitch.
void HaloView::paintRays(QPainter *p) {
    const double w = width(), h = height();
    const QPointF centre(w / 2, h / 2);
    const double outer = std::min(w, h) / 2;
    const double inner = outer * m_inner;
    const double pulse = m_spectrum && !m_calm ? m_spectrum->pulse() : 0;
    const double level = m_spectrum ? m_spectrum->level() : 0;

    // A soft glow behind the pixels, swelling with the music.
    QRadialGradient glow(centre, outer);
    QColor core = m_color;
    core.setAlphaF(std::clamp(0.08 + level * 0.35 + pulse * 0.2, 0.0, 0.55));
    QColor edge = m_color;
    edge.setAlphaF(0);
    glow.setColorAt(m_inner * 0.9, core);
    glow.setColorAt(1.0, edge);
    p->setPen(Qt::NoPen);
    p->setBrush(glow);
    p->drawEllipse(centre, outer, outer);

    if (!m_spectrum)
        return;
    const auto &bands = m_spectrum->bands();
    const int rays = Spectrum::Bands * 2;
    const double span = outer - inner;
    if (!m_calm)
        m_rotation += 0.0006 + level * 0.004;

    const int cell = std::max(4, int(std::round(outer / 34)));
    const int gap = std::max(1, cell / 5);
    const int half = int(std::ceil(outer / cell));
    // Rows torn on this frame: only on strong beats, and never when calm.
    const bool tearing = !m_calm && pulse > 0.72;
    const quint32 tearSeed = quint32(m_rotation * 100000);

    for (int gy = -half; gy < half; ++gy) {
        int shift = 0;
        if (tearing) {
            const quint32 hsh = (quint32(gy + 1000) * 2654435761u) ^ tearSeed;
            if (hsh % 9 == 0)
                shift = (hsh & 16) ? cell : -cell;
        }
        for (int gx = -half; gx < half; ++gx) {
            const double x = (gx + 0.5) * cell, y = (gy + 0.5) * cell;
            const double r = std::hypot(x, y);
            const double r0 = inner + 4 + pulse * span * 0.05;
            if (r < r0 || r > outer)
                continue;
            double angle = std::atan2(y, x) + Dsp::Pi / 2 - m_rotation;
            angle = std::fmod(angle + 4 * Dsp::Pi, 2 * Dsp::Pi);
            const int i = std::clamp(int(angle / (2 * Dsp::Pi) * rays), 0, rays - 1);
            const int band = i < Spectrum::Bands ? i : rays - 1 - i;
            const double v = m_calm ? 0.15 : bands[size_t(band)];
            const double reach = r0 + cell + v * (span - cell * 2) * (0.75 + pulse * 0.25);
            if (r > reach)
                continue;
            // Brighter towards the cover, like light leaving it.
            const double fade = 1.0 - (r - r0) / std::max(1.0, reach - r0) * 0.6;
            p->fillRect(QRectF(centre.x() + gx * cell + shift + gap / 2.0, centre.y() + gy * cell + gap / 2.0,
                               cell - gap, cell - gap),
                        shade(m_color, std::clamp(v * 0.6 + fade * 0.5, 0.0, 1.0)));
        }
    }
}

// Bars as stacks of LED cells with a floating peak cell.
void HaloView::paintBars(QPainter *p) {
    if (!m_spectrum)
        return;
    const auto &bands = m_spectrum->bands();
    const auto &peaks = m_spectrum->peaks();
    const double w = width(), h = height();
    const double step = w / Spectrum::Bands;
    const double cell = std::max(3.0, std::floor(step));
    const int rows = std::max(1, int(h / cell));
    for (int b = 0; b < Spectrum::Bands; ++b) {
        const double v = m_calm ? 0.05 : bands[size_t(b)];
        const int lit = std::max(1, int(std::round(v * rows)));
        const double x = std::floor(b * step) + 1;
        for (int r = 0; r < lit; ++r)
            p->fillRect(QRectF(x, h - (r + 1) * cell + 1, cell - 2, cell - 2), shade(m_color, 0.3 + double(r) / rows * 0.7));
        if (!m_calm) {
            const int peak = std::clamp(int(std::round(peaks[size_t(b)] * rows)), lit, rows - 1);
            p->fillRect(QRectF(x, h - (peak + 1) * cell + 1, cell - 2, cell - 2), m_color);
        }
    }
}
