#include "playlists.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <tuple>

#include "analysis.h"

namespace {

const QString SmartSuffix = ".halo-smart";
constexpr int TrashKeep = 50;

qint64 nowSeconds() { return QDateTime::currentSecsSinceEpoch(); }

enum class Kind { Text, Number, Date, Unknown };

Kind kindOf(const QString &field) {
    static const QStringList text{"title", "artist", "album", "genre", "folder"};
    static const QStringList number{"year", "length", "plays", "bpm", "loudness"};
    static const QStringList date{"added", "played"};
    if (text.contains(field))
        return Kind::Text;
    if (number.contains(field))
        return Kind::Number;
    if (date.contains(field))
        return Kind::Date;
    return Kind::Unknown;
}

QString textOf(const Track &t, const QString &field) {
    if (field == "title")
        return t.title;
    if (field == "artist")
        return t.artist + (t.albumArtist.isEmpty() || t.albumArtist == t.artist ? QString() : "\n" + t.albumArtist);
    if (field == "album")
        return t.album;
    if (field == "genre")
        return t.genre;
    if (field == "folder")
        return QFileInfo(t.path).absolutePath();
    return {};
}

double numberOf(const Track &t, const TrackFacts &f, const QString &field) {
    if (field == "year")
        return t.year;
    if (field == "length")
        return t.duration / 60.0;
    if (field == "plays")
        return f.plays;
    if (field == "bpm")
        return f.bpm;
    if (field == "loudness")
        return f.loudness;
    return 0;
}

// Whether a track passes one rule. Unknown fields and operators never get
// here: fromSpec drops them.
bool passes(const PlaylistRule &r, const Track &t, const TrackFacts &f, qint64 now) {
    switch (kindOf(r.field)) {
    case Kind::Text: {
        const QString have = textOf(t, r.field);
        const QString want = r.value.trimmed();
        if (r.op == "contains")
            return have.contains(want, Qt::CaseInsensitive);
        if (r.op == "notContains")
            return !have.contains(want, Qt::CaseInsensitive);
        if (r.op == "startsWith")
            return have.startsWith(want, Qt::CaseInsensitive);
        // "is" against an artist matches either credit line.
        const QStringList lines = have.split('\n');
        const bool equal = std::any_of(lines.begin(), lines.end(),
                                       [&](const QString &l) { return l.compare(want, Qt::CaseInsensitive) == 0; });
        return r.op == "is" ? equal : !equal;
    }
    case Kind::Number: {
        bool ok = false;
        const double want = r.value.trimmed().toDouble(&ok);
        if (!ok)
            return false;
        const double have = numberOf(t, f, r.field);
        // Tempo and loudness are unknown until analysed: such a song
        // matches no number rule about them, rather than counting as 0.
        if ((r.field == "bpm" && f.bpm <= 0) || (r.field == "loudness" && f.loudness == 0))
            return false;
        if (r.field == "year" && t.year <= 0)
            return r.op == "isNot";
        if (r.op == "greater")
            return have > want;
        if (r.op == "less")
            return have < want;
        const double tolerance = r.field == "length" ? 0.5 : r.field == "loudness" || r.field == "bpm" ? 0.5 : 1e-9;
        const bool equal = std::fabs(have - want) <= tolerance;
        return r.op == "is" ? equal : !equal;
    }
    case Kind::Date: {
        const qint64 when = r.field == "added" ? t.mtime : f.lastPlayed;
        if (r.op == "never")
            return when <= 0;
        bool ok = false;
        const double days = r.value.trimmed().toDouble(&ok);
        if (!ok || days < 0)
            return false;
        const bool within = when > 0 && now - when <= qint64(days * 86400);
        return r.op == "inLast" ? within : !within;
    }
    case Kind::Unknown:
        break;
    }
    return false;
}

bool validOp(const QString &field, const QString &op) {
    switch (kindOf(field)) {
    case Kind::Text:
        return QStringList{"contains", "notContains", "is", "isNot", "startsWith"}.contains(op);
    case Kind::Number:
        return QStringList{"is", "isNot", "greater", "less"}.contains(op);
    case Kind::Date:
        return QStringList{"inLast", "notInLast"}.contains(op) || (op == "never" && field == "played");
    case Kind::Unknown:
        break;
    }
    return false;
}

QString slug(const QString &name) {
    QString s = name.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    for (const QChar c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            out += c;
        else if (c.isSpace() || c == '-' || c == '_' || c == '.')
            out += '-';
    }
    out.replace(QRegularExpression("-+"), "-");
    out = out.left(60);
    while (out.startsWith('-'))
        out.remove(0, 1);
    while (out.endsWith('-'))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("playlist") : out;
}

// Reads a file that belongs in the playlist folder: a regular file, not a
// link, and not over the size cap.
bool readBounded(const QString &path, QByteArray &out) {
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.size() > Playlists::MaxFileBytes)
        return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    out = f.read(Playlists::MaxFileBytes + 1);
    return out.size() <= Playlists::MaxFileBytes;
}

bool writeFile(const QString &path, const QByteArray &data) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return f.commit();
}

}

// ---- the rules, on plain data ----

QString Playlists::cleanName(const QString &name) {
    QString s;
    for (const QChar c : name)
        s += c.isPrint() ? c : QChar(' ');
    s = s.simplified();
    return s.left(MaxNameLength);
}

