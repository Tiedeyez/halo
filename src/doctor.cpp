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

// "Lil Wayne Feat. Bun B" is Lil Wayne's song: the credit before any guest.
QString leadArtist(const QString &artist) {
    static const QRegularExpression guest(R"(\s*[\(\[]?\s*\b(feat|ft|featuring)\b\.?.*$)",
                                          QRegularExpression::CaseInsensitiveOption);
    QString lead = artist;
    lead.remove(guest);
    lead = lead.trimmed();
    return lead.isEmpty() ? artist.trimmed() : lead;
}

// Only the sites' names taken out, the other words kept: "DJ Drop
// Mixtapetrap.com" is "DJ Drop".
QString withoutSites(const QString &text) {
    QString s = text;
    s.remove(siteExpression());
    static const QRegularExpression emptyBrackets(R"([\(\[\{]\s*[\)\]\}])");
    s.remove(emptyBrackets);
    static const QRegularExpression edges(R"(^[\s\-|/~:]+|[\s\-|/~:]+$)");
    s.remove(edges);
    return s.simplified();
}

// A scene release names its files "01-artist_name-song_title-GRP.mp3": the
// number, then the artist, each closed by a hyphen with no space around it.
QString sceneArtist(const QString &path) {
    static const QRegularExpression scene(R"(^\d{1,3}-([^-\s][^-]*)-[^-\s])");
    const auto m = scene.match(QFileInfo(path).completeBaseName());
    if (!m.hasMatch())
        return {};
    QString artist = m.captured(1);
    artist.replace('_', ' ');
    return Library::capitalised(artist.simplified());
}

// A name as its words, small letters and digits only: "Jay-Z" is jay, z.
QStringList wordsOf(const QString &text) {
    static const QRegularExpression gap(R"([^\p{L}\p{N}]+)");
    return text.toLower().split(gap, Qt::SkipEmptyParts);
}

// Where `part` sits whole inside `all`, or -1.
int findWords(const QStringList &all, const QStringList &part, int from = 0) {
    for (int i = from; i + part.size() <= all.size(); ++i)
        if (all.mid(i, part.size()) == part)
            return i;
    return -1;
}

// An artist the library already knows: named by the tags of several songs.
struct KnownArtist {
    QStringList words;
    QString name;
    int songs = 0;
};

// The known artist a folder's name opens with: "lil-wayne-homecoming-2011"
// is Lil Wayne's once other songs carry that name. The longest name wins.
// A one-word name could as well be the album's first word ("Future
// Nostalgia"), so it counts only when a dash sets it apart ("Drake - So Far
// Gone") or it is the whole name.
const KnownArtist *folderNameArtist(const QString &folder, const QList<KnownArtist> &known) {
    QString name = QFileInfo(folder).fileName();
    name.replace('_', ' ');
    const QStringList words = wordsOf(name);
    static const QRegularExpression dashed(R"(^\s*\S+\s+-\s+\S)");
    const bool setApart = dashed.match(name).hasMatch();
    const KnownArtist *best = nullptr;
    for (const KnownArtist &a : known) {
        if (a.words.size() > words.size() || words.mid(0, a.words.size()) != a.words)
            continue;
        if (a.words.size() == 1 && words.size() > 1 && !setApart)
            continue;
        if (!best || a.words.size() > best->words.size()
            || (a.words.size() == best->words.size() && a.songs > best->songs))
            best = &a;
    }
    return best;
}

