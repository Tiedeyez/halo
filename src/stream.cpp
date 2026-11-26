#include "stream.h"
#include "safety.h"

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace {
// Six seconds of decoded audio: enough to ride out a busy disk or a slow
// network share, small enough not to matter on an 8 GB laptop.
constexpr size_t RingFloats = size_t(Stream::SampleRate) * Stream::Channels * 6;
}

Stream::Stream(const QString &path, double startSeconds, const QString &ffmpeg)
    : m_path(path), m_start(std::max(0.0, startSeconds)),
      m_ffmpeg(ffmpeg.isEmpty() ? Safety::systemTool("ffmpeg") : ffmpeg), m_ring(RingFloats) {}

Stream::~Stream() {
    m_stop = true;
    if (const int pid = m_pid.load())
        ::kill(pid, SIGKILL);
    if (m_thread.joinable())
        m_thread.join();
}

void Stream::start() {
    if (m_ffmpeg.isEmpty()) {
        fail(QCoreApplication::translate("Stream", "ffmpeg isn't installed, so CymtX can't decode audio."));
        return;
    }
    m_thread = std::thread([this] { run(); });
}

int Stream::read(float *out, int frames) {
    const size_t got = m_ring.read(out, size_t(frames) * Channels) / Channels;
    m_consumed.fetch_add(static_cast<long long>(got), std::memory_order_relaxed);
    return int(got);
}

bool Stream::ready() const {
    return m_eof.load() || m_failed.load() || m_ring.readable() >= size_t(SampleRate) * Channels / 2;
}

QString Stream::error() const {
    std::lock_guard lock(m_errorLock);
    return m_error;
}

void Stream::fail(const QString &message) {
    {
        std::lock_guard lock(m_errorLock);
        m_error = message;
    }
    m_failed = true;
    m_eof = true;
}

void Stream::run() {
    int out[2], err[2];
    if (::pipe2(out, O_CLOEXEC) != 0 || ::pipe2(err, O_CLOEXEC) != 0) {
        fail(QString::fromLocal8Bit(std::strerror(errno)));
        return;
    }

    const QByteArray program = QFile::encodeName(m_ffmpeg);
    const QByteArray file = QFile::encodeName(m_path);
    const QByteArray start = QByteArray::number(m_start, 'f', 3);
    const QByteArray rate = QByteArray::number(SampleRate);
    // -ss before -i seeks by the container index, which for audio is exact on
    // current ffmpeg and starts instantly even deep into a long mix.
    // Songs that aren't 48 kHz (most are 44.1) go through soxr at precision
    // 28. ffmpeg's default resampler left an alias 45 dB under a 19 kHz tone
    // and dropped 0.5 dB at 20 kHz; soxr puts the alias at -159 dB and stays
    // flat (tools/audio-lab/resample.py). Arch's ffmpeg is built with soxr.
    const QByteArray resample = "aresample=" + rate + ":resampler=soxr:precision=28";
    std::vector<const char *> argv{program.constData(), "-nostdin", "-hide_banner", "-v", "error",
                                   // A "song" that is really a playlist (HLS, concat) can't
                                   // make ffmpeg reach the network: local files only.
                                   "-protocol_whitelist", "file",
                                   "-ss", start.constData(), "-i", file.constData(),
                                   "-map", "0:a:0", "-vn", "-af", resample.constData(),
                                   "-f", "f32le", "-ac", "2", "-ar", rate.constData(),
                                   "pipe:1", nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err[1], STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, program.constData(), &actions, nullptr,
                                    const_cast<char *const *>(argv.data()), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(out[1]);
    ::close(err[1]);
    if (spawned != 0) {
        ::close(out[0]);
        ::close(err[0]);
        fail(QString::fromLocal8Bit(std::strerror(spawned)));
        return;
    }
    m_pid = pid;
    if (m_stop)
        ::kill(pid, SIGKILL);

    std::vector<char> bytes(64 * 1024);
    std::vector<float> floats(bytes.size() / sizeof(float) + 1);
    size_t pending = 0; // bytes of a float split across two reads
    QByteArray errors;
    long long frames = 0;
    bool outOpen = true, errOpen = true;

    while (outOpen && !m_stop) {
        // Only read when there is room, so the pipe and ffmpeg wait for us.
        const size_t room = m_ring.writable() * sizeof(float);
        if (room < 4096) {
            ::usleep(5000);
            continue;
        }
        pollfd fds[2] = {{out[0], POLLIN, 0}, {err[0], short(errOpen ? POLLIN : 0), 0}};
        if (::poll(fds, 2, 100) <= 0)
            continue;
        if (errOpen && (fds[1].revents & (POLLIN | POLLHUP))) {
            char buffer[1024];
            const ssize_t n = ::read(err[0], buffer, sizeof buffer);
            if (n > 0 && errors.size() < 8192)
                errors.append(buffer, int(n));
            else if (n <= 0)
                errOpen = false;
        }
        if (fds[0].revents & (POLLIN | POLLHUP)) {
            const size_t want = std::min(room, bytes.size()) - pending;
            const ssize_t n = ::read(out[0], bytes.data() + pending, (want / sizeof(float)) * sizeof(float));
            if (n <= 0) {
                outOpen = false;
                break;
            }
            const size_t total = pending + size_t(n);
            const size_t count = total / sizeof(float);
            std::memcpy(floats.data(), bytes.data(), count * sizeof(float));
            pending = total - count * sizeof(float);
            if (pending)
                std::memmove(bytes.data(), bytes.data() + count * sizeof(float), pending);
            m_ring.write(floats.data(), count);
            frames += static_cast<long long>(count / Channels);
        }
    }

    // Closing our end of the pipe first means an ffmpeg blocked writing to a
    // full pipe gets EPIPE instead of waiting forever; a stop is a kill,
    // since ffmpeg would otherwise try to flush what nobody will read.
    ::close(out[0]);
    if (m_stop)
        ::kill(pid, SIGKILL);
    // Drain what is left of stderr for the error message.
    while (errOpen) {
        char buffer[1024];
        const ssize_t n = ::read(err[0], buffer, sizeof buffer);
        if (n <= 0)
            break;
        if (errors.size() < 8192)
            errors.append(buffer, int(n));
    }
    ::close(err[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    m_pid = 0;

    if (!m_stop && frames == 0 && !(WIFEXITED(status) && WEXITSTATUS(status) == 0 && m_start > 0)) {
        const QString detail = QString::fromUtf8(errors).trimmed().section('\n', -1);
        fail(detail.isEmpty() ? QCoreApplication::translate("Stream", "ffmpeg couldn't read this file.") : detail);
        return;
    }
    m_eof = true;
}
