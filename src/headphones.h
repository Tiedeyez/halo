#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <vector>

#include "dsp.h"

class Engine;

// Headphone correction, checked automatically.
//
// Whenever the audio output changes (and at start), the output's name is
// looked up in a database of 6,229 headphones measured by oratory1990,
// crinacle, Rtings and others (OPRA's dataset, which includes AutoEQ's;
// src/data/headphones.json, CC BY-SA 4.0). On an exact, unambiguous match
// the engine applies that model's correction, which makes the headphones
// neutral; on anything less nothing happens, since a wrong correction is
// worse than none. Nothing is looked up online.
//
// Only the output's name is known: Bluetooth and USB headphones name
// themselves ("WH-1000XM4", "AirPods Pro"); headphones in a 3.5 mm jack
// show up as the computer's own output and can't be told apart.
namespace Hp {

struct Band {
    int type = 0;   // 0 peak, 1 low shelf, 2 high shelf
    double frequency = 1000, gain = 0, q = 0.7;
};

struct Profile {
    QString vendor, product, subtype, source;
    double preamp = 0;
    std::vector<Band> bands;
    QString title() const { return vendor.isEmpty() ? product : vendor + " " + product; }
};

// Lower case letters and digits only.
QString normalize(const QString &text);
// The part of an output's name that could be a headphone model: without a
// possessive ("Ana's AirPods Pro"), bracketed notes ("(A2DP)") or the words
// Linux adds for profiles ("Analog Stereo", "Hands-Free").
QString modelPart(const QString &deviceName);

class Database {
public:
    // The JSON written by tools/headphones/build.py. False if it isn't that.
    bool load(const QByteArray &json);
    int size() const { return int(m_profiles.size()); }
    // The profile for an output name, or nullptr when there's no exact,
    // unambiguous match.
    const Profile *match(const QString &deviceName) const;

private:
    std::vector<Profile> m_profiles;
    // Normalised "product" and "vendor product" to profile index; -1 marks
    // a name more than one product shares.
    QHash<QString, int> m_byName;
};

// Filters for the engine, at `rate`.
std::vector<Dsp::Coefficients> design(const Profile &profile, double rate = 48000);

}

class Headphones : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString device READ device NOTIFY changed)
    Q_PROPERTY(QString model READ model NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(bool matched READ matched NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(int known READ known CONSTANT)

public:
    // stateFile: where turned-off devices are remembered (empty: the default
    // under ~/.local/state/cymtx).
    explicit Headphones(Engine *engine, const QString &stateFile = {}, QObject *parent = nullptr);

    QString device() const { return m_device; }
    QString model() const { return m_profile ? m_profile->title() : QString(); }
    QString source() const { return m_profile ? m_profile->source : QString(); }
    bool matched() const { return m_profile != nullptr; }
    bool active() const { return m_profile && !m_off.contains(m_device); }
    int known() const { return m_database.size(); }

    // On for this output (it is by default when there's a match), or off and
    // remembered as off for it.
    Q_INVOKABLE void setActive(bool on);

public slots:
    void outputChanged(const QString &deviceName);

signals:
    void changed();

private:
    void apply();
    void save() const;

    Engine *m_engine;
    QString m_stateFile;
    Hp::Database m_database;
    QString m_device;
    const Hp::Profile *m_profile = nullptr;
    QSet<QString> m_off;
};
