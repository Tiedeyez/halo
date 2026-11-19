#include "visualizers.h"

#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "visual.h"

namespace {
constexpr double Tau = 6.28318530717958647692;

// 4 x 4 ordered dither thresholds, 0..1.
constexpr float Bayer[4][4] = {{0 / 16.f, 8 / 16.f, 2 / 16.f, 10 / 16.f},
                               {12 / 16.f, 4 / 16.f, 14 / 16.f, 6 / 16.f},
                               {3 / 16.f, 11 / 16.f, 1 / 16.f, 9 / 16.f},
                               {15 / 16.f, 7 / 16.f, 13 / 16.f, 5 / 16.f}};

QRgb mixRgb(QRgb a, QRgb b, double t) {
    // Integer maths in 1/256ths: called for nearly every pixel of every frame.
    const int w = int(std::clamp(t, 0.0, 1.0) * 256.0 + 0.5);
    const auto m = [w](int x, int y) { return (x * (256 - w) + y * w + 128) >> 8; };
    return qRgb(m(qRed(a), qRed(b)), m(qGreen(a), qGreen(b)), m(qBlue(a), qBlue(b)));
}

}

PixelVisual::PixelVisual(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
    setRenderTarget(QQuickPaintedItem::Image);
    setFillColor(Qt::transparent);
    m_caps.assign(Spectrum::Bands, 0.0f);
    m_capFall.assign(Spectrum::Bands, 0.0f);
    for (auto &c : m_palette)
        c = QColor("#FFD60A");
    rebuildRamp();
    connect(this, &QQuickItem::widthChanged, this, [this] { ensureSize(); });
    connect(this, &QQuickItem::heightChanged, this, [this] { ensureSize(); });
}

QString PixelVisual::modeName(int mode) {
    switch (mode) {
    case Skyline: return tr("Skyline");
    case Scope: return tr("Phosphor");
    case Ridges: return tr("Pulsar");
    case Waterfall: return tr("Waterfall");
    case Warp: return tr("Warp");
    case Aurora: return tr("Aurora");
    case Ring: return tr("Halo");
    default: return {};
    }
}

QObject *PixelVisual::spectrum() const { return m_spectrum; }

void PixelVisual::setSpectrum(QObject *spectrum) {
    auto *s = qobject_cast<Spectrum *>(spectrum);
    if (s == m_spectrum)
        return;
    m_spectrum = s;
    emit changed();
}

void PixelVisual::setMode(int mode) {
    mode = std::clamp(mode, 0, int(Count) - 1);
    if (mode == m_mode)
        return;
    m_mode = mode;
    // Trails and history belong to the visual that made them.
    std::fill(m_glow.begin(), m_glow.end(), 0.0f);
    m_history.clear();
    m_stars.clear();
    m_scroll = 0;
    emit changed();
    step(0, 0, 0.3, 0.3, 0.3, 0.3);
}

void PixelVisual::setPalette(const QStringList &palette) {
    if (palette == m_paletteNames)
        return;
    m_paletteNames = palette;
    for (size_t i = 0; i < m_palette.size(); ++i)
        m_palette[i] = QColor(palette.value(int(i), palette.value(0, "#FFD60A")));
    rebuildRamp();
    emit changed();
    update();
}

void PixelVisual::setWindow(const QColor &window) {
    if (window == m_window)
        return;
    m_window = window;
    rebuildRamp();
    emit changed();
    update();
}

void PixelVisual::ensureSize() {
    const int w = std::max(1, int(width())), h = std::max(1, int(height()));
    if (m_image.width() == w && m_image.height() == h)
        return;
    m_image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_image.fill(Qt::transparent);
    m_glow.assign(size_t(w) * size_t(h), 0.0f);
    m_glowHue.assign(size_t(w) * size_t(h), 0.0f);
    m_history.clear();
    m_stars.clear();
}

float PixelVisual::random01() {
    m_seed ^= m_seed << 13;
    m_seed ^= m_seed >> 17;
    m_seed ^= m_seed << 5;
    return float(m_seed & 0xFFFFFF) / float(0x1000000);
}

