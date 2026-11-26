#include "cymatics.h"

#include <algorithm>
#include <cmath>

namespace Cymatics {

namespace {
constexpr double Pi = 3.14159265358979323846;
}

const std::vector<Mode> &modes() {
    static const std::vector<Mode> list = [] {
        std::vector<Mode> all;
        // n < m: with n == m the two halves are the same wave, and their
        // difference is a flat plate.
        for (int m = 2; m <= 8; ++m)
            for (int n = 0; n < m; ++n)
                for (int sign : {1, -1})
                    all.push_back({n, m, sign});
        std::stable_sort(all.begin(), all.end(), [](const Mode &a, const Mode &b) { return a.pitch() < b.pitch(); });
        return all;
    }();
    return list;
}

double height(const Mode &mode, double x, double y) {
    return std::cos(mode.n * Pi * x) * std::cos(mode.m * Pi * y)
         + mode.sign * std::cos(mode.m * Pi * x) * std::cos(mode.n * Pi * y);
}

void Pilot::change() {
    const int count = std::max(2, int(std::lround(double(modes().size()) * m_reach)));
    // Where the tone sits in the range this music has covered.
    const double span = std::max(0.08, m_high - m_low);
    const double at = std::clamp((m_tone - m_low) / span, 0.0, 1.0);
    const int wanted = std::clamp(int(std::lround(at * (count - 1))), 0, count - 1);
    // A change always shows something new: the nearest figure to the one
    // wanted that isn't showing and wasn't among the last few, so a steady
    // tone walks round its neighbours instead of flipping between two.
    const int remembered = std::min(4, count - 2);
    const auto recent = [&](int index) {
        if (index == m_to)
            return true;
        for (int i = 0; i < remembered; ++i)
            if (m_recent[i] == index)
                return true;
        return false;
    };
    int next = m_to;
    for (int reach = 0; reach < count && next == m_to; ++reach)
        for (int side : {1, -1}) {
            const int index = wanted + side * reach;
            if (index >= 0 && index < count && !recent(index)) {
                next = index;
                break;
            }
        }
    if (next == m_to)
        next = (m_to + 1) % count;
    for (int i = 3; i > 0; --i)
        m_recent[i] = m_recent[i - 1];
    m_recent[0] = m_to;
    m_from = m_to;
    m_to = next;
    m_morph = 0;
    m_sinceChange = 0;
    ++m_changes;
}

void Pilot::step(double dt, double pulse, double level, double tone) {
    dt = std::clamp(dt, 0.0, 0.1);
    tone = std::clamp(tone, 0.0, 1.0);
    level = std::clamp(level, 0.0, 1.0);
    if (!m_started) {
        m_started = true;
        m_tone = tone;
        m_low = std::min(m_low, tone);
        m_high = std::max(m_high, tone);
    }
    // The tone, eased over about a quarter of a second, and the range it
    // has covered: the ends follow at once outward and creep back inward.
    if (level > 0.03) {
        m_tone += (tone - m_tone) * (1 - std::exp(-dt * 4));
        m_low = m_tone < m_low ? m_tone : m_low + (m_tone - m_low) * (1 - std::exp(-dt * 0.04));
        m_high = m_tone > m_high ? m_tone : m_high + (m_tone - m_high) * (1 - std::exp(-dt * 0.04));
    }

    m_sinceChange += dt;
    m_kick = std::max(m_kick * std::exp(-dt / 0.14), 0.0);
    bool beat = false;
    if (m_armed && pulse > 0.5) {
        m_armed = false;
        beat = true;
        ++m_beats;
        m_kick = 1;
    } else if (!m_armed && pulse < 0.3) {
        m_armed = true;
    }

    if (morphing()) {
        m_morph += dt / MorphSeconds;
        if (m_morph >= 1) {
            m_morph = 0;
            m_from = m_to;
        }
    } else if (beat && m_beats % m_beatsPerChange == 0) {
        change();
    } else if (level > 0.08 && m_sinceChange > 3.0 * m_beatsPerChange + 2.0) {
        // Music without a beat to count still moves on.
        change();
    }

    // The sand is thrown about while the figure changes, most at the middle
    // of the change, and a little by every beat.
    const double thrown = morphing() ? std::sin(Pi * m_morph) : 0.0;
    m_agitation = std::max(thrown, 0.22 * m_kick);
    // Loud music drives the sand onto the lines; a quiet plate lets it lie
    // a little loose, but the figure always reads.
    const double target = 0.5 + 0.5 * std::min(1.0, level * 1.5);
    m_gather += (target - m_gather) * (1 - std::exp(-dt * 3));
}

}