Playlist Playlists::fromSpec(const QVariantMap &spec) {
    Playlist p;
    p.smart = true;
    p.name = cleanName(spec.value("name").toString());
    p.description = spec.value("description").toString().left(2000);
    p.pinned = spec.value("pinned").toBool();
    p.matchAll = spec.value("matchAll", true).toBool();
    for (const QVariant &v : spec.value("rules").toList()) {
        const QVariantMap m = v.toMap();
        PlaylistRule r{m.value("field").toString(), m.value("op").toString(), m.value("value").toString().left(500)};
        if (validOp(r.field, r.op) && p.rules.size() < 50)
            p.rules.push_back(r);
    }
    static const QStringList sorts{"added", "title", "artist", "album", "year", "plays", "played", "bpm", "loudness", "random"};
    const QString sort = spec.value("sort").toString();
    p.sort = sorts.contains(sort) ? sort : QStringLiteral("added");
    p.descending = spec.value("descending", true).toBool();
    p.limit = std::clamp(spec.value("limit").toInt(), 0, MaxEntries);
    p.created = spec.value("created").toLongLong();
    return p;
}

QVariantMap Playlists::toSpec(const Playlist &p) {
    QVariantList rules;
    for (const PlaylistRule &r : p.rules)
        rules << QVariantMap{{"field", r.field}, {"op", r.op}, {"value", r.value}};
    return {{"name", p.name}, {"description", p.description}, {"pinned", p.pinned}, {"matchAll", p.matchAll},
            {"rules", rules}, {"sort", p.sort}, {"descending", p.descending}, {"limit", p.limit},
            {"created", p.created}};
}

std::vector<int> Playlists::evaluate(const Playlist &p, const std::vector<Track> &tracks,
                                     const std::function<TrackFacts(const Track &)> &facts, qint64 now) {
    std::vector<int> rows;
    std::vector<TrackFacts> known(tracks.size());
    for (int i = 0; i < int(tracks.size()); ++i) {
        const Track &t = tracks[size_t(i)];
        const TrackFacts f = facts(t);
        bool keep = p.matchAll;
        for (const PlaylistRule &r : p.rules) {
            const bool ok = passes(r, t, f, now);
            if (p.matchAll && !ok) {
                keep = false;
                break;
            }
            if (!p.matchAll && ok) {
                keep = true;
                break;
            }
        }
        if (p.rules.empty())
            keep = true;
        if (keep) {
            rows.push_back(i);
            known[size_t(i)] = f;
        }
    }

    if (p.sort == "random") {
        // The same order all day, a new one tomorrow, like Lost & Found.
        QRandomGenerator rng(quint32(qHash(p.name)) ^ quint32(now / 86400));
        for (int i = int(rows.size()) - 1; i > 0; --i)
            std::swap(rows[size_t(i)], rows[size_t(rng.bounded(i + 1))]);
    } else {
        auto key = [&](int i) -> double {
            const Track &t = tracks[size_t(i)];
            const TrackFacts &f = known[size_t(i)];
            if (p.sort == "added")
                return double(t.mtime);
            if (p.sort == "year")
                return t.year;
            if (p.sort == "plays")
                return f.plays;
            if (p.sort == "played")
                return double(f.lastPlayed);
            if (p.sort == "bpm")
                return f.bpm;
            if (p.sort == "loudness")
                return f.loudness;
            return 0;
        };
        auto text = [&](int i) -> QString {
            const Track &t = tracks[size_t(i)];
            return p.sort == "title" ? t.title : p.sort == "artist" ? t.artist : t.album;
        };
        const bool byText = p.sort == "title" || p.sort == "artist" || p.sort == "album";
        std::stable_sort(rows.begin(), rows.end(), [&](int a, int b) {
            if (byText) {
                const int c = QString::localeAwareCompare(text(a), text(b));
                if (c != 0)
                    return p.descending ? c > 0 : c < 0;
                // An album's songs stay in album order.
                const Track &x = tracks[size_t(a)], &y = tracks[size_t(b)];
                return std::tie(x.disc, x.number) < std::tie(y.disc, y.number);
            }
            const double x = key(a), y = key(b);
            return p.descending ? x > y : x < y;
        });
    }
    if (p.limit > 0 && int(rows.size()) > p.limit)
        rows.resize(size_t(p.limit));
    return rows;
}

namespace {
QString haloLine(const Playlist &p) {
    QJsonObject halo{{"created", p.created}, {"pinned", p.pinned}};
    if (!p.description.isEmpty())
        halo.insert("description", p.description);
    // Plain ASCII (other characters as \u escapes), so the line is the same
    // bytes whatever encoding the rest of the file is in.
    const QString json = QString::fromUtf8(QJsonDocument(halo).toJson(QJsonDocument::Compact));
    QString out = "#HALO:";
    for (const QChar c : json)
        out += c.unicode() < 0x80 ? QString(c) : QString("\\u%1").arg(uint(c.unicode()), 4, 16, QChar('0'));
    return out;
}
}

// A song can be in a list twice: what was kept about each is filed under
// the path and which occurrence it is.
static QString occurrence(const QString &path, int n) {
    return n == 0 ? path : path + QChar(0) + QString::number(n);
}

// Tags that describe the one entry under them. Anything else above the
// first entry belongs to the file as a whole.
static bool perEntryTag(const QString &line) {
    static const QStringList tags{"#EXTINF", "#EXTVLCOPT", "#EXTBYT", "#EXTBIN", "#KODIPROP", "#EXTALBUMARTURL"};
    for (const QString &t : tags)
        if (line.startsWith(t))
            return true;
    return false;
}

