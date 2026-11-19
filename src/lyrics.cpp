#include "lyrics.h"
#include "safety.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringConverter>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

#include <fileref.h>
#include <tpropertymap.h>

// ---- Lyrics ----

int Lyrics::lineAt(double seconds) const {
    if (!synced() || lines.empty() || seconds < lines.front().start)
        return -1;
    // The last line that has started.
    auto it = std::upper_bound(lines.begin(), lines.end(), seconds,
                               [](double t, const LyricLine &line) { return t < line.start; });
    return int(it - lines.begin()) - 1;
}

void Lyrics::close(double duration) {
    for (size_t i = 0; i < lines.size(); ++i) {
        LyricLine &line = lines[i];
        const double next = i + 1 < lines.size() ? lines[i + 1].start : (duration > 0 ? duration : line.start + 6);
        if (line.end < line.start || line.end > next)
            line.end = next;
    }
}

namespace {

// "[01:02.34]", "[1:02]", "[01:02:345]" -> seconds, or -1.
double stampSeconds(const QString &minutes, const QString &secs, const QString &fraction) {
    bool okM = false, okS = false;
    const double m = minutes.toDouble(&okM), s = secs.toDouble(&okS);
    if (!okM || !okS)
        return -1;
    double f = 0;
    if (!fraction.isEmpty())
        f = fraction.toDouble() / std::pow(10.0, fraction.size());
    return m * 60 + s + f;
}

QString yamlScalar(QString value) {
    value = value.trimmed();
    if (value.startsWith('\'') && value.endsWith('\'') && value.size() >= 2)
        return value.mid(1, value.size() - 2).replace("''", "'");
    if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2) {
        const QString inner = value.mid(1, value.size() - 2);
        QString out;
        for (int i = 0; i < inner.size(); ++i) {
            const QChar c = inner[i];
            if (c != '\\' || i + 1 >= inner.size()) {
                out += c;
                continue;
            }
            const QChar e = inner[++i];
            if (e == 'n') out += '\n';
            else if (e == 't') out += '\t';
            else if (e == 'u' && i + 4 < inner.size()) {
                out += QChar(inner.mid(i + 1, 4).toUShort(nullptr, 16));
                i += 4;
            } else out += e;
        }
        return out;
    }
    if (value == "null" || value == "~")
        return {};
    // A plain scalar; a comment after it would start with " #".
    const int comment = value.indexOf(" #");
    return comment >= 0 ? value.left(comment).trimmed() : value;
}

int indentOf(const QString &line) {
    int i = 0;
    while (i < line.size() && line[i] == ' ')
        ++i;
    return i;
}

}

