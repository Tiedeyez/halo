#include "audiooutput.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QSpan>
#include <algorithm>
#include <cmath>

#include "engine.h"
#include "stream.h"

AudioOutput::AudioOutput(Engine *engine, QObject *parent) : QObject(parent), m_engine(engine) {
    m_scratch.resize(8192 * 2);
    connect(&m_devices, &QMediaDevices::audioOutputsChanged, this, &AudioOutput::start);
}

AudioOutput::~AudioOutput() {
    if (m_sink)
        m_sink->stop();
}

void AudioOutput::setProblem(const QString &problem) {
    if (problem == m_problem)
        return;
    m_problem = problem;
    emit problemChanged();
}

void AudioOutput::start() {
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        setProblem(tr("No audio output found. Connect speakers or headphones."));
        return;
    }

    // The engine renders 48 kHz stereo floats; take 16-bit when that is all
    // the device offers, and convert.
    QAudioFormat format;
    format.setSampleRate(Stream::SampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(format))
        format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        setProblem(tr("%1 can't play 48 kHz stereo.").arg(device.description()));
        return;
    }

    m_sink = std::make_unique<QAudioSink>(device, format);
    m_sink->setBufferFrameCount(PeriodFrames * 2);
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State) {
        if (m_sink && m_sink->error() == QtAudio::OpenError)
            setProblem(tr("The audio output couldn't be opened."));
        else if (m_sink && m_sink->error() == QtAudio::FatalError)
            setProblem(tr("The audio output stopped working."));
    });

    Engine *engine = m_engine;
    if (format.sampleFormat() == QAudioFormat::Float) {
        m_sink->start([engine](QSpan<float> buffer) {
            engine->render(buffer.data(), int(buffer.size() / 2));
        });
    } else {
        float *scratch = m_scratch.data();
        const qsizetype capacity = qsizetype(m_scratch.size() / 2);
        // TPDF dither (two uniform values, one least significant bit each)
        // before rounding to 16 bits, so quiet passages fade into steady
        // hiss instead of the distortion plain rounding leaves.
        m_sink->start([engine, scratch, capacity, seed = quint32(0x9e3779b9)](QSpan<qint16> buffer) mutable {
            auto uniform = [&seed] {
                seed ^= seed << 13;
                seed ^= seed >> 17;
                seed ^= seed << 5;
                return float(seed) * (1.0f / 4294967296.0f);
            };
            qsizetype done = 0;
            const qsizetype frames = buffer.size() / 2;
            while (done < frames) {
                const qsizetype block = std::min(capacity, frames - done);
                engine->render(scratch, int(block));
                for (qsizetype i = 0; i < block * 2; ++i) {
                    const float dither = uniform() - uniform();
                    const float v = std::clamp(scratch[i] * 32767.0f + dither, -32768.0f, 32767.0f);
                    buffer[done * 2 + i] = qint16(std::lround(v));
                }
                done += block;
            }
        });
    }
    if (m_sink->error() != QtAudio::NoError)
        setProblem(tr("The audio output couldn't be opened."));
    else
        setProblem({});
}
