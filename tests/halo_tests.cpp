#include <QDir>
#include <sys/stat.h>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSaveFile>
#include <QColor>
#include <QWheelEvent>
#include <QQuickView>
#include <QQmlPropertyMap>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlComponent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <cmath>
#include <vector>

#include "analysis.h"
#include "dsp.h"
#include "engine.h"
#include "library.h"
#include "lyrics.h"
#include "doctor.h"
#include "headphones.h"
#include "kaleidopilot.h"
#include "player.h"
#include "remote.h"
#include "visualizers.h"
#include "safety.h"
#include "ring.h"
#include "stream.h"
#include "theme.h"
#include "translator.h"
#include "visual.h"

namespace {

constexpr double Pi = 3.14159265358979323846;

// A 16-bit stereo WAV, so the tests need nothing but ffmpeg.
void writeWav(const QString &path, const std::vector<float> &mono, int rate = 48000) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const quint32 frames = quint32(mono.size());
    const quint32 dataBytes = frames * 4;
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char *>(&v), 2); };
    f.write("RIFF");
    u32(36 + dataBytes);
    f.write("WAVEfmt ");
    u32(16);
    u16(1);
    u16(2);
    u32(quint32(rate));
    u32(quint32(rate) * 4);
    u16(4);
    u16(16);
    f.write("data");
    u32(dataBytes);
    for (float s : mono) {
        const qint16 v = qint16(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767));
        f.write(reinterpret_cast<const char *>(&v), 2);
        f.write(reinterpret_cast<const char *>(&v), 2);
    }
}

// Clicks of decaying noise on every beat, like a kick drum.
std::vector<float> clickTrack(double bpm, double seconds, int rate) {
    std::vector<float> out(size_t(seconds * rate), 0.0f);
    const double period = 60.0 / bpm * rate;
    quint32 seed = 12345;
    for (double beat = 0; beat < out.size(); beat += period)
        for (int i = 0; i < rate / 20 && size_t(beat) + size_t(i) < out.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            const float noise = float(int(seed >> 9) % 2000 - 1000) / 1000.0f;
            out[size_t(beat) + size_t(i)] += 0.8f * noise * std::exp(-float(i) / (rate / 120.0f));
        }
    return out;
}

void waitReady(Stream &s) {
    QElapsedTimer t;
    t.start();
    while (!s.ready() && t.elapsed() < 5000)
        QThread::msleep(5);
}

// The engine renders far faster than real time in a test, so short files
// must be fully decoded first, or their end looks like a slow disk.
// Waits for ffmpeg to finish a test file. Generous, so a loaded machine
// doesn't fail the test half-decoded; callers check it actually finished.
bool waitDecoded(Stream &s) {
    QElapsedTimer t;
    t.start();
    while (!s.decoded() && t.elapsed() < 60000)
        QThread::msleep(5);
    return s.decoded();
}

}


// A tiny stand-in for lrclib.net: answers /api/get and /api/search from
// canned replies and records every request it was sent.
class FakeLrclib : public QObject {
    Q_OBJECT
public:
    struct Reply {
        int code = 200;
        QByteArray body;
        QByteArray extraHeaders;
    };
    QHash<QString, Reply> replies; // keyed by path ("/api/get")
    QStringList requests;           // "path?query"
    QStringList agents;

    FakeLrclib() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    buffer[socket] += socket->readAll();
                    if (!buffer[socket].contains("\r\n\r\n"))
                        return;
                    const QByteArray head = buffer.take(socket);
                    const QList<QByteArray> lines = head.split('\n');
                    const QByteArray target = lines.value(0).split(' ').value(1);
                    for (const QByteArray &l : lines)
                        if (l.toLower().startsWith("user-agent:"))
                            agents << QString::fromUtf8(l.mid(11).trimmed());
                    requests << QString::fromUtf8(QByteArray::fromPercentEncoding(target));
                    const QString path = QString::fromUtf8(target.split('?').value(0));
                    const Reply r = replies.value(path, Reply{404, R"({"name":"TrackNotFound"})", {}});
                    QByteArray out = "HTTP/1.1 " + QByteArray::number(r.code) + " X\r\nContent-Type: application/json\r\n" +
                                     r.extraHeaders + "Content-Length: " + QByteArray::number(r.body.size()) +
                                     "\r\nConnection: close\r\n\r\n" + r.body;
                    socket->write(out);
                    socket->disconnectFromHost();
                });
            }
        });
    }
    QString url() const { return QString("http://127.0.0.1:%1").arg(server.serverPort()); }

private:
    QTcpServer server;
    QHash<QTcpSocket *, QByteArray> buffer;
};

class HaloTests : public QObject {
    Q_OBJECT

private slots:
    // Halo finds Omarchy's language choice where omarchy-language puts it.
    void languageFileFollowsOmarchy() {
        const QByteArray oldOverride = qgetenv("OMARCHY_LANGUAGE_FILE"), oldConfig = qgetenv("XDG_CONFIG_HOME");
        qunsetenv("OMARCHY_LANGUAGE_FILE");
        qputenv("XDG_CONFIG_HOME", "/x/config");
        QCOMPARE(Translator::defaultLanguageFile(), QString("/x/config/omarchy/language"));
        qputenv("OMARCHY_LANGUAGE_FILE", "/y/language");
        QCOMPARE(Translator::defaultLanguageFile(), QString("/y/language"));
        qunsetenv("XDG_CONFIG_HOME");
        qunsetenv("OMARCHY_LANGUAGE_FILE");
        QCOMPARE(Translator::defaultLanguageFile(), QDir::homePath() + "/.config/omarchy/language");
        if (!oldOverride.isEmpty()) qputenv("OMARCHY_LANGUAGE_FILE", oldOverride);
        if (!oldConfig.isEmpty()) qputenv("XDG_CONFIG_HOME", oldConfig);
    }

    void ringWrapsAround() {
        Ring ring(8);
        float in[6] = {1, 2, 3, 4, 5, 6}, out[8] = {};
        QCOMPARE(ring.write(in, 6), size_t(6));
        QCOMPARE(ring.read(out, 4), size_t(4));
        QCOMPARE(ring.write(in, 6), size_t(6));
        QCOMPARE(ring.writable(), size_t(0));
        QCOMPARE(ring.write(in, 1), size_t(0)); // full: refuses, never overwrites
        QCOMPARE(ring.read(out, 8), size_t(8));
        QCOMPARE(out[0], 5.0f);
        QCOMPARE(out[2], 1.0f);
        QCOMPARE(out[7], 6.0f);
    }

    void tempoOfAClickTrack_data() {
        QTest::addColumn<double>("bpm");
        QTest::newRow("95") << 95.0;
        QTest::newRow("120") << 120.0;
        QTest::newRow("140") << 140.0;
    }

    void tempoOfAClickTrack() {
        QFETCH(double, bpm);
        const auto signal = clickTrack(bpm, 30, Analysis::Rate);
        const double found = Analysis::estimateBpm(signal.data(), signal.size());
        QVERIFY2(std::fabs(found - bpm) < 1.5, qPrintable(QString("found %1 for %2").arg(found).arg(bpm)));
    }

    void noBeatInSilenceOrASteadyTone() {
        std::vector<float> silence(Analysis::Rate * 20, 0.0f);
        QCOMPARE(Analysis::estimateBpm(silence.data(), silence.size()), 0.0);
        std::vector<float> tone(Analysis::Rate * 20);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.3f * float(std::sin(2 * Pi * 440 * double(i) / Analysis::Rate));
        QCOMPARE(Analysis::estimateBpm(tone.data(), tone.size()), 0.0);
    }

    void loudnessOfASine() {
        // A 1 kHz sine at amplitude 0.1 is -23 dB mean square; K-weighting
        // adds a fraction of a dB at 1 kHz.
        std::vector<float> tone(Analysis::Rate * 10);
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.1f * float(std::sin(2 * Pi * 1000 * double(i) / Analysis::Rate));
        const double lufs = Analysis::loudness(tone.data(), tone.size());
        QVERIFY2(lufs > -24.5 && lufs < -21.5, qPrintable(QString::number(lufs)));
        // Twice the amplitude is 6 dB louder.
        for (float &s : tone)
            s *= 2;
        QVERIFY(std::fabs(Analysis::loudness(tone.data(), tone.size()) - lufs - 6.02) < 0.2);
    }

    void readsEbur128Summary() {
        const QByteArray log = "[Parsed_ebur128_1 @ 0x1] Summary:\n\n  Integrated loudness:\n    I:         -11.2 LUFS\n"
                               "    Threshold: -21.3 LUFS\n\n  Loudness range:\n    LRA:         5.6 LU\n\n"
                               "  True peak:\n    Peak:        0.3 dBFS\n";
        double lufs = 0, peak = 0;
        QVERIFY(Analysis::parseEbur128(log, lufs, peak));
        QCOMPARE(lufs, -11.2);
        QCOMPARE(peak, 0.3);
        // Silence: no finite peak, so nothing is taken from it.
        const QByteArray silent = "Summary:\n  Integrated loudness:\n    I: -70.0 LUFS\n  True peak:\n    Peak: -inf dBFS\n";
        lufs = 1, peak = 1;
        QVERIFY(!Analysis::parseEbur128(silent, lufs, peak));
        QCOMPARE(lufs, 1.0);
        QVERIFY(!Analysis::parseEbur128("no summary here", lufs, peak));
    }

