#include "doctor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <fileref.h>
#include <tag.h>
#include <tpropertymap.h>

#include "library.h"

namespace Doctor {

QString kindName(Kind kind) {
    switch (kind) {
    case Kind::Watermark: return "watermark";
    case Kind::NumberedTitle: return "numbered";
    case Kind::MissingTitle: return "notitle";
    case Kind::MissingArtist: return "noartist";
    case Kind::MissingAlbum: return "noalbum";
    case Kind::MixedAlbumArtist: return "albumartist";
    case Kind::Duplicate: return "duplicate";
    }
    return {};
}

namespace {
// Addresses and the mixtape sites that stamp their name into tags.
const QRegularExpression &siteExpression() {
    static const QRegularExpression site(
        R"((https?://\S+|www\.\S+|\b[\w-]+\.(com|net|org|info|biz|us|co|fm|to)\b|\b(datpiff|livemixtapes|mixfiend|mixtapetorrent|spinrilla|hotnewhiphop|audiomack|mixtapemonkey|newhiphopmixtapes|coast2coast)\b))",
        QRegularExpression::CaseInsensitiveOption);
    return site;
}

QString key(const QString &s) {
    QString out;
    for (const QChar c : s.toLower())
        if (c.isLetterOrNumber())
            out += c;
    return out;
}
}

bool hasWatermark(const QString &text) { return siteExpression().match(text).hasMatch(); }

QString stripWatermark(const QString &text) {
    if (!hasWatermark(text))
        return text;
    QString s = text;
    // A bracketed part that names a site goes whole: "[MixFiend.com]",
    // "(Downloaded from DatPiff)".
    static const QRegularExpression bracketed(R"(\s*[\(\[\{][^\)\]\}]*[\)\]\}])");
    auto it = bracketed.globalMatch(s);
    QList<QPair<int, int>> cut;
    while (it.hasNext()) {
        const auto m = it.next();
        if (hasWatermark(m.captured(0)))
            cut.prepend({int(m.capturedStart()), int(m.capturedLength())});
    }
    for (const auto &[start, length] : cut)
        s.remove(start, length);
    // Otherwise the site's own segment between separators: "Song - DatPiff
    // Exclusive", "MixFiend.com | Song".
    if (hasWatermark(s)) {
        const QStringList parts = s.split(QRegularExpression(R"(\s+[-|/~:]\s+|\s*\|\s*)"));
        QStringList kept;
        for (const QString &p : parts)
            if (!hasWatermark(p))
                kept << p.trimmed();
        s = kept.join(" - ");
    }
    return s.simplified();
}

QString field(const Tags &t, const QString &name) {
    if (name == "title") return t.title;
    if (name == "artist") return t.artist;
    if (name == "album") return t.album;
    if (name == "albumartist") return t.albumArtist;
    if (name == "comment") return t.comment;
    return {};
}

std::vector<Finding> diagnose(const std::vector<File> &files) {
    std::vector<Finding> out;
    for (const File &f : files) {
        const Tags &t = f.tags;
        // At most one finding per field, so fixing one never makes another
        // stale. Watermarks on the fields that aren't checked further:
        for (const QString name : {"albumartist", "comment"}) {
            const QString v = field(t, name);
            if (!v.isEmpty() && hasWatermark(v))
                out.push_back({Kind::Watermark, f.path, {}, name, v, stripWatermark(v)});
        }
        // The title: watermark and numbering are fixed together.
        const QString title = stripWatermark(t.title);
        if (title.isEmpty()) {
            out.push_back({Kind::MissingTitle, f.path, {}, "title", t.title, Library::titleFromFileName(f.path)});
        } else {
            QString clean = Library::cleanTitle(title, t.artist);
            // A bare leading number is the track number when the tag says so
            // ("01 3 Way" on track 1), which Library can't know.
            static const QRegularExpression bare(R"(^\s*(\d{1,3})\s+(\S.*)$)");
            int number = t.number;
            if (number <= 0) {
                static const QRegularExpression lead(R"(^(\d{1,3})\D)");
                if (const auto n = lead.match(QFileInfo(f.path).fileName()); n.hasMatch())
                    number = n.captured(1).toInt();
            }
            if (const auto m = bare.match(clean); m.hasMatch() && number > 0 && m.captured(1).toInt() == number)
                clean = Library::cleanTitle(m.captured(2), t.artist);
            if (clean != t.title)
                out.push_back({clean != title ? Kind::NumberedTitle : Kind::Watermark, f.path, {}, "title", t.title,
                               clean});
        }
        // A tag that is nothing but a site's name counts as missing.
        const QString artistNow = stripWatermark(t.artist), albumNow = stripWatermark(t.album);
        if (!artistNow.isEmpty() && artistNow != t.artist)
            out.push_back({Kind::Watermark, f.path, {}, "artist", t.artist, artistNow});
        if (!albumNow.isEmpty() && albumNow != t.album)
            out.push_back({Kind::Watermark, f.path, {}, "album", t.album, albumNow});
        if (artistNow.isEmpty()) {
            // The album artist, or "Artist - Song" in the file name.
            QString artist = t.albumArtist;
            if (artist.isEmpty()) {
                const QString name = Library::titleFromFileName(f.path);
                const int dash = name.indexOf(" - ");
                if (dash > 0)
                    artist = name.left(dash).trimmed();
            }
            out.push_back({Kind::MissingArtist, f.path, {}, artist.isEmpty() ? QString() : "artist", t.artist, artist});
        }
        if (albumNow.isEmpty())
            out.push_back({Kind::MissingAlbum, f.path, {}, "album", t.album,
                           Library::prettyFolderName(QFileInfo(f.path).absolutePath())});
    }

    // Album artists that disagree inside one folder: the most common wins.
    QHash<QString, QList<const File *>> byFolder;
    for (const File &f : files)
        byFolder[QFileInfo(f.path).absolutePath()].append(&f);
    for (auto it = byFolder.cbegin(); it != byFolder.cend(); ++it) {
        QHash<QString, int> counts;
        for (const File *f : *it)
            if (!f->tags.albumArtist.isEmpty() && !hasWatermark(f->tags.albumArtist))
                ++counts[f->tags.albumArtist];
        if (counts.size() < 2)
            continue;
        QString common;
        int best = 0;
        for (auto c = counts.cbegin(); c != counts.cend(); ++c)
            if (c.value() > best || (c.value() == best && c.key() < common))
                best = c.value(), common = c.key();
        for (const File *f : *it)
            if (!f->tags.albumArtist.isEmpty() && !hasWatermark(f->tags.albumArtist) && f->tags.albumArtist != common)
                out.push_back({Kind::MixedAlbumArtist, f->path, {}, "albumartist", f->tags.albumArtist, common});
    }

    // The same song twice: same artist and title, lengths within 2 s.
    QHash<QString, QList<const File *>> bySong;
    for (const File &f : files) {
        const QString t = Library::cleanTitle(stripWatermark(f.tags.title), f.tags.artist);
        if (t.isEmpty() || f.tags.artist.isEmpty())
            continue;
        bySong[key(f.tags.artist) + "|" + key(t)].append(&f);
    }
    for (auto it = bySong.cbegin(); it != bySong.cend(); ++it) {
        QList<const File *> group = *it;
        std::sort(group.begin(), group.end(), [](const File *a, const File *b) { return a->path < b->path; });
        QSet<int> used;
        for (int i = 0; i < group.size(); ++i) {
            if (used.contains(i))
                continue;
            QStringList others;
            for (int j = i + 1; j < group.size(); ++j)
                if (!used.contains(j) && std::fabs(group[i]->tags.duration - group[j]->tags.duration) < 2.0) {
                    others << group[j]->path;
                    used.insert(j);
                }
            if (!others.isEmpty())
                out.push_back({Kind::Duplicate, group[i]->path, others, {}, {}, {}});
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const Finding &a, const Finding &b) {
        if (a.kind != b.kind)
            return int(a.kind) < int(b.kind);
        return a.path < b.path;
    });
    return out;
}

namespace {
TagLib::String toTag(const QString &s) { return TagLib::String(s.toStdString(), TagLib::String::UTF8); }
QString fromTag(const TagLib::String &s) { return QString::fromStdString(s.to8Bit(true)).trimmed(); }
const char *propertyName(const QString &field) {
    if (field == "title") return "TITLE";
    if (field == "artist") return "ARTIST";
    if (field == "album") return "ALBUM";
    if (field == "albumartist") return "ALBUMARTIST";
    if (field == "comment") return "COMMENT";
    return nullptr;
}
}

bool readTags(const QString &path, Tags &out) {
    TagLib::FileRef file(QFile::encodeName(path).constData(), true, TagLib::AudioProperties::Fast);
    if (file.isNull() || !file.tag())
        return false;
    const TagLib::PropertyMap p = file.properties();
    auto first = [&p](const char *name) {
        const auto it = p.find(name);
        return it == p.end() || it->second.isEmpty() ? QString() : fromTag(it->second.front());
    };
    out.title = first("TITLE");
    out.artist = first("ARTIST");
    out.album = first("ALBUM");
    out.albumArtist = first("ALBUMARTIST");
    out.comment = first("COMMENT");
    out.number = int(file.tag()->track());
    out.duration = file.audioProperties() ? file.audioProperties()->lengthInMilliseconds() / 1000.0 : 0;
    return true;
}

bool readValues(const QString &path, const QString &field, QStringList &out) {
    const char *name = propertyName(field);
    if (!name)
        return false;
    TagLib::FileRef file(QFile::encodeName(path).constData(), false);
    if (file.isNull() || !file.tag())
        return false;
    out.clear();
    const TagLib::PropertyMap p = file.properties();
    const auto it = p.find(name);
    if (it != p.end())
        for (const TagLib::String &v : it->second)
            out << QString::fromStdString(v.to8Bit(true));
    return true;
}

bool writeValues(const QString &path, const QString &field, const QStringList &values) {
    const char *name = propertyName(field);
    if (!name)
        return false;
    TagLib::FileRef file(QFile::encodeName(path).constData(), false);
    if (file.isNull() || !file.tag())
        return false;
    TagLib::PropertyMap p = file.properties();
    if (values.isEmpty()) {
        p.erase(name);
    } else {
        TagLib::StringList list;
        for (const QString &v : values)
            list.append(toTag(v));
        p.replace(name, list);
    }
    file.setProperties(p);
    return file.save();
}

QStringList withFirst(QStringList values, const QString &value) {
    if (values.isEmpty()) {
        if (!value.isEmpty())
            values << value;
    } else if (value.isEmpty()) {
        values.removeFirst();
    } else {
        values[0] = value;
    }
    return values;
}

bool writeField(const QString &path, const QString &field, const QString &value) {
    QStringList values;
    return readValues(path, field, values) && writeValues(path, field, withFirst(values, value));
}

}

