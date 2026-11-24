#include "library.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickTextureFactory>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThreadPool>
#include <QtConcurrent>
#include <QUrl>
#include <algorithm>
#include <QDateTime>
#include <cmath>
#include <random>

#include <fileref.h>
#include <tag.h>
#include <tdebuglistener.h>
#include <tpropertymap.h>
#include <tvariant.h>

namespace {

// Mixtape tags are often broken; TagLib copes and says so on stderr for
// every file. Nobody can act on that, so it is kept quiet.
class QuietTagLib : public TagLib::DebugListener {
public:
    QuietTagLib() { TagLib::setDebugListener(this); }
    void printMessage(const TagLib::String &) override {}
};
QuietTagLib quietTagLib;

QString fromTag(const TagLib::String &s) { return QString::fromStdString(s.to8Bit(true)).trimmed(); }

int leadingNumber(const QString &s) {
    static const QRegularExpression number("^\\s*(\\d+)");
    const auto m = number.match(s);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

QString albumIdFor(const QString &folder) {
    return QString::fromLatin1(QCryptographicHash::hash(folder.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}

QString mostCommon(const QStringList &values, double share = 0) {
    QHash<QString, int> counts;
    for (const QString &v : values)
        if (!v.isEmpty())
            ++counts[v];
    QString best;
    int top = 0;
    for (auto it = counts.begin(); it != counts.end(); ++it)
        if (it.value() > top || (it.value() == top && it.key() < best)) {
            top = it.value();
            best = it.key();
        }
    if (share > 0 && top < share * values.size())
        return {};
    return best;
}

const QStringList &audioSuffixes() {
    static const QStringList suffixes{"mp3", "flac", "ogg", "oga", "opus", "m4a", "m4b", "aac", "alac", "wav",
                                      "wma", "aif", "aiff", "ape", "wv", "mpc", "dsf", "mka", "webm"};
    return suffixes;
}

const QStringList &coverNames() {
    static const QStringList names{"cover", "folder", "front", "album", "albumart", "art", "artwork"};
    return names;
}

QString coverInFolder(const QString &folder) {
    const QFileInfoList images =
        QDir(folder).entryInfoList({"*.jpg", "*.jpeg", "*.png", "*.webp", "*.JPG", "*.JPEG", "*.PNG"}, QDir::Files,
                                   QDir::Name);
    if (images.isEmpty())
        return {};
    for (const QString &name : coverNames())
        for (const QFileInfo &image : images)
            if (image.completeBaseName().compare(name, Qt::CaseInsensitive) == 0 ||
                image.completeBaseName().startsWith(name + " ", Qt::CaseInsensitive))
                return image.absoluteFilePath();
    // Mixtape folders often carry one scan named after the tape.
    return images.first().absoluteFilePath();
}

QString searchText(const Track &t) {
    return (t.title + " " + t.artist + " " + t.album + " " + t.albumArtist + " " + t.genre + " " +
            QFileInfo(t.path).dir().dirName())
        .toLower();
}

QJsonArray toJson(const Track &t) {
    return {t.path, t.title, t.artist, t.album, t.albumArtist, t.genre, t.number, t.disc,
            t.year, t.duration, double(t.mtime), double(t.size), t.embeddedArt};
}

Track fromJson(const QJsonArray &a) {
    Track t;
    t.path = a.at(0).toString();
    t.title = a.at(1).toString();
    t.artist = a.at(2).toString();
    t.album = a.at(3).toString();
    t.albumArtist = a.at(4).toString();
    t.genre = a.at(5).toString();
    t.number = a.at(6).toInt();
    t.disc = a.at(7).toInt();
    t.year = a.at(8).toInt();
    t.duration = a.at(9).toDouble();
    t.mtime = qint64(a.at(10).toDouble());
    t.size = qint64(a.at(11).toDouble());
    t.embeddedArt = a.at(12).toBool();
    return t;
}

// Album cover sources, readable from the image provider's worker threads.
QMutex coverLock;
QHash<QString, Covers::Source> coverSources;

QString coverCacheDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/halo/covers";
}

}

namespace Covers {

void setSources(QHash<QString, Source> sources) {
    QMutexLocker lock(&coverLock);
    coverSources = std::move(sources);
}

QImage load(const QString &albumId, int size) {
    Source source;
    {
        QMutexLocker lock(&coverLock);
        const auto it = coverSources.constFind(albumId);
        if (it == coverSources.constEnd())
            return {};
        source = it.value();
    }
    const QString cached = QString("%1/%2-%3.jpg").arg(coverCacheDirectory(), albumId).arg(size);
    QImage image;
    if (QFileInfo::exists(cached) && image.load(cached))
        return image;

    if (!source.file.isEmpty()) {
        QImageReader reader(source.file);
        reader.setAutoTransform(true);
        image = reader.read();
    }
    if (image.isNull() && !source.track.isEmpty()) {
        TagLib::FileRef file(QFile::encodeName(source.track).constData(), false);
        if (!file.isNull()) {
            const auto pictures = file.complexProperties("PICTURE");
            if (!pictures.isEmpty()) {
                const auto data = pictures.front().value("data").toByteVector();
                image.loadFromData(reinterpret_cast<const uchar *>(data.data()), int(data.size()));
            }
        }
    }
    if (image.isNull())
        return {};
    // Square, centred, scaled once and cached, so a grid of a thousand
    // albums scrolls without decoding full-size scans.
    const int side = std::min(image.width(), image.height());
    image = image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side)
                .scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGB32);
    QDir().mkpath(coverCacheDirectory());
    QSaveFile out(cached);
    if (out.open(QIODevice::WriteOnly) && image.save(&out, "JPG", 88))
        out.commit();
    return image;
}

QString ensure(const QString &albumId, int size) {
    const QString cached = QString("%1/%2-%3.jpg").arg(coverCacheDirectory(), albumId).arg(size);
    if (QFileInfo::exists(cached))
        return cached;
    return load(albumId, size).isNull() ? QString() : cached;
}

}