Lyrics Lyrics::parseLrc(const QString &text) {
    static const QRegularExpression stamp(R"(^\s*\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\])");
    static const QRegularExpression tag(R"(^\s*\[([a-zA-Z#]+):([^\]]*)\]\s*$)");
    static const QRegularExpression wordStamp(R"(<(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?>)");

    Lyrics out;
    double offset = 0;
    std::vector<LyricLine> timed;
    QStringList untimed;
    std::vector<double> breaks; // empty timed lines: where singing pauses

    for (QString raw : text.split('\n')) {
        raw.remove('\r');
        const auto t = tag.match(raw);
        if (t.hasMatch()) {
            if (t.captured(1).compare("offset", Qt::CaseInsensitive) == 0)
                offset = t.captured(2).trimmed().toDouble() / 1000.0;
            continue;
        }
        std::vector<double> starts;
        QString rest = raw;
        for (auto m = stamp.match(rest); m.hasMatch(); m = stamp.match(rest)) {
            const double s = stampSeconds(m.captured(1), m.captured(2), m.captured(3));
            if (s >= 0)
                starts.push_back(s);
            rest = rest.mid(m.capturedEnd());
        }
        if (starts.empty()) {
            untimed << raw.trimmed(); // blanks too: plain() keeps one between verses
            continue;
        }
        // Enhanced LRC: "<00:12.30>Word <00:12.80>word".
        std::vector<LyricWord> words;
        if (rest.contains('<')) {
            auto it = wordStamp.globalMatch(rest);
            QList<QRegularExpressionMatch> marks;
            while (it.hasNext())
                marks << it.next();
            if (!marks.isEmpty()) {
                for (int i = 0; i < marks.size(); ++i) {
                    const int from = marks[i].capturedEnd();
                    const int to = i + 1 < marks.size() ? marks[i + 1].capturedStart() : rest.size();
                    const QString w = rest.mid(from, to - from);
                    if (!w.trimmed().isEmpty())
                        words.push_back({stampSeconds(marks[i].captured(1), marks[i].captured(2), marks[i].captured(3)), w});
                }
                rest.remove(wordStamp);
            }
        }
        QString lineText = rest.simplified();
        // "()" or "..." alone is a pause written as punctuation, not words.
        if (std::none_of(lineText.begin(), lineText.end(), [](QChar c) { return c.isLetterOrNumber(); }))
            lineText.clear();
        for (double s : starts) {
            if (lineText.isEmpty()) {
                breaks.push_back(s - offset);
                continue;
            }
            LyricLine line;
            line.start = std::max(0.0, s - offset);
            line.text = lineText;
            for (const LyricWord &w : words)
                line.words.push_back({std::max(0.0, w.start - offset), w.text});
            timed.push_back(std::move(line));
        }
    }

    if (timed.empty())
        return plain(untimed.join('\n'));
    std::stable_sort(timed.begin(), timed.end(), [](const LyricLine &a, const LyricLine &b) { return a.start < b.start; });
    // A pause marker ends the line before it, so the window can show the break.
    std::sort(breaks.begin(), breaks.end());
    for (size_t i = 0; i < timed.size(); ++i) {
        const double next = i + 1 < timed.size() ? timed[i + 1].start : 1e9;
        auto b = std::upper_bound(breaks.begin(), breaks.end(), timed[i].start);
        if (b != breaks.end() && *b < next)
            timed[i].end = *b;
    }
    // Word timing on any line is worth showing; lines without it fill
    // evenly over their own time.
    bool anyWords = false;
    for (const LyricLine &l : timed)
        anyWords = anyWords || !l.words.empty();
    out.kind = anyWords ? Words : Synced;
    out.lines = std::move(timed);
    return out;
}

Lyrics Lyrics::parseLyricsfile(const QString &text) {
    Lyrics out;
    bool inLines = false, instrumental = false;
    int lineIndent = -1;
    LyricLine *line = nullptr;
    LyricWord *word = nullptr;

    for (QString raw : text.split('\n')) {
        raw.remove('\r');
        if (raw.trimmed().isEmpty() || raw.trimmed().startsWith('#'))
            continue;
        const int indent = indentOf(raw);
        const QString body = raw.mid(indent);
        if (indent == 0 && !body.startsWith("- ")) {
            inLines = body.startsWith("lines:");
            line = nullptr;
            word = nullptr;
            continue;
        }
        if (!inLines) {
            if (body.startsWith("instrumental:") && yamlScalar(body.mid(13)) == "true")
                instrumental = true;
            continue;
        }
        const bool item = body.startsWith("- ");
        const QString entry = item ? body.mid(2) : body;
        const int colon = entry.indexOf(':');
        if (colon < 0)
            continue;
        const QString key = entry.left(colon).trimmed();
        const QString value = entry.mid(colon + 1);
        if (item && lineIndent < 0)
            lineIndent = indent;
        if (item && indent == lineIndent) {
            out.lines.push_back({});
            line = &out.lines.back();
            word = nullptr;
        } else if (item && line && indent > lineIndent) {
            line->words.push_back({});
            word = &line->words.back();
        }
        if (!line)
            continue;
        // Keys of the word item sit deeper than the line's own keys.
        const bool wordKey = word && (item ? indent > lineIndent : indent > lineIndent + 2);
        if (key == "text") {
            if (wordKey) word->text = yamlScalar(value);
            else line->text = yamlScalar(value);
        } else if (key == "start_ms") {
            const double s = yamlScalar(value).toDouble() / 1000.0;
            if (wordKey) word->start = s;
            else line->start = s;
        } else if (key == "end_ms" && !wordKey) {
            line->end = yamlScalar(value).toDouble() / 1000.0;
        } else if (key == "words") {
            word = nullptr;
        }
    }

    if (instrumental) {
        out.kind = Instrumental;
        out.lines.clear();
        return out;
    }
    std::vector<LyricLine> kept;
    bool anyWords = false, anyTime = false;
    for (LyricLine &l : out.lines) {
        l.text = l.text.simplified();
        if (l.text.isEmpty())
            continue;
        anyTime = anyTime || l.start >= 0;
        anyWords = anyWords || !l.words.empty();
        kept.push_back(std::move(l));
    }
    out.lines = std::move(kept);
    if (out.lines.empty())
        out.kind = None;
    else if (!anyTime)
        out.kind = Plain;
    else
        out.kind = anyWords ? Words : Synced;
    std::stable_sort(out.lines.begin(), out.lines.end(), [](const LyricLine &a, const LyricLine &b) { return a.start < b.start; });
    return out;
}

