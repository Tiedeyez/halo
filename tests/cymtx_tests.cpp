#include <QDir>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSaveFile>
#include <QRegularExpression>
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
#include "playlists.h"
#include "remote.h"
#include "sandplate.h"
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


// A stand-in player for the decks' controls: the properties they read and
// set, and the two calls they make.
class DeckPlayerStub : public QObject {
    Q_OBJECT
    Q_PROPERTY(double volume MEMBER volume NOTIFY changed)
    Q_PROPERTY(bool eqEnabled MEMBER eqEnabled NOTIFY changed)
    Q_PROPERTY(double eqPreamp READ preamp WRITE setPreamp NOTIFY changed)
    Q_PROPERTY(bool playing MEMBER playing NOTIFY changed)
    Q_PROPERTY(bool hasTrack MEMBER hasTrack NOTIFY changed)
    Q_PROPERTY(double position MEMBER position NOTIFY changed)
    Q_PROPERTY(double duration MEMBER duration NOTIFY changed)
public:
    double volume = 0.5, eqPreampValue = 0, position = 0, duration = 200, seekedTo = -1;
    bool eqEnabled = false, playing = false, hasTrack = true;
    double preamp() const { return eqPreampValue; }
    void setPreamp(double db) { eqPreampValue = std::clamp(db, -12.0, 12.0); emit changed(); }
public slots:
    void togglePlay() { playing = !playing; emit changed(); }
    void seek(double seconds) { seekedTo = seconds; position = seconds; emit changed(); }
signals:
    void changed();
};

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

// A misbehaving listener on CymtX's socket, on its own thread so a blocking
// client in the test thread can talk to it: it reads the request line, then
// either drips bytes without ever ending the line, or sends far too much.
struct RogueSocket {
    enum Mode { Trickle, Flood };
    QString path;
    int listener = -1;
    std::atomic<bool> stop{false};
    std::thread worker;

    RogueSocket(const QString &socketPath, Mode mode) : path(socketPath) {
        listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        const QByteArray name = QFile::encodeName(path);
        std::memcpy(address.sun_path, name.constData(), std::min<size_t>(name.size(), sizeof(address.sun_path) - 1));
        ::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address));
        ::listen(listener, 1);
        worker = std::thread([this, mode] {
            const int client = ::accept(listener, nullptr, nullptr);
            if (client < 0)
                return;
            char c = 0;
            while (::read(client, &c, 1) == 1 && c != '\n') {}
            const QByteArray chunk(64 * 1024, 'x');
            if (mode == Flood) {
                qint64 sent = 0;
                while (!stop && sent <= Safety::MaxSocketReply + chunk.size()) {
                    const ssize_t n = ::send(client, chunk.constData(), chunk.size(), MSG_NOSIGNAL);
                    if (n <= 0)
                        break;
                    sent += n;
                }
            } else {
                while (!stop && ::send(client, "x", 1, MSG_NOSIGNAL) == 1)
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            ::close(client);
        });
    }
    ~RogueSocket() {
        stop = true;
        ::shutdown(listener, SHUT_RDWR);
        ::close(listener);
        worker.join();
        QFile::remove(path);
    }
};