Library::Library(const QStringList &folders, const QString &cacheDirectory, const QString &stateDirectory,
                 QObject *parent)
    : QObject(parent), m_folders(folders.isEmpty() ? defaultFolders() : folders) {
    const QString cache = cacheDirectory.isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/halo"
                              : cacheDirectory;
    const QString state = stateDirectory.isEmpty()
                              ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/halo"
                              : stateDirectory;
    m_cacheFile = cache + "/library.json";
    m_statsFile = state + "/stats.json";
    m_progress.setInterval(250);
    connect(&m_progress, &QTimer::timeout, this, &Library::scanProgress);
    connect(&m_watcher, &QFutureWatcher<std::vector<Track>>::finished, this, [this] {
        m_progress.stop();
        adopt(m_watcher.result());
        saveCache();
        m_scanning = false;
        emit scanningChanged();
        // A folder added mid-scan gets its own pass.
        if (m_rescanAgain) {
            m_rescanAgain = false;
            rescan();
        }
    });
    m_statsSave.setSingleShot(true);
    m_statsSave.setInterval(2000);
    connect(&m_statsSave, &QTimer::timeout, this, &Library::saveStats);
    loadStats();
}

Library::~Library() {
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
    flush();
}

QString Library::foldersFile() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/halo/folders";
}

// The folders listed one per line in ~/.config/halo/folders; without that
// file, the desktop's music folder (XDG_MUSIC_DIR, which Qt reads from
// user-dirs.dirs) and ~/Music, the ones that exist.
QStringList Library::defaultFolders() {
    QStringList folders;
    if (!qEnvironmentVariableIsEmpty("HALO_MUSIC"))
        return qEnvironmentVariable("HALO_MUSIC").split(':', Qt::SkipEmptyParts);
    QFile config(foldersFile());
    if (config.open(QIODevice::ReadOnly))
        for (const QString &line : QString::fromUtf8(config.readAll()).split('\n')) {
            QString folder = line.trimmed();
            if (folder.startsWith("~/"))
                folder = QDir::homePath() + folder.mid(1);
            if (!folder.isEmpty() && !folder.startsWith('#'))
                folders << QDir::cleanPath(folder);
        }
    if (!folders.isEmpty())
        return folders;
    const QString xdg = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    const QString home = QDir::cleanPath(QDir::homePath() + "/Music");
    for (const QString &candidate : {xdg, home})
        if (!folders.contains(candidate) && QFileInfo(candidate).isDir())
            folders << candidate;
    if (folders.isEmpty())
        folders << xdg;
    return folders;
}