Lyrics Lyrics::plain(const QString &text) {
    Lyrics out;
    // Keep single blank lines between verses; drop runs of them.
    bool blank = true;
    for (QString l : text.split('\n')) {
        l = l.remove('\r').simplified();
        if (l.isEmpty() && blank)
            continue;
        blank = l.isEmpty();
        LyricLine line;
        line.text = l;
        out.lines.push_back(line);
    }
    while (!out.lines.empty() && out.lines.back().text.isEmpty())
        out.lines.pop_back();
    out.kind = out.lines.empty() ? None : Plain;
    return out;
}

bool Lyrics::isJunk(const QString &text) {
    const QString t = text.trimmed();
    if (t.isEmpty())
        return true;
    int lines = 0, chars = 0;
    for (const QString &l : t.split('\n'))
        if (!l.trimmed().isEmpty()) {
            ++lines;
            chars += l.trimmed().size();
        }
    static const QRegularExpression site(R"((https?://|www\.|\b[\w-]+\.(com|net|org|info|biz|us|co)\b))",
                                         QRegularExpression::CaseInsensitiveOption);
    if (lines < 4 && site.match(t).hasMatch())
        return true;
    // Real lyrics are more than a line or two.
    return lines < 3 && chars < 80;
}

// ---- LyricsQuery ----

