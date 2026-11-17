#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

struct LyricWord {
    double start = 0; // seconds
    QString text;     // keeps its trailing space, so words join back into the line
};

struct LyricLine {
    double start = -1; // seconds; -1 for unsynced lines
    double end = -1;   // when the next line starts (or the song's end)
    QString text;
    std::vector<LyricWord> words; // empty unless the source timed every word
};

// One song's lyrics, however they were found.
struct Lyrics {
    enum Kind { None, Plain, Synced, Words, Instrumental };
    Kind kind = None;
    std::vector<LyricLine> lines;

    bool synced() const { return kind == Synced || kind == Words; }
    // The line sung at `seconds`, or -1 before the first one.
    int lineAt(double seconds) const;
    // Fills each line's end from the next line's start; the last ends at `duration`.
    void close(double duration);

    // "[mm:ss.xx] text" with several stamps per line, [offset:±ms], and
    // "<mm:ss.xx>" word stamps (enhanced LRC). Plain text gives Plain lyrics.
    static Lyrics parseLrc(const QString &text);
    // LRCLIB's Lyricsfile: YAML with lines[].start_ms/end_ms and
    // lines[].words[].text/start_ms. Only the subset LRCLIB writes is read.
    static Lyrics parseLyricsfile(const QString &text);
    static Lyrics plain(const QString &text);
    // Tag "lyrics" on downloaded mixtapes are often a site's watermark
    // ("MixFiend.com / MixtapeTorrent.com"), not lyrics.
    static bool isJunk(const QString &text);
};

// What LRCLIB is asked for: titles and artists tidied of what search can't
// match ("(Feat. X)", "[Prod. Y]", a site's "-MF" suffix, a list of artists).
struct LyricsQuery {
    QString title, artist, album;
    int duration = 0;
    static QString cleanTitle(const QString &title);
    // False for titles that can't be found anywhere ("Track 10").
    static bool searchable(const QString &title);
    static QString mainArtist(const QString &artist);
    static QString normalized(const QString &text); // lower case, letters and digits only
};

// Picks the LRCLIB search result that is this song, or -1: the title must
// match, the artist must be among the credits, and the length must be close.
// Synced lyrics win over plain ones, word timing over line timing.
int pickSearchResult(const QList<QJsonObject> &results, const LyricsQuery &query);

// Finds, caches and follows the lyrics of the song playing.
//
// Local sources come first and never touch the network: a .lrc file beside
// the song, then lyrics in its tags. LRCLIB (lrclib.net) is asked only when
// the user turned "online" on, with the song's title, artist, album and
// length; answers and misses are cached so a song is looked up once.
class LyricsService : public QObject {
    Q_OBJECT
    Q_PROPERTY(int status READ status NOTIFY changed)
    Q_PROPERTY(int kind READ kind NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QVariantList lines READ lines NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY onlineChanged)
    // Whether Now Playing shows the lyrics (L); remembered.
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY shownChanged)
    Q_PROPERTY(double offset READ offset NOTIFY offsetChanged)
    Q_PROPERTY(int current READ current NOTIFY currentChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(double time READ time NOTIFY progressChanged)
    Q_PROPERTY(double gap READ gap NOTIFY progressChanged)

public:
    enum Status { Idle, Searching, Found, NotFound, Offline, Failed };
    Q_ENUM(Status)
    enum KindValue { KindNone = Lyrics::None, KindPlain = Lyrics::Plain, KindSynced = Lyrics::Synced,
                     KindWords = Lyrics::Words, KindInstrumental = Lyrics::Instrumental };
    Q_ENUM(KindValue)

    // Directories are for tests; empty means Halo's cache and state folders.
    explicit LyricsService(const QString &cacheDirectory = {}, const QString &stateDirectory = {},
                           QObject *parent = nullptr);
    ~LyricsService() override;

    int status() const { return m_status; }
    int kind() const { return m_lyrics.kind; }
    QString source() const { return m_source; }
    QVariantList lines() const { return m_lines; }
    int count() const { return int(m_lyrics.lines.size()); }
    bool online() const { return m_online; }
    void setOnline(bool on);
    bool shown() const { return m_shown; }
    void setShown(bool shown);
    double offset() const { return m_offset; }
    int current() const { return m_current; }
    double progress() const { return m_progress; }
    double time() const { return m_time; }
    double gap() const { return m_gap; }
    const Lyrics &lyrics() const { return m_lyrics; }
    QString path() const { return m_path; }

    QJsonObject toJson() const;

    // Where LRCLIB answers; tests point it at a local server.
    void setEndpoint(const QString &url) { m_endpoint = url; }

public slots:
    // A new song: look for its lyrics.
    void load(const QString &path, const QString &title, const QString &artist, const QString &album,
              double duration);
    // The song's playing position, many times a second.
    void follow(double seconds);
    // Moves the timing earlier or later for this song (saved per song).
    void nudge(double seconds);
    void resetOffset();
    // Looks again, online included, ignoring a cached miss.
    void retry();

signals:
    void changed();
    void onlineChanged();
    void shownChanged();
    void offsetChanged();
    void currentChanged();
    void progressChanged();

private:
    void setStatus(Status status);
    void adopt(const Lyrics &lyrics, const QString &source);
    bool loadLocal();
    bool loadCache();
    void fetch();
    void fetchSearch();
    void finishOnline(const QJsonObject &record);
    void finishMissing();
    void storeCache(const QJsonObject &entry);
    QString cacheFile() const;
    void loadState();
    void saveState();
    void rebuildLines();

    QString m_cacheDirectory, m_stateFile, m_endpoint;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QTimer m_retryTimer;
    QDateTime m_blockedUntil; // LRCLIB asked us to wait (429 Retry-After)

    QString m_path;
    LyricsQuery m_query;
    double m_duration = 0;
    int m_generation = 0;

    Lyrics m_lyrics;
    QString m_source;
    QVariantList m_lines;
    int m_status = Idle;
    bool m_online = false;
    bool m_shown = true;
    double m_offset = 0;
    QJsonObject m_offsets;
    int m_current = -1;
    double m_progress = 0, m_time = 0, m_gap = 0;
};
