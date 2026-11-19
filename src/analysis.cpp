#include "analysis.h"
#include "safety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

#include "dsp.h"

QJsonObject TrackAnalysis::toJson() const {
    return {{"bpm", std::round(bpm * 10) / 10},
            {"lufs", std::round(loudness * 10) / 10},
            {"tp", hasTruePeak() ? QJsonValue(std::round(truePeak * 10) / 10) : QJsonValue()},
            {"start", std::round(audibleStart * 100) / 100},
            {"end", std::round(audibleEnd * 100) / 100},
            {"duration", std::round(duration * 100) / 100}};
}

TrackAnalysis TrackAnalysis::fromJson(const QJsonObject &o) {
    TrackAnalysis a;
    a.bpm = o.value("bpm").toDouble();
    a.loudness = o.value("lufs").toDouble(-14);
    a.truePeak = o.value("tp").isDouble() ? o.value("tp").toDouble() : NAN;
    a.audibleStart = o.value("start").toDouble();
    a.audibleEnd = o.value("end").toDouble();
    a.duration = o.value("duration").toDouble();
    a.valid = true;
    return a;
}

namespace Analysis {

// How far the beat's autocorrelation peak must rise above the average over
// all tempos; set from real tracks (see tests and the research notes).
constexpr double PeakOverMean = 1.25;
// Mean spectral flux per bin below which nothing is struck at all (a pure
// tone measures about 0.01, mixtape hip-hop about 0.3).
constexpr double MinimumFlux = 0.04;

// ITU-R BS.1770 loudness: K-weighting (a high shelf, then a high-pass), mean
// square over 400 ms blocks every 100 ms, an absolute gate at -70 LUFS and a
// relative gate 10 LU under the mean. Halo analyses ffmpeg's mono downmix,
// which keeps the power of a centred mix (it sums at -3 dB per channel), and
// at 22 kHz it misses the top octave; checked against ffmpeg's ebur128 meter
// on real tracks, that leaves it about 0.5 LU low, which is added back.
double loudness(const float *mono, size_t count, int rate) {
    const auto shelf = Dsp::highShelf(rate, 1681.974450955533, 0.7071752369554196, 3.999843853973347);
    const auto high = Dsp::highPass(rate, 38.13547087602444, 0.5003270373238773);
    Dsp::Biquad s, h;
    const size_t step = size_t(rate) / 10;
    if (count < step * 4)
        return -70;
    std::vector<double> steps;
    steps.reserve(count / step + 1);
    double sum = 0;
    size_t inStep = 0;
    for (size_t i = 0; i < count; ++i) {
        const float k = h.process(high, s.process(shelf, mono[i]));
        sum += double(k) * k;
        if (++inStep == step) {
            steps.push_back(sum / double(step));
            sum = 0;
            inStep = 0;
        }
    }
    std::vector<double> blocks;
    for (size_t i = 0; i + 4 <= steps.size(); ++i)
        blocks.push_back((steps[i] + steps[i + 1] + steps[i + 2] + steps[i + 3]) / 4);
    auto lufs = [](double ms) { return -0.691 + 10 * std::log10(std::max(ms, 1e-12)) + 0.5; };
    double total = 0;
    int passed = 0;
    for (double b : blocks)
        if (lufs(b) > -70) {
            total += b;
            ++passed;
        }
    if (!passed)
        return -70;
    const double gate = lufs(total / passed) - 10;
    total = 0;
    passed = 0;
    for (double b : blocks)
        if (lufs(b) > -70 && lufs(b) > gate) {
            total += b;
            ++passed;
        }
    return passed ? lufs(total / passed) : -70;
}

// Where the music starts and stops: the first and last 50 ms window louder
// than -48 dBFS, so a long silent lead-in or a hidden track after a gap is
// found.
void audibleEdges(const float *mono, size_t count, int rate, double &start, double &end) {
    const size_t window = size_t(rate) / 20;
    const double threshold = std::pow(10.0, -48.0 / 10.0);
    long first = -1, last = -1;
    const size_t windows = count / window;
    for (size_t w = 0; w < windows; ++w) {
        double ms = 0;
        for (size_t i = 0; i < window; ++i) {
            const double v = mono[w * window + i];
            ms += v * v;
        }
        if (ms / double(window) > threshold) {
            if (first < 0)
                first = long(w);
            last = long(w);
        }
    }
    start = first < 0 ? 0 : first * 0.05;
    end = last < 0 ? double(count) / rate : (last + 1) * 0.05;
}

// Tempo from the spectral flux onset envelope: the autocorrelation of the
// envelope peaks at the beat period, weighted toward 120 BPM so a track isn't
// read at half or double time without reason (the prior librosa uses).
double estimateBpm(const float *mono, size_t count, int rate) {
    constexpr size_t Frame = 1024, Hop = 256;
    if (count < Frame * 8)
        return 0;
    // Two minutes from the middle is plenty and keeps long mixes quick.
    size_t begin = 0, length = count;
    const size_t limit = size_t(rate) * 120;
    if (count > limit) {
        begin = (count - limit) / 2;
        length = limit;
    }
    std::vector<float> window(Frame);
    for (size_t i = 0; i < Frame; ++i)
        window[i] = float(0.5 - 0.5 * std::cos(2 * Dsp::Pi * double(i) / double(Frame)));

    std::vector<std::complex<float>> buffer(Frame);
    std::vector<float> previous(Frame / 2, 0.0f), envelope;
    envelope.reserve(length / Hop + 1);
    for (size_t pos = begin; pos + Frame <= begin + length; pos += Hop) {
        for (size_t i = 0; i < Frame; ++i)
            buffer[i] = {mono[pos + i] * window[i], 0};
        Dsp::fft(buffer);
        float flux = 0;
        for (size_t k = 1; k < Frame / 2; ++k) {
            const float magnitude = std::log1p(1000.0f * std::abs(buffer[k]));
            flux += std::max(0.0f, magnitude - previous[k]);
            previous[k] = magnitude;
        }
        envelope.push_back(flux);
    }
    if (envelope.size() < 64)
        return 0;
    envelope[0] = 0;

    // Remove the slow trend so only the pulses remain.
    const double fps = double(rate) / Hop;
    const size_t half = size_t(fps / 4);
    std::vector<double> onset(envelope.size());
    double running = 0;
    std::vector<double> prefix(envelope.size() + 1, 0);
    for (size_t i = 0; i < envelope.size(); ++i)
        prefix[i + 1] = prefix[i] + envelope[i];
    for (size_t i = 0; i < envelope.size(); ++i) {
        const size_t a = i > half ? i - half : 0, b = std::min(envelope.size(), i + half + 1);
        running = (prefix[b] - prefix[a]) / double(b - a);
        onset[i] = std::max(0.0, envelope[i] - running);
    }

    const int minLag = int(std::floor(60.0 * fps / 200.0));
    const int maxLag = int(std::ceil(60.0 * fps / 60.0));
    if (size_t(maxLag * 2) >= onset.size())
        return 0;
    std::vector<double> ac(size_t(maxLag) * 2 + 2, 0);
    for (int lag = 0; lag < int(ac.size()); ++lag) {
        double s = 0;
        for (size_t i = size_t(lag); i < onset.size(); ++i)
            s += onset[i] * onset[i - size_t(lag)];
        ac[size_t(lag)] = s / double(onset.size() - size_t(lag));
    }
    if (ac[0] <= 0)
        return 0;

    int best = -1;
    double bestScore = 0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        const double bpm = 60.0 * fps / lag;
        const double octave = std::log2(bpm / 120.0);
        const double prior = std::exp(-0.5 * octave * octave / (0.9 * 0.9));
        const double score = prior * (ac[size_t(lag)] + 0.5 * ac[size_t(lag) * 2]);
        if (score > bestScore) {
            bestScore = score;
            best = lag;
        }
    }
    // A beat has to stand out from the rest of the autocorrelation: a steady
    // tone, speech or ambient drone gives a flat curve with no clear period.
    double mean = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
        mean += ac[size_t(lag)];
    mean /= double(maxLag - minLag + 1);
    double flux = 0;
    for (float e : envelope)
        flux += e;
    flux /= double(envelope.size()) * (Frame / 2);
    if (qEnvironmentVariableIsSet("HALO_DEBUG_TEMPO"))
        qInfo("tempo: flux %.5f, zero-lag %.3f, peak/mean %.3f", flux, best >= 0 ? ac[size_t(best)] / ac[0] : 0.0,
              best >= 0 && mean > 0 ? ac[size_t(best)] / mean : 0.0);
    if (best < 0 || flux < MinimumFlux || ac[size_t(best)] / ac[0] < 0.08 || mean <= 0 ||
        ac[size_t(best)] / mean < PeakOverMean)
        return 0;
    double lag = best;
    if (best > minLag && best < maxLag) {
        const double a = ac[size_t(best) - 1], b = ac[size_t(best)], c = ac[size_t(best) + 1];
        const double d = a - 2 * b + c;
        if (d < 0)
            lag += 0.5 * (a - c) / d;
    }
    return std::round(600.0 * fps / lag) / 10.0;
}