using namespace Doctor;

LibraryDoctor::LibraryDoctor(Library *library, const QString &logFile, QObject *parent)
    : QObject(parent), m_library(library),
      m_logFile(logFile.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/halo/doctor.json"
                    : logFile) {
    loadLog();
}

QVariantList LibraryDoctor::findings() const {
    QVariantList out;
    for (const Finding &f : m_findings)
        out.append(QVariantMap{{"kind", kindName(f.kind)},
                               {"path", f.path},
                               {"file", QFileInfo(f.path).fileName()},
                               {"others", f.others},
                               {"field", f.field},
                               {"before", f.before},
                               {"after", f.after},
                               {"fixable", !f.field.isEmpty() && f.before != f.after}});
    return out;
}

int LibraryDoctor::fixable() const {
    return int(std::count_if(m_findings.begin(), m_findings.end(),
                             [](const Finding &f) { return !f.field.isEmpty() && f.before != f.after; }));
}

void LibraryDoctor::scan() {
    if (m_scanning || !m_library)
        return;
    m_scanning = true;
    emit changed();
    QStringList paths;
    for (const Track &t : m_library->tracks())
        paths << t.path;
    auto *watcher = new QFutureWatcher<std::vector<Finding>>(this);
    connect(watcher, &QFutureWatcher<std::vector<Finding>>::finished, this, [this, watcher] {
        m_findings = watcher->result();
        watcher->deleteLater();
        m_scanning = false;
        emit changed();
    });
    // Reading every file's tags takes a few seconds on a large library, so it
    // happens off the GUI thread, at low priority.
    watcher->setFuture(QtConcurrent::run([paths] {
        QThread::currentThread()->setPriority(QThread::LowestPriority);
        std::vector<File> files;
        files.reserve(size_t(paths.size()));
        for (const QString &p : paths) {
            File f{p, {}};
            if (readTags(p, f.tags))
                files.push_back(f);
        }
        return diagnose(files);
    }));
}