class CymtXTests : public QObject {
    Q_OBJECT

private slots:
    // CymtX finds Omarchy's language choice where omarchy-language puts it.
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
            qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Style.qml")), "CymtX", 1, 0, "Style");
            qmlRegisterType(QUrl::fromLocalFile(QFINDTESTDATA("../src/SmoothWheel.qml")), "CymtX", 1, 0, "SmoothWheel");
            registered = true;
        }
        QQmlComponent component(view.engine());
        component.setData(R"(
            import QtQuick
            import CymtX
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

    // The record player's controls work by hand: the fader sets the volume,
    // the dial the EQ's gain (and turns the EQ on), start/stop plays and
    // pauses, and dragging the tonearm's headshell over the record seeks.
    void deckControlsWorkByHand() {
        QFile manifestFile(QFINDTESTDATA("../src/turntable/manifest.json"));
        QVERIFY(manifestFile.open(QIODevice::ReadOnly));
        const QVariantMap manifest = QJsonDocument::fromJson(manifestFile.readAll()).object().toVariantMap();
        QFile energyFile(QFINDTESTDATA("../src/hologram/manifest.json"));
        QVERIFY(energyFile.open(QIODevice::ReadOnly));
        const QVariantMap energy = QJsonDocument::fromJson(energyFile.readAll()).object().toVariantMap();
        QVERIFY(manifest.contains("fader") && manifest.contains("dial") && manifest.contains("start"));

        DeckPlayerStub player;
        QQmlPropertyMap themeStub, translatorStub;
        themeStub.insert("accent", QColor("#7aa2f7"));
        themeStub.insert("accentForeground", QColor("#000000"));
        themeStub.insert("palette", QVariantList{});
        themeStub.insert("radius", 4);
        translatorStub.insert("language", "en");
        QQuickView view;
        view.rootContext()->setContextProperty("player", &player);
        view.rootContext()->setContextProperty("theme", &themeStub);
        view.rootContext()->setContextProperty("translator", &translatorStub);
        view.rootContext()->setContextProperty("deckManifest", manifest);
        view.rootContext()->setContextProperty("deckGeometry", energy);
        qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Style.qml")), "CymtX", 1, 0, "Style");
        qmlRegisterType(QUrl::fromLocalFile(QFINDTESTDATA("../src/DeckControls.qml")), "CymtX", 1, 0, "DeckControls");
        QQmlComponent component(view.engine());
        // A deck as the visuals are: its manifest, its scale and the arm's
        // geometry (heading() as in TurntableVisual.qml).
        component.setData(R"(
            import QtQuick
            import CymtX
            Item {
                id: deck
                width: 900; height: 900
                readonly property var manifest: deckManifest
                readonly property real px: width / manifest.image
                function heading(progress) {
                    const m = manifest
                    const r = m.grooveOut + (m.grooveIn - m.grooveOut) * Math.max(0, Math.min(1, progress))
                    const dx = m.centre[0] - m.pivot[0], dy = m.centre[1] - m.pivot[1]
                    const d = Math.hypot(dx, dy)
                    const cosA = (m.armLength * m.armLength + d * d - r * r) / (2 * m.armLength * d)
                    return Math.atan2(dy, dx) + Math.acos(Math.max(-1, Math.min(1, cosA)))
                }
                readonly property alias controls: controls
                DeckControls { id: controls; deck: deck; geometry: deckGeometry }
                readonly property var f: manifest.fader
                readonly property point faderHigh: Qt.point((f.x + f.w / 2) * px, f.high * px)
                readonly property point faderLow: Qt.point((f.x + f.w / 2) * px, f.low * px)
                readonly property point dial: Qt.point(manifest.dial.cx * px, manifest.dial.cy * px)
                readonly property point start: Qt.point(manifest.start.x * width, manifest.start.y * height)
                readonly property point stylusAtStart: controls.stylusAt(0)
                readonly property point stylusHalfway: controls.stylusAt(0.5)
            }
        )", QUrl::fromLocalFile(QFINDTESTDATA("../src/DeckControls.qml")));
        QScopedPointer<QQuickItem> root(qobject_cast<QQuickItem *>(component.create(view.rootContext())));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setParentItem(view.contentItem());
        view.resize(900, 900);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        auto at = [&](const char *name) { return root->property(name).toPointF().toPoint(); };

        // The fader: the far end is full, the near end silent.
        QTest::mousePress(&view, Qt::LeftButton, {}, at("faderHigh"));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, at("faderHigh"));
        QVERIFY2(player.volume > 0.97, qPrintable(QString::number(player.volume)));
        QTest::mousePress(&view, Qt::LeftButton, {}, at("faderHigh"));
        QTest::mouseMove(&view, at("faderLow"));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, at("faderLow"));
        QVERIFY2(player.volume < 0.03, qPrintable(QString::number(player.volume)));

        // The dial: dragged up 75 px is +12 dB (24 dB over 150 px), and the
        // EQ comes on; a double click sets it back to 0.
        const QPoint dial = at("dial");
        QTest::mousePress(&view, Qt::LeftButton, {}, dial);
        for (int dy = 15; dy <= 75; dy += 15)
            QTest::mouseMove(&view, dial - QPoint(0, dy));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, dial - QPoint(0, 75));
        QVERIFY(player.eqEnabled);
        QCOMPARE(player.preamp(), 12.0);
        QTest::mouseDClick(&view, Qt::LeftButton, {}, dial);
        QCOMPARE(player.preamp(), 0.0);

        // Start/stop.
        QVERIFY(!player.playing);
        QTest::mouseClick(&view, Qt::LeftButton, {}, at("start"));
        QVERIFY(player.playing);
        QTest::mouseClick(&view, Qt::LeftButton, {}, at("start"));
        QVERIFY(!player.playing);

        // The tonearm: from the first groove, its headshell dragged to where
        // the stylus is halfway through the song, seeks to the middle; a
        // press away from the headshell does nothing.
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(450, 450));
        QCOMPARE(player.seekedTo, -1.0);
        const QPoint from = at("stylusAtStart"), to = at("stylusHalfway");
        QTest::mousePress(&view, Qt::LeftButton, {}, from);
        for (int k = 1; k <= 8; ++k)
            QTest::mouseMove(&view, from + (to - from) * k / 8);
        QTest::mouseRelease(&view, Qt::LeftButton, {}, to);
        QVERIFY2(std::abs(player.seekedTo - 100) < 4, qPrintable(QString::number(player.seekedTo)));
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
        qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Style.qml")), "CymtX", 1, 0, "Style");
        qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Secrets.qml")), "CymtX", 1, 0, "Secrets");
        qmlRegisterType(QUrl::fromLocalFile(QFINDTESTDATA("../src/SiegeGame.qml")), "CymtX", 1, 0, "SiegeGame");
        qmlRegisterUncreatableType<LyricsService>("CymtX", 1, 0, "Lyrics", "from main");
        QQmlComponent component(view.engine());
        component.setData(R"(
            import QtQuick
            import CymtX
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
        QObject *secrets = view.engine()->singletonInstance<QObject *>("CymtX", "Secrets");
        QVERIFY(secrets);
        QCOMPARE(secrets->property("siege").toBool(), false);
        QVERIFY(secrets->property("best").toInt() >= game->property("score").toInt());
    }

    void plateModesMatchThePublishedTable() {
        using namespace Plate;
        // A free circular plate, Poisson's ratio 0.33: eigenvalues squared
        // from Leissa, Vibration of Plates (NASA SP-160), table 2.5, by
        // nodal diameters n and nodal circles s.
        const struct { int n, s; double lambda2; } table[] = {
            {0, 1, 9.084}, {0, 2, 38.55}, {0, 3, 87.80}, {1, 1, 20.52}, {1, 2, 59.86},
            {2, 0, 5.253}, {2, 1, 35.25}, {2, 2, 84.38}, {3, 0, 12.23}, {3, 1, 52.91},
        };
        for (const auto &row : table) {
            // n = 0 and 1 have no vibration without a nodal circle, so their
            // first eigenvalue is s = 1.
            const int index = row.n < 2 ? row.s - 1 : row.s;
            const auto roots = eigenvalues(row.n, index + 1);
            QVERIFY(int(roots.size()) == index + 1);
            const double lambda2 = roots[size_t(index)] * roots[size_t(index)];
            QVERIFY2(std::fabs(lambda2 - row.lambda2) / row.lambda2 < 0.005,
                     qPrintable(QString("n=%1 s=%2: %3 vs %4").arg(row.n).arg(row.s).arg(lambda2).arg(row.lambda2)));
        }
        // The shapes: largest value 1, a mode with nodal diameters still at
        // the centre, a ring mode moving there; and the lowest ring mode's
        // one nodal circle at 0.68 of the radius.
        const Mode ring = mode(0, 0, eigenvalues(0, 1).front());
        const Mode petals = mode(6, 0, eigenvalues(6, 1).front());
        float largest = 0;
        for (float w : petals.shape)
            largest = std::max(largest, std::fabs(w));
        QVERIFY(std::fabs(largest - 1) < 1e-4);
        QVERIFY(std::fabs(petals.shape.front()) < 1e-6);
        QVERIFY(std::fabs(ring.shape.front()) > 0.5);
        double node = -1;
        for (int i = 1; i < Samples; ++i)
            if ((ring.shape[size_t(i - 1)] < 0) != (ring.shape[size_t(i)] < 0))
                node = double(i) / (Samples - 1);
        QVERIFY2(std::fabs(node - 0.68) < 0.01, qPrintable(QString::number(node)));
    }

    void sandGathersOnTheNodalLines() {
        using namespace Plate;
        Sand sand;
        sand.setFold(6);
        sand.setGrains(4000);
        // One tone, at the pitch of the plate's lowest ring mode: every
        // other mode stays quiet, and the sand should walk to that mode's
        // nodal circle at 0.68 of the radius.
        std::vector<float> bands(size_t(Spectrum::Bands), 0.05f);
        const double at = std::log(LowestRingHertz / 40.0) / std::log(16000.0 / 40.0) * (Spectrum::Bands - 1);
        bands[size_t(at)] = bands[size_t(at) + 1] = 1.0f;
        // How far the grains are from that circle, on average.
        const auto spread = [&sand] {
            double sum = 0;
            for (const Grain &g : sand.grains())
                sum += std::fabs(std::hypot(g.x, g.y) - 0.68);
            return sum / double(sand.grains().size());
        };
        const double strewn = spread();
        QVERIFY2(strewn > 0.18, qPrintable(QString::number(strewn)));
        for (int f = 0; f < 600; ++f)
            sand.step(1.0 / 60, bands, 0, 0.6);
        // Gathered on the circle, in a band and not on a hairline: sand
        // takes up room.
        const double gathered = spread();
        QVERIFY2(gathered < 0.07 && gathered > 0.005, qPrintable(QString("%1 -> %2").arg(strewn).arg(gathered)));
        // Every grain is still on the plate.
        for (const Grain &g : sand.grains())
            QVERIFY(std::hypot(g.x, g.y) <= 1.0);
        // Silence: nothing moves.
        const auto before = sand.grains();
        std::vector<float> silence(size_t(Spectrum::Bands), 0.0f);
        for (int f = 0; f < 60; ++f)
            sand.step(1.0 / 60, silence, 0, 0);
        for (size_t i = 0; i < before.size(); ++i)
            QVERIFY(before[i].x == sand.grains()[i].x && before[i].y == sand.grains()[i].y);
        // A beat throws the sand: grains that lay still are in the air.
        const auto airborne = [&sand] {
            double sum = 0;
            for (const Grain &g : sand.grains())
                sum += g.heat;
            return sum / double(sand.grains().size());
        };
        for (int f = 0; f < 240; ++f)
            sand.step(1.0 / 60, bands, 0, 0.6);
        const double lying = airborne();
        for (int f = 0; f < 4; ++f)
            sand.step(1.0 / 60, bands, 1.0, 0.6);
        QVERIFY(sand.kick() > 0.3);
        QVERIFY2(airborne() > lying * 1.5 + 0.05, qPrintable(QString("%1 -> %2").arg(lying).arg(airborne())));
        // A knock moves the sand under it.
        const auto resting = sand.grains();
        sand.tap(0.68, 0, 1);
        int moved = 0;
        for (size_t i = 0; i < resting.size(); ++i)
            moved += resting[i].x != sand.grains()[i].x;
        QVERIFY(moved > 20);
    }

    void sandBench() {
        if (qEnvironmentVariableIsEmpty("CYMTX_BENCH"))
            QSKIP("set CYMTX_BENCH=1 to time the sand");
        std::vector<float> bands(size_t(Spectrum::Bands), 0.5f);
        for (size_t i = 0; i < bands.size(); ++i)
            bands[i] = 0.4f + 0.5f * float((i * 37) % 11) / 11;
        Plate::Sand sand;
        sand.setFold(12);
        sand.setGrains(9000);
        QElapsedTimer timer;
        timer.start();
        for (int f = 0; f < 200; ++f)
            sand.step(1.0 / 60, bands, f % 30 == 0, 0.6);
        qInfo("sand.step: %.2f ms", timer.nsecsElapsed() / 200e6);
        SandPlate plate;
        plate.setWidth(632);
        plate.setHeight(345);
        plate.setCentre(QPointF(196, 172));
        plate.setRadius(162);
        plate.setGrains(9000);
        plate.setVivid(true);
        plate.setSong("/m/b.mp3", "B");
        timer.restart();
        for (int f = 0; f < 100; ++f)
            plate.step(1.0 / 60, 0, 0.6);
        qInfo("plate.step vivid (fold %d): %.2f ms", plate.fold(), timer.nsecsElapsed() / 100e6);
        plate.setVivid(false);
        plate.setCentre(QPointF(316, 172));
        plate.setRadius(374);
        plate.setGrains(6000);
        timer.restart();
        for (int f = 0; f < 100; ++f)
            plate.step(1.0 / 60, 0, 0.6);
        qInfo("plate.step background: %.2f ms", timer.nsecsElapsed() / 100e6);
    }

    void sandPlateDrawsAMandala() {
        // The fold comes from the song, the same every time, 5 to 12.
        QCOMPARE(SandPlate::foldFor("/m/a.mp3", "A"), SandPlate::foldFor("/m/a.mp3", "A"));
        QSet<int> folds;
        for (int i = 0; i < 200; ++i) {
            const int fold = SandPlate::foldFor(QString("/m/%1.mp3").arg(i), "Song");
            QVERIFY(fold >= 5 && fold <= 12);
            folds.insert(fold);
        }
        QVERIFY(folds.size() >= 6);
        // Drawn with every image of every grain, so the picture mirrors
        // left to right about the plate's centre.
        SandPlate plate;
        plate.setWidth(80);
        plate.setHeight(80);
        plate.setCentre(QPointF(40, 40));
        plate.setRadius(36);
        plate.setGrains(3000);
        plate.setVivid(true);
        plate.setSong("/m/a.mp3", "A");
        plate.step(1.0 / 60, 0, 0.5);
        const QImage &f = plate.frame();
        QCOMPARE(f.size(), QSize(80, 80));
        int lit = 0, same = 0, sandCells = 0;
        for (int y = 0; y < 80; ++y)
            for (int x = 0; x < 40; ++x) {
                // (x, y) are swapped: the mirror line that every fold has is
                // the horizontal one through the centre.
                const bool left = qAlpha(f.pixel(y, x)) == 255, right = qAlpha(f.pixel(y, 79 - x)) == 255;
                lit += left || right;
                same += left && right;
                sandCells += left;
            }
        QVERIFY(sandCells > 200);
        QVERIFY2(same > lit * 0.9, qPrintable(QString("%1 of %2").arg(same).arg(lit)));
        // Outside the plate nothing is drawn.
        QCOMPARE(qAlpha(f.pixel(1, 1)), 0);
        // The background never draws brighter than its ceiling.
        plate.setVivid(false);
        plate.setStrength(0.1);
        plate.step(1.0 / 60, 0, 0.5);
        for (int y = 0; y < 80; ++y)
            for (int x = 0; x < 80; ++x)
                QVERIFY(qAlpha(plate.frame().pixel(x, y)) <= 27);
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
        // Missing album comes from the album the folder's other songs name.
        QCOMPARE(find(Kind::MissingAlbum, "/m/HCSM5-2015/03_Pull_Up.mp3")->after, QString("HCSM5"));
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

    // What the Doctor proposes for the tapes people really have: folders
    // named like web addresses, scene file names, guests in the album artist.
    void doctorReadsMixtapeNames() {
        using namespace Doctor;
        QCOMPARE(Library::prettyFolderName("/m/lil-wayne-tear-drop-tune-part-3-2010"),
                 QString("Lil Wayne Tear Drop Tune Part 3 (2010)"));
        QCOMPARE(Library::prettyFolderName("/m/the-cartel-lil-wayne-drake-the-carter-meets-the-cartel-ii-2009"),
                 QString("The Cartel Lil Wayne Drake The Carter Meets The Cartel II (2009)"));
        QCOMPARE(Library::prettyFolderName("/m/lil-wayne-gone-til-november-2010_202511"),
                 QString("Lil Wayne Gone Til November (2010)"));
        QCOMPARE(Library::prettyFolderName("/m/djwhiteowl_lil_wayne_-_new_orlens_nightmare_pt_9"),
                 QString("Djwhiteowl Lil Wayne - New Orlens Nightmare Pt 9"));
        // Names with capitals or spaces are left as written.
        QCOMPARE(Library::prettyFolderName("/m/Young_Dolph_-_16_Zips-2015"), QString("Young Dolph - 16 Zips (2015)"));
        QCOMPARE(Library::prettyFolderName("/m/OK Computer"), QString("OK Computer"));
        QCOMPARE(Library::prettyFolderName("/m/Jay-Z"), QString("Jay - Z"));
        QCOMPARE(Library::capitalised("dj drama and 2pac"), QString("DJ Drama And 2pac"));
        QCOMPARE(Library::capitalised("eMOTIONAL"), QString("eMOTIONAL"));

        std::vector<File> files;
        auto add = [&](const QString &path, Tags t) { files.push_back({path, t}); };
        // a scene release with no artist tags at all
        add("/m/Dedication_2-2007-RAGEMP3/01-dj_drama_and_lil_wayne-the_best_in_the_business.mp3", {"The Best", "", "D2", "", "", 1, 100});
        add("/m/Dedication_2-2007-RAGEMP3/02-dj_drama_and_lil_wayne-getem.mp3", {"Getem", "", "D2", "", "", 2, 101});
        // title-then-artist names are not guessed at
        add("/m/Versus/02.HOW DEEZ HOES BE-GUCCI MANE.mp3", {"How Deez", "", "Versus", "", "", 2, 102});
        // a folder where nearly everyone is one artist
        add("/m/home/01 A.mp3", {"A", "Lil Wayne", "Home", "", "", 1, 110});
        add("/m/home/02 B.mp3", {"B", "Lil Wayne Feat. Rick Ross", "Home", "", "", 2, 111});
        add("/m/home/03 C.mp3", {"C", "lil wayne", "Home", "", "", 3, 112});
        add("/m/home/04 D.mp3", {"D", "", "Home", "", "", 4, 113});
        // a folder with no clear artist
        add("/m/mix/01 A.mp3", {"E", "One", "Mix", "", "", 1, 120});
        add("/m/mix/02 B.mp3", {"F", "Two", "Mix", "", "", 2, 121});
        add("/m/mix/03 C.mp3", {"G", "", "Mix", "", "", 3, 122});
        // folders named for an artist the library knows (three songs or more)
        add("/m/lil-wayne-homecoming-2011/01. If I Die Today (Feat. Rick Ross).mp3", {"If I Die Today", "", "Homecoming", "", "", 1, 170});
        add("/m/lil-wayne-homecoming-2011/14. Pandemonium (Feat. Wale) Rick Ross.mp3", {"Pandemonium", "", "Homecoming", "", "", 14, 171});
        add("/m/lil-wayne-homecoming-2011/16. Can A Drummer Get Some Lil Wayne.mp3", {"Drummer", "", "Homecoming", "", "", 16, 172});
        add("/m/lil-wayne-homecoming-2011/15 Erykah Badu-Jump Up (Feat Lil Wayne).mp3", {"Jump Up", "", "Homecoming", "", "", 15, 183});
        add("/m/lil-wayne-homecoming-2011/17. Faded (Feat. Red Cafe) Bun B.mp3", {"Faded", "", "Homecoming", "", "", 17, 184});
        add("/m/lil-wayne-homecoming-2011/Lil wayne-Peoples Champ.mp3", {"Peoples Champ", "", "Homecoming", "", "", 0, 185});
        add("/m/the-cartel-lil-wayne-rikers-2010/02 Uptown.mp3", {"Uptown", "", "Rikers", "", "", 2, 173});
        add("/m/ross/01 R.mp3", {"R1", "Rick Ross Feat. Bun B", "Ross", "", "", 1, 174});
        add("/m/ross/02 R.mp3", {"R2", "Rick Ross", "Ross", "", "", 2, 175});
        add("/m/ross/03 R.mp3", {"R3", "Rick Ross", "Ross", "", "", 3, 176});
        add("/m/Drake - So Far Gone/01 Lust.mp3", {"Lust", "", "So Far Gone", "", "", 1, 177});
        add("/m/drake-in-the-morning/01 Song.mp3", {"Song", "", "Morning", "", "", 1, 178});
        add("/m/drakes/01 D.mp3", {"D1", "Drake", "D", "", "", 1, 179});
        add("/m/drakes/02 D.mp3", {"D2", "Drake", "D", "", "", 2, 180});
        add("/m/drakes/03 D.mp3", {"D3", "Drake", "D", "", "", 3, 181});
        add("/m/one-two-three/01 Song.mp3", {"Song", "", "OTT", "", "", 1, 182});
        // album artists: guests are the same artist; the lead of most songs wins
        add("/m/drought/01.mp3", {"H", "x", "Drought", "Lil Wayne", "", 1, 130});
        add("/m/drought/02.mp3", {"I", "x", "Drought", "Lil Wayne Feat.Currency", "", 2, 131});
        add("/m/drought/03.mp3", {"J", "x", "Drought", "Lil Wayne Feat. Bun B", "", 3, 132});
        add("/m/drought/04.mp3", {"K", "x", "Drought", "Jadakiss", "", 4, 133});
        add("/m/drought/05.mp3", {"L", "x", "Drought", "Jadakiss", "", 5, 134});
        add("/m/drought/06.mp3", {"L2", "x", "Drought", "Lil Wayne", "", 6, 135});
        // two artists share a tape about evenly: nobody's album
        add("/m/split/01.mp3", {"P", "x", "Split", "Lil Wayne", "", 1, 150});
        add("/m/split/02.mp3", {"Q", "x", "Split", "Lil Wayne", "", 2, 151});
        add("/m/split/03.mp3", {"R", "x", "Split", "Jadakiss", "", 3, 152});
        add("/m/split/04.mp3", {"S", "x", "Split", "Jadakiss", "", 4, 153});
        add("/m/split/05.mp3", {"T", "x", "Split", "Jadakiss", "", 5, 154});
        // a tape by a pair, some songs by one of them alone
        add("/m/face/01.mp3", {"U", "x", "Face", "Juelz Santana  Lil Wayne", "", 1, 160});
        add("/m/face/02.mp3", {"V", "x", "Face", "Juelz Santana & Lil Wayne", "", 2, 161});
        add("/m/face/03.mp3", {"W", "x", "Face", "Juelz Santana & Lil Wayne", "", 3, 162});
        add("/m/face/04.mp3", {"X", "x", "Face", "Lil Wayne Ft. Young Buck", "", 4, 163});
        add("/m/face/05.mp3", {"Y", "x", "Face", "Juelz Santana", "", 5, 164});
        add("/m/face/06.mp3", {"Z", "x", "Face", "Juelz Santana & Bezel", "", 6, 165});
        // no artist leads: a compilation
        add("/m/comp/01.mp3", {"M", "x", "Comp", "One", "", 1, 140});
        add("/m/comp/02.mp3", {"N", "x", "Comp", "Two", "", 2, 141});
        add("/m/comp/03.mp3", {"O", "x", "Comp", "Three", "", 3, 142});
        // a title that is only a site's stamp
        add("/m/jeezy/09. Young Jeezy - DJ Drop Mixtapetrap.com.mp3", {"DJ Drop Mixtapetrap.com", "Young Jeezy", "Mr 17.5", "", "", 9, 20});
        add("/m/jeezy/10. www.site.net.mp3", {"www.site.net", "Young Jeezy", "Mr 17.5", "", "", 10, 21});
        const auto found = diagnose(files);
        auto find = [&](Kind k, const QString &path) -> const Finding * {
            for (const Finding &f : found)
                if (f.kind == k && f.path == path)
                    return &f;
            return nullptr;
        };
        QCOMPARE(find(Kind::MissingArtist, "/m/Dedication_2-2007-RAGEMP3/02-dj_drama_and_lil_wayne-getem.mp3")->after,
                 QString("DJ Drama And Lil Wayne"));
        const Finding *versus = find(Kind::MissingArtist, "/m/Versus/02.HOW DEEZ HOES BE-GUCCI MANE.mp3");
        QVERIFY(versus && versus->field.isEmpty() && versus->after.isEmpty());
        QCOMPARE(find(Kind::MissingArtist, "/m/home/04 D.mp3")->after, QString("Lil Wayne"));
        QVERIFY(find(Kind::MissingArtist, "/m/mix/03 C.mp3")->field.isEmpty());
        // the folder's name opens with a known artist; a guest in brackets is fine
        QCOMPARE(find(Kind::MissingArtist, "/m/lil-wayne-homecoming-2011/01. If I Die Today (Feat. Rick Ross).mp3")->after,
                 QString("Lil Wayne"));
        QCOMPARE(find(Kind::MissingArtist, "/m/lil-wayne-homecoming-2011/16. Can A Drummer Get Some Lil Wayne.mp3")->after,
                 QString("Lil Wayne"));
        // a file that credits another known artist is left alone
        QVERIFY(find(Kind::MissingArtist, "/m/lil-wayne-homecoming-2011/14. Pandemonium (Feat. Wale) Rick Ross.mp3")->field.isEmpty());
        // so is one whose name pairs another artist with the song, or ends in a
        // name the tags only mention as a guest
        QVERIFY(find(Kind::MissingArtist, "/m/lil-wayne-homecoming-2011/15 Erykah Badu-Jump Up (Feat Lil Wayne).mp3")->field.isEmpty());
        QVERIFY(find(Kind::MissingArtist, "/m/lil-wayne-homecoming-2011/17. Faded (Feat. Red Cafe) Bun B.mp3")->field.isEmpty());
        QCOMPARE(find(Kind::MissingArtist, "/m/lil-wayne-homecoming-2011/Lil wayne-Peoples Champ.mp3")->after, QString("Lil Wayne"));
        // the artist must open the name, and a rarely seen name is not known
        QVERIFY(find(Kind::MissingArtist, "/m/the-cartel-lil-wayne-rikers-2010/02 Uptown.mp3")->field.isEmpty());
        QVERIFY(find(Kind::MissingArtist, "/m/one-two-three/01 Song.mp3")->field.isEmpty());
        // a one-word name needs the dash that sets it apart
        QCOMPARE(find(Kind::MissingArtist, "/m/Drake - So Far Gone/01 Lust.mp3")->after, QString("Drake"));
        QVERIFY(find(Kind::MissingArtist, "/m/drake-in-the-morning/01 Song.mp3")->field.isEmpty());
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/drought/04.mp3")->after, QString("Lil Wayne"));
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/drought/02.mp3")->after, QString("Lil Wayne"));
        QVERIFY(!find(Kind::MixedAlbumArtist, "/m/drought/01.mp3"));
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/comp/02.mp3")->after, QString("Various Artists"));
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/split/01.mp3")->after, QString("Various Artists"));
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/split/05.mp3")->after, QString("Various Artists"));
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/face/01.mp3")->after, QString("Juelz Santana & Lil Wayne"));
        QCOMPARE(find(Kind::MixedAlbumArtist, "/m/face/05.mp3")->after, QString("Juelz Santana & Lil Wayne"));
        QVERIFY(!find(Kind::MixedAlbumArtist, "/m/face/02.mp3"));
        QCOMPARE(find(Kind::MissingTitle, "/m/jeezy/09. Young Jeezy - DJ Drop Mixtapetrap.com.mp3")->after, QString("DJ Drop"));
        // nothing usable anywhere: pointed out, with no fix offered
        const Finding *bare = find(Kind::MissingTitle, "/m/jeezy/10. www.site.net.mp3");
        QVERIFY(bare && bare->field.isEmpty());
    }

    // A log that can't be read is set aside, not replaced by an empty one.
    // A badly kept library: files dropped in one place, names from video
    // sites, tags that say nothing. The Doctor must not take a pile for an
    // album, and must say which fixes it is sure of.
    void doctorHandlesAMessyLibrary() {
        using namespace Doctor;
        std::vector<File> files;
        auto add = [&](const QString &path, Tags t) { files.push_back({path, t}); };
        // the music folder itself, and a Downloads folder: piles, not albums
        add("/m/Daft Punk - One More Time (Official Video) [dQw4w9WgXcQ].mp3", {"", "", "", "", "", 0, 320});
        add("/m/loose.mp3", {"Loose", "Somebody", "", "One", "", 0, 100});
        add("/m/loose2.mp3", {"Loose Two", "Somebody Else", "", "Two", "", 0, 101});
        add("/home/u/Downloads/a.mp3", {"A", "X", "", "", "", 0, 102});
        add("/home/u/Downloads/track.mp3", {"Unknown", "Unknown Artist", "Unknown Album", "", "", 0, 103});
        // a large folder that is neither numbered nor agreed on an album
        for (int i = 0; i < 9; ++i)
            add(QString("/m/grab bag/song %1.mp3").arg(QChar('a' + i)),
                {QString("Song %1").arg(QChar('A' + i)), i < 7 ? "Mostly Her" : "", i % 2 ? QString("Album %1").arg(i) : QString(), "", "", 0, 110.0 + i});
        // a real album: numbered files, most of them tagged alike
        add("/m/Discovery/01 One More Time.mp3", {"One More Time", "Daft Punk", "Discovery", "", "", 0, 200});
        add("/m/Discovery/02 Aerodynamic.mp3", {"AERODYNAMIC", "daft punk", "Discovery", "", "", 0, 201});
        add("/m/Discovery/03 Digital Love.mp3", {"Digital Love (Official Audio)", "Daft Punk", "Digital Love", "", "", 0, 202});
        add("/m/Discovery/04 Harder.mp3", {"Track 04", "Daft Punk", "Discovery", "", "", 4, 203});
        add("/m/Discovery/05 Crescendolls.mp3", {"Crescendolls", "Daft-Punk", "", "Various Artists", "", 5, 204});
        add("/m/Discovery/06 Nightvision.mp3", {"Nightvision", "", "Discovery", "Various Artists", "", 6, 205});
        const auto found = diagnose(files, {"/m"});
        auto find = [&](Kind k, const QString &path) -> const Finding * {
            for (const Finding &f : found)
                if (f.kind == k && f.path == path)
                    return &f;
            return nullptr;
        };
        // In a pile: no album from the folder's name, no album artist judged,
        // no artist from the neighbours, no track numbers.
        for (const Finding &f : found)
            if (f.path.startsWith("/m/loose") || f.path.startsWith("/home/u/Downloads") || f.path.startsWith("/m/grab bag"))
                QVERIFY2(f.kind != Kind::MissingAlbum && f.kind != Kind::MixedAlbumArtist && f.kind != Kind::MixedAlbum
                             && f.kind != Kind::MissingNumber && f.why != "folder-songs",
                         qPrintable(f.path + " " + kindName(f.kind)));
        // ...but a missing artist is still pointed out there,
        const Finding *grab = find(Kind::MissingArtist, "/m/grab bag/song h.mp3");
        QVERIFY(grab && grab->field.isEmpty());
        // and tags that say nothing count as missing.
        QVERIFY(find(Kind::MissingArtist, "/home/u/Downloads/track.mp3"));
        QVERIFY(find(Kind::MissingTitle, "/home/u/Downloads/track.mp3"));
        // A video site's file name: artist and title, without the label or the id.
        const QString video = "/m/Daft Punk - One More Time (Official Video) [dQw4w9WgXcQ].mp3";
        QCOMPARE(find(Kind::MissingArtist, video)->after, QString("Daft Punk"));
        QVERIFY(find(Kind::MissingArtist, video)->sure); // a name the library knows
        QCOMPARE(find(Kind::MissingTitle, video)->after, QString("One More Time"));
        QVERIFY(!find(Kind::MissingTitle, video)->sure);
        // The album: spelling by capitals alone is certain, by punctuation is not.
        const Finding *lower = find(Kind::ArtistSpelling, "/m/Discovery/02 Aerodynamic.mp3");
        QVERIFY(lower && lower->sure);
        QCOMPARE(lower->after, QString("Daft Punk"));
        const Finding *hyphen = find(Kind::ArtistSpelling, "/m/Discovery/05 Crescendolls.mp3");
        QVERIFY(hyphen && !hyphen->sure);
        QCOMPARE(hyphen->after, QString("Daft Punk"));
        // Capitals, labels, a title that is only its number.
        QCOMPARE(find(Kind::Case, "/m/Discovery/02 Aerodynamic.mp3"), nullptr); // one word: could be meant
        QCOMPARE(find(Kind::UploadLabel, "/m/Discovery/03 Digital Love.mp3")->after, QString("Digital Love"));
        QVERIFY(find(Kind::UploadLabel, "/m/Discovery/03 Digital Love.mp3")->sure);
        QCOMPARE(find(Kind::MissingTitle, "/m/Discovery/04 Harder.mp3")->after, QString("Harder"));
        // Track numbers from the file names; an album most of the folder names.
        const Finding *number = find(Kind::MissingNumber, "/m/Discovery/02 Aerodynamic.mp3");
        QVERIFY(number && number->sure);
        QCOMPARE(number->field, QString("tracknumber"));
        QCOMPARE(number->after, QString("2"));
        QVERIFY(!find(Kind::MissingNumber, "/m/Discovery/04 Harder.mp3"));
        QCOMPARE(find(Kind::MixedAlbum, "/m/Discovery/03 Digital Love.mp3")->after, QString("Discovery"));
        const Finding *album = find(Kind::MissingAlbum, "/m/Discovery/05 Crescendolls.mp3");
        QCOMPARE(album->after, QString("Discovery"));
        QVERIFY(!album->sure);
        // "Various Artists" is nobody's name; the neighbours say who it is.
        const Finding *various = find(Kind::MissingArtist, "/m/Discovery/06 Nightvision.mp3");
        QCOMPARE(various->after, QString("Daft Punk"));
        QCOMPARE(various->why, QString("folder-songs"));
        QVERIFY(!various->sure);
        // Stripping is conservative: words of the song stay.
        for (const Finding &f : found)
            if (!f.field.isEmpty())
                QVERIFY2(f.before != f.after, qPrintable(f.path));
    }

    // "Fix all" writes only what is certain; a folder's proposals and a
    // typed answer are one press each, and one U takes each back whole.
    void doctorFixesAFolderAtATime() {
        QTemporaryDir music, state;
        QVERIFY(QDir(music.path()).mkpath("Some Tape"));
        QStringList songs;
        for (int i = 1; i <= 3; ++i) {
            const QString song = music.filePath(QString("Some Tape/0%1 Song %1.mp3").arg(i));
            QProcess ff;
            ff.start("ffmpeg", {"-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=12",
                                "-metadata", QString("title=Song %1").arg(i), "-y", song});
            QVERIFY(ff.waitForFinished(30000));
            QCOMPARE(ff.exitCode(), 0);
            songs << song;
        }
        Library library({music.path()}, state.filePath("cache"), state.filePath("state"));
        library.rescan();
        QTRY_COMPARE_WITH_TIMEOUT(int(library.tracks().size()), 3, 20000);
        LibraryDoctor doctor(&library, state.filePath("doctor.json"));
        auto scan = [&] {
            doctor.scan();
            QTRY_VERIFY_WITH_TIMEOUT(!doctor.scanning(), 20000);
        };
        scan();
        const QString folder = music.filePath("Some Tape");
        QCOMPARE(doctor.counts().value("noartist").toInt(), 3);
        QCOMPARE(doctor.counts().value("noalbum").toInt(), 3);
        QCOMPARE(doctor.counts().value("nonumber/sure").toInt(), 3);
        // The album comes from the folder's name: a judgement, so not in Fix all.
        QCOMPARE(doctor.fixAll("noalbum"), 0);
        QCOMPARE(doctor.fixGroup("noalbum", folder), 3);
        Doctor::Tags t;
        QVERIFY(Doctor::readTags(songs[1], t));
        QCOMPARE(t.album, QString("Some Tape"));
        // Track numbers are certain.
        QCOMPARE(doctor.fixAll("nonumber"), 3);
        QVERIFY(Doctor::readTags(songs[2], t));
        QCOMPARE(t.number, 3);
        // Nothing names the artist: the person does, once for the folder.
        const QVariantMap first = [&] {
            for (const QVariant &v : doctor.findings())
                if (v.toMap().value("kind") == "noartist")
                    return v.toMap();
            return QVariantMap();
        }();
        QVERIFY(first.value("groupFirst").toBool() && first.value("answerable").toBool());
        QVERIFY(!first.value("fixable").toBool());
        QCOMPARE(first.value("groupSize").toInt(), 3);
        QCOMPARE(doctor.setGroup("noartist", folder, "   "), 0);
        QCOMPARE(doctor.setGroup("nonumber", folder, "9"), 0); // not a kind that takes an answer
        QCOMPARE(doctor.setGroup("noartist", music.filePath("Elsewhere"), "Nobody"), 0);
        QCOMPARE(doctor.setGroup("noartist", folder, "  Typed\n Artist\t"), 3);
        for (const QString &song : songs) {
            QVERIFY(Doctor::readTags(song, t));
            QCOMPARE(t.artist, QString("Typed Artist"));
        }
        QCOMPARE(doctor.counts().value("noartist").toInt(), 0);
        // U: the typed answer, then the numbers, then the album, each whole.
        QCOMPARE(doctor.undoable(), 9);
        QVERIFY(doctor.undoLast());
        QCOMPARE(doctor.undoable(), 6);
        for (const QString &song : songs) {
            QVERIFY(Doctor::readTags(song, t));
            QCOMPARE(t.artist, QString());
            QCOMPARE(t.album, QString("Some Tape"));
        }
        // The batches survive a restart.
        LibraryDoctor again(&library, state.filePath("doctor.json"));
        QVERIFY(again.undoLast());
        QCOMPARE(again.undoable(), 3);
        QVERIFY(Doctor::readTags(songs[2], t));
        QCOMPARE(t.number, 0);
        // A file edited since is left alone, and U says so.
        QVERIFY(Doctor::writeField(songs[0], "album", "Edited by hand"));
        QVERIFY(!again.undoLast());
        QCOMPARE(again.undoable(), 0);
        QVERIFY(Doctor::readTags(songs[0], t));
        QCOMPARE(t.album, QString("Edited by hand"));
        QVERIFY(Doctor::readTags(songs[1], t));
        QCOMPARE(t.album, QString());
    }

    // What `cymtx doctor` and an agent use: the same checks and guarded
    // writes with no window, sharing one undo log with the window.
    void doctorWorksWithoutAWindow() {
        QTemporaryDir music, state;
        QVERIFY(QDir(music.path()).mkpath("Some Tape"));
        QStringList songs;
        for (int i = 1; i <= 3; ++i) {
            const QString song = music.filePath(QString("Some Tape/0%1 Song %1.mp3").arg(i));
            QProcess ff;
            ff.start("ffmpeg", {"-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=12",
                                "-metadata", QString("title=Song %1 (Official Video)").arg(i), "-y", song});
            QVERIFY(ff.waitForFinished(30000));
            QCOMPARE(ff.exitCode(), 0);
            songs << song;
        }
        const QString log = state.filePath("doctor.json");
        LibraryDoctor window(nullptr, log); // stands for the open window
        LibraryDoctor command(nullptr, log);
        command.diagnoseFiles(songs, {music.path()});
        QVERIFY(command.found().size() >= 9);
        // Certain fixes of every kind: the labels and the track numbers.
        QCOMPARE(command.fixCertain({}), 6);
        Doctor::Tags t;
        QVERIFY(Doctor::readTags(songs[0], t));
        QCOMPARE(t.title, QString("Song 1"));
        QCOMPARE(t.number, 1);
        QCOMPARE(t.artist, QString());
        // An agent's answers: good ones are written, the rest refused and said.
        QSignalSpy said(&command, &LibraryDoctor::message);
        QCOMPARE(command.applyAnswers({{songs[0], "artist", "Drake"},
                                       {songs[1], "artist", " Drake\n & Lil Wayne "},
                                       {songs[2], "artist", ""},
                                       {songs[2], "tracknumber", "0"},
                                       {songs[2], "tracknumber", "12"},
                                       {songs[2], "genre", "Rap"},
                                       {songs[2], "title", "Song 3"},
                                       {music.filePath("missing.mp3"), "artist", "Nobody"}}),
                 3);
        QCOMPARE(said.count(), 6); // five refusals and the summary
        QVERIFY(Doctor::readTags(songs[1], t));
        QCOMPARE(t.artist, QString("Drake & Lil Wayne"));
        QVERIFY(Doctor::readTags(songs[2], t));
        QCOMPARE(t.number, 12);
        QCOMPARE(t.artist, QString());
        // An empty value clears an album, never a title.
        QCOMPARE(command.applyAnswers({{songs[0], "album", "Wrong"}}), 1);
        QCOMPARE(command.applyAnswers({{songs[0], "album", ""}, {songs[0], "title", ""}}), 1);
        QVERIFY(Doctor::readTags(songs[0], t));
        QCOMPARE(t.album, QString());
        QCOMPARE(t.title, QString("Song 1"));
        // The window undoes what the command wrote: one log, read afresh.
        QCOMPARE(window.undoable(), 0);
        QVERIFY(window.undoLast()); // the cleared album
        QVERIFY(Doctor::readTags(songs[0], t));
        QCOMPARE(t.album, QString("Wrong"));
        QVERIFY(window.undoLast()); // the album
        QVERIFY(window.undoLast()); // the three answers, whole
        QVERIFY(Doctor::readTags(songs[1], t));
        QCOMPARE(t.artist, QString());
        QVERIFY(Doctor::readTags(songs[2], t));
        QCOMPARE(t.number, 3);
        // ...and the command sees that the window did.
        QCOMPARE(window.undoable(), 6);
        QVERIFY(command.undoLast()); // the six certain fixes
        QCOMPARE(command.undoable(), 0);
        QVERIFY(Doctor::readTags(songs[0], t));
        QCOMPARE(t.title, QString("Song 1 (Official Video)"));
        QCOMPARE(t.number, 0);
    }

    void doctorKeepsAnUnreadableLog() {
        QTemporaryDir state;
        const QString log = state.filePath("doctor.json");
        {
            QFile f(log);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"fixes\": [ {\"path\": \"/a\", \"field\": \"title\", \"before\": \"x\"");   // cut short
        }
        LibraryDoctor doctor(nullptr, log);
        QCOMPARE(doctor.undoable(), 0);
        QVERIFY(!QFile::exists(log));
        QFile aside(log + ".unreadable");
        QVERIFY(aside.open(QIODevice::ReadOnly));
        QVERIFY(aside.readAll().contains("\"before\": \"x\""));
        // a good log still loads
        {
            QFile f(log);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"fixes\": [ {\"path\": \"/a\", \"field\": \"title\", \"before\": \"x\", \"after\": \"y\"} ]}");
        }
        LibraryDoctor again(nullptr, log);
        QCOMPARE(again.undoable(), 1);
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

    // Beats are found on loud, compressed masters too: the bass sits near
    // the top of its scale and a kick lifts it only a little (the old
    // ratio-over-average test never fired there), and on quiet ones; a
    // steady, noisy bass with no kicks gives none.
    void beatsAtAnyLoudness() {
        auto beatsIn = [](double floor, double kick, bool kicks) {
            BeatDetector beats;
            int found = 0, ticks = 0;
            double before = 0;
            uint32_t seed = 12345;
            const double period = 60.0 / 140 / 0.016;          // 140 BPM, in 16 ms ticks
            for (int t = 0; t < 900; ++t) {                     // 14.4 s
                seed = seed * 1664525u + 1013904223u;
                const double noise = (double(seed >> 8) / double(1u << 24) - 0.5) * 0.02;
                double bass = floor + noise;
                if (kicks) {
                    const double phase = std::fmod(t, period);
                    // A kick: up over two ticks, then back down over ten.
                    bass += phase < 2 ? kick * (phase + 1) / 2 : kick * std::max(0.0, 1 - (phase - 2) / 10);
                }
                const double pulse = beats.step(std::clamp(bass, 0.0, 1.0));
                if (t > 60) {                                  // after the first second
                    found += pulse > before * 0.9 + 1e-9 ? 1 : 0;
                    ++ticks;
                }
                before = pulse;
            }
            return std::pair<int, double>{found, ticks * 0.016 * 140 / 60};
        };
        const auto loud = beatsIn(0.80, 0.08, true);
        QVERIFY2(std::abs(loud.first - loud.second) <= 2, qPrintable(QString("loud: %1 of %2").arg(loud.first).arg(loud.second)));
        const auto quiet = beatsIn(0.35, 0.15, true);
        QVERIFY2(std::abs(quiet.first - quiet.second) <= 2, qPrintable(QString("quiet: %1 of %2").arg(quiet.first).arg(quiet.second)));
        const auto steady = beatsIn(0.80, 0.0, false);
        QVERIFY2(steady.first <= 1, qPrintable(QString("steady: %1").arg(steady.first)));
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
        QFile marker(dir.filePath("samples/.cymtxignore"));
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
        // A cover is only named when a song with cached art is loaded.
        QVERIFY(!status.value("status").toObject().contains("cover"));
        QVERIFY(Covers::cached(QString()).isEmpty());
        QVERIFY(Covers::cached("no-such-album-0000").isEmpty());
        QVERIFY(!remote.handle({{"cmd", "volume"}, {"args", QJsonArray{"150"}}}).value("ok").toBool());
        QVERIFY(remote.handle({{"cmd", "volume"}, {"args", QJsonArray{"40"}}}).value("ok").toBool());
        QCOMPARE(player.volume(), 0.4);
        QVERIFY(remote.handle({{"cmd", "repeat"}, {"args", QJsonArray{"one"}}}).value("ok").toBool());
        QCOMPARE(player.repeat(), int(Player::RepeatOne));
        QVERIFY(!remote.handle({{"cmd", "nonsense"}}).value("ok").toBool());
        QVERIFY(!remote.handle({{"cmd", "grab"}, {"args", QJsonArray{"/tmp/x.txt"}}}).value("ok").toBool());
        // visual: by name, by number, next/previous wrapping, bad names refused.
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"halo"}}}).value("visual").toString(), QString("halo"));
        QCOMPARE(player.visualStyle(), 9);
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"next"}}}).value("visual").toString(), QString("turntable"));
        QCOMPARE(player.visualStyle(), 10);
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"next"}}}).value("visual").toString(), QString("hologram"));
        QCOMPARE(player.visualStyle(), 11);
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"next"}}}).value("visual").toString(), QString("cymatics"));
        QCOMPARE(player.visualStyle(), 12);
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"next"}}}).value("visual").toString(), QString("ring"));
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"previous"}}}).value("visual").toString(), QString("cymatics"));
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"3"}}}).value("visual").toString(), QString("skyline"));
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"10"}}}).value("visual").toString(), QString("turntable"));
        QVERIFY(!remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"13"}}}).value("ok").toBool());
        QCOMPARE(remote.handle({{"cmd", "visual"}, {"args", QJsonArray{"3"}}}).value("visual").toString(), QString("skyline"));
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

    void pixelVisualsKeepTheirPeaksOnLoudMasters() {
        // A loud, compressed master: every band near the top. Drawn straight
        // it is one solid block; with the floor taken off, the difference
        // between its bands is most of the picture.
        const double floor = PixelVisual::floorFor(0.85);
        const double quieter = PixelVisual::shaped(0.75, floor), louder = PixelVisual::shaped(0.95, floor);
        QVERIFY2(louder - quieter > 0.35, qPrintable(QString::number(louder - quieter)));
        QVERIFY(louder < 1.0 && quieter > 0.0);
        // Quiet music is barely changed, silence not at all, and nothing
        // leaves 0..1.
        QVERIFY(std::fabs(PixelVisual::shaped(0.3, PixelVisual::floorFor(0.15)) - 0.3) < 0.08);
        QCOMPARE(PixelVisual::shaped(0.4, PixelVisual::floorFor(0)), 0.4);
        QCOMPARE(PixelVisual::shaped(0.2, 0.5), 0.0);
        QCOMPARE(PixelVisual::shaped(1.0, 0.5), 1.0);
        QCOMPARE(PixelVisual::shaped(1.0, 1.0), 0.0);
        // The middle moves with `middle`: the Skyline's tallest tower and
        // the Warp's stars follow it to the song's side.
        PixelVisual v;
        v.setWidth(96);
        v.setHeight(40);
        v.setMode(PixelVisual::Warp);
        v.setMiddle(0.3);
        for (int i = 0; i < 240; ++i)
            v.step(1.0 / 60, i % 30 == 0 ? 1.0 : 0.2, 0.7, 0.7, 0.5, 0.4);
        int leftHalf = 0, rightHalf = 0;
        for (int y = 0; y < 40; ++y)
            for (int x = 0; x < 96; ++x)
                if (qAlpha(v.frame().pixel(x, y)))
                    (x < 48 ? leftHalf : rightHalf)++;
        QVERIFY2(leftHalf > rightHalf, qPrintable(QString("%1 vs %2").arg(leftHalf).arg(rightHalf)));
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

    // AAA on every Omarchy theme: each text colour CymtX draws, on each
    // surface it draws it on, reads at 7:1 through CymtX's own Theme and
    // Style, for the themes installed here, the user's own themes, and
    // accents chosen to be awkward (mid greys and pure primaries, where
    // neither black nor white reaches 7:1 on the fill). Set
    // CYMTX_CONTRAST_REPORT=1 to print the worst pair per theme.
    void everyThemeReadsAt7To1() {
        QTemporaryDir synthetic;
        QStringList dirs;
        for (const QString &root : {QStringLiteral("/usr/share/omarchy/themes"),
                                    QDir::homePath() + "/.config/omarchy/themes"})
            for (const QFileInfo &d : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
                dirs << d.absoluteFilePath();
        for (const char *accent : {"#808080", "#777777", "#6e6e6e", "#ff0000", "#00ff00", "#0000ff", "#ffff00",
                                   "#ff00ff", "#ffffff", "#000000", "#3a3a3a", "#1e66f5"}) {
            const QString dir = synthetic.filePath(QString("accent-%1").arg(QString(accent).mid(1)));
            QDir().mkpath(dir);
            QFile f(dir + "/colors.toml");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray("accent = \"") + accent + "\"\n");
            f.close();
            dirs << dir;
        }
        // A theme folder with no colors.toml falls back to CymtX's yellow.
        dirs << synthetic.filePath("no-colours");
        QDir().mkpath(dirs.last());

        auto luminance = [](const QColor &c) {
            auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * lin(c.redF()) + 0.7152 * lin(c.greenF()) + 0.0722 * lin(c.blueF());
        };
        auto contrast = [&](const QColor &a, const QColor &b) {
            const double x = luminance(a), y = luminance(b);
            return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
        };
        const bool report = qEnvironmentVariableIntValue("CYMTX_CONTRAST_REPORT") == 1;
        qmlRegisterSingletonType(QUrl::fromLocalFile(QFINDTESTDATA("../src/Style.qml")), "CymtX", 1, 0, "Style");
        QStringList failures;
        for (const QString &dir : std::as_const(dirs)) {
            Theme theme(dir);
            QQmlPropertyMap playerStub;
            playerStub.insert("calm", false);
            QQmlEngine engine;
            engine.rootContext()->setContextProperty("theme", &theme);
            engine.rootContext()->setContextProperty("player", &playerStub);
            QObject *style = engine.singletonInstance<QObject *>("CymtX", "Style");
            QVERIFY2(style, "Style.qml didn't load");
            auto colour = [&](const char *name) { return style->property(name).value<QColor>(); };
            const QStringList texts{"text", "muted", "faint", "danger", "accentText"};
            const QStringList surfaces{"window", "panel", "raised", "hover", "scrim"};
            QList<std::pair<QString, QString>> pairs;
            for (const QString &t : texts)
                for (const QString &s : surfaces)
                    pairs.append(std::make_pair(t, s));
            pairs.append(std::make_pair(QString("accentText"), QString("accentWash")));
            pairs.append(std::make_pair(QString("accentInk"), QString("accentFill")));
            pairs.append(std::make_pair(QString("accentInk"), QString("accentFillHover")));
            double worst = 99;
            QString worstPair;
            for (const auto &[t, s] : pairs) {
                const double ratio = contrast(colour(t.toLatin1()), colour(s.toLatin1()));
                if (ratio < worst) {
                    worst = ratio;
                    worstPair = t + " on " + s;
                }
                if (ratio < 7)
                    failures << QString("%1: %2 on %3 = %4").arg(QFileInfo(dir).fileName(), t, s).arg(ratio, 0, 'f', 2);
            }
            if (report)
                qInfo("%-34s accent %s  fill %s  accentInk %s  accentText %s  worst %.2f (%s)",
                      qPrintable(QFileInfo(dir).fileName()), qPrintable(theme.accent()),
                      qPrintable(colour("accentFill").name()), qPrintable(colour("accentInk").name()),
                      qPrintable(colour("accentText").name()), worst, qPrintable(worstPair));
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join("\n")));
    }

    // The first run: CymtX finds the desktop's music folder by itself, a
    // folder chosen on the welcome screen is kept (with the ones it found)
    // and scanned, and having seen the welcome is remembered in state.json.
    void firstRunFindsMusicAndRemembersOnboarding() {
        QTemporaryDir home, config, cache, state, chosen;
        // HOME too: without it the fallback finds the real ~/Music.
        const QByteArray oldMusic = qgetenv("CYMTX_MUSIC"), oldConfig = qgetenv("XDG_CONFIG_HOME"), oldHome = qgetenv("HOME");
        qunsetenv("CYMTX_MUSIC");
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        qputenv("HOME", home.path().toUtf8());
        QDir().mkpath(home.filePath("Tunes"));
        QFile dirs(config.filePath("user-dirs.dirs"));
        QVERIFY(dirs.open(QIODevice::WriteOnly));
        dirs.write("XDG_MUSIC_DIR=\"" + home.filePath("Tunes").toUtf8() + "\"\n");
        dirs.close();
        QVERIFY(Library::defaultFolders().contains(home.filePath("Tunes")));

        writeWav(chosen.filePath("song.wav"), std::vector<float>(48000 * 12, 0.1f));
        {
            Library library({}, cache.path(), state.path());
            QVERIFY(library.folders().contains(home.filePath("Tunes")));
            QVERIFY(!library.addFolder(chosen.filePath("missing")));
            QSignalSpy scanned(&library, &Library::scanningChanged);
            QVERIFY(library.addFolder(QUrl::fromLocalFile(chosen.path()).toString()));
            while (library.scanning() || scanned.isEmpty())
                QVERIFY(scanned.wait(10000));
            QCOMPARE(library.trackCount(), 1);
            QFile list(Library::foldersFile());
            QVERIFY(list.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(list.readAll());
            QVERIFY(text.contains(home.filePath("Tunes") + "\n"));
            QVERIFY(text.contains(QDir::cleanPath(chosen.path()) + "\n"));
            // Adding it again changes nothing.
            QVERIFY(library.addFolder(chosen.path()));
            list.seek(0);
            QCOMPARE(QString::fromUtf8(list.readAll()), text);
        }
        QCOMPARE(Library::defaultFolders(), (QStringList{home.filePath("Tunes"), QDir::cleanPath(chosen.path())}));

        {
            Library library({chosen.path()}, cache.path(), state.path());
            Analyzer analyzer(cache.filePath("analysis.json"));
            Player player(&library, &analyzer, state.path());
            QVERIFY(!player.onboarded());
            player.setOnboarded(true);
            player.flush();
        }
        {
            Library library({chosen.path()}, cache.path(), state.path());
            Analyzer analyzer(cache.filePath("analysis.json"));
            Player player(&library, &analyzer, state.path());
            QVERIFY(player.onboarded());
        }
        if (oldMusic.isEmpty()) qunsetenv("CYMTX_MUSIC"); else qputenv("CYMTX_MUSIC", oldMusic);
        if (oldConfig.isEmpty()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", oldConfig);
        qputenv("HOME", oldHome);
    }

    // The setup screen's side of the library: the folders it offers, the
    // songs it counts in them, and the folders file it writes.
    void setupOffersCountsAndSavesFolders() {
        QTemporaryDir home, config, cache, state, drive;
        const QByteArray oldMusic = qgetenv("CYMTX_MUSIC"), oldConfig = qgetenv("XDG_CONFIG_HOME"), oldHome = qgetenv("HOME");
        qunsetenv("CYMTX_MUSIC");
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        qputenv("HOME", home.path().toUtf8());
        auto touch = [](const QString &path) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            return f.open(QIODevice::WriteOnly) && f.write("x") == 1;
        };
        const QString music = home.filePath("Music"), downloads = home.filePath("Downloads");
        QVERIFY(touch(music + "/Album/01.mp3"));
        QVERIFY(touch(music + "/Album/02.flac"));
        QVERIFY(touch(music + "/Album/cover.jpg"));
        QVERIFY(touch(music + "/.trash/gone.mp3"));          // hidden folders don't count
        QVERIFY(touch(music + "/Ringtones/.nomedia"));
        QVERIFY(touch(music + "/Ringtones/ring.mp3"));       // nor a folder marked .nomedia
        QVERIFY(touch(downloads + "/mix.ogg"));
        QVERIFY(touch(drive.filePath("Tapes/a.mp3")));

        bool more = true;
        QCOMPARE(Library::countAudioFiles(music, &more), 2);
        QVERIFY(!more);
        QCOMPARE(Library::countAudioFiles(downloads), 1);
        QCOMPARE(Library::countAudioFiles(home.filePath("nowhere")), 0);
        QCOMPARE(Library::localPath("~/Music"), music);
        QCOMPARE(Library::localPath(QUrl::fromLocalFile(music).toString()), music);

        Library library({}, cache.path(), state.path());
        QVERIFY(!library.foldersPinned());
        // What it reads now comes first and is ticked; Downloads is offered, unticked.
        const QVariantList choices = library.folderChoices();
        QCOMPARE(choices.first().toMap().value("path").toString(), music);
        QVERIFY(choices.first().toMap().value("chosen").toBool());
        QCOMPARE(choices.first().toMap().value("kind").toString(), QString("music"));
        bool offeredDownloads = false;
        for (const QVariant &c : choices)
            if (c.toMap().value("path").toString() == downloads) {
                offeredDownloads = true;
                QVERIFY(!c.toMap().value("chosen").toBool());
            }
        QVERIFY(offeredDownloads);

        // Counting answers by signal, for a folder and for something that isn't one.
        QSignalSpy counted(&library, &Library::songsCounted);
        library.countSongs(music);
        library.countSongs(home.filePath("nowhere"));
        QTRY_COMPARE_WITH_TIMEOUT(counted.size(), 2, 10000);
        QHash<QString, int> answers;
        for (const auto &c : counted)
            answers.insert(c.at(0).toString(), c.at(1).toInt());
        QCOMPARE(answers.value(music), 2);
        QCOMPARE(answers.value(home.filePath("nowhere")), -1);

        // Nothing usable: refused, and no file is written.
        QVERIFY(!library.setFolders({}));
        QVERIFY(!library.setFolders({home.filePath("nowhere"), ""}));
        QVERIFY(!QFile::exists(Library::foldersFile()));

        // Exactly the folders chosen, once each, missing ones dropped.
        QSignalSpy scanned(&library, &Library::scanningChanged);
        QVERIFY(library.setFolders({drive.filePath("Tapes"), "~/Downloads", drive.filePath("Tapes") + "/", home.filePath("nowhere")}));
        QCOMPARE(library.folders(), (QStringList{drive.filePath("Tapes"), downloads}));
        QCOMPARE(Library::defaultFolders(), (QStringList{drive.filePath("Tapes"), downloads}));
        while (library.scanning() || scanned.isEmpty())
            QVERIFY(scanned.wait(10000));
        // ~/Music is no longer read: only the two songs in the chosen folders.
        QCOMPARE(library.trackCount(), 2);
        QVERIFY(!library.folderChoices().first().toMap().value("path").toString().isEmpty());

        // CYMTX_MUSIC decides the folders: nothing can be saved over it.
        qputenv("CYMTX_MUSIC", music.toUtf8());
        QVERIFY(library.foldersPinned());
        QVERIFY(!library.setFolders({music}));
        QCOMPARE(Library::defaultFolders(), QStringList{music});

        if (oldMusic.isEmpty()) qunsetenv("CYMTX_MUSIC"); else qputenv("CYMTX_MUSIC", oldMusic);
        if (oldConfig.isEmpty()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", oldConfig);
        qputenv("HOME", oldHome);
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
            QVERIFY(lrclib.agents.value(0).startsWith("CymtX/"));
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
        const QString dir = root.filePath("cymtx");
        QVERIFY(QDir().mkpath(dir + "/covers"));
        chmod(QFile::encodeName(dir).constData(), 0755);
        chmod(QFile::encodeName(dir + "/covers").constData(), 0755);
        const QString state = dir + "/stats.json", cover = dir + "/covers/a.jpg";
        const QString victim = outside.filePath("not-cymtxs.txt");
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

    // `cymtx status` (and the bar widget polling it) gives up on a listener
    // that drips a reply forever or sends far more than any real reply.
    void cliReplyIsBoundedInTimeAndSize() {
        QTemporaryDir run;
        const QByteArray old = qgetenv("CYMTX_SOCKET");
        qputenv("CYMTX_SOCKET", QFile::encodeName(run.filePath("cymtx.sock")));
        {
            RogueSocket rogue(Remote::socketName(), RogueSocket::Trickle);
            QElapsedTimer clock;
            clock.start();
            QJsonObject reply;
            QVERIFY(!Remote::send({{"cmd", "status"}}, &reply, 800));
            QVERIFY2(clock.elapsed() < 3000, "a trickling reply kept the client waiting");
        }
        {
            RogueSocket rogue(Remote::socketName(), RogueSocket::Flood);
            QJsonObject reply;
            QVERIFY(!Remote::send({{"cmd", "status"}}, &reply, 10000));
        }
        if (old.isEmpty())
            qunsetenv("CYMTX_SOCKET");
        else
            qputenv("CYMTX_SOCKET", old);
    }

    void socketCutsOffOversizeCommands() {
        QTemporaryDir run, music, cache, state;
        const QByteArray old = qgetenv("CYMTX_SOCKET");
        qputenv("CYMTX_SOCKET", QFile::encodeName(run.filePath("cymtx.sock")));
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
            qunsetenv("CYMTX_SOCKET");
        else
            qputenv("CYMTX_SOCKET", old);
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
    // ---- playlists ----

    // A scanned library of `n` twelve-second tones, for the playlist tests.
    static void scanTones(const QString &folder, Library &library, int n, QStringList &files) {
        for (int i = 0; i < n; ++i) {
            files << folder + QString("/%1.wav").arg(i);
            writeWav(files.last(), std::vector<float>(48000 * 12, 0.05f));
        }
        library.rescan();
        QTRY_COMPARE_WITH_TIMEOUT(library.trackCount(), n, 20000);
    }

    void playlistsPersistAsM3u8() {
        QTemporaryDir music, cache, state, lists;
        Library library({music.path()}, cache.path(), state.path());
        QStringList files;
        scanTones(music.path(), library, 4, files);
        if (QTest::currentTestFailed()) return;
        QString id;
        {
            Playlists playlists(&library, nullptr, lists.path());
            id = playlists.create("Road Trip", {files[0], files[1], files[0]});
            QCOMPARE(id, QString("road-trip"));
            QCOMPARE(playlists.paths(id), (QStringList{files[0], files[1]})); // duplicates dropped on create
            QCOMPARE(playlists.add(id, {files[1], files[2]}), 1);            // files[1] already there
            QCOMPARE(playlists.add(id, {files[1]}, true), 1);                 // unless asked
            QCOMPARE(playlists.paths(id), (QStringList{files[0], files[1], files[2], files[1]}));
            QVERIFY(playlists.removeRows(id, {3, 3, 99}));
            QVERIFY(playlists.move(id, 2, 0));
            QVERIFY(!playlists.move(id, 0, 5));
            QVERIFY(playlists.rename(id, "Night Drive"));
            QVERIFY(playlists.setPinned(id, true));
            QVERIFY(!playlists.rename(id, "  \n "));
            QCOMPARE(playlists.add("nope", {files[3]}), 0);
        }
        Playlists again(&library, nullptr, lists.path());
        QCOMPARE(again.count(), 1);
        const Playlist *p = again.find(id);
        QVERIFY(p);
        QCOMPARE(p->name, QString("Night Drive"));
        QVERIFY(p->pinned);
        QCOMPARE(again.paths(id), (QStringList{files[2], files[0], files[1]}));
        QCOMPARE(again.lookup("night DRIVE")->id, id);

        QFile f(lists.filePath(id + ".m3u8"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.value(0), QString("#EXTM3U"));
        QCOMPARE(lines.value(1), QString("#PLAYLIST:Night Drive"));
        QVERIFY(lines.value(2).startsWith("#CYMTX:{"));
        QCOMPARE(int(lines.filter(QRegularExpression("^#EXTINF:12,")).size()), 3);
        const QStringList entries = lines.filter(QRegularExpression("^[^#]"));
        QCOMPARE(entries, (QStringList{files[2], files[0], files[1]}));
        for (const QString &e : entries)
            QVERIFY(e.startsWith('/'));
    }

    void playlistM3uParsing() {
        QByteArray data = "\xEF\xBB\xBF#EXTM3U\r\n#PLAYLIST:Mixed\r\n#EXTINF:100,A - B\r\nsub/song.mp3\r\n"
                          "file:///music/My%20Song.flac\r\nhttp://radio.example/stream\r\n"
                          "Albums\\Disc 1\\track.flac\r\n/abs/x.ogg\r\n\r\n# comment\r\n";
        const Playlist p = Playlists::parseM3u(data, "/base/dir");
        QCOMPARE(p.name, QString("Mixed"));
        QCOMPARE(p.paths, (QStringList{"/base/dir/sub/song.mp3", "/music/My Song.flac",
                                        "/base/dir/Albums/Disc 1/track.flac", "/abs/x.ogg"}));
        QByteArray many;
        for (int i = 0; i < Playlists::MaxEntries + 5; ++i)
            many += "/m/" + QByteArray::number(i) + ".mp3\n";
        QCOMPARE(int(Playlists::parseM3u(many, "/").paths.size()), Playlists::MaxEntries);
    }

    // A field with several values (two ARTIST credits on a FLAC): a fix
    // changes the one it is about, and undo puts back the whole list.
    void doctorKeepsEveryCreditInAField() {
        using L = QStringList;
        QCOMPARE(Doctor::withFirst({"A [x.com]", "B", "C"}, "A"), (L{"A", "B", "C"}));
        QCOMPARE(Doctor::withFirst({"www.x.com", "B"}, ""), (L{"B"}));
        QCOMPARE(Doctor::withFirst({}, "A"), (L{"A"}));
        QCOMPARE(Doctor::withFirst({}, ""), L{});

        QTemporaryDir music, state;
        const QString song = music.filePath("01 Song.flac");
        QProcess ff;
        ff.start("ffmpeg", {"-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=12", "-metadata",
                            "title=Song", "-metadata", "artist=Tester", "-metadata", "album=Album", "-y", song});
        QVERIFY(ff.waitForFinished(30000));
        QCOMPARE(ff.exitCode(), 0);
        const L credits{"Tester [DatPiff.com]", "Second Credit", "Third Credit"};
        const L albumCredits{"Various", "Guest"};
        QVERIFY(Doctor::writeValues(song, "artist", credits));
        QVERIFY(Doctor::writeValues(song, "albumartist", albumCredits));
        L got;
        QVERIFY(Doctor::readValues(song, "artist", got));
        QCOMPARE(got, credits);

        // Setting one field by hand keeps the rest of its list too.
        QVERIFY(Doctor::writeField(song, "albumartist", "Various Artists"));
        QVERIFY(Doctor::readValues(song, "albumartist", got));
        QCOMPARE(got, (L{"Various Artists", "Guest"}));

        Library library({music.path()}, state.filePath("cache"), state.filePath("state"));
        library.rescan();
        QTRY_COMPARE_WITH_TIMEOUT(int(library.tracks().size()), 1, 20000);
        const QString log = state.filePath("doctor.json");
        {
            LibraryDoctor doctor(&library, log);
            doctor.scan();
            QTRY_VERIFY_WITH_TIMEOUT(!doctor.scanning(), 20000);
            QCOMPARE(doctor.fixable(), 1);
            const QVariantMap f = doctor.findings().first().toMap();
            QCOMPARE(f.value("field").toString(), QString("artist"));
            QCOMPARE(f.value("after").toString(), QString("Tester"));
            QVERIFY(doctor.fix(0));
            QVERIFY(Doctor::readValues(song, "artist", got));
            QCOMPARE(got, (L{"Tester", "Second Credit", "Third Credit"}));
            QVERIFY(Doctor::readValues(song, "albumartist", got));
            QCOMPARE(got, (L{"Various Artists", "Guest"}));
        }
        // After a restart, undo restores every credit.
        {
            LibraryDoctor doctor(&library, log);
            QCOMPARE(doctor.undoable(), 1);
            QVERIFY(doctor.undoLast());
            QVERIFY(Doctor::readValues(song, "artist", got));
            QCOMPARE(got, credits);
        }
        // A credit changed by hand after the fix, even a later one, stops the undo.
        LibraryDoctor doctor(&library, log);
        doctor.scan();
        QTRY_VERIFY_WITH_TIMEOUT(!doctor.scanning(), 20000);
        QVERIFY(doctor.fix(0));
        QVERIFY(Doctor::writeValues(song, "artist", {"Tester", "Second Credit", "Someone New"}));
        QVERIFY(!doctor.undoLast());
        QVERIFY(Doctor::readValues(song, "artist", got));
        QCOMPARE(got, (L{"Tester", "Second Credit", "Someone New"}));
    }

    // A playlist another player keeps in the shared folder: radio entries
    // and that player's tags survive everything CymtX does to the file.
    void sharedPlaylistKeepsWhatCymtXDoesNotPlay() {
        QTemporaryDir music, cache, state, lists;
        Library library({music.path()}, cache.path(), state.path());
        QStringList files;
        scanTones(music.path(), library, 3, files);
        if (QTest::currentTestFailed()) return;
        const QString rel = QDir(lists.path()).relativeFilePath(files[1]);
        const QByteArray original = "#EXTM3U\r\n#EXTINF:-1,Radio One\r\n#EXTVLCOPT:network-caching=1000\r\n"
                                    "http://radio.example/one\r\n#EXTINF:12,Tone Zero\r\n" + files[0].toUtf8() +
                                    "\r\n# a note\r\n#EXTGRP:Faves\r\n" + rel.toUtf8() +
                                    "\r\n#EXTINF:-1,Radio Two\r\nhttps://radio.example/two\r\n";
        const QString file = lists.filePath("mixed.m3u");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(original);
        }
        auto read = [&] {
            QFile f(file);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        auto without = [](QByteArray data, const QByteArray &prefix) {
            QList<QByteArray> kept;
            for (const QByteArray &line : data.split('\n'))
                if (!line.startsWith(prefix))
                    kept << line;
            return kept.join('\n');
        };
        Playlists playlists(&library, nullptr, lists.path());
        QCOMPARE(playlists.paths("mixed"), (QStringList{files[0], files[1]}));

        // Pin, unpin, describe: one line of CymtX's is added, nothing else moves.
        QVERIFY(playlists.setPinned("mixed", true));
        QVERIFY(read().contains("\n#CYMTX:{"));
        QCOMPARE(without(read(), "#CYMTX:"), original);
        QVERIFY(playlists.setPinned("mixed", false));
        QVERIFY(playlists.setDescription("mixed", "For the road"));
        QCOMPARE(without(read(), "#CYMTX:"), original);
        QVERIFY(playlists.rename("mixed", "Mixed Up"));
        QCOMPARE(without(without(read(), "#CYMTX:"), "#PLAYLIST:"), original);
        QVERIFY(read().contains("#PLAYLIST:Mixed Up\r\n"));
        {
            Playlists again(&library, nullptr, lists.path());
            QCOMPARE(again.find("mixed")->name, QString("Mixed Up"));
            QCOMPARE(again.find("mixed")->description, QString("For the road"));
            QVERIFY(!again.find("mixed")->pinned);
            QVERIFY(again.setPinned("mixed", true));
        }

        // Changing the songs rewrites the list, with the streams, their tags
        // and the other player's lines still in place.
        Playlists edited(&library, nullptr, lists.path());
        QVERIFY(edited.find("mixed")->pinned);
        QCOMPARE(edited.add("mixed", {files[2]}), 1);
        // The file's own line endings stay (it was written with CRLF).
        QVERIFY(!read().replace("\r\n", "").contains('\n'));
        QStringList lines = QString::fromUtf8(read()).remove('\r').split('\n', Qt::SkipEmptyParts);
        auto index = [&](const QString &line) { return int(lines.indexOf(line)); };
        QVERIFY(index("http://radio.example/one") > 0);
        QCOMPARE(lines.value(index("http://radio.example/one") - 1), QString("#EXTVLCOPT:network-caching=1000"));
        QCOMPARE(lines.value(index("http://radio.example/one") - 2), QString("#EXTINF:-1,Radio One"));
        QVERIFY(index("http://radio.example/one") < index(files[0]));
        QVERIFY(index(files[0]) < index("# a note"));
        QCOMPARE(index("#EXTGRP:Faves"), index("# a note") + 1);
        QVERIFY(index(rel) > index("#EXTGRP:Faves")); // spelt as the file had it
        QCOMPARE(lines.value(index("https://radio.example/two") - 1), QString("#EXTINF:-1,Radio Two"));
        QVERIFY(index(rel) < index("https://radio.example/two"));
        QVERIFY(index("https://radio.example/two") < index(files[2]));

        // Taking out the song a stream followed keeps the stream.
        QVERIFY(edited.move("mixed", 0, 1));
        QVERIFY(edited.removeRows("mixed", {0, 1}));
        const QString text = QString::fromUtf8(read()).remove('\r');
        QVERIFY(text.contains("#EXTINF:-1,Radio One\n#EXTVLCOPT:network-caching=1000\nhttp://radio.example/one\n"));
        QVERIFY(text.contains("#EXTINF:-1,Radio Two\nhttps://radio.example/two\n"));
        QVERIFY(!text.contains("#EXTGRP")); // that tag belonged to a removed song
        Playlists last(&library, nullptr, lists.path());
        QCOMPARE(last.paths("mixed"), (QStringList{files[2]}));
        QCOMPARE(int(last.find("mixed")->kept.size()), 2);
    }

    void playlistHeaderPatchKeepsEveryOtherByte() {
        Playlist p;
        p.name = "New Name";
        p.created = 5;
        p.pinned = true;
        const QByteArray cymtx = "#CYMTX:{\"created\":5,\"pinned\":true}";
        // No header at all, no final newline, not UTF-8.
        QCOMPARE(Playlists::patchHeader("caf\xE9.mp3\nhttp://x/y", p, false), cymtx + "\ncaf\xE9.mp3\nhttp://x/y");
        QCOMPARE(Playlists::patchHeader("", p, true), QByteArray("#PLAYLIST:New Name\n") + cymtx + "\n");
        // A BOM and CRLF stay; existing lines are replaced where they are.
        QCOMPARE(Playlists::patchHeader("\xEF\xBB\xBF#EXTM3U\r\n#PLAYLIST:Old\r\n#CYMTX:{}\r\na.mp3\r\n", p, true),
                 QByteArray("\xEF\xBB\xBF#EXTM3U\r\n#PLAYLIST:New Name\r\n") + cymtx + "\r\na.mp3\r\n");
        QCOMPARE(Playlists::patchHeader("#EXTM3U\r\n#PLAYLIST:Old\r\na.mp3\r\n", p, false),
                 QByteArray("#EXTM3U\r\n#PLAYLIST:Old\r\n") + cymtx + "\r\na.mp3\r\n");
        // More entries than CymtX lists stay in the file through a rewrite.
        QByteArray many = "#EXTM3U\n";
        for (int i = 0; i < Playlists::MaxEntries + 5; ++i)
            many += "/m/" + QByteArray::number(i) + ".mp3\n";
        const Playlist big = Playlists::parseM3u(many, "/");
        QCOMPARE(int(big.paths.size()), Playlists::MaxEntries);
        const QString again = Playlists::m3u8(big, nullptr);
        QVERIFY(again.endsWith(QString("/m/%1.mp3\n").arg(Playlists::MaxEntries + 4)));
        QCOMPARE(int(again.count('\n')), Playlists::MaxEntries + 5 + 3);
    }

    // Text from tags, file names and playlists is shown as written: markup
    // in it is never interpreted, so it can't fetch an image from somewhere.
    void tagTextIsNeverMarkup() {
        QDir src(QFileInfo(QFINDTESTDATA("../src/Style.qml")).absolutePath());
        QStringList files;
        for (const QString &name : src.entryList({"*.qml"}, QDir::Files))
            files << src.filePath(name);
        files << QFINDTESTDATA("../plugin/CymtXWidget.qml");
        QVERIFY(files.size() > 40);
        const QRegularExpression opens("^\\s*(?:[A-Za-z_][\\w.]*:\\s*)?(?:Text|Label|TextEdit|TextArea) \\{");
        int checked = 0;
        for (const QString &path : files) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
            for (int i = 0; i < lines.size(); ++i) {
                if (!opens.match(lines[i]).hasMatch())
                    continue;
                QString body;
                int depth = 0;
                for (int j = i; j < lines.size(); ++j) {
                    body += lines[j] + "\n";
                    depth += int(lines[j].count('{')) - int(lines[j].count('}'));
                    if (depth <= 0)
                        break;
                }
                ++checked;
                QVERIFY2(body.contains("textFormat: Text.PlainText"),
                         qPrintable(QString("%1:%2 has no textFormat: Text.PlainText").arg(path).arg(i + 1)));
            }
        }
        QVERIFY(checked > 100);

        // A stock menu item or tooltip would read markup, so the two places
        // a tag or a playlist's name reaches one are made plain.
        auto source = [](const QString &name) {
            QFile f(QFINDTESTDATA("../src/" + name));
            return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
        };
        QVERIFY(source("AddToPlaylistMenu.qml").contains(QRegularExpression("contentItem: Text \\{\\s+textFormat: Text.PlainText")));
        QVERIFY(source("Main.qml").contains("ToolTip.toolTip.contentItem.textFormat = Text.PlainText"));

        // Shown for real: nothing asks a server for the image in the text.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QString markup = QString("<b>x</b><img src=\"http://127.0.0.1:%1/x.png\">").arg(server.serverPort());
        QQuickView view;
        view.rootContext()->setContextProperty("markup", markup);
        QQmlComponent component(view.engine());
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            Column {
                id: root
                width: 400; height: 300
                property alias probe: probe
                property string tipFormat: ""
                Text { textFormat: Text.PlainText; text: markup }
                TextField { text: markup }
                MenuItem {
                    id: item
                    text: markup
                    contentItem: Text { textFormat: Text.PlainText; text: item.text }
                }
                Rectangle {
                    id: tipped
                    width: 20; height: 20
                    ToolTip.text: markup
                    Component.onCompleted: {
                        tipped.ToolTip.toolTip.contentItem.textFormat = Text.PlainText
                        ToolTip.visible = true
                        root.tipFormat = tipped.ToolTip.toolTip.contentItem.textFormat === Text.PlainText ? "plain" : "other"
                    }
                }
                Loader { id: probe; active: false; sourceComponent: Text { text: markup } }
            }
        )", QUrl::fromLocalFile(QFINDTESTDATA("../src/Main.qml")));
        QScopedPointer<QQuickItem> root(qobject_cast<QQuickItem *>(component.create(view.rootContext())));
        QVERIFY2(root, qPrintable(component.errorString()));
        root->setParentItem(view.contentItem());
        view.resize(400, 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QCOMPARE(root->property("tipFormat").toString(), QString("plain"));
        QVERIFY(!server.waitForNewConnection(700));
        // The probe works: text left to guess its format does fetch.
        root->property("probe").value<QObject *>()->setProperty("active", true);
        QVERIFY(server.waitForNewConnection(5000));
    }

    // An MP3 tagged by ffmpeg keeps its comment in a TXXX frame; writing any
    // other field makes TagLib add a COMM frame beside it, so the comment
    // reads back twice. Fixing the title and then the comment must still
    // clear the watermark from every copy, and undo must still work.
    void doctorFixesACommentStoredTwice() {
        QCOMPARE(Doctor::withFirst({"A | x.com", "A | x.com"}, "A"), (QStringList{"A", "A"}));
        QCOMPARE(Doctor::withFirst({"x.com", "x.com", "B"}, ""), (QStringList{"B"}));
        QTemporaryDir music, state;
        const QString song = music.filePath("Song.mp3");
        QProcess ff;
        ff.start("ffmpeg", {"-loglevel", "error", "-f", "lavfi", "-i", "sine=frequency=440:duration=12", "-metadata",
                            "title=Song [DatPiff.com]", "-metadata", "artist=Tester", "-metadata", "album=Album",
                            "-metadata", "comment=Hot tape | DatPiff.com", "-y", song});
        QVERIFY(ff.waitForFinished(30000));
        QCOMPARE(ff.exitCode(), 0);
        Library library({music.path()}, state.filePath("cache"), state.filePath("state"));
        library.rescan();
        QTRY_COMPARE_WITH_TIMEOUT(int(library.tracks().size()), 1, 20000);
        LibraryDoctor doctor(&library, state.filePath("doctor.json"));
        doctor.scan();
        QTRY_VERIFY_WITH_TIMEOUT(!doctor.scanning(), 20000);
        const int fixes = doctor.fixable();
        QVERIFY(fixes >= 1);
        QCOMPARE(doctor.fixAll("watermark"), fixes);
        Doctor::Tags t;
        QVERIFY(Doctor::readTags(song, t));
        QCOMPARE(t.title, QString("Song"));
        QStringList comments;
        QVERIFY(Doctor::readValues(song, "comment", comments));
        for (const QString &c : comments)
            QVERIFY2(!Doctor::hasWatermark(c), qPrintable(c));
        doctor.scan();
        QTRY_VERIFY_WITH_TIMEOUT(!doctor.scanning(), 20000);
        QCOMPARE(doctor.fixable(), 0);
        // One U takes back everything that press wrote.
        QCOMPARE(doctor.undoable(), fixes);
        QVERIFY(doctor.undoLast());
        QCOMPARE(doctor.undoable(), 0);
        QVERIFY(Doctor::readTags(song, t));
        QCOMPARE(t.title, QString("Song [DatPiff.com]"));
        QCOMPARE(t.comment, QString("Hot tape | DatPiff.com"));
    }

    // What a rewrite must not lose or move, on plain data.
    void playlistRewriteKeepsTheFileAsItWas() {
        auto again = [](const QByteArray &data, const std::function<void(Playlist &)> &change = {}) {
            Playlist p = Playlists::parseM3u(data, "/pl");
            if (change)
                change(p);
            return Playlists::m3u8(p, nullptr);
        };
        auto body = [](const QString &text) { // without the name and CymtX's line
            QStringList out;
            for (const QString &line : text.split('\n', Qt::SkipEmptyParts))
                if (!line.startsWith("#PLAYLIST:") && !line.startsWith("#CYMTX:"))
                    out << line;
            return out;
        };
        using L = QStringList;
        // The header keeps its attributes and stays first.
        const QByteArray tv = "#EXTM3U url-tvg=\"x\"\n#EXTINF:-1,r\nhttp://s\n";
        QCOMPARE(body(again(tv)), (L{"#EXTM3U url-tvg=\"x\"", "#EXTINF:-1,r", "http://s"}));
        QVERIFY(Playlists::patchHeader(tv, Playlist(), false).startsWith("#EXTM3U url-tvg=\"x\"\n#CYMTX:"));
        // Tags above the first song that are about the file stay at the top
        // when that song goes or moves; a tag about the song goes with it.
        const QByteArray tagged = "#EXTM3U\n#EXTENC:UTF-8\n#EXTGRP:Rock\n# mine\n#EXTVLCOPT:x\n/a\n/b\nhttp://s\n";
        QCOMPARE(body(again(tagged)), (L{"#EXTM3U", "#EXTENC:UTF-8", "#EXTGRP:Rock", "# mine", "#EXTVLCOPT:x", "/a", "/b", "http://s"}));
        QCOMPARE(body(again(tagged, [](Playlist &p) { p.paths.removeFirst(); })),
                 (L{"#EXTM3U", "#EXTENC:UTF-8", "#EXTGRP:Rock", "# mine", "/b", "http://s"}));
        QCOMPARE(body(again(tagged, [](Playlist &p) { p.paths.move(0, 1); })),
                 (L{"#EXTM3U", "#EXTENC:UTF-8", "#EXTGRP:Rock", "# mine", "/b", "http://s", "#EXTVLCOPT:x", "/a"}));
        // A song listed twice: each keeps its place, spelling and #EXTINF,
        // and what follows the second stays after the second.
        const QByteArray twice = "#EXTM3U\na.mp3\n/b\n#EXTINF:3,Second\n./a.mp3\nhttp://s\n/c\n#EXTINF:9,left over\n";
        const L twiceBody{"#EXTM3U", "a.mp3", "/b", "#EXTINF:3,Second", "./a.mp3", "http://s", "/c", "#EXTINF:9,left over"};
        QCOMPARE(body(again(twice)), twiceBody);
        QCOMPARE(body(again(again(twice).toUtf8())), twiceBody);
        QCOMPARE(Playlists::parseM3u(twice, "/pl").paths, (L{"/pl/a.mp3", "/b", "/pl/a.mp3", "/c"}));
        // Not UTF-8: every byte comes back, and the list says so.
        const QByteArray latin = "#EXTM3U\n#EXTINF:-1,R\xE9\nhttp://s/\xE9\n/music/caf\xE9.mp3\n";
        const Playlist old = Playlists::parseM3u(latin, "/pl");
        QVERIFY(old.bytes);
        QVERIFY(!Playlists::parseM3u("#EXTM3U\n/music/caf\xC3\xA9.mp3\n", "/pl").bytes);
        QCOMPARE(body(Playlists::m3u8(old, nullptr)).join('\n').toLatin1() + "\n", latin);
    }

    // The same through the files: a playlist in an old code page keeps its
    // bytes when a song is added, and refuses a name it can't hold.
    void playlistInAnOldCodePageKeepsItsBytes() {
        QTemporaryDir music, cache, state, lists;
        Library library({music.path()}, cache.path(), state.path());
        QStringList files;
        scanTones(music.path(), library, 2, files);
        if (QTest::currentTestFailed()) return;
        const QByteArray original = "#EXTM3U\r\n#EXTINF:-1,R\xE9\r\nhttp://s/\xE9\r\n/music/caf\xE9.mp3\r\n";
        const QString file = lists.filePath("old.m3u");
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(original);
        }
        auto read = [&] {
            QFile f(file);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        Playlists playlists(&library, nullptr, lists.path());
        QVERIFY(playlists.find("old")->bytes);
        QCOMPARE(playlists.add("old", {files[0]}), 1);
        const QByteArray now = read();
        QVERIFY(now.contains("#EXTINF:-1,R\xE9\r\nhttp://s/\xE9\r\n"));
        QVERIFY(now.contains("/music/caf\xE9.mp3\r\n"));
        QVERIFY(now.contains(files[0].toUtf8() + "\r\n"));
        QVERIFY(!now.contains("\xEF\xBF\xBD")); // no replacement characters
        // A name outside what the file can hold changes nothing on disk.
        QSignalSpy said(&playlists, &Playlists::message);
        QVERIFY(!playlists.rename("old", QString::fromUtf8("\xE6\x97\xA5\xE6\x9C\xAC")));
        QCOMPARE(said.size(), 1);
        QCOMPARE(read(), now);
        QVERIFY(playlists.rename("old", QString::fromUtf8("Caf\xC3\xA9")));
        QVERIFY(read().contains("#PLAYLIST:Caf\xE9\r\n"));
    }

    void smartPlaylistRules() {
        auto track = [](const QString &title, const QString &artist, const QString &album, int year, int number,
                        qint64 mtime, const QString &albumArtist = {}) {
            Track t;
            t.path = "/m/" + album + "/" + title + ".flac";
            t.title = title;
            t.artist = artist;
            t.albumArtist = albumArtist;
            t.album = album;
            t.genre = artist == "Dilla" ? "Hip-Hop" : "Jazz";
            t.year = year;
            t.number = number;
            t.duration = 180;
            t.mtime = mtime;
            return t;
        };
        const qint64 now = 1'800'000'000;
        const qint64 day = 86400;
        std::vector<Track> tracks{
            track("Workinonit", "Dilla", "Donuts", 2006, 2, now - 2 * day),
            track("Waves", "Dilla", "Donuts", 2006, 1, now - 400 * day),
            track("So What", "Miles", "Kind of Blue", 1959, 1, now - 10 * day, "Miles Davis"),
            track("Untitled", "Unknown", "Tape", 0, 1, now - 100 * day),
        };
        QHash<QString, TrackFacts> facts;
        facts[tracks[0].path] = {5, now - day, 92, -9};
        facts[tracks[1].path] = {1, now - 60 * day, 0, 0}; // not analysed
        facts[tracks[2].path] = {9, 0, 136, -18};
        auto factsOf = [&](const Track &t) { return facts.value(t.path); };
        auto titles = [&](const QVariantMap &spec, qint64 at = 1'800'000'000) {
            QStringList out;
            for (int i : Playlists::evaluate(Playlists::fromSpec(spec), tracks, factsOf, at))
                out << tracks[size_t(i)].title;
            return out;
        };
        auto rule = [](const QString &f, const QString &op, const QString &v) {
            return QVariantMap{{"field", f}, {"op", op}, {"value", v}};
        };
        auto spec = [](QVariantList rules, bool all = true, const QString &sort = "title", bool desc = false, int limit = 0) {
            return QVariantMap{{"rules", rules}, {"matchAll", all}, {"sort", sort}, {"descending", desc}, {"limit", limit}};
        };

        QCOMPARE(titles(spec({rule("artist", "contains", "dil")})), (QStringList{"Waves", "Workinonit"}));
        QCOMPARE(titles(spec({rule("artist", "notContains", "dil")})), (QStringList{"So What", "Untitled"}));
        QCOMPARE(titles(spec({rule("artist", "is", "miles davis")})), QStringList{"So What"}); // album artist line
        QCOMPARE(titles(spec({rule("artist", "isNot", "Miles")})), (QStringList{"Untitled", "Waves", "Workinonit"}));
        QCOMPARE(titles(spec({rule("title", "startsWith", "w")})), (QStringList{"Waves", "Workinonit"}));
        QCOMPARE(titles(spec({rule("genre", "is", "hip-hop")})), (QStringList{"Waves", "Workinonit"}));
        // Unknown tempo matches no tempo rule, either way.
        QCOMPARE(titles(spec({rule("bpm", "less", "200")})), (QStringList{"So What", "Workinonit"}));
        QCOMPARE(titles(spec({rule("bpm", "isNot", "92")})), QStringList{"So What"});
        QCOMPARE(titles(spec({rule("loudness", "greater", "-12")})), QStringList{"Workinonit"});
        QCOMPARE(titles(spec({rule("plays", "greater", "3")})), (QStringList{"So What", "Workinonit"}));
        QCOMPARE(titles(spec({rule("length", "is", "3")})).size(), 4);
        // An unknown year is never "is", always "isNot".
        QCOMPARE(titles(spec({rule("year", "less", "2000")})), QStringList{"So What"});
        QCOMPARE(titles(spec({rule("year", "isNot", "2006")})), (QStringList{"So What", "Untitled"}));
        QCOMPARE(titles(spec({rule("added", "inLast", "30")})), (QStringList{"So What", "Workinonit"}));
        QCOMPARE(titles(spec({rule("added", "notInLast", "30")})), (QStringList{"Untitled", "Waves"}));
        QCOMPARE(titles(spec({rule("played", "never", "")})), (QStringList{"So What", "Untitled"}));
        QCOMPARE(titles(spec({rule("played", "inLast", "7")})), QStringList{"Workinonit"});
        // All versus any.
        QCOMPARE(titles(spec({rule("artist", "is", "Dilla"), rule("plays", "greater", "3")})), QStringList{"Workinonit"});
        QCOMPARE(titles(spec({rule("artist", "is", "Unknown"), rule("plays", "greater", "8")}, false)),
                 (QStringList{"So What", "Untitled"}));
        // No rules: everything.
        QCOMPARE(titles(spec({})).size(), 4);
        // Sorting and limits; an album's songs keep album order on a tie.
        QCOMPARE(titles(spec({}, true, "plays", true, 2)), (QStringList{"So What", "Workinonit"}));
        QCOMPARE(titles(spec({rule("artist", "is", "Dilla")}, true, "album")), (QStringList{"Waves", "Workinonit"}));
        QCOMPARE(titles(spec({}, true, "added", true)), (QStringList{"Workinonit", "So What", "Untitled", "Waves"}));

        // Invalid rules are dropped; an unknown sort falls back.
        const Playlist p = Playlists::fromSpec(spec({rule("artist", "greater", "1"), rule("bogus", "is", "x"),
                                                     rule("added", "never", ""), rule("plays", "less", "2")},
                                                    true, "nonsense"));
        QCOMPARE(int(p.rules.size()), 1);
        QCOMPARE(p.sort, QString("added"));
        QCOMPARE(Playlists::fromSpec(Playlists::toSpec(p)).rules.size(), p.rules.size());

        // Random: the same all day, different another day.
        std::vector<Track> many;
        for (int i = 0; i < 30; ++i)
            many.push_back(track(QString("T%1").arg(i), "A", "B", 2000, i, now));
        Playlist r = Playlists::fromSpec({{"sort", "random"}});
        r.name = "Shuffle";
        auto none = [](const Track &) { return TrackFacts{}; };
        const auto morning = Playlists::evaluate(r, many, none, now - now % day + 3600);
        const auto evening = Playlists::evaluate(r, many, none, now - now % day + 20 * 3600);
        const auto tomorrow = Playlists::evaluate(r, many, none, now - now % day + day + 3600);
        QCOMPARE(morning, evening);
        QVERIFY(morning != tomorrow);
        QCOMPARE(int(morning.size()), 30);
    }

    void playlistTrashAndNames() {
        QTemporaryDir lists;
        Playlists playlists(nullptr, nullptr, lists.path());
        QCOMPARE(Playlists::cleanName("  Late\nNight\tMix\x01 "), QString("Late Night Mix"));
        QCOMPARE(Playlists::cleanName(QString(500, 'x')).size(), Playlists::MaxNameLength);
        QVERIFY(playlists.create(" \n ").isEmpty());
        QCOMPARE(playlists.create("Road Trip"), QString("road-trip"));
        QCOMPARE(playlists.create("Road Trip"), QString("road-trip-2"));
        QCOMPARE(playlists.create("Café / Ñu!"), QString("cafe-nu"));
        QCOMPARE(playlists.create("???"), QString("playlist"));
        const QString smart = playlists.createSmart("Heavy rotation", {{"rules", QVariantList{QVariantMap{
                                                                           {"field", "plays"}, {"op", "greater"}, {"value", "3"}}}}});
        QCOMPARE(smart, QString("heavy-rotation"));
        QVERIFY(QFileInfo::exists(lists.filePath("heavy-rotation.cymtx-smart")));
        QCOMPARE(playlists.add(smart, {"/x.mp3"}), 0); // smart playlists fill themselves
        QVERIFY(!playlists.setSpec("road-trip", {}));
        QVERIFY(playlists.setSpec(smart, {{"matchAll", false}, {"rules", QVariantList{}}}));
        QCOMPARE(playlists.find(smart)->name, QString("Heavy rotation")); // the name stays when not given
        QCOMPARE(playlists.count(), 5);

        // Delete to the trash, then back with the same id.
        QVERIFY(playlists.remove("road-trip"));
        QVERIFY(!QFileInfo::exists(lists.filePath("road-trip.m3u8")));
        QVERIFY(QFileInfo::exists(lists.filePath(".trash/road-trip.m3u8")));
        QVERIFY(playlists.canRestore());
        QCOMPARE(playlists.restoreLast(), QString("road-trip"));
        QVERIFY(playlists.find("road-trip"));
        // The id was taken again meanwhile: the restored one gets a new id.
        QVERIFY(playlists.remove("road-trip"));
        QCOMPARE(playlists.create("Road Trip"), QString("road-trip"));
        QCOMPARE(playlists.restoreLast(), QString("road-trip-3"));
        QCOMPARE(playlists.find("road-trip-3")->name, QString("Road Trip"));
        QVERIFY(playlists.restoreLast().isEmpty());
        // A smart playlist survives the trash as smart.
        QVERIFY(playlists.remove(smart));
        QCOMPARE(playlists.restoreLast(), smart);
        QVERIFY(playlists.find(smart)->smart);

        // Duplicates are independent copies.
        const QString copy = playlists.duplicate("road-trip");
        QVERIFY(!copy.isEmpty() && copy != "road-trip");
        QVERIFY(playlists.find(copy)->name.contains("Road Trip"));
    }

    void playlistFolderIgnoresLinksAndHugeFiles() {
        QTemporaryDir lists, outside;
        QFile real(outside.filePath("real.m3u8"));
        QVERIFY(real.open(QIODevice::WriteOnly));
        real.write("#EXTM3U\n/a.mp3\n");
        real.close();
        QVERIFY(QFile::link(real.fileName(), lists.filePath("link.m3u8")));
        QFile big(lists.filePath("big.m3u8"));
        QVERIFY(big.open(QIODevice::WriteOnly));
        QVERIFY(big.resize(Playlists::MaxFileBytes + 1));
        big.close();
        QFile ok(lists.filePath("Fine_Name.m3u"));
        QVERIFY(ok.open(QIODevice::WriteOnly));
        ok.write("#EXTM3U\n/a.mp3\nb.mp3\n");
        ok.close();
        Playlists playlists(nullptr, nullptr, lists.path());
        QCOMPARE(playlists.count(), 1);
        const Playlist *p = playlists.find("Fine_Name");
        QVERIFY(p);
        QCOMPARE(p->name, QString("Fine Name"));
        const QStringList paths = p->paths; // find() points into a list that create() can move
        QCOMPARE(paths, (QStringList{"/a.mp3", lists.filePath("b.mp3")}));

        // Export writes a readable file; import makes a new playlist of it.
        const QString out = outside.filePath("out.m3u8");
        QVERIFY(playlists.exportFile("Fine_Name", out));
        const QString imported = playlists.importFile(out);
        QCOMPARE(imported, QString("fine-name"));
        QCOMPARE(playlists.paths(imported), paths);
        QVERIFY(playlists.importFile(outside.filePath("missing.m3u8")).isEmpty());
    }

    void playlistSuggestions() {
        QTemporaryDir music, cache, state, lists;
        Library library({music.path()}, cache.path(), state.path());
        QStringList files;
        scanTones(music.path(), library, 3, files);
        if (QTest::currentTestFailed()) return;
        Playlists playlists(&library, nullptr, lists.path());
        const QString empty = playlists.create("Empty");
        QVERIFY(playlists.suggest(empty).isEmpty());
        QVERIFY(playlists.suggest("nope").isEmpty());
        const QString id = playlists.create("One", {files[0]});
        QVERIFY(playlists.suggest(id, 0).isEmpty());
        const QStringList s = playlists.suggest(id, 10);
        QVERIFY(!s.contains(files[0]));
        QCOMPARE(QSet<QString>(s.begin(), s.end()), (QSet<QString>{files[1], files[2]}));
        QCOMPARE(playlists.previewCount({}), 3);
    }

    void remotePlaylistCommands() {
        QTemporaryDir music, cache, state, lists;
        Library library({music.path()}, cache.path(), state.path());
        QStringList files;
        scanTones(music.path(), library, 3, files);
        if (QTest::currentTestFailed()) return;
        Analyzer analyzer(cache.filePath("analysis.json"));
        analyzer.setBackground(false);
        Player player(&library, &analyzer, state.path());
        Playlists playlists(&library, &analyzer, lists.path());
        Remote remote(&player);
        auto call = [&](QStringList args) {
            QJsonArray a;
            for (const QString &s : args)
                a.append(s);
            return remote.handle({{"cmd", "playlist"}, {"args", a}});
        };
        QVERIFY(!call({"list"}).value("ok").toBool()); // no playlists wired yet
        remote.setPlaylists(&playlists);
        QCOMPARE(call({"list"}).value("playlists").toArray().size(), 0);
        QJsonObject r = call({"new", "Gym", files[0]});
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("playlist").toObject().value("count").toInt(), 1);
        r = call({"add", "gym", files[0], files[1]});
        QCOMPARE(r.value("added").toInt(), 1);
        QCOMPARE(r.value("skipped").toInt(), 1);
        r = call({"show", "Gym"});
        QCOMPARE(r.value("songs").toArray().size(), 2);
        QCOMPARE(r.value("songs").toArray().at(1).toObject().value("path").toString(), files[1]);
        QVERIFY(!call({"show", "nothing"}).value("ok").toBool());
        QVERIFY(!call({"new", "  "}).value("ok").toBool());
        QVERIFY(!call({"smart", "Bad", "plays sideways 3"}).value("ok").toBool());
        QVERIFY(!call({"smart", "Bad", "plays"}).value("ok").toBool());
        r = call({"smart", "Long ones", "length greater 0.1"});
        QVERIFY(r.value("ok").toBool());
        QCOMPARE(r.value("playlist").toObject().value("count").toInt(), 3);
        QVERIFY(!call({"add", "Long ones", files[2]}).value("ok").toBool());
        QVERIFY(call({"rename", "Gym", "Leg day"}).value("ok").toBool());
        QVERIFY(call({"play", "Leg day"}).value("ok").toBool());
        QCOMPARE(player.queuePaths(), (QStringList{files[0], files[1]}));
        r = call({"save", "Queue copy"});
        QCOMPARE(r.value("playlist").toObject().value("count").toInt(), 2);
        QVERIFY(call({"delete", "Leg day"}).value("ok").toBool());
        QCOMPARE(call({"list"}).value("playlists").toArray().size(), 2);
        QVERIFY(call({"restore"}).value("ok").toBool());
        QCOMPARE(call({"show", "Leg day"}).value("songs").toArray().size(), 2);
        QVERIFY(!call({"frobnicate", "Leg day"}).value("ok").toBool());
        player.flush();
    }

};

QTEST_MAIN(CymtXTests)
#include "cymtx_tests.moc"