bool Library::addFolder(const QString &folder) {
    QString path = folder.startsWith("file:") ? QUrl(folder).toLocalFile() : folder;
    if (path.startsWith("~/"))
        path = QDir::homePath() + path.mid(1);
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return false;
    path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!m_folders.contains(path)) {
        // The first folder chosen by hand keeps the ones Halo found, so
        // nothing that was in the library drops out of it.
        const QString listFile = foldersFile();
        const bool fresh = !QFileInfo::exists(listFile);
        QByteArray text;
        if (fresh) {
            for (const QString &f : std::as_const(m_folders))
                if (QFileInfo(f).isDir() && f != path)
                    text += f.toUtf8() + '\n';
        } else {
            // Start on a new line even if the file doesn't end with one.
            QFile peek(listFile);
            if (peek.open(QIODevice::ReadOnly) && peek.size() > 0 && peek.seek(peek.size() - 1) && peek.read(1) != "\n")
                text += '\n';
        }
        text += path.toUtf8() + '\n';
        QDir().mkpath(QFileInfo(listFile).absolutePath());
        QFile file(listFile);
        if (!file.open(fresh ? QIODevice::WriteOnly : QIODevice::Append) || file.write(text) != text.size())
            return false;
        file.close();
        m_folders << path;
        emit changed();
    }
    if (m_scanning)
        m_rescanAgain = true;
    else
        rescan();
    return true;
}

bool Library::isAudioFile(const QString &path) {
    return audioSuffixes().contains(QFileInfo(path).suffix().toLower());
}

// Mixtape tags often repeat the track number and the artist in the title
// ("05 Young Dolph - Goin' In"); the list already shows both.
QString Library::cleanTitle(const QString &title, const QString &artist) {
    // A number is only a track number when punctuation or the artist follows
    // it, so "21 Savage" and "99 Problems" keep theirs.
    QString t = title;
    static const QRegularExpression numbered("^\\s*(\\d{1,3})(\\s*[-._)]+\\s*|\\s+)(.*)$");
    const auto m = numbered.match(t);
    if (m.hasMatch()) {
        const QString rest = m.captured(3);
        const bool punctuated = m.captured(2).trimmed().size() > 0;
        if (punctuated || (!artist.isEmpty() && rest.startsWith(artist + " - ", Qt::CaseInsensitive)))
            t = rest;
    }
    if (!artist.isEmpty() && t.startsWith(artist + " - ", Qt::CaseInsensitive) && t.size() > artist.size() + 3)
        t = t.mid(artist.size() + 3);
    t = t.trimmed();
    return t.isEmpty() ? title : t;
}

// DJ tapes tag every track with the host as the artist and put the real
// credit in the title: "10 Mack Maine - Young Mulah Baby". When the leading
// number is the track's own number, the title is "<number> <artist> - <song>",
// so the song becomes the title and the performer the artist; the host stays
// as the album artist so the tape still groups and reads as theirs.
void Library::splitCredit(Track &t) {
    // The number may be followed by punctuation ("01 - Artist - Song"); the
    // performer starts with a letter or digit, never the separator.
    static const QRegularExpression credited("^\\s*(\\d{1,3})\\s*[-._)]*\\s+([\\p{L}\\p{N}].*?)\\s+-\\s+(\\S.*)$");
    const auto m = credited.match(t.title);
    if (!m.hasMatch() || t.number <= 0 || m.captured(1).toInt() != t.number)
        return;
    const QString performer = m.captured(2).trimmed(), song = m.captured(3).trimmed();
    if (performer.compare(t.artist, Qt::CaseInsensitive) == 0)
        return; // cleanTitle already handles the artist repeating itself
    if (t.albumArtist.isEmpty())
        t.albumArtist = t.artist;
    t.artist = performer;
    t.title = song;
}

// "01 - Artist - Song [Prod. X].mp3" reads as "Artist - Song [Prod. X]".
QString Library::titleFromFileName(const QString &fileName) {
    QString name = QFileInfo(fileName).completeBaseName();
    name.replace('_', ' ');
    static const QRegularExpression number("^\\s*\\d{1,3}\\s*[-._)]*\\s+");
    name.remove(number);
    return name.simplified();
}

// "Young_Dolph_-_16_Zips-2015" reads as "Young Dolph - 16 Zips (2015)", and
// a web address made into a folder, "lil-wayne-tear-drop-tune-part-3-2010",
// as "Lil Wayne Tear Drop Tune Part 3 (2010)".
QString Library::prettyFolderName(const QString &folder) {
    QString name = QFileInfo(folder).fileName();
    // An archive's own date stamp after the name: "..._202511".
    static const QRegularExpression stamp("_20\\d{2}(0[1-9]|1[0-2])$");
    name.remove(stamp);
    // With no space or underscore anywhere, the hyphens are the spaces.
    const bool slug = !name.contains(' ') && !name.contains('_') && name.count('-') >= 2;
    name.replace('_', ' ');
    static const QRegularExpression scene("-(\\d{4})(-[A-Za-z0-9]+)?$");
    name.replace(scene, " (\\1)");
    if (slug) {
        name.replace('-', ' ');
    } else {
        static const QRegularExpression dashes("\\s*-\\s*");
        name.replace(dashes, " - ");
    }
    return capitalised(name.simplified());
}