TrackAnalysis analyze(const float *mono, size_t count, int rate) {
    TrackAnalysis a;
    a.duration = double(count) / rate;
    audibleEdges(mono, count, rate, a.audibleStart, a.audibleEnd);
    a.loudness = loudness(mono, count, rate);
    const size_t from = std::min(count, size_t(a.audibleStart * rate));
    a.bpm = estimateBpm(mono + from, count - from, rate);
    a.valid = true;
    return a;
}

// Eight minutes is enough to know a song; a long DJ mix is measured on its
// first eight and its end is taken from its length.
// Two branches: ebur128 measures loudness and true peak (4x oversampled) on
// the song as it is, and a 22 kHz mono copy feeds the tempo and edge maths.
// -v info is what lets ebur128 print its summary; -nostats keeps the
// progress line out.
QStringList ffmpegArguments(const QString &path) {
    // -protocol_whitelist file: a "song" that is really a playlist (HLS,
    // concat) can't make ffmpeg fetch anything from the network.
    return {"-nostdin", "-hide_banner", "-nostats", "-v", "info", "-protocol_whitelist", "file", "-i", path, "-t", "480",
            "-filter_complex",
            QString("[0:a:0]asplit=2[m][a];[m]ebur128=peak=true:framelog=quiet,anullsink;"
                    "[a]aresample=%1,aformat=sample_fmts=flt:channel_layouts=mono[out]").arg(Rate),
            "-map", "[out]", "-f", "f32le", "pipe:1"};
}

