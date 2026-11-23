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

enum class Kind { Watermark, NumberedTitle, MissingTitle, MissingArtist, MissingAlbum, MixedAlbumArtist, Duplicate };

struct Finding {
    Kind kind;
    QString path;          // the file to change (the first of a duplicate set)
    QStringList others;    // for duplicates: the other copies
    QString field;         // "title", "artist", "album", "albumartist", "comment"; empty if not fixable
    QString before, after; // the field's value now, and proposed
};

QString kindName(Kind kind);

// A site's name or address in a tag ("Goin' In [MixFiend.com]",
// "DatPiff Exclusive"), removed with the brackets or separator around it.
// Returns the text unchanged when there's nothing to remove.
QString stripWatermark(const QString &text);
bool hasWatermark(const QString &text);

// Everything wrong with these files, in a stable order.
std::vector<Finding> diagnose(const std::vector<File> &files);

// TagLib, on real files.
bool readTags(const QString &path, Tags &out);
// A field can hold several values (two ARTIST credits on a FLAC). These read
// and write the whole list, exactly as stored.
bool readValues(const QString &path, const QString &field, QStringList &out);
bool writeValues(const QString &path, const QString &field, const QStringList &values);
// The list with its first value changed (or, for an empty value, removed);
// every other value stays.
QStringList withFirst(QStringList values, const QString &value);
// Sets a field's first value and keeps the rest; false if the file couldn't
// be written.
bool writeField(const QString &path, const QString &field, const QString &value);
QString field(const Tags &tags, const QString &name);

}

class LibraryDoctor : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList findings READ findings NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(int fixable READ fixable NOTIFY changed)
    Q_PROPERTY(int undoable READ undoable NOTIFY changed)

public:
    // logFile: where fixes are recorded for undo (empty: the default under
    // ~/.local/state/halo).
    explicit LibraryDoctor(class Library *library, const QString &logFile = {}, QObject *parent = nullptr);

    QVariantList findings() const;
    bool scanning() const { return m_scanning; }
    int fixable() const;
    int undoable() const { return int(m_log.size()); }

    Q_INVOKABLE void scan();
    // Applies finding `index`; false (and a message) if the file changed or
    // couldn't be written.
    Q_INVOKABLE bool fix(int index);
    // Every fixable finding of a kind ("watermark", ...).
    Q_INVOKABLE int fixAll(const QString &kind);
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
    };
    std::vector<Entry> m_log;
    bool m_scanning = false;
};
