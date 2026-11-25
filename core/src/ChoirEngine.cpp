#include "vcs/ChoirEngine.h"

#include <algorithm>
#include <cmath>

namespace vcs
{
namespace
{
    float dbToGain (float db) noexcept { return db <= -96.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

    constexpr float kConsonantLevel = 0.5f;   // -6 dB: unvoiced sounds in the backing voices
    constexpr float kRegisterTracking = 0.15f; // formant follows 15% of the harmony interval
    constexpr float kMaxVibratoCents = 30.0f;
    constexpr float kMaxFormantSpread = 1.5f;  // semitones
    constexpr float kMaxLevelDrift = 1.5f;     // dB
    constexpr float kFadeSeconds = 0.08f;
} // namespace

const std::array<Arrangement, 9>& arrangements() noexcept
{
    // Steps are diatonic degrees relative to the lead (+2 = third above,
    // -3 = fourth below, -7 = octave below). 0 = unison double.
    static const std::array<Arrangement, 9> a { {
        { "Unison Double", 1, { 0 } },
        { "2-Part (3rd above)", 2, { 2, 0 } },
        { "2-Part (3rd below)", 2, { -2, 0 } },
        { "3-Part Triad", 3, { 2, -3, 0 } },
        { "4-Part", 4, { 2, -3, -7, 0 } },
        { "5-Part", 5, { 2, 4, -3, -7, 0 } },
        { "8-Part Choir", 8, { 2, 4, 7, -3, -5, -7, 9, 0 } },
        { "Octaves", 3, { 7, -7, 0 } },
        { "Fifths", 3, { 4, -7, 0 } },
    } };
    return a;
}

void ChoirEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (16, maxBlockSize);

    VocalAnalyzer::Config ac;
    ac.sampleRate = sr;
    ac.historySeconds = 0.6; // latency + max timing spread + grain reach
    analysis.prepare (ac);
    latency = PsolaVoice::requiredLatency (analysis, ac.hop);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.engine = this;
        v.index = i;
        v.psola.prepare (analysis, latency);
    }
    voiceBuf.assign ((size_t) maxBlock, 0.0f);
    reset();
}

void ChoirEngine::reset()
{
    analysis.reset();
    now = 0;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.psola.reset();
        const uint32_t seed = 0x1234567u + 7919u * (uint32_t) i;
        Rng r (seed);
        v.staticDetune = r.bipolar();
        v.staticTiming = r.uniform();
        v.staticFormant = r.bipolar();
        v.staticTone = r.bipolar();
        v.vibratoRate = 4.8f + 1.4f * r.uniform();
        v.vibratoPhase = 6.2831853f * r.uniform();
        v.pitchDrift.init (seed ^ 0xA5A5A5u, sr, 0.6f, 1.6f);
        v.timingDrift.init (seed ^ 0x5A5A5Au, sr, 1.5f, 3.5f);
        v.levelDrift.init (seed ^ 0x3C3C3Cu, sr, 0.8f, 2.0f);
        v.lastGrainTime = 0;
        v.activeGain = 0.0f;
        v.active = false;
        v.toneState = 0.0f;
        v.lastTargetNote = -1;
    }
    layoutArrangement = layoutSingers = -1;
    layoutWidth = -1.0f;
    leadGain = dbToGain (settings.leadGainDb);
    choirGain = 0.0f;
    allNotesOff();
}

Key ChoirEngine::activeKey() const noexcept
{
    if (! settings.autoKey)
        return settings.manualKey;
    if (analysis.keyIsKnown())
        return analysis.detectedKey();
    return { 0, ScaleType::Chromatic }; // until a key is detected: parallel intervals
}

int ChoirEngine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

// ---------------------------------------------------------------- MIDI
void ChoirEngine::noteOn (int note, int sampleOffset) noexcept
{
    if (note < 0 || note > 127 || midiCount >= kMidiQueue)
        return;
    midiQueue[(size_t) ((midiHead + midiCount) % kMidiQueue)] = { now + sampleOffset, note, true };
    ++midiCount;
}

