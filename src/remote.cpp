#include "remote.h"
#include "safety.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

#include "library.h"
#include "lyrics.h"
#include "player.h"
#include "playlists.h"

namespace {
const char *Path = "/org/mpris/MediaPlayer2";
const char *Service = "org.mpris.MediaPlayer2.halo";

QDBusObjectPath trackId(const QString &path) {
    // An MPRIS track id is a D-Bus object path, so it can only carry
    // [A-Za-z0-9_]; a hash of the file path is unique enough.
    return QDBusObjectPath(QStringLiteral("/org/halo/track/t") +
                           QString::number(qHash(path), 16));
}
}

// ---- MprisRoot ----

MprisRoot::MprisRoot(QObject *parent) : QDBusAbstractAdaptor(parent) {}

QStringList MprisRoot::mimeTypes() const {
    return {"audio/mpeg", "audio/flac", "audio/ogg", "audio/opus", "audio/mp4", "audio/aac", "audio/x-wav",
            "audio/x-ms-wma", "audio/x-aiff", "audio/x-ape", "audio/x-wavpack"};
}

void MprisRoot::Quit() { QCoreApplication::quit(); }

// ---- MprisPlayer ----

MprisPlayer::MprisPlayer(QObject *parent, Player *player) : QDBusAbstractAdaptor(parent), m_player(player) {
    connect(player, &Player::playingChanged, this, [this] { changed({"PlaybackStatus"}); });
    connect(player, &Player::currentChanged, this, [this] {
        refreshArt();
        changed({"Metadata", "CanGoNext", "CanGoPrevious", "CanPlay"});
    });
    connect(player, &Player::upNextChanged, this, [this] { changed({"CanGoNext"}); });
    connect(player, &Player::volumeChanged, this, [this] { changed({"Volume"}); });
    connect(player, &Player::settingsChanged, this, [this] { changed({"LoopStatus", "Shuffle"}); });
    connect(player, &Player::seeked, this, [this](double s) { emit Seeked(qlonglong(s * 1e6)); });
    refreshArt();
}

// The cover is cut and cached on a worker thread, then announced.
void MprisPlayer::refreshArt() {
    const QString id = m_player->albumId();
    if (id == m_artFor)
        return;
    m_artFor = id;
    m_artUrl.clear();
    if (id.isEmpty())
        return;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, id] {
        watcher->deleteLater();
        if (id != m_artFor)
            return;
        const QString file = watcher->result();
        m_artUrl = file.isEmpty() ? QString() : QUrl::fromLocalFile(file).toString();
        if (!m_artUrl.isEmpty())
            changed({"Metadata"});
    });
    watcher->setFuture(QtConcurrent::run([id] { return Covers::ensure(id, 512); }));
}

QString MprisPlayer::playbackStatus() const {
    if (!m_player->hasTrack())
        return "Stopped";
    return m_player->playing() ? "Playing" : "Paused";
}

QString MprisPlayer::loopStatus() const {
    switch (m_player->repeat()) {
    case Player::RepeatAll: return "Playlist";
    case Player::RepeatOne: return "Track";
    default: return "None";
    }
}

void MprisPlayer::setLoopStatus(const QString &status) {
    m_player->setRepeat(status == "Playlist" ? Player::RepeatAll : status == "Track" ? Player::RepeatOne : Player::RepeatOff);
}

bool MprisPlayer::shuffle() const { return m_player->shuffle(); }
void MprisPlayer::setShuffle(bool on) { m_player->setShuffle(on); }