QString LyricsQuery::cleanTitle(const QString &title) {
    QString t = title;
    static const QRegularExpression number(R"(^\s*\d{1,3}\s*[-._)]+\s*)");
    static const QRegularExpression extras(
        R"(\s*[\(\[\{][^\)\]\}]*\b(feat|ft|featuring|prod|produced|explicit|clean|dirty|bonus|snippet|cdq|no dj|dj|hosted|skit|intro|outro|tagged|mixed|instrumental)\b[^\)\]\}]*[\)\]\}])",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailingFeat(R"(\s+(feat\.?|ft\.?|featuring)\s+.*$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression siteSuffix(R"(\s*-\s*(mf|mixfiend|datpiff|livemixtapes|spinrilla)\s*$)", QRegularExpression::CaseInsensitiveOption);
    t.remove(number);
    t.remove(extras);
    t.remove(trailingFeat);
    t.remove(siteSuffix);
    t.replace('_', ' ');
    return t.simplified();
}

bool LyricsQuery::searchable(const QString &title) {
    // "Track 10", "lil wayne bad guy 4 - Track 28", "Untitled": rips without
    // real titles can't match anything, so they cost LRCLIB no request.
    static const QRegularExpression generic(R"((^|[-:]\s*)(track|untitled|audio track)\s*\d*\s*$)",
                                            QRegularExpression::CaseInsensitiveOption);
    const QString t = cleanTitle(title);
    return !normalized(t).isEmpty() && !generic.match(t).hasMatch();
}

QString LyricsQuery::mainArtist(const QString &artist) {
    static const QRegularExpression split(R"(\s*(,|&|\band\b|\bx\b|\bfeat\.?|\bft\.?|\bfeaturing\b|/|;)\s*)",
                                          QRegularExpression::CaseInsensitiveOption);
    const QString first = artist.split(split, Qt::SkipEmptyParts).value(0).simplified();
    if (first == "*" || first.compare("Various artists", Qt::CaseInsensitive) == 0 ||
        first.compare("Unknown artist", Qt::CaseInsensitive) == 0)
        return {};
    return first;
}

QString LyricsQuery::normalized(const QString &text) {
    QString out;
    for (const QChar c : text.toCaseFolded())
        if (c.isLetterOrNumber())
            out += c;
    return out;
}

int pickSearchResult(const QList<QJsonObject> &results, const LyricsQuery &query) {
    const QString wantTitle = LyricsQuery::normalized(LyricsQuery::cleanTitle(query.title));
    const QString wantArtist = LyricsQuery::normalized(LyricsQuery::mainArtist(query.artist));
    const QString wantAlbum = LyricsQuery::normalized(query.album);
    if (wantTitle.isEmpty())
        return -1;
    int best = -1;
    double bestScore = -1e9;
    for (int i = 0; i < results.size(); ++i) {
        const QJsonObject r = results[i];
        const QString title = LyricsQuery::normalized(LyricsQuery::cleanTitle(r.value("trackName").toString()));
        const QString artist = LyricsQuery::normalized(r.value("artistName").toString());
        if (title.isEmpty())
            continue;
        const bool sameTitle = title == wantTitle ||
                               ((title.contains(wantTitle) || wantTitle.contains(title)) &&
                                double(std::min(title.size(), wantTitle.size())) / std::max(title.size(), wantTitle.size()) >= 0.7);
        if (!sameTitle)
            continue;
        if (!wantArtist.isEmpty() && !artist.contains(wantArtist) && !wantArtist.contains(artist))
            continue;
        const bool synced = !r.value("syncedLyrics").toString().isEmpty();
        const bool plain = !r.value("plainLyrics").toString().isEmpty();
        const bool instrumental = r.value("instrumental").toBool();
        if (!synced && !plain && !instrumental)
            continue;
        double score = (synced ? 4 : plain ? 1 : 0) + (r.value("hasWordSync").toBool() ? 2 : 0) + (title == wantTitle ? 1 : 0);
        if (!wantAlbum.isEmpty() && LyricsQuery::normalized(r.value("albumName").toString()) == wantAlbum)
            score += 1;
        const double length = r.value("duration").toDouble();
        if (query.duration > 0 && length > 0) {
            const double diff = std::abs(length - query.duration);
            // Mixtape cuts run longer or shorter than the album version, but
            // not by much; a far-off length is a different recording.
            if (diff > 10)
                continue;
            score -= diff / 3;
        }
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

// ---- LyricsService ----

namespace {

const char *UserAgent = "Halo/0.1.0 (Omarchy music player)";
constexpr qint64 MissRecheckSeconds = 7 * 24 * 3600;

QString kindName(int kind) {
    switch (kind) {
    case Lyrics::Plain: return "plain";
    case Lyrics::Synced: return "synced";
    case Lyrics::Words: return "words";
    case Lyrics::Instrumental: return "instrumental";
    }
    return "none";
}

QString statusName(int status) {
    switch (status) {
    case LyricsService::Searching: return "searching";
    case LyricsService::Found: return "found";
    case LyricsService::NotFound: return "not-found";
    case LyricsService::Offline: return "offline";
    case LyricsService::Failed: return "failed";
    }
    return "idle";
}

QString readText(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 512 * 1024)
        return {};
    const QByteArray bytes = f.readAll();
    auto decode = QStringDecoder(QStringDecoder::Utf8);
    QString text = decode(bytes);
    // Old .lrc files are often Latin-1 or Windows-1252.
    if (decode.hasError())
        text = QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    return text;
}

}

namespace {
// Every LRCLIB request: redirects only within the same site, and a reply
// larger than any lyrics could be is cut off, so a bad or hijacked server
// can't make Halo hold an unbounded download.
QNetworkReply *boundedGet(QNetworkAccessManager *network, QNetworkRequest request) {
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    QNetworkReply *reply = network->get(request);
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > Safety::MaxLyricsReply || total > Safety::MaxLyricsReply) {
            reply->setProperty("haloOversize", true);
            reply->abort();
        }
    });
    return reply;
}
}

