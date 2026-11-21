// Decks spike: how small a PipeWire quantum this machine holds, silently.
//
// It never makes a sound and never touches the user's audio:
//   - it creates its own null sink (support.null-audio-sink, a driver of its
//     own, so it gets its own graph and clock; the hardware or Bluetooth
//     graph the user is listening on is never re-clocked to a small quantum),
//   - connects a pw_stream to that sink only (target.object, no autoconnect
//     to the default), and writes zeros,
//   - removes the sink when it exits.
//
// For each quantum it reports: callback interval (mean, p99, max), how many
// callbacks came late (interval > 1.5 quanta = a would-be xrun), time spent
// in the callback, the real-time policy of the data thread, and PipeWire's
// own delay figures from pw_stream_get_time_n. An optional synthetic DSP
// load (busy-wait, a fraction of the period) shows the headroom left.
//
// Build: g++ -O2 -std=c++17 tools/decks-spikes/pw-latency.cpp -o /tmp/pw-latency \
//          $(pkg-config --cflags --libs libpipewire-0.3)
// Run:   /tmp/pw-latency [seconds-per-case=10]
//        SLICE_US=300 /tmp/pw-latency 10   (also asks EEVDF for a 0.3 ms slice)
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace {

// EEVDF (Linux 6.6+) lets any thread ask for a shorter time slice, with no
// privilege: SCHED_OTHER with sched_runtime set. A shorter slice means an
// earlier virtual deadline, so a waking audio thread preempts sooner. This
// is the one scheduling lever an unprivileged app has without rtkit.
struct SchedAttr {
    uint32_t size, policy;
    uint64_t flags;
    int32_t nice;
    uint32_t priority;
    uint64_t runtime, deadline, period;
};
uint64_t g_sliceNs = 0;
bool g_sliceOk = false;
int g_sliceErrno = 0;

double nowUs() {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

struct Case {
    int quantum;
    double load; // fraction of the period burnt in the callback
};

struct State {
    pw_main_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    int quantum = 128;
    double load = 0;
    // Written by the data thread only; read after the run.
    std::vector<double> intervals, busy;
    std::vector<int> sizes;
    double last = 0;
    int policy = -1, priority = 0;
    int64_t delayMax = 0, delaySum = 0, delayN = 0;
    int64_t queuedMax = 0, bufferedMax = 0;
    long callbacks = 0;
};

void onProcess(void *data) {
    auto *s = static_cast<State *>(data);
    const double t0 = nowUs();
    if (s->last > 0)
        s->intervals.push_back(t0 - s->last);
    s->last = t0;
    if (s->policy < 0) {
        s->policy = sched_getscheduler(0) & ~SCHED_RESET_ON_FORK;
        sched_param p{};
        sched_getparam(0, &p);
        s->priority = p.sched_priority;
        if (g_sliceNs) {
            SchedAttr a{};
            a.size = sizeof a;
            a.policy = SCHED_OTHER;
            a.runtime = g_sliceNs;
            // PipeWire marks its data thread reset-on-fork; an unprivileged
            // thread may not clear that flag, so keep it or get EPERM.
            a.flags = 0x01; // SCHED_FLAG_RESET_ON_FORK
            g_sliceOk = syscall(SYS_sched_setattr, 0, &a, 0) == 0;
            g_sliceErrno = g_sliceOk ? 0 : errno;
        }
    }

    pw_buffer *b = pw_stream_dequeue_buffer(s->stream);
    if (!b)
        return;
    spa_buffer *buf = b->buffer;
    auto *dst = static_cast<float *>(buf->datas[0].data);
    if (!dst)
        return;
    const uint32_t stride = sizeof(float) * 2;
    uint32_t frames = buf->datas[0].maxsize / stride;
    if (b->requested)
        frames = std::min<uint32_t>(frames, uint32_t(b->requested));
    // Silence: zeros, always.
    std::memset(dst, 0, size_t(frames) * stride);
    s->sizes.push_back(int(frames));

    // Synthetic DSP: spin for a fraction of the period.
    if (s->load > 0) {
        const double until = t0 + s->load * frames * 1e6 / 48000.0;
        volatile double x = 1;
        while (nowUs() < until)
            x = x * 1.0000001 + 1e-9;
    }

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = frames * stride;
    pw_stream_queue_buffer(s->stream, b);

    pw_time tm{};
    if (pw_stream_get_time_n(s->stream, &tm, sizeof tm) == 0 && tm.rate.denom) {
        // delay is in units of rate (1/48000 here): the graph's latency
        // downstream of us; queued/buffered are frames waiting in us.
        const int64_t d = tm.delay * 1000000 / tm.rate.denom * tm.rate.num;
        s->delayMax = std::max(s->delayMax, d);
        s->delaySum += d;
        ++s->delayN;
        s->queuedMax = std::max<int64_t>(s->queuedMax, int64_t(tm.queued));
        s->bufferedMax = std::max<int64_t>(s->bufferedMax, int64_t(tm.buffered));
    }
    s->busy.push_back(nowUs() - t0);
    ++s->callbacks;
}

const pw_stream_events streamEvents = [] {
    pw_stream_events e{};
    e.version = PW_VERSION_STREAM_EVENTS;
    e.process = onProcess;
    return e;
}();

double percentile(std::vector<double> v, double p) {
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(p * double(v.size() - 1)))];
}

