#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <array>
#include <memory>
#include <vector>

#include "analysis.h"
#include "engine.h"
#include "library.h"

class Library;
class Stream;
class Player;

// The queue as the window shows it.
class QueueModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { PathRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumIdRole, DurationRole, CurrentRole,
                 DriftRole };
    explicit QueueModel(Player *player);
    int rowCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset();
    void currentMoved(int from, int to);

signals:
    void countChanged();

private:
    Player *m_player;
    // Songs queued before the library has read them (opened from a file
    // manager while the first scan runs) are described from their tags once
    // and kept here until the library changes.
    mutable QHash<QString, Track> m_described;
};

// Saved seconds in tracks.
class MomentModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { PathRole = Qt::UserRole + 1, TitleRole, ArtistRole, AlbumIdRole, SecondsRole, CreatedRole };
    explicit MomentModel(Player *player);
    int rowCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void reset();

signals:
    void countChanged();

private:
    Player *m_player;
};

struct Moment {
    QString path;
    double seconds = 0;
    qint64 created = 0;
};

class Player : public QObject {
    Q_OBJECT
    Q_PROPERTY(QueueModel *queue READ queueModel CONSTANT)
    Q_PROPERTY(MomentModel *moments READ momentModel CONSTANT)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(QString title READ title NOTIFY currentChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY currentChanged)
    Q_PROPERTY(QString album READ album NOTIFY currentChanged)
    Q_PROPERTY(QString albumId READ albumId NOTIFY currentChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool hasTrack READ hasTrack NOTIFY currentChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY currentChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool blend READ blend WRITE setBlend NOTIFY settingsChanged)
    Q_PROPERTY(bool drift READ drift WRITE setDrift NOTIFY settingsChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY settingsChanged)
    Q_PROPERTY(int repeat READ repeat WRITE setRepeat NOTIFY settingsChanged)
    Q_PROPERTY(bool normalize READ normalize WRITE setNormalize NOTIFY settingsChanged)
    Q_PROPERTY(bool calm READ calm WRITE setCalm NOTIFY settingsChanged)
    // The welcome screen has been seen (or music was found without it).
    Q_PROPERTY(bool onboarded READ onboarded WRITE setOnboarded NOTIFY settingsChanged)
    Q_PROPERTY(int visualStyle READ visualStyle WRITE setVisualStyle NOTIFY settingsChanged)
    Q_PROPERTY(bool eqEnabled READ eqEnabled WRITE setEqEnabled NOTIFY eqChanged)
    Q_PROPERTY(QVariantList eqGains READ eqGains NOTIFY eqChanged)
    Q_PROPERTY(double eqPreamp READ eqPreamp WRITE setEqPreamp NOTIFY eqChanged)
    Q_PROPERTY(QString eqPreset READ eqPreset NOTIFY eqChanged)
    Q_PROPERTY(QStringList eqPresets READ eqPresets CONSTANT)
    Q_PROPERTY(double bpm READ bpm NOTIFY analysisChanged)
    Q_PROPERTY(double loudness READ loudness NOTIFY analysisChanged)
    Q_PROPERTY(bool blending READ blending NOTIFY blendingChanged)
    Q_PROPERTY(QString nextTitle READ nextTitle NOTIFY upNextChanged)
    Q_PROPERTY(QString nextArtist READ nextArtist NOTIFY upNextChanged)
    Q_PROPERTY(double blendIn READ blendIn NOTIFY positionChanged)
    Q_PROPERTY(QVariantList currentMoments READ currentMoments NOTIFY momentsChanged)
    Q_PROPERTY(int sleepRemaining READ sleepRemaining NOTIFY sleepChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(int analyzedCount READ analyzedCount NOTIFY analysisProgress)

public:
    enum Repeat { RepeatOff, RepeatAll, RepeatOne };

    Player(Library *library, Analyzer *analyzer, const QString &stateDirectory = {}, QObject *parent = nullptr);
    ~Player() override;

    Engine &engine() { return m_engine; }
    QueueModel *queueModel() { return &m_queueModel; }
    MomentModel *momentModel() { return &m_momentModel; }
    Library *library() const { return m_library; }

    const QStringList &queuePaths() const { return m_queue; }
    Q_INVOKABLE QStringList queueList() const { return m_queue; }
    bool isDrift(int index) const { return m_driftPaths.contains(m_queue.value(index)); }
    const std::vector<Moment> &moments() const { return m_moments; }

    int currentIndex() const { return m_index; }
    QString currentPath() const { return m_queue.value(m_index); }
    QString title() const;
    QString artist() const;
    QString album() const;
    QString albumId() const;
    bool playing() const { return m_playing; }
    bool hasTrack() const { return m_index >= 0 && m_index < m_queue.size(); }
    double position() const { return m_position; }
    double duration() const;
    double volume() const { return m_volume; }
    void setVolume(double volume);
    bool blend() const { return m_blend; }
    void setBlend(bool on);
    bool drift() const { return m_drift; }
    void setDrift(bool on);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on);
    int repeat() const { return m_repeat; }
    void setRepeat(int mode);
    bool normalize() const { return m_normalize; }
    void setNormalize(bool on);
    bool calm() const { return m_calm; }
    void setCalm(bool on);
    bool onboarded() const { return m_onboarded; }
    void setOnboarded(bool on);
    int visualStyle() const { return m_visualStyle; }
    void setVisualStyle(int style);
    bool eqEnabled() const { return m_eqEnabled; }
    void setEqEnabled(bool on);
    QVariantList eqGains() const;
    double eqPreamp() const { return m_eqPreamp; }
    void setEqPreamp(double db);
    QString eqPreset() const { return m_eqPreset; }
    QStringList eqPresets() const;
    double bpm() const { return m_currentAnalysis.bpm; }
    double loudness() const { return m_currentAnalysis.valid ? m_currentAnalysis.loudness : 0; }
    bool blending() const { return m_blendingNow; }
    QString nextTitle() const;
    QString nextArtist() const;
    double blendIn() const;
    QVariantList currentMoments() const;
    int sleepRemaining() const;
    QString problem() const { return m_problem; }
    void setAudioProblem(const QString &problem);
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    int analyzedCount() const;