bool LibraryDoctor::apply(const Finding &f) {
    if (f.field.isEmpty() || f.before == f.after)
        return false;
    // The file must still say what it said at the scan.
    Tags now;
    if (!readTags(f.path, now) || field(now, f.field) != f.before) {
        emit message(tr("%1 changed since the scan; scan again.").arg(QFileInfo(f.path).fileName()));
        return false;
    }
    // Only the value the finding is about changes: a field can hold several
    // (a FLAC with two ARTIST credits), and the others stay as they are. The
    // whole list goes in the log, so undo puts back exactly what was there.
    QStringList all;
    const QStringList next = readValues(f.path, f.field, all) ? withFirst(all, f.after) : QStringList();
    if (next == all || !writeValues(f.path, f.field, next)) {
        emit message(tr("Couldn't write %1.").arg(QFileInfo(f.path).fileName()));
        return false;
    }
    m_log.push_back({f.path, f.field, f.before, f.after, true, all, next});
    return true;
}

bool LibraryDoctor::fix(int index) {
    if (index < 0 || index >= int(m_findings.size()))
        return false;
    const bool ok = apply(m_findings[size_t(index)]);
    if (ok) {
        m_findings.erase(m_findings.begin() + index);
        saveLog();
        if (m_library)
            m_library->rescan();
        emit changed();
    }
    return ok;
}