LyricsService::LyricsService(const QString &cacheDirectory, const QString &stateDirectory, QObject *parent)
    : QObject(parent),
      m_cacheDirectory(cacheDirectory.isEmpty()
                           ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/halo/lyrics"
                           : cacheDirectory),
      m_stateFile((stateDirectory.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + "/halo"
                       : stateDirectory) + "/lyrics.json"),
      m_endpoint(qEnvironmentVariable("HALO_LRCLIB_URL", "https://lrclib.net")) {
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (m_status == Searching)
            fetch();
    });
    loadState();
}

LyricsService::~LyricsService() {
    if (m_reply)
        m_reply->abort();
}

void LyricsService::setStatus(Status status) {
    if (m_status == status)
        return;
    m_status = status;
    emit changed();
}

void LyricsService::setOnline(bool on) {
    if (m_online == on)
        return;
    m_online = on;
    saveState();
    emit onlineChanged();
    if (on && (m_status == Offline || m_status == Failed))
        fetch();
    else if (!on && m_status == Searching) {
        if (m_reply)
            m_reply->abort();
        setStatus(Offline);
    }
}

void LyricsService::setShown(bool shown) {
    if (m_shown == shown)
        return;
    m_shown = shown;
    saveState();
    emit shownChanged();
}

void LyricsService::load(const QString &path, const QString &title, const QString &artist, const QString &album,
                         double duration) {
    if (path == m_path && std::abs(duration - m_duration) < 0.5 && m_status != Idle)
        return;
    const bool samePath = path == m_path;
    ++m_generation;
    if (m_reply)
        m_reply->abort();
    m_retryTimer.stop();
    m_path = path;
    m_duration = duration;
    m_query = {title, artist, album, int(std::lround(duration))};
    if (!samePath) {
        m_offset = m_offsets.value(path).toDouble();
        emit offsetChanged();
        m_lyrics = {};
        m_source.clear();
        m_lines.clear();
        m_current = -1;
        m_progress = m_gap = 0;
        emit currentChanged();
    } else if (m_lyrics.synced()) {
        // Only the length changed (it was unknown until the song opened).
        m_lyrics.close(duration);
        rebuildLines();
        emit changed();
        return;
    }
    m_status = Idle;
    if (path.isEmpty()) {
        emit changed();
        return;
    }
    if (loadLocal() || loadCache())
        return;
    if (m_online)
        fetch();
    else {
        m_status = Offline;
        emit changed();
    }
}

void LyricsService::adopt(const Lyrics &lyrics, const QString &source) {
    m_lyrics = lyrics;
    m_lyrics.close(m_duration);
    m_source = source;
    m_status = Found;
    m_current = -2; // forces the next follow() to announce the line
    rebuildLines();
    emit changed();
    follow(m_time - m_offset);
}

bool LyricsService::loadLocal() {
    const QFileInfo song(m_path);
    // A .lrc beside the song, named like it.
    for (const QString &suffix : {QStringLiteral(".lrc"), QStringLiteral(".LRC")}) {
        const QString lrc = song.absolutePath() + "/" + song.completeBaseName() + suffix;
        if (!QFileInfo::exists(lrc))
            continue;
        const QString text = readText(lrc);
        const Lyrics l = Lyrics::parseLrc(text);
        if (l.kind != Lyrics::None && !Lyrics::isJunk(text)) {
            adopt(l, "file");
            return true;
        }
    }
    // Lyrics in the tags (ID3 USLT, Vorbis LYRICS, MP4 ©lyr), timed or not.
    if (!song.exists())
        return false;
    TagLib::FileRef ref(QFile::encodeName(m_path).constData(), false);
    if (ref.isNull() || !ref.file())
        return false;
    const TagLib::PropertyMap properties = ref.file()->properties();
    for (const char *key : {"LYRICS", "UNSYNCEDLYRICS"}) {
        if (!properties.contains(key))
            continue;
        for (const auto &value : properties[key]) {
            const QString text = QString::fromStdString(value.to8Bit(true));
            if (Lyrics::isJunk(text))
                continue;
            const Lyrics l = Lyrics::parseLrc(text);
            if (l.kind != Lyrics::None) {
                adopt(l, "tags");
                return true;
            }
        }
    }
    return false;
}

