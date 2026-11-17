#pragma once

#include <QObject>
#include <random>

// Flies the Mandelbrot visual: an endless dive that only ever goes in.
//
// The zoom moves forward at a steady rate (doubling about every seven
// seconds) and the beat adds thrust; nothing ever pulls it back out. On the
// way down, the pilot keeps looking around the middle of the view, in double
// precision, for the edge of the set (points that escape, but only after
// many iterations, where the detail is) and steers toward one of the best,
// picked at random, so every dive wanders its own way down.
//
// The GPU draws in 32-bit floats, which run out of resolution about 60,000x
// down. Before that, a new dive starts at another rich place in the set and
// a pixel dissolve hands over to it, both still diving, so the motion never
// reverses. Each pixel is drawn from one view or the other, never both.
namespace Mandel {

struct View {
    double x = -0.6, y = 0, scale = 2.7, angle = 0;
};

// Iterations before c escapes |z| > 16, or `limit` if it never does.
int escape(double cx, double cy, int limit);

class Pilot {
public:
    static constexpr double StartScale = 2.4;
    static constexpr double DeepestScale = 4.5e-5;   // where float runs out
    static constexpr double Rate = 0.1;               // e-folds a second
    static constexpr double Handover = 2.5;           // seconds of dissolve

    explicit Pilot(unsigned seed = std::random_device{}());

    // Advance by dt seconds. pulse (0..1) adds thrust; low (0..1) turns it.
    void step(double dt, double pulse = 0, double low = 0);

    const View &current() const { return m_current; }
    const View &next() const { return m_next; }
    double reveal() const { return m_reveal; }   // 0 = current, 1 = next
    int dives() const { return m_dives; }

private:
    View start();
    void steer(View &view, double &tx, double &ty, double dt);
    void look(const View &view, double &tx, double &ty);
    bool needsLook(const View &view, double tx, double ty) const;

    std::mt19937 m_rng;
    View m_current, m_next;
    double m_tx = 0, m_ty = 0, m_nextTx = 0, m_nextTy = 0;
    double m_lookTimer = 0, m_nextLookTimer = 0;
    double m_reveal = 0;
    bool m_handingOver = false;
    int m_dives = 1;
};

}

class MandelPilot : public QObject {
    Q_OBJECT
    Q_PROPERTY(double x READ x NOTIFY moved)
    Q_PROPERTY(double y READ y NOTIFY moved)
    Q_PROPERTY(double scale READ scale NOTIFY moved)
    Q_PROPERTY(double angle READ angle NOTIFY moved)
    Q_PROPERTY(double nextX READ nextX NOTIFY moved)
    Q_PROPERTY(double nextY READ nextY NOTIFY moved)
    Q_PROPERTY(double nextScale READ nextScale NOTIFY moved)
    Q_PROPERTY(double nextAngle READ nextAngle NOTIFY moved)
    Q_PROPERTY(double reveal READ reveal NOTIFY moved)

public:
    explicit MandelPilot(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void step(double dt, double pulse, double low) {
        m_pilot.step(dt, pulse, low);
        emit moved();
    }

    double x() const { return m_pilot.current().x; }
    double y() const { return m_pilot.current().y; }
    double scale() const { return m_pilot.current().scale; }
    double angle() const { return m_pilot.current().angle; }
    double nextX() const { return m_pilot.next().x; }
    double nextY() const { return m_pilot.next().y; }
    double nextScale() const { return m_pilot.next().scale; }
    double nextAngle() const { return m_pilot.next().angle; }
    double reveal() const { return m_pilot.reveal(); }

signals:
    void moved();

private:
    Mandel::Pilot m_pilot;
};