bool parseEbur128(const QByteArray &log, double &lufs, double &truePeak) {
    // The summary is the last block ebur128 prints:
    //   Integrated loudness:\n    I:  -11.2 LUFS ... True peak:\n    Peak:  0.3 dBFS
    const int summary = log.lastIndexOf("Summary:");
    if (summary < 0)
        return false;
    const QByteArray tail = log.mid(summary);
    auto number = [&tail](const QByteArray &section, const QByteArray &key, double &out) {
        const int s = tail.indexOf(section);
        const int k = s < 0 ? -1 : tail.indexOf(key, s);
        if (k < 0)
            return false;
        int end = tail.indexOf('\n', k);
        if (end < 0)
            end = tail.size();
        const QList<QByteArray> words = tail.mid(k + key.size(), end - k - key.size()).simplified().split(' ');
        bool ok = false;
        out = words.value(0).toDouble(&ok);
        return ok && std::isfinite(out);
    };
    double i = 0, tp = 0;
    if (!number("Integrated loudness:", "I:", i) || !number("True peak:", "Peak:", tp))
        return false;
    lufs = i;
    truePeak = tp;
    return true;
}

TrackAnalysis analyzeFile(const QString &path, QString *error) {
    const QString ffmpeg = Safety::systemTool("ffmpeg");
    if (ffmpeg.isEmpty()) {
        if (error)
            *error = "ffmpeg isn't installed";
        return {};
    }
    QProcess process;
    process.start(ffmpeg, ffmpegArguments(path));
    if (!process.waitForFinished(5 * 60 * 1000) || process.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error && error->isEmpty())
            *error = "ffmpeg couldn't read the file";
        return {};
    }
    const QByteArray bytes = process.readAllStandardOutput();
    const QByteArray log = process.readAllStandardError();
    const auto *samples = reinterpret_cast<const float *>(bytes.constData());
    const size_t count = size_t(bytes.size()) / sizeof(float);
    if (count == 0) {
        if (error)
            *error = "no audio in the file";
        return {};
    }
    TrackAnalysis a = analyze(samples, count);
    parseEbur128(log, a.loudness, a.truePeak);
    return a;
}

}

Analyzer::Analyzer(const QString &cacheFile, QObject *parent)
    : QObject(parent),
      m_cacheFile(cacheFile.isEmpty()
                      ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/halo/analysis.json"
                      : cacheFile) {
    m_save.setSingleShot(true);
    m_save.setInterval(3000);
    connect(&m_save, &QTimer::timeout, this, &Analyzer::flush);
    connect(&m_process, &QProcess::readyReadStandardOutput, this,
            [this] { m_buffer.append(m_process.readAllStandardOutput()); });
    connect(&m_process, &QProcess::finished, this, &Analyzer::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finished();
    });
    load();
}

Analyzer::~Analyzer() {
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    flush();
}

void Analyzer::load() {
    QFile file(m_cacheFile);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject tracks = QJsonDocument::fromJson(file.readAll()).object().value("tracks").toObject();
    for (auto it = tracks.begin(); it != tracks.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        m_results.insert(it.key(), {qint64(o.value("mtime").toDouble()), TrackAnalysis::fromJson(o)});
    }
}

