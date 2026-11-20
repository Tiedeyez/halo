#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class Player;

// org.mpris.MediaPlayer2: who Halo is, for media keys, the Omarchy bar and
// every now-playing widget.
class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ uriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes)

public:
    explicit MprisRoot(QObject *parent);
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("Halo"); }
    QString desktopEntry() const { return QStringLiteral("halo"); }
    QStringList uriSchemes() const { return {"file"}; }
    QStringList mimeTypes() const;

signals:
    void raiseRequested();

public slots:
    void Raise() { emit raiseRequested(); }
    void Quit();
};

// org.mpris.MediaPlayer2.Player: the transport.
class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPlay)
    Q_PROPERTY(bool CanSeek READ canPlay)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    MprisPlayer(QObject *parent, Player *player);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const;
    void setShuffle(bool on);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canControl() const { return true; }

    static QVariantMap metadataFor(const QString &path, const QString &title, const QString &artist,
                                   const QString &album, double duration, const QString &artUrl);

signals:
    void Seeked(qlonglong position);

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &track, qlonglong position);
    void OpenUri(const QString &uri);

private:
    void changed(const QStringList &names);
    void refreshArt();

    Player *m_player;
    QString m_artUrl;
    QString m_artFor;
};

// The MPRIS service and the local socket the `halo` command talks to.
class LyricsService;
class Playlists;

class Remote : public QObject {
    Q_OBJECT

public:
    explicit Remote(Player *player, QObject *parent = nullptr);
    ~Remote() override;

    // Socket name in $XDG_RUNTIME_DIR.
    static QString socketName();
    // Sends one request to a running Halo. Returns false if none answers.
    static bool send(const QJsonObject &request, QJsonObject *reply, int timeoutMs = 2000);
    // Whether another Halo is listening, even if it is too busy to answer yet.
    static bool running(int timeoutMs = 1000);

    // Starts listening; false (and nothing changed) if another Halo already
    // owns the socket.
    bool listen();
    QJsonObject handle(const QJsonObject &request);
    // Why the control socket couldn't be opened, if it couldn't.
    QString error() const { return m_error; }
    // Lets `halo lyrics` read and steer the lyrics; optional.
    void setLyrics(LyricsService *lyrics) { m_lyrics = lyrics; }
    void setPlaylists(Playlists *playlists) { m_playlists = playlists; }

signals:
    void raiseRequested();
    // For looking at the window without a display: save it as a PNG, or
    // set a property of the window (which view is showing).
    void grabRequested(const QString &path);
    void uiRequested(const QString &name, const QString &value);

private:
    Player *m_player;
    LyricsService *m_lyrics = nullptr;
    Playlists *m_playlists = nullptr;
    QJsonObject playlistCommand(const QStringList &args);
    QString m_error;
    QLocalServer m_server;
    MprisRoot *m_root = nullptr;
    MprisPlayer *m_mpris = nullptr;
};
