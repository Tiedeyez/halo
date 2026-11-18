#pragma once

#include <QObject>
#include <random>

// Flies the Fractal visual: a Julia set that never stops changing shape.
//
// The formula is z -> (1 - m) z^2 + m z^3 + c. The morph m drifts between
// the square (m = 0, the classic Julia sets) and the cube (m = 1, threefold
// ones) and back, and c travels around the edge of that family's
// connectedness set, the one place where its Julia sets are intricate
// (inside they go plain and round, outside they fall to dust). The edge is
// found along a ray each step by bisection; c eases toward it, so when the
// edge jumps (a ray crossing an antenna) the shape still glides.
//
// Everything only moves forward: the beat speeds the journey around the
// edge up and never turns it back, the bass speeds the morph and the turn.
namespace Fractal {

struct Complex {
    double re = 0, im = 0;
};

// Steps until the critical point 0 escapes under the morph-m polynomial
// with constant c, or `limit` if it never does.
int criticalEscape(double cre, double cim, double m, int limit);

// The edge of the connectedness set along the ray at `angle`: the furthest
// radius out to which c keeps 0's orbit bounded (to a few thousandths).
double edgeRadius(double angle, double m);

// How far from the centre the Julia set for (c, m) reaches: the furthest
// point, over many directions, that takes a while to escape.
double reach(Complex c, double m);

class Pilot {
public:
    static constexpr double Journey = 0.045;   // radians a second around the edge
    static constexpr double Morph = 0.014;     // radians a second of the morph's cycle
    static constexpr double Ease = 0.9;        // seconds for c to close most of a jump
    static constexpr double MaxSpeed = 0.5;    // furthest c travels in a second (1/120 a frame)

    explicit Pilot(unsigned seed = std::random_device{}());

    // Advance by dt seconds. pulse (0..1) speeds the journey; low (0..1)
    // the morph and the turn.
    void step(double dt, double pulse = 0, double low = 0);

    Complex c() const { return m_c; }
    double morph() const;           // 0..1
    double journey() const { return m_journey; }
    double angle() const { return m_angle; }
    double zoom() const;            // width of the view across its shorter side

private:
    Complex target() const;

    double m_journey = 0, m_morphPhase = 0, m_angle = 0, m_breath = 0;
    double m_wobble = 0;            // a per-run offset, so no two runs match
    double m_frame = 3.0;           // eased width that fits the set
    double m_measureIn = 0;         // seconds until the set is measured again
    double m_fit = 3.0;             // the width the last measure asked for
    Complex m_c;
};

}

class FractalPilot : public QObject {
    Q_OBJECT
    Q_PROPERTY(double cx READ cx NOTIFY moved)
    Q_PROPERTY(double cy READ cy NOTIFY moved)
    Q_PROPERTY(double morph READ morph NOTIFY moved)
    Q_PROPERTY(double angle READ angle NOTIFY moved)
    Q_PROPERTY(double zoom READ zoom NOTIFY moved)

public:
    explicit FractalPilot(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void step(double dt, double pulse, double low) {
        m_pilot.step(dt, pulse, low);
        emit moved();
    }

    double cx() const { return m_pilot.c().re; }
    double cy() const { return m_pilot.c().im; }
    double morph() const { return m_pilot.morph(); }
    double angle() const { return m_pilot.angle(); }
    double zoom() const { return m_pilot.zoom(); }

signals:
    void moved();

private:
    Fractal::Pilot m_pilot;
};