float PixelVisual::band(double u) const {
    if (!m_spectrum)
        return 0;
    const auto &b = m_spectrum->bands();
    const double at = std::clamp(u, 0.0, 1.0) * (Spectrum::Bands - 1);
    const int i = int(at);
    const double f = at - i;
    return float(b[size_t(i)] * (1 - f) + b[size_t(std::min(i + 1, Spectrum::Bands - 1))] * f);
}

// After dithering the ramp has only Steps + 1 shades, so they are blended
// here once per palette instead of for every pixel of every frame.
static constexpr int RampSteps = 9;

QRgb PixelVisual::ramp(double t, int x, int y) const {
    t = std::clamp(t, 0.0, 1.0);
    const double q = t * RampSteps + Bayer[y & 3][x & 3] - 0.5;
    return m_rampLut[size_t(std::clamp(int(std::floor(q + 0.5)), 0, RampSteps))];
}

void PixelVisual::rebuildRamp() {
    for (int k = 0; k <= RampSteps; ++k) {
        // Five stops from the theme: window, blue, magenta, accent, foreground.
        const double t = double(k) / RampSteps;
        const QRgb stops[5] = {m_window.rgb(), m_palette[2].rgb(), m_palette[3].rgb(), m_palette[0].rgb(),
                               mixRgb(m_palette[0].rgb(), m_palette[5].rgb(), 0.65)};
        const double s = t * 4;
        const int i = std::min(3, int(s));
        // The darkest stop stays close to the window so quiet parts recede.
        const double f = s - i;
        QRgb a = stops[i], b = stops[i + 1];
        if (i == 0)
            b = mixRgb(m_window.rgb(), stops[1], 0.55);
        if (i == 1)
            a = mixRgb(m_window.rgb(), stops[1], 0.55);
        m_rampLut[size_t(k)] = mixRgb(a, b, f);
    }
}

QRgb PixelVisual::tint(QRgb base, double v, int x, int y) const {
    static constexpr int Steps = 5;
    const double q = std::clamp(v, 0.0, 1.0) * Steps + Bayer[y & 3][x & 3] - 0.5;
    return mixRgb(m_window.rgb(), base, std::clamp(std::floor(q + 0.5) / Steps, 0.0, 1.0));
}

void PixelVisual::plot(int x, int y, QRgb c) {
    if (x < 0 || y < 0 || x >= m_image.width() || y >= m_image.height())
        return;
    reinterpret_cast<QRgb *>(m_image.scanLine(y))[x] = c;
}

void PixelVisual::fade(double keep) {
    for (auto &g : m_glow)
        g *= float(keep);
}

void PixelVisual::step(double dt, double pulse, double level, double low, double mid, double high) {
    ensureSize();
    if (m_image.isNull())
        return;
    m_time += dt;
    // A beat lights everything for a moment, then lets go quickly.
    m_flash = std::max(m_flash * std::exp(-dt / 0.12), pulse > 0.5 ? 1.0 : 0.0);
    const In in{dt, pulse, level, low, mid, high};
    switch (m_mode) {
    case Skyline: drawSkyline(in); break;
    case Scope: drawScope(in); break;
    case Ridges: drawRidges(in); break;
    case Waterfall: drawWaterfall(in); break;
    case Warp: drawWarp(in); break;
    case Aurora: drawAurora(in); break;
    case Ring: drawRing(in); break;
    default: break;
    }
    update();
}

void PixelVisual::paint(QPainter *painter) {
    if (!m_image.isNull())
        painter->drawImage(QRectF(0, 0, width(), height()), m_image);
}

