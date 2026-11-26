#include "headphones.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include "engine.h"

namespace Hp {

QString normalize(const QString &text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text.toLower())
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out += c;
    return out;
}

QString modelPart(const QString &deviceName) {
    QString s = deviceName;
    // "Ana's AirPods Pro", "Ana’s AirPods Pro"
    s.remove(QRegularExpression(R"(^\S+['’]s\s+)"));
    // "(A2DP)", "[LE]"
    s.remove(QRegularExpression(R"(\s*[\(\[][^\)\]]*[\)\]])"));
    // Words Linux or Bluetooth add after the name.
    static const QRegularExpression tail(
        R"((\s+|-)(analog|digital|stereo|mono|surround|output|playback|headset|hands-?free|a2dp|hfp|hsp|le|sink|iec958)$)",
        QRegularExpression::CaseInsensitiveOption);
    QString previous;
    while (previous != s) {
        previous = s;
        const auto m = tail.match(s);
        if (m.hasMatch())
            s.truncate(m.capturedStart());
        s = s.trimmed();
    }
    return s.trimmed();
}

bool Database::load(const QByteArray &json) {
    const QJsonArray list = QJsonDocument::fromJson(json).object().value("headphones").toArray();
    if (list.isEmpty())
        return false;
    m_profiles.clear();
    m_byName.clear();
    m_profiles.reserve(size_t(list.size()));
    for (const QJsonValue &v : list) {
        const QJsonArray a = v.toArray();
        Profile p;
        p.vendor = a.at(0).toString();
        p.product = a.at(1).toString();
        p.subtype = a.at(2).toString();
        p.source = a.at(3).toString();
        p.preamp = a.at(4).toDouble();
        for (const QJsonValue &b : a.at(5).toArray()) {
            const QJsonArray band = b.toArray();
            Band x{band.at(0).toInt(), band.at(1).toDouble(), band.at(2).toDouble(), band.at(3).toDouble()};
            if (x.type < 0 || x.type > 2 || !(x.frequency > 10 && x.frequency < 23000) || !(x.q > 0.05 && x.q < 30) ||
                !(std::fabs(x.gain) <= 24))
                continue;
            p.bands.push_back(x);
        }
        if (p.product.isEmpty() || p.bands.empty() || p.bands.size() > size_t(Engine::MaxCorrection))
            continue;
        const int index = int(m_profiles.size());
        m_profiles.push_back(p);
        for (const QString &key : {normalize(p.product), normalize(p.vendor + p.product)}) {
            // Too short or digits only ("10", "770") would match too much.
            if (key.size() < 4 || !key.contains(QRegularExpression("[a-z]")))
                continue;
            const auto it = m_byName.constFind(key);
            if (it == m_byName.constEnd())
                m_byName.insert(key, index);
            else if (*it != index)
                m_byName[key] = -1;
        }
    }
    return !m_profiles.empty();
}

const Profile *Database::match(const QString &deviceName) const {
    const QString key = normalize(modelPart(deviceName));
    if (key.size() < 4)
        return nullptr;
    // The computer's own outputs, never headphones by name.
    static const QStringList generic{"builtinaudio", "speakers", "speaker", "headphones", "hdmi", "displayport",
                                     "usbaudio", "dummyoutput", "default", "pulseaudio", "pipewire"};
    for (const QString &g : generic)
        if (key == g || key.startsWith("builtin") || key.startsWith("hdmi"))
            return nullptr;
    const int index = m_byName.value(key, -1);
    return index >= 0 ? &m_profiles[size_t(index)] : nullptr;
}

std::vector<Dsp::Coefficients> design(const Profile &profile, double rate) {
    std::vector<Dsp::Coefficients> out;
    for (const Band &b : profile.bands) {
        const double f = std::min(b.frequency, rate * 0.45);
        out.push_back(b.type == 1 ? Dsp::lowShelf(rate, f, b.q, b.gain)
                    : b.type == 2 ? Dsp::highShelf(rate, f, b.q, b.gain)
                                  : Dsp::peaking(rate, f, b.q, b.gain));
    }
    return out;
}

}

Headphones::Headphones(Engine *engine, const QString &stateFile, QObject *parent)
    : QObject(parent), m_engine(engine),
      m_stateFile(stateFile.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/cymtx/headphones.json"
                      : stateFile) {
    QFile data(":/data/headphones.json");
    if (data.open(QIODevice::ReadOnly))
        m_database.load(data.readAll());
    QFile state(m_stateFile);
    if (state.open(QIODevice::ReadOnly)) {
        for (const QJsonValue &v : QJsonDocument::fromJson(state.read(256 * 1024)).object().value("off").toArray())
            m_off.insert(v.toString());
    }
}

void Headphones::outputChanged(const QString &deviceName) {
    const Hp::Profile *profile = m_database.match(deviceName);
    if (deviceName == m_device && profile == m_profile)
        return;
    m_device = deviceName;
    m_profile = profile;
    apply();
    emit changed();
}

void Headphones::setActive(bool on) {
    if (!m_profile || on == active())
        return;
    if (on)
        m_off.remove(m_device);
    else
        m_off.insert(m_device);
    save();
    apply();
    emit changed();
}

void Headphones::apply() {
    if (!m_engine)
        return;
    if (active())
        m_engine->setCorrection(Hp::design(*m_profile), float(m_profile->preamp));
    else
        m_engine->setCorrection({}, 0);
}

void Headphones::save() const {
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile file(m_stateFile);
    if (!file.open(QIODevice::WriteOnly))
        return;
    QJsonArray off;
    for (const QString &d : m_off)
        off.append(d);
    file.write(QJsonDocument(QJsonObject{{"off", off}}).toJson());
    file.commit();
}