QString Playlists::m3u8(const Playlist &p, const std::function<const Track *(const QString &)> &track) {
    QString out = (p.header.isEmpty() ? QStringLiteral("#EXTM3U") : p.header) + "\n#PLAYLIST:" + cleanName(p.name) + "\n" +
                  haloLine(p) + "\n";
    // Kept lines go back where they were: above their song, or after the
    // song they followed. Each is written once.
    QHash<QString, QList<int>> above, below;
    for (int i = 0; i < int(p.kept.size()); ++i)
        (p.kept[size_t(i)].before ? above : below)[p.kept[size_t(i)].anchor] << i;
    std::vector<bool> done(p.kept.size(), false);
    auto put = [&](const QList<int> &which) {
        for (int i : which) {
            if (done[size_t(i)])
                continue;
            done[size_t(i)] = true;
            for (const QString &line : p.kept[size_t(i)].lines)
                out += line + "\n";
        }
    };
    put(below.value(QString()));
    QHash<QString, int> count;
    for (const QString &path : p.paths) {
        const QString key = occurrence(path, count[path]++);
        put(above.value(key));
        const Track *t = track ? track(path) : nullptr;
        if (t) {
            QString credit = t->artist.isEmpty() ? t->title : t->artist + " - " + t->title;
            credit.replace('\n', ' ');
            out += QString("#EXTINF:%1,%2\n").arg(qRound(t->duration)).arg(credit);
        } else if (p.extinf.contains(key)) {
            out += p.extinf.value(key) + "\n";
        }
        // A song Halo added a second time is spelt like its first.
        out += p.written.value(key, p.written.value(path, path)) + "\n";
        put(below.value(key));
    }
    // A stream whose song was taken out goes to the end; tags that belonged
    // to a removed song go with it.
    for (int i = 0; i < int(p.kept.size()); ++i)
        if (!p.kept[size_t(i)].before)
            put({i});
    return out;
}

Playlist Playlists::parseM3u(const QByteArray &data, const QString &baseDir) {
    Playlist p;
    // Not UTF-8 (an older player's .m3u in its own code page): each byte is
    // read as one character, so every line can be written back byte for byte.
    QStringDecoder utf8(QStringDecoder::Utf8);
    QString text = utf8(data);
    p.bytes = utf8.hasError();
    if (p.bytes)
        text = QString::fromLatin1(data);
    p.bom = data.startsWith("\xEF\xBB\xBF");
    if (p.bom && p.bytes)
        text.remove(0, 3);
    else if (text.startsWith(QChar(0xFEFF))) // the decoder usually takes the mark off itself
        text.remove(0, 1);
    p.crlf = text.contains("\r\n");
    const QStringList lines = text.split('\n');
    QStringList pending; // tags since the last entry
    QHash<QString, int> count;
    bool first = true;   // nothing but tags so far
    QString last;        // the last song's key
    auto keepAfterLast = [&](const QStringList &what) { p.kept.push_back({last, false, what}); };
    for (QString raw : lines) {
        if (raw.endsWith('\r'))
            raw.chop(1);
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith('#')) {
            if (line.startsWith("#PLAYLIST:"))
                p.name = cleanName(line.mid(10));
            else if (line.startsWith("#HALO:")) {
                const QJsonObject o = QJsonDocument::fromJson(line.mid(6).toUtf8()).object();
                p.created = qint64(o.value("created").toDouble());
                p.pinned = o.value("pinned").toBool();
                p.description = o.value("description").toString().left(2000);
            } else if (line.startsWith("#EXTM3U") && p.header.isEmpty() && first && pending.isEmpty() && p.kept.empty())
                p.header = raw; // with whatever attributes it carries
            else
                pending << raw;
            continue;
        }
        if (first) {
            // Above the first entry: the file's own tags stay at the top
            // whatever happens to that entry.
            QStringList file, entry;
            for (const QString &t : pending)
                (perEntryTag(t.trimmed()) ? entry : file) << t;
            if (!file.isEmpty())
                p.kept.push_back({QString(), false, file});
            pending = entry;
            first = false;
        }
        QString path;
        if (line.startsWith("file://"))
            path = QUrl(line).toLocalFile();
        else if (!line.contains("://")) {
            path = line;
            if (!path.startsWith('/')) {
                path.replace('\\', '/');
                path = QDir(baseDir).absoluteFilePath(path);
            }
        }
        // A stream (Halo plays files), or more songs than Halo lists: not
        // shown, and kept in the file as it is.
        if (path.isEmpty() || p.paths.size() >= MaxEntries) {
            pending << raw;
            keepAfterLast(pending);
            pending.clear();
            continue;
        }
        path = QDir::cleanPath(path);
        const QString key = occurrence(path, count[path]++);
        // Halo writes #EXTINF for songs in the library; other tags stay.
        QString extinf;
        for (int i = int(pending.size()) - 1; i >= 0; --i)
            if (pending[i].trimmed().startsWith("#EXTINF")) {
                if (extinf.isEmpty())
                    extinf = pending[i];
                pending.removeAt(i);
            }
        if (!pending.isEmpty())
            p.kept.push_back({key, true, pending});
        if (!extinf.isEmpty())
            p.extinf.insert(key, extinf);
        if (line != path)
            p.written.insert(key, line);
        pending.clear();
        p.paths << path;
        last = key;
    }
    if (!pending.isEmpty())
        keepAfterLast(pending);
    return p;
}

