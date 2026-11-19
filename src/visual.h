#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QQuickPaintedItem>
#include <QTimer>
#include <complex>
#include <vector>

class Engine;

// Turns what the engine played into 64 bands and a beat pulse, sixty times a
// second while something is watching and the music plays. It reads the
// engine's tap, so it sees the track itself and not every other sound on
// the machine.
class Spectrum : public QObject {
    Q_OBJECT
    Q_PROPERTY(double pulse READ pulse NOTIFY updated)
    Q_PROPERTY(double level READ level NOTIFY updated)
    // Lows, mids and highs, 0 to 1, for small meters (the playing row's).
    Q_PROPERTY(double low READ low NOTIFY updated)
    Q_PROPERTY(double mid READ mid NOTIFY updated)
    Q_PROPERTY(double high READ high NOTIFY updated)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    static constexpr int Bands = 64;
    explicit Spectrum(Engine *engine, QObject *parent = nullptr);

    const std::vector<float> &bands() const { return m_bands; }
    const std::vector<float> &peaks() const { return m_peaks; }
    // The last 2048 samples the engine played, oldest first (for scopes).
    const std::vector<float> &wave() const { return m_wave; }
    double pulse() const { return m_pulse; }
    double level() const { return m_level; }
    double low() const { return average(0, 10); }
    double mid() const { return average(10, 36); }
    double high() const { return average(36, Bands); }
    bool active() const { return m_active; }
    void setActive(bool active);
    // The average of bands [from, to), for visuals drawn in QML shaders.
    Q_INVOKABLE double bandAverage(int from, int to) const { return average(from, to); }

    // The pure part, for tests: magnitudes of 2048 samples at 48 kHz into
    // `Bands` log-spaced bands, 0 to 1.
    static std::vector<float> analyse(const std::vector<float> &samples);

signals:
    void updated();
    void activeChanged();

private:
    void tick();
    double average(int from, int to) const {
        double sum = 0;
        for (int i = from; i < to && i < int(m_bands.size()); ++i)
            sum += m_bands[size_t(i)];
        return to > from ? sum / (to - from) : 0;
    }

    Engine *m_engine;
    QTimer m_timer;
    bool m_active = false;
    std::vector<float> m_history, m_read, m_wave;
    size_t m_historyPos = 0;
    std::vector<float> m_bands, m_peaks, m_peakVelocity;
    double m_pulse = 0, m_level = 0, m_bassAverage = 0;
    int m_idleFrames = 0;
};

// The halo: a ring of light around the cover, one ray per band, breathing
// with the beat. Painted in C++ so it costs little on integrated graphics.
class HaloView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *spectrum READ spectrum WRITE setSpectrum NOTIFY spectrumChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(double inner READ inner WRITE setInner NOTIFY innerChanged)
    Q_PROPERTY(bool calm READ calm WRITE setCalm NOTIFY calmChanged)
    Q_PROPERTY(int style READ style WRITE setStyle NOTIFY styleChanged)

public:
    enum Style { Rays, Bars };
    Q_ENUM(Style)
    explicit HaloView(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QObject *spectrum() const { return m_spectrum; }
    void setSpectrum(QObject *spectrum);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    double inner() const { return m_inner; }
    void setInner(double inner);
    bool calm() const { return m_calm; }
    void setCalm(bool calm);
    int style() const { return m_style; }
    void setStyle(int style);

signals:
    void spectrumChanged();
    void colorChanged();
    void innerChanged();
    void calmChanged();
    void styleChanged();

private:
    void paintRays(QPainter *p);
    void paintBars(QPainter *p);

    Spectrum *m_spectrum = nullptr;
    QColor m_color{"#FFD60A"};
    double m_inner = 0.62;
    bool m_calm = false;
    int m_style = Rays;
    double m_rotation = 0;
};
