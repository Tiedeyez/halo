#include "mandelpilot.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Mandel {

namespace {
// Places with detail all the way down, where dives begin.
struct Seed { double x, y; };
constexpr Seed Seeds[] = {
    {-0.743643887, 0.131825904},   // Seahorse Valley
    {0.2549870, 0.0005000},        // Elephant Valley
    {-0.0452407411, 0.9868162204}, // a triple spiral
    {-1.7686, -0.0017},            // a mini-brot on the needle
    {-0.1011, 0.9563},             // the top of the main bulb
    {-1.25066, 0.02012},           // the period-2 bulb's edge
    {0.3602, -0.6413},             // a spiral arm below
    {-0.7453, 0.1127},             // Seahorse Valley's lower side
};

// How many grid points the pilot looks at across the middle of the view.
constexpr int Grid = 17;
}

int escape(double cx, double cy, int limit) {
    double x = 0, y = 0;
    for (int i = 0; i < limit; ++i) {
        const double xx = x * x, yy = y * y;
        if (xx + yy > 256)
            return i;
        y = 2 * x * y + cy;
        x = xx - yy + cx;
    }
    return limit;
}

Pilot::Pilot(unsigned seed) : m_rng(seed) {
    m_current = start();
    m_tx = m_current.x;
    m_ty = m_current.y;
    look(m_current, m_tx, m_ty);
    m_next = m_current;
}

View Pilot::start() {
    std::uniform_int_distribution<int> pick(0, int(std::size(Seeds)) - 1);
    std::uniform_real_distribution<double> jitter(-0.004, 0.004);
    std::uniform_real_distribution<double> turn(0, 6.283185307);
    const Seed s = Seeds[pick(m_rng)];
    View v;
    v.x = s.x + jitter(m_rng);
    v.y = s.y + jitter(m_rng);
    // Start already some way in, so the set's own shape doesn't fill the
    // screen and every dive looks different.
    v.scale = StartScale * std::exp(-2.0);
    v.angle = turn(m_rng);
    return v;
}

// Looks across the middle of the view for the edge of the set and picks,
// at random among the best few, a point to steer toward.
void Pilot::look(const View &view, double &tx, double &ty) {
    const double depth = std::log(StartScale / view.scale);
    const int limit = int(120 + depth * 40);
    struct Candidate { double x, y; int n; };
    std::vector<Candidate> found;
    found.reserve(Grid * Grid);
    const double reach = view.scale * 0.32;
    for (int j = 0; j < Grid; ++j)
        for (int i = 0; i < Grid; ++i) {
            const double cx = view.x + (i - Grid / 2) * reach / (Grid / 2);
            const double cy = view.y + (j - Grid / 2) * reach / (Grid / 2);
            const int n = escape(cx, cy, limit);
            if (n < limit)
                found.push_back({cx, cy, n});
        }
    if (found.empty()) {
        // All inside the set: head for the corner, out toward its edge.
        tx = view.x + reach;
        ty = view.y + reach;
        return;
    }
    // The edge is where points escape latest.
    std::sort(found.begin(), found.end(), [](const Candidate &a, const Candidate &b) { return a.n > b.n; });
    const int best = std::min<int>(5, int(found.size()));
    std::uniform_int_distribution<int> pick(0, best - 1);
    const Candidate &c = found[size_t(pick(m_rng))];
    tx = c.x;
    ty = c.y;
}

// Keep a target until the view is centred on it (then look again, deeper)
// or the dive has left it behind; switching on every look made it twitch.
bool Pilot::needsLook(const View &view, double tx, double ty) const {
    const double d = std::hypot(tx - view.x, ty - view.y) / view.scale;
    return d < 0.06 || d > 0.42;
}

void Pilot::steer(View &view, double &tx, double &ty, double dt) {
    // Glide toward the target: a fraction of the way each second, so the
    // view drifts rather than jumps (the target is never more than a third
    // of the view away).
    const double k = 1.0 - std::exp(-dt * 0.9);
    view.x += (tx - view.x) * k;
    view.y += (ty - view.y) * k;
}

void Pilot::step(double dt, double pulse, double low) {
    dt = std::clamp(dt, 0.0, 0.25);
    // Forward only: the beat adds thrust, never takes it away.
    const double zoom = std::exp(-Rate * dt * (1.0 + 1.5 * std::clamp(pulse, 0.0, 1.0)));
    const double turn = dt * (0.02 + 0.12 * std::clamp(low, 0.0, 1.0));

    m_current.scale *= zoom;
    m_current.angle += turn;
    m_lookTimer -= dt;
    if (m_lookTimer <= 0 && needsLook(m_current, m_tx, m_ty)) {
        look(m_current, m_tx, m_ty);
        m_lookTimer = 0.5;
    }
    steer(m_current, m_tx, m_ty, dt);

    if (!m_handingOver && m_current.scale < DeepestScale * std::exp(Rate * Handover * 2)) {
        // Start the next dive before the floor, so the dissolve finishes
        // while this one still has resolution.
        m_handingOver = true;
        m_reveal = 0;
        m_next = start();
        m_nextTx = m_next.x;
        m_nextTy = m_next.y;
        look(m_next, m_nextTx, m_nextTy);
        m_nextLookTimer = 0.5;
    }
    if (m_handingOver) {
        m_next.scale *= zoom;
        m_next.angle += turn;
        m_nextLookTimer -= dt;
        if (m_nextLookTimer <= 0 && needsLook(m_next, m_nextTx, m_nextTy)) {
            look(m_next, m_nextTx, m_nextTy);
            m_nextLookTimer = 0.5;
        }
        steer(m_next, m_nextTx, m_nextTy, dt);
        m_reveal = std::min(1.0, m_reveal + dt / Handover);
        if (m_reveal >= 1.0) {
            m_current = m_next;
            m_tx = m_nextTx;
            m_ty = m_nextTy;
            m_lookTimer = m_nextLookTimer;
            m_reveal = 0;
            m_handingOver = false;
            ++m_dives;
        }
    } else {
        m_next = m_current;
    }
}

}