// ---- Skyline: a mirrored spectrum, bass in the middle, with falling caps
// and a rippling reflection under a horizon line.
void PixelVisual::drawSkyline(const In &in) {
    const int w = m_image.width(), h = m_image.height();
    m_image.fill(Qt::transparent);
    const int horizon = int(h * 0.68);
    const int centre = w / 2;
    const double maxH = horizon - h * 0.08;
    for (int x = 0; x < w; ++x) {
        // Distance from the centre picks the band: bass in the middle.
        const double u = std::abs(x - centre + 0.5) / std::max(1.0, w / 2.0);
        const int bi = std::clamp(int(u * (Spectrum::Bands - 1)), 0, Spectrum::Bands - 1);
        const double v = std::pow(band(u), 1.35);
        const int top = horizon - int(std::lround(v * maxH));
        // Caps fall slowly after the bar drops away.
        float &cap = m_caps[size_t(bi)];
        float &fall = m_capFall[size_t(bi)];
        if (v >= cap) {
            cap = float(v);
            fall = 0;
        } else {
            fall += float(in.dt * 0.9);
            cap = std::max(float(v), cap - fall * float(in.dt) * 6);
        }
        for (int y = top; y < horizon; ++y) {
            const double t = 0.25 + 0.75 * double(horizon - y) / maxH;
            plot(x, y, ramp(t * (0.55 + 0.45 * v) + 0.12 * m_flash, x, y));
        }
        // The cap, in the accent, a hair above the bar.
        const int capY = horizon - 2 - int(std::lround(cap * maxH));
        if (capY >= 0 && cap > 0.02)
            plot(x, capY, mixRgb(m_palette[0].rgb(), m_palette[5].rgb(), 0.35 * m_flash));
        // The reflection: dimmer, shorter, wobbling sideways on the beat.
        const int depth = int((horizon - top) * 0.45);
        for (int d = 1; d < depth && horizon + d < h; ++d) {
            const double fadeOut = 1.0 - double(d) / std::max(1, depth);
            const int wobble = int(std::lround(std::sin(d * 0.9 + m_time * 6) * in.pulse * 1.5));
            const int sx = std::clamp(x + wobble, 0, w - 1);
            if ((d + int(m_time * 20)) % 3 == 0)
                continue;   // water lines
            plot(sx, horizon + d, tint(m_palette[2].rgb(), 0.55 * fadeOut * (0.4 + v), sx, horizon + d));
        }
    }
    // The horizon itself.
    for (int x = 0; x < w; ++x)
        plot(x, horizon, tint(m_palette[1].rgb(), 0.35 + 0.5 * m_flash, x, horizon));
}

