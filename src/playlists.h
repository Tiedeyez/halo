#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>
#include <vector>

#include "library.h"

class Analyzer;

// One condition of a smart playlist: "artist contains Dilla",
// "plays greater 3", "added inLast 30".
struct PlaylistRule {
    QString field, op, value;
};

struct Playlist {
    QString id;   // the file's base name; stable across renames
    QString name, description;
    bool smart = false;
    bool pinned = false;
    qint64 created = 0, modified = 0;
    QStringList paths; // a manual playlist's songs, in order
    // A smart playlist: songs from the library that match the rules.
    bool matchAll = true;
    std::vector<PlaylistRule> rules;
    QString sort = "added"; // added, title, artist, album, year, plays, played, bpm, loudness, random
    bool descending = true;
    int limit = 0; // 0 = no limit
};

// Facts about a track the rules can ask for that the tags don't hold.
struct TrackFacts {
    int plays = 0;
    qint64 lastPlayed = 0; // seconds since the epoch, 0 = never
    double bpm = 0;        // 0 = unknown
    double loudness = 0;   // LUFS, 0 = unknown
};

// Playlists live as files in ~/.local/share/halo/playlists, so any player
// can read them and they survive Halo: a manual playlist is an .m3u8
// (absolute paths, #EXTINF lines, Halo's own details on a #HALO: line that
// other players skip), a smart playlist a small .halo-smart JSON file.
// Files dropped into the folder, or written by `halo playlist`, show up
// in a running Halo by themselves.
class Playlists : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList all READ all NOTIFY changed)
    Q_PROPERTY(QVariantList manual READ manual NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool canRestore READ canRestore NOTIFY changed)
    Q_PROPERTY(QString folder READ folder CONSTANT)

public:
    static constexpr int MaxEntries = 100000;
    static constexpr qint64 MaxFileBytes = 16 * 1024 * 1024;
    static constexpr int MaxNameLength = 120;

    explicit Playlists(Library *library, Analyzer *analyzer = nullptr, const QString &folder = {},
                       QObject *parent = nullptr);

    static QString defaultFolder();
    QString folder() const { return m_folder; }

    QVariantList all() const;
    QVariantList manual() const;
    int count() const { return int(m_lists.size()); }
    bool canRestore() const { return !m_trash.isEmpty(); }
    const Playlist *find(const QString &id) const;
    // By id, or else by name ignoring case (for the command line).
    const Playlist *lookup(const QString &idOrName) const;

    Q_INVOKABLE QString create(const QString &name, const QStringList &paths = {});
    Q_INVOKABLE QString createSmart(const QString &name, const QVariantMap &spec);
    Q_INVOKABLE QString duplicate(const QString &id);
    Q_INVOKABLE bool rename(const QString &id, const QString &name);
    Q_INVOKABLE bool setDescription(const QString &id, const QString &text);
    Q_INVOKABLE bool setPinned(const QString &id, bool pinned);
    // Moves the file to the folder's .trash; restoreLast() brings it back.
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QString restoreLast();

    // Adds songs not already in the playlist; returns how many were added
    // (the rest were there). `allowDuplicates` adds them all.
    Q_INVOKABLE int add(const QString &id, const QStringList &paths, bool allowDuplicates = false);
    Q_INVOKABLE bool removeRows(const QString &id, const QVariantList &rows);
    Q_INVOKABLE bool move(const QString &id, int from, int to);
    Q_INVOKABLE bool contains(const QString &id, const QString &path) const;

    Q_INVOKABLE QStringList paths(const QString &id) const;
    Q_INVOKABLE QVariantMap info(const QString &id) const;
    Q_INVOKABLE QVariantMap spec(const QString &id) const;
    Q_INVOKABLE bool setSpec(const QString &id, const QVariantMap &spec);
    // A preview count for the rules editor, without saving.
    Q_INVOKABLE int previewCount(const QVariantMap &spec) const;
    // Songs from the library that would fit, by tempo and loudness, best
    // first, none already in it: Drift's ear, offered as suggestions.
    Q_INVOKABLE QStringList suggest(const QString &id, int count = 10) const;
    // Title, artist and album id for songs, for lists outside a model.
    Q_INVOKABLE QVariantList describe(const QStringList &paths) const;

    Q_INVOKABLE QString importFile(const QString &file);
    Q_INVOKABLE bool exportFile(const QString &id, const QString &file) const;

    // ---- the rules, on plain data, for the tests ----
    static Playlist fromSpec(const QVariantMap &spec);
    static QVariantMap toSpec(const Playlist &p);
    static std::vector<int> evaluate(const Playlist &p, const std::vector<Track> &tracks,
                                     const std::function<TrackFacts(const Track &)> &facts, qint64 now);
    static QString m3u8(const Playlist &p, const std::function<const Track *(const QString &)> &track);
    // Parses an .m3u/.m3u8: relative entries resolve against `baseDir`,
    // file:// URLs are decoded, anything else (streams) is skipped.
    static Playlist parseM3u(const QByteArray &data, const QString &baseDir);
    static QString cleanName(const QString &name);

signals:
    void changed();
    void contentsChanged(const QString &id);
    void message(const QString &text);

private:
    void load();
    bool save(Playlist &p);
    QString fileOf(const Playlist &p) const;
    QString newId(const QString &name) const;
    Playlist *edit(const QString &id);
    TrackFacts factsOf(const Track &t) const;
    void reloadSoon();

    Library *m_library;
    Analyzer *m_analyzer;
    QString m_folder;
    std::vector<Playlist> m_lists;
    QHash<QString, QString> m_files; // id -> its file
    QStringList m_trash; // files in .trash, most recent last
    QFileSystemWatcher m_watcher;
    QTimer m_reload;
    QTimer m_analysed; // smart playlists catch up as tempo and loudness arrive
    qint64 m_ownWrite = 0; // ignore the watcher right after our own save
    // all() evaluates every smart playlist; kept until something changes.
    mutable QVariantList m_allCache;
    mutable bool m_allValid = false;
};

// A playlist's songs, with the roles TrackList expects.
class PlaylistTrackModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString playlistId READ playlistId WRITE setPlaylistId NOTIFY playlistIdChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(double totalDuration READ totalDuration NOTIFY countChanged)
    Q_PROPERTY(int missing READ missing NOTIFY countChanged)

public:
    enum Roles { PathRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumRole, AlbumIdRole, DurationRole,
                 NumberRole, PlaysRole, MissingRole };
    PlaylistTrackModel(Playlists *playlists, Library *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override { return int(m_paths.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString playlistId() const { return m_id; }
    void setPlaylistId(const QString &id);
    double totalDuration() const;
    int missing() const;
    Q_INVOKABLE QStringList paths() const { return m_paths; }
    Q_INVOKABLE QString pathAt(int row) const { return m_paths.value(row); }
    Q_INVOKABLE int rowOf(const QString &path) const { return int(m_paths.indexOf(path)); }

signals:
    void playlistIdChanged();
    void countChanged();

private:
    void refresh();
    Playlists *m_playlists;
    Library *m_library;
    QString m_id;
    QStringList m_paths;
    mutable QHash<QString, Track> m_described;
};
