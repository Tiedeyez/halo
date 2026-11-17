#include "engine.h"

#include <algorithm>
#include <cmath>

#include "stream.h"

namespace {
constexpr float Rate = 48000.0f;
// Ten milliseconds, so pause and cuts never click.
constexpr float RampStep = 1.0f / (Rate * 0.010f);
// A quarter second for the volume knob to settle.
constexpr float GainSmoothing = 1.0f / (Rate * 0.25f);
// Loudness correction can arrive a moment after a track starts (it is
// measured in the background), so it glides in over two seconds.
constexpr float TrackGainSmoothing = 1.0f / (Rate * 2.0f);
constexpr int DeclickFrames = 480;
}

Engine::Engine() {
    m_mix.resize(MaxBlock * 2);
    m_scratch.resize(MaxBlock * 2);
    m_tapScratch.resize(MaxBlock);
    for (auto &slot : m_retired)
        slot.store(nullptr);
    for (int d = 0; d < Decks; ++d) {
        m_playingFlag[d] = false;
        m_ended[d] = false;
    }
}

void Engine::post(const Command &c) {
    // The queue holds 128 commands and the audio thread drains it every few
    // milliseconds, so it only fills if audio has stopped entirely.
    m_commands.push(c);
}

void Engine::load(int deck, Stream *stream) { post({Op::Load, deck, stream}); }
void Engine::playNow(int deck) { post({Op::PlayNow, deck}); }
void Engine::armGapless(int deck) { post({Op::Arm, deck}); }
void Engine::unload(int deck) { post({Op::Unload, deck}); }
void Engine::setPaused(bool paused) { post({Op::Pause, 0, nullptr, 0, paused}); }
void Engine::setVolume(float volume) { post({Op::Volume, 0, nullptr, std::clamp(volume, 0.0f, 1.0f)}); }
void Engine::setTrackGain(int deck, float db) { post({Op::Gain, deck, nullptr, db}); }

void Engine::blend(int deck, double seconds, bool bassSwap) {
    Command c{Op::Blend, deck, nullptr, float(seconds), bassSwap};
    post(c);
}

void Engine::setEqualizer(const std::array<float, 10> &gains, float preampDb, bool enabled) {
    Command c{Op::Eq, 0, nullptr, preampDb, enabled};
    for (int i = 0; i < 10; ++i)
        c.eq[i] = gains[i];
    post(c);
}

Stream *Engine::retired() {
    for (auto &slot : m_retired)
        if (Stream *s = slot.exchange(nullptr))
            return s;
    return nullptr;
}

void Engine::release(int index) {
    Deck &deck = m_decks[index];
    if (deck.stream) {
        for (auto &slot : m_retired) {
            Stream *expected = nullptr;
            if (slot.compare_exchange_strong(expected, deck.stream))
                break;
        }
    }
    deck.stream = nullptr;
    deck.playing = deck.armed = deck.fadingIn = deck.fadingOut = deck.bassSwap = false;
    m_playingFlag[index] = false;
}

void Engine::apply(const Command &c) {
    switch (c.op) {
    case Op::Load: {
        release(c.deck);
        Deck &deck = m_decks[c.deck];
        deck.stream = c.stream;
        deck.gain = deck.targetGain;
        deck.lowCut[0].reset();
        deck.lowCut[1].reset();
        m_ended[c.deck] = false;
        break;
    }
    case Op::PlayNow: {
        const int other = 1 - c.deck;
        release(other);
        Deck &deck = m_decks[c.deck];
        if (!deck.stream)
            break;
        deck.playing = true;
        deck.armed = deck.fadingIn = deck.fadingOut = deck.bassSwap = false;
        deck.declick = DeclickFrames;
        m_playingFlag[c.deck] = true;
        m_ended[c.deck] = false;
        m_current = c.deck;
        m_blendingFlag = false;
        break;
    }
    case Op::Arm:
        if (m_decks[c.deck].stream)
            m_decks[c.deck].armed = true;
        break;
    case Op::Blend: {
        Deck &in = m_decks[c.deck];
        Deck &out = m_decks[1 - c.deck];
        if (!in.stream)
            break;
        const long long length = std::max(1LL, (long long)(c.value * Rate));
        in.playing = true;
        in.armed = false;
        in.fadingIn = true;
        in.fadingOut = false;
        in.fadePos = 0;
        in.fadeLength = length;
        in.bassSwap = c.flag;
        m_playingFlag[c.deck] = true;
        m_ended[c.deck] = false;
        if (out.playing) {
            out.fadingOut = true;
            out.fadingIn = false;
            out.fadePos = 0;
            out.fadeLength = length;
            out.bassSwap = c.flag;
            m_blendingFlag = true;
        }
        m_current = c.deck;
        break;
    }
    case Op::Unload:
        release(c.deck);
        break;
    case Op::Pause:
        m_paused = c.flag;
        break;
    case Op::Volume:
        m_volume = c.value;
        break;
    case Op::Gain:
        m_decks[c.deck].targetGain = Dsp::dbToGain(std::clamp(c.value, -18.0f, 9.0f));
        break;
    case Op::Eq:
        m_eqOn = c.flag;
        m_preamp = Dsp::dbToGain(c.value);
        for (int i = 0; i < 10; ++i)
            m_eq[i] = Dsp::peaking(Rate, Dsp::EqFrequencies[i], 1.41, c.eq[i]);
        break;
    }
}

