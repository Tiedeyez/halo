// CymtX: a music player for Omarchy. Your library, mixed like a DJ would.

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QQmlComponent>
#include <QUrl>
#include <cstdio>

#include "doctor.h"
#include "headphones.h"
#include "kaleidopilot.h"
#include "pointer.h"
#include "analysis.h"
#include "audiooutput.h"
#include "cli.h"
#include "library.h"
#include "lyrics.h"
#include "player.h"
#include "playlists.h"
#include "remote.h"
#include "sandplate.h"
#include "safety.h"
#include "stream.h"
#include "theme.h"
#include "translator.h"
#include "visual.h"
#include "visualizers.h"

static constexpr const char *Version = "1.1.0";

int main(int argc, char *argv[]) {
    // CymtX was Halo: the first start takes a copy of what Halo kept.
    Safety::adoptOldFolders(Safety::ownRoots());
    // Before anything is written: CymtX's files are private to you.
    Safety::makePrivate(Safety::ownFolders());
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    // Commands need no display, so an agent can run them over SSH.
    if (Cli::isCommand(arguments)) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QCoreApplication app(argc, argv);
        app.setApplicationName("cymtx");
        app.setApplicationVersion(Version);
        return Cli::run(arguments);
    }

    // "cymtx open a.flac", "cymtx a.flac" (the launcher's %F), or nothing.
    QStringList files = arguments.mid(1);
    if (!files.isEmpty() && files.first() == "open")
        files.removeFirst();
    for (QString &f : files) {
        if (f.startsWith("file://"))
            f = QUrl(f).toLocalFile();
        f = QFileInfo(f).absoluteFilePath();
    }

    // A player needs no low latency; ask PipeWire for ~21 ms periods unless
    // the environment says otherwise.
    if (qEnvironmentVariableIsEmpty("PIPEWIRE_LATENCY"))
        qputenv("PIPEWIRE_LATENCY", QByteArray::number(AudioOutput::PeriodFrames) + "/" +
                                        QByteArray::number(Stream::SampleRate));

    QGuiApplication app(argc, argv);
    app.setApplicationName("cymtx");
    app.setApplicationVersion(Version);
    app.setDesktopFileName("cymtx");
    app.setWindowIcon(QIcon::fromTheme("cymtx"));
    QQuickStyle::setStyle("Material");

    // One CymtX at a time: a second launch (a song double-clicked in the file
    // manager while CymtX plays) hands its files to the first and exits,
    // before it reads the library or the saved queue, so it is quick and
    // never writes over the running CymtX's state.
    auto handOver = [&files] {
        QJsonArray list;
        for (const QString &f : files)
            list.append(f);
        // Generous: the request is queued even if that CymtX is busy.
        Remote::send({{"cmd", "open"}, {"args", list}}, nullptr, 8000);
    };
    if (Remote::running()) {
        handOver();
        return 0;
    }

    Translator translator;
    app.installTranslator(&translator);
    // CYMTX_THEME_DIR points CymtX at one theme's folder (one with a
    // colors.toml), to check how it looks without switching Omarchy to it.
    Theme theme(qEnvironmentVariable("CYMTX_THEME_DIR"));
    Library library;
    Analyzer analyzer;
    Player player(&library, &analyzer);
    Remote remote(&player);
    Playlists playlists(&library, &analyzer);
    remote.setPlaylists(&playlists);
    QObject::connect(&playlists, &Playlists::message, &player, &Player::message);
    // Lyrics follow the song: a new song looks them up, every position
    // update moves the line being sung.
    LyricsService lyrics;
    remote.setLyrics(&lyrics);
    auto loadLyrics = [&] {
        lyrics.load(player.currentPath(), player.title(), player.artist(), player.album(), player.duration());
    };
    QObject::connect(&player, &Player::currentChanged, &lyrics, loadLyrics);
    QObject::connect(&player, &Player::positionChanged, &lyrics, [&] { lyrics.follow(player.position()); });
    QObject::connect(&player, &Player::seeked, &lyrics, [&](double seconds) { lyrics.follow(seconds); });

    // Two launched at the same moment: the one that lost the race hands over
    // too, and leaves the saved state alone.
    if (!remote.listen()) {
        player.forgetState();
        handOver();
        return 0;
    }

    library.loadCache();
    library.rescan();
    // The background sweep starts once the scan has settled.
    QObject::connect(&library, &Library::scanningChanged, &analyzer, [&] {
        if (library.scanning())
            return;
        QStringList paths;
        for (const Track &t : library.tracks())
            paths << t.path;
        analyzer.enqueue(paths);
    });

    AudioOutput audio(&player.engine());
    Headphones headphones(&player.engine());
    QObject::connect(&audio, &AudioOutput::deviceChanged, &headphones, &Headphones::outputChanged);
    QObject::connect(&audio, &AudioOutput::problemChanged, &player, [&] { player.setAudioProblem(audio.problem()); });
    audio.start();
    player.setAudioProblem(audio.problem().isEmpty() ? remote.error() : audio.problem());

    if (Safety::systemTool("ffmpeg").isEmpty())
        player.setAudioProblem(QCoreApplication::translate("main", "ffmpeg isn't installed. Install it with: omarchy-pkg-add ffmpeg"));

    Spectrum spectrum(&player.engine());
    qmlRegisterType<HaloView>("CymtX", 1, 0, "HaloView");
    qmlRegisterType<KaleidoPilot>("CymtX", 1, 0, "KaleidoPilot");
    qmlRegisterType<SandPlate>("CymtX", 1, 0, "SandPlate");
    qmlRegisterType<PixelVisual>("CymtX", 1, 0, "PixelVisual");
    qmlRegisterSingletonType(QUrl("qrc:/Style.qml"), "CymtX", 1, 0, "Style");
    qmlRegisterSingletonType(QUrl("qrc:/Secrets.qml"), "CymtX", 1, 0, "Secrets");
    qmlRegisterSingletonType(QUrl("qrc:/PlaylistUi.qml"), "CymtX", 1, 0, "PlaylistUi");
    qmlRegisterUncreatableType<QueueModel>("CymtX", 1, 0, "QueueModel", "from player");
    qmlRegisterUncreatableType<MomentModel>("CymtX", 1, 0, "MomentModel", "from player");

    AlbumModel albums(&library);
    TrackModel tracks(&library);
    TrackModel albumTracks(&library);
    albumTracks.setMode(TrackModel::AlbumTracks);

    QQmlApplicationEngine engine;
    engine.addImageProvider("cover", new CoverProvider);
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    auto *context = engine.rootContext();
    context->setContextProperty("translator", &translator);
    context->setContextProperty("theme", &theme);
    context->setContextProperty("library", &library);
    context->setContextProperty("player", &player);
    context->setContextProperty("lyrics", &lyrics);
    context->setContextProperty("headphones", &headphones);
    LibraryDoctor doctor(&library);
    context->setContextProperty("doctor", &doctor);
    qmlRegisterUncreatableType<LyricsService>("CymtX", 1, 0, "Lyrics", "from main");
    context->setContextProperty("spectrumSource", &spectrum);
    context->setContextProperty("albums", &albums);
    context->setContextProperty("tracks", &tracks);
    context->setContextProperty("albumTracks", &albumTracks);
    PlaylistTrackModel playlistTracks(&playlists, &library);
    context->setContextProperty("playlists", &playlists);
    context->setContextProperty("playlistTracks", &playlistTracks);
    PointerWatch pointer;
    context->setContextProperty("pointer", &pointer);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        QQmlComponent check(&engine, QUrl("qrc:/Main.qml"));
        fprintf(stderr, "cymtx: the interface didn't load:\n%s\n", qPrintable(check.errorString()));
        return 1;
    }
    engine.rootObjects().first()->installEventFilter(&pointer);

    // MPRIS Raise and a second launch bring the window forward. Wayland
    // doesn't let an app raise itself, so ask Hyprland when it's there.
    QObject::connect(&remote, &Remote::raiseRequested, &app, [&] {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            window->show();
            window->raise();
            window->requestActivate();
        }
        if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
            QProcess::startDetached(Safety::systemTool("hyprctl"), {"dispatch", "hl.dsp.focus({ window = \"class:cymtx\" })"});
    });

    QObject::connect(&remote, &Remote::grabRequested, &app, [&](const QString &path) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        if (!window)
            return;
        // A window that isn't on screen (a QA copy on a hidden workspace)
        // grabs only part of itself; rendering the content item offscreen
        // gets the whole frame at the window's size.
        if (window->isExposed()) {
            window->grabWindow().save(path);
            return;
        }
        QSharedPointer<QQuickItemGrabResult> grab = window->contentItem()->grabToImage(window->size());
        if (grab)
            QObject::connect(grab.data(), &QQuickItemGrabResult::ready, &app, [grab, path] { grab->image().save(path); });
    });
    QObject::connect(&remote, &Remote::uiRequested, &app, [&](const QString &name, const QString &value) {
        static const QStringList allowed{"view", "nowPlaying", "helpVisible", "width", "height", "siege", "dialog"};
        if (!allowed.contains(name))
            return;
        if (name == "siege") {
            if (QObject *secrets = engine.singletonInstance<QObject *>("CymtX", "Secrets"))
                secrets->setProperty("siege", value == "true");
            return;
        }
        QObject *root = engine.rootObjects().first();
        // For QA harnesses: open a playlist dialog (smart, new, rename).
        if (name == "dialog") {
            QMetaObject::invokeMethod(root, "openDialog", Q_ARG(QVariant, value));
            return;
        }
        if (name == "view")
            QMetaObject::invokeMethod(root, "show", Q_ARG(QVariant, value.toInt()));
        else if (name == "nowPlaying" || name == "helpVisible")
            root->setProperty(name.toLatin1().constData(), value == "true");
        else
            root->setProperty(name.toLatin1().constData(), value.toInt());
    });

    if (!files.isEmpty()) {
        QJsonArray list;
        for (const QString &f : files)
            list.append(f);
        remote.handle({{"cmd", "open"}, {"args", list}});
    }

    const int code = app.exec();
    player.flush();
    library.flush();
    analyzer.flush();
    return code;
}