// A name written all in small letters gets a capital on each word ("lil
// wayne" reads as "Lil Wayne", "dj" as "DJ"); any other name is left as its
// writer had it.
QString Library::capitalised(const QString &name) {
    for (const QChar c : name)
        if (c.isUpper())
            return name;
    QStringList words = name.split(' ', Qt::SkipEmptyParts);
    for (QString &w : words) {
        if (w == "dj" || w == "mc" || w == "ii" || w == "iii" || w == "iv") {
            w = w.toUpper();
            continue;
        }
        for (int i = 0; i < w.size(); ++i)
            if (w[i].isLetter()) {
                w[i] = w[i].toUpper();
                break;
            } else if (w[i].isDigit()) {
                break; // "2pac", "50cent": a word that starts with a number stays
            }
    }
    return words.join(' ');
}

Track Library::readTrack(const QString &path) {
    Track t;
    t.path = path;
    const QFileInfo info(path);
    t.mtime = info.lastModified().toMSecsSinceEpoch();
    t.size = info.size();
    TagLib::FileRef file(QFile::encodeName(path).constData(), true, TagLib::AudioProperties::Fast);
    if (!file.isNull()) {
        if (TagLib::Tag *tag = file.tag()) {
            t.title = fromTag(tag->title());
            t.artist = fromTag(tag->artist());
            t.album = fromTag(tag->album());
            t.genre = fromTag(tag->genre());
            t.year = int(tag->year());
            t.number = int(tag->track());
        }
        const TagLib::PropertyMap properties = file.properties();
        if (properties.contains("ALBUMARTIST") && !properties["ALBUMARTIST"].isEmpty())
            t.albumArtist = fromTag(properties["ALBUMARTIST"].front());
        if (properties.contains("DISCNUMBER") && !properties["DISCNUMBER"].isEmpty())
            t.disc = leadingNumber(fromTag(properties["DISCNUMBER"].front()));
        if (const auto *audio = file.audioProperties())
            t.duration = audio->lengthInMilliseconds() / 1000.0;
        t.embeddedArt = file.complexPropertyKeys().contains("PICTURE");
    }
    if (t.title.isEmpty())
        t.title = titleFromFileName(path);
    if (t.number == 0)
        t.number = leadingNumber(info.fileName());
    return t;
}

std::vector<Track> Library::scanFolders(const QStringList &folders, const QHash<QString, Track> &previous,
                                        std::atomic<int> *counter) {
    std::vector<Track> tracks;
    QSet<QString> seen;
    QHash<QString, bool> ignoredDirectories;
    for (const QString &folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!isAudioFile(path) || seen.contains(path))
                continue;
            // Hidden folders (a .trash, a sync tool's staging area) are skipped,
            // and so is a folder holding a .haloignore or .nomedia file.
            if (path.mid(folder.size()).contains("/."))
                continue;
            const QString directory = QFileInfo(path).absolutePath();
            auto ignored = ignoredDirectories.constFind(directory);
            if (ignored == ignoredDirectories.constEnd())
                ignored = ignoredDirectories.insert(directory, QFileInfo::exists(directory + "/.haloignore") ||
                                                                   QFileInfo::exists(directory + "/.nomedia"));
            if (ignored.value())
                continue;
            seen.insert(path);
            const QFileInfo info = it.fileInfo();
            const auto old = previous.constFind(path);
            if (old != previous.constEnd() && old->mtime == info.lastModified().toMSecsSinceEpoch() &&
                old->size == info.size())
                tracks.push_back(*old);
            else
                tracks.push_back(readTrack(path));
            if (counter)
                counter->fetch_add(1);
        }
    }
    return tracks;
}