// ---- Phosphor: an oscilloscope trace that leaves a glowing trail, like a
// CRT, with a faint graticule. The beat warms the trace toward the accent.
void PixelVisual::drawScope(const In &in) {
    const int w = m_image.width(), h = m_image.height();
    fade(std::exp(-in.dt / 0.07));
    if (m_spectrum && !m_spectrum->wave().empty()) {
        const auto &wave = m_spectrum->wave();
        // Trigger on a rising zero crossing so the trace stands still on a tone.
        size_t start = wave.size() / 2;
        for (size_t i = wave.size() / 2; i + 1 < wave.size() && i < wave.size() / 2 + 600; ++i)
            if (wave[i] <= 0 && wave[i + 1] > 0) {
                start = i;
                break;
            }
        const size_t span = std::min<size_t>(wave.size() - start, 1200);
        // Automatic gain: the window's own peak fills about 80% of the height,
        // eased so the trace doesn't jump in size from one frame to the next.
        float peak = 0.02f;
        for (size_t i = start; i < start + span; ++i)
            peak = std::max(peak, std::abs(wave[i]));
        m_scroll += (peak - m_scroll) * (peak > m_scroll ? 0.5 : 0.06);
        const double gain = 0.40 * h / std::max(0.03, m_scroll);
        int px = -1, py = 0;
        for (int x = 0; x < w; ++x) {
            // Each column is the average of its slice of samples: a calm,
            // continuous line rather than one jagged sample per column.
            const size_t a = start + size_t(double(x) / w * span), b = start + size_t(double(x + 1) / w * span);
            float s = 0;
            for (size_t i = a; i < std::max(a + 1, b); ++i)
                s += wave[i];
            s /= float(std::max<size_t>(1, b - a));
            const int y = std::clamp(int(std::lround(h * 0.5 - s * gain)), 0, h - 1);
            // Join to the last point so steep edges stay continuous.
            if (px >= 0) {
                const int y0 = std::min(py, y), y1 = std::max(py, y);
                for (int yy = y0; yy <= y1; ++yy) {
                    float &g = m_glow[size_t(yy) * size_t(w) + size_t(x)];
                    g = std::min(1.0f, g + 0.9f);
                    m_glowHue[size_t(yy) * size_t(w) + size_t(x)] = float(m_flash);
                }
            }
            px = x;
            py = y;
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            float g = m_glow[i];
            // A soft bloom: each cell borrows a little from its neighbours.
            if (x > 0 && x + 1 < w && y > 0 && y + 1 < h)
                g = std::max(g, 0.35f * std::max({m_glow[i - 1], m_glow[i + 1], m_glow[i - size_t(w)], m_glow[i + size_t(w)]}));
            // A quiet graticule: every other cell of a few lines, and the centre line.
            const bool grid = ((x % std::max(8, w / 6) == 0) || (y % std::max(8, h / 4) == 0) || y == h / 2) && ((x + y) % 2 == 0);
            if (g < 0.04f) {
                plot(x, y, grid ? tint(m_palette[1].rgb(), y == h / 2 ? 0.22 : 0.12, x, y) : qRgba(0, 0, 0, 0));
                continue;
            }
            const QRgb trace = mixRgb(m_palette[1].rgb(), m_palette[0].rgb(), m_glowHue[i]);
            const QRgb hot = mixRgb(trace, m_palette[5].rgb(), std::max(0.0, double(g) - 0.75) * 2.4);
            plot(x, y, tint(hot, std::min(1.0, double(g) * 1.15), x, y));
        }
    }
}

// ---- Pulsar: stacked ridgelines of the spectrum's recent past, front ones
// hiding the ones behind, after the famous radio-pulsar plot. The newest
// line is at the front; a beat lights it in the accent.
void PixelVisual::drawRidges(const In &in) {
    const int w = m_image.width(), h = m_image.height();
    m_scroll += in.dt;
    if (m_history.empty() || m_scroll > 0.07) {
        m_scroll = 0;
        std::vector<float> row(static_cast<size_t>(w));
        for (int x = 0; x < w; ++x) {
            // Bass in the middle, falling off toward the edges like the plot,
            // with a little grain so quiet lines aren't ruler-flat.
            const double u = std::abs((x + 0.5) / w - 0.5) * 2;
            const double bell = std::exp(-std::pow(u * 2.4, 2));
            const double grain = 0.05 * random01();
            row[size_t(x)] = float((std::pow(band(u * 0.8), 1.6) * 1.3 + grain) * bell);
        }
        m_history.push_back(std::move(row));
        const size_t lines = size_t(std::clamp(h / 5, 12, 26));
        while (m_history.size() > lines)
            m_history.erase(m_history.begin());
    }
    m_image.fill(Qt::transparent);
    const size_t n = m_history.size();
    if (!n)
        return;
    const double spacing = (h * 0.70) / double(std::max<size_t>(n, 12));
    const double amp = spacing * 7.0;
    const int margin = w / 7;
    std::vector<int> floor(size_t(w), h);   // lowest visible row per column, front lines first
    for (size_t k = 0; k < n; ++k) {
        // k = 0 is the newest, at the front (bottom).
        const auto &row = m_history[n - 1 - k];
        const int base = int(h * 0.92 - k * spacing);
        const double depth = 1.0 - double(k) / double(n);
        const bool newest = k == 0;
        for (int x = margin; x < w - margin; ++x) {
            const int y = base - int(std::lround(row[size_t(x)] * amp));
            if (y >= floor[size_t(x)])
                continue;   // hidden behind a nearer line
            const QRgb c = newest ? mixRgb(m_palette[5].rgb(), m_palette[0].rgb(), 0.4 + 0.6 * m_flash)
                                  : mixRgb(m_palette[2].rgb(), m_palette[5].rgb(), depth * 0.8);
            plot(x, y, tint(c, 0.25 + 0.75 * depth, x, y));
            // What lies under this line hides everything further back.
            floor[size_t(x)] = y;
        }
    }
}