    // Seconds of the crossfade between two tracks, from their tempos.
    static double blendSeconds(double bpm);
    // Ranks `candidates` for Drift after a track with `seed`; best first.
    static QStringList driftOrder(const TrackAnalysis &seed, const QString &seedFolder,
                                  const QList<QPair<QString, TrackAnalysis>> &candidates);

    QJsonObject status() const;
    // Never write the state file (a second Halo handing over to the first).
    void forgetState() { m_stateFile.clear(); }

public slots:
    void playPaths(const QStringList &paths, int start = 0);
    void playAlbum(const QString &albumId, int start = 0);
    void enqueue(const QStringList &paths);
    void playNext(const QStringList &paths);
    void playAt(int index);
    void play();
    void pause();
    void togglePlay();
    void next();
    void previous();
    void seek(double seconds);
    void seekBy(double seconds);
    void removeAt(int index);
    void move(int from, int to);
    void clearQueue();
    void undo();
    void redo();
    void cycleRepeat();
    void addMoment();
    void jumpMoment(int direction);
    void playMoment(int index);
    void removeMoment(int index);
    void setEqGain(int band, double db);
    void applyEqPreset(const QString &name);
    void startSleep(int minutes);
    void cancelSleep();
    void flush();

signals:
    void currentChanged();
    void playingChanged();
    void positionChanged();
    void volumeChanged();
    void settingsChanged();
    void eqChanged();
    void analysisChanged();
    void blendingChanged();
    void upNextChanged();
    void momentsChanged();
    void sleepChanged();
    void problemChanged();
    void undoChanged();
    void analysisProgress();
    void seeked(double seconds);
    void message(const QString &text);

private:
    struct Snapshot {
        QStringList queue;
        int index;
    };

    void tick();
    void startTrack(int index, double at, bool autoplay);
    void prepareNext();
    void dropPrepared();
    int nextIndex() const;
    void advanceTo(int index);
    void stopAtEnd();
    void maybeDrift();
    void reclaim();
    void pushUndo();
    void afterQueueEdit();
    void applyEq();
    double endPoint() const;
    void updateGain(int deck, const QString &path);
    void countPlay();
    void scheduleSave();
    void save();
    void restore();
    void setPlaying(bool playing);

    Library *m_library;
    Analyzer *m_analyzer;
    Engine m_engine;
    QueueModel m_queueModel;
    MomentModel m_momentModel;
    QString m_stateFile;

    QStringList m_queue;
    QStringList m_unshuffled;
    QSet<QString> m_driftPaths;
    QStringList m_history;
    int m_index = -1;
    // The GUI's view of the two decks: which stream it put on each, which
    // one is playing, and a stream waiting to fill before it starts.
    std::array<Stream *, 2> m_streams{nullptr, nullptr};
    int m_active = 0;
    int m_pendingDeck = -1;
    int m_preparedIndex = -1;
    QString m_preparedPath;
    std::vector<Stream *> m_graveyard;
    QSet<QString> m_failed;
    Track m_currentTrack, m_nextTrack;
    qint64 m_lastPositionSave = 0;
    qint64 m_blendPosted = 0;
    int m_lastSwitches = 0;
    bool m_blendingNow = false;

    bool m_playing = false;
    double m_position = 0;
    double m_pendingSeek = -1;
    double m_volume = 0.8;
    bool m_blend = true;
    bool m_drift = true;
    bool m_shuffle = false;
    int m_repeat = RepeatOff;
    bool m_normalize = true;
    bool m_calm = false;
    bool m_onboarded = false;
    int m_visualStyle = 0;
    bool m_eqEnabled = false;
    std::array<double, 10> m_eqGains{};
    double m_eqPreamp = 0;
    QString m_eqPreset = "Flat";
    TrackAnalysis m_currentAnalysis;
    bool m_counted = false;
    double m_listened = 0;

    std::vector<Moment> m_moments;
    std::vector<Snapshot> m_undo, m_redo;

    QTimer m_tick;
    QTimer m_saveTimer;
    QElapsedTimer m_sleepClock;
    int m_sleepMinutes = 0;
    bool m_sleepFading = false;
    double m_sleepVolume = 0;
    QString m_problem, m_audioProblem;
};
