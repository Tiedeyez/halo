#pragma once

#include <QObject>
#include <QString>

// Flies the Kaleidoscope visual: a mirrored Kali-set fractal, its own for
// every song and turning in time with it.
//
// From the file: the song's identity (path, title, artist) picks the number
// of mirrors and a sequence of designs; the album picks the colour family,
// so an album's songs belong together; the tempo sets the turn, one
// mirror's width every eight beats, locked to the beat; loudness sets the
// glow.
//
// The design changes with the music's phrases, not its beats: it holds for
// 32 beats (eight bars) and morphs into the next design over the phrase's
// last four. Which design comes next is worked out from the song and the
// phrase number, so a moment in a song always looks the same.
//
// The beat only pulses it: the view snaps in and springs back, it flashes,
// and a shockwave rings out. The shape stays put.
//
// Everything eases, so a seek or a new song glides; a new song's mirror
// count crossfades in.
namespace Kaleido {

// FNV-1a: the same text gives the same number on every run and machine
// (qHash is seeded per process).
quint64 fingerprint(const QString &text);

struct Song {
    int folds = 6;
    double cx = 0.65, cy = 0.5;     // the Kali constant at the start of the song
    double ring = 0.3;              // where the bright lines sit
    double hue = 0;                 // 0..1 around the theme's colours
    double direction = 1;           // which way it turns
    double zoom = 1.1;
    double twist = 0.6;             // the inner turn of the first design
    quint64 seed = 0;               // picks every later design
};

// Everything that comes from the file alone.
Song fromFile(const QString &path, const QString &title, const QString &artist, const QString &album);

class Pilot {
public:
    static constexpr int BeatsPerMirror = 8;
    static constexpr int BeatsPerPhrase = 32;   // a design holds this long
    static constexpr int BeatsToMorph = 4;      // ...then morphs over this many

    void setSong(const Song &song, double bpm, double duration, double loudness);
    // dt seconds; position in the song (s); pulse, low, mid 0..1.
    void step(double dt, double position, double pulse, double low, double mid);

    int folds() const { return m_song.folds; }
    int previousFolds() const { return m_previousFolds; }
    double foldMix() const { return m_foldMix; }   // 0 = previous, 1 = this song's
    double spin() const { return m_spin; }
    double twist() const { return m_twist; }
    double cx() const { return m_cx; }
    double cy() const { return m_cy; }
    double ring() const { return m_song.ring; }
    double zoom() const { return m_zoom; }
    double hue() const { return m_hue; }
    double glow() const { return m_glow; }
    double kick() const { return m_kick; }   // 1 on a beat, falling fast
    int beats() const { return m_beats; }

    // The design meant for `position` (before easing).
    void shapeAt(double position, double &cx, double &cy, double &twist) const;
    double spinAt(double position) const;

private:
    Song m_song;
    double m_bpm = 120, m_duration = 180, m_loudness = -14;
    int m_previousFolds = 6;
    double m_foldMix = 1;
    double m_spin = 0, m_twist = 0;
    double m_cx = 0.65, m_cy = 0.5, m_zoom = 1.4, m_hue = 0, m_glow = 1;
    double m_kick = 0;
    bool m_beat = false, m_started = false;
    int m_beats = 0;
};

}

class KaleidoPilot : public QObject {
    Q_OBJECT
    Q_PROPERTY(int folds READ folds NOTIFY moved)
    Q_PROPERTY(int previousFolds READ previousFolds NOTIFY moved)
    Q_PROPERTY(double foldMix READ foldMix NOTIFY moved)
    Q_PROPERTY(double spin READ spin NOTIFY moved)
    Q_PROPERTY(double twist READ twist NOTIFY moved)
    Q_PROPERTY(double cx READ cx NOTIFY moved)
    Q_PROPERTY(double cy READ cy NOTIFY moved)
    Q_PROPERTY(double ring READ ring NOTIFY moved)
    Q_PROPERTY(double zoom READ zoom NOTIFY moved)
    Q_PROPERTY(double hue READ hue NOTIFY moved)
    Q_PROPERTY(double glow READ glow NOTIFY moved)
    Q_PROPERTY(double kick READ kick NOTIFY moved)

public:
    explicit KaleidoPilot(QObject *parent = nullptr) : QObject(parent) {}

    Q_INVOKABLE void setSong(const QString &path, const QString &title, const QString &artist, const QString &album,
                             double bpm, double duration, double loudness) {
        m_pilot.setSong(Kaleido::fromFile(path, title, artist, album), bpm, duration, loudness);
        emit moved();
    }
    Q_INVOKABLE void step(double dt, double position, double pulse, double low, double mid) {
        m_pilot.step(dt, position, pulse, low, mid);
        emit moved();
    }

    int folds() const { return m_pilot.folds(); }
    int previousFolds() const { return m_pilot.previousFolds(); }
    double foldMix() const { return m_pilot.foldMix(); }
    double spin() const { return m_pilot.spin(); }
    double twist() const { return m_pilot.twist(); }
    double cx() const { return m_pilot.cx(); }
    double cy() const { return m_pilot.cy(); }
    double ring() const { return m_pilot.ring(); }
    double zoom() const { return m_pilot.zoom(); }
    double hue() const { return m_pilot.hue(); }
    double glow() const { return m_pilot.glow(); }
    double kick() const { return m_pilot.kick(); }

signals:
    void moved();

private:
    Kaleido::Pilot m_pilot;
};