// Whether a file's own name credits someone else: a name the library's tags
// mention ("14. Pandemonium Rick Ross.mp3" in a Lil Wayne folder), or an
// "Artist-Song" pair where neither side is the folder's artist ("14 Erykah
// Badu-Jump Up In The Air"). Guests in brackets don't count.
bool namesAnotherArtist(const QString &path, const KnownArtist &own, const QList<QStringList> &mentioned) {
    QString base = QFileInfo(path).completeBaseName();
    static const QRegularExpression bracketed(R"([\(\[\{][^\)\]\}]*[\)\]\}]?)");
    base.replace(bracketed, " ");
    static const QRegularExpression number(R"(^\s*\d{1,3}[\s.\-_]+)");
    base.remove(number);
    if (const int dash = base.indexOf('-'); dash >= 0
        && wordsOf(base.left(dash)) != own.words && wordsOf(base.mid(dash + 1)) != own.words)
        return true;
    QStringList words = wordsOf(base);
    for (int at = findWords(words, own.words); at >= 0; at = findWords(words, own.words, at)) {
        words.remove(at, own.words.size());
        words.insert(at, QString()); // so the words on either side don't join
    }
    for (const QStringList &name : mentioned)
        if (name != own.words && findWords(words, name) >= 0)
            return true;
    return false;
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
    QHash<QString, QList<const File *>> byFolder;
    for (const File &f : files)
        byFolder[QFileInfo(f.path).absolutePath()].append(&f);
    // A folder's artist, for the songs in it that have none: the lead artist
    // of at least two thirds of its songs that name one, when at least half
    // of the folder does.
    QHash<QString, QString> folderArtist;
    for (auto it = byFolder.cbegin(); it != byFolder.cend(); ++it) {
        QHash<QString, int> counts;
        QHash<QString, QString> spelling;
        int named = 0;
        for (const File *f : *it) {
            const QString lead = leadArtist(stripWatermark(f->tags.artist));
            if (lead.isEmpty())
                continue;
            ++named;
            ++counts[key(lead)];
            if (!spelling.contains(key(lead)))
                spelling.insert(key(lead), lead);
        }
        if (named * 2 < it->size())
            continue;
        for (auto c = counts.cbegin(); c != counts.cend(); ++c)
            if (c.value() * 3 >= named * 2)
                folderArtist.insert(it.key(), spelling.value(c.key()));
    }
    // The artists this library knows: a lead artist on at least three songs.
    // A folder whose name opens with one of them is taken as that artist's.
    QList<KnownArtist> known;
    {
        QHash<QString, int> at;
        for (const File &f : files) {
            const QString lead = leadArtist(stripWatermark(f.tags.artist));
            const QStringList words = wordsOf(lead);
            if (words.isEmpty())
                continue;
            const QString k = words.join(' ');
            if (!at.contains(k)) {
                at.insert(k, int(known.size()));
                known.append({words, lead, 0});
            }
            ++known[at.value(k)].songs;
        }
        known.removeIf([](const KnownArtist &a) { return a.songs < 3; });
    }
    // Every name the artist tags mention, guests and partners too, to tell
    // when a file's own name credits someone other than its folder's artist.
    // A one-word name is as likely a word of the title, so it needs three
    // mentions.
    QList<QStringList> mentioned;
    {
        static const QRegularExpression joiner(
            R"(\s*(?:&|,|/|;|\(|\)|\[|\]|\b(?:and|x|with|feat|ft|featuring)\b\.?)\s*)",
            QRegularExpression::CaseInsensitiveOption);
        QHash<QString, int> count;
        QHash<QString, QStringList> wordsFor;
        for (const File &f : files)
            for (const QString &part : stripWatermark(f.tags.artist).split(joiner, Qt::SkipEmptyParts)) {
                const QStringList words = wordsOf(part);
                if (words.isEmpty())
                    continue;
                const QString k = words.join(' ');
                ++count[k];
                wordsFor.insert(k, words);
            }
        for (auto it = count.cbegin(); it != count.cend(); ++it)
            if (it.value() >= 3 || wordsFor.value(it.key()).size() > 1)
                mentioned.append(wordsFor.value(it.key()));
    }
    QHash<QString, const KnownArtist *> namedFor;
    for (auto it = byFolder.cbegin(); it != byFolder.cend(); ++it)
        if (!folderArtist.contains(it.key()))
            if (const KnownArtist *a = folderNameArtist(it.key(), known))
                namedFor.insert(it.key(), a);
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
            // From the file's name, without a site's stamp or the artist said
            // again; failing that, the tag's own words without the stamp.
            QString named = Library::cleanTitle(stripWatermark(Library::titleFromFileName(f.path)), t.artist);
            if (named.isEmpty() || named.compare(t.artist, Qt::CaseInsensitive) == 0)
                named = withoutSites(t.title);
            const bool usable = !named.isEmpty() && named != t.title;
            out.push_back({Kind::MissingTitle, f.path, {}, usable ? "title" : QString(), t.title, usable ? named : QString()});
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
            // The album artist, "Artist - Song" in the file name, a scene
            // release's "01-artist-song", the artist nearly every other
            // song in the folder has, or the known artist the folder's name
            // opens with. Otherwise it is only pointed out.
            QString artist = hasWatermark(t.albumArtist) ? QString() : t.albumArtist;
            if (artist.isEmpty()) {
                const QString name = Library::titleFromFileName(f.path);
                const int dash = name.indexOf(" - ");
                if (dash > 0)
                    artist = stripWatermark(name.left(dash).trimmed());
            }
            if (artist.isEmpty())
                artist = sceneArtist(f.path);
            if (artist.isEmpty())
                artist = folderArtist.value(QFileInfo(f.path).absolutePath());
            if (artist.isEmpty())
                if (const KnownArtist *a = namedFor.value(QFileInfo(f.path).absolutePath());
                    a && !namesAnotherArtist(f.path, *a, mentioned))
                    artist = a->name;
            out.push_back({Kind::MissingArtist, f.path, {}, artist.isEmpty() ? QString() : "artist", t.artist, artist});
        }
        if (albumNow.isEmpty())
            out.push_back({Kind::MissingAlbum, f.path, {}, "album", t.album,
                           Library::prettyFolderName(QFileInfo(f.path).absolutePath())});
    }

    // Album artists that disagree inside one folder. Guests don't count as
    // a different artist ("Lil Wayne Feat. Bun B" is Lil Wayne's). The lead
    // artist of two thirds of the folder's songs is the album's. So is a
    // joint credit ("Juelz Santana & Lil Wayne") when two thirds of the
    // songs are by the pair or by one of them. With neither, the tape is a
    // compilation: "Various Artists".
    for (auto it = byFolder.cbegin(); it != byFolder.cend(); ++it) {
        QHash<QString, int> counts;                      // by lead artist, spelling aside
        QHash<QString, QHash<QString, int>> spellings;   // each lead's spellings
        QSet<QString> exact;
        int total = 0;
        for (const File *f : *it) {
            const QString &a = f->tags.albumArtist;
            if (a.isEmpty() || hasWatermark(a))
                continue;
            const QString lead = leadArtist(a);
            ++total;
            exact.insert(a);
            ++counts[key(lead)];
            ++spellings[key(lead)][lead];
        }
        if (exact.size() < 2)
            continue;
        // In a stable order: most songs first, then by name.
        QStringList leads = counts.keys();
        std::sort(leads.begin(), leads.end(), [&counts](const QString &x, const QString &y) {
            return counts[x] != counts[y] ? counts[x] > counts[y] : x < y;
        });
        auto spelled = [&spellings](const QString &lead) {
            QString bestName;
            int most = 0;
            const auto &names = spellings[lead];
            for (auto n = names.cbegin(); n != names.cend(); ++n)
                if (n.value() > most || (n.value() == most && n.key() < bestName))
                    most = n.value(), bestName = n.key();
            return bestName;
        };
        QString common = QStringLiteral("Various Artists");
        if (counts[leads.first()] * 3 >= total * 2) {
            common = spelled(leads.first());
        } else {
            static const QRegularExpression joiner(R"(\s*(?:&|,|/|\band\b|\bx\b)\s*)",
                                                   QRegularExpression::CaseInsensitiveOption);
            for (const QString &lead : leads) {
                const QString name = spelled(lead);
                const QStringList members = name.split(joiner, Qt::SkipEmptyParts);
                if (members.size() < 2)
                    continue;
                int support = counts[lead];
                QSet<QString> seen{lead};
                for (const QString &m : members)
                    if (!seen.contains(key(m))) {
                        seen.insert(key(m));
                        support += counts.value(key(m));
                    }
                if (support * 3 >= total * 2) {
                    common = name;
                    break;
                }
            }
        }
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
        return values;
    }
    // A value stored twice is one value (TagLib reads an ID3 comment kept
    // in two kinds of frame as two): every copy of the first changes.
    const QString old = values.first();
    if (value.isEmpty())
        values.removeAll(old);
    else
        for (QString &v : values)
            if (v == old)
                v = value;
    return values;
}