QString LyricsService::cacheFile() const {
    return m_cacheDirectory + "/" +
           QCryptographicHash::hash(m_path.toUtf8(), QCryptographicHash::Sha1).toHex() + ".json";
}

bool LyricsService::loadCache() {
    QFile f(cacheFile());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject entry = QJsonDocument::fromJson(f.readAll()).object();
    const QString format = entry.value("format").toString();
    const QString text = entry.value("text").toString();
    if (format == "missing") {
        const qint64 age = QDateTime::currentSecsSinceEpoch() - qint64(entry.value("fetched").toDouble());
        if (age > MissRecheckSeconds && m_online)
            return false; // LRCLIB grows; ask again now and then
        m_status = NotFound;
        m_source = "lrclib";
        emit changed();
        return true;
    }
    Lyrics l;
    if (format == "instrumental")
        l.kind = Lyrics::Instrumental;
    else if (format == "lyricsfile")
        l = Lyrics::parseLyricsfile(text);
    else if (format == "lrc")
        l = Lyrics::parseLrc(text);
    else if (format == "plain")
        l = Lyrics::plain(text);
    if (l.kind == Lyrics::None)
        return false;
    adopt(l, entry.value("source").toString("lrclib"));
    return true;
}

void LyricsService::storeCache(const QJsonObject &entry) {
    QDir().mkpath(m_cacheDirectory);
    QSaveFile f(cacheFile());
    if (!f.open(QIODevice::WriteOnly))
        return;
    QJsonObject e = entry;
    e.insert("fetched", double(QDateTime::currentSecsSinceEpoch()));
    e.insert("path", m_path);
    f.write(QJsonDocument(e).toJson(QJsonDocument::Compact));
    f.commit();
}

void LyricsService::fetch() {
    if (m_path.isEmpty() || !LyricsQuery::searchable(m_query.title))
        return finishMissing();
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    if (m_reply)
        m_reply->abort();
    m_status = Searching;
    emit changed();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (m_blockedUntil.isValid() && now < m_blockedUntil) {
        m_retryTimer.start(int(now.msecsTo(m_blockedUntil)));
        return;
    }

    if (!Safety::allowedEndpoint(m_endpoint))
        return setStatus(Failed);   // https only (plain http just to this machine, for tests)
    // /api/get needs an artist; without one, search by title alone.
    if (LyricsQuery::mainArtist(m_query.artist).isEmpty())
        return fetchSearch();

    QUrl url(m_endpoint + "/api/get");
    QUrlQuery q;
    q.addQueryItem("track_name", LyricsQuery::cleanTitle(m_query.title));
    q.addQueryItem("artist_name", LyricsQuery::mainArtist(m_query.artist));
    if (!m_query.album.isEmpty())
        q.addQueryItem("album_name", m_query.album);
    if (m_query.duration > 0)
        q.addQueryItem("duration", QString::number(m_query.duration));
    url.setQuery(q);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, UserAgent);
    request.setTransferTimeout(12000);
    QNetworkReply *reply = boundedGet(m_network, request);
    m_reply = reply;
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->property("haloOversize").toBool())
            return setStatus(Failed);
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code == 200) {
            const QJsonObject record = QJsonDocument::fromJson(reply->readAll()).object();
            if (!record.isEmpty())
                return finishOnline(record);
            return fetchSearch();
        }
        // Not found, or a query LRCLIB won't take as an exact match (400):
        // the search is looser.
        if (code == 404 || code == 400)
            return fetchSearch();
        if (code == 429) {
            // LRCLIB asks clients to honour Retry-After, with some jitter.
            const int wait = std::max(5, reply->rawHeader("Retry-After").toInt());
            const int jitter = int(wait * 1000 * 0.3 * QRandomGenerator::global()->generateDouble());
            m_blockedUntil = QDateTime::currentDateTimeUtc().addMSecs(qint64(wait) * 1000 + jitter);
            m_retryTimer.start(wait * 1000 + jitter);
            return;
        }
        // No network, a timeout, or a server error: nothing is cached, so
        // the song is looked up again the next time it plays.
        setStatus(Failed);
    });
}