// ---- Waterfall: a scrolling spectrogram, newest at the top, the theme's
// colours as heat. Each beat leaves a faint accent tick at the left edge.
void PixelVisual::drawWaterfall(const In &in) {
    const int w = m_image.width(), h = m_image.height();
    m_scroll += in.dt * 34;   // rows a second
    int rows = int(m_scroll);
    m_scroll -= rows;
    rows = std::min(rows, h);
    if (in.dt == 0)
        rows = 1;
    // Scroll the heat buffer down, then write the new rows at the top.
    if (rows > 0 && rows < h) {
        std::memmove(m_glow.data() + size_t(rows) * size_t(w), m_glow.data(), size_t(h - rows) * size_t(w) * sizeof(float));
        std::memmove(m_glowHue.data() + size_t(rows) * size_t(w), m_glowHue.data(),
                     size_t(h - rows) * size_t(w) * sizeof(float));
    }
    for (int r = 0; r < rows; ++r)
        for (int x = 0; x < w; ++x) {
            m_glow[size_t(r) * size_t(w) + size_t(x)] = std::pow(band((x + 0.5) / w), 2.4f) * 1.15f;
            m_glowHue[size_t(r) * size_t(w) + size_t(x)] = float(m_flash > 0.8 ? 1 : 0);
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            const double age = double(y) / h;
            const double v = m_glow[i] * (1.0 - 0.75 * age);
            if (x == 0 && m_glowHue[i] > 0.5) {
                plot(x, y, m_palette[0].rgb());
                continue;
            }
            plot(x, y, v < 0.08 ? qRgba(0, 0, 0, 0) : ramp(v, x, y));
        }
}

// ---- Warp: flying through a field of stars, faster as the music gets
// louder, with the spectrum as a ring of light at the far end of the tunnel.
void PixelVisual::drawWarp(const In &in) {
    const int w = m_image.width(), h = m_image.height();
    if (m_stars.empty())
        for (int i = 0; i < 260; ++i)
            m_stars.push_back({random01() * 2 - 1, random01() * 2 - 1, random01() * 0.95f + 0.05f, -1, -1});
    fade(std::exp(-in.dt / 0.09));
    const double speed = 0.18 + 1.1 * in.level + 1.8 * in.pulse;
    const double cx = w * 0.5, cy = h * 0.5, scale = std::min(w, h) * 0.5;
    for (auto &s : m_stars) {
        s.z -= float(speed * in.dt);
        if (s.z <= 0.02f) {
            s = {random01() * 2 - 1, random01() * 2 - 1, 1.0f, -1, -1};
            continue;
        }
        const double sx = cx + s.x / s.z * scale * 0.35, sy = cy + s.y / s.z * scale * 0.35;
        if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
            s.z = 0;
            continue;
        }
        // The streak from where it was: longer when faster.
        const double angle = std::atan2(s.y, s.x);
        const float hue = float(std::fmod(angle / Tau + 1.0, 1.0));
        const int steps = s.px < 0 ? 1 : std::max(1, int(std::hypot(sx - s.px, sy - s.py)));
        for (int k = 0; k <= steps; ++k) {
            const double t = steps ? double(k) / steps : 1;
            const int x = int(s.px < 0 ? sx : s.px + (sx - s.px) * t);
            const int y = int(s.py < 0 ? sy : s.py + (sy - s.py) * t);
            if (x < 0 || y < 0 || x >= w || y >= h)
                continue;
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            m_glow[i] = std::min(1.0f, m_glow[i] + float(0.25 + 0.75 * (1 - s.z)));
            m_glowHue[i] = hue;
        }
        s.px = float(sx);
        s.py = float(sy);
    }
    m_image.fill(Qt::transparent);
    // The ring: the spectrum around a circle, breathing with the beat.
    const double r0 = scale * (0.16 + 0.05 * in.pulse);
    for (int a = 0; a < 180; ++a) {
        const double t = a / 180.0;
        const double u = std::abs(t * 2 - 1);   // mirrored, bass at top and bottom
        const double r = r0 + band(u) * scale * 0.22;
        const double ang = t * Tau + m_time * 0.15;
        for (double rr = r0; rr <= r; rr += 1) {
            const int x = int(cx + std::cos(ang) * rr), y = int(cy + std::sin(ang) * rr);
            plot(x, y, ramp(0.35 + 0.65 * (rr - r0) / std::max(1.0, r - r0 + 1), x, y));
        }
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            if (m_glow[i] < 0.06f)
                continue;
            const QRgb base = m_glowHue[i] < 0.33f ? m_palette[1].rgb() : m_glowHue[i] < 0.66f ? m_palette[3].rgb()
                                                                                                 : m_palette[0].rgb();
            plot(x, y, tint(mixRgb(base, m_palette[5].rgb(), std::max(0.0, m_glow[i] - 0.7) * 2), m_glow[i], x, y));
        }
}

