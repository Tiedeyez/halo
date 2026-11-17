// Halo: a music player for Omarchy. Your library, mixed like a DJ would.

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QUrl>

#include "headphones.h"
#include "mandelpilot.h"
#include "analysis.h"
#include "audiooutput.h"
#include "cli.h"
#include "library.h"
#include "lyrics.h"
#include "player.h"
#include "remote.h"
#include "stream.h"
#include "theme.h"
#include "translator.h"
#include "visual.h"

static constexpr const char *Version = "0.1.0";

int main(int argc, char *argv[]) {
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);

    // Commands need no display, so an agent can run them over SSH.
    if (Cli::isCommand(arguments)) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QCoreApplication app(argc, argv);
        app.setApplicationName("halo");
        app.setApplicationVersion(Version);
        return Cli::run(arguments);
    }

    // "halo open a.flac", "halo a.flac" (the launcher's %F), or nothing.
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
    app.setApplicationName("halo");
    app.setApplicationVersion(Version);
    app.setDesktopFileName("halo");
    app.setWindowIcon(QIcon::fromTheme("halo"));
    QQuickStyle::setStyle("Material");

    // One Halo at a time: a second launch (a song double-clicked in the file
    // manager while Halo plays) hands its files to the first and exits,
    // before it reads the library or the saved queue, so it is quick and
    // never writes over the running Halo's state.
    auto handOver = [&files] {
        QJsonArray list;
        for (const QString &f : files)
            list.append(f);
        // Generous: the request is queued even if that Halo is busy.
        Remote::send({{"cmd", "open"}, {"args", list}}, nullptr, 8000);
    };
    if (Remote::running()) {
        handOver();
        return 0;
    }

    Translator translator;
    app.installTranslator(&translator);
    Theme theme;
    Library library;
    Analyzer analyzer;
    Player player(&library, &analyzer);
    Remote remote(&player);
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

    if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
        player.setAudioProblem(QCoreApplication::translate("main", "ffmpeg isn't installed. Install it with: omarchy-pkg-add ffmpeg"));

    Spectrum spectrum(&player.engine());
    qmlRegisterType<HaloView>("Halo", 1, 0, "HaloView");
    qmlRegisterType<MandelPilot>("Halo", 1, 0, "MandelPilot");
    qmlRegisterSingletonType(QUrl("qrc:/Style.qml"), "Halo", 1, 0, "Style");
    qmlRegisterUncreatableType<QueueModel>("Halo", 1, 0, "QueueModel", "from player");
    qmlRegisterUncreatableType<MomentModel>("Halo", 1, 0, "MomentModel", "from player");

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
    qmlRegisterUncreatableType<LyricsService>("Halo", 1, 0, "Lyrics", "from main");
    context->setContextProperty("spectrumSource", &spectrum);
    context->setContextProperty("albums", &albums);
    context->setContextProperty("tracks", &tracks);
    context->setContextProperty("albumTracks", &albumTracks);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // MPRIS Raise and a second launch bring the window forward. Wayland
    // doesn't let an app raise itself, so ask Hyprland when it's there.
    QObject::connect(&remote, &Remote::raiseRequested, &app, [&] {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
            window->show();
            window->raise();
            window->requestActivate();
        }
        if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE"))
            QProcess::startDetached("hyprctl", {"dispatch", "hl.dsp.focus({ window = \"class:halo\" })"});
    });

    QObject::connect(&remote, &Remote::grabRequested, &app, [&](const QString &path) {
        if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
            window->grabWindow().save(path);
    });
    QObject::connect(&remote, &Remote::uiRequested, &app, [&](const QString &name, const QString &value) {
        static const QStringList allowed{"view", "nowPlaying", "helpVisible", "width", "height"};
        if (!allowed.contains(name))
            return;
        QObject *root = engine.rootObjects().first();
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