void ChoirEngine::noteOff (int note, int sampleOffset) noexcept
{
    if (note < 0 || note > 127 || midiCount >= kMidiQueue)
        return;
    midiQueue[(size_t) ((midiHead + midiCount) % kMidiQueue)] = { now + sampleOffset, note, false };
    ++midiCount;
}

void ChoirEngine::allNotesOff() noexcept
{
    midiHead = midiCount = 0;
    heldNotes.reset();
    numHeld = 0;
}

void ChoirEngine::applyMidiEventsUpTo (int64_t time) noexcept
{
    bool changed = false;
    while (midiCount > 0 && midiQueue[(size_t) midiHead].time <= time)
    {
        const auto& e = midiQueue[(size_t) midiHead];
        heldNotes.set ((size_t) e.note, e.on);
        midiHead = (midiHead + 1) % kMidiQueue;
        --midiCount;
        changed = true;
    }
    if (! changed)
        return;
    numHeld = 0;
    for (int n = 127; n >= 0; --n) // highest note first
        if (heldNotes.test ((size_t) n))
            sortedHeld[(size_t) numHeld++] = n;
}

int ChoirEngine::midiNoteForPart (int part) const noexcept
{
    return numHeld > 0 ? sortedHeld[(size_t) (part % numHeld)] : -1;
}

// ---------------------------------------------------------------- layout
void ChoirEngine::updateVoiceLayout() noexcept
{
    const int singers = std::clamp (settings.singers, 1, kMaxVoices);
    const int arr = std::clamp (settings.arrangement, 0, (int) arrangements().size() - 1);
    if (singers == layoutSingers && arr == layoutArrangement && settings.width == layoutWidth)
        return;
    layoutSingers = singers;
    layoutArrangement = arr;
    layoutWidth = settings.width;

    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.active = i < singers;
        // Even slots across the stage, filled from the outside in, alternating
        // sides (L, R, L+1, R-1, ...): consecutive voices, which sing different
        // parts, land on opposite sides so no part clumps in one place.
        float pos = 0.0f;
        if (singers > 1 && i < singers)
        {
            const int slot = (i % 2 == 0) ? i / 2 : singers - 1 - i / 2;
            pos = -1.0f + 2.0f * (float) slot / (float) (singers - 1);
        }
        v.pan = std::clamp (pos * settings.width, -1.0f, 1.0f);
    }
}

// ---------------------------------------------------------------- voice
double ChoirEngine::Voice::timingOffset (int64_t synthTime) noexcept
{
    const auto& s = engine->settings;
    const float drift = 0.5f + 0.5f * timingDrift.advance ((double) std::max<int64_t> (0, synthTime - lastGrainTime));
    const float amount = s.humanize * (0.6f * staticTiming + 0.4f * drift);
    return (double) (std::clamp (s.timingSpreadMs, 0.0f, 40.0f) * 0.001f * (float) engine->sr * amount);
}