QVariantMap MprisPlayer::metadataFor(const QString &path, const QString &title, const QString &artist,
                                     const QString &album, double duration, const QString &artUrl) {
    QVariantMap map;
    if (path.isEmpty()) {
        map.insert("mpris:trackid", QVariant::fromValue(QDBusObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack")));
        return map;
    }
    map.insert("mpris:trackid", QVariant::fromValue(trackId(path)));
    map.insert("mpris:length", qlonglong(duration * 1e6));
    map.insert("xesam:title", title);
    if (!artist.isEmpty())
        map.insert("xesam:artist", QStringList{artist});
    if (!album.isEmpty())
        map.insert("xesam:album", album);
    map.insert("xesam:url", QUrl::fromLocalFile(path).toString());
    if (!artUrl.isEmpty())
        map.insert("mpris:artUrl", artUrl);
    return map;
}

QVariantMap MprisPlayer::metadata() const {
    if (!m_player->hasTrack())
        return metadataFor({}, {}, {}, {}, 0, {});
    return metadataFor(m_player->currentPath(), m_player->title(), m_player->artist(), m_player->album(),
                       m_player->duration(), m_artUrl);
}

double MprisPlayer::volume() const { return m_player->volume(); }
void MprisPlayer::setVolume(double volume) { m_player->setVolume(volume); }
qlonglong MprisPlayer::position() const { return qlonglong(m_player->position() * 1e6); }
bool MprisPlayer::canGoNext() const { return m_player->hasTrack(); }
bool MprisPlayer::canGoPrevious() const { return m_player->hasTrack(); }
bool MprisPlayer::canPlay() const { return m_player->hasTrack() || m_player->queuePaths().size() > 0; }

void MprisPlayer::Next() { m_player->next(); }
void MprisPlayer::Previous() { m_player->previous(); }
void MprisPlayer::Pause() { m_player->pause(); }
void MprisPlayer::PlayPause() { m_player->togglePlay(); }
void MprisPlayer::Stop() {
    m_player->pause();
    m_player->seek(0);
}
void MprisPlayer::Play() { m_player->play(); }
void MprisPlayer::Seek(qlonglong offset) { m_player->seekBy(double(offset) / 1e6); }

void MprisPlayer::SetPosition(const QDBusObjectPath &track, qlonglong position) {
    // The spec says to ignore a stale track id.
    if (track.path() != trackId(m_player->currentPath()).path())
        return;
    m_player->seek(double(position) / 1e6);
}

void MprisPlayer::OpenUri(const QString &uri) {
    const QUrl url(uri);
    if (url.isLocalFile())
        m_player->playPaths({url.toLocalFile()});
}

void MprisPlayer::changed(const QStringList &names) {
    QVariantMap properties;
    for (const QString &name : names) {
        const int index = metaObject()->indexOfProperty(name.toLatin1().constData());
        if (index >= 0)
            properties.insert(name, metaObject()->property(index).read(this));
    }
    QDBusMessage signal = QDBusMessage::createSignal(Path, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    signal << QString("org.mpris.MediaPlayer2.Player") << properties << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

// ---- Remote ----

Remote::Remote(Player *player, QObject *parent) : QObject(parent), m_player(player) {
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                // One command is a short JSON line; a peer that sends more
                // without a newline is cut off rather than buffered forever.
                if (!socket->canReadLine() && socket->bytesAvailable() > Safety::MaxSocketLine) {
                    socket->abort();
                    return;
                }
                while (socket->canReadLine()) {
                    const QByteArray line = socket->readLine(Safety::MaxSocketLine + 1);
                    if (line.size() > Safety::MaxSocketLine) {
                        socket->abort();
                        return;
                    }
                    const QJsonObject request = QJsonDocument::fromJson(line).object();
                    socket->write(QJsonDocument(handle(request)).toJson(QJsonDocument::Compact) + "\n");
                    socket->flush();
                }
            });
        }
    });
}

Remote::~Remote() {
    QDBusConnection::sessionBus().unregisterService(Service);
}

QString Remote::socketName() {
    if (!qEnvironmentVariableIsEmpty("HALO_SOCKET"))
        return qEnvironmentVariable("HALO_SOCKET");
    // Qt hands back a private (0700, yours) runtime folder or nothing. With
    // nothing there is no socket: a fixed name in the shared /tmp could be
    // taken by another account first.
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return runtime.isEmpty() ? QString() : runtime + "/halo.sock";
}

bool Remote::send(const QJsonObject &request, QJsonObject *reply, int timeoutMs) {
    if (socketName().isEmpty())
        return false;
    QLocalSocket socket;
    socket.connectToServer(socketName());
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    socket.flush();
    while (!socket.canReadLine())
        if (!socket.waitForReadyRead(timeoutMs))
            return false;
    if (reply)
        *reply = QJsonDocument::fromJson(socket.readLine()).object();
    return true;
}

bool Remote::running(int timeoutMs) {
    // A connection is accepted by the kernel even while that Halo's main
    // thread is busy (starting a song, merging a scan), so this doesn't
    // mistake a slow Halo for none. Waiting for a reply to a ping did, and
    // the second launch then took the socket and played over the first.
    if (socketName().isEmpty())
        return false;
    QLocalSocket socket;
    socket.connectToServer(socketName());
    return socket.waitForConnected(timeoutMs);
}

