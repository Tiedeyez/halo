#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <cmath>
#include <vector>

// What CymtX knows about a track after listening to it once.
struct TrackAnalysis {
    double bpm = 0;          // 0 when no steady beat was found
    double loudness = -14;   // integrated loudness, LUFS (BS.1770 by ffmpeg's ebur128)
    double truePeak = NAN;   // dBTP, 4x oversampled; NaN until measured
    double audibleStart = 0; // seconds of silence before the music starts
    double audibleEnd = 0;   // where the music stops, seconds (0 = unknown)
    double duration = 0;     // seconds analysed
    bool valid = false;

    bool hasTruePeak() const { return std::isfinite(truePeak); }

    QJsonObject toJson() const;
    static TrackAnalysis fromJson(const QJsonObject &o);
};

namespace Analysis {
constexpr int Rate = 22050;

// The pure maths, on mono samples at `rate`; tests feed synthetic signals.
TrackAnalysis analyze(const float *mono, size_t count, int rate = Rate);
double estimateBpm(const float *mono, size_t count, int rate = Rate);
double loudness(const float *mono, size_t count, int rate = Rate);
void audibleEdges(const float *mono, size_t count, int rate, double &start, double &end);

// Decodes a file with ffmpeg and analyses it, blocking. For the CLI.
TrackAnalysis analyzeFile(const QString &path, QString *error = nullptr);
QStringList ffmpegArguments(const QString &path);
// Reads integrated loudness and true peak from ebur128's summary on
// ffmpeg's stderr. False when either is missing (or -inf, for silence).
bool parseEbur128(const QByteArray &log, double &lufs, double &truePeak);
}

// Analyses tracks in the background, one ffmpeg at a time at the lowest CPU
// priority, and remembers the results by path and modification time.
class Analyzer : public QObject {
    Q_OBJECT
    Q_PROPERTY(int analyzed READ analyzedCount NOTIFY progress)

public:
    explicit Analyzer(const QString &cacheFile = {}, QObject *parent = nullptr);
    ~Analyzer() override;

    // A result if we have a current one for this file.
    bool lookup(const QString &path, TrackAnalysis &out) const;
    // Not analysed, changed since, or analysed before true peak was measured.
    bool needsWork(const QString &path) const;
    bool has(const QString &path) const { return m_results.contains(path); }
    // Jumps the queue: wanted now (current or next track).
    void prioritize(const QString &path);
    // Queues at the back, for the library sweep.
    void enqueue(const QStringList &paths);
    void setBackground(bool enabled);
    int analyzedCount() const { return int(m_results.size()); }
    int pendingCount() const { return int(m_queue.size()); }
    void flush();

signals:
    void analyzed(const QString &path);
    void progress();

private:
    struct Entry {
        qint64 mtime = 0;
        TrackAnalysis analysis;
    };
    void next();
    void finished();
    void load();
    void scheduleSave();

    QString m_cacheFile;
    QHash<QString, Entry> m_results;
    QStringList m_queue;
    QStringList m_urgent;
    QProcess m_process;
    QString m_running;
    QByteArray m_buffer;
    bool m_background = true;
    QTimer m_save;
};