    void measuresTruePeakBetweenSamples() {
        // A sine at a quarter of the sample rate, 45 degrees out of phase:
        // every sample is +-0.7071 (-3 dBFS), but the waveform between them
        // reaches 1.0 (0 dBTP). A sample-peak meter would miss 3 dB.
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg isn't installed");
        QTemporaryDir dir;
        const QString path = dir.filePath("overs.wav");
        const int rate = 48000, frames = rate * 5;
        QByteArray pcm;
        for (int i = 0; i < frames; ++i) {
            const float s = float(std::sin(2 * Pi * 0.25 * i + Pi / 4));
            pcm.append(reinterpret_cast<const char *>(&s), 4).append(reinterpret_cast<const char *>(&s), 4);
        }
        auto u32 = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
        auto u16 = [](quint16 v) { return QByteArray(reinterpret_cast<const char *>(&v), 2); };
        QByteArray wav = "RIFF" + u32(36 + pcm.size()) + "WAVEfmt " + u32(16) + u16(3) + u16(2) + u32(rate) +
                         u32(rate * 8) + u16(8) + u16(32) + "data" + u32(pcm.size()) + pcm;
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(wav);
        f.close();
        QString error;
        const TrackAnalysis a = Analysis::analyzeFile(path, &error);
        QVERIFY2(a.hasTruePeak(), qPrintable(error));
        // ffmpeg's meter reads high near the top of the band (+0.6 dB here,
        // up to about +1 dB at 15 kHz) and never low, so the gain cap built
        // on it errs on the safe side. What matters is that the hidden 3 dB
        // is caught.
        QVERIFY2(a.truePeak > -0.3 && a.truePeak < 1.2, qPrintable(QString::number(a.truePeak)));
        // And it survives the cache.
        QCOMPARE(TrackAnalysis::fromJson(a.toJson()).truePeak, std::round(a.truePeak * 10) / 10);
        QVERIFY(!TrackAnalysis::fromJson(QJsonObject{{"lufs", -14}}).hasTruePeak());
    }

    void limiterIsTransparentThenHoldsTheCeiling() {
        using Dsp::TruePeakLimiter;
        const int delay = TruePeakLimiter::Delay;
        auto run = [delay](const std::vector<float> &x) {
            TruePeakLimiter lim;
            std::vector<float> y;
            for (size_t i = 0; i < x.size() + size_t(delay); ++i) {
                float l = i < x.size() ? x[i] : 0, r = l;
                lim.process(l, r);
                if (i >= size_t(delay))
                    y.push_back(l);
            }
            return y;
        };
        auto sine = [](double f, double amp, double phase = 0) {
            std::vector<float> x(48000);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = float(amp * std::sin(2 * Pi * f * double(i) / 48000 + phase));
            return x;
        };
        // Below the ceiling: the same samples, only delayed.
        const auto quiet = sine(1000, 0.5);
        QVERIFY(run(quiet) == quiet);

        // Peaks between samples are held under -1 dBTP: check the output
        // at 8 points between every pair of samples.
        const auto overs = run(sine(12000, 1.0, Pi / 4));
        double peak = 0;
        for (size_t n = 32; n + 32 < overs.size(); ++n)
            for (int k = 0; k < 8; ++k) {
                double s = 0;
                for (int j = -31; j <= 32; ++j) {
                    const double t = j - k / 8.0;
                    const double sinc = t == 0 ? 1 : std::sin(Pi * t) / (Pi * t);
                    s += overs[n + j] * sinc * (0.42 + 0.5 * std::cos(Pi * t / 32) + 0.08 * std::cos(2 * Pi * t / 32));
                }
                peak = std::max(peak, std::fabs(s));
            }
        QVERIFY2(20 * std::log10(peak) < -0.9, qPrintable(QString::number(20 * std::log10(peak))));

        // Loud bass is turned down, not distorted: the residual after a
        // best-fit 60 Hz sine is under -80 dB.
        const auto bass = run(sine(60, 1.4));
        double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
        for (size_t n = 24000; n < 44000; ++n) {
            const double s = std::sin(2 * Pi * 60 * n / 48000), c = std::cos(2 * Pi * 60 * n / 48000);
            ss += s * s, cc += c * c, sc += s * c, ys += bass[n] * s, yc += bass[n] * c;
        }
        const double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
        double sig = 0, res = 0;
        for (size_t n = 24000; n < 44000; ++n) {
            const double fit = a * std::sin(2 * Pi * 60 * n / 48000) + b * std::cos(2 * Pi * 60 * n / 48000);
            sig += fit * fit, res += (bass[n] - fit) * (bass[n] - fit);
        }
        QVERIFY2(10 * std::log10(res / sig) < -80, qPrintable(QString::number(10 * std::log10(res / sig))));
    }

    void wheelGlidesAndTouchpadIsDirect() {
        QQmlPropertyMap playerStub, themeStub;
        playerStub.insert("calm", false);
        themeStub.insert("accent", QColor("#7aa2f7"));
        themeStub.insert("accentForeground", QColor("#000000"));
        themeStub.insert("palette", QVariantList{});
        themeStub.insert("radius", 4);
        QQuickView view;
        view.rootContext()->setContextProperty("player", &playerStub);
        view.rootContext()->setContextProperty("theme", &themeStub);
        static bool registered = false;
        if (!registered) {
            qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Style.qml")), "Halo", 1, 0, "Style");
            qmlRegisterType(QUrl::fromLocalFile(QFINDTESTDATA("../src/SmoothWheel.qml")), "Halo", 1, 0, "SmoothWheel");
            registered = true;
        }
        QQmlComponent component(view.engine());
        component.setData(R"(
            import QtQuick
            import Halo
            ListView {
                id: list
                width: 300; height: 300
                model: 100
                delegate: Rectangle { width: 300; height: 40; color: "grey" }
                SmoothWheel { flickable: list }
            })", QUrl());
        auto *list = qobject_cast<QQuickItem *>(component.create(view.rootContext()));
        QVERIFY2(list, qPrintable(component.errorString()));
        view.setContent(QUrl(), &component, list);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto wheel = [&](QPoint pixels, QPoint angle) {
            QWheelEvent e(QPointF(150, 150), view.mapToGlobal(QPointF(150, 150)), pixels, angle, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&view, &e);
        };
        // A mouse notch glides there.
        wheel({}, {0, -120});
        QTest::qWait(60);
        const double partway = list->property("contentY").toDouble();
        QVERIFY2(partway > 0 && partway < 120, qPrintable(QString::number(partway)));
        QTRY_COMPARE(list->property("contentY").toDouble(), 120.0);
        // Two quick notches add up rather than restarting from where it is.
        wheel({}, {0, -120});
        wheel({}, {0, -120});
        QTRY_COMPARE(list->property("contentY").toDouble(), 360.0);
        // A touchpad moves at once, by its pixels.
        wheel({0, -30}, {0, -24});
        QCOMPARE(list->property("contentY").toDouble(), 390.0);
        // Never past the end.
        for (int i = 0; i < 60; ++i)
            wheel({}, {0, -120});
        QTRY_COMPARE(list->property("contentY").toDouble(), 100.0 * 40 - 300);
    }