bool Remote::listen() {
    // Another Halo listening means this one should hand over and exit.
    if (running())
        return false;
    // Nobody listening: a socket file left behind by a crash is stale.
    if (!socketName().isEmpty())
        QLocalServer::removeServer(socketName());
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (socketName().isEmpty()) {
        m_error = tr("Halo has no private runtime folder (XDG_RUNTIME_DIR), so `halo` commands from a terminal won't reach this window.");
    } else if (!m_server.listen(socketName())) {
        // Another Halo that started at the same moment won the socket.
        if (running(500))
            return false;
        // No other Halo: the socket itself can't be made (a path over the
        // 108-byte limit, a folder that isn't writable). Keep running and
        // say so; quitting silently looked like Halo never opened.
        m_error = tr("Halo couldn't open its control socket %1 (%2), so `halo` commands from a terminal won't reach this window.")
                      .arg(socketName(), m_server.errorString());
    }

    auto *object = new QObject(this);
    m_root = new MprisRoot(object);
    m_mpris = new MprisPlayer(object, m_player);
    connect(m_root, &MprisRoot::raiseRequested, this, &Remote::raiseRequested);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        bus.registerObject(Path, object);
        bus.registerService(Service);
    }
    return true;
}

QJsonObject Remote::handle(const QJsonObject &request) {
    const QString cmd = request.value("cmd").toString();
    QStringList args;
    for (const auto &v : request.value("args").toArray())
        args << v.toString();
    auto ok = [this] { return QJsonObject{{"ok", true}, {"status", m_player->status()}}; };

    if (cmd == "ping")
        return {{"ok", true}};
    if (cmd == "playlist")
        return playlistCommand(args);
    if (cmd == "lyrics") {
        if (!m_lyrics)
            return {{"ok", false}, {"error", "lyrics aren't available"}};
        const QString what = args.value(0);
        if (what == "online" && args.size() == 2 && (args[1] == "on" || args[1] == "off"))
            m_lyrics->setOnline(args[1] == "on");
        else if (what == "retry")
            m_lyrics->retry();
        else if (what == "offset" && args.size() == 2) {
            bool number = false;
            const double seconds = args[1].toDouble(&number);
            if (args[1] == "reset")
                m_lyrics->resetOffset();
            else if (number && std::abs(seconds) <= 30)
                m_lyrics->nudge(seconds);
            else
                return {{"ok", false}, {"error", "offset takes seconds (-30 to 30, like +0.5) or reset"}};
        } else if (!what.isEmpty())
            return {{"ok", false}, {"error", "lyrics takes: online <on|off>, retry, offset <seconds|reset>"}};
        return {{"ok", true}, {"lyrics", m_lyrics->toJson()}};
    }
    if (cmd == "status")
        return ok();
    if (cmd == "play") {
        m_player->play();
        return ok();
    }
    if (cmd == "pause") {
        m_player->pause();
        return ok();
    }
    if (cmd == "toggle") {
        m_player->togglePlay();
        return ok();
    }
    if (cmd == "next") {
        m_player->next();
        return ok();
    }
    if (cmd == "previous") {
        m_player->previous();
        return ok();
    }
    if (cmd == "seek" && !args.isEmpty()) {
        const QString a = args.first();
        bool valid = false;
        const double value = a.mid(a.startsWith('+') || a.startsWith('-') ? 1 : 0).toDouble(&valid);
        if (!valid)
            return {{"ok", false}, {"error", "seek takes seconds, like 90, +10 or -10"}};
        if (a.startsWith('+'))
            m_player->seekBy(value);
        else if (a.startsWith('-'))
            m_player->seekBy(-value);
        else
            m_player->seek(value);
        return ok();
    }
    if (cmd == "volume" && !args.isEmpty()) {
        bool valid = false;
        const int value = args.first().toInt(&valid);
        if (!valid || value < 0 || value > 100)
            return {{"ok", false}, {"error", "volume takes 0 to 100"}};
        m_player->setVolume(value / 100.0);
        return ok();
    }
    if (cmd == "add" || cmd == "open" || cmd == "playnext") {
        const QStringList files = Library::expand(args);
        if (files.isEmpty() && cmd != "open")
            return {{"ok", false}, {"error", "no audio files in what was given"}};
        if (cmd == "add")
            m_player->enqueue(files);
        else if (cmd == "playnext")
            m_player->playNext(files);
        else if (!files.isEmpty())
            m_player->playPaths(files);
        if (cmd == "open")
            emit raiseRequested();
        return ok();
    }
    if (cmd == "queue") {
        QJsonArray items;
        const QStringList &queue = m_player->queuePaths();
        for (int i = 0; i < queue.size(); ++i) {
            const Track *t = m_player->library()->track(queue[i]);
            items.append(QJsonObject{{"index", i},
                                     {"path", queue[i]},
                                     {"title", t ? t->title : Library::titleFromFileName(queue[i])},
                                     {"artist", t ? t->artist : QString()},
                                     {"current", i == m_player->currentIndex()},
                                     {"drift", m_player->isDrift(i)}});
        }
        return {{"ok", true}, {"queue", items}};
    }
    if (cmd == "clear") {
        m_player->clearQueue();
        return ok();
    }
    if (cmd == "moment") {
        m_player->addMoment();
        return ok();
    }
    if ((cmd == "blend" || cmd == "drift" || cmd == "shuffle" || cmd == "normalize") && !args.isEmpty()) {
        const QString v = args.first();
        if (v != "on" && v != "off")
            return {{"ok", false}, {"error", cmd + " takes on or off"}};
        const bool on = v == "on";
        if (cmd == "blend")
            m_player->setBlend(on);
        else if (cmd == "drift")
            m_player->setDrift(on);
        else if (cmd == "shuffle")
            m_player->setShuffle(on);
        else
            m_player->setNormalize(on);
        return ok();
    }
    if (cmd == "repeat" && !args.isEmpty()) {
        const QString v = args.first();
        if (v != "off" && v != "all" && v != "one")
            return {{"ok", false}, {"error", "repeat takes off, all or one"}};
        m_player->setRepeat(v == "all" ? Player::RepeatAll : v == "one" ? Player::RepeatOne : Player::RepeatOff);
        return ok();
    }
    if (cmd == "sleep" && !args.isEmpty()) {
        if (args.first() == "off") {
            m_player->cancelSleep();
            return ok();
        }
        bool valid = false;
        const int minutes = args.first().toInt(&valid);
        if (!valid || minutes <= 0 || minutes > 720)
            return {{"ok", false}, {"error", "sleep takes minutes (1 to 720) or off"}};
        m_player->startSleep(minutes);
        return ok();
    }
    if (cmd == "grab" && args.size() == 1 && args.first().endsWith(".png")) {
        // Writes a new file only: it never replaces one that exists.
        if (QFileInfo::exists(args.first()))
            return {{"ok", false}, {"error", "grab won't overwrite an existing file"}};
        emit grabRequested(args.first());
        return {{"ok", true}};
    }
    if (cmd == "visual") {
        static const QStringList names{"halo", "bars", "kaleidoscope", "skyline", "phosphor",
                                       "pulsar", "waterfall", "warp", "aurora", "hologram", "turntable"};
        if (!args.isEmpty()) {
            const QString v = args.first().toLower();
            bool number = false;
            const int n = v.toInt(&number);
            const int index = v == "next" ? (m_player->visualStyle() + 1) % int(names.size())
                            : v == "previous" ? (m_player->visualStyle() + int(names.size()) - 1) % int(names.size())
                            : number ? n : int(names.indexOf(v));
            if (index < 0 || index >= int(names.size()))
                return {{"ok", false}, {"error", "visual takes next, previous, 0 to 9 or a name: " + names.join(", ")}};
            m_player->setVisualStyle(index);
        }
        return {{"ok", true}, {"visual", names.value(m_player->visualStyle())}};
    }
    if (cmd == "ui" && args.size() == 2) {
        emit uiRequested(args[0], args[1]);
        return {{"ok", true}};
    }
    if (cmd == "rescan") {
        m_player->library()->rescan();
        return ok();
    }
    return {{"ok", false}, {"error", QString("unknown command %1").arg(cmd)}};
}

