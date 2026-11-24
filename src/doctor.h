#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <vector>

// The Library Doctor: finds what's wrong with a library's tags and proposes
// fixes, using only what's on this computer (the tags, file and folder
// names). It never writes on its own: each fix is shown as before and after
// and applied only when asked, after checking the file still says what it
// said at the scan. Every write is logged with the old value, so any fix
// can be undone. Duplicates are only pointed out; nothing is deleted.
namespace Doctor {

struct Tags {
    QString title, artist, album, albumArtist, comment;
    int number = 0;
    double duration = 0;
};

struct File {
    QString path;
    Tags tags;
};

// In the order they are shown.
enum class Kind {
    Watermark,
    UploadLabel,     // "(Official Video)" and the like in a title
    NumberedTitle,
    MissingTitle,
    MissingArtist,
    MissingAlbum,
    MissingNumber,   // no track number, though the file's name opens with one
    ArtistSpelling,  // the same artist written another way than most songs write it
    MixedAlbum,      // an album name most of the folder doesn't share
    MixedAlbumArtist,
    Case,            // ALL CAPITALS or all small letters
    Duplicate
};

struct Finding {
    Kind kind;
    QString path;          // the file to change (the first of a duplicate set)
    QStringList others;    // for duplicates: the other copies
    QString field;         // "title", "artist", "album", "albumartist", "comment", "tracknumber"; empty if not fixable
    QString before, after; // the field's value now, and proposed
    // Whether the fix is certain enough for "Fix all": it comes from the
    // file's own tags, or matches a name the library already uses. The rest
    // are judgements (a folder's name, what the neighbours say) and are
    // applied one at a time or a folder at a time.
    bool sure = false;
    QString why;           // where the proposal comes from: "tag", "filename", "title", "folder-songs", "folder-name", "library", "case"
};

QString kindName(Kind kind);

// A site's name or address in a tag ("Goin' In [MixFiend.com]",
// "DatPiff Exclusive"), removed with the brackets or separator around it.
// Returns the text unchanged when there's nothing to remove.
QString stripWatermark(const QString &text);
bool hasWatermark(const QString &text);

// Everything wrong with these files, in a stable order. libraryRoots: the
// music folders themselves, which are never taken for an album.
std::vector<Finding> diagnose(const std::vector<File> &files, const QStringList &libraryRoots = {});

// TagLib, on real files.
bool readTags(const QString &path, Tags &out);
// A field can hold several values (two ARTIST credits on a FLAC). These read
// and write the whole list, exactly as stored.
bool readValues(const QString &path, const QString &field, QStringList &out);
bool writeValues(const QString &path, const QString &field, const QStringList &values);
// The list with its first value (and any exact copies of it) changed, or
// for an empty value removed; every other value stays.
QStringList withFirst(QStringList values, const QString &value);
// The values without repeats, in order.
QStringList distinct(const QStringList &values);
// Sets a field's first value and keeps the rest; false if the file couldn't
// be written.
bool writeField(const QString &path, const QString &field, const QString &value);
QString field(const Tags &tags, const QString &name);

}

class LibraryDoctor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    // Per kind: how many findings ("watermark") and how many of them "Fix
    // all" would apply ("watermark/sure").
    Q_PROPERTY(QVariantMap counts READ counts NOTIFY changed)
    Q_PROPERTY(int fixable READ fixable NOTIFY changed)
    Q_PROPERTY(int undoable READ undoable NOTIFY changed)

public:
    // logFile: where fixes are recorded for undo (empty: the default under
    // ~/.local/state/halo).
    explicit LibraryDoctor(class Library *library, const QString &logFile = {}, QObject *parent = nullptr);

    QVariantList findings() const;
    QVariantMap counts() const;
    bool scanning() const { return m_scanning; }
    int fixable() const;
    int undoable() const { return int(m_log.size()); }

    Q_INVOKABLE void scan();
    // Applies finding `index`; false (and a message) if the file changed or
    // couldn't be written.
    Q_INVOKABLE bool fix(int index);
    // Every finding of a kind ("watermark", ...) whose fix is sure.
    Q_INVOKABLE int fixAll(const QString &kind);
    // Every fixable finding of a kind in one folder, sure or not: the
    // person is looking at that folder and says yes to it.
    Q_INVOKABLE int fixGroup(const QString &kind, const QString &folder);
    // The person's own answer for a kind in one folder ("these are all by
    // Lil Wayne"), written to every song the finding names there. Only for
    // the kinds a folder can share an answer to: a missing artist or
    // album, an album or album artist that disagrees.
    Q_INVOKABLE int setGroup(const QString &kind, const QString &folder, const QString &value);
    // Takes back the last thing done here: one fix, or everything one
    // "Fix all", "Fix these" or typed answer wrote. False if any file had
    // changed since and was left alone.
    Q_INVOKABLE bool undoLast();

signals:
    void changed();
    void message(const QString &text);

private:
    bool apply(const Doctor::Finding &f);
    void loadLog();
    void saveLog() const;

    class Library *m_library;
    QString m_logFile;
    std::vector<Doctor::Finding> m_findings;
    struct Entry {
        QString path, field, before, after;
        bool whole = false; // the lists below were recorded
        QStringList beforeAll, afterAll;
        qint64 batch = 0;   // fixes written by one press share a number; 0: on its own
    };
    void changedFindings();
    void finishBatch(int done);
    qint64 m_batch = 0;
    mutable QVariantList m_view;
    mutable bool m_viewStale = true;
    std::vector<Entry> m_log;
    bool m_scanning = false;
    // The undo log on disk: at most this many fixes (the newest), and never
    // read past this size. 50,000 fixes of ordinary tags is about 15 MiB.
    static constexpr size_t MaxLogEntries = 50000;
    static constexpr qint64 MaxLogBytes = 64 * 1024 * 1024;
    static constexpr int MaxAnswer = 200; // a typed answer's longest length
};