std::vector<Album> Library::buildAlbums(std::vector<Track> &tracks) {
    static const QRegularExpression discFolder("^(cd|disc|disk)\\s*\\d+", QRegularExpression::CaseInsensitiveOption);
    QHash<QString, int> byFolder;
    std::vector<Album> albums;
    for (int i = 0; i < int(tracks.size()); ++i) {
        QString folder = QFileInfo(tracks[i].path).absolutePath();
        if (discFolder.match(QFileInfo(folder).fileName()).hasMatch())
            folder = QFileInfo(folder).absolutePath();
        auto it = byFolder.find(folder);
        if (it == byFolder.end()) {
            Album album;
            album.folder = folder;
            album.id = albumIdFor(folder);
            albums.push_back(album);
            it = byFolder.insert(folder, int(albums.size()) - 1);
        }
        albums[size_t(it.value())].tracks.push_back(i);
    }
    for (Album &album : albums) {
        QStringList titles, artists, albumArtists;
        for (int i : album.tracks) {
            const Track &t = tracks[size_t(i)];
            titles << t.album;
            artists << t.artist;
            albumArtists << t.albumArtist;
            album.duration += t.duration;
            album.year = std::max(album.year, t.year);
            album.added = std::max(album.added, t.mtime);
            if (album.coverTrack.isEmpty() && t.embeddedArt)
                album.coverTrack = t.path;
        }
        album.title = mostCommon(titles, 0.5);
        if (album.title.isEmpty())
            album.title = prettyFolderName(album.folder);
        album.artist = mostCommon(albumArtists, 0.5);
        if (album.artist.isEmpty())
            album.artist = mostCommon(artists, 0.6);
        if (album.artist.isEmpty())
            album.artist = artists.isEmpty() || mostCommon(artists).isEmpty() ? QString() : QStringLiteral("*");
        album.coverFile = coverInFolder(album.folder);
        std::sort(album.tracks.begin(), album.tracks.end(), [&](int a, int b) {
            const Track &x = tracks[size_t(a)], &y = tracks[size_t(b)];
            if (x.disc != y.disc)
                return x.disc < y.disc;
            if (x.number != y.number)
                return x.number < y.number;
            return QString::compare(x.path, y.path, Qt::CaseInsensitive) < 0;
        });
        for (int i : album.tracks)
            tracks[size_t(i)].albumId = album.id;
    }
    return albums;
}

void Library::adopt(std::vector<Track> all) {
    // Sample packs and one-shots (a drum machine's kit, a DAW's exports)
    // often live under ~/Music too. Anything shorter than ten seconds is a
    // sound, not a song, and stays out of the library; it still plays when
    // opened or dropped. The scan cache keeps them, so they aren't re-read.
    m_all = std::move(all);
    std::vector<Track> tracks;
    tracks.reserve(m_all.size());
    for (const Track &t : m_all)
        if (t.duration <= 0 || t.duration >= MinimumSeconds)
            tracks.push_back(t);
    for (Track &t : tracks) {
        splitCredit(t);
        t.title = cleanTitle(t.title, t.artist);
        t.search = searchText(t);
    }
    m_albums = buildAlbums(tracks);
    m_tracks = std::move(tracks);
    m_byPath.clear();
    m_byAlbum.clear();
    for (int i = 0; i < int(m_tracks.size()); ++i)
        m_byPath.insert(m_tracks[size_t(i)].path, i);
    QHash<QString, Covers::Source> sources;
    for (int i = 0; i < int(m_albums.size()); ++i) {
        m_byAlbum.insert(m_albums[size_t(i)].id, i);
        sources.insert(m_albums[size_t(i)].id, {m_albums[size_t(i)].coverFile, m_albums[size_t(i)].coverTrack});
    }
    Covers::setSources(std::move(sources));
    emit changed();
}

void Library::rescan() {
    if (m_scanning)
        return;
    m_scanning = true;
    m_counter = 0;
    emit scanningChanged();
    m_progress.start();
    QHash<QString, Track> previous;
    for (const Track &t : m_all)
        previous.insert(t.path, t);
    const QStringList folders = m_folders;
    std::atomic<int> *counter = &m_counter;
    m_watcher.setFuture(QtConcurrent::run([folders, previous, counter] {
        return scanFolders(folders, previous, counter);
    }));
}

void Library::loadCache() {
    QFile file(m_cacheFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("version").toInt() != 1)
        return;
    std::vector<Track> tracks;
    const QJsonArray array = root.value("tracks").toArray();
    tracks.reserve(size_t(array.size()));
    for (const auto &value : array)
        tracks.push_back(fromJson(value.toArray()));
    adopt(std::move(tracks));
}

void Library::saveCache() {
    QJsonArray array;
    for (const Track &t : m_all)
        array.append(toJson(t));
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile file(m_cacheFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"tracks", array}}).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

const Track *Library::track(const QString &path) const {
    const auto it = m_byPath.constFind(path);
    return it == m_byPath.constEnd() ? nullptr : &m_tracks[size_t(it.value())];
}

const Album *Library::album(const QString &id) const {
    const auto it = m_byAlbum.constFind(id);
    return it == m_byAlbum.constEnd() ? nullptr : &m_albums[size_t(it.value())];
}

Track Library::describe(const QString &path) const {
    if (const Track *t = track(path))
        return *t;
    Track t = readTrack(path);
    splitCredit(t);
    t.title = cleanTitle(t.title, t.artist);
    return t;
}

QStringList Library::albumPaths(const QString &id) const {
    QStringList paths;
    if (const Album *a = album(id))
        for (int i : a->tracks)
            paths << m_tracks[size_t(i)].path;
    return paths;
}

