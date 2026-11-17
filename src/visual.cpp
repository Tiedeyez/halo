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
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
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
    p->setRenderHint(QPainter::Antialiasing);
    if (m_style == Bars)
        paintBars(p);
    else
        paintRays(p);
}

void HaloView::paintRays(QPainter *p) {
    const double w = width(), h = height();
    const QPointF centre(w / 2, h / 2);
    const double outer = std::min(w, h) / 2;
    const double inner = outer * m_inner;
    const double pulse = m_spectrum && !m_calm ? m_spectrum->pulse() : 0;
    const double level = m_spectrum ? m_spectrum->level() : 0;

    // The glow behind the cover swells with the music.
    QRadialGradient glow(centre, outer);
    QColor core = m_color;
    core.setAlphaF(std::clamp(0.10 + level * 0.45 + pulse * 0.25, 0.0, 0.7));
    QColor edge = m_color;
    edge.setAlphaF(0);
    glow.setColorAt(m_inner * 0.9, core);
    glow.setColorAt(1.0, edge);
    p->setPen(Qt::NoPen);
    p->setBrush(glow);
    p->drawEllipse(centre, outer, outer);

    if (!m_spectrum)
        return;
    // Rays mirrored left and right, lows at the top, so the ring reads as one
    // shape instead of a spinning chart. Calm mode keeps a still ring.
    const auto &bands = m_spectrum->bands();
    const int rays = Spectrum::Bands * 2;
    const double span = outer - inner;
    if (!m_calm)
        m_rotation += 0.0006 + level * 0.004;
    for (int i = 0; i < rays; ++i) {
        const int band = i < Spectrum::Bands ? i : rays - 1 - i;
        const double v = m_calm ? 0.15 : bands[size_t(band)];
        const double angle = -Dsp::Pi / 2 + (double(i) + 0.5) / rays * 2 * Dsp::Pi + m_rotation;
        const double r0 = inner + 4 + pulse * span * 0.06;
        const double r1 = r0 + 2 + v * (span - 8) * (0.75 + pulse * 0.25);
        QColor c = m_color;
        c.setAlphaF(std::clamp(0.35 + v * 0.65, 0.0, 1.0));
        p->setPen(QPen(c, std::max(1.5, outer * 2 * Dsp::Pi / rays * 0.45), Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(centre.x() + std::cos(angle) * r0, centre.y() + std::sin(angle) * r0),
                    QPointF(centre.x() + std::cos(angle) * r1, centre.y() + std::sin(angle) * r1));
    }
}

void HaloView::paintBars(QPainter *p) {
    if (!m_spectrum)
        return;
    const auto &bands = m_spectrum->bands();
    const auto &peaks = m_spectrum->peaks();
    const double w = width(), h = height();
    const double step = w / Spectrum::Bands;
    for (int b = 0; b < Spectrum::Bands; ++b) {
        const double v = m_calm ? 0.05 : bands[size_t(b)];
        QColor c = m_color;
        c.setAlphaF(0.35 + v * 0.65);
        p->setPen(Qt::NoPen);
        p->setBrush(c);
        const double bh = std::max(1.0, v * h);
        p->drawRect(QRectF(b * step + 1, h - bh, std::max(1.0, step - 2), bh));
        if (!m_calm) {
            p->setBrush(m_color);
            p->drawRect(QRectF(b * step + 1, h - peaks[size_t(b)] * h - 2, std::max(1.0, step - 2), 2));
        }
    }
}