// ---- Aurora: curtains of light over a still, starry sky. The bands set how
// tall each fold of the curtain reaches; the beat brightens its lower edge.
void PixelVisual::drawAurora(const In &in) {
    const int w = m_image.width(), h = m_image.height();
    m_image.fill(Qt::transparent);
    // Stars: fixed per cell, twinkling slowly.
    for (int y = 0; y < int(h * 0.75); ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned hsh = unsigned(x * 73856093u) ^ unsigned(y * 19349663u);
            if (hsh % 97 != 0)
                continue;
            const double tw = 0.5 + 0.5 * std::sin(m_time * (1.5 + (hsh % 7) * 0.3) + hsh);
            plot(x, y, tint(m_palette[5].rgb(), 0.25 + 0.4 * tw, x, y));
        }
    // Three curtains, each its own colour, speed and fold.
    const QRgb colours[3] = {m_palette[1].rgb(), m_palette[0].rgb(), m_palette[3].rgb()};
    for (int layer = 0; layer < 3; ++layer) {
        const double speed = 0.06 + 0.04 * layer;
        const double base = h * (0.62 + 0.07 * layer);
        for (int x = 0; x < w; ++x) {
            const double u = double(x) / w;
            const double drift = u + m_time * speed;
            const double fold = std::sin(drift * Tau * (1.3 + 0.4 * layer)) * 0.5 + 0.5;
            const double wave = std::sin(drift * Tau * 3.1 + m_time * 0.7 + layer) * 0.5 + 0.5;
            const double energy = band(std::fmod(drift * 0.7 + layer * 0.21, 1.0));
            const double reach = (0.18 + 0.55 * energy * (0.6 + 0.4 * fold)) * h;
            const double bottom = base + std::sin(drift * Tau * 2 + m_time) * h * 0.03;
            // Vertical rays: the curtain is made of fine folds of light (per
            // column, so worked out once here, not for every pixel).
            const double rays = 0.55 + 0.45 * std::sin(x * 1.7 + layer * 2.0 + std::sin(m_time * 0.9 + x * 0.21) * 2.0);
            const double column = (0.35 + 0.65 * wave) * (0.5 + 0.5 * in.level) * rays;
            const double span = std::max(1.0, reach);
            const int top = std::max(0, int(bottom - reach));
            // exp(-2.2 t) stepped down the column by one multiply per pixel.
            double falloff = std::exp(-2.2 * (bottom - top) / span);
            const double stepUp = std::exp(2.2 / span);
            for (int y = top; y <= int(bottom); ++y, falloff *= stepUp) {
                if (y >= h)
                    break;
                const double t = (bottom - y) / span;   // 0 at the lower edge
                double v = falloff * column;
                v += (t < 0.08 ? 0.45 : 0) * (0.4 + 0.6 * m_flash);    // the bright lower hem
                if (v < 0.1)
                    continue;
                const QRgb hem = mixRgb(colours[layer], m_palette[5].rgb(), t < 0.08 ? 0.4 * m_flash : 0);
                const QRgb now = reinterpret_cast<const QRgb *>(m_image.constScanLine(y))[x];
                const QRgb c = tint(hem, std::min(1.0, v), x, y);
                // Brighter curtain wins where they overlap.
                plot(x, y, qAlpha(now) && qGray(now) > qGray(c) ? now : c);
            }
        }
    }
    // A dark, still horizon with a faint reflection of the brightest curtain.
    const int ground = int(h * 0.86);
    for (int y = ground; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const int mirror = ground - (y - ground) * 2;
            const QRgb above = mirror >= 0 ? reinterpret_cast<const QRgb *>(m_image.constScanLine(mirror))[x] : 0;
            plot(x, y, qAlpha(above) && (x + y) % 2 ? mixRgb(m_window.rgb(), above, 0.3) : m_window.rgb());
        }
}

