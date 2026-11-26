#pragma once

#include <QObject>
#include <QVector4D>
#include <vector>

// Flies the Cymatics visual and the window's background: sand on a plate
// that the music shakes, as in Chladni's experiment. A plate ringing at one
// pitch stands still along its nodal lines and the sand collects there, so
// every pitch draws its own figure, and a higher pitch a finer one.
//
// The figure is a standing wave of a square plate,
//     cos(n pi x) cos(m pi y) + sign * cos(m pi x) cos(n pi y),
// and the pilot picks (n, m, sign) from how bright the music sounds (the
// spectrum's centre of mass): a bass line draws a few broad curves, cymbals
// and voices a fine lattice. The brightness is measured against the range the
// song itself has covered, so a dark master still uses the whole list.
//
// The figure changes on the beat, every `beatsPerChange` beats, and never
// to one of the last few shown: a steady tone walks round its neighbours.
// Between changes it holds. A change takes a moment: the
// two waves cross-fade, so the lines travel to their new places while the
// sand is thrown about (`agitation`), then settles. Loudness gathers the
// sand onto the lines (`gather`); each beat kicks it (`kick`).
namespace Cymatics {

struct Mode {
    int n = 1, m = 2, sign = 1;
    bool operator==(const Mode &o) const { return n == o.n && m == o.m && sign == o.sign; }
    bool operator!=(const Mode &o) const { return !(*this == o); }
    // The plate's pitch for this figure rises with n^2 + m^2.
    int pitch() const { return n * n + m * m; }
};

// Every figure up to m = 8, lowest pitch first.
const std::vector<Mode> &modes();

// The plate's height at (x, y), 0..1 across it; -2..2.
double height(const Mode &mode, double x, double y);

class Pilot {
public:
    static constexpr double MorphSeconds = 0.45;

    void setBeatsPerChange(int beats) { m_beatsPerChange = beats < 1 ? 1 : beats; }
    int beatsPerChange() const { return m_beatsPerChange; }
    // How much of the list is used, 0..1: the background keeps to the broad
    // figures at its start.
    void setReach(double reach) { m_reach = reach < 0.05 ? 0.05 : reach > 1 ? 1 : reach; }
    double reach() const { return m_reach; }

    // dt seconds; the beat's pulse, loudness and tone (the spectrum's centre
    // of mass), each 0..1.
    void step(double dt, double pulse, double level, double tone);

    int index() const { return m_to; }           // the figure held, or being formed
    const Mode &from() const { return modes()[size_t(m_from)]; }
    const Mode &to() const { return modes()[size_t(m_to)]; }
    double morph() const { return m_morph; }      // 0 = from, 1 = to
    bool morphing() const { return m_from != m_to; }
    double agitation() const { return m_agitation; }
    double gather() const { return m_gather; }
    double kick() const { return m_kick; }
    int beats() const { return m_beats; }
    int changes() const { return m_changes; }

private:
    void change();

    int m_beatsPerChange = 2;
    double m_reach = 1;
    int m_from = 2, m_to = 2;
    double m_morph = 0, m_agitation = 0, m_gather = 0.5, m_kick = 0;
    double m_tone = 0.4, m_low = 0.3, m_high = 0.5;
    double m_sinceChange = 0;
    bool m_armed = true, m_started = false;
    int m_beats = 0, m_changes = 0;
    int m_recent[4] = {-1, -1, -1, -1};   // the last figures shown, so none comes straight back
};

}

class CymaticsPilot : public QObject {
    Q_OBJECT
    Q_PROPERTY(int beatsPerChange READ beatsPerChange WRITE setBeatsPerChange NOTIFY tuned)
    Q_PROPERTY(double reach READ reach WRITE setReach NOTIFY tuned)
    // n, m, sign of the two figures being mixed.
    Q_PROPERTY(QVector4D from READ from NOTIFY moved)
    Q_PROPERTY(QVector4D to READ to NOTIFY moved)
    Q_PROPERTY(double morph READ morph NOTIFY moved)
    Q_PROPERTY(double agitation READ agitation NOTIFY moved)
    Q_PROPERTY(double gather READ gather NOTIFY moved)
    Q_PROPERTY(double kick READ kick NOTIFY moved)

public:
    explicit CymaticsPilot(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void step(double dt, double pulse, double level, double tone) {
        m_pilot.step(dt, pulse, level, tone);
        emit moved();
    }

    int beatsPerChange() const { return m_pilot.beatsPerChange(); }
    void setBeatsPerChange(int beats) {
        m_pilot.setBeatsPerChange(beats);
        emit tuned();
    }
    double reach() const { return m_pilot.reach(); }
    void setReach(double reach) {
        m_pilot.setReach(reach);
        emit tuned();
    }
    QVector4D from() const { return vector(m_pilot.from()); }
    QVector4D to() const { return vector(m_pilot.to()); }
    double morph() const { return m_pilot.morph(); }
    double agitation() const { return m_pilot.agitation(); }
    double gather() const { return m_pilot.gather(); }
    double kick() const { return m_pilot.kick(); }

signals:
    void moved();
    void tuned();

private:
    static QVector4D vector(const Cymatics::Mode &mode) {
        return QVector4D(float(mode.n), float(mode.m), float(mode.sign), 0);
    }
    Cymatics::Pilot m_pilot;
};