void stopLater(void *data, uint64_t) { pw_main_loop_quit(static_cast<State *>(data)->loop); }

} // namespace

int main(int argc, char **argv) {
    const int seconds = argc > 1 ? std::max(2, std::atoi(argv[1])) : 10;
    if (const char *slice = std::getenv("SLICE_US"))
        g_sliceNs = uint64_t(std::atof(slice) * 1000);
    pw_init(&argc, &argv);

    const std::vector<Case> cases{{1024, 0}, {512, 0}, {256, 0}, {128, 0}, {64, 0}, {128, 0.25}, {256, 0.25}, {256, 0.5}, {512, 0.5}};

    std::printf("PipeWire %s, %d s per case, silent null sink of our own, slice request %s\n", pw_get_library_version(),
                seconds, g_sliceNs ? std::to_string(g_sliceNs / 1000).append(" us").c_str() : "none");
    std::printf("%-8s %-5s %8s %8s %8s %8s %6s %8s %8s %9s %7s %s\n", "quantum", "load", "period", "mean", "p99", "max",
                "late", "busyP99", "busyMax", "pwDelay", "frames", "sched");

    for (const Case &c : cases) {
        State s;
        s.quantum = c.quantum;
        s.load = c.load;
        s.intervals.reserve(size_t(seconds) * 48000 / 32 + 64);
        s.busy.reserve(s.intervals.capacity());
        s.sizes.reserve(s.intervals.capacity());
        s.loop = pw_main_loop_new(nullptr);
        pw_context *context = pw_context_new(pw_main_loop_get_loop(s.loop), nullptr, 0);
        pw_core *core = pw_context_connect(context, nullptr, 0);
        if (!core) {
            std::fprintf(stderr, "can't connect to PipeWire: %s\n", std::strerror(errno));
            return 1;
        }

        const std::string sinkName = "halo-decks-spike-" + std::to_string(c.quantum);
        const std::string latency = std::to_string(c.quantum) + "/48000";
        // A name per case, so `pw-top -b` run alongside counts each case's
        // xruns (its ERR column) on its own line.
        const std::string streamName = "halo-decks-spike-q" + std::to_string(c.quantum) + "-load" +
                                       std::to_string(int(c.load * 100));
        // Our own silent sink. node.driver makes it a driver, so it is a
        // graph of its own; priority.session 0 keeps the session manager
        // from ever choosing it as the default output.
        pw_properties *sinkProps = pw_properties_new(
            "factory.name", "support.null-audio-sink", "node.name", sinkName.c_str(), "node.description",
            "Halo decks spike (silent)", "media.class", "Audio/Sink", "audio.position", "FL,FR", "audio.rate",
            "48000", "node.driver", "true", "priority.driver", "0", "priority.session", "0", "node.virtual", "true",
            "node.latency", latency.c_str(), "node.force-quantum", std::to_string(c.quantum).c_str(),
            "object.linger", "false", nullptr);
        auto *sink = static_cast<pw_proxy *>(pw_core_create_object(core, "adapter", PW_TYPE_INTERFACE_Node,
                                                                   PW_VERSION_NODE, &sinkProps->dict, 0));
        pw_properties_free(sinkProps);
        // Let the sink appear before we target it.
        pw_loop_iterate(pw_main_loop_get_loop(s.loop), 200);
        for (int i = 0; i < 20; ++i)
            pw_loop_iterate(pw_main_loop_get_loop(s.loop), 10);

        s.stream = pw_stream_new_simple(
            pw_main_loop_get_loop(s.loop), streamName.c_str(),
            pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE,
                              "Music", PW_KEY_NODE_LATENCY, latency.c_str(), PW_KEY_TARGET_OBJECT, sinkName.c_str(),
                              "node.dont-reconnect", "true", "node.dont-fallback", "true", "node.dont-move", "true",
                              "node.autoconnect", "true", nullptr),
            &streamEvents, &s);

        uint8_t pod[1024];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(pod, sizeof pod);
        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_F32;
        info.channels = 2;
        info.rate = 48000;
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
        pw_stream_connect(s.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                          pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                          PW_STREAM_FLAG_RT_PROCESS),
                          params, 1);

        spa_source *timer = pw_loop_add_timer(pw_main_loop_get_loop(s.loop), stopLater, &s);
        timespec value{seconds, 0}, interval{0, 0};
        pw_loop_update_timer(pw_main_loop_get_loop(s.loop), timer, &value, &interval, false);
        pw_main_loop_run(s.loop);

        pw_stream_destroy(s.stream);
        pw_proxy_destroy(sink);
        pw_core_disconnect(core);
        pw_context_destroy(context);
        pw_main_loop_destroy(s.loop);

        // Skip the first half second (start-up).
        const size_t skip = std::min(s.intervals.size(), size_t(48000 / 2 / std::max(32, c.quantum)));
        std::vector<double> iv(s.intervals.begin() + long(skip), s.intervals.end());
        std::vector<double> busy(s.busy.begin() + long(std::min(skip, s.busy.size())), s.busy.end());
        int frames = s.sizes.empty() ? 0 : s.sizes[s.sizes.size() / 2];
        const double period = 1e6 * (frames ? frames : c.quantum) / 48000.0;
        double mean = 0;
        for (double v : iv)
            mean += v;
        mean = iv.empty() ? 0 : mean / double(iv.size());
        const long late = std::count_if(iv.begin(), iv.end(), [&](double v) { return v > 1.5 * period; });
        const char *pol = s.policy == SCHED_FIFO ? "FIFO" : s.policy == SCHED_RR ? "RR" : s.policy == SCHED_OTHER ? "OTHER" : "?";
        std::printf("%-8d %-5.2f %7.0fus %7.0fus %7.0fus %7.0fus %6ld %7.0fus %7.0fus %8.0fus %7d %s/%d  (%zu callbacks)\n",
                    c.quantum, c.load, period, mean, percentile(iv, 0.99), iv.empty() ? 0 : *std::max_element(iv.begin(), iv.end()),
                    late, percentile(busy, 0.99), busy.empty() ? 0 : *std::max_element(busy.begin(), busy.end()),
                    s.delayN ? double(s.delaySum) / double(s.delayN) : 0.0, frames, pol, s.priority, iv.size());
        if (g_sliceNs && !g_sliceOk)
            std::printf("  (slice request refused: %s)\n", std::strerror(g_sliceErrno));
        std::fflush(stdout);
    }
    pw_deinit();
    return 0;
}