// `halo playlist ...`: the same playlists the window shows, by name or id.
QJsonObject Remote::playlistCommand(const QStringList &args) {
    if (!m_playlists)
        return {{"ok", false}, {"error", "playlists aren't available"}};
    const QString sub = args.value(0, "list");
    auto fail = [](const QString &e) { return QJsonObject{{"ok", false}, {"error", e}}; };
    auto found = [&](const QString &name) -> const Playlist * {
        return name.isEmpty() ? nullptr : m_playlists->lookup(name);
    };
    auto songs = [&](const QStringList &paths) {
        QJsonArray items;
        for (const QString &p : paths) {
            const Track *t = m_player->library()->track(p);
            items.append(QJsonObject{{"path", p},
                                     {"title", t ? t->title : Library::titleFromFileName(QFileInfo(p).fileName())},
                                     {"artist", t ? t->artist : QString()},
                                     {"missing", !t && !QFileInfo::exists(p)}});
        }
        return items;
    };
    auto summary = [&](const QString &id) { return QJsonObject::fromVariantMap(m_playlists->info(id)); };

    if (sub == "list") {
        QJsonArray items;
        for (const QVariant &v : m_playlists->all())
            items.append(QJsonObject::fromVariantMap(v.toMap()));
        return {{"ok", true}, {"playlists", items}};
    }
    if (sub == "new" || sub == "save") {
        const QString name = args.value(1);
        if (Playlists::cleanName(name).isEmpty())
            return fail("give the playlist a name");
        const QStringList paths = sub == "save" ? m_player->queuePaths() : Library::expand(args.mid(2));
        const QString id = m_playlists->create(name, paths);
        if (id.isEmpty())
            return fail("couldn't create the playlist");
        return {{"ok", true}, {"playlist", summary(id)}};
    }
    if (sub == "smart") {
        // halo playlist smart NAME [any] "plays greater 3" "added inLast 30" ...
        const QString name = args.value(1);
        if (Playlists::cleanName(name).isEmpty())
            return fail("give the playlist a name");
        QVariantList rules;
        bool matchAll = true;
        for (const QString &a : args.mid(2)) {
            if (a == "any") {
                matchAll = false;
                continue;
            }
            const QStringList parts = a.split(' ', Qt::SkipEmptyParts);
            if (parts.size() < 2)
                return fail(QString("a rule is \"field op value\", not \"%1\"").arg(a));
            rules << QVariantMap{{"field", parts[0]}, {"op", parts[1]}, {"value", parts.mid(2).join(' ')}};
        }
        const QVariantMap spec{{"matchAll", matchAll}, {"rules", rules}};
        if (Playlists::fromSpec(spec).rules.size() != size_t(rules.size()))
            return fail("a rule isn't understood; fields: title artist album genre folder year length plays bpm "
                        "loudness added played; ops: contains notContains is isNot startsWith greater less inLast "
                        "notInLast never");
        const QString id = m_playlists->createSmart(name, spec);
        if (id.isEmpty())
            return fail("couldn't create the playlist");
        return {{"ok", true}, {"playlist", summary(id)}};
    }
    if (sub == "restore") {
        const QString id = m_playlists->restoreLast();
        if (id.isEmpty())
            return fail("nothing to restore");
        return {{"ok", true}, {"playlist", summary(id)}};
    }
    if (sub == "import") {
        const QString id = m_playlists->importFile(args.value(1));
        if (id.isEmpty())
            return fail("couldn't read that playlist file");
        return {{"ok", true}, {"playlist", summary(id)}};
    }

    const Playlist *p = found(args.value(1));
    if (!p)
        return fail(args.value(1).isEmpty() ? "name a playlist" : QString("no playlist called \"%1\"").arg(args.value(1)));
    const QString id = p->id;

    if (sub == "show")
        return {{"ok", true}, {"playlist", summary(id)}, {"songs", songs(m_playlists->paths(id))}};
    if (sub == "play" || sub == "shuffle" || sub == "queue") {
        const QStringList paths = m_playlists->paths(id);
        if (paths.isEmpty())
            return fail("the playlist is empty");
        if (sub == "queue")
            m_player->enqueue(paths);
        else {
            m_player->setShuffle(sub == "shuffle");
            m_player->playPaths(paths, 0);
        }
        return {{"ok", true}, {"status", m_player->status()}};
    }
    if (sub == "add" || sub == "addcurrent") {
        if (p->smart)
            return fail("a smart playlist fills itself from its rules");
        const QStringList paths = sub == "addcurrent" ? QStringList{m_player->currentPath()} : Library::expand(args.mid(2));
        if (paths.isEmpty() || paths.first().isEmpty())
            return fail(sub == "addcurrent" ? "nothing is playing" : "no audio files in what was given");
        const int added = m_playlists->add(id, paths);
        return {{"ok", true}, {"added", added}, {"skipped", int(paths.size()) - added}, {"playlist", summary(id)}};
    }
    if (sub == "rename") {
        if (!m_playlists->rename(id, args.value(2)))
            return fail("give the new name");
        return {{"ok", true}, {"playlist", summary(id)}};
    }
    if (sub == "delete" || sub == "remove") {
        if (!m_playlists->remove(id))
            return fail("couldn't delete the playlist");
        return {{"ok", true}, {"deleted", id}};
    }
    if (sub == "export") {
        if (!m_playlists->exportFile(id, args.value(2)))
            return fail("couldn't write that file");
        return {{"ok", true}, {"file", args.value(2)}};
    }
    if (sub == "suggest") {
        const int n = std::clamp(args.value(2, "10").toInt(), 1, 100);
        return {{"ok", true}, {"songs", songs(m_playlists->suggest(id, n))}};
    }
    return fail(QString("unknown playlist command \"%1\"").arg(sub));
}