QVariantMap Library::albumInfo(const QString &id) const {
    const Album *a = album(id);
    if (!a)
        return {};
    return {{"title", a->title}, {"artist", a->artist}, {"year", a->year}, {"folder", a->folder}};
}

QStringList Library::expand(const QStringList &paths) {
    QStringList files;
    for (const QString &arg : paths) {
        const QFileInfo info(arg);
        if (info.isDir()) {
            QStringList found;
            QDirIterator it(info.absoluteFilePath(), QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString p = it.next();
                if (isAudioFile(p))
                    found << p;
            }
            std::sort(found.begin(), found.end(),
                      [](const QString &a, const QString &b) { return QString::compare(a, b, Qt::CaseInsensitive) < 0; });
            files += found;
        } else if (info.isFile() && isAudioFile(arg)) {
            files << info.absoluteFilePath();
        }
    }
    return files;
}

void Library::recordPlay(const QString &path) {
    ++m_plays[path];
    m_lastPlayed[path] = QDateTime::currentSecsSinceEpoch();
    m_statsSave.start();
}

std::vector<int> Library::lostAndFound(qint64 now, int count) const {
    return pickLostAndFound(m_tracks, m_plays, m_lastPlayed, now, count);
}

std::vector<int> Library::pickLostAndFound(const std::vector<Track> &tracks, const QHash<QString, int> &plays,
                                           const QHash<QString, qint64> &lastPlayed, qint64 now, int count) {
    constexpr qint64 Day = 86400;
    // The same pick all day; a new one at midnight UTC.
    std::mt19937 rng(quint32(now / Day) * 2654435761u);
    struct Scored { int index; double score; };
    std::vector<Scored> loved, unheard, fresh;
    for (int i = 0; i < int(tracks.size()); ++i) {
        const Track &t = tracks[size_t(i)];
        const int n = plays.value(t.path);
        const qint64 last = lastPlayed.value(t.path);
        if (n >= 2 && last > 0 && now - last >= 45 * Day) {
            const double away = double(now - last) / Day;
            loved.push_back({i, n * std::log1p(away / 45.0)});
        } else if (n == 0 && t.mtime > 0 && now - t.mtime / 1000 >= 30 * Day) {
            unheard.push_back({i, 0});
        } else if (n == 0) {
            // A new library has no history yet: its unplayed songs, oldest
            // first, so the page is never empty on day one.
            fresh.push_back({i, double(t.mtime)});
        }
    }
    // Weighted picks without replacement: the score raised to a random
    // power (Efraimidis-Spirakis), so favourites come back more often but
    // not always the same ones.
    std::uniform_real_distribution<double> u(1e-9, 1.0);
    for (Scored &s : loved)
        s.score = std::pow(u(rng), 1.0 / std::max(0.01, s.score));
    std::sort(loved.begin(), loved.end(), [](const Scored &a, const Scored &b) { return a.score > b.score; });
    std::shuffle(unheard.begin(), unheard.end(), rng);

    std::vector<int> picked;
    for (const Scored &s : loved)
        if (int(picked.size()) < count)
            picked.push_back(s.index);
    for (const Scored &s : unheard)
        if (int(picked.size()) < count)
            picked.push_back(s.index);
    if (int(picked.size()) < count) {
        std::shuffle(fresh.begin(), fresh.end(), rng);
        std::stable_sort(fresh.begin(), fresh.end(), [](const Scored &a, const Scored &b) {
            return qint64(a.score / 86400000) < qint64(b.score / 86400000);   // by day added
        });
        for (const Scored &s : fresh)
            if (int(picked.size()) < count)
                picked.push_back(s.index);
    }

    // Never the same artist twice in a row, where it can be helped.
    auto artist = [&](int i) { const Track &t = tracks[size_t(i)]; return (t.artist.isEmpty() ? t.albumArtist : t.artist).toLower(); };
    for (size_t i = 1; i < picked.size(); ++i) {
        if (artist(picked[i]) != artist(picked[i - 1]))
            continue;
        for (size_t j = i + 1; j < picked.size(); ++j)
            if (artist(picked[j]) != artist(picked[i - 1])) {
                std::swap(picked[i], picked[j]);
                break;
            }
    }
    return picked;
}