QStringList distinct(const QStringList &values) {
    QStringList out;
    for (const QString &v : values)
        if (!out.contains(v))
            out << v;
    return out;
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
    // Windows Media files don't hand a list of values back in the order it
    // was given, so a field with several can't be changed safely there.
    const QString suffix = QFileInfo(f.path).suffix().toLower();
    if (distinct(all).size() > 1 && (suffix == "wma" || suffix == "asf")) {
        emit message(tr("%1 has several values in that field, which Halo can't change safely in this format.")
                         .arg(QFileInfo(f.path).fileName()));
        return false;
    }
    if (next == all || !writeValues(f.path, f.field, next)) {
        emit message(tr("Couldn't write %1.").arg(QFileInfo(f.path).fileName()));
        return false;
    }
    // What the file holds now is what counts: it must show the fix, and it
    // is what undo will look for. If the format didn't take the list as
    // given, the old one goes back and the fix is refused.
    QStringList stored;
    Tags now2;
    if (!readValues(f.path, f.field, stored) || !readTags(f.path, now2) || field(now2, f.field) != f.after
        || distinct(stored) != distinct(next)) {
        writeValues(f.path, f.field, all);
        emit message(tr("%1 didn't keep the change; left as it was.").arg(QFileInfo(f.path).fileName()));
        return false;
    }
    m_log.push_back({f.path, f.field, f.before, f.after, true, all, stored});
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
    // (Copies of a value don't count: writing another field can add one.)
    const bool same = e.whole ? readValues(e.path, e.field, all) && distinct(all) == distinct(e.afterAll)
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
    // A log Halo can't read (far too large, or not what it wrote) is set
    // aside under another name, never cut short and written over: it is the
    // only record of what the tags said before.
    const QByteArray bytes = f.read(MaxLogBytes + 1);
    f.close();
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (bytes.size() > MaxLogBytes || (!bytes.trimmed().isEmpty() && (error.error != QJsonParseError::NoError || !doc.isObject()))) {
        const QString aside = m_logFile + ".unreadable";
        QFile::remove(aside);
        QFile::rename(m_logFile, aside);
        return;
    }
    for (const QJsonValue &v : doc.object().value("fixes").toArray()) {
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
    // The newest MaxLogEntries fixes are kept, so the file stays well inside
    // what loadLog will read back.
    const size_t first = m_log.size() > MaxLogEntries ? m_log.size() - MaxLogEntries : 0;
    for (size_t i = first; i < m_log.size(); ++i) {
        const Entry &e = m_log[i];
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
