#pragma once

#include <array>
#include <atomic>
#include <vector>

#include "dsp.h"
#include "ring.h"

class Stream;

// Mixes two decks into the output on the audio thread. The GUI thread never
// touches the mixing state directly: it posts commands to a lock-free queue
// that the audio callback drains at the start of every block, and reads back
// a few atomics. The engine never frees a Stream; a stream it lets go of is
// handed back through retired() for the GUI thread to delete.
class Engine {
public:
    static constexpr int Decks = 2;
    static constexpr int MaxBlock = 8192;

    Engine();

    // ---- GUI thread ----
    // Puts a stream on a deck without playing it, so it can fill up first.
    void load(int deck, Stream *stream);
    // Starts a loaded deck at once, cutting whatever the other deck plays.
    void playNow(int deck);
    // Plays `deck` straight after the current deck runs out, sample-exact.
    void armGapless(int deck);
    // Crossfades from the current deck into `deck` over `seconds`, with an
    // optional DJ bass swap: the lows of the outgoing track give way to the
    // lows of the incoming one halfway through.
    void blend(int deck, double seconds, bool bassSwap);
    // Lets go of a deck's stream (it comes back through retired()).
    void unload(int deck);
    void setPaused(bool paused);
    void setVolume(float volume);
    // Track loudness correction for a deck, in dB; ramps smoothly.
    void setTrackGain(int deck, float db);
    // Ten band gains in dB and a preamp; flat when `enabled` is false.
    void setEqualizer(const std::array<float, 10> &gains, float preampDb, bool enabled);
    // Headphone correction (src/headphones.h): filters already designed for
    // 48 kHz, applied before the user's EQ. An empty list turns it off.
    static constexpr int MaxCorrection = 16;
    void setCorrection(const std::vector<Dsp::Coefficients> &filters, float preampDb);

    int current() const { return m_current.load(); }
    // Bumps every time the engine switched decks by itself (a gapless hand-over).
    int switches() const { return m_switches.load(); }
    bool deckPlaying(int deck) const { return m_playingFlag[deck].load(); }
    bool deckEnded(int deck) const { return m_ended[deck].load(); }
    bool blending() const { return m_blendingFlag.load(); }
    Stream *retired();

    // Mono samples of what was played, for the visualiser (GUI thread reads).
    Ring &tap() { return m_tap; }

    // ---- audio thread ----
    void render(float *out, int frames);

private:
    enum class Op { Load, PlayNow, Arm, Blend, Unload, Pause, Volume, Gain, Eq, Correction };
    struct Command {
        Op op;
        int deck = 0;
        Stream *stream = nullptr;
        float value = 0;
        bool flag = false;
        std::array<float, 11> eq{};
        std::array<Dsp::Coefficients, MaxCorrection> correction{};
        int filters = 0;
    };

    // A tiny SPSC queue of commands, same scheme as Ring.
    struct Queue {
        std::array<Command, 128> items;
        std::atomic<size_t> write{0}, read{0};
        bool push(const Command &c) {
            const size_t w = write.load(std::memory_order_relaxed);
            if (w - read.load(std::memory_order_acquire) >= items.size())
                return false;
            items[w % items.size()] = c;
            write.store(w + 1, std::memory_order_release);
            return true;
        }
        bool pop(Command &c) {
            const size_t r = read.load(std::memory_order_relaxed);
            if (r == write.load(std::memory_order_acquire))
                return false;
            c = items[r % items.size()];
            read.store(r + 1, std::memory_order_release);
            return true;
        }
    };

    struct Deck {
        Stream *stream = nullptr;
        bool playing = false;
        bool armed = false;
        // Crossfade: position and length in frames, and whether this deck is
        // coming in or going out.
        long long fadePos = 0, fadeLength = 0;
        bool fadingIn = false, fadingOut = false, bassSwap = false;
        // De-click ramp after a cut, in frames left.
        int declick = 0;
        float gain = 1, targetGain = 1;
        Dsp::Biquad lowCut[2];
        Dsp::Coefficients lowCutCoefficients;
    };

    void apply(const Command &c);
    void release(int deck);
    void mixDeck(int index, float *mix, int offset, int frames);
    void post(const Command &c);

    Queue m_commands;
    std::array<Deck, Decks> m_decks;
    std::array<std::atomic<bool>, Decks> m_playingFlag{};
    std::array<std::atomic<bool>, Decks> m_ended{};
    std::atomic<int> m_current{0};
    std::atomic<int> m_switches{0};
    std::atomic<bool> m_blendingFlag{false};

    // Streams waiting for the GUI thread to delete them.
    std::array<std::atomic<Stream *>, 8> m_retired{};

    bool m_paused = true;
    float m_pauseGain = 0;
    float m_volume = 0.8f, m_volumeSmooth = 0.8f;
    bool m_eqOn = false;
    float m_preamp = 1;
    std::array<Dsp::Coefficients, 10> m_eq{};
    std::array<std::array<Dsp::Biquad, 10>, 2> m_eqState{};
    std::array<Dsp::Coefficients, MaxCorrection> m_correction{};
    std::array<std::array<Dsp::Biquad, MaxCorrection>, 2> m_correctionState{};
    int m_correctionFilters = 0;
    float m_correctionGain = 1;
    Dsp::TruePeakLimiter m_limiter{48000, -1.0};

    std::vector<float> m_mix, m_scratch;
    Ring m_tap{16384};
    std::vector<float> m_tapScratch;
};