void Library::loadStats() {
    QFile file(m_statsFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject plays = QJsonDocument::fromJson(file.readAll()).object().value("plays").toObject();
    for (auto it = plays.begin(); it != plays.end(); ++it) {
        const QJsonArray a = it.value().toArray();
        m_plays.insert(it.key(), a.at(0).toInt());
        m_lastPlayed.insert(it.key(), qint64(a.at(1).toDouble()));
    }
}

void Library::saveStats() {
    QJsonObject plays;
    for (auto it = m_plays.begin(); it != m_plays.end(); ++it)
        plays.insert(it.key(), QJsonArray{it.value(), double(m_lastPlayed.value(it.key()))});
    QDir().mkpath(QFileInfo(m_statsFile).absolutePath());
    QSaveFile file(m_statsFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"plays", plays}}).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void Library::flush() {
    if (m_statsSave.isActive()) {
        m_statsSave.stop();
        saveStats();
    }
}

// ---- AlbumModel ----

AlbumModel::AlbumModel(Library *library, QObject *parent) : QAbstractListModel(parent), m_library(library) {
    connect(library, &Library::changed, this, &AlbumModel::rebuild);
    rebuild();
}

QHash<int, QByteArray> AlbumModel::roleNames() const {
    return {{IdRole, "albumId"}, {TitleRole, "title"},  {ArtistRole, "artist"},
            {YearRole, "year"},  {CountRole, "tracks"}, {DurationRole, "duration"}};
}

QVariant AlbumModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= int(m_rows.size()))
        return {};
    const Album &a = m_library->albums()[size_t(m_rows[size_t(index.row())])];
    switch (role) {
    case IdRole: return a.id;
    case TitleRole: return a.title;
    case ArtistRole: return a.artist;
    case YearRole: return a.year;
    case CountRole: return int(a.tracks.size());
    case DurationRole: return a.duration;
    }
    return {};
}

void AlbumModel::setQuery(const QString &query) {
    if (query == m_query)
        return;
    m_query = query;
    emit queryChanged();
    rebuild();
}

void AlbumModel::setSort(int sort) {
    if (sort == m_sort)
        return;
    m_sort = sort;
    emit sortChanged();
    rebuild();
}

QString AlbumModel::idAt(int row) const {
    return row >= 0 && row < int(m_rows.size()) ? m_library->albums()[size_t(m_rows[size_t(row)])].id : QString();
}

void AlbumModel::rebuild() {
    beginResetModel();
    m_rows.clear();
    const QStringList words = m_query.toLower().split(' ', Qt::SkipEmptyParts);
    const auto &albums = m_library->albums();
    const auto &tracks = m_library->tracks();
    for (int i = 0; i < int(albums.size()); ++i) {
        const Album &a = albums[size_t(i)];
        bool keep = words.isEmpty();
        if (!keep) {
            const QString hay = (a.title + " " + a.artist + " " + QFileInfo(a.folder).fileName()).toLower();
            keep = std::all_of(words.begin(), words.end(), [&](const QString &w) { return hay.contains(w); });
            // An album also matches when one of its songs does.
            for (size_t k = 0; !keep && k < a.tracks.size(); ++k)
                keep = TrackModel::matches(tracks[size_t(a.tracks[k])], words);
        }
        if (keep)
            m_rows.push_back(i);
    }
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](int x, int y) {
        const Album &a = albums[size_t(x)], &b = albums[size_t(y)];
        if (m_sort == RecentlyAdded && a.added != b.added)
            return a.added > b.added;
        if (m_sort == ByArtist) {
            if (a.artist.isEmpty() != b.artist.isEmpty())
                return b.artist.isEmpty();
            const int c = QString::localeAwareCompare(a.artist, b.artist);
            if (c != 0)
                return c < 0;
            if (a.year != b.year)
                return a.year < b.year;
        }
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    endResetModel();
    emit countChanged();
}

// ---- TrackModel ----

TrackModel::TrackModel(Library *library, QObject *parent) : QAbstractListModel(parent), m_library(library) {
    connect(library, &Library::changed, this, &TrackModel::rebuild);
    rebuild();
}

QHash<int, QByteArray> TrackModel::roleNames() const {
    return {{PathRole, "path"},       {TitleRole, "title"},       {ArtistRole, "artist"}, {AlbumRole, "album"},
            {AlbumIdRole, "albumId"}, {DurationRole, "duration"}, {NumberRole, "number"}, {PlaysRole, "plays"}};
}

QVariant TrackModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= int(m_rows.size()))
        return {};
    const Track &t = m_library->tracks()[size_t(m_rows[size_t(index.row())])];
    switch (role) {
    case PathRole: return t.path;
    case TitleRole: return t.title;
    case ArtistRole: return t.artist;
    case AlbumRole: return t.album.isEmpty() ? (m_library->album(t.albumId) ? m_library->album(t.albumId)->title : QString()) : t.album;
    case AlbumIdRole: return t.albumId;
    case DurationRole: return t.duration;
    case NumberRole: return t.number;
    case PlaysRole: return m_library->playCount(t.path);
    }
    return {};
}

