#include "fractalpilot.h"

#include <algorithm>
#include <cmath>

namespace Fractal {

namespace {
constexpr double Tau = 6.283185307179586;
}

int criticalEscape(double cre, double cim, double m, int limit) {
    double x = 0, y = 0;
    for (int n = 0; n < limit; ++n) {
        // z^2 and z^3, blended.
        const double x2 = x * x - y * y, y2 = 2 * x * y;
        const double x3 = x2 * x - y2 * y, y3 = x2 * y + y2 * x;
        x = (1 - m) * x2 + m * x3 + cre;
        y = (1 - m) * y2 + m * y3 + cim;
        if (x * x + y * y > 16)
            return n;
    }
    return limit;
}

double edgeRadius(double angle, double m) {
    const double ux = std::cos(angle), uy = std::sin(angle);
    // 0 is always inside; 2.2 is always outside for this family.
    double inside = 0, outside = 2.2;
    for (int i = 0; i < 22; ++i) {
        const double mid = (inside + outside) / 2;
        if (criticalEscape(mid * ux, mid * uy, m, 160) >= 160)
            inside = mid;
        else
            outside = mid;
    }
    return inside;
}

double reach(Complex c, double m) {
    double furthest = 0.3;
    for (int a = 0; a < 48; ++a) {
        const double angle = Tau * a / 48, ux = std::cos(angle), uy = std::sin(angle);
        for (double r = 2.6; r > furthest; r -= 0.04) {
            double x = r * ux, y = r * uy;
            int n = 0;
            for (; n < 24; ++n) {
                const double x2 = x * x - y * y, y2 = 2 * x * y;
                const double x3 = x2 * x - y2 * y, y3 = x2 * y + y2 * x;
                x = (1 - m) * x2 + m * x3 + c.re;
                y = (1 - m) * y2 + m * y3 + c.im;
                if (x * x + y * y > 16)
                    break;
            }
            if (n >= 24) {
                furthest = r;
                break;
            }
        }
    }
    return furthest;
}

Pilot::Pilot(unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> unit(0, 1);
    m_journey = unit(rng) * Tau;
    m_morphPhase = unit(rng) * Tau;
    m_angle = unit(rng) * Tau;
    m_wobble = unit(rng) * 100;
    m_c = target();
    m_fit = m_frame = std::clamp(2.3 * reach(m_c, morph()), 2.4, 4.8);
}

double Pilot::morph() const { return 0.5 - 0.5 * std::cos(m_morphPhase); }

double Pilot::zoom() const {
    // Framed to fit the set, with a slow breath that keeps it alive.
    return m_frame * (1 + 0.05 * std::sin(m_breath));
}

Complex Pilot::target() const {
    const double m = morph();
    const double r = edgeRadius(m_journey, m);
    // Hover just inside and just outside the edge in turn: filled shapes
    // with tendrils, then lace about to break into dust.
    const double drift = 1.0 + 0.018 * std::sin(m_journey * 3.1 + m_wobble) - 0.006;
    return {r * drift * std::cos(m_journey), r * drift * std::sin(m_journey)};
}

void Pilot::step(double dt, double pulse, double low) {
    dt = std::clamp(dt, 0.0, 0.25);
    pulse = std::clamp(pulse, 0.0, 1.0);
    low = std::clamp(low, 0.0, 1.0);
    m_journey += dt * Journey * (1 + 3 * pulse);
    m_morphPhase += dt * Morph * (1 + 1.5 * low);
    m_angle += dt * (0.025 + 0.05 * low);
    m_breath += dt * 0.11;
    const Complex t = target();
    // Ease toward the edge, but never faster than MaxSpeed: where the edge
    // leaps (a ray crossing an antenna) the shape still glides across.
    const double dx = t.re - m_c.re, dy = t.im - m_c.im, gap = std::hypot(dx, dy);
    if (gap > 0) {
        const double move = std::min(gap * (1 - std::exp(-dt / Ease * 3)), MaxSpeed * dt);
        m_c.re += dx / gap * move;
        m_c.im += dy / gap * move;
    }
    // Measure the set four times a second and ease the framing to fit it.
    m_measureIn -= dt;
    if (m_measureIn <= 0) {
        m_measureIn = 0.25;
        m_fit = std::clamp(2.3 * reach(m_c, morph()), 2.4, 4.8);
    }
    m_frame += (m_fit - m_frame) * (1 - std::exp(-dt / 1.5));
}

}