    void siegeShootsTheWordsAndKeepsItsKeys() {
        QTemporaryDir cache, state, music;
        LyricsService lyricsService(cache.path(), state.path());
        lyricsService.setEndpoint("http://127.0.0.1:9");
        const QString song = music.filePath("song.mp3");
        QFile(song).open(QIODevice::WriteOnly);
        QFile lrc(music.filePath("song.lrc"));
        QVERIFY(lrc.open(QIODevice::WriteOnly));
        lrc.write("[00:02.00]WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW\n[00:04.00]MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM\n"
                  "[00:06.00]HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH\n[00:08.00]the end\n");
        lrc.close();
        lyricsService.load(song, "Song", "Artist", "", 60);
        QCOMPARE(lyricsService.kind(), int(Lyrics::Synced));
        lyricsService.follow(0);

        QQmlPropertyMap playerStub, themeStub, spectrumStub, translatorStub;
        playerStub.insert("calm", false);
        playerStub.insert("playing", true);
        playerStub.insert("bpm", 120.0);
        themeStub.insert("accent", QColor("#7aa2f7"));
        themeStub.insert("accentForeground", QColor("#000000"));
        themeStub.insert("palette", QVariantList{});
        themeStub.insert("radius", 4);
        spectrumStub.insert("pulse", 0.0);
        spectrumStub.insert("level", 0.0);
        translatorStub.insert("language", "en");
        QQuickView view;
        view.rootContext()->setContextProperty("player", &playerStub);
        view.rootContext()->setContextProperty("theme", &themeStub);
        view.rootContext()->setContextProperty("spectrumSource", &spectrumStub);
        view.rootContext()->setContextProperty("translator", &translatorStub);
        view.rootContext()->setContextProperty("lyrics", &lyricsService);
        qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Style.qml")), "Halo", 1, 0, "Style");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Secrets.qml")), "Halo", 1, 0, "Secrets");
        qmlRegisterType(QUrl::fromLocalFile(QFINDTESTDATA("../src/SiegeGame.qml")), "Halo", 1, 0, "SiegeGame");
        qmlRegisterUncreatableType<LyricsService>("Halo", 1, 0, "Lyrics", "from main");
        QQmlComponent component(view.engine());
        component.setData(R"(
            import QtQuick
            import Halo
            Item {
                width: 640; height: 420
                property bool spacePlayed: false
                property alias game: game
                Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: parent.spacePlayed = true }
                Component.onCompleted: Secrets.siege = true
                SiegeGame { id: game; anchors.fill: parent }
            })", QUrl());
        auto *root = qobject_cast<QQuickItem *>(component.create(view.rootContext()));
        QVERIFY2(root, qPrintable(component.errorString()));
        view.setContent(QUrl(), &component, root);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto *game = root->property("game").value<QQuickItem *>();
        QVERIFY(game);
        QTRY_VERIFY(game->hasActiveFocus());
        QCOMPARE(game->property("rows").toList().size(), 4);

        // Past the intro, hold fire while the song moves on.
        QElapsedTimer clock;
        clock.start();
        double t = 0;
        auto play = [&](int ms) {
            const qint64 until = clock.elapsed() + ms;
            while (clock.elapsed() < until) {
                QTest::qWait(16);
                t += 0.016;
                lyricsService.follow(t);
            }
        };
        QTest::mouseMove(&view, QPoint(320, 200)); // steer to the middle
        play(1700);
        QTest::keyPress(&view, Qt::Key_Space);
        play(1200);
        QTest::keyRelease(&view, Qt::Key_Space);
        QVERIFY2(game->property("score").toInt() >= 20, qPrintable(game->property("score").toString()));
        // The letters shot are gone from the rows' text, gaps kept.
        int holed = 0;
        for (const QVariant &row : game->property("rows").toList()) {
            const QVariantMap r = row.toMap();
            QCOMPARE(r["shown"].toString().size(), r["text"].toString().size());
            holed += r["shown"] != r["text"];
        }
        QVERIFY2(holed >= 2, qPrintable(QString::number(holed)));
        // Space was the game's, not the player's.
        QCOMPARE(root->property("spacePlayed").toBool(), false);
        QVERIFY(game->property("multiplier").toInt() >= 1);

        // Esc leaves.
        QTest::keyClick(&view, Qt::Key_Escape);
        QObject *secrets = view.engine()->singletonInstance<QObject *>("Halo", "Secrets");
        QVERIFY(secrets);
        QCOMPARE(secrets->property("siege").toBool(), false);
        QVERIFY(secrets->property("best").toInt() >= game->property("score").toInt());
    }

    void kaleidoscopeComesFromTheFile() {
        using namespace Kaleido;
        const Song a = fromFile("/m/HCSM5/03.mp3", "3 Way", "Young Dolph", "HCSM5");
        const Song again = fromFile("/m/HCSM5/03.mp3", "3 Way", "Young Dolph", "HCSM5");
        QCOMPARE(a.folds, again.folds);
        QCOMPARE(a.cx, again.cx);
        QCOMPARE(a.hue, again.hue);
        // Same album, same colours; another song, another shape.
        const Song b = fromFile("/m/HCSM5/04.mp3", "Hustler Mentality", "Young Dolph", "HCSM5");
        QCOMPARE(a.hue, b.hue);
        QVERIFY(a.cx != b.cx || a.cy != b.cy);
        // Across many songs: most mirror counts appear, every constant stays
        // in the box that makes lace.
        QSet<int> mirrors;
        for (int i = 0; i < 200; ++i) {
            const Song s = fromFile(QString("/m/%1.mp3").arg(i), QString("Song %1").arg(i), "Artist", "Album");
            mirrors.insert(s.folds);
            QVERIFY(s.folds >= 5 && s.folds <= 12);
            QVERIFY(s.cx >= 0.52 && s.cx <= 0.82 && s.cy >= 0.32 && s.cy <= 0.62);
        }
        QVERIFY2(mirrors.size() >= 6, qPrintable(QString::number(mirrors.size())));
    }

    void kaleidoscopeTurnsWithTheBeat() {
        using namespace Kaleido;
        Pilot pilot;
        Song song = fromFile("/m/a.mp3", "A", "B", "C");
        pilot.setSong(song, 120, 240, -10);
        const double wedge = 2 * Pi / song.folds;
        // Settle, then 8 beats at 120 BPM (4 s) turn exactly one mirror.
        double position = 10;
        for (int f = 0; f < 120; ++f)
            pilot.step(1.0 / 60, position += 1.0 / 60, 0, 0, 0);
        const double before = pilot.spin();
        for (int f = 0; f < 240; ++f)
            pilot.step(1.0 / 60, position += 1.0 / 60, 0, 0, 0);
        QVERIFY2(std::fabs(std::fabs(pilot.spin() - before) - wedge) < wedge * 0.03,
                 qPrintable(QString("%1 vs %2").arg(std::fabs(pilot.spin() - before)).arg(wedge)));
        // The song's design is stable during a phrase, then morphs smoothly
        // during its final four beats into a deterministic next design.
        double cx0, cy0, twist0, cx, cy, twist;
        pilot.shapeAt(1.0, cx0, cy0, twist0);
        pilot.shapeAt(13.0, cx, cy, twist);
        QCOMPARE(cx, cx0);
        QCOMPARE(cy, cy0);
        QCOMPARE(twist, twist0);
        double cxAt28, cyAt28, twistAt28;
        pilot.shapeAt(14.0, cxAt28, cyAt28, twistAt28); // 28 beats at 120 BPM
        double cxAt30, cyAt30, twistAt30;
        pilot.shapeAt(15.0, cxAt30, cyAt30, twistAt30);
        QVERIFY(cxAt30 != cxAt28 || cyAt30 != cyAt28 || twistAt30 != twistAt28);
        double cxAt32, cyAt32, twistAt32;
        pilot.shapeAt(16.0, cxAt32, cyAt32, twistAt32);
        QVERIFY(cxAt32 != cx0 || cyAt32 != cy0 || twistAt32 != twist0);
        double cxAt44, cyAt44, twistAt44;
        pilot.shapeAt(22.0, cxAt44, cyAt44, twistAt44);
        QCOMPARE(cxAt44, cxAt32);
        QCOMPARE(cyAt44, cyAt32);
        QCOMPARE(twistAt44, twistAt32);
        double cxRepeat, cyRepeat, twistRepeat;
        pilot.shapeAt(16.0, cxRepeat, cyRepeat, twistRepeat);
        QCOMPARE(cxRepeat, cxAt32);
        QCOMPARE(cyRepeat, cyAt32);
        QCOMPARE(twistRepeat, twistAt32);

        // Beat pulses do not change the phrase design or its easing.
        Pilot pulseFree, pulseDriven;
        pulseFree.setSong(song, 120, 240, -10);
        pulseDriven.setSong(song, 120, 240, -10);
        for (int f = 0; f < 60 * 10; ++f) {
            const double pulse = (f % 30) < 4 ? 1.0 : 0.0;
            const double at = f / 60.0;
            pulseFree.step(1.0 / 60, at, 0, 0.6, 0.4);
            pulseDriven.step(1.0 / 60, at, pulse, 0.6, 0.4);
            QCOMPARE(pulseDriven.cx(), pulseFree.cx());
            QCOMPARE(pulseDriven.cy(), pulseFree.cy());
            QCOMPARE(pulseDriven.twist(), pulseFree.twist());
        }
        QCOMPARE(pulseDriven.beats(), 20);
        // Each beat kicks: the view snaps in by a fifth, then springs back
        // before the next.
        Pilot punch;
        punch.setSong(song, 120, 240, -10);
        punch.step(1.0 / 60, 1, 0, 0, 0);
        const double rest = punch.zoom();
        punch.step(1.0 / 60, 1, 1.0, 0, 0);
        QVERIFY2(punch.zoom() < rest * 0.8, qPrintable(QString("%1 vs %2").arg(punch.zoom()).arg(rest)));
        QVERIFY(punch.kick() > 0.8);
        for (int f = 0; f < 29; ++f)
            punch.step(1.0 / 60, 1, 0, 0, 0);
        QVERIFY(punch.kick() < 0.02);
        QVERIFY(std::fabs(punch.zoom() - rest) < rest * 0.01);
        // A seek a minute on turns the short way and glides: no frame turns
        // more than a twentieth of a mirror.
        double spin = pilot.spin();
        position += 60;
        for (int f = 0; f < 120; ++f) {
            pilot.step(1.0 / 60, position += 1.0 / 60, 0, 0, 0);
            QVERIFY(std::fabs(pilot.spin() - spin) < wedge / 20);
            spin = pilot.spin();
        }
        // A new song with other mirrors crossfades in over a second and a half.
        Song other = song;
        other.folds = song.folds == 6 ? 8 : 6;
        pilot.setSong(other, 90, 200, -14);
        QCOMPARE(pilot.foldMix(), 0.0);
        QCOMPARE(pilot.previousFolds(), song.folds);
        for (int f = 0; f < 100; ++f)
            pilot.step(1.0 / 60, 1, 0, 0, 0);
        QVERIFY(pilot.foldMix() > 0.9);
    }

    void headphoneNamesMatchOnlyExactly() {
        QCOMPARE(Hp::modelPart("Ana's AirPods Pro"), QString("AirPods Pro"));
        QCOMPARE(Hp::modelPart("WH-1000XM4 (A2DP)"), QString("WH-1000XM4"));
        QCOMPARE(Hp::modelPart("Arctis 7 Analog Stereo"), QString("Arctis 7"));
        QCOMPARE(Hp::modelPart("Jabra Elite 85h Hands-Free"), QString("Jabra Elite 85h"));

        Hp::Database db;
        QVERIFY(db.load(R"({"headphones":[
            ["Sony","WH-1000XM4","over_the_ear","oratory1990",-6.1,[[1,105,4.5,0.7],[0,3000,-2,2],[2,10000,-1,0.7]]],
            ["Apple","AirPods Pro","in_ear","crinacle",-3,[[0,200,1,1]]],
            ["Acme","Elite","in_ear","x",-1,[[0,200,1,1]]],
            ["Other","Elite","over_the_ear","x",-1,[[0,200,1,1]]],
            ["Koss","770","over_the_ear","x",-1,[[0,200,1,1]]]]})"));
        QCOMPARE(db.match("WH-1000XM4")->title(), QString("Sony WH-1000XM4"));
        QCOMPARE(db.match("Sony WH-1000XM4 (LE)")->title(), QString("Sony WH-1000XM4"));
        QCOMPARE(db.match("Sam’s AirPods Pro")->title(), QString("Apple AirPods Pro"));
        // Two products called "Elite": only the brand settles it.
        QVERIFY(db.match("Elite") == nullptr);
        QCOMPARE(db.match("Acme Elite")->title(), QString("Acme Elite"));
        // Never a near miss, a number alone, or the computer's own output.
        QVERIFY(db.match("WH-1000XM3") == nullptr);
        QVERIFY(db.match("770") == nullptr);
        QVERIFY(db.match("Built-in Audio Analog Stereo") == nullptr);
        QVERIFY(db.match("WH202A") == nullptr);

        // The filters do what the profile says: +4.5 dB well under the
        // 105 Hz shelf, -2 dB at the 3 kHz peak.
        const auto filters = Hp::design(*db.match("WH-1000XM4"));
        auto gainAt = [&](double f) {
            std::vector<Dsp::Biquad> state(filters.size());
            double peak = 0;
            for (int n = 0; n < 48000; ++n) {
                float x = float(std::sin(2 * Pi * f * n / 48000));
                for (size_t i = 0; i < filters.size(); ++i)
                    x = state[i].process(filters[i], x);
                if (n > 24000)
                    peak = std::max(peak, double(std::fabs(x)));
            }
            return 20 * std::log10(peak);
        };
        QVERIFY2(std::fabs(gainAt(30) - 4.5) < 0.6, qPrintable(QString::number(gainAt(30))));
        QVERIFY2(std::fabs(gainAt(3000) + 2) < 0.4, qPrintable(QString::number(gainAt(3000))));

        // The real bundled database loads and knows the well-known ones.
        QFile real(QFINDTESTDATA("../src/data/headphones.json"));
        QVERIFY(real.open(QIODevice::ReadOnly));
        Hp::Database all;
        QVERIFY(all.load(real.readAll()));
        QVERIFY2(all.size() > 6000, qPrintable(QString::number(all.size())));
        QVERIFY(all.match("WH-1000XM4") != nullptr);
        QVERIFY(all.match("Built-in Audio Analog Stereo") == nullptr);
    }

    void doctorStripsWatermarks() {
        QCOMPARE(Doctor::stripWatermark("Goin' In [MixFiend.com]"), QString("Goin' In"));
        QCOMPARE(Doctor::stripWatermark("Hustler Mentality - DatPiff Exclusive"), QString("Hustler Mentality"));
        QCOMPARE(Doctor::stripWatermark("LiveMixtapes.com | Young Dolph"), QString("Young Dolph"));
        QCOMPARE(Doctor::stripWatermark("www.site.net"), QString());
        // Ordinary names are left alone, dots and all.
        QCOMPARE(Doctor::stripWatermark("Mr. Brightside"), QString("Mr. Brightside"));
        QCOMPARE(Doctor::stripWatermark("21 Savage"), QString("21 Savage"));
        QVERIFY(!Doctor::hasWatermark("Dr. Dre"));
    }

    void doctorProposesOneFixPerField() {
        using namespace Doctor;
        std::vector<File> files;
        auto add = [&](const QString &path, Tags t) { files.push_back({path, t}); };
        add("/m/HCSM5-2015/01_3_Way.mp3", {"01 3 Way [DatPiff.com]", "Young Dolph", "HCSM5", "Young Dolph", "", 1, 200});
        add("/m/HCSM5-2015/02_Hustler.mp3", {"Hustler Mentality", "", "HCSM5", "Young Dolph", "", 2, 180});
        add("/m/HCSM5-2015/03_Pull_Up.mp3", {"Pull Up", "Young Dolph", "", "Dolph", "", 3, 190});
        add("/m/Other/Pull Up.mp3", {"Pull Up", "Young Dolph", "Other", "", "", 1, 191});
        add("/m/Other/x.mp3", {"Clean", "Artist", "LiveMixtapes.com", "", "", 2, 100});
        const auto found = diagnose(files);
        QMultiHash<QString, QString> fields;
        for (const Finding &f : found)
            if (!f.field.isEmpty()) {
                QVERIFY2(!fields.contains(f.path, f.field), qPrintable(f.path + " " + f.field));
                fields.insert(f.path, f.field);
            }
        auto find = [&](Kind k, const QString &path) -> const Finding * {
            for (const Finding &f : found)
                if (f.kind == k && f.path == path)
                    return &f;
            return nullptr;
        };
        // Watermark and number go in one title fix.
        const Finding *title = find(Kind::NumberedTitle, "/m/HCSM5-2015/01_3_Way.mp3");
        QVERIFY(title);
        QCOMPARE(title->after, QString("3 Way"));
        QVERIFY(!find(Kind::Watermark, "/m/HCSM5-2015/01_3_Way.mp3"));
        // Missing artist comes from the album artist.
        QCOMPARE(find(Kind::MissingArtist, "/m/HCSM5-2015/02_Hustler.mp3")->after, QString("Young Dolph"));
        // Missing album comes from the folder, scene year tidied.
        QCOMPARE(find(Kind::MissingAlbum, "/m/HCSM5-2015/03_Pull_Up.mp3")->after, QString("HCSM5 (2015)"));
        // The odd album artist out is brought in line.
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/HCSM5-2015/03_Pull_Up.mp3")->after, QString("Young Dolph"));
        // An album that is only a site's name is missing, not emptied.
        const Finding *album = find(Kind::MissingAlbum, "/m/Other/x.mp3");
        QVERIFY(album);
        QCOMPARE(album->before, QString("LiveMixtapes.com"));
        QCOMPARE(album->after, QString("Other"));
        // The same song in two places, pointed out only.
        const Finding *dup = find(Kind::Duplicate, "/m/HCSM5-2015/03_Pull_Up.mp3");
        QVERIFY(dup);
        QCOMPARE(dup->others, QStringList{"/m/Other/Pull Up.mp3"});
        QVERIFY(dup->field.isEmpty());
    }

    void doctorFixesRealFilesAndUndoes() {
        QTemporaryDir music, state;
        const QString song = music.filePath("01_Song.mp3");
        QProcess ff;
        ff.start("ffmpeg", {"-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=12", "-metadata",
                            "title=01 Song [DatPiff.com]", "-metadata", "artist=Tester", "-metadata", "album=Album",
                            "-y", song});
        QVERIFY(ff.waitForFinished(30000));
        QCOMPARE(ff.exitCode(), 0);
        Library library({music.path()}, state.filePath("cache"), state.filePath("state"));
        library.rescan();
        QTRY_COMPARE_WITH_TIMEOUT(int(library.tracks().size()), 1, 20000);
        const QString log = state.filePath("doctor.json");
        {
            LibraryDoctor doctor(&library, log);
            doctor.scan();
            QTRY_VERIFY_WITH_TIMEOUT(!doctor.scanning(), 20000);
            QCOMPARE(doctor.fixable(), 1);
            QCOMPARE(doctor.findings().first().toMap().value("after").toString(), QString("Song"));
            QVERIFY(doctor.fix(0));
            Doctor::Tags t;
            QVERIFY(Doctor::readTags(song, t));
            QCOMPARE(t.title, QString("Song"));
            QCOMPARE(t.artist, QString("Tester"));
            QCOMPARE(doctor.undoable(), 1);
        }
        // The undo log survives a restart.
        LibraryDoctor doctor(&library, log);
        QCOMPARE(doctor.undoable(), 1);
        QVERIFY(doctor.undoLast());
        Doctor::Tags t;
        QVERIFY(Doctor::readTags(song, t));
        QCOMPARE(t.title, QString("01 Song [DatPiff.com]"));
        // A file changed after the scan is never overwritten.
        doctor.scan();
        QTRY_VERIFY_WITH_TIMEOUT(!doctor.scanning(), 20000);
        QVERIFY(Doctor::writeField(song, "title", "Edited by hand"));
        QSignalSpy said(&doctor, &LibraryDoctor::message);
        QVERIFY(!doctor.fix(0));
        QCOMPARE(said.size(), 1);
        QVERIFY(Doctor::readTags(song, t));
        QCOMPARE(t.title, QString("Edited by hand"));
    }

    void lostAndFoundBringsBackFavourites() {
        const qint64 day = 86400, now = 1800000000;
        std::vector<Track> tracks;
        QHash<QString, int> plays;
        QHash<QString, qint64> last;
        auto add = [&](const QString &artist, int n, qint64 daysAgo, qint64 addedDaysAgo = 400) {
            Track t;
            t.path = QString("/m/%1/%2").arg(artist).arg(tracks.size());
            t.artist = artist;
            t.mtime = (now - addedDaysAgo * day) * 1000;
            if (n > 0) {
                plays.insert(t.path, n);
                last.insert(t.path, now - daysAgo * day);
            }
            tracks.push_back(t);
        };
        for (int i = 0; i < 6; ++i) add("A", 12, 120);   // loved, gone four months
        for (int i = 0; i < 6; ++i) add("B", 5, 60);
        for (int i = 0; i < 6; ++i) add("C", 30, 3);     // played this week: not lost
        for (int i = 0; i < 6; ++i) add("D", 1, 200);    // heard once: not a favourite
        for (int i = 0; i < 6; ++i) add("E", 0, 0, 90);  // never played, here three months
        for (int i = 0; i < 6; ++i) add("F", 0, 0, 5);   // never played, but just added

        const auto pick = Library::pickLostAndFound(tracks, plays, last, now, 16);
        QCOMPARE(int(pick.size()), 16);
        QSet<QString> artists;
        for (int i : pick)
            artists.insert(tracks[size_t(i)].artist);
        // Favourites first, then the long-unheard; never this week's, a
        // one-off or something just added.
        QVERIFY(artists.contains("A") && artists.contains("B") && artists.contains("E"));
        QVERIFY(!artists.contains("C") && !artists.contains("D") && !artists.contains("F"));
        for (int k = 0; k < 12; ++k)
            QVERIFY(tracks[size_t(pick[size_t(k)])].artist != "E");
        // Asked for more than there is history for, it reaches the just
        // added ones last, so a new library's page isn't empty.
        const auto all = Library::pickLostAndFound(tracks, plays, last, now, 24);
        QCOMPARE(int(all.size()), 24);
        QSet<QString> late;
        for (int i : all)
            late.insert(tracks[size_t(i)].artist);
        QVERIFY(late.contains("F") && !late.contains("C"));
        // Spread out: never the same artist twice in a row while another is left.
        int repeats = 0;
        for (size_t k = 1; k < pick.size(); ++k)
            repeats += tracks[size_t(pick[k])].artist == tracks[size_t(pick[k - 1])].artist;
        QVERIFY2(repeats <= 4, qPrintable(QString::number(repeats)));
        // The same all day, different another day.
        QCOMPARE(Library::pickLostAndFound(tracks, plays, last, now + 3600, 16), pick);
        QVERIFY(Library::pickLostAndFound(tracks, plays, last, now + 3 * day, 6) !=
                Library::pickLostAndFound(tracks, plays, last, now, 6));
    }

    void findsWhereTheMusicIs() {
        const int rate = Analysis::Rate;
        std::vector<float> s(size_t(rate) * 6, 0.0f);
        for (size_t i = size_t(rate) * 2; i < size_t(rate) * 5; ++i)
            s[i] = 0.2f * float(std::sin(2 * Pi * 300 * double(i) / rate));
        double start = 0, end = 0;
        Analysis::audibleEdges(s.data(), s.size(), rate, start, end);
        QVERIFY(std::fabs(start - 2.0) < 0.06);
        QVERIFY(std::fabs(end - 5.0) < 0.06);
    }

    void analysesAFileWithFfmpeg() {
        QTemporaryDir dir;
        const QString path = dir.filePath("beat.wav");
        writeWav(path, clickTrack(110, 25, 48000));
        QString error;
        const TrackAnalysis a = Analysis::analyzeFile(path, &error);
        QVERIFY2(a.valid, qPrintable(error));
        QVERIFY(std::fabs(a.bpm - 110) < 1.5);
        QVERIFY(std::fabs(a.duration - 25) < 0.1);
        // The cache round-trips.
        const TrackAnalysis back = TrackAnalysis::fromJson(a.toJson());
        QCOMPARE(back.bpm, std::round(a.bpm * 10) / 10);
    }

    void blendIsOnePhrase() {
        QCOMPARE(Player::blendSeconds(0), 6.0);
        QCOMPARE(Player::blendSeconds(60), 8.0);
        QVERIFY(std::fabs(Player::blendSeconds(126) - 8 * 60.0 / 126 * 2) < 1e-9); // 3.8 s doubled
        QVERIFY(std::fabs(Player::blendSeconds(200) - 4.8) < 1e-9);
        for (double bpm = 40; bpm <= 220; bpm += 7) {
            const double s = Player::blendSeconds(bpm);
            QVERIFY(s >= 4 && s <= 10);
        }
    }

    void driftPrefersCloseTempoAndLevelOnAnotherAlbum() {
        TrackAnalysis seed;
        seed.bpm = 120;
        seed.loudness = -10;
        auto with = [](double bpm, double lufs) {
            TrackAnalysis a;
            a.bpm = bpm;
            a.loudness = lufs;
            a.valid = true;
            return a;
        };
        const QList<QPair<QString, TrackAnalysis>> candidates{
            {"/m/b/far-tempo.mp3", with(150, -10)},
            {"/m/b/quiet.mp3", with(121, -22)},
            {"/m/b/half-time.mp3", with(60.5, -10)},
            {"/m/b/close.mp3", with(121, -10.5)},
            {"/m/a/same-album.mp3", with(120, -10)},
        };
        const QStringList order = Player::driftOrder(seed, "/m/a", candidates);
        QCOMPARE(order.first(), QString("/m/b/close.mp3"));
        QVERIFY(order.indexOf("/m/b/half-time.mp3") < order.indexOf("/m/b/far-tempo.mp3"));
        QVERIFY(order.indexOf("/m/b/close.mp3") < order.indexOf("/m/a/same-album.mp3"));
        // A 25 % tempo jump is the worst match; loudness is evened out anyway.
        QCOMPARE(order.last(), QString("/m/b/far-tempo.mp3"));
    }

    // Two files of opposite constant level: the hand-over must be on the
    // very next sample, with no silent frame between them.
    void gaplessIsSampleExact() {
        QTemporaryDir dir;
        const QString a = dir.filePath("a.wav"), b = dir.filePath("b.wav");
        writeWav(a, std::vector<float>(24000, 0.25f));
        writeWav(b, std::vector<float>(24000, -0.25f));
        Engine engine;
        auto *sa = new Stream(a, 0), *sb = new Stream(b, 0);
        sa->start();
        sb->start();
        QVERIFY(waitDecoded(*sa));
        QVERIFY(waitDecoded(*sb));
        engine.setVolume(1.0f);
        engine.setPaused(false);
        engine.load(0, sa);
        engine.playNow(0);
        engine.load(1, sb);
        engine.armGapless(1);

        std::vector<float> out;
        std::vector<float> block(512 * 2);
        for (int i = 0; i < 120; ++i) {
            engine.render(block.data(), 512);
            out.insert(out.end(), block.begin(), block.end());
        }
        QCOMPARE(engine.switches(), 1);
        QCOMPARE(engine.current(), 1);
        // After the pause ramp and de-click, find the sign change.
        size_t flip = 0;
        for (size_t i = 2000; i < out.size() / 2; ++i)
            if (out[i * 2] < 0) {
                flip = i;
                break;
            }
        QVERIFY(flip > 0);
        QVERIFY(out[(flip - 1) * 2] > 0.1f); // the last sample of A is full level
        QVERIFY(out[flip * 2] < -0.1f);       // and B starts on the next
        // Both tracks ran out, so the engine has handed both back.
        QSet<Stream *> back;
        while (Stream *s = engine.retired())
            back.insert(s);
        QCOMPARE(back, (QSet<Stream *>{sa, sb}));
        qDeleteAll(back);
    }

    void blendCrossfadesWithEqualPower() {
        QTemporaryDir dir;
        const QString a = dir.filePath("a.wav"), b = dir.filePath("b.wav");
        writeWav(a, std::vector<float>(48000 * 3, 0.25f));
        writeWav(b, std::vector<float>(48000 * 3, 0.25f));
        Engine engine;
        auto *sa = new Stream(a, 0), *sb = new Stream(b, 0);
        sa->start();
        sb->start();
        waitReady(*sa);
        waitReady(*sb);
        engine.setVolume(1.0f);
        engine.setPaused(false);
        engine.load(0, sa);
        engine.playNow(0);
        std::vector<float> block(480 * 2);
        for (int i = 0; i < 20; ++i)
            engine.render(block.data(), 480);
        engine.load(1, sb);
        engine.blend(1, 1.0, false);
        // Halfway through, two equal tracks at cos and sin of 45 degrees sum
        // to sqrt(2) times one: louder, never quieter.
        for (int i = 0; i < 50; ++i)
            engine.render(block.data(), 480);
        QVERIFY(engine.blending());
        QVERIFY(block[0] > 0.25f * 1.3f);
        for (int i = 0; i < 60; ++i)
            engine.render(block.data(), 480);
        QVERIFY(!engine.blending());
        QCOMPARE(engine.current(), 1);
        QVERIFY(std::fabs(block[0] - 0.25f) < 0.02f);
        bool retiredA = false;
        while (Stream *s = engine.retired()) {
            retiredA = retiredA || s == sa;
            delete s;
        }
        QVERIFY(retiredA);
        engine.unload(1);
        engine.render(block.data(), 16);
        while (Stream *s = engine.retired())
            delete s;
    }

    void streamReportsAnUnreadableFile() {
        QTemporaryDir dir;
        const QString path = dir.filePath("broken.mp3");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not audio at all");
        f.close();
        Stream s(path, 0);
        s.start();
        waitReady(s);
        QVERIFY(s.failed());
        QVERIFY(!s.error().isEmpty());
    }

    void spectrumFindsATone() {
        std::vector<float> samples(2048);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = 0.5f * float(std::sin(2 * Pi * 1000 * double(i) / 48000));
        const auto bands = Spectrum::analyse(samples);
        const auto peak = std::max_element(bands.begin(), bands.end()) - bands.begin();
        // 1 kHz sits about 57 % of the way from 40 Hz to 16 kHz on a log scale.
        QVERIFY2(peak > 30 && peak < 40, qPrintable(QString::number(peak)));
        QVERIFY(bands[size_t(peak)] > 0.8f);
        QVERIFY(bands[2] < 0.3f);
    }

    void namesFromMessyFiles() {
        QCOMPARE(Library::titleFromFileName("/x/01 - Song Name.mp3"), QString("Song Name"));
        QCOMPARE(Library::titleFromFileName("/x/07_Another_One.flac"), QString("Another One"));
        QCOMPARE(Library::titleFromFileName("/x/2Pac.mp3"), QString("2Pac"));
        QCOMPARE(Library::cleanTitle("05 Young Dolph - Goin' In", "Young Dolph"), QString("Goin' In"));
        QCOMPARE(Library::cleanTitle("Goin' In", "Young Dolph"), QString("Goin' In"));
        QCOMPARE(Library::cleanTitle("21 Savage", ""), QString("21 Savage"));
        QCOMPARE(Library::cleanTitle("99 Problems", "Jay-Z"), QString("99 Problems"));
        QCOMPARE(Library::cleanTitle("03. Intro", ""), QString("Intro"));
        {
            Track t;
            t.title = "10 Mack Maine - Young Mulah Baby (Feat. Gorilla Zoe)";
            t.artist = "Biggy Jiggy Mixtapes";
            t.number = 10;
            Library::splitCredit(t);
            QCOMPARE(t.title, QString("Young Mulah Baby (Feat. Gorilla Zoe)"));
            QCOMPARE(t.artist, QString("Mack Maine"));
            QCOMPARE(t.albumArtist, QString("Biggy Jiggy Mixtapes"));
        }
        {
            // "01 - Lil Wayne - Intro Day 1" with Lil Wayne as the artist:
            // nothing to split, and never "- Lil Wayne" as a performer.
            Track t;
            t.title = "01 - Lil Wayne - Intro Day 1";
            t.artist = "Lil Wayne";
            t.number = 1;
            Library::splitCredit(t);
            QCOMPARE(t.artist, QString("Lil Wayne"));
            QCOMPARE(Library::cleanTitle(t.title, t.artist), QString("Intro Day 1"));
            Track u;
            u.title = "07 - Gudda Gudda - Wall";
            u.artist = "DJ Host";
            u.number = 7;
            Library::splitCredit(u);
            QCOMPARE(u.artist, QString("Gudda Gudda"));
            QCOMPARE(u.title, QString("Wall"));
        }
        {
            // The number isn't this track's, so it's part of the name.
            Track t;
            t.title = "99 Problems - Remix";
            t.artist = "Jay-Z";
            t.number = 4;
            Library::splitCredit(t);
            QCOMPARE(t.title, QString("99 Problems - Remix"));
            QCOMPARE(t.artist, QString("Jay-Z"));
        }
        QCOMPARE(Library::prettyFolderName("/m/Young_Dolph_-_16_Zips-2015"), QString("Young Dolph - 16 Zips (2015)"));
        QVERIFY(Library::isAudioFile("/a/b.FLAC"));
        QVERIFY(!Library::isAudioFile("/a/cover.jpg"));
    }

    void foldersAreAlbumsAndDiscFoldersJoinTheirParent() {
        std::vector<Track> tracks(4);
        tracks[0].path = "/m/Album/CD1/01 a.mp3";
        tracks[0].disc = 1;
        tracks[0].number = 1;
        tracks[1].path = "/m/Album/CD2/01 b.mp3";
        tracks[1].disc = 2;
        tracks[1].number = 1;
        tracks[2].path = "/m/Album/CD1/02 c.mp3";
        tracks[2].disc = 1;
        tracks[2].number = 2;
        tracks[3].path = "/m/Other/x.mp3";
        for (auto &t : tracks) {
            t.artist = "Someone";
            t.album = t.path.contains("Album") ? "The Album" : "";
        }
        const auto albums = Library::buildAlbums(tracks);
        QCOMPARE(int(albums.size()), 2);
        const Album &a = albums[0].folder == "/m/Album" ? albums[0] : albums[1];
        QCOMPARE(a.title, QString("The Album"));
        QCOMPARE(int(a.tracks.size()), 3);
        QCOMPARE(tracks[size_t(a.tracks[0])].path, QString("/m/Album/CD1/01 a.mp3"));
        QCOMPARE(tracks[size_t(a.tracks[1])].path, QString("/m/Album/CD1/02 c.mp3"));
        QCOMPARE(tracks[size_t(a.tracks[2])].path, QString("/m/Album/CD2/01 b.mp3"));
    }

    void scanSkipsIgnoredAndHiddenFolders() {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("keep");
        QDir(dir.path()).mkpath("samples");
        QDir(dir.path()).mkpath(".trash");
        const std::vector<float> second(48000, 0.1f);
        writeWav(dir.filePath("keep/song.wav"), second);
        writeWav(dir.filePath("samples/kick.wav"), second);
        writeWav(dir.filePath(".trash/old.wav"), second);
        QFile marker(dir.filePath("samples/.haloignore"));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.close();
        const auto tracks = Library::scanFolders({dir.path()}, {});
        QCOMPARE(int(tracks.size()), 1);
        QVERIFY(tracks[0].path.endsWith("keep/song.wav"));
        QVERIFY(std::fabs(tracks[0].duration - 1.0) < 0.05);
        QCOMPARE(Library::expand({dir.filePath("keep")}).size(), 1);
    }

    void shortClipsStayOutOfTheLibrary() {
        QTemporaryDir music, cache, state;
        writeWav(music.filePath("one-shot.wav"), std::vector<float>(48000, 0.1f));
        writeWav(music.filePath("song.wav"), std::vector<float>(48000 * 12, 0.1f));
        Library library({music.path()}, cache.path(), state.path());
        QSignalSpy done(&library, &Library::scanningChanged);
        library.rescan();
        QVERIFY(done.wait(10000) || !library.scanning());
        while (library.scanning())
            QVERIFY(done.wait(10000));
        QCOMPARE(library.trackCount(), 1);
        QVERIFY(library.tracks()[0].path.endsWith("song.wav"));
        // A clip is still describable, so it plays when opened directly.
        QVERIFY(library.describe(music.filePath("one-shot.wav")).duration > 0.9);
    }

    void queueUndoAndRedo() {
        QTemporaryDir music, cache, state;
        QStringList files;
        for (int i = 0; i < 3; ++i) {
            files << music.filePath(QString("%1.wav").arg(i));
            writeWav(files.last(), std::vector<float>(48000, 0.0f));
        }
        Library library({music.path()}, cache.path(), state.path());
        Analyzer analyzer(cache.filePath("analysis.json"));
        analyzer.setBackground(false);
        Player player(&library, &analyzer, state.path());
        player.playPaths(files, 0);
        QCOMPARE(player.queuePaths().size(), 3);
        player.removeAt(2);
        QCOMPARE(player.queuePaths().size(), 2);
        player.move(1, 0);
        QCOMPARE(player.currentIndex(), 1); // the playing song moved with it
        player.undo();
        QCOMPARE(player.queuePaths().size(), 2);
        QCOMPARE(player.currentIndex(), 0);
        player.undo();
        QCOMPARE(player.queuePaths().size(), 3);
        player.redo();
        QCOMPARE(player.queuePaths().size(), 2);
        player.clearQueue();
        QCOMPARE(player.queuePaths(), QStringList{files[0]});
        player.undo();
        QCOMPARE(player.queuePaths().size(), 2);

        player.addMoment();
        QCOMPARE(int(player.moments().size()), 1);
        player.addMoment(); // the same second again is refused
        QCOMPARE(int(player.moments().size()), 1);
        player.flush();
    }

    void remoteSpeaksJson() {
        QTemporaryDir music, cache, state;
        Library library({music.path()}, cache.path(), state.path());
        Analyzer analyzer(cache.filePath("analysis.json"));
        Player player(&library, &analyzer, state.path());
        Remote remote(&player);
        const QJsonObject status = remote.handle({{"cmd", "status"}});
        QVERIFY(status.value("ok").toBool());
        QVERIFY(status.value("status").toObject().contains("playing"));
        QVERIFY(!remote.handle({{"cmd", "volume"}, {"args", QJsonArray{"150"}}}).value("ok").toBool());
        QVERIFY(remote.handle({{"cmd", "volume"}, {"args", QJsonArray{"40"}}}).value("ok").toBool());
        QCOMPARE(player.volume(), 0.4);
        QVERIFY(remote.handle({{"cmd", "repeat"}, {"args", QJsonArray{"one"}}}).value("ok").toBool());
        QCOMPARE(player.repeat(), int(Player::RepeatOne));
        QVERIFY(!remote.handle({{"cmd", "nonsense"}}).value("ok").toBool());
        QVERIFY(!remote.handle({{"cmd", "grab"}, {"args", QJsonArray{"/tmp/x.txt"}}}).value("ok").toBool());
        // visual: by name, by number, next/previous wrapping, bad names refused.
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"hologram"}}}).value("visual").toString(), QString("hologram"));
        QCOMPARE(player.visualStyle(), 9);
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"next"}}}).value("visual").toString(), QString("halo"));
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"previous"}}}).value("visual").toString(), QString("hologram"));
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"3"}}}).value("visual").toString(), QString("skyline"));
        QVERIFY(!remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"10"}}}).value("ok").toBool());
        QVERIFY(!remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"mandelbrot"}}}).value("ok").toBool());
        QCOMPARE(remote.handle({{"cmd", "visual"}}).value("visual").toString(), QString("skyline"));
    }

    void pixelVisualsDrawOnlyTheThemesColours() {
        // Every mode draws something at rest and on a beat, at the grid's
        // size, and never a colour that isn't the window, the theme's
        // palette or a blend between them (no stray white or black).
        const QStringList palette{"#2d95dc", "#46ade6", "#2d7fe0", "#9d8cf0", "#6cc6ee", "#c3ecf8"};
        for (int mode = 0; mode < PixelVisual::Count; ++mode) {
            PixelVisual v;
            v.setWidth(48);
            v.setHeight(40);
            v.setPalette(palette);
            v.setWindow(QColor("#0a1430"));
            v.setMode(mode);
            for (int i = 0; i < 90; ++i)
                v.step(1.0 / 60, i % 30 == 0 ? 1.0 : 0.2, 0.6, 0.7, 0.5, 0.4);
            const QImage &f = v.frame();
            QCOMPARE(f.size(), QSize(48, 40));
            int lit = 0;
            for (int y = 0; y < f.height(); ++y)
                for (int x = 0; x < f.width(); ++x) {
                    const QColor c = QColor::fromRgba(f.pixel(x, y));
                    if (c.alpha() == 0)
                        continue;
                    ++lit;
                    QVERIFY2(c.red() < 250 || c.green() < 250 || c.blue() < 250, qPrintable(PixelVisual::modeName(mode)));
                }
            // Waterfall is a spectrogram: with no spectrum attached it is
            // silence, so dark is right.
            if (mode != PixelVisual::Waterfall)
                QVERIFY2(lit > 20, qPrintable(PixelVisual::modeName(mode) + " drew almost nothing"));
        }
    }

    void themeAccentFallsBack() {
        QTemporaryDir dir;
        QCOMPARE(Theme::readAccent(dir.filePath("missing.toml")), QString("#FFD60A"));

        // The visuals' palette: six theme colours, the accent standing in
        // for any a theme leaves out.
        QFile partial(dir.filePath("partial.toml"));
        QVERIFY(partial.open(QIODevice::WriteOnly));
        partial.write("accent = \"#82FB9C\"\ncyan = \"#7cf8f7\"\nmagenta = '#86a7df'\nforeground = \"#ddf7ff\"\n");
        partial.close();
        QCOMPARE(Theme::readPalette(partial.fileName(), "#82FB9C"),
                 (QStringList{"#82FB9C", "#7cf8f7", "#82FB9C", "#86a7df", "#82FB9C", "#ddf7ff"}));
        QCOMPARE(Theme::readPalette(dir.filePath("missing.toml"), "#FFD60A").size(), 6);
        QFile f(dir.filePath("colors.toml"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("accent = \"#89b4fa\"\n");
        f.close();
        QCOMPARE(Theme::readAccent(dir.filePath("colors.toml")), QString("#89b4fa"));
    }

    // ---- lyrics ----

    void lrcParsesStampsOffsetsAndWords() {
        const Lyrics l = Lyrics::parseLrc("[ti:Song]\n[offset:+500]\n[00:10.50]First line\n"
                                          "[00:05.00][00:20.00]Chorus\n[00:12.00]\n[00:30.25]Last\n");
        QCOMPARE(int(l.kind), int(Lyrics::Synced));
        QCOMPARE(int(l.lines.size()), 4);
        // Sorted by time, the offset shown 0.5 s earlier.
        QCOMPARE(l.lines[0].text, QString("Chorus"));
        QVERIFY(std::abs(l.lines[0].start - 4.5) < 1e-6);
        QVERIFY(std::abs(l.lines[1].start - 10.0) < 1e-6);
        // The empty stamp at 12 s ends "First line" there: a break.
        QVERIFY(std::abs(l.lines[1].end - 11.5) < 1e-6);
        QCOMPARE(l.lines[2].text, QString("Chorus"));

        const Lyrics enhanced = Lyrics::parseLrc("[00:01.00]<00:01.00>Young <00:01.40>Money<00:02.00>\n");
        QCOMPARE(int(enhanced.kind), int(Lyrics::Words));
        QCOMPARE(enhanced.lines[0].text, QString("Young Money"));
        QCOMPARE(int(enhanced.lines[0].words.size()), 2);
        QVERIFY(std::abs(enhanced.lines[0].words[1].start - 1.4) < 1e-6);

        const Lyrics pause = Lyrics::parseLrc("[00:01.00]Words here\n[00:03.00]()\n[00:09.00]More words\n");
        QCOMPARE(int(pause.lines.size()), 2);
        QVERIFY(std::abs(pause.lines[0].end - 3.0) < 1e-6);

        const Lyrics plain = Lyrics::parseLrc("Line one\n\n\nLine two\n");
        QCOMPARE(int(plain.kind), int(Lyrics::Plain));
        QCOMPARE(int(plain.lines.size()), 3); // a single blank kept between verses
    }

    void lyricsfileGivesWordTiming() {
        const QString yaml = "version: '1.0'\nmetadata:\n  title: A Milli\n  instrumental: false\nlines:\n"
                             "- text: Young Money!\n  start_ms: 6320\n  end_ms: 12780\n  words:\n"
                             "  - text: 'Young '\n    start_ms: 6320\n  - text: Money!\n    start_ms: 6620\n"
                             "- text: 'Mack, I''m goin'' in'\n  start_ms: 16870\n  end_ms: 19680\n  words:\n"
                             "  - text: 'Mack, '\n    start_ms: 16870\n  - text: \"I'm goin' in\"\n    start_ms: 17250\n";
        const Lyrics l = Lyrics::parseLyricsfile(yaml);
        QCOMPARE(int(l.kind), int(Lyrics::Words));
        QCOMPARE(int(l.lines.size()), 2);
        QCOMPARE(l.lines[1].text, QString("Mack, I'm goin' in"));
        QVERIFY(std::abs(l.lines[0].end - 12.78) < 1e-6);
        QCOMPARE(l.lines[0].words[0].text, QString("Young "));
        QVERIFY(std::abs(l.lines[1].words[1].start - 17.25) < 1e-6);
        QCOMPARE(l.lines[1].words[1].text, QString("I'm goin' in"));

        const Lyrics instrumental = Lyrics::parseLyricsfile("version: '1.0'\nmetadata:\n  instrumental: true\nlines: []\n");
        QCOMPARE(int(instrumental.kind), int(Lyrics::Instrumental));
    }

    void lyricsJunkIsRejected() {
        QVERIFY(Lyrics::isJunk("MixFiend.com / MixtapeTorrent.com"));
        QVERIFY(Lyrics::isJunk("Downloaded from www.example.net"));
        QVERIFY(Lyrics::isJunk("   "));
        QVERIFY(!Lyrics::isJunk("A millionaire, I'm a Young Money millionaire\nTougher than Nigerian hair\n"
                                "My criteria compared to your career just isn't fair\nI'm a venereal disease"));
    }

    void lyricsQueriesAreTidied() {
        QCOMPARE(LyricsQuery::cleanTitle("Hustle-MF"), QString("Hustle"));
        QCOMPARE(LyricsQuery::cleanTitle("5. Damn Damn"), QString("Damn Damn"));
        QCOMPARE(LyricsQuery::cleanTitle("Young Mulah Baby (Feat. Gorilla Zoe)"), QString("Young Mulah Baby"));
        QCOMPARE(LyricsQuery::cleanTitle("3 Way [Prod. By Tm-88]"), QString("3 Way"));
        QCOMPARE(LyricsQuery::cleanTitle("Dope Boy Swag ft. Lil Wayne"), QString("Dope Boy Swag"));
        QCOMPARE(LyricsQuery::cleanTitle("99 Problems"), QString("99 Problems"));
        QCOMPARE(LyricsQuery::mainArtist("Young Jeezy & T.I."), QString("Young Jeezy"));
        QCOMPARE(LyricsQuery::mainArtist("Ace Hood, Rick Ross, Lil Wayne"), QString("Ace Hood"));
        QCOMPARE(LyricsQuery::mainArtist("*"), QString());
        QVERIFY(!LyricsQuery::searchable("lil wayne bad guy 4 - Track 28"));
        QVERIFY(!LyricsQuery::searchable("Track 10"));
        QVERIFY(!LyricsQuery::searchable("Untitled"));
        QVERIFY(LyricsQuery::searchable("Back On Track"));
        QVERIFY(LyricsQuery::searchable("Green And Yellow"));
    }

    void lyricsSearchPicksTheRightSong() {
        LyricsQuery q{"Get Em Jeezy (Feat. X)", "Young Jeezy", "", 205};
        auto record = [](QString artist, QString title, double length, bool synced, bool words = false) {
            return QJsonObject{{"artistName", artist}, {"trackName", title}, {"duration", length},
                               {"syncedLyrics", synced ? "[00:01.00]x" : ""}, {"plainLyrics", "x"},
                               {"hasWordSync", words}, {"instrumental", false}};
        };
        QList<QJsonObject> results{record("Someone Else", "Get 'Em Jeezy", 205, true),
                                   record("Young Jeezy", "Get 'Em Jeezy", 260, true),
                                   record("Young Jeezy", "Get 'Em Jeezy", 206, false),
                                   record("Young Jeezy", "Get 'Em Jeezy", 207, true)};
        // Wrong artist and a far-off length are out; synced beats plain.
        QCOMPARE(pickSearchResult(results, q), 3);
        results << record("Young Jeezy", "Get Em Jeezy", 205, true, true);
        QCOMPARE(pickSearchResult(results, q), 4);
        QCOMPARE(pickSearchResult({record("Young Jeezy", "Trap Or Die", 205, true)}, q), -1);
    }

    void lyricsFollowLinesAndWords() {
        Lyrics l = Lyrics::parseLrc("[00:10.00]<00:10.00>Ab <00:11.00>cd\n[00:20.00]<00:20.00>Next <00:21.00>one\n");
        l.close(40);
        QCOMPARE(l.lineAt(5), -1);
        QCOMPARE(l.lineAt(10.5), 0);
        QCOMPARE(l.lineAt(25), 1);

        QTemporaryDir cache, state;
        LyricsService service(cache.path(), state.path());
        service.setEndpoint("http://127.0.0.1:9"); // never reached: offline
        QTemporaryDir music;
        const QString song = music.filePath("song.mp3");
        QFile(song).open(QIODevice::WriteOnly);
        QFile lrc(music.filePath("song.lrc"));
        QVERIFY(lrc.open(QIODevice::WriteOnly));
        lrc.write("[00:10.00]<00:10.00>Ab <00:11.00>cd\n[00:20.00]<00:20.00>Next <00:21.00>one\n[00:21.50]\n[00:40.00]Last line\n");
        lrc.close();
        service.load(song, "Song", "Artist", "", 60);
        QCOMPARE(service.status(), int(LyricsService::Found));
        QCOMPARE(service.source(), QString("file"));
        QCOMPARE(service.kind(), int(Lyrics::Words));

        service.follow(10.5); // half of "Ab " (3 of 5 characters sung: 1.5 rounded)
        QCOMPARE(service.current(), 0);
        QVERIFY(service.progress() > 0.2 && service.progress() < 0.6);
        service.follow(19);
        QVERIFY(service.progress() > 0.99);
        // Before the first line, the wait counts down.
        service.follow(5);
        QCOMPARE(service.current(), -1);
        QVERIFY(service.gap() > 0.4 && service.gap() < 0.6);
        // After "one" ends at 21.5 there's a long break before "Last line".
        service.follow(30);
        QCOMPARE(service.current(), 1);
        QVERIFY(service.gap() > 0.4 && service.gap() < 0.6);

        // A nudge shows the lyrics earlier, and is kept for this song.
        service.nudge(1.0);
        service.follow(9.2);
        QCOMPARE(service.current(), 0);
        LyricsService again(cache.path(), state.path());
        again.load(song, "Song", "Artist", "", 60);
        QVERIFY(std::abs(again.offset() - 1.0) < 1e-6);
    }

    void lyricsComeFromLrclibAndAreCached() {
        FakeLrclib lrclib;
        // /api/get misses (404), search finds it with word timing.
        const QByteArray yaml = "version: '1.0'\nlines:\n- text: Hello there\n  start_ms: 1000\n  end_ms: 3000\n  words:\n"
                                "  - text: 'Hello '\n    start_ms: 1000\n  - text: there\n    start_ms: 1500\n";
        QJsonObject hit{{"artistName", "Lil Wayne"}, {"trackName", "Hello"}, {"duration", 120}, {"instrumental", false},
                        {"plainLyrics", "Hello there"}, {"syncedLyrics", "[00:01.00]Hello there"},
                        {"hasWordSync", true}, {"lyricsfile", QString::fromUtf8(yaml)}};
        lrclib.replies["/api/search"] = {200, QJsonDocument(QJsonArray{hit}).toJson(), {}};

        QTemporaryDir cache, state, music;
        const QString song = music.filePath("hello.mp3");
        QFile(song).open(QIODevice::WriteOnly);
        {
            LyricsService service(cache.path(), state.path());
            service.setEndpoint(lrclib.url());
            // Off by default: nothing is sent.
            service.load(song, "Hello (Feat. Drake)", "Lil Wayne", "Tape", 121);
            QCOMPARE(service.status(), int(LyricsService::Offline));
            QVERIFY(lrclib.requests.isEmpty());

            service.setOnline(true);
            QTRY_COMPARE(service.status(), int(LyricsService::Found));
            QCOMPARE(service.kind(), int(Lyrics::Words));
            QCOMPARE(service.source(), QString("lrclib"));
            QCOMPARE(lrclib.requests.size(), 2);
            QVERIFY(lrclib.requests[0].startsWith("/api/get?"));
            QVERIFY(lrclib.requests[0].contains("track_name=Hello&"));
            QVERIFY(lrclib.requests[0].contains("duration=121"));
            QVERIFY(lrclib.requests[1].startsWith("/api/search?"));
            QVERIFY(lrclib.agents.value(0).startsWith("Halo/"));
        }
        // The next time, the cache answers and LRCLIB isn't asked.
        {
            LyricsService service(cache.path(), state.path());
            service.setEndpoint(lrclib.url());
            QVERIFY(service.online()); // the choice is remembered
            service.load(song, "Hello (Feat. Drake)", "Lil Wayne", "Tape", 121);
            QCOMPARE(service.status(), int(LyricsService::Found));
            QCOMPARE(lrclib.requests.size(), 2);
        }
        // A miss is cached too.
        const QString other = music.filePath("freestyle.mp3");
        QFile(other).open(QIODevice::WriteOnly);
        {
            LyricsService service(cache.path(), state.path());
            service.setEndpoint(lrclib.url());
            service.load(other, "Freestyle", "Nobody", "", 90);
            QTRY_COMPARE(service.status(), int(LyricsService::NotFound));
            const int asked = lrclib.requests.size();
            service.load(song, "Hello", "Lil Wayne", "Tape", 121);
            service.load(other, "Freestyle", "Nobody", "", 90);
            QCOMPARE(service.status(), int(LyricsService::NotFound));
            QCOMPARE(lrclib.requests.size(), asked);
        }
    }

    void lyricsWithoutArtistSearchByTitle() {
        FakeLrclib lrclib;
        lrclib.replies["/api/search"] = {200, "[]", {}};
        QTemporaryDir cache, state, music;
        const QString song = music.filePath("x.mp3");
        QFile(song).open(QIODevice::WriteOnly);
        LyricsService service(cache.path(), state.path());
        service.setEndpoint(lrclib.url());
        service.setOnline(true);
        service.load(song, "Green And Yellow Lil Wayne", "", "", 200);
        QTRY_COMPARE(service.status(), int(LyricsService::NotFound));
        QCOMPARE(lrclib.requests.size(), 1);
        QVERIFY(lrclib.requests[0].startsWith("/api/search?"));
        // A generic title costs no request at all.
        const QString other = music.filePath("y.mp3");
        QFile(other).open(QIODevice::WriteOnly);
        service.load(other, "Track 12", "Lil Wayne", "", 200);
        QCOMPARE(service.status(), int(LyricsService::NotFound));
        QCOMPARE(lrclib.requests.size(), 1);
    }

    void lyricsHonourRetryAfter() {
        FakeLrclib lrclib;
        lrclib.replies["/api/get"] = {429, R"({"name":"RateLimitExceeded"})", "Retry-After: 60\r\n"};
        QTemporaryDir cache, state, music;
        const QString song = music.filePath("a.mp3");
        QFile(song).open(QIODevice::WriteOnly);
        LyricsService service(cache.path(), state.path());
        service.setEndpoint(lrclib.url());
        service.setOnline(true);
        service.load(song, "A", "B", "", 100);
        QTRY_COMPARE(lrclib.requests.size(), 1);
        QTest::qWait(300);
        // Still waiting, not hammering the server, not caching a miss.
        QCOMPARE(service.status(), int(LyricsService::Searching));
        const QString other = music.filePath("b.mp3");
        QFile(other).open(QIODevice::WriteOnly);
        service.load(other, "C", "D", "", 100);
        QTest::qWait(300);
        QCOMPARE(lrclib.requests.size(), 1);
    }

    // ---- Safety (marketplace review pass, 2026-11-19) ----

    void privateFilesAndFoldersOnly() {
        QTemporaryDir root, outside;
        const QString dir = root.filePath("halo");
        QVERIFY(QDir().mkpath(dir + "/covers"));
        chmod(QFile::encodeName(dir).constData(), 0755);
        chmod(QFile::encodeName(dir + "/covers").constData(), 0755);
        const QString state = dir + "/stats.json", cover = dir + "/covers/a.jpg";
        const QString victim = outside.filePath("not-halos.txt");
        for (const QString &f : {state, cover, victim}) {
            QFile file(f);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
            file.close();
            chmod(QFile::encodeName(f).constData(), 0644);
        }
        QVERIFY(QFile::link(victim, dir + "/link.json"));
        const mode_t before = umask(022);
        Safety::makePrivate({dir, root.filePath("missing")});
        const auto mode = [](const QString &f) {
            struct stat st {};
            lstat(QFile::encodeName(f).constData(), &st);
            return int(st.st_mode & 0777);
        };
        QCOMPARE(mode(dir), 0700);
        QCOMPARE(mode(dir + "/covers"), 0700);
        QCOMPARE(mode(state), 0600);
        QCOMPARE(mode(cover), 0600);
        QCOMPARE(mode(victim), 0644);   // a link's target is never touched
        // New files are private from now on.
        QSaveFile fresh(dir + "/state.json");
        QVERIFY(fresh.open(QIODevice::WriteOnly));
        fresh.write("{}");
        QVERIFY(fresh.commit());
        QCOMPARE(mode(dir + "/state.json"), 0600);
        umask(before);
    }

    void systemToolsComeFromSystemFolders() {
        if (!QFileInfo::exists("/usr/bin/ffmpeg"))
            QSKIP("no /usr/bin/ffmpeg");
        QTemporaryDir bin;
        QFile fake(bin.filePath("ffmpeg"));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\nexit 0\n");
        fake.close();
        fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", QFile::encodeName(bin.path()) + ":" + path);
        QCOMPARE(QStandardPaths::findExecutable("ffmpeg"), fake.fileName());   // PATH would pick the fake
        QCOMPARE(Safety::systemTool("ffmpeg"), QString("/usr/bin/ffmpeg"));
        qputenv("PATH", path);
    }

    void lyricsEndpointMustBeHttps() {
        QVERIFY(Safety::allowedEndpoint("https://lrclib.net"));
        QVERIFY(Safety::allowedEndpoint("http://127.0.0.1:9"));
        QVERIFY(Safety::allowedEndpoint("http://localhost:8080"));
        QVERIFY(Safety::allowedEndpoint("http://[::1]:8080"));
        QVERIFY(!Safety::allowedEndpoint("http://lrclib.net"));
        QVERIFY(!Safety::allowedEndpoint("http://127.0.0.1.example.com"));
        QVERIFY(!Safety::allowedEndpoint("ftp://lrclib.net"));
        QVERIFY(!Safety::allowedEndpoint("https://user:pw@lrclib.net"));
        QVERIFY(!Safety::allowedEndpoint("not a url"));

        QTemporaryDir cache, state, music;
        const QString song = music.filePath("a.mp3");
        QFile(song).open(QIODevice::WriteOnly);
        LyricsService service(cache.path(), state.path());
        service.setEndpoint("http://example.invalid");
        service.setOnline(true);
        service.load(song, "Title", "Artist", "", 100);
        QTRY_COMPARE(service.status(), int(LyricsService::Failed));
        QVERIFY(QDir(cache.path()).entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty());
    }

    void lyricsOversizeReplyIsCutOff() {
        FakeLrclib lrclib;
        QByteArray huge(int(Safety::MaxLyricsReply) + 512 * 1024, 'x');
        lrclib.replies["/api/get"] = {200, "{\"plainLyrics\":\"" + huge + "\"}", {}};
        QTemporaryDir cache, state, music;
        const QString song = music.filePath("a.mp3");
        QFile(song).open(QIODevice::WriteOnly);
        LyricsService service(cache.path(), state.path());
        service.setEndpoint(lrclib.url());
        service.setOnline(true);
        service.load(song, "Title", "Artist", "", 100);
        QTRY_COMPARE_WITH_TIMEOUT(service.status(), int(LyricsService::Failed), 10000);
        QCOMPARE(lrclib.requests.size(), 1);   // failed, not retried as a search
    }

    void socketCutsOffOversizeCommands() {
        QTemporaryDir run, music, cache, state;
        const QByteArray old = qgetenv("HALO_SOCKET");
        qputenv("HALO_SOCKET", QFile::encodeName(run.filePath("halo.sock")));
        Library library({music.path()}, cache.path(), state.path());
        Analyzer analyzer(cache.filePath("analysis.json"));
        Player player(&library, &analyzer, state.path());
        Remote remote(&player);
        QVERIFY(remote.listen());
        QLocalSocket flood;
        flood.connectToServer(Remote::socketName());
        QVERIFY(flood.waitForConnected(2000));
        flood.write(QByteArray(int(Safety::MaxSocketLine) * 2, 'a'));   // no newline, ever
        flood.flush();
        QTRY_COMPARE_WITH_TIMEOUT(flood.state(), QLocalSocket::UnconnectedState, 5000);
        // An ordinary command still works.
        QLocalSocket client;
        client.connectToServer(Remote::socketName());
        QVERIFY(client.waitForConnected(2000));
        client.write("{\"cmd\":\"ping\"}\n");
        client.flush();
        QTRY_VERIFY(client.canReadLine());
        QVERIFY(QJsonDocument::fromJson(client.readLine()).object().value("ok").toBool());
        if (old.isEmpty())
            qunsetenv("HALO_SOCKET");
        else
            qputenv("HALO_SOCKET", old);
    }

    void ffmpegNeverFetchesFromANetwork() {
        if (Safety::systemTool("ffmpeg").isEmpty())
            QSKIP("no ffmpeg");
        FakeLrclib server;   // any HTTP server on 127.0.0.1 will do
        QTemporaryDir music;
        // A "song" that is really an HLS playlist pointing at the network.
        const QString evil = music.filePath("evil.mp3");
        QFile f(evil);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:10,\n" + server.url().toUtf8() +
                "/segment.ts\n#EXT-X-ENDLIST\n");
        f.close();
        QString error;
        Analysis::analyzeFile(evil, &error);
        Stream stream(evil, 0, QString());
        stream.start();
        QTest::qWait(1500);
        QVERIFY2(server.requests.isEmpty(), qPrintable(server.requests.join(", ")));
        QVERIFY(Analysis::ffmpegArguments(evil).contains("-protocol_whitelist"));
    }
};

QTEST_MAIN(HaloTests)
#include "halo_tests.moc"
