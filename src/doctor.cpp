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
    case Kind::UploadLabel: return "labels";
    case Kind::NumberedTitle: return "numbered";
    case Kind::MissingNumber: return "nonumber";
    case Kind::ArtistSpelling: return "spelling";
    case Kind::MixedAlbum: return "album";
    case Kind::Case: return "case";
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
    if (name == "tracknumber") return t.number > 0 ? QString::number(t.number) : QString();
    return {};
}

namespace {
// A tag that says nothing: "Unknown Artist", "Untitled", "<unknown>".
bool isPlaceholder(const QString &text) {
    static const QRegularExpression empty(
        R"(^\s*<?\s*(unknown(\s+(artist|album|title|track))?|untitled|no\s+(artist|album|title)|n/?a|none)\s*>?\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    return empty.match(text).hasMatch();
}

bool isVarious(const QString &text) {
    const QString k = key(text);
    return k == "variousartists" || k == "various" || k == "va";
}

// What a video site or a download tool adds to a name: "(Official Video)",
// "[Lyrics]", "(HD)", and on a file's name the video's own id in square
// brackets. The words of the song stay.
QString stripLabels(const QString &text, bool fileName) {
    static const QRegularExpression label(
        R"(\s*[\(\[]\s*(?:(?:the\s+)?official\s+)?(?:(?:music|lyrics?|hd|hq|4k)\s+)?(?:video|audio|visuali[sz]er|lyrics?|hd|hq|4k|1080p|720p|\d{3}\s?kbps)(?:\s+(?:video|hd|hq|4k))?\s*[\)\]])",
        QRegularExpression::CaseInsensitiveOption);
    QString s = text;
    s.remove(label);
    bool removed = s != text;
    if (fileName) {
        // Eleven letters, digits, - or _ in square brackets at the very
        // end, and not a plain word ("[Alternative]").
        static const QRegularExpression id(R"(\s*\[([A-Za-z0-9_-]{11})\]\s*$)");
        if (const auto m = id.match(s); m.hasMatch()) {
            const QString inner = m.captured(1);
            static const QRegularExpression random(R"([0-9_-]|[a-z][A-Z])");
            if (random.match(inner).hasMatch()) {
                s.remove(m.capturedStart(), m.capturedLength());
                removed = true;
            }
        }
    }
    s = s.trimmed();
    return removed && !s.isEmpty() ? s : text;
}

// "Track 07", "07": a title that is only its place on the album.
bool isOnlyANumber(const QString &title, bool *named = nullptr) {
    static const QRegularExpression only(R"(^\s*(track|audiotrack|audio track|titel|pista|piste)?\s*#?\d{1,3}\s*$)",
                                         QRegularExpression::CaseInsensitiveOption);
    const auto m = only.match(title);
    if (named)
        *named = m.hasMatch() && !m.captured(1).isEmpty();
    return m.hasMatch();
}

// A name written all in capitals or all in small letters, set in ordinary
// capitals; empty when it is fine as it is or too short to tell ("ABBA").
QString recased(const QString &text) {
    int upper = 0, lower = 0;
    for (const QChar c : text) {
        if (c.isUpper()) ++upper;
        else if (c.isLower()) ++lower;
    }
    if (upper > 0 && lower > 0)
        return {};
    if (lower > 0 ? lower < 3 : (upper < 5 || !text.simplified().contains(' ')))
        return {};
    // A capital on each word, a word starting after a space, a hyphen, a
    // slash or an opening bracket; "dj" and "mc" and roman numerals whole.
    QString fixed = text.toLower();
    static const QRegularExpression word(R"((?<![\p{L}\p{N}'’.])([\p{L}])([\p{L}\p{N}'’]*))");
    auto it = word.globalMatch(fixed);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString w = m.captured(0);
        if (w == "dj" || w == "mc" || w == "ii" || w == "iii" || w == "iv")
            fixed.replace(m.capturedStart(0), w.size(), w.toUpper());
        else
            fixed[m.capturedStart(1)] = fixed[m.capturedStart(1)].toUpper();
    }
    return fixed == text ? QString() : fixed;
}

// The number a file's name opens with ("07 Song.mp3", "07-artist-song.mp3"),
// or 0. Three digits and more are a disc and a track, or a year.
int fileNumber(const QString &path) {
    static const QRegularExpression number(R"(^\s*(\d{1,2})(?=[\s.\-_\)]))");
    if (const auto m = number.match(QFileInfo(path).fileName()); m.hasMatch())
        return m.captured(1).toInt();
    // "Some Tape - Track 07.mp3", "07.mp3".
    static const QRegularExpression last(R"((?:^|\btrack\s*#?)(\d{1,2})\s*$)", QRegularExpression::CaseInsensitiveOption);
    const auto m = last.match(QFileInfo(path).completeBaseName());
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

// "Album (Disc 2)" and "Album" are one album for telling albums apart.
QString albumKey(const QString &album) {
    static const QRegularExpression disc(R"([\(\[]?\b(disc|disk|cd|part|pt|vol|volume)\b\.?\s*\d+[\)\]]?)",
                                         QRegularExpression::CaseInsensitiveOption);
    QString a = album;
    a.remove(disc);
    const QString k = key(a);
    return k.isEmpty() ? key(album) : k;
}

// A folder people drop files into, whatever is in them.
bool isDumpFolder(const QString &folder, const QSet<QString> &roots) {
    if (roots.contains(folder) || folder == QDir::homePath() || folder == QDir::rootPath())
        return true;
    static const QSet<QString> names{"downloads", "download", "desktop", "documents", "music", "musik", "musique",
                                     "musica", "newfolder", "misc", "unsorted", "singles", "random", "mp3", "mp3s",
                                     "songs", "tracks", "audio", "temp", "tmp", "stuff", "incoming"};
    return names.contains(key(QFileInfo(folder).fileName()));
}

// What a folder says about the songs in it.
struct Folder {
    QList<const File *> files;
    bool dump = false;   // a place files are dropped: its name means nothing
    bool loose = false;  // not one album: nothing here speaks for its neighbours
    bool numbered = false;
    QString artist;      // the lead artist of two thirds of its songs
    QString album;       // the album two thirds of its tagged songs name
    bool albumByMost = false; // ...and at least half the folder carries one
    const KnownArtist *named = nullptr;
    QHash<int, int> numbers; // how many files claim each track number
};
}

std::vector<Finding> diagnose(const std::vector<File> &files, const QStringList &libraryRoots) {
    std::vector<Finding> out;
    QSet<QString> roots;
    for (const QString &r : libraryRoots)
        roots.insert(QDir::cleanPath(r));
    QHash<QString, Folder> folders;
    for (const File &f : files)
        folders[QFileInfo(f.path).absolutePath()].files.append(&f);

    // The artists this library knows: a lead artist on at least three songs,
    // under the spelling most of those songs use.
    QList<KnownArtist> known;
    QHash<QString, QString> canonical; // key(lead) -> the spelling to use
    {
        QHash<QString, QHash<QString, int>> spellings;
        for (const File &f : files) {
            const QString a = stripWatermark(f.tags.artist);
            if (a.isEmpty() || isPlaceholder(a))
                continue;
            const QString lead = leadArtist(a);
            if (!key(lead).isEmpty())
                ++spellings[key(lead)][lead];
        }
        QStringList keys = spellings.keys();
        std::sort(keys.begin(), keys.end());
        for (const QString &k : keys) {
            const auto &names = spellings[k];
            QString best;
            int most = 0, total = 0;
            for (auto n = names.cbegin(); n != names.cend(); ++n) {
                total += n.value();
                // Most songs; then a name in ordinary capitals over one all
                // in capitals or small letters; then by name.
                const bool plainer = recased(n.key()).isEmpty() && !recased(best).isEmpty();
                if (n.value() > most || (n.value() == most && (plainer || (recased(n.key()).isEmpty() == recased(best).isEmpty() && n.key() < best))))
                    most = n.value(), best = n.key();
            }
            if (names.size() > 1)
                canonical.insert(k, best);
            if (total >= 3 && !wordsOf(best).isEmpty())
                known.append({wordsOf(best), best, total});
        }
    }
    auto knownArtist = [&known](const QString &name) -> const KnownArtist * {
        const QStringList words = wordsOf(name);
        for (const KnownArtist &a : known)
            if (a.words == words)
                return &a;
        return nullptr;
    };
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

    for (auto it = folders.begin(); it != folders.end(); ++it) {
        Folder &d = *it;
        d.dump = isDumpFolder(it.key(), roots);
        // Numbered like an album: most files carry a track number, and the
        // numbers are nearly all different.
        // The file names count on their own: on a tape put together from
        // other releases each tag still holds its old album's number.
        QSet<int> byName, byTag;
        int named = 0, tagged = 0;
        for (const File *f : d.files) {
            const int inName = fileNumber(f->path), inTag = f->tags.number;
            if (inName > 0)
                ++named, byName.insert(inName);
            if (inTag > 0)
                ++tagged, byTag.insert(inTag);
            if (const int n = inTag > 0 ? inTag : inName; n > 0)
                ++d.numbers[n];
        }
        auto inTurn = [&d](int with, qsizetype different) {
            return with * 10 >= d.files.size() * 6 && different * 10 >= with * 8;
        };
        d.numbered = inTurn(named, byName.size()) || inTurn(tagged, byTag.size());
        // The album two thirds of the tagged songs name.
        QHash<QString, int> albums;
        QHash<QString, QHash<QString, int>> albumSpellings;
        int withAlbum = 0;
        for (const File *f : d.files) {
            const QString a = stripWatermark(f->tags.album);
            if (a.isEmpty() || isPlaceholder(a))
                continue;
            ++withAlbum;
            ++albums[albumKey(a)];
            ++albumSpellings[albumKey(a)][a];
        }
        for (auto a = albums.cbegin(); a != albums.cend(); ++a)
            if (a.value() >= 2 && a.value() * 3 >= withAlbum * 2) {
                int most = 0;
                const auto &names = albumSpellings[a.key()];
                for (auto n = names.cbegin(); n != names.cend(); ++n)
                    if (n.value() > most || (n.value() == most && n.key() < d.album))
                        most = n.value(), d.album = n.key();
                d.albumByMost = withAlbum * 2 >= d.files.size();
            }
        // One album, or a pile? A dump is a pile. So is a large folder whose
        // songs are neither numbered in turn nor agreed on an album.
        d.loose = d.dump || (d.files.size() >= 8 && !d.numbered && !d.albumByMost);
        if (d.dump)
            continue;
        // The lead artist of at least two thirds of its songs that name
        // one, when at least half of the folder does.
        if (!d.loose) {
            QHash<QString, int> counts;
            QHash<QString, QString> spelling;
            int namedSongs = 0;
            for (const File *f : d.files) {
                const QString a = stripWatermark(f->tags.artist);
                if (a.isEmpty() || isPlaceholder(a))
                    continue;
                const QString lead = leadArtist(a);
                ++namedSongs;
                ++counts[key(lead)];
                if (!spelling.contains(key(lead)))
                    spelling.insert(key(lead), canonical.value(key(lead), lead));
            }
            if (namedSongs * 2 >= d.files.size())
                for (auto c = counts.cbegin(); c != counts.cend(); ++c)
                    if (c.value() * 3 >= namedSongs * 2)
                        d.artist = spelling.value(c.key());
        }
        if (d.artist.isEmpty())
            d.named = folderNameArtist(it.key(), known);
    }

    for (const File &f : files) {
        const Tags &t = f.tags;
        const Folder &d = folders[QFileInfo(f.path).absolutePath()];
        auto add = [&](Kind kind, const QString &fieldName, const QString &before, const QString &after, bool sure,
                       const char *why) {
            Finding x{kind, f.path, {}, fieldName, before, after};
            x.sure = sure && !fieldName.isEmpty();
            x.why = QString::fromLatin1(why);
            out.push_back(x);
        };
        // At most one finding per field, so fixing one never makes another
        // stale. Watermarks on the fields that aren't checked further:
        for (const QString name : {"albumartist", "comment"}) {
            const QString v = field(t, name);
            if (!v.isEmpty() && hasWatermark(v))
                add(Kind::Watermark, name, v, stripWatermark(v), true, "tag");
        }

        // The artist first: the title's fix depends on who the song is by.
        // A tag that is nothing but a site's name, or "Unknown Artist",
        // counts as missing.
        QString artistNow = stripWatermark(t.artist);
        if (isPlaceholder(artistNow))
            artistNow.clear();
        const QString fromFile = stripLabels(stripWatermark(Library::titleFromFileName(f.path)), true);
        if (!artistNow.isEmpty() && artistNow != t.artist) {
            add(Kind::Watermark, "artist", t.artist, artistNow, true, "tag");
        } else if (artistNow.isEmpty()) {
            // In turn: the album artist; "Artist - Song" in the file's name
            // or the title; a scene release's "01-artist-song"; a known
            // artist the title opens or closes with; the artist nearly
            // every other song in the folder has; the known artist the
            // folder's name opens with. Otherwise it is only pointed out.
            QString artist;
            bool sure = false;
            const char *why = "";
            if (!t.albumArtist.isEmpty() && !hasWatermark(t.albumArtist) && !isPlaceholder(t.albumArtist)
                && !isVarious(t.albumArtist)) {
                artist = t.albumArtist, sure = true, why = "tag";
            }
            auto beforeDash = [&](const QString &name, const char *source) {
                const int dash = name.indexOf(" - ");
                if (!artist.isEmpty() || dash <= 0)
                    return;
                QString left = stripWatermark(name.left(dash).trimmed());
                static const QRegularExpression number(R"(^\s*\d{1,3}\s*[-._\)]*\s+)");
                left.remove(number);
                static const QRegularExpression letters(R"(\p{L})");
                if (left.isEmpty() || !letters.match(left).hasMatch() || isPlaceholder(left) || isVarious(left))
                    return;
                const KnownArtist *k = knownArtist(leadArtist(left));
                // A name the library knows is taken as the library spells it.
                artist = k ? k->name + left.mid(leadArtist(left).size()) : left;
                sure = k != nullptr;
                why = source;
            };
            beforeDash(fromFile, "filename");
            beforeDash(stripLabels(stripWatermark(t.title), false), "title");
            if (artist.isEmpty()) {
                artist = sceneArtist(f.path);
                if (!artist.isEmpty()) {
                    const KnownArtist *k = knownArtist(artist);
                    if (k)
                        artist = k->name;
                    sure = k != nullptr, why = "filename";
                }
            }
            if (artist.isEmpty()) {
                // "lil wayne freestyle", "BANG BANG-YOUNG JEEZY FEAT. X".
                QString title = stripWatermark(t.title);
                static const QRegularExpression bracketed(R"([\(\[\{][^\)\]\}]*[\)\]\}]?)");
                title.replace(bracketed, " ");
                const QStringList words = wordsOf(title);
                const KnownArtist *found = nullptr;
                for (const KnownArtist &a : known)
                    if (a.words.size() > 1 && a.words.size() < words.size() && words.mid(0, a.words.size()) == a.words
                        && (!found || a.words.size() > found->words.size()))
                        found = &a;
                if (!found)
                    if (const int dash = title.lastIndexOf('-'); dash > 0)
                        found = knownArtist(leadArtist(title.mid(dash + 1)));
                if (found)
                    artist = found->name, why = "title";
            }
            if (artist.isEmpty() && !d.artist.isEmpty())
                artist = d.artist, why = "folder-songs";
            if (artist.isEmpty() && d.named && !namesAnotherArtist(f.path, *d.named, mentioned))
                artist = d.named->name, why = "folder-name";
            add(Kind::MissingArtist, artist.isEmpty() ? QString() : "artist", t.artist, artist, sure, why);
        } else if (const QString lead = leadArtist(artistNow); canonical.contains(key(lead))
                   && canonical.value(key(lead)) != lead && artistNow.startsWith(lead)) {
            // The same artist written another way than most of the library
            // writes it. Capitals and spaces alone are certain; punctuation
            // ("Lil' Wayne") could be someone else.
            const QString usual = canonical.value(key(lead));
            add(Kind::ArtistSpelling, "artist", t.artist, usual + artistNow.mid(lead.size()),
                usual.toLower().simplified() == lead.toLower().simplified(), "library");
        } else if (const QString fixed = recased(artistNow); !fixed.isEmpty()) {
            add(Kind::Case, "artist", t.artist, fixed, false, "case");
        }

        // The title: watermark, labels and numbering are fixed together.
        const QString unmarked = stripWatermark(t.title);
        const QString title = stripLabels(unmarked, false);
        bool named = false;
        const bool onlyNumber = isOnlyANumber(title, &named);
        // From the file's name, without a site's stamp, a video label or the
        // artist said again.
        QString fromName = Library::cleanTitle(fromFile, t.artist);
        if (t.artist.isEmpty())
            if (const int dash = fromName.indexOf(" - "); dash > 0 && dash + 3 < fromName.size())
                fromName = fromName.mid(dash + 3).trimmed();
        if (fromName.compare(t.artist, Qt::CaseInsensitive) == 0 || isOnlyANumber(fromName) || isPlaceholder(fromName))
            fromName.clear();
        if (title.isEmpty() || isPlaceholder(title) || (onlyNumber && (named || !fromName.isEmpty()))) {
            // Failing the file's name, the tag's own words without the stamp.
            QString better = fromName;
            if (better.isEmpty() && title.isEmpty())
                better = withoutSites(t.title);
            const bool usable = !better.isEmpty() && better != t.title;
            add(Kind::MissingTitle, usable ? "title" : QString(), t.title, usable ? better : QString(), false, "filename");
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
                add(clean != title ? Kind::NumberedTitle : title != unmarked ? Kind::UploadLabel : Kind::Watermark,
                    "title", t.title, clean, true, "tag");
            else if (const QString fixed = recased(t.title); !fixed.isEmpty())
                add(Kind::Case, "title", t.title, fixed, false, "case");
        }

        // The album. In a pile of loose songs a song without one is a
        // single, and nothing is wrong with it.
        QString albumNow = stripWatermark(t.album);
        if (isPlaceholder(albumNow))
            albumNow.clear();
        if (!albumNow.isEmpty() && albumNow != t.album) {
            add(Kind::Watermark, "album", t.album, albumNow, true, "tag");
        } else if (albumNow.isEmpty()) {
            if (!d.loose) {
                const QString fromFolder = Library::prettyFolderName(QFileInfo(f.path).absolutePath());
                if (d.albumByMost)
                    add(Kind::MissingAlbum, "album", t.album, d.album, false, "folder-songs");
                else
                    add(Kind::MissingAlbum, fromFolder.isEmpty() ? QString() : "album", t.album, fromFolder, false,
                        "folder-name");
            }
        } else if (!d.loose && !d.album.isEmpty() && albumKey(albumNow) != albumKey(d.album)) {
            // An album name most of the folder doesn't share (often the
            // song's own name where the album belongs).
            add(Kind::MixedAlbum, "album", t.album, d.album, false, "folder-songs");
        }

        // A track number the file's name gives and the tag lacks.
        if (t.number <= 0 && !d.loose && d.numbered)
            if (const int n = fileNumber(f.path); n > 0)
                add(Kind::MissingNumber, "tracknumber", {}, QString::number(n), d.numbers.value(n) == 1, "filename");
    }

    // Album artists that disagree inside one folder. Guests don't count as
    // a different artist ("Lil Wayne Feat. Bun B" is Lil Wayne's). The lead
    // artist of two thirds of the folder's songs is the album's. So is a
    // joint credit ("Juelz Santana & Lil Wayne") when two thirds of the
    // songs are by the pair or by one of them. With neither, the tape is a
    // compilation: "Various Artists". A pile of loose songs is no album.
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        if (it->loose)
            continue;
        QHash<QString, int> counts;                      // by lead artist, spelling aside
        QHash<QString, QHash<QString, int>> spellings;   // each lead's spellings
        QSet<QString> exact;
        int total = 0;
        for (const File *f : it->files) {
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
        for (const File *f : it->files)
            if (!f->tags.albumArtist.isEmpty() && !hasWatermark(f->tags.albumArtist) && f->tags.albumArtist != common) {
                Finding x{Kind::MixedAlbumArtist, f->path, {}, "albumartist", f->tags.albumArtist, common};
                x.why = QStringLiteral("folder-songs");
                out.push_back(x);
            }
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
        // A folder's findings together, whatever its subfolders are called.
        const int slashA = a.path.lastIndexOf('/'), slashB = b.path.lastIndexOf('/');
        if (const int byFolder = QStringView(a.path).left(slashA).compare(QStringView(b.path).left(slashB)); byFolder != 0)
            return byFolder < 0;
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
    if (field == "tracknumber") return "TRACKNUMBER";
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

namespace {
bool fixableFinding(const Finding &f) { return !f.field.isEmpty() && f.before != f.after; }

// The field a typed answer goes to, for the kinds that take one.
QString answerField(Kind kind) {
    switch (kind) {
    case Kind::MissingArtist: return "artist";
    case Kind::MissingAlbum:
    case Kind::MixedAlbum: return "album";
    case Kind::MixedAlbumArtist: return "albumartist";
    default: return {};
    }
}
}

// The findings as the page shows them. Findings are in order of kind, then
// path, so a folder's are together; the first of each folder carries what
// its heading needs. Built once per change, not once per read.
QVariantList LibraryDoctor::findings() const {
    if (!m_viewStale)
        return m_view;
    m_view.clear();
    m_view.reserve(qsizetype(m_findings.size()));
    for (size_t i = 0; i < m_findings.size();) {
        const QString folder = QFileInfo(m_findings[i].path).absolutePath();
        size_t end = i;
        int fixes = 0;
        while (end < m_findings.size() && m_findings[end].kind == m_findings[i].kind
               && QFileInfo(m_findings[end].path).absolutePath() == folder) {
            fixes += fixableFinding(m_findings[end]);
            ++end;
        }
        for (size_t n = i; n < end; ++n) {
            const Finding &f = m_findings[n];
            m_view.append(QVariantMap{{"kind", kindName(f.kind)},
                                      {"path", f.path},
                                      {"file", QFileInfo(f.path).fileName()},
                                      {"folder", folder},
                                      {"others", f.others},
                                      {"field", f.field},
                                      {"before", f.before},
                                      {"after", f.after},
                                      {"fixable", fixableFinding(f)},
                                      {"sure", f.sure},
                                      {"why", f.why},
                                      {"groupFirst", n == i},
                                      {"groupSize", int(end - i)},
                                      {"groupFixes", fixes},
                                      {"answerable", !answerField(f.kind).isEmpty()}});
        }
        i = end;
    }
    m_viewStale = false;
    return m_view;
}

QVariantMap LibraryDoctor::counts() const {
    QHash<QString, int> n;
    for (const Finding &f : m_findings) {
        ++n[kindName(f.kind)];
        if (f.sure && fixableFinding(f))
            ++n[kindName(f.kind) + "/sure"];
    }
    QVariantMap out;
    for (auto it = n.cbegin(); it != n.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

int LibraryDoctor::fixable() const {
    return int(std::count_if(m_findings.begin(), m_findings.end(), fixableFinding));
}

void LibraryDoctor::changedFindings() {
    m_viewStale = true;
    emit changed();
}

void LibraryDoctor::scan() {
    if (m_scanning || !m_library)
        return;
    m_scanning = true;
    emit changed();
    QStringList paths;
    for (const Track &t : m_library->tracks())
        paths << t.path;
    const QStringList roots = m_library->folders();
    auto *watcher = new QFutureWatcher<std::vector<Finding>>(this);
    connect(watcher, &QFutureWatcher<std::vector<Finding>>::finished, this, [this, watcher] {
        m_findings = watcher->result();
        watcher->deleteLater();
        m_scanning = false;
        changedFindings();
    });
    // Reading every file's tags takes a few seconds on a large library, so it
    // happens off the GUI thread, at low priority.
    watcher->setFuture(QtConcurrent::run([paths, roots] {
        QThread::currentThread()->setPriority(QThread::LowestPriority);
        std::vector<File> files;
        files.reserve(size_t(paths.size()));
        for (const QString &p : paths) {
            File f{p, {}};
            if (readTags(p, f.tags))
                files.push_back(f);
        }
        return diagnose(files, roots);
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
    m_log.push_back({f.path, f.field, f.before, f.after, true, all, stored, m_batch});
    return true;
}

bool LibraryDoctor::fix(int index) {
    if (index < 0 || index >= int(m_findings.size()))
        return false;
    ++m_batch;
    const bool ok = apply(m_findings[size_t(index)]);
    if (ok) {
        m_findings.erase(m_findings.begin() + index);
        saveLog();
        if (m_library)
            m_library->rescan();
        changedFindings();
    }
    return ok;
}

void LibraryDoctor::finishBatch(int done) {
    if (done <= 0)
        return;
    saveLog();
    if (m_library)
        m_library->rescan();
    changedFindings();
    emit message(tr("Fixed %n tag(s). U in the Doctor undoes them.", "", done));
}

int LibraryDoctor::fixAll(const QString &kind) {
    ++m_batch;
    int done = 0;
    for (int i = int(m_findings.size()) - 1; i >= 0; --i)
        if (m_findings[size_t(i)].sure && kindName(m_findings[size_t(i)].kind) == kind
            && apply(m_findings[size_t(i)])) {
            m_findings.erase(m_findings.begin() + i);
            ++done;
        }
    finishBatch(done);
    return done;
}

int LibraryDoctor::fixGroup(const QString &kind, const QString &folder) {
    ++m_batch;
    int done = 0;
    for (int i = int(m_findings.size()) - 1; i >= 0; --i) {
        const Finding &f = m_findings[size_t(i)];
        if (kindName(f.kind) == kind && QFileInfo(f.path).absolutePath() == folder && apply(f)) {
            m_findings.erase(m_findings.begin() + i);
            ++done;
        }
    }
    finishBatch(done);
    return done;
}

int LibraryDoctor::setGroup(const QString &kind, const QString &folder, const QString &value) {
    // One line of ordinary text, no longer than a tag reasonably is.
    QString answer;
    for (const QChar c : value)
        answer += c.category() == QChar::Other_Control ? QChar(' ') : c;
    answer = answer.simplified().left(MaxAnswer);
    if (answer.isEmpty())
        return 0;
    ++m_batch;
    int done = 0;
    for (int i = int(m_findings.size()) - 1; i >= 0; --i) {
        Finding f = m_findings[size_t(i)];
        const QString field = answerField(f.kind);
        if (field.isEmpty() || kindName(f.kind) != kind || QFileInfo(f.path).absolutePath() != folder)
            continue;
        f.field = field;
        f.after = answer;
        if (apply(f)) {
            m_findings.erase(m_findings.begin() + i);
            ++done;
        }
    }
    finishBatch(done);
    return done;
}

bool LibraryDoctor::undoLast() {
    if (m_log.empty())
        return false;
    // Everything the last press wrote, newest first.
    const qint64 batch = m_log.back().batch;
    int undone = 0, left = 0;
    QString leftFile;
    do {
        const Entry e = m_log.back();
        m_log.pop_back();
        // The file must still hold what the fix left. An entry from before
        // Halo logged whole lists is checked, and put back, by its first value.
        Tags now;
        QStringList all;
        // (Copies of a value don't count: writing another field can add one.)
        const bool same = e.whole ? readValues(e.path, e.field, all) && distinct(all) == distinct(e.afterAll)
                                  : readTags(e.path, now) && field(now, e.field) == e.after;
        if (same && (e.whole ? writeValues(e.path, e.field, e.beforeAll) : writeField(e.path, e.field, e.before))) {
            ++undone;
        } else {
            ++left;
            leftFile = QFileInfo(e.path).fileName();
        }
    } while (batch != 0 && !m_log.empty() && m_log.back().batch == batch);
    saveLog();
    if (left == 1)
        emit message(tr("%1 changed since the fix; not undone.").arg(leftFile));
    else if (left > 1)
        emit message(tr("%n file(s) changed since the fix; not undone.", "", left));
    else if (undone > 1)
        emit message(tr("Undid %n fix(es).", "", undone));
    if (undone > 0 && m_library)
        m_library->rescan();
    emit changed();
    return left == 0;
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
        e.batch = qint64(o.value("batch").toDouble());
        m_batch = std::max(m_batch, e.batch);
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
        if (e.batch > 0)
            o.insert("batch", double(e.batch));
        fixes.append(o);
    }
    f.write(QJsonDocument(QJsonObject{{"fixes", fixes}}).toJson());
    f.commit();
}
