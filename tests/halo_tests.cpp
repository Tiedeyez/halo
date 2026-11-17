#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <cmath>
#include <vector>

#include "analysis.h"
#include "engine.h"
#include "library.h"
#include "player.h"
#include "remote.h"
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
void waitDecoded(Stream &s) {
    QElapsedTimer t;
    t.start();
    while (!s.decoded() && t.elapsed() < 5000)
        QThread::msleep(5);
}

}

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
        waitDecoded(*sa);
        waitDecoded(*sb);
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
    }

    void themeAccentFallsBack() {
        QTemporaryDir dir;
        QCOMPARE(Theme::readAccent(dir.filePath("missing.toml")), QString("#FFD60A"));
        QFile f(dir.filePath("colors.toml"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("accent = \"#89b4fa\"\n");
        f.close();
        QCOMPARE(Theme::readAccent(dir.filePath("colors.toml")), QString("#89b4fa"));
    }
};

QTEST_MAIN(HaloTests)
#include "halo_tests.moc"