void LyricsService::fetchSearch() {
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    if (m_status != Searching) {
        m_status = Searching;
        emit changed();
    }
    if (!Safety::allowedEndpoint(m_endpoint))
        return setStatus(Failed);
    QUrl url(m_endpoint + "/api/search");
    QUrlQuery q;
    q.addQueryItem("track_name", LyricsQuery::cleanTitle(m_query.title));
    const QString artist = LyricsQuery::mainArtist(m_query.artist);
    if (!artist.isEmpty())
        q.addQueryItem("artist_name", artist);
    url.setQuery(q);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, UserAgent);
    request.setTransferTimeout(12000);
    QNetworkReply *reply = boundedGet(m_network, request);
    m_reply = reply;
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->property("haloOversize").toBool())
            return setStatus(Failed);
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code != 200) {
            if (code == 429) {
                const int wait = std::max(5, reply->rawHeader("Retry-After").toInt());
                m_blockedUntil = QDateTime::currentDateTimeUtc().addSecs(wait);
                m_retryTimer.start(wait * 1000);
                return;
            }
            return setStatus(Failed);
        }
        QList<QJsonObject> results;
        for (const auto &v : QJsonDocument::fromJson(reply->readAll()).array())
            results << v.toObject();
        const int pick = pickSearchResult(results, m_query);
        if (pick < 0)
            return finishMissing();
        finishOnline(results[pick]);
    });
}

void LyricsService::finishOnline(const QJsonObject &record) {
    if (record.value("instrumental").toBool()) {
        Lyrics l;
        l.kind = Lyrics::Instrumental;
        storeCache({{"format", "instrumental"}, {"source", "lrclib"}});
        return adopt(l, "lrclib");
    }
    // Word timing when LRCLIB has it, then line timing, then plain text.
    const QString lyricsfile = record.value("lyricsfile").toString();
    if (record.value("hasWordSync").toBool() && !lyricsfile.isEmpty()) {
        const Lyrics l = Lyrics::parseLyricsfile(lyricsfile);
        if (l.kind == Lyrics::Words || l.kind == Lyrics::Synced) {
            storeCache({{"format", "lyricsfile"}, {"source", "lrclib"}, {"text", lyricsfile}});
            return adopt(l, "lrclib");
        }
    }
    const QString synced = record.value("syncedLyrics").toString();
    if (!synced.isEmpty()) {
        const Lyrics l = Lyrics::parseLrc(synced);
        if (l.kind != Lyrics::None) {
            storeCache({{"format", "lrc"}, {"source", "lrclib"}, {"text", synced}});
            return adopt(l, "lrclib");
        }
    }
    const QString plainText = record.value("plainLyrics").toString();
    if (!plainText.isEmpty()) {
        const Lyrics l = Lyrics::plain(plainText);
        if (l.kind != Lyrics::None) {
            storeCache({{"format", "plain"}, {"source", "lrclib"}, {"text", plainText}});
            return adopt(l, "lrclib");
        }
    }
    finishMissing();
}

void LyricsService::finishMissing() {
    storeCache({{"format", "missing"}, {"source", "lrclib"}});
    m_source = "lrclib";
    setStatus(NotFound);
}

void LyricsService::retry() {
    if (m_path.isEmpty())
        return;
    QFile::remove(cacheFile());
    const QString path = m_path;
    m_path.clear();
    load(path, m_query.title, m_query.artist, m_query.album, m_duration);
}