bool TrackModel::matches(const Track &track, const QStringList &words) {
    return std::all_of(words.begin(), words.end(), [&](const QString &w) { return track.search.contains(w); });
}

void TrackModel::setQuery(const QString &query) {
    if (query == m_query)
        return;
    m_query = query;
    emit queryChanged();
    rebuild();
}

void TrackModel::setMode(int mode) {
    if (mode == m_mode)
        return;
    m_mode = mode;
    emit modeChanged();
    rebuild();
}

void TrackModel::setAlbumId(const QString &id) {
    if (id == m_albumId)
        return;
    m_albumId = id;
    emit albumIdChanged();
    if (m_mode == AlbumTracks)
        rebuild();
}

double TrackModel::totalDuration() const {
    double total = 0;
    for (int i : m_rows)
        total += m_library->tracks()[size_t(i)].duration;
    return total;
}

QStringList TrackModel::paths() const {
    QStringList out;
    out.reserve(qsizetype(m_rows.size()));
    for (int i : m_rows)
        out << m_library->tracks()[size_t(i)].path;
    return out;
}

QString TrackModel::pathAt(int row) const {
    return row >= 0 && row < int(m_rows.size()) ? m_library->tracks()[size_t(m_rows[size_t(row)])].path : QString();
}

int TrackModel::rowOf(const QString &path) const {
    for (int r = 0; r < int(m_rows.size()); ++r)
        if (m_library->tracks()[size_t(m_rows[size_t(r)])].path == path)
            return r;
    return -1;
}

void TrackModel::rebuild() {
    beginResetModel();
    m_rows.clear();
    const QStringList words = m_query.toLower().split(' ', Qt::SkipEmptyParts);
    const auto &tracks = m_library->tracks();
    if (m_mode == LostAndFound) {
        for (int i : m_library->lostAndFound(QDateTime::currentSecsSinceEpoch()))
            if (matches(tracks[size_t(i)], words))
                m_rows.push_back(i);
    } else if (m_mode == AlbumTracks) {
        if (const Album *a = m_library->album(m_albumId))
            for (int i : a->tracks)
                if (matches(tracks[size_t(i)], words))
                    m_rows.push_back(i);
    } else {
        for (int i = 0; i < int(tracks.size()); ++i)
            if ((m_mode != MostPlayed || m_library->playCount(tracks[size_t(i)].path) > 0) &&
                matches(tracks[size_t(i)], words))
                m_rows.push_back(i);
        if (m_mode == MostPlayed) {
            std::stable_sort(m_rows.begin(), m_rows.end(), [&](int a, int b) {
                const int pa = m_library->playCount(tracks[size_t(a)].path), pb = m_library->playCount(tracks[size_t(b)].path);
                if (pa != pb)
                    return pa > pb;
                return m_library->lastPlayed(tracks[size_t(a)].path) > m_library->lastPlayed(tracks[size_t(b)].path);
            });
            if (m_rows.size() > 200)
                m_rows.resize(200);
        } else {
            const auto &albums = m_library->albums();
            Q_UNUSED(albums);
            std::stable_sort(m_rows.begin(), m_rows.end(), [&](int a, int b) {
                const Track &x = tracks[size_t(a)], &y = tracks[size_t(b)];
                if (x.artist.isEmpty() != y.artist.isEmpty())
                    return y.artist.isEmpty(); // unknown artists last
                const int c = QString::localeAwareCompare(x.artist, y.artist);
                if (c != 0)
                    return c < 0;
                if (x.albumId != y.albumId)
                    return x.albumId < y.albumId;
                if (x.disc != y.disc)
                    return x.disc < y.disc;
                return x.number < y.number;
            });
        }
    }
    endResetModel();
    emit countChanged();
}

// ---- CoverProvider ----

namespace {
class CoverResponse : public QQuickImageResponse, public QRunnable {
public:
    CoverResponse(const QString &id, int size) : m_id(id), m_size(size) { setAutoDelete(false); }
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    void run() override {
        m_image = Covers::load(m_id, m_size);
        emit finished();
    }
    QString errorString() const override { return m_image.isNull() ? QStringLiteral("no cover") : QString(); }

private:
    QString m_id;
    int m_size;
    QImage m_image;
};
}

QQuickImageResponse *CoverProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    // Two sizes cover every place a cover is drawn.
    const int size = std::max(requestedSize.width(), requestedSize.height()) > 256 ? 640 : 256;
    auto *response = new CoverResponse(id, size);
    static QThreadPool pool;
    pool.setMaxThreadCount(2);
    pool.start(response);
    return response;
}
