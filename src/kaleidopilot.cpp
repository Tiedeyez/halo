#include "kaleidopilot.h"

#include <algorithm>
#include <cmath>

namespace Kaleido {

namespace {
constexpr double Tau = 6.283185307179586;

// A number 0..1 from bits `shift` onward of a fingerprint.
double unit(quint64 f, int shift) { return double((f >> shift) & 0xffff) / 65535.0; }

double ease(double now, double goal, double dt, double seconds) {
    return now + (goal - now) * (1 - std::exp(-dt / seconds));
}
}

quint64 fingerprint(const QString &text) {
    quint64 h = 1469598103934665603ULL;
    for (const QChar c : text) {
        h ^= c.unicode();
        h *= 1099511628211ULL;
    }
    return h;
}

Song fromFile(const QString &path, const QString &title, const QString &artist, const QString &album) {
    Song s;
    const quint64 f = fingerprint(path + '\n' + title + '\n' + artist);
    static const int mirrors[] = {5, 6, 7, 8, 9, 10, 12};
    s.folds = mirrors[(f >> 3) % 7];
    // Kali constants in this box make lace rather than dust or a blank.
    s.cx = 0.52 + 0.30 * unit(f, 8);
    s.cy = 0.32 + 0.30 * unit(f, 24);
    s.ring = 0.1 + 0.5 * unit(f, 40);
    s.direction = (f >> 1) & 1 ? 1 : -1;
    s.zoom = 0.95 + 0.3 * unit(f, 56);
    s.phaseX = Tau * unit(f ^ 0x9e3779b97f4a7c15ULL, 4);
    s.phaseY = Tau * unit(f ^ 0x9e3779b97f4a7c15ULL, 20);
    // The album alone picks the colours.
    s.hue = unit(fingerprint(album.isEmpty() ? path.section('/', 0, -2) : album), 16);
    return s;
}

void Pilot::setSong(const Song &song, double bpm, double duration, double loudness) {
    if (m_started && song.folds != m_song.folds) {
        m_previousFolds = m_song.folds;
        m_foldMix = 0;
    }
    m_song = song;
    m_bpm = bpm > 40 && bpm < 240 ? bpm : 120;
    m_duration = duration > 1 ? duration : 180;
    m_loudness = loudness < 0 ? loudness : -14;
    if (!m_started) {
        m_previousFolds = song.folds;
        m_cx = song.cx;
        m_cy = song.cy;
        m_zoom = song.zoom;
        m_hue = song.hue;
        m_started = true;
    }
}

void Pilot::shapeAt(double position, double &cx, double &cy) const {
    // A slow loop through the constant's neighbourhood across the song.
    const double u = std::clamp(position / m_duration, 0.0, 1.0);
    cx = m_song.cx + 0.07 * std::sin(Tau * u * 3 + m_song.phaseX);
    cy = m_song.cy + 0.07 * std::sin(Tau * u * 2 + m_song.phaseY);
}

double Pilot::spinAt(double position) const {
    const double beats = position * m_bpm / 60;
    return m_song.direction * beats * (Tau / m_song.folds) / BeatsPerMirror;
}

void Pilot::step(double dt, double position, double pulse, double low, double mid) {
    dt = std::clamp(dt, 0.0, 0.25);
    pulse = std::clamp(pulse, 0.0, 1.0);
    low = std::clamp(low, 0.0, 1.0);
    mid = std::clamp(mid, 0.0, 1.0);

    // Turning with the tempo. The picture repeats every mirror's width, so a
    // seek or a new song only ever turns the short way to catch up, and
    // eases there rather than snapping.
    const double wedge = Tau / m_song.folds;
    double gap = spinAt(position) - m_spin;
    gap -= wedge * std::round(gap / wedge);
    m_spin += gap * (1 - std::exp(-dt / (std::fabs(gap) > 0.2 * wedge ? 0.35 : 0.05)));

    // Each beat clicks the inner pattern round a notch.
    const bool beat = pulse > 0.5;
    if (beat && !m_beat) {
        m_twistGoal += 0.10 + 0.22 * low;
        ++m_beats;
    }
    m_beat = pulse > 0.3 && (m_beat || beat);
    m_twist = ease(m_twist, m_twistGoal, dt, 0.18);

    double cx, cy;
    shapeAt(position, cx, cy);
    // The mids wobble it around its path.
    cx += 0.012 * mid * std::sin(m_twist * 7.0);
    cy += 0.012 * mid * std::cos(m_twist * 5.0);
    m_cx = ease(m_cx, cx, dt, 0.4);
    m_cy = ease(m_cy, cy, dt, 0.4);

    // The beat punches the view in.
    m_zoom = ease(m_zoom, m_song.zoom * (1 - 0.08 * pulse), dt, 0.05);
    // Around the colour wheel the short way.
    double dh = m_song.hue - m_hue;
    dh -= std::round(dh);
    m_hue = m_hue + dh * (1 - std::exp(-dt / 1.2));
    m_hue -= std::floor(m_hue);
    // Loud masters glow a touch more (-6 LUFS: 1.15, -20: 0.85).
    m_glow = ease(m_glow, std::clamp(1.0 + (m_loudness + 13) * 0.02, 0.85, 1.15), dt, 1.0);
    m_foldMix = std::min(1.0, m_foldMix + dt / 1.5);
}

}