int LibraryDoctor::fixAll(const QString &kind) {
    int done = 0;
    for (int i = int(m_findings.size()) - 1; i >= 0; --i)
        if (kindName(m_findings[size_t(i)].kind) == kind && apply(m_findings[size_t(i)])) {
            m_findings.erase(m_findings.begin() + i);
            ++done;
        }
    if (done > 0) {
        saveLog();
        if (m_library)
            m_library->rescan();
        emit changed();
        emit message(tr("Fixed %n tag(s). U in the Doctor undoes the last.", "", done));
    }
    return done;
}

bool LibraryDoctor::undoLast() {
    if (m_log.empty())
        return false;
    const Entry e = m_log.back();
    // The file must still hold what the fix left. An entry from before Halo
    // logged whole lists is checked, and put back, by its first value.
    Tags now;
    QStringList all;
    const bool same = e.whole ? readValues(e.path, e.field, all) && all == e.afterAll
                              : readTags(e.path, now) && field(now, e.field) == e.after;
    if (!same) {
        emit message(tr("%1 changed since the fix; not undone.").arg(QFileInfo(e.path).fileName()));
        m_log.pop_back();
        saveLog();
        emit changed();
        return false;
    }
    const bool ok = e.whole ? writeValues(e.path, e.field, e.beforeAll) : writeField(e.path, e.field, e.before);
    m_log.pop_back();
    saveLog();
    if (ok && m_library)
        m_library->rescan();
    emit changed();
    return ok;
}

void LibraryDoctor::loadLog() {
    QFile f(m_logFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const QJsonValue &v : QJsonDocument::fromJson(f.read(8 * 1024 * 1024)).object().value("fixes").toArray()) {
        const QJsonObject o = v.toObject();
        Entry e{o.value("path").toString(), o.value("field").toString(), o.value("before").toString(),
                o.value("after").toString(), o.contains("beforeAll") && o.contains("afterAll"), {}, {}};
        for (const QJsonValue &x : o.value("beforeAll").toArray())
            e.beforeAll << x.toString();
        for (const QJsonValue &x : o.value("afterAll").toArray())
            e.afterAll << x.toString();
        m_log.push_back(e);
    }
}

void LibraryDoctor::saveLog() const {
    QDir().mkpath(QFileInfo(m_logFile).absolutePath());
    QSaveFile f(m_logFile);
    if (!f.open(QIODevice::WriteOnly))
        return;
    QJsonArray fixes;
    for (const Entry &e : m_log) {
        QJsonObject o{{"path", e.path}, {"field", e.field}, {"before", e.before}, {"after", e.after}};
        if (e.whole) {
            o.insert("beforeAll", QJsonArray::fromStringList(e.beforeAll));
            o.insert("afterAll", QJsonArray::fromStringList(e.afterAll));
        }
        fixes.append(o);
    }
    f.write(QJsonDocument(QJsonObject{{"fixes", fixes}}).toJson());
    f.commit();
}
