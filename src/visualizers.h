#pragma once

#include <QColor>
#include <QImage>
#include <QQuickPaintedItem>
#include <QStringList>
#include <array>
#include <vector>

class Spectrum;

// The pixel visualisers (V cycles through them). Each is drawn on the CPU into
// a small image, one pixel per screen cell, and enlarged by the same pixel
// grid as the kaleidoscope, so every one shares Halo's square-pixel look and
// costs next to nothing on integrated graphics (a 90 x 90 grid is 8,100
// pixels a frame).
//
// Colours come only from the Omarchy theme: the window, the accent, the
// theme's cyan, blue and magenta, and its foreground. Gradients are dithered
// with a 4 x 4 Bayer matrix into a few steps, like pixel art, instead of
// smooth fades.
//
// QML calls step() once per screen refresh while music plays; calm (C) stops
// calling it and the last frame holds.
class PixelVisual : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *spectrum READ spectrum WRITE setSpectrum NOTIFY changed)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY changed)
    // accent, cyan, blue, magenta, bright, foreground (Style.paletteColour order)
    Q_PROPERTY(QStringList palette READ palette WRITE setPalette NOTIFY changed)
    Q_PROPERTY(QColor window READ window WRITE setWindow NOTIFY changed)

public:
    enum Mode { Skyline, Scope, Ridges, Waterfall, Warp, Aurora, Ring, Count };
    Q_ENUM(Mode)

    explicit PixelVisual(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QObject *spectrum() const;
    void setSpectrum(QObject *spectrum);
    int mode() const { return m_mode; }
    void setMode(int mode);
    QStringList palette() const { return m_paletteNames; }
    void setPalette(const QStringList &palette);
    QColor window() const { return m_window; }
    void setWindow(const QColor &window);

    // dt seconds, and the eased beat, loudness, bass, mids and treble (0..1).
    Q_INVOKABLE void step(double dt, double pulse, double level, double low, double mid, double high);

    // For tests: the frame as drawn, one pixel per cell.
    const QImage &frame() const { return m_image; }
    static QString modeName(int mode);
    Q_INVOKABLE QString name() const { return modeName(m_mode); }

signals:
    void changed();

private:
    struct In {
        double dt, pulse, level, low, mid, high;
    };
    void ensureSize();
    QRgb ramp(double t, int x, int y) const;        // window -> blue -> magenta -> accent -> foreground, dithered
    QRgb tint(QRgb base, double v, int x, int y) const;   // window -> base, dithered
    void plot(int x, int y, QRgb c);
    void fade(double keep);                          // phosphor persistence on m_glow
    float band(double u) const;                      // 0..1 across the spectrum, interpolated

    void drawSkyline(const In &in);
    void drawScope(const In &in);
    void drawRidges(const In &in);
    void drawWaterfall(const In &in);
    void drawWarp(const In &in);
    void drawAurora(const In &in);
    void drawRing(const In &in);
    void rebuildRamp();                              // the ramp's few shades, once per palette

    Spectrum *m_spectrum = nullptr;
    int m_mode = Skyline;
    QStringList m_paletteNames;
    std::array<QColor, 6> m_palette;
    QColor m_window{"#0a0a0d"};
    QImage m_image;
    std::vector<float> m_glow;                       // per cell, 0..1, for trails
    std::vector<float> m_glowHue;                    // per cell, the ramp position of its trail
    std::vector<std::vector<float>> m_history;       // past spectra, newest last
    double m_time = 0, m_scroll = 0, m_flash = 0;
    std::vector<float> m_caps, m_capFall;
    struct Star {
        float x, y, z, px, py;
    };
    std::vector<Star> m_stars;
    // ramp() quantises to 10 shades; they are blended once, not per pixel.
    std::array<QRgb, 10> m_rampLut{};
    double m_rotation = 0;
    unsigned m_seed = 0x2545F491u;
    float random01();
};