QByteArray Playlists::patchHeader(const QByteArray &data, const Playlist &p, bool name) {
    static const QByteArray bom("\xEF\xBB\xBF");
    const bool hasBom = data.startsWith(bom);
    QList<QByteArray> lines = (hasBom ? data.mid(bom.size()) : data).split('\n');
    const QByteArray eol = data.contains("\r\n") ? "\r" : "";
    // In a file that isn't UTF-8 the name goes in a byte a character, as
    // the rest of it is read (saveHeader refuses names that can't).
    QStringDecoder utf8(QStringDecoder::Utf8);
    (void)QString(utf8(data));
    const QString name_ = cleanName(p.name);
    const QByteArray halo = haloLine(p).toLatin1(),
                     title = "#PLAYLIST:" + (utf8.hasError() ? name_.toLatin1() : name_.toUtf8());
    int head = -1, titleAt = -1, haloAt = -1;
    for (int i = 0; i < int(lines.size()); ++i) {
        const QByteArray t = lines[i].trimmed();
        const QByteArray end = lines[i].endsWith('\r') ? "\r" : "";
        if (t.startsWith("#HALO:")) {
            lines[i] = halo + end;
            haloAt = i;
        } else if (t.startsWith("#PLAYLIST:")) {
            if (name)
                lines[i] = title + end;
            titleAt = i;
        } else if (head < 0 && t.startsWith("#EXTM3U")) {
            head = i;
        }
    }
    int at = titleAt >= 0 ? titleAt + 1 : head >= 0 ? head + 1 : 0;
    if (name && titleAt < 0)
        lines.insert(at++, title + eol);
    if (haloAt < 0)
        lines.insert(at, halo + eol);
    return (hasBom ? bom : QByteArray()) + lines.join('\n');
}

// ---- Playlists ----

QString Playlists::defaultFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/halo/playlists";
}

Playlists::Playlists(Library *library, Analyzer *analyzer, const QString &folder, QObject *parent)
    : QObject(parent), m_library(library), m_analyzer(analyzer), m_folder(folder.isEmpty() ? defaultFolder() : folder) {
    QDir().mkpath(m_folder);
    QFile::setPermissions(m_folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // First connection, so the cache is dropped before anyone reads it again.
    connect(this, &Playlists::changed, this, [this] { m_allValid = false; });
    m_reload.setSingleShot(true);
    m_reload.setInterval(250);
    connect(&m_reload, &QTimer::timeout, this, &Playlists::load);
    m_watcher.addPath(m_folder);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { reloadSoon(); });
    // Smart playlists follow the library, and counts in the sidebar with them.
    if (m_library)
        connect(m_library, &Library::changed, this, [this] {
            emit changed();
            for (const Playlist &p : m_lists)
                if (p.smart)
                    emit contentsChanged(p.id);
        });
    // Tempo and loudness arrive in the background; smart playlists that
    // ask about them catch up every few seconds, not per song.
    m_analysed.setSingleShot(true);
    m_analysed.setInterval(5000);
    connect(&m_analysed, &QTimer::timeout, this, [this] {
        bool any = false;
        for (const Playlist &p : m_lists)
            if (p.smart)
                for (const PlaylistRule &r : p.rules)
                    if (r.field == "bpm" || r.field == "loudness")
                        any = true;
        if (!any)
            return;
        emit changed();
        for (const Playlist &p : m_lists)
            if (p.smart)
                emit contentsChanged(p.id);
    });
    if (m_analyzer)
        connect(m_analyzer, &Analyzer::analyzed, this, [this] {
            if (!m_analysed.isActive())
                m_analysed.start();
        });
    load();
}

void Playlists::reloadSoon() {
    if (QDateTime::currentMSecsSinceEpoch() - m_ownWrite < 1000)
        return;
    m_reload.start();
}

void Playlists::load() {
    std::vector<Playlist> lists;
    QHash<QString, QString> files;
    QDir dir(m_folder);
    const QFileInfoList entries = dir.entryInfoList({"*.m3u8", "*.m3u", "*" + SmartSuffix}, QDir::Files | QDir::NoSymLinks,
                                                    QDir::Name);
    QSet<QString> seen;
    for (const QFileInfo &info : entries) {
        const QString file = info.fileName();
        const bool smart = file.endsWith(SmartSuffix);
        const QString id = smart ? file.chopped(SmartSuffix.size()) : info.completeBaseName();
        if (id.isEmpty() || seen.contains(id))
            continue;
        QByteArray data;
        if (!readBounded(info.absoluteFilePath(), data))
            continue;
        Playlist p;
        if (smart) {
            const QJsonObject o = QJsonDocument::fromJson(data).object();
            if (o.isEmpty())
                continue;
            p = fromSpec(o.toVariantMap());
        } else {
            p = parseM3u(data, info.absolutePath());
        }
        p.id = id;
        if (p.name.isEmpty())
            p.name = cleanName(info.completeBaseName().replace('-', ' ').replace('_', ' '));
        if (p.created <= 0)
            p.created = info.birthTime().isValid() ? info.birthTime().toSecsSinceEpoch() : info.lastModified().toSecsSinceEpoch();
        p.modified = info.lastModified().toSecsSinceEpoch();
        seen.insert(id);
        files.insert(id, info.absoluteFilePath());
        lists.push_back(std::move(p));
    }
    m_lists = std::move(lists);
    m_files = files;

    m_trash.clear();
    QDir trash(m_folder + "/.trash");
    for (const QFileInfo &info : trash.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Time | QDir::Reversed))
        m_trash << info.absoluteFilePath();

    emit changed();
    for (const Playlist &p : m_lists)
        emit contentsChanged(p.id);
}

