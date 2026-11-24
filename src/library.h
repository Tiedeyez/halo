#pragma once

#include <QAbstractListModel>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <atomic>
#include <memory>
#include <vector>

struct Track {
    QString path, title, artist, album, albumArtist, genre;
    int number = 0, disc = 0, year = 0;
    double duration = 0;
    qint64 mtime = 0, size = 0;
    bool embeddedArt = false;
    QString albumId; // filled when albums are built
    QString search;  // lower-case haystack for the filter
};

struct Album {
    QString id, folder, title, artist;
    int year = 0;
    double duration = 0;
    qint64 added = 0;
    std::vector<int> tracks;
    QString coverFile;  // an image in the folder, if any
    QString coverTrack; // else a track with embedded art
};

// Where covers come from and where they are cached; safe from any thread.
namespace Covers {
struct Source {
    QString file, track;
};
void setSources(QHash<QString, Source> sources);
// Path of a cached JPEG of the album's cover at `size` px, made on demand;
// empty when the album has no art.
QString ensure(const QString &albumId, int size = 512);
QImage load(const QString &albumId, int size);
}

// Every track under the music folders. Scanning runs on a worker thread and
// reuses what it read last time for files whose size and date are unchanged,
// so a library of a hundred thousand tracks opens instantly after the first
// scan. A folder is an album: that is how mixtapes, rips and downloads are
// laid out (a "CD1"/"Disc 2" subfolder joins its parent).
class Library : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(int scanned READ scanned NOTIFY scanProgress)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY changed)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY changed)
    Q_PROPERTY(QStringList folders READ folders NOTIFY changed)

public:
    explicit Library(const QStringList &folders = {}, const QString &cacheDirectory = {},
                     const QString &stateDirectory = {}, QObject *parent = nullptr);
    ~Library() override;

    static constexpr double MinimumSeconds = 10;
    static QStringList defaultFolders();
    static bool isAudioFile(const QString &path);
    static Track readTrack(const QString &path);
    static QString titleFromFileName(const QString &fileName);
    static QString cleanTitle(const QString &title, const QString &artist);
    static void splitCredit(Track &t);
    static QString prettyFolderName(const QString &folder);
    static QString capitalised(const QString &name);
    // Scans synchronously on the calling thread (for the CLI and tests).
    static std::vector<Track> scanFolders(const QStringList &folders, const QHash<QString, Track> &previous,
                                          std::atomic<int> *counter = nullptr);
    static std::vector<Album> buildAlbums(std::vector<Track> &tracks);
    // Files stay as they are; a folder becomes its audio files, in name order.
    static QStringList expand(const QStringList &paths);

    Q_INVOKABLE void rescan();
    // Adds a music folder (a path or a file:// URL from the folder dialog)
    // to ~/.config/halo/folders and scans it. False if it isn't a folder.
    Q_INVOKABLE bool addFolder(const QString &folder);
    static QString foldersFile();
    void loadCache();

    bool scanning() const { return m_scanning; }
    int scanned() const { return m_counter.load(); }
    int trackCount() const { return int(m_tracks.size()); }
    int albumCount() const { return int(m_albums.size()); }
    QStringList folders() const { return m_folders; }

    const std::vector<Track> &tracks() const { return m_tracks; }
    const std::vector<Album> &albums() const { return m_albums; }
    const Track *track(const QString &path) const;
    const Album *album(const QString &id) const;
    // A file outside the library (dropped or opened) still gets a Track.
    Track describe(const QString &path) const;
    Q_INVOKABLE QStringList albumPaths(const QString &id) const;
    Q_INVOKABLE QVariantMap albumInfo(const QString &id) const;

    // Play statistics, kept locally.
    void recordPlay(const QString &path);
    int playCount(const QString &path) const { return m_plays.value(path); }
    qint64 lastPlayed(const QString &path) const { return m_lastPlayed.value(path); }

    // Lost & Found: songs you loved and haven't heard in a while, picked
    // fresh each day (the same all day). `now` in seconds since the epoch.
    std::vector<int> lostAndFound(qint64 now, int count = 30) const;
    // The rule itself, on plain data, for the tests: songs played at least
    // twice and not in 45 days, the more plays and the longer away the
    // likelier; when there aren't enough, songs never played here that have
    // been in the library a month, then any never played, oldest first.
    // Same artist never twice in a row.
    static std::vector<int> pickLostAndFound(const std::vector<Track> &tracks, const QHash<QString, int> &plays,
                                             const QHash<QString, qint64> &lastPlayed, qint64 now, int count);
    void flush();

signals:
    void changed();
    void scanningChanged();
    void scanProgress();

private:
    void adopt(std::vector<Track> tracks);
    void saveCache();
    void loadStats();
    void saveStats();

    QStringList m_folders;
    QString m_cacheFile, m_statsFile;
    std::vector<Track> m_all; // everything scanned, sounds included
    std::vector<Track> m_tracks;
    std::vector<Album> m_albums;
    QHash<QString, int> m_byPath;
    QHash<QString, int> m_byAlbum;
    bool m_scanning = false;
    bool m_rescanAgain = false;
    std::atomic<int> m_counter{0};
    QTimer m_progress;
    QFutureWatcher<std::vector<Track>> m_watcher;
    QHash<QString, int> m_plays;
    QHash<QString, qint64> m_lastPlayed;
    QTimer m_statsSave;
};

class AlbumModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int sort READ sort WRITE setSort NOTIFY sortChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, TitleRole, ArtistRole, YearRole, CountRole, DurationRole };
    enum Sort { RecentlyAdded, ByArtist, ByTitle };
    explicit AlbumModel(Library *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return int(m_rows.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int sort() const { return m_sort; }
    void setSort(int sort);
    Q_INVOKABLE QString idAt(int row) const;

signals:
    void queryChanged();
    void sortChanged();
    void countChanged();

private:
    void rebuild();
    Library *m_library;
    QString m_query;
    int m_sort = RecentlyAdded;
    std::vector<int> m_rows;
};

class TrackModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString albumId READ albumId WRITE setAlbumId NOTIFY albumIdChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(double totalDuration READ totalDuration NOTIFY countChanged)

public:
    enum Roles { PathRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumRole, AlbumIdRole, DurationRole,
                 NumberRole, PlaysRole };
    enum Mode { AllSongs, AlbumTracks, MostPlayed, LostAndFound };
    explicit TrackModel(Library *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return int(m_rows.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    int mode() const { return m_mode; }
    void setMode(int mode);
    QString albumId() const { return m_albumId; }
    void setAlbumId(const QString &id);
    double totalDuration() const;
    Q_INVOKABLE QStringList paths() const;
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int rowOf(const QString &path) const;

    static bool matches(const Track &track, const QStringList &words);

signals:
    void queryChanged();
    void modeChanged();
    void albumIdChanged();
    void countChanged();

private:
    void rebuild();
    Library *m_library;
    QString m_query, m_albumId;
    int m_mode = AllSongs;
    std::vector<int> m_rows;
};

class CoverProvider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
};