void Engine::mixDeck(int index, float *mix, int offset, int frames) {
    Deck &deck = m_decks[index];
    if (!deck.stream || frames <= 0)
        return;
    float *in = m_scratch.data();
    const int got = deck.stream->read(in, frames);

    // The bass swap: a high-pass that rises on the outgoing track through the
    // first half of the blend and falls away on the incoming one through the
    // second half, so only one kick drum carries the low end at a time.
    bool cut = false;
    if (deck.bassSwap && deck.fadeLength > 0) {
        const double t = std::clamp(double(deck.fadePos) / double(deck.fadeLength), 0.0, 1.0);
        double cutoff = 20;
        if (deck.fadingOut)
            cutoff = t < 0.5 ? 20 * std::pow(12.5, t * 2) : 250;
        else if (deck.fadingIn)
            cutoff = t < 0.5 ? 250 : 250 * std::pow(1 / 12.5, (t - 0.5) * 2);
        if (cutoff > 22) {
            deck.lowCutCoefficients = Dsp::highPass(Rate, cutoff, 0.7071);
            cut = true;
        }
    }

    const double length = double(std::max(1LL, deck.fadeLength));
    for (int i = 0; i < got; ++i) {
        deck.gain += (deck.targetGain - deck.gain) * TrackGainSmoothing;
        float g = deck.gain;
        if (deck.fadingIn || deck.fadingOut) {
            const double t = std::min(1.0, double(deck.fadePos + i) / length);
            // Equal power, so the blend doesn't dip in the middle.
            g *= float(deck.fadingIn ? std::sin(t * Dsp::Pi / 2) : std::cos(t * Dsp::Pi / 2));
        }
        if (deck.declick > 0) {
            g *= 1.0f - float(deck.declick) / DeclickFrames;
            --deck.declick;
        }
        float l = in[i * 2], r = in[i * 2 + 1];
        if (cut) {
            l = deck.lowCut[0].process(deck.lowCutCoefficients, l);
            r = deck.lowCut[1].process(deck.lowCutCoefficients, r);
        }
        mix[(offset + i) * 2] += l * g;
        mix[(offset + i) * 2 + 1] += r * g;
    }

    if (deck.fadingIn || deck.fadingOut) {
        deck.fadePos += got;
        if (deck.fadePos >= deck.fadeLength) {
            if (deck.fadingOut) {
                release(index);
                m_blendingFlag = false;
                return;
            }
            deck.fadingIn = false;
            deck.bassSwap = false;
        }
    }

    if (got < frames && deck.stream->drained()) {
        const int other = 1 - index;
        if (index == m_current.load() && m_decks[other].armed && m_decks[other].stream) {
            // Gapless: the next track starts on the very next sample.
            release(index);
            Deck &next = m_decks[other];
            next.armed = false;
            next.playing = true;
            m_playingFlag[other] = true;
            m_current = other;
            m_switches.fetch_add(1);
            mixDeck(other, mix, offset + got, frames - got);
            return;
        }
        if (deck.fadingOut)
            m_blendingFlag = false;
        else if (index == m_current.load())
            m_ended[index] = true;
        release(index);
    }
}

void Engine::render(float *out, int frames) {
    while (frames > MaxBlock) {
        render(out, MaxBlock);
        out += MaxBlock * 2;
        frames -= MaxBlock;
    }

    Command c;
    while (m_commands.pop(c))
        apply(c);

    float *mix = m_mix.data();
    std::fill(mix, mix + frames * 2, 0.0f);

    const bool silent = m_paused && m_pauseGain <= 0.0f;
    if (!silent) {
        // Decks that play at the start of the block; one that a gapless
        // hand-over starts is mixed by the hand-over itself.
        bool playing[Decks];
        for (int d = 0; d < Decks; ++d)
            playing[d] = m_decks[d].playing;
        for (int d = 0; d < Decks; ++d)
            if (playing[d])
                mixDeck(d, mix, 0, frames);
    }

    float *tap = m_tapScratch.data();
    for (int i = 0; i < frames; ++i) {
        const float pauseTarget = m_paused ? 0.0f : 1.0f;
        if (m_pauseGain < pauseTarget)
            m_pauseGain = std::min(pauseTarget, m_pauseGain + RampStep);
        else if (m_pauseGain > pauseTarget)
            m_pauseGain = std::max(pauseTarget, m_pauseGain - RampStep);
        m_volumeSmooth += (m_volume - m_volumeSmooth) * GainSmoothing;

        float l = mix[i * 2], r = mix[i * 2 + 1];
        if (m_eqOn) {
            l *= m_preamp;
            r *= m_preamp;
            for (int b = 0; b < 10; ++b) {
                l = m_eqState[0][b].process(m_eq[b], l);
                r = m_eqState[1][b].process(m_eq[b], r);
            }
        }
        // The visualiser sees the music before the volume knob, so the halo
        // still moves when it's quiet.
        tap[i] = (l + r) * 0.5f * m_pauseGain;

        // Cubic volume curve: the knob feels even across its travel.
        const float v = m_volumeSmooth * m_volumeSmooth * m_volumeSmooth * m_pauseGain;
        l *= v;
        r *= v;

        // True-peak limiter at -1 dBTP with 1.6 ms lookahead
        // (Dsp::TruePeakLimiter; tools/audio-lab/limiter.cpp measures it).
        m_limiter.process(l, r);
        out[i * 2] = l;
        out[i * 2 + 1] = r;
    }
    if (!silent)
        m_tap.write(tap, size_t(frames));
}