// ---- Halo: the ring of rays around the cover, one cell per pixel. A cell
// lights when its band reaches out that far; rays are mirrored left and
// right with the lows at the top, so the ring reads as one shape. A soft
// glow in the accent swells with the music, and on a hard beat a few rows
// tear sideways for a frame. Drawn like the other pixel visuals (a small
// image enlarged by the pixel grid), so it costs the same at any size.
void PixelVisual::drawRing(const In &in) {
    const int w = m_image.width(), h = m_image.height();
    m_image.fill(Qt::transparent);
    const double cx = w / 2.0, cy = h / 2.0;
    const double outer = std::min(w, h) / 2.0;
    const double inner = outer * 0.56;
    const double span = outer - inner;
    const int bands = Spectrum::Bands, rays = bands * 2;
    const bool calm = in.dt == 0;
    if (!calm)
        m_rotation += (0.0006 + in.level * 0.004) * in.dt * 60;
    const double r0 = inner + 1 + in.pulse * span * 0.05;
    const bool tearing = in.pulse > 0.72;
    const unsigned tearSeed = unsigned(m_rotation * 100000);
    const QRgb accent = m_palette[0].rgb();
    const double glowCore = std::clamp(0.08 + in.level * 0.35 + in.pulse * 0.2, 0.0, 0.55);
    for (int y = 0; y < h; ++y) {
        int shift = 0;
        if (tearing) {
            const unsigned hsh = (unsigned(y + 1000) * 2654435761u) ^ tearSeed;
            if (hsh % 9 == 0)
                shift = (hsh & 16) ? 1 : -1;
        }
        QRgb *row = reinterpret_cast<QRgb *>(m_image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double dx = x + 0.5 - cx - shift, dy = y + 0.5 - cy;
            const double r = std::sqrt(dx * dx + dy * dy);
            if (r > outer || r < inner * 0.9)
                continue;
            double angle = std::atan2(dy, dx) + M_PI / 2 - m_rotation;
            angle = std::fmod(angle + 4 * M_PI, 2 * M_PI);
            const int i = std::clamp(int(angle / (2 * M_PI) * rays), 0, rays - 1);
            const int b = i < bands ? i : rays - 1 - i;
            const double v = calm ? 0.15 : band(double(b) / (bands - 1));
            const double reach = r0 + 1 + v * (span - 2) * (0.75 + in.pulse * 0.25);
            if (r >= r0 && r <= reach) {
                // Brighter towards the cover, like light leaving it.
                const double fade = 1.0 - (r - r0) / std::max(1.0, reach - r0) * 0.6;
                row[x] = tint(accent, std::clamp(0.3 + v * 0.6 + fade * 0.4, 0.0, 1.0), x, y);
            } else {
                // The glow: dim and smooth (dithering a faint glow only
                // makes a checkerboard), fading out to the rim.
                const double g = glowCore * (1.0 - std::clamp((r - inner * 0.9) / (outer - inner * 0.9), 0.0, 1.0));
                if (g > 0.03)
                    row[x] = mixRgb(m_window.rgb(), accent, g);
            }
        }
    }
}