void LyricsService::follow(double seconds) {
    const double t = seconds + m_offset;
    m_time = t;
    int line = -1;
    double progress = 0, gap = 0;
    if (m_lyrics.synced() && !m_lyrics.lines.empty()) {
        line = m_lyrics.lineAt(t);
        if (line >= 0) {
            const LyricLine &l = m_lyrics.lines[size_t(line)];
            if (!l.words.empty()) {
                // Characters sung so far, each word filling over its own time.
                int total = 0, done = 0;
                for (const LyricWord &w : l.words)
                    total += int(w.text.size());
                for (size_t i = 0; i < l.words.size(); ++i) {
                    const LyricWord &w = l.words[i];
                    const double next = i + 1 < l.words.size() ? l.words[i + 1].start : std::max(l.end, w.start + 0.4);
                    if (t >= next) {
                        done += int(w.text.size());
                        continue;
                    }
                    if (t > w.start)
                        done += int(std::round(w.text.size() * (t - w.start) / std::max(0.05, next - w.start)));
                    break;
                }
                progress = total > 0 ? double(done) / total : 1;
            } else {
                progress = std::clamp((t - l.start) / std::max(0.2, l.end - l.start), 0.0, 1.0);
            }
            // Past the end of a line with more than a few seconds to the
            // next one: a break, counted down on screen.
            const double next = size_t(line) + 1 < m_lyrics.lines.size() ? m_lyrics.lines[size_t(line) + 1].start : -1;
            if (next > 0 && t >= l.end && next - l.end >= 4)
                gap = (next - t) / (next - l.end);
        } else {
            const double first = m_lyrics.lines.front().start;
            if (first >= 4)
                gap = std::clamp((first - t) / first, 0.0, 1.0);
        }
    }
    if (line != m_current) {
        m_current = line;
        emit currentChanged();
    }
    if (progress != m_progress || gap != m_gap || m_lyrics.synced()) {
        m_progress = progress;
        m_gap = gap;
        emit progressChanged();
    }
}

void LyricsService::nudge(double seconds) {
    if (m_path.isEmpty())
        return;
    const double raw = m_time - m_offset;
    m_offset = std::clamp(std::round((m_offset + seconds) * 20) / 20, -30.0, 30.0);
    if (std::abs(m_offset) < 0.001)
        m_offsets.remove(m_path);
    else
        m_offsets.insert(m_path, m_offset);
    saveState();
    emit offsetChanged();
    follow(raw);
}

void LyricsService::resetOffset() {
    nudge(-m_offset);
}

void LyricsService::rebuildLines() {
    m_lines.clear();
    for (const LyricLine &l : m_lyrics.lines) {
        QVariantList words;
        for (const LyricWord &w : l.words)
            words << QVariantMap{{"start", w.start}, {"text", w.text}};
        m_lines << QVariantMap{{"start", l.start}, {"end", l.end}, {"text", l.text}, {"words", words}};
    }
}

void LyricsService::loadState() {
    QFile f(m_stateFile);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_online = o.value("online").toBool(false);
    m_shown = o.value("shown").toBool(true);
    m_offsets = o.value("offsets").toObject();
}

void LyricsService::saveState() {
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile f(m_stateFile);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(QJsonObject{{"version", 1}, {"online", m_online}, {"shown", m_shown}, {"offsets", m_offsets}})
                .toJson(QJsonDocument::Indented));
    f.commit();
}

QJsonObject LyricsService::toJson() const {
    QJsonArray lines;
    for (const LyricLine &l : m_lyrics.lines) {
        QJsonObject o{{"text", l.text}};
        if (l.start >= 0)
            o.insert("start", std::round(l.start * 100) / 100);
        if (!l.words.empty())
            o.insert("words", int(l.words.size()));
        lines.append(o);
    }
    return {{"status", statusName(m_status)},
            {"kind", kindName(m_lyrics.kind)},
            {"source", m_source},
            {"online", m_online},
            {"offset", m_offset},
            {"current", m_current},
            {"path", m_path},
            {"lines", lines}};
}