void Analyzer::flush() {
    if (!m_save.isActive() && m_results.isEmpty())
        return;
    m_save.stop();
    QJsonObject tracks;
    for (auto it = m_results.begin(); it != m_results.end(); ++it) {
        QJsonObject o = it->analysis.toJson();
        o.insert("mtime", double(it->mtime));
        tracks.insert(it.key(), o);
    }
    QDir().mkpath(QFileInfo(m_cacheFile).absolutePath());
    QSaveFile file(m_cacheFile);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"tracks", tracks}}).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void Analyzer::scheduleSave() {
    if (!m_save.isActive())
        m_save.start();
}

bool Analyzer::lookup(const QString &path, TrackAnalysis &out) const {
    const auto it = m_results.constFind(path);
    if (it == m_results.constEnd())
        return false;
    if (it->mtime != QFileInfo(path).lastModified().toMSecsSinceEpoch())
        return false;
    out = it->analysis;
    return true;
}

bool Analyzer::needsWork(const QString &path) const {
    TrackAnalysis a;
    return !lookup(path, a) || !a.hasTruePeak();
}

void Analyzer::prioritize(const QString &path) {
    if (path.isEmpty() || path == m_running || !needsWork(path))
        return;
    m_urgent.removeAll(path);
    m_urgent.prepend(path);
    next();
}

void Analyzer::enqueue(const QStringList &paths) {
    for (const QString &path : paths) {
        // Songs analysed before true peak was measured go round once more.
        if (needsWork(path) && !m_queue.contains(path))
            m_queue.append(path);
    }
    emit progress();
    next();
}

void Analyzer::setBackground(bool enabled) {
    m_background = enabled;
    next();
}

void Analyzer::next() {
    if (m_process.state() != QProcess::NotRunning || !m_running.isEmpty())
        return;
    QString path;
    while (!m_urgent.isEmpty() && path.isEmpty()) {
        path = m_urgent.takeFirst();
        if (!needsWork(path))
            path.clear();
    }
    while (path.isEmpty() && m_background && !m_queue.isEmpty()) {
        path = m_queue.takeFirst();
        if (!needsWork(path) || !QFileInfo::exists(path))
            path.clear();
    }
    if (path.isEmpty())
        return;
    const QString ffmpeg = Safety::systemTool("ffmpeg");
    if (ffmpeg.isEmpty())
        return;
    m_running = path;
    m_buffer.clear();
    // The lowest CPU priority, so analysis never takes time from playback or
    // from anything else on the machine.
    const QString nice = Safety::systemTool("nice");
    if (nice.isEmpty())
        m_process.start(ffmpeg, Analysis::ffmpegArguments(path));
    else
        m_process.start(nice, QStringList{"-n", "19", ffmpeg} + Analysis::ffmpegArguments(path));
}

void Analyzer::finished() {
    const QString path = m_running;
    m_buffer.append(m_process.readAllStandardOutput());
    const QByteArray bytes = std::exchange(m_buffer, {});
    const QByteArray log = m_process.readAllStandardError();
    if (path.isEmpty()) {
        QTimer::singleShot(0, this, &Analyzer::next);
        return;
    }
    // The tempo and loudness maths runs over the whole song, up to a second
    // on a laptop; on the GUI thread that froze the window once per song
    // through a library sweep. It runs on a pool thread at low priority, and
    // m_running stays set until it's done so only one song is in flight.
    auto *watcher = new QFutureWatcher<TrackAnalysis>(this);
    connect(watcher, &QFutureWatcher<TrackAnalysis>::finished, this, [this, watcher, path] {
        watcher->deleteLater();
        TrackAnalysis analysis = watcher->result();
        // A file ffmpeg can't read is remembered too, so it isn't retried
        // on every pass; its analysis is marked as having no beat.
        analysis.valid = true;
        m_results.insert(path, {QFileInfo(path).lastModified().toMSecsSinceEpoch(), analysis});
        m_running.clear();
        scheduleSave();
        emit analyzed(path);
        emit progress();
        next();
    });
    watcher->setFuture(QtConcurrent::run([bytes, log] {
        QThread::currentThread()->setPriority(QThread::LowestPriority);
        const size_t count = size_t(bytes.size()) / sizeof(float);
        TrackAnalysis analysis;
        if (count > 0) {
            analysis = Analysis::analyze(reinterpret_cast<const float *>(bytes.constData()), count);
            // ebur128 replaces the estimate when it reported; a song with no
            // finite peak (silence) keeps the estimate and is marked measured.
            if (!Analysis::parseEbur128(log, analysis.loudness, analysis.truePeak))
                analysis.truePeak = -144;
        }
        return analysis;
    }));
}