GrainShape ChoirEngine::Voice::shapeFor (const AnalysisMark& mark, int64_t synthTime) noexcept
{
    const auto& s = engine->settings;
    const double dt = (double) std::clamp<int64_t> (synthTime - lastGrainTime, 0, (int64_t) engine->sr);
    lastGrainTime = synthTime;

    const float fadeStep = (float) (dt / (kFadeSeconds * engine->sr));
    activeGain = active ? std::min (1.0f, activeGain + fadeStep) : std::max (0.0f, activeGain - fadeStep);

    const float h = std::clamp (s.humanize, 0.0f, 1.0f);
    const float drift = pitchDrift.advance (dt);
    const float level = levelDrift.advance (dt);
    vibratoPhase = std::fmod (vibratoPhase + (float) (6.2831853 * vibratoRate * dt / engine->sr), 6.2831853f);

    GrainShape g;
    g.gain = activeGain * dbToGain (level * kMaxLevelDrift * h);

    float formantSemis = s.formantShift + staticFormant * s.formantSpread * kMaxFormantSpread * h;

    if (! mark.voiced || mark.note < 0)
    {
        g.gain *= mark.voiced ? 1.0f : kConsonantLevel;
        if (s.mode == HarmonyMode::Midi && engine->numHeld == 0)
            g.gain = 0.0f;
        g.formantFactor = std::exp2 (std::clamp (formantSemis, -5.0f, 7.0f) / 12.0f);
        return g;
    }

    const int leadNote = mark.note;
    int target;
    if (s.mode == HarmonyMode::Midi)
    {
        target = engine->midiNoteForPart (index);
        if (target < 0)
        {
            lastTargetNote = -1;
            g.gain = 0.0f;
            return g;
        }
    }
    else
    {
        const auto& arr = arrangements()[(size_t) std::clamp (s.arrangement, 0, (int) arrangements().size() - 1)];
        target = diatonicTranspose (engine->activeKey(), leadNote, arr.steps[(size_t) (index % arr.numParts)]);
    }
    lastTargetNote = target;

    const float cents = s.pitchSpreadCents * h * (0.5f * staticDetune + 0.5f * drift)
                        + s.vibratoVariation * kMaxVibratoCents * h * std::sin (vibratoPhase);
    const float expression = (mark.midi - (float) leadNote) * (1.0f - std::clamp (s.correction, 0.0f, 1.0f));
    const float targetMidi = (float) target + expression + cents * 0.01f;
    g.pitchRatio = std::exp2 ((targetMidi - mark.midi) / 12.0f);

    formantSemis += kRegisterTracking * (float) (target - leadNote);
    g.formantFactor = std::exp2 (std::clamp (formantSemis, -5.0f, 7.0f) / 12.0f);
    return g;
}

// ---------------------------------------------------------------- process
void ChoirEngine::process (const float* input, float* outL, float* outR, int numSamples) noexcept
{
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (maxBlock, numSamples - done);
        processChunk (input + done, outL + done, outR + done, n);
        done += n;
    }
}

void ChoirEngine::processChunk (const float* input, float* outL, float* outR, int n) noexcept
{
    updateVoiceLayout();
    analysis.push (input, n);
    applyMidiEventsUpTo (now + n - latency);

    std::fill (outL, outL + n, 0.0f);
    std::fill (outR, outR + n, 0.0f);

    // Level-normalise the choir so its loudness doesn't jump with singer count
    // (N decorrelated voices sum to about +10 log10 N dB).
    const float choirTarget = dbToGain (settings.choirGainDb) / std::sqrt ((float) std::max (1, layoutSingers));
    const float leadTarget = dbToGain (settings.leadGainDb);
    const float choirStep = (choirTarget - choirGain) / (float) n;
    const float leadStep = (leadTarget - leadGain) / (float) n;

    const float toneCoef = 1.0f - std::exp (-6.2831853f * 2500.0f / (float) sr);

    for (auto& v : voices)
    {
        if (! v.active && v.activeGain <= 0.0f)
        {
            v.meter = 0.0f;
            continue;
        }

        std::fill (voiceBuf.begin(), voiceBuf.begin() + n, 0.0f);
        v.psola.render (analysis, now, voiceBuf.data(), n, v);

        const float b = std::clamp (settings.brightness + v.staticTone * 0.25f * settings.humanize, -1.0f, 1.0f);
        const float hfGain = b >= 0.0f ? b : 0.7f * b;
        const float angle = (v.pan + 1.0f) * 0.7853982f;
        const float gl = std::cos (angle), gr = std::sin (angle);
        float cg = choirGain;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float x = voiceBuf[(size_t) i];
            v.toneState += toneCoef * (x - v.toneState);
            const float y = x + hfGain * (x - v.toneState);
            cg += choirStep;
            outL[i] += y * gl * cg;
            outR[i] += y * gr * cg;
            peak = std::max (peak, std::abs (y * cg));
        }
        v.meter = peak;

        if (! v.active && v.activeGain <= 0.0f)
            v.psola.reset(); // fully faded out: restart cleanly when re-enabled
    }
    choirGain = choirTarget;

    float lg = leadGain;
    for (int i = 0; i < n; ++i)
    {
        lg += leadStep;
        const float x = analysis.sampleAt (now + i - latency) * lg;
        outL[i] += x;
        outR[i] += x;
    }
    leadGain = leadTarget;
    now += n;
}
} // namespace vcs
