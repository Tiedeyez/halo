#pragma once

#include <QString>
#include <atomic>
#include <mutex>
#include <thread>

#include "ring.h"

// One track being decoded. An ffmpeg process turns the file into 48 kHz
// stereo floats on a pipe, and a thread of its own copies them into a ring
// only while the ring has room. The pipe fills and ffmpeg waits, so a
// one-hour mixtape is never decoded into memory ahead of time.
class Stream {
public:
    static constexpr int SampleRate = 48000;
    static constexpr int Channels = 2;

    Stream(const QString &path, double startSeconds, const QString &ffmpeg = {});
    ~Stream();

    Stream(const Stream &) = delete;
    Stream &operator=(const Stream &) = delete;

    void start();

    const QString &path() const { return m_path; }
    double startSeconds() const { return m_start; }

    // Audio thread: copies up to `frames` stereo frames, returns how many.
    int read(float *out, int frames);
    // Seconds into the track of the next frame the audio thread will read.
    double position() const { return m_start + double(m_consumed.load(std::memory_order_relaxed)) / SampleRate; }

    // Enough is decoded to start without a gap (or the track is that short).
    bool ready() const;
    // ffmpeg is done and every frame has been read.
    bool drained() const { return m_eof.load() && m_ring.readable() < Channels; }
    bool failed() const { return m_failed.load(); }
    // ffmpeg has delivered the whole file (it may not all be read yet).
    bool decoded() const { return m_eof.load(); }
    QString error() const;

private:
    void run();
    void fail(const QString &message);

    QString m_path;
    double m_start;
    QString m_ffmpeg;
    Ring m_ring;
    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_eof{false};
    std::atomic<bool> m_failed{false};
    std::atomic<long long> m_consumed{0};
    std::atomic<int> m_pid{0};
    mutable std::mutex m_errorLock;
    QString m_error;
};