QString Playlists::fileOf(const Playlist &p) const {
    const QString known = m_files.value(p.id);
    if (!known.isEmpty())
        return known;
    return m_folder + "/" + p.id + (p.smart ? SmartSuffix : QStringLiteral(".m3u8"));
}

bool Playlists::save(Playlist &p) {
    p.modified = nowSeconds();
    QByteArray data;
    if (p.smart) {
        QVariantMap spec = toSpec(p);
        spec.insert("version", 1);
        data = QJsonDocument(QJsonObject::fromVariantMap(spec)).toJson(QJsonDocument::Indented);
    } else {
        const QString text = m3u8(p, [this](const QString &path) { return m_library ? m_library->track(path) : nullptr; });
        if (p.bytes) {
            // A file that isn't UTF-8 goes back a byte a character, as it was
            // read. A name with a character that can't be written that way
            // would turn the whole file into something else, so it isn't saved.
            for (const QChar c : text)
                if (c.unicode() > 0xFF) {
                    emit message(tr("“%1” isn't a UTF-8 playlist, and that name can't be written into it. Nothing was changed.").arg(p.name));
                    m_reload.start(); // back to what the file holds
                    return false;
                }
            data = text.toLatin1();
        } else {
            data = text.toUtf8();
        }
        // The file keeps the line endings and the byte-order mark it came with.
        if (p.crlf)
            data.replace("\n", "\r\n");
        if (p.bom)
            data.prepend("\xEF\xBB\xBF");
    }
    return write(p, fileOf(p), data);
}

bool Playlists::saveHeader(Playlist &p, bool name) {
    // The file as it is now on disk, with those lines changed: a playlist in
    // this folder can be another player's too, and may hold things Halo
    // doesn't list.
    const QString file = m_files.value(p.id);
    QByteArray data;
    if (p.smart || file.isEmpty() || !readBounded(file, data))
        return save(p);
    if (name && p.bytes)
        for (const QChar c : p.name)
            if (c.unicode() > 0xFF) {
                emit message(tr("“%1” isn't a UTF-8 playlist, and that name can't be written into it. Nothing was changed.").arg(p.id));
                m_reload.start();
                return false;
            }
    p.modified = nowSeconds();
    return write(p, file, patchHeader(data, p, name));
}

bool Playlists::write(Playlist &p, const QString &file, const QByteArray &data) {
    m_ownWrite = QDateTime::currentMSecsSinceEpoch();
    if (!writeFile(file, data)) {
        emit message(tr("Couldn't save the playlist “%1”.").arg(p.name));
        return false;
    }
    m_files.insert(p.id, file);
    emit changed();
    emit contentsChanged(p.id);
    return true;
}

QString Playlists::newId(const QString &name) const {
    const QString base = slug(name);
    QString id = base;
    for (int n = 2;; ++n) {
        const bool taken = find(id) || QFileInfo::exists(m_folder + "/" + id + ".m3u8") ||
                           QFileInfo::exists(m_folder + "/" + id + ".m3u") ||
                           QFileInfo::exists(m_folder + "/" + id + SmartSuffix);
        if (!taken)
            return id;
        id = base + "-" + QString::number(n);
    }
}

const Playlist *Playlists::find(const QString &id) const {
    for (const Playlist &p : m_lists)
        if (p.id == id)
            return &p;
    return nullptr;
}

Playlist *Playlists::edit(const QString &id) { return const_cast<Playlist *>(find(id)); }

