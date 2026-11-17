#pragma once

#include <QAudioFormat>
#include <QMediaDevices>
#include <QObject>
#include <memory>
#include <vector>

class Engine;
class QAudioSink;

// Feeds the engine to the default audio output. Qt calls back on its own
// realtime audio thread with a buffer to fill, and the engine renders straight
// into it: nothing on the GUI thread can delay a block. Follows the default
// output when it changes (headphones in or out) and reports a missing or
// failed device instead of going quiet.
class AudioOutput : public QObject {
    Q_OBJECT

public:
    explicit AudioOutput(Engine *engine, QObject *parent = nullptr);
    ~AudioOutput() override;

    QString problem() const { return m_problem; }
    void start();

    // The frames per callback PipeWire is asked for, about 21 ms: a player
    // needs no drum-pad latency, and longer periods save battery.
    static constexpr int PeriodFrames = 1024;

signals:
    void problemChanged();
    // The output now playing, by its name ("WH-1000XM4", "Built-in Audio
    // Analog Stereo"); for the headphone check.
    void deviceChanged(const QString &name);

private:
    void setProblem(const QString &problem);

    Engine *m_engine;
    QMediaDevices m_devices;
    std::unique_ptr<QAudioSink> m_sink;
    // Scratch for 16-bit devices, sized once so the callback never allocates.
    std::vector<float> m_scratch;
    QString m_problem;
};
