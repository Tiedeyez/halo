#include "player.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>

#include "stream.h"

namespace {

// Loudness everything is brought to, the level most streaming services use.
constexpr double TargetLufs = -14.0;
// Loudness correction never lifts a song's true peak above this, so the
// limiter is left for blends and EQ boosts.
constexpr double PeakCeilingDbtp = -1.0;
// How long before a track ends the next one starts decoding.
constexpr double PrepareAhead = 20.0;

struct Preset {
    const char *name;
    std::array<double, 10> gains;
    double preamp;
};

// Gentle curves; nothing past 6 dB, and a negative preamp wherever a band is
// raised so the limiter rarely has to work.
const std::array<Preset, 8> &presets() {
    static const std::array<Preset, 8> list{{
        {"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0},
        {"Bass", {6, 5, 3.5, 1.5, 0, 0, 0, 0, 0, 0}, -5},
        {"Hip-hop", {5, 4.5, 2, 0, -1, -0.5, 0.5, 1.5, 1, 1.5}, -4.5},
        {"Vocal", {-2, -1.5, -1, 0.5, 2, 3, 3, 2, 0.5, -0.5}, -3},
        {"Treble", {0, 0, 0, 0, 0, 0.5, 2, 3.5, 4.5, 5}, -4.5},
        {"Loudness", {5, 4, 1.5, 0, -1, -1, 0, 1.5, 3.5, 4.5}, -4.5},
        {"Late night", {-4, -3, -1.5, 0, 1, 1.5, 1, 0, -1.5, -3}, 1},
        {"Warm", {2, 2, 1.5, 1, 0.5, 0, -0.5, -1, -1.5, -2}, -2},
    }};
    return list;
}

QString formatTime(double seconds) {
    const int s = int(std::max(0.0, seconds));
    return s >= 3600 ? QString("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

}

// ---- QueueModel ----

QueueModel::QueueModel(Player *player) : QAbstractListModel(player), m_player(player) {}

int QueueModel::rowCount(const QModelIndex &) const { return int(m_player->queuePaths().size()); }

QHash<int, QByteArray> QueueModel::roleNames() const {
    return {{PathRole, "path"},         {TitleRole, "title"},     {ArtistRole, "artist"}, {AlbumIdRole, "albumId"},
            {DurationRole, "duration"}, {CurrentRole, "current"}, {DriftRole, "drift"}};
}

QVariant QueueModel::data(const QModelIndex &index, int role) const {
    const QStringList &paths = m_player->queuePaths();
    if (!index.isValid() || index.row() >= paths.size())
        return {};
    const QString &path = paths[index.row()];
    if (role == PathRole)
        return path;
    if (role == CurrentRole)
        return index.row() == m_player->currentIndex();
    if (role == DriftRole)
        return m_player->isDrift(index.row());
    const Track *t = m_player->library()->track(path);
    if (!t && QFileInfo::exists(path)) {
        auto it = m_described.find(path);
        if (it == m_described.end())
            it = m_described.insert(path, m_player->library()->describe(path));
        t = &it.value();
    }
    switch (role) {
    case TitleRole: return t ? t->title : Library::titleFromFileName(path);
    case ArtistRole: return t ? t->artist : QString();
    case AlbumIdRole: return t ? t->albumId : QString();
    case DurationRole: return t ? t->duration : 0.0;
    }
    return {};
}

void QueueModel::reset() {
    beginResetModel();
    m_described.clear();
    endResetModel();
    emit countChanged();
}

void QueueModel::currentMoved(int from, int to) {
    for (int row : {from, to})
        if (row >= 0 && row < rowCount())
            emit dataChanged(index(row), index(row), {CurrentRole});
}

// ---- MomentModel ----

MomentModel::MomentModel(Player *player) : QAbstractListModel(player), m_player(player) {}

int MomentModel::rowCount(const QModelIndex &) const { return int(m_player->moments().size()); }

QHash<int, QByteArray> MomentModel::roleNames() const {
    return {{PathRole, "path"},       {TitleRole, "title"},     {ArtistRole, "artist"},
            {AlbumIdRole, "albumId"}, {SecondsRole, "seconds"}, {CreatedRole, "created"}};
}

QVariant MomentModel::data(const QModelIndex &index, int role) const {
    const auto &moments = m_player->moments();
    if (!index.isValid() || index.row() >= int(moments.size()))
        return {};
    const Moment &m = moments[size_t(index.row())];
    if (role == PathRole)
        return m.path;
    if (role == SecondsRole)
        return m.seconds;
    if (role == CreatedRole)
        return QDateTime::fromSecsSinceEpoch(m.created);
    const Track *t = m_player->library()->track(m.path);
    switch (role) {
    case TitleRole: return t ? t->title : Library::titleFromFileName(m.path);
    case ArtistRole: return t ? t->artist : QString();
    case AlbumIdRole: return t ? t->albumId : QString();
    }
    return {};
}

void MomentModel::reset() {
    beginResetModel();
    endResetModel();
    emit countChanged();
}

// ---- Player ----

Player::Player(Library *library, Analyzer *analyzer, const QString &stateDirectory, QObject *parent)
    : QObject(parent), m_library(library), m_analyzer(analyzer), m_queueModel(this), m_momentModel(this) {
    const QString state = stateDirectory.isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/halo"
                              : stateDirectory;
    m_stateFile = state + "/state.json";

    m_tick.setInterval(33);
    connect(&m_tick, &QTimer::timeout, this, &Player::tick);
    m_tick.start();
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(1500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Player::save);

    connect(m_analyzer, &Analyzer::analyzed, this, [this](const QString &path) {
        for (int d = 0; d < 2; ++d)
            if (m_streams[d] && m_streams[d]->path() == path)
                updateGain(d, path);
        if (path == currentPath()) {
            m_analyzer->lookup(path, m_currentAnalysis);
            emit analysisChanged();
        }
    });
    connect(m_analyzer, &Analyzer::progress, this, &Player::analysisProgress);
    // Titles in the queue come from the library, which may finish scanning
    // after the queue was restored.
    connect(m_library, &Library::changed, this, [this] {
        m_queueModel.reset();
        m_momentModel.reset();
        if (hasTrack())
            m_currentTrack = m_library->describe(currentPath());
        emit currentChanged();
        emit upNextChanged();
    });

    restore();
    m_engine.setVolume(float(m_volume));
    applyEq();
}

Player::~Player() {
    save();
    // The audio output is gone by now, so nothing reads the streams.
    for (Stream *s : m_streams)
        delete s;
    while (Stream *s = m_engine.retired())
        if (std::find(m_streams.begin(), m_streams.end(), s) == m_streams.end())
    for (Stream *s : m_graveyard)
        if (std::find(m_streams.begin(), m_streams.end(), s) == m_streams.end())
            delete s;
}

// ---- reading ----

QString Player::title() const { return hasTrack() ? m_currentTrack.title : QString(); }
QString Player::artist() const { return hasTrack() ? m_currentTrack.artist : QString(); }
QString Player::albumId() const { return hasTrack() ? m_currentTrack.albumId : QString(); }

QString Player::album() const {
    if (!hasTrack())
        return {};
    if (!m_currentTrack.album.isEmpty())
        return m_currentTrack.album;
    const Album *a = m_library->album(m_currentTrack.albumId);
    return a ? a->title : QString();
}

double Player::duration() const {
    if (!hasTrack())
        return 0;
    if (m_currentTrack.duration > 0)
        return m_currentTrack.duration;
    return m_currentAnalysis.valid ? m_currentAnalysis.duration : 0;
}

QString Player::nextTitle() const { return m_nextTrack.path.isEmpty() ? QString() : m_nextTrack.title; }
QString Player::nextArtist() const { return m_nextTrack.path.isEmpty() ? QString() : m_nextTrack.artist; }

QVariantList Player::eqGains() const {
    QVariantList list;
    for (double g : m_eqGains)
        list << g;
    return list;
}

QStringList Player::eqPresets() const {
    QStringList names;
    for (const auto &p : presets())
        names << QString::fromLatin1(p.name);
    return names;
}

QVariantList Player::currentMoments() const {
    QVariantList list;
    const QString path = currentPath();
    for (const Moment &m : m_moments)
        if (m.path == path)
            list << m.seconds;
    return list;
}

int Player::sleepRemaining() const {
    if (m_sleepMinutes <= 0)
        return 0;
    return std::max(0, int(m_sleepMinutes * 60 - m_sleepClock.elapsed() / 1000));
}

// Songs in the library that have been analysed (the cache can also hold
// files that have since left the library).
int Player::analyzedCount() const {
    int count = 0;
    for (const Track &t : m_library->tracks())
        count += m_analyzer->has(t.path) ? 1 : 0;
    return count;
}

// Eight beats at the outgoing tempo is one musical phrase: long enough to
// feel like a DJ blend, short enough not to muddy two songs. No beat means a
// plain six-second crossfade.
double Player::blendSeconds(double bpm) {
    if (bpm <= 0)
        return 6.0;
    double seconds = 8 * 60.0 / bpm;
    while (seconds < 4)
        seconds *= 2;
    while (seconds > 10)
        seconds /= 2;
    return seconds;
}

double Player::endPoint() const {
    const double length = duration();
    if (m_currentAnalysis.valid && m_currentAnalysis.audibleEnd > 0 && m_currentAnalysis.duration < 479 &&
        m_currentAnalysis.audibleEnd <= length + 1)
        return m_currentAnalysis.audibleEnd;
    return length;
}

double Player::blendIn() const {
    if (!m_blend || m_preparedIndex < 0 || m_blendingNow || !m_playing)
        return -1;
    return std::max(0.0, endPoint() - blendSeconds(m_currentAnalysis.bpm) - m_position);
}

QJsonObject Player::status() const {
    QJsonObject o{{"playing", m_playing},
                  {"path", currentPath()},
                  {"title", title()},
                  {"artist", artist()},
                  {"album", album()},
                  {"position", std::round(m_position * 10) / 10},
                  {"duration", std::round(duration() * 10) / 10},
                  {"volume", int(std::round(m_volume * 100))},
                  {"index", m_index},
                  {"queue", int(m_queue.size())},
                  {"blend", m_blend},
                  {"drift", m_drift},
                  {"shuffle", m_shuffle},
                  {"repeat", m_repeat == RepeatOne ? "one" : m_repeat == RepeatAll ? "all" : "off"},
                  {"normalize", m_normalize},
                  {"sleep", sleepRemaining()},
                  // So scripts (and tests) can wait for a scan to finish.
                  {"library", QJsonObject{{"scanning", m_library->scanning()},
                                          {"scanned", m_library->scanned()},
                                          {"songs", m_library->trackCount()},
                                          {"albums", m_library->albumCount()}}}};
    if (m_currentAnalysis.valid)
        o.insert("analysis", m_currentAnalysis.toJson());
    if (!m_nextTrack.path.isEmpty())
        o.insert("next", QJsonObject{{"title", m_nextTrack.title}, {"artist", m_nextTrack.artist},
                                     {"path", m_nextTrack.path}});
    if (!m_problem.isEmpty())
        o.insert("problem", m_problem);
    return o;
}

QStringList Player::driftOrder(const TrackAnalysis &seed, const QString &seedFolder,
                               const QList<QPair<QString, TrackAnalysis>> &candidates) {
    QList<QPair<double, QString>> scored;
    for (const auto &[path, a] : candidates) {
        double tempo = 3;
        if (seed.bpm > 0 && a.bpm > 0) {
            // Half and double time mix as well as the same tempo.
            double best = 1e9;
            // Half and double time cost one point: they mix, but the same
            // tempo mixes better.
            for (double factor : {0.5, 1.0, 2.0})
                best = std::min(best, std::fabs(std::log(a.bpm * factor / seed.bpm)) + (factor == 1.0 ? 0 : std::log(1.03)));
            tempo = best / std::log(1.03); // one point per 3 % of tempo
        }
        const double level = std::fabs(a.loudness - seed.loudness) / 2;
        const double sameAlbum = QFileInfo(path).absolutePath() == seedFolder ? 4 : 0;
        scored.append({tempo + level + sameAlbum, path});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
    QStringList order;
    for (const auto &s : scored)
        order << s.second;
    return order;
}

// ---- setters ----

void Player::setVolume(double volume) {
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume + 1, m_volume + 1))
        return;
    m_volume = volume;
    if (!m_sleepFading)
        m_engine.setVolume(float(volume));
    emit volumeChanged();
    scheduleSave();
}

void Player::setBlend(bool on) {
    if (on == m_blend)
        return;
    m_blend = on;
    // The prepared track was set up for the other mode (gapless arms it,
    // Blend starts it past its silence), so set it up again.
    dropPrepared();
    emit settingsChanged();
    emit positionChanged();
    emit message(on ? tr("Blend on: tracks mix into each other") : tr("Blend off: gapless playback"));
    scheduleSave();
}

void Player::setDrift(bool on) {
    if (on == m_drift)
        return;
    m_drift = on;
    emit settingsChanged();
    emit message(on ? tr("Drift on: Halo keeps playing from your library") : tr("Drift off"));
    scheduleSave();
}

void Player::setShuffle(bool on) {
    if (on == m_shuffle)
        return;
    pushUndo();
    m_shuffle = on;
    const QString current = currentPath();
    if (on) {
        m_unshuffled = m_queue;
        QStringList upcoming = m_queue.mid(m_index + 1);
        for (qsizetype i = upcoming.size() - 1; i > 0; --i)
            upcoming.swapItemsAt(i, QRandomGenerator::global()->bounded(int(i) + 1));
        m_queue = m_queue.mid(0, m_index + 1) + upcoming;
    } else if (!m_unshuffled.isEmpty()) {
        // Back to the order the songs were added in, keeping anything added
        // while shuffled at the end.
        QStringList restored;
        for (const QString &p : m_unshuffled)
            if (m_queue.contains(p) && !restored.contains(p))
                restored << p;
        for (const QString &p : m_queue)
            if (!restored.contains(p))
                restored << p;
        m_queue = restored;
        m_index = int(m_queue.indexOf(current));
        m_unshuffled.clear();
    }
    afterQueueEdit();
    emit settingsChanged();
}

void Player::setRepeat(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (mode == m_repeat)
        return;
    m_repeat = mode;
    dropPrepared();
    emit settingsChanged();
    scheduleSave();
}

void Player::cycleRepeat() { setRepeat((m_repeat + 1) % 3); }

void Player::setNormalize(bool on) {
    if (on == m_normalize)
        return;
    m_normalize = on;
    for (int d = 0; d < 2; ++d)
        if (m_streams[d])
            updateGain(d, m_streams[d]->path());
    emit settingsChanged();
    scheduleSave();
}

void Player::setCalm(bool on) {
    if (on == m_calm)
        return;
    m_calm = on;
    emit settingsChanged();
    scheduleSave();
}

void Player::setVisualStyle(int style) {
    if (style == m_visualStyle)
        return;
    m_visualStyle = std::clamp(style, 0, 2);
    emit settingsChanged();
    scheduleSave();
}

void Player::setEqEnabled(bool on) {
    if (on == m_eqEnabled)
        return;
    m_eqEnabled = on;
    applyEq();
}

void Player::setEqPreamp(double db) {
    m_eqPreamp = std::clamp(db, -12.0, 12.0);
    applyEq();
}

void Player::setEqGain(int band, double db) {
    if (band < 0 || band >= 10)
        return;
    m_eqGains[size_t(band)] = std::clamp(db, -12.0, 12.0);
    m_eqPreset = "Custom";
    m_eqEnabled = true;
    applyEq();
}

void Player::applyEqPreset(const QString &name) {
    for (const auto &p : presets())
        if (name == QLatin1String(p.name)) {
            m_eqGains = p.gains;
            m_eqPreamp = p.preamp;
            m_eqPreset = name;
            m_eqEnabled = name != "Flat";
            applyEq();
            return;
        }
}

void Player::applyEq() {
    std::array<float, 10> gains;
    for (int i = 0; i < 10; ++i)
        gains[size_t(i)] = float(m_eqGains[size_t(i)]);
    m_engine.setEqualizer(gains, float(m_eqPreamp), m_eqEnabled);
    emit eqChanged();
    scheduleSave();
}

void Player::setAudioProblem(const QString &problem) {
    m_audioProblem = problem;
    const QString combined = problem;
    if (combined != m_problem) {
        m_problem = combined;
        emit problemChanged();
    }
}

void Player::setPlaying(bool playing) {
    if (playing == m_playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

// ---- decks ----

void Player::updateGain(int deck, const QString &path) {
    double db = 0;
    TrackAnalysis a;
    if (m_normalize && m_analyzer->lookup(path, a) && a.loudness > -60) {
        db = std::clamp(TargetLufs - a.loudness, -12.0, 6.0);
        if (a.hasTruePeak())
            db = std::min(db, PeakCeilingDbtp - a.truePeak);
    }
    m_engine.setTrackGain(deck, float(db));
}

void Player::dropPrepared() {
    if (m_preparedIndex < 0 && m_preparedPath.isEmpty())
        return;
    const int deck = 1 - m_active;
    if (m_streams[deck] && deck != m_pendingDeck && !m_blendingNow) {
        m_engine.unload(deck); // the engine hands it back through retired()
        m_streams[deck] = nullptr;
    }
    m_preparedIndex = -1;
    m_preparedPath.clear();
}

void Player::startTrack(int index, double at, bool autoplay) {
    if (index < 0 || index >= m_queue.size())
        return;
    dropPrepared();
    const QString path = m_queue[index];
    const int deck = 1 - m_active;
    auto *stream = new Stream(path, at);
    stream->start();
    m_streams[deck] = stream;
    updateGain(deck, path);
    m_engine.load(deck, stream);
    m_pendingDeck = deck;
    m_blendingNow = false;
    emit blendingChanged();

    const int previous = m_index;
    m_index = index;
    m_position = at;
    m_queueModel.currentMoved(previous, index);
    advanceTo(index);
    if (autoplay) {
        m_engine.setPaused(false);
        setPlaying(true);
    }
    emit positionChanged();
}

// Everything that follows the current track changing, however it changed.
void Player::advanceTo(int index) {
    m_index = index;
    m_currentTrack = m_library->describe(currentPath());
    m_counted = false;
    m_listened = 0;
    m_currentAnalysis = {};
    m_analyzer->lookup(currentPath(), m_currentAnalysis);
    m_analyzer->prioritize(currentPath());
    m_history.removeAll(currentPath());
    m_history.append(currentPath());
    while (m_history.size() > 200)
        m_history.removeFirst();
    const int ni = nextIndex();
    m_nextTrack = ni >= 0 ? m_library->describe(m_queue[ni]) : Track{};
    if (ni >= 0)
        m_analyzer->prioritize(m_queue[ni]);
    emit currentChanged();
    emit analysisChanged();
    emit upNextChanged();
    emit momentsChanged();
    scheduleSave();
}

int Player::nextIndex() const {
    if (m_queue.isEmpty() || m_index < 0)
        return -1;
    if (m_repeat == RepeatOne)
        return m_index;
    for (int step = 1; step <= m_queue.size(); ++step) {
        int i = m_index + step;
        if (i >= m_queue.size()) {
            if (m_repeat != RepeatAll)
                return -1;
            i %= int(m_queue.size());
        }
        if (!m_failed.contains(m_queue[i]))
            return i;
    }
    return -1;
}

void Player::prepareNext() {
    int ni = nextIndex();
    if (ni < 0) {
        maybeDrift();
        ni = nextIndex();
    }
    if (ni < 0)
        return;
    const QString path = m_queue[ni];
    const int deck = 1 - m_active;
    if (m_streams[deck])
        return; // still fading out from the last blend
    double start = 0;
    TrackAnalysis a;
    // Blend skips the next track's silent lead-in; gapless keeps every sample.
    if (m_blend && m_analyzer->lookup(path, a) && a.audibleStart > 0.2 && a.audibleStart < 12)
        start = a.audibleStart;
    auto *stream = new Stream(path, start);
    stream->start();
    m_streams[deck] = stream;
    updateGain(deck, path);
    m_engine.load(deck, stream);
    if (!m_blend)
        m_engine.armGapless(deck);
    m_preparedIndex = ni;
    m_preparedPath = path;
    if (m_nextTrack.path != path) {
        m_nextTrack = m_library->describe(path);
        emit upNextChanged();
    }
}

void Player::maybeDrift() {
    if (!m_drift || m_queue.isEmpty() || nextIndex() >= 0)
        return;
    const auto &tracks = m_library->tracks();
    if (tracks.empty())
        return;
    const QString seedPath = currentPath();
    TrackAnalysis seed;
    m_analyzer->lookup(seedPath, seed);
    const QSet<QString> recent(m_history.begin(), m_history.end());
    const QSet<QString> queued(m_queue.begin(), m_queue.end());

    // Sample the library rather than scoring every track of a huge one.
    QList<QPair<QString, TrackAnalysis>> candidates;
    QStringList unanalysed;
    const int tries = int(std::min<size_t>(tracks.size(), 4000));
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < tries; ++i) {
        const Track &t = tracks[tracks.size() <= 4000 ? size_t(i) : size_t(rng->bounded(int(tracks.size())))];
        if (recent.contains(t.path) || queued.contains(t.path) || m_failed.contains(t.path))
            continue;
        TrackAnalysis a;
        if (m_analyzer->lookup(t.path, a) && a.duration > 20)
            candidates.append({t.path, a});
        else if (unanalysed.size() < 50)
            unanalysed << t.path;
    }
    QString pick;
    if (candidates.size() >= 3) {
        const QStringList order = driftOrder(seed, QFileInfo(seedPath).absolutePath(), candidates);
        pick = order[rng->bounded(int(std::min<qsizetype>(6, order.size())))];
    } else if (!unanalysed.isEmpty()) {
        pick = unanalysed[rng->bounded(int(unanalysed.size()))];
    }
    // Grow the pool Drift draws from.
    m_analyzer->enqueue(unanalysed.mid(0, 20));
    if (pick.isEmpty())
        return;
    m_queue.append(pick);
    m_driftPaths.insert(pick);
    m_queueModel.reset();
    scheduleSave();
}

void Player::reclaim() {
    // Streams are only ever freed here, and only once the engine has handed
    // them back: a stream the GUI replaced may still be mid-read on the audio
    // thread until the engine applies the command that lets go of it. A
    // handed-back stream the GUI still shows (its track just ended) waits
    // until the GUI lets go of it too.
    while (Stream *s = m_engine.retired())
        if (std::find(m_graveyard.begin(), m_graveyard.end(), s) == m_graveyard.end())
            m_graveyard.push_back(s);
    for (auto it = m_graveyard.begin(); it != m_graveyard.end();) {
        Stream *s = *it;
        if (std::find(m_streams.begin(), m_streams.end(), s) != m_streams.end()) {
            ++it;
            continue;
        }
        delete s;
        it = m_graveyard.erase(it);
    }
}

void Player::stopAtEnd() {
    if (m_streams[m_active]) {
        m_streams[m_active] = nullptr;
    }
    m_engine.setPaused(true);
    setPlaying(false);
    m_position = 0;
    emit positionChanged();
    scheduleSave();
}

void Player::countPlay() {
    if (m_counted || !hasTrack())
        return;
    const double length = duration();
    if (m_listened >= std::min(240.0, length > 0 ? length * 0.5 : 30.0)) {
        m_counted = true;
        m_library->recordPlay(currentPath());
    }
}

void Player::tick() {
    // A stream the engine let go of is only deleted once it is in the
    // graveyard; mark the decks the engine finished with first.
    if (m_pendingDeck >= 0) {
        Stream *s = m_streams[m_pendingDeck];
        if (!s) {
            m_pendingDeck = -1;
        } else if (s->failed()) {
            const QString path = s->path();
            m_failed.insert(path);
            m_problem = tr("Can't play %1: %2").arg(QFileInfo(path).fileName(), s->error());
            emit problemChanged();
            m_streams[m_pendingDeck] = nullptr;
            m_engine.unload(m_pendingDeck);
            m_pendingDeck = -1;
            const int ni = nextIndex();
            if (ni >= 0 && ni != m_index)
                startTrack(ni, 0, m_playing);
            else
                stopAtEnd();
        } else if (s->ready()) {
            const int other = 1 - m_pendingDeck;
            m_engine.playNow(m_pendingDeck);
            if (m_streams[other]) {
                m_streams[other] = nullptr;
            }
            m_active = m_pendingDeck;
            m_pendingDeck = -1;
            m_lastSwitches = m_engine.switches();
            if (!m_audioProblem.isEmpty() || m_problem != m_audioProblem) {
                m_problem = m_audioProblem;
                emit problemChanged();
            }
        }
    }

    if (m_pendingDeck < 0) {
        // The engine started the prepared track by itself (gapless).
        if (m_engine.switches() != m_lastSwitches) {
            m_lastSwitches = m_engine.switches();
            if (m_streams[m_active]) {
                m_streams[m_active] = nullptr;
            }
            m_active = 1 - m_active;
            const int previous = m_index;
            const int ni = m_preparedIndex;
            m_preparedIndex = -1;
            m_preparedPath.clear();
            if (ni >= 0) {
                m_queueModel.currentMoved(previous, ni);
                advanceTo(ni);
            }
        }
        // The outgoing track of a blend has faded out.
        // (The engine raises its flag on its next block, so give it a moment.)
        if (m_blendingNow && !m_engine.blending() && QDateTime::currentMSecsSinceEpoch() - m_blendPosted > 400) {
            m_blendingNow = false;
            emit blendingChanged();
        }
        // The track ran out with nothing armed after it.
        if (m_playing && m_streams[m_active] && m_engine.deckEnded(m_active)) {
            m_streams[m_active] = nullptr;
            const int prepared = m_preparedIndex;
            const int prepDeck = 1 - m_active;
            if (prepared >= 0 && m_streams[prepDeck]) {
                // Blend was due but couldn't start in time: cut to it.
                m_engine.playNow(prepDeck);
                m_active = prepDeck;
                m_preparedIndex = -1;
                m_preparedPath.clear();
                const int previous = m_index;
                m_queueModel.currentMoved(previous, prepared);
                advanceTo(prepared);
            } else {
                stopAtEnd();
            }
        }
    }

    // Position, from the stream the engine is reading.
    Stream *current = m_pendingDeck >= 0 ? nullptr : m_streams[m_active];
    if (current) {
        const double position = current->position();
        const double delta = position - m_position;
        if (m_playing && delta > 0 && delta < 1)
            m_listened += delta;
        if (std::fabs(delta) > 0.02) {
            m_position = position;
            emit positionChanged();
        }
        countPlay();

        if (m_playing && !m_blendingNow) {
            const double remaining = endPoint() - m_position;
            if (m_preparedIndex < 0 && remaining < PrepareAhead && remaining > 0)
                prepareNext();
            const int prepDeck = 1 - m_active;
            if (m_blend && m_preparedIndex >= 0 && m_streams[prepDeck] && m_streams[prepDeck]->ready()) {
                const double length = std::min(blendSeconds(m_currentAnalysis.bpm), std::max(1.0, duration() / 3));
                if (remaining <= length) {
                    m_engine.blend(prepDeck, std::max(1.0, remaining), true);
                    m_streams[m_active] = nullptr; // the engine retires it after the fade
                    m_active = prepDeck;
                    m_blendingNow = true;
                    m_blendPosted = QDateTime::currentMSecsSinceEpoch();
                    emit blendingChanged();
                    const int previous = m_index;
                    const int ni = m_preparedIndex;
                    m_preparedIndex = -1;
                    m_preparedPath.clear();
                    m_queueModel.currentMoved(previous, ni);
                    advanceTo(ni);
                }
            }
            if (m_preparedIndex >= 0 && m_streams[prepDeck] && m_streams[prepDeck]->failed()) {
                m_failed.insert(m_streams[prepDeck]->path());
                m_engine.unload(prepDeck);
                m_streams[prepDeck] = nullptr;
                m_preparedIndex = -1;
                m_preparedPath.clear();
            }
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_playing && now - m_lastPositionSave > 10000) {
            m_lastPositionSave = now;
            scheduleSave();
        }
    }

    // Sleep timer: fade out over fifteen seconds, then pause.
    if (m_sleepMinutes > 0) {
        const double left = m_sleepMinutes * 60.0 - m_sleepClock.elapsed() / 1000.0;
        if (left <= 15 && m_playing) {
            m_sleepFading = true;
            m_engine.setVolume(float(m_volume * std::max(0.0, left / 15)));
        }
        if (left <= 0) {
            pause();
            m_sleepMinutes = 0;
            m_sleepFading = false;
            m_engine.setVolume(float(m_volume));
            emit sleepChanged();
        } else if (int(left) != int(left + 0.033)) {
            emit sleepChanged();
        }
    }

    reclaim();
}

// ---- actions ----

void Player::playPaths(const QStringList &paths, int start) {
    const QStringList files = Library::expand(paths);
    if (files.isEmpty())
        return;
    pushUndo();
    m_queue = files;
    m_unshuffled.clear();
    m_driftPaths.clear();
    m_failed.clear();
    start = std::clamp(start, 0, int(files.size()) - 1);
    if (m_shuffle) {
        // Shuffle plays the chosen song first, then the rest in any order.
        m_unshuffled = m_queue;
        QString first = m_queue.takeAt(start);
        for (qsizetype i = m_queue.size() - 1; i > 0; --i)
            m_queue.swapItemsAt(i, QRandomGenerator::global()->bounded(int(i) + 1));
        m_queue.prepend(first);
        start = 0;
    }
    m_index = -1;
    m_queueModel.reset();
    startTrack(start, 0, true);
    emit undoChanged();
}

void Player::playAlbum(const QString &albumId, int start) { playPaths(m_library->albumPaths(albumId), start); }

void Player::enqueue(const QStringList &paths) {
    const QStringList files = Library::expand(paths);
    if (files.isEmpty())
        return;
    pushUndo();
    const bool wasEmpty = m_queue.isEmpty();
    m_queue += files;
    if (m_shuffle)
        m_unshuffled += files;
    afterQueueEdit();
    emit message(tr("Added %n songs to the queue", "", int(files.size())));
    if (wasEmpty)
        startTrack(0, 0, true);
}

void Player::playNext(const QStringList &paths) {
    const QStringList files = Library::expand(paths);
    if (files.isEmpty())
        return;
    pushUndo();
    const bool wasEmpty = m_queue.isEmpty();
    for (int i = 0; i < files.size(); ++i)
        m_queue.insert(m_index + 1 + i, files[i]);
    afterQueueEdit();
    emit message(tr("Playing %n songs next", "", int(files.size())));
    if (wasEmpty)
        startTrack(0, 0, true);
}

void Player::playAt(int index) {
    if (index < 0 || index >= m_queue.size())
        return;
    m_failed.remove(m_queue[index]);
    startTrack(index, 0, true);
}

void Player::play() {
    if (!hasTrack()) {
        if (!m_queue.isEmpty())
            startTrack(0, 0, true);
        return;
    }
    if (!m_streams[m_active] && m_pendingDeck < 0) {
        // Nothing decoded yet (a restored session, or the queue ended).
        startTrack(m_index, m_position, true);
        return;
    }
    m_engine.setPaused(false);
    setPlaying(true);
}

void Player::pause() {
    m_engine.setPaused(true);
    setPlaying(false);
    scheduleSave();
}

void Player::togglePlay() {
    if (m_playing)
        pause();
    else
        play();
}

void Player::next() {
    const int ni = nextIndex();
    if (ni >= 0 && m_repeat != RepeatOne) {
        startTrack(ni, 0, true);
        return;
    }
    if (m_repeat == RepeatOne && m_index + 1 < m_queue.size()) {
        startTrack(m_index + 1, 0, true);
        return;
    }
    maybeDrift();
    if (m_index + 1 < m_queue.size())
        startTrack(m_index + 1, 0, true);
}

// Like every player: back to the start of the song, or to the one before
// it in the first three seconds.
void Player::previous() {
    if (!hasTrack())
        return;
    if (m_position > 3 || m_index == 0)
        seek(0);
    else
        startTrack(m_index - 1, 0, true);
}

void Player::seek(double seconds) {
    if (!hasTrack())
        return;
    const double length = duration();
    seconds = std::clamp(seconds, 0.0, length > 1 ? length - 0.5 : std::max(0.0, seconds));
    startTrack(m_index, seconds, m_playing);
    emit seeked(seconds);
}

void Player::seekBy(double seconds) { seek(m_position + seconds); }

void Player::removeAt(int index) {
    if (index < 0 || index >= m_queue.size())
        return;
    pushUndo();
    const bool current = index == m_index;
    const QString path = m_queue.takeAt(index);
    m_driftPaths.remove(path);
    m_unshuffled.removeOne(path);
    if (index < m_index)
        --m_index;
    if (current) {
        if (m_index < m_queue.size())
            startTrack(m_index, 0, m_playing);
        else {
            m_index = int(m_queue.size()) - 1;
            stopAtEnd();
            advanceTo(m_index);
        }
    }
    afterQueueEdit();
}

void Player::move(int from, int to) {
    if (from < 0 || from >= m_queue.size() || to < 0 || to >= m_queue.size() || from == to)
        return;
    pushUndo();
    const QString current = currentPath();
    m_queue.move(from, to);
    m_index = int(m_queue.indexOf(current));
    afterQueueEdit();
}

void Player::clearQueue() {
    if (m_queue.isEmpty())
        return;
    pushUndo();
    // The song that is playing stays; everything else goes.
    const QString current = currentPath();
    m_queue = current.isEmpty() ? QStringList() : QStringList{current};
    m_index = current.isEmpty() ? -1 : 0;
    m_unshuffled.clear();
    m_driftPaths.clear();
    afterQueueEdit();
    emit message(tr("Queue cleared. Ctrl+Z brings it back."));
}

void Player::afterQueueEdit() {
    dropPrepared();
    m_queueModel.reset();
    const int ni = nextIndex();
    m_nextTrack = ni >= 0 ? m_library->describe(m_queue[ni]) : Track{};
    emit upNextChanged();
    emit undoChanged();
    scheduleSave();
}

void Player::pushUndo() {
    m_undo.push_back({m_queue, m_index});
    if (m_undo.size() > 50)
        m_undo.erase(m_undo.begin());
    m_redo.clear();
}

void Player::undo() {
    if (m_undo.empty())
        return;
    m_redo.push_back({m_queue, m_index});
    const Snapshot s = m_undo.back();
    m_undo.pop_back();
    const QString current = currentPath();
    m_queue = s.queue;
    const int found = int(m_queue.indexOf(current));
    m_index = found >= 0 ? found : s.index;
    afterQueueEdit();
    if (found < 0 && hasTrack())
        startTrack(m_index, 0, m_playing);
    else
        advanceTo(m_index);
}

void Player::redo() {
    if (m_redo.empty())
        return;
    m_undo.push_back({m_queue, m_index});
    const Snapshot s = m_redo.back();
    m_redo.pop_back();
    const QString current = currentPath();
    m_queue = s.queue;
    const int found = int(m_queue.indexOf(current));
    m_index = found >= 0 ? found : s.index;
    afterQueueEdit();
    if (found < 0 && hasTrack())
        startTrack(m_index, 0, m_playing);
    else
        advanceTo(m_index);
}

void Player::addMoment() {
    if (!hasTrack())
        return;
    const QString path = currentPath();
    for (const Moment &m : m_moments)
        if (m.path == path && std::fabs(m.seconds - m_position) < 2) {
            emit message(tr("There's already a moment here"));
            return;
        }
    m_moments.insert(m_moments.begin(), {path, m_position, QDateTime::currentSecsSinceEpoch()});
    m_momentModel.reset();
    emit momentsChanged();
    emit message(tr("Moment saved at %1").arg(formatTime(m_position)));
    scheduleSave();
}

void Player::jumpMoment(int direction) {
    const QVariantList marks = currentMoments();
    double best = -1;
    for (const QVariant &v : marks) {
        const double s = v.toDouble();
        if (direction > 0 && s > m_position + 1 && (best < 0 || s < best))
            best = s;
        if (direction < 0 && s < m_position - 2 && (best < 0 || s > best))
            best = s;
    }
    if (best >= 0)
        seek(best);
    else
        emit message(direction > 0 ? tr("No moment after this point") : tr("No moment before this point"));
}

void Player::playMoment(int index) {
    if (index < 0 || index >= int(m_moments.size()))
        return;
    const Moment m = m_moments[size_t(index)];
    if (!QFileInfo::exists(m.path)) {
        emit message(tr("That file is no longer there"));
        return;
    }
    if (m.path == currentPath()) {
        seek(m.seconds);
        return;
    }
    int at = int(m_queue.indexOf(m.path));
    if (at < 0) {
        pushUndo();
        at = m_index + 1;
        m_queue.insert(at, m.path);
        afterQueueEdit();
    }
    startTrack(at, m.seconds, true);
}

void Player::removeMoment(int index) {
    if (index < 0 || index >= int(m_moments.size()))
        return;
    m_moments.erase(m_moments.begin() + index);
    m_momentModel.reset();
    emit momentsChanged();
    scheduleSave();
}

void Player::startSleep(int minutes) {
    m_sleepMinutes = std::max(0, minutes);
    m_sleepClock.restart();
    m_sleepFading = false;
    m_engine.setVolume(float(m_volume));
    emit sleepChanged();
    if (minutes > 0)
        emit message(tr("Halo will fade out and pause in %n minutes", "", minutes));
}

void Player::cancelSleep() {
    if (m_sleepMinutes == 0)
        return;
    m_sleepMinutes = 0;
    m_sleepFading = false;
    m_engine.setVolume(float(m_volume));
    emit sleepChanged();
    emit message(tr("Sleep timer off"));
}

// ---- state ----

void Player::scheduleSave() {
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void Player::flush() {
    if (m_saveTimer.isActive())
        save();
}

void Player::save() {
    m_saveTimer.stop();
    if (m_stateFile.isEmpty())
        return;
    QJsonArray moments;
    for (const Moment &m : m_moments)
        moments.append(QJsonObject{{"path", m.path}, {"seconds", m.seconds}, {"created", double(m.created)}});
    QJsonArray gains;
    for (double g : m_eqGains)
        gains.append(g);
    const QJsonObject root{
        {"version", 1},
        {"queue", QJsonArray::fromStringList(m_queue)},
        {"drift", QJsonArray::fromStringList(QStringList(m_driftPaths.begin(), m_driftPaths.end()))},
        {"history", QJsonArray::fromStringList(m_history.mid(std::max<qsizetype>(0, m_history.size() - 100)))},
        {"index", m_index},
        {"position", m_position},
        {"volume", m_volume},
        {"settings", QJsonObject{{"blend", m_blend}, {"drift", m_drift}, {"shuffle", m_shuffle},
                                 {"repeat", m_repeat}, {"normalize", m_normalize}, {"calm", m_calm},
                                 {"visual", m_visualStyle}}},
        {"eq", QJsonObject{{"enabled", m_eqEnabled}, {"gains", gains}, {"preamp", m_eqPreamp},
                           {"preset", m_eqPreset}}},
        {"moments", moments}};
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile file(m_stateFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.commit();
    }
}

void Player::restore() {
    QFile file(m_stateFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("version").toInt() != 1)
        return;
    for (const auto &v : root.value("queue").toArray())
        m_queue << v.toString();
    for (const auto &v : root.value("drift").toArray())
        m_driftPaths.insert(v.toString());
    for (const auto &v : root.value("history").toArray())
        m_history << v.toString();
    m_volume = std::clamp(root.value("volume").toDouble(0.8), 0.0, 1.0);
    const QJsonObject settings = root.value("settings").toObject();
    m_blend = settings.value("blend").toBool(true);
    m_drift = settings.value("drift").toBool(true);
    m_shuffle = settings.value("shuffle").toBool(false);
    m_repeat = std::clamp(settings.value("repeat").toInt(0), 0, 2);
    m_normalize = settings.value("normalize").toBool(true);
    m_calm = settings.value("calm").toBool(false);
    m_visualStyle = std::clamp(settings.value("visual").toInt(0), 0, 2);
    const QJsonObject eq = root.value("eq").toObject();
    m_eqEnabled = eq.value("enabled").toBool(false);
    const QJsonArray gains = eq.value("gains").toArray();
    for (int i = 0; i < 10 && i < gains.size(); ++i)
        m_eqGains[size_t(i)] = std::clamp(gains.at(i).toDouble(), -12.0, 12.0);
    m_eqPreamp = std::clamp(eq.value("preamp").toDouble(), -12.0, 12.0);
    m_eqPreset = eq.value("preset").toString("Flat");
    for (const auto &v : root.value("moments").toArray()) {
        const QJsonObject o = v.toObject();
        m_moments.push_back({o.value("path").toString(), o.value("seconds").toDouble(),
                             qint64(o.value("created").toDouble())});
    }
    const int index = root.value("index").toInt(-1);
    if (index >= 0 && index < m_queue.size()) {
        // Comes back paused where it was; nothing decodes until Play.
        m_position = std::max(0.0, root.value("position").toDouble());
        advanceTo(index);
    }
    m_queueModel.reset();
    m_momentModel.reset();
}