const Playlist *Playlists::lookup(const QString &idOrName) const {
    if (const Playlist *p = find(idOrName))
        return p;
    for (const Playlist &p : m_lists)
        if (p.name.compare(idOrName.simplified(), Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}

QVariantList Playlists::all() const {
    if (m_allValid)
        return m_allCache;
    std::vector<const Playlist *> order;
    for (const Playlist &p : m_lists)
        order.push_back(&p);
    // Pinned first, then by name.
    std::stable_sort(order.begin(), order.end(), [](const Playlist *a, const Playlist *b) {
        if (a->pinned != b->pinned)
            return a->pinned;
        return QString::localeAwareCompare(a->name, b->name) < 0;
    });
    QVariantList out;
    for (const Playlist *p : order)
        out << QVariantMap{{"id", p->id}, {"name", p->name}, {"smart", p->smart}, {"pinned", p->pinned},
                           {"count", p->smart ? int(paths(p->id).size()) : int(p->paths.size())}};
    m_allCache = out;
    m_allValid = true;
    return out;
}

QVariantList Playlists::manual() const {
    QVariantList out;
    for (const QVariant &v : all())
        if (!v.toMap().value("smart").toBool())
            out << v;
    return out;
}

TrackFacts Playlists::factsOf(const Track &t) const {
    TrackFacts f;
    if (m_library) {
        f.plays = m_library->playCount(t.path);
        f.lastPlayed = m_library->lastPlayed(t.path);
    }
    TrackAnalysis a;
    if (m_analyzer && m_analyzer->lookup(t.path, a) && a.valid) {
        f.bpm = a.bpm;
        f.loudness = a.loudness;
    }
    return f;
}

QStringList Playlists::paths(const QString &id) const {
    const Playlist *p = find(id);
    if (!p)
        return {};
    if (!p->smart)
        return p->paths;
    QStringList out;
    if (!m_library)
        return out;
    const auto &tracks = m_library->tracks();
    for (int i : evaluate(*p, tracks, [this](const Track &t) { return factsOf(t); }, nowSeconds()))
        out << tracks[size_t(i)].path;
    return out;
}

int Playlists::previewCount(const QVariantMap &spec) const {
    if (!m_library)
        return 0;
    return int(evaluate(fromSpec(spec), m_library->tracks(), [this](const Track &t) { return factsOf(t); }, nowSeconds()).size());
}

QVariantMap Playlists::info(const QString &id) const {
    const Playlist *p = find(id);
    if (!p)
        return {};
    const QStringList list = paths(id);
    double duration = 0;
    int missing = 0;
    QStringList covers;
    for (const QString &path : list) {
        const Track *t = m_library ? m_library->track(path) : nullptr;
        if (t) {
            duration += t->duration;
            if (covers.size() < 4 && !t->albumId.isEmpty() && !covers.contains(t->albumId))
                covers << t->albumId;
        } else if (!QFileInfo::exists(path)) {
            ++missing;
        }
    }
    return {{"id", p->id}, {"name", p->name}, {"description", p->description}, {"smart", p->smart},
            {"pinned", p->pinned}, {"count", int(list.size())}, {"duration", duration}, {"missing", missing},
            {"covers", covers}, {"created", p->created}, {"modified", p->modified}, {"file", fileOf(*p)}};
}

QVariantMap Playlists::spec(const QString &id) const {
    const Playlist *p = find(id);
    return p && p->smart ? toSpec(*p) : QVariantMap{};
}

QString Playlists::create(const QString &name, const QStringList &paths) {
    const QString clean = cleanName(name);
    if (clean.isEmpty())
        return {};
    Playlist p;
    p.id = newId(clean);
    p.name = clean;
    p.created = nowSeconds();
    for (const QString &path : paths)
        if (!p.paths.contains(path) && p.paths.size() < MaxEntries)
            p.paths << path;
    m_lists.push_back(p);
    if (!save(m_lists.back())) {
        m_lists.pop_back();
        return {};
    }
    return p.id;
}

QString Playlists::createSmart(const QString &name, const QVariantMap &spec) {
    Playlist p = fromSpec(spec);
    p.name = cleanName(name.isEmpty() ? spec.value("name").toString() : name);
    if (p.name.isEmpty())
        return {};
    p.id = newId(p.name);
    p.created = nowSeconds();
    m_lists.push_back(p);
    if (!save(m_lists.back())) {
        m_lists.pop_back();
        return {};
    }
    return p.id;
}

QString Playlists::duplicate(const QString &id) {
    const Playlist *p = find(id);
    if (!p)
        return {};
    Playlist copy = *p;
    copy.name = cleanName(tr("%1 copy").arg(p->name));
    copy.id = newId(copy.name);
    copy.created = nowSeconds();
    copy.pinned = false;
    m_lists.push_back(copy);
    if (!save(m_lists.back())) {
        m_lists.pop_back();
        return {};
    }
    return copy.id;
}

bool Playlists::rename(const QString &id, const QString &name) {
    Playlist *p = edit(id);
    const QString clean = cleanName(name);
    if (!p || clean.isEmpty())
        return false;
    if (clean == p->name)
        return true;
    p->name = clean;
    return saveHeader(*p, true);
}

bool Playlists::setDescription(const QString &id, const QString &text) {
    Playlist *p = edit(id);
    if (!p)
        return false;
    p->description = text.trimmed().left(2000);
    return saveHeader(*p, false);
}

bool Playlists::setPinned(const QString &id, bool pinned) {
    Playlist *p = edit(id);
    if (!p)
        return false;
    p->pinned = pinned;
    return saveHeader(*p, false);
}

bool Playlists::remove(const QString &id) {
    const Playlist *p = find(id);
    if (!p)
        return false;
    const QString file = fileOf(*p);
    const QString trashDir = m_folder + "/.trash";
    QDir().mkpath(trashDir);
    QString target = trashDir + "/" + QFileInfo(file).fileName();
    if (QFileInfo::exists(target))
        target = trashDir + "/" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "-" + QFileInfo(file).fileName();
    m_ownWrite = QDateTime::currentMSecsSinceEpoch();
    if (!QFile::rename(file, target))
        return false;
    // The newest file last, so restoreLast takes it.
    QFile(target).setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    m_trash << target;
    while (m_trash.size() > TrashKeep)
        QFile::remove(m_trash.takeFirst());
    m_files.remove(id);
    m_lists.erase(std::remove_if(m_lists.begin(), m_lists.end(), [&](const Playlist &x) { return x.id == id; }),
                  m_lists.end());
    emit changed();
    return true;
}

QString Playlists::restoreLast() {
    while (!m_trash.isEmpty()) {
        const QString from = m_trash.takeLast();
        if (!QFileInfo::exists(from))
            continue;
        QString name = QFileInfo(from).fileName();
        // A timestamp added when the name was taken in the trash comes off.
        name.remove(QRegularExpression("^\\d{13}-"));
        const bool smart = name.endsWith(SmartSuffix);
        const QString id = smart ? name.chopped(SmartSuffix.size()) : QFileInfo(name).completeBaseName();
        const QString suffix = smart ? SmartSuffix : "." + QFileInfo(name).suffix();
        const QString target = m_folder + "/" + (find(id) ? newId(id) : id) + suffix;
        m_ownWrite = QDateTime::currentMSecsSinceEpoch();
        if (!QFile::rename(from, target))
            return {};
        load();
        return smart ? QFileInfo(target).fileName().chopped(SmartSuffix.size()) : QFileInfo(target).completeBaseName();
    }
    emit changed();
    return {};
}

int Playlists::add(const QString &id, const QStringList &paths, bool allowDuplicates) {
    Playlist *p = edit(id);
    if (!p || p->smart)
        return 0;
    QSet<QString> have(p->paths.begin(), p->paths.end());
    int added = 0;
    for (const QString &path : paths) {
        if (path.isEmpty() || p->paths.size() >= MaxEntries)
            continue;
        if (!allowDuplicates && have.contains(path))
            continue;
        p->paths << path;
        have.insert(path);
        ++added;
    }
    if (added > 0)
        save(*p);
    return added;
}

bool Playlists::removeRows(const QString &id, const QVariantList &rows) {
    Playlist *p = edit(id);
    if (!p || p->smart)
        return false;
    QList<int> order;
    for (const QVariant &v : rows)
        order << v.toInt();
    std::sort(order.begin(), order.end(), std::greater<int>());
    order.erase(std::unique(order.begin(), order.end()), order.end());
    bool any = false;
    for (int row : order)
        if (row >= 0 && row < p->paths.size()) {
            p->paths.removeAt(row);
            any = true;
        }
    return any && save(*p);
}

bool Playlists::move(const QString &id, int from, int to) {
    Playlist *p = edit(id);
    if (!p || p->smart || from < 0 || to < 0 || from >= p->paths.size() || to >= p->paths.size() || from == to)
        return false;
    p->paths.move(from, to);
    return save(*p);
}

bool Playlists::contains(const QString &id, const QString &path) const {
    const Playlist *p = find(id);
    return p && !p->smart && p->paths.contains(path);
}

bool Playlists::setSpec(const QString &id, const QVariantMap &spec) {
    Playlist *p = edit(id);
    if (!p || !p->smart)
        return false;
    Playlist next = fromSpec(spec);
    next.id = p->id;
    next.created = p->created;
    if (next.name.isEmpty())
        next.name = p->name;
    if (spec.value("description").isNull())
        next.description = p->description;
    *p = next;
    return save(*p);
}

QStringList Playlists::suggest(const QString &id, int count) const {
    const Playlist *p = find(id);
    if (!p || !m_library || count <= 0)
        return {};
    const QStringList members = paths(id);
    if (members.isEmpty())
        return {};
    const QSet<QString> inList(members.begin(), members.end());

    // What the playlist sounds like: each member's tempo and loudness, and
    // who and what genre it leans on.
    struct Seed {
        double bpm, loudness;
    };
    std::vector<Seed> seeds;
    QHash<QString, int> artists, genres;
    QSet<QString> folders;
    for (const QString &path : members) {
        const Track *t = m_library->track(path);
        if (!t)
            continue;
        ++artists[t->artist.toLower()];
        if (!t->genre.isEmpty())
            ++genres[t->genre.toLower()];
        folders.insert(QFileInfo(path).absolutePath());
        const TrackFacts f = factsOf(*t);
        if (f.bpm > 0 || f.loudness != 0)
            seeds.push_back({f.bpm, f.loudness});
    }
    // Long playlists: a spread of members is enough.
    if (seeds.size() > 200) {
        std::vector<Seed> few;
        for (size_t i = 0; i < seeds.size(); i += seeds.size() / 200 + 1)
            few.push_back(seeds[i]);
        seeds.swap(few);
    }

    QList<QPair<double, int>> scored;
    const auto &tracks = m_library->tracks();
    for (int i = 0; i < int(tracks.size()); ++i) {
        const Track &t = tracks[size_t(i)];
        if (inList.contains(t.path))
            continue;
        const TrackFacts f = factsOf(t);
        // Sound: the nearest member in tempo (half and double time count,
        // one point per 3 %) and loudness (a point per 2 LU), as Drift does.
        double sound = 6;
        if (!seeds.empty() && (f.bpm > 0 || f.loudness != 0)) {
            sound = 1e9;
            for (const Seed &s : seeds) {
                double tempo = 3;
                if (s.bpm > 0 && f.bpm > 0) {
                    double best = 1e9;
                    for (double factor : {0.5, 1.0, 2.0})
                        best = std::min(best, std::fabs(std::log(f.bpm * factor / s.bpm)) + (factor == 1.0 ? 0 : std::log(1.03)));
                    tempo = best / std::log(1.03);
                }
                const double level = (s.loudness != 0 && f.loudness != 0) ? std::fabs(f.loudness - s.loudness) / 2 : 1.5;
                sound = std::min(sound, tempo + level);
            }
        }
        // Taste: the playlist's artists and genres pull a song closer; its
        // own albums a little, so suggestions aren't all one record.
        double taste = 0;
        if (artists.contains(t.artist.toLower()))
            taste -= 2.5;
        if (!t.genre.isEmpty() && genres.contains(t.genre.toLower()))
            taste -= 2;
        if (folders.contains(QFileInfo(t.path).absolutePath()))
            taste -= 1;
        scored.append({sound + taste, i});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    // At most two songs by one artist, and never two in a row.
    QStringList out;
    QHash<QString, int> used;
    QString last;
    for (const auto &s : scored) {
        const Track &t = tracks[size_t(s.second)];
        const QString artist = t.artist.toLower();
        if (used.value(artist) >= 2 || (!artist.isEmpty() && artist == last))
            continue;
        out << t.path;
        ++used[artist];
        last = artist;
        if (out.size() >= count)
            break;
    }
    return out;
}

QVariantList Playlists::describe(const QStringList &paths) const {
    QVariantList out;
    for (const QString &path : paths.mid(0, 500)) {
        const Track *t = m_library ? m_library->track(path) : nullptr;
        out << QVariantMap{{"path", path},
                           {"title", t ? t->title : Library::titleFromFileName(QFileInfo(path).fileName())},
                           {"artist", t ? t->artist : QString()},
                           {"albumId", t ? t->albumId : QString()},
                           {"duration", t ? t->duration : 0.0}};
    }
    return out;
}

QString Playlists::importFile(const QString &file) {
    QByteArray data;
    const QFileInfo info(file);
    if (!info.isFile() || info.size() > MaxFileBytes) {
        emit message(tr("“%1” isn't a playlist file Halo can read.").arg(info.fileName()));
        return {};
    }
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    data = f.read(MaxFileBytes);
    Playlist p = parseM3u(data, info.absolutePath());
    if (p.name.isEmpty())
        p.name = cleanName(info.completeBaseName().replace('_', ' '));
    const QString id = create(p.name, p.paths);
    if (!id.isEmpty() && !p.description.isEmpty())
        setDescription(id, p.description);
    return id;
}

bool Playlists::exportFile(const QString &id, const QString &file) const {
    const Playlist *p = find(id);
    if (!p || file.isEmpty())
        return false;
    Playlist flat = *p;
    flat.paths = paths(id);
    flat.written.clear(); // spellings relative to the playlist folder mean nothing elsewhere
    return writeFile(file, m3u8(flat, [this](const QString &path) { return m_library ? m_library->track(path) : nullptr; }).toUtf8());
}

// ---- PlaylistTrackModel ----

PlaylistTrackModel::PlaylistTrackModel(Playlists *playlists, Library *library, QObject *parent)
    : QAbstractListModel(parent), m_playlists(playlists), m_library(library) {
    connect(playlists, &Playlists::contentsChanged, this, [this](const QString &id) {
        if (id == m_id)
            refresh();
    });
    connect(playlists, &Playlists::changed, this, [this] {
        if (!m_id.isEmpty() && !m_playlists->find(m_id))
            refresh();
    });
    connect(library, &Library::changed, this, [this] {
        m_described.clear();
        if (!m_id.isEmpty()) {
            beginResetModel();
            endResetModel();
        }
    });
}

void PlaylistTrackModel::setPlaylistId(const QString &id) {
    if (id == m_id)
        return;
    m_id = id;
    emit playlistIdChanged();
    refresh();
}

void PlaylistTrackModel::refresh() {
    const QStringList next = m_playlists->paths(m_id);
    if (next == m_paths)
        return;
    beginResetModel();
    m_paths = next;
    endResetModel();
    emit countChanged();
}

double PlaylistTrackModel::totalDuration() const {
    double total = 0;
    for (const QString &path : m_paths)
        if (const Track *t = m_library->track(path))
            total += t->duration;
    return total;
}

int PlaylistTrackModel::missing() const {
    int n = 0;
    for (const QString &path : m_paths)
        if (!m_library->track(path) && !QFileInfo::exists(path))
            ++n;
    return n;
}

QVariant PlaylistTrackModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_paths.size())
        return {};
    const QString &path = m_paths[index.row()];
    const Track *t = m_library->track(path);
    Track described;
    if (!t) {
        auto it = m_described.find(path);
        if (it == m_described.end()) {
            Track d;
            d.path = path;
            if (QFileInfo::exists(path))
                d = m_library->describe(path);
            if (d.title.isEmpty())
                d.title = Library::titleFromFileName(QFileInfo(path).fileName());
            it = m_described.insert(path, d);
        }
        described = it.value();
        t = &described;
    }
    switch (role) {
    case PathRole: return path;
    case TitleRole: return t->title;
    case ArtistRole: return t->artist;
    case AlbumRole: return t->album;
    case AlbumIdRole: return t->albumId;
    case DurationRole: return t->duration;
    case NumberRole: return t->number;
    case PlaysRole: return m_library->playCount(path);
    case MissingRole: return !m_library->track(path) && !QFileInfo::exists(path);
    }
    return {};
}

QHash<int, QByteArray> PlaylistTrackModel::roleNames() const {
    return {{PathRole, "path"}, {TitleRole, "title"}, {ArtistRole, "artist"}, {AlbumRole, "album"},
            {AlbumIdRole, "albumId"}, {DurationRole, "duration"}, {NumberRole, "number"}, {PlaysRole, "plays"},
            {MissingRole, "missing"}};
}
