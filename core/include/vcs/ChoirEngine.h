#pragma once
// The choir engine: one analysed lead vocal in, a humanised multi-voice
// harmony ensemble out (stereo).
//
//   input -> VocalAnalyzer (pitch, marks, note, key) ---------------------.
//                 |                                                       |
//                 +--> voice 1..16: harmony target -> humanise -> PSOLA -> tone -> pan --> choir bus
//                 |                                                       |
//                 +--> lead (delayed by the engine latency so it lines up) ------------> lead bus
//
// Everything is allocated in prepare(); process() is real-time safe.

#include "vcs/Humanizer.h"
#include "vcs/MusicTheory.h"
#include "vcs/PsolaVoice.h"
#include "vcs/VocalAnalyzer.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <vector>

namespace vcs
{
enum class HarmonyMode : int
{
    Diatonic = 0, ///< Harmonies follow the key (auto-detected or chosen)
    Midi = 1      ///< Each part sings a note held on the MIDI input
};

struct Arrangement
{
    const char* name;
    int numParts;
    std::array<int, 8> steps; ///< Diatonic steps from the lead, in fill priority order
};

/** Built-in voicings. Parts are filled in order, round-robin, so with fewer
    singers than parts the most important harmonies are sung first. */
const std::array<Arrangement, 9>& arrangements() noexcept;

struct ChoirSettings
{
    int arrangement = 3;      ///< Index into arrangements()
    int singers = 6;          ///< 1..16
    HarmonyMode mode = HarmonyMode::Diatonic;
    bool autoKey = true;
    Key manualKey { 0, ScaleType::Major };

    float leadGainDb = 0.0f;
    float choirGainDb = -3.0f;

    float correction = 0.6f;      ///< 0 = harmonies keep the lead's scoops/vibrato, 1 = exact notes
    float humanize = 0.6f;        ///< Master amount for all variation below (0..1)
    float timingSpreadMs = 18.0f; ///< Max late offset of a singer vs the lead
    float pitchSpreadCents = 10.0f;
    float vibratoVariation = 0.5f; ///< Each singer's own vibrato depth (0..1 -> 0..30 cents)
    float formantSpread = 0.5f;    ///< Per-singer formant variation (0..1 -> +-1.5 st)
    float formantShift = 0.0f;     ///< Global formant ("gender"), semitones -5..+7
    float brightness = 0.0f;       ///< -1 darker .. +1 brighter
    float width = 0.8f;            ///< Stereo spread of the singers (0..1)
};

class ChoirEngine
{
public:
    static constexpr int kMaxVoices = 16;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Fixed processing latency (samples): report it to the host. */
    int latencySamples() const noexcept { return latency; }

    /** Call at the start of each block (audio thread). */
    void setSettings (const ChoirSettings& s) noexcept { settings = s; }

    // MIDI harmony input; sampleOffset is relative to the next process() block.
    void noteOn (int note, int sampleOffset) noexcept;
    void noteOff (int note, int sampleOffset) noexcept;
    void allNotesOff() noexcept;

    /** Mono in, stereo out. outL/outR are overwritten. */
    void process (const float* input, float* outL, float* outR, int numSamples) noexcept;

    // --- telemetry (read from the audio thread or copied out by the plugin) -
    const VocalAnalyzer& analyzer() const noexcept { return analysis; }
    Key activeKey() const noexcept;
    bool keyIsDetected() const noexcept { return settings.autoKey && analysis.keyIsKnown(); }
    int activeVoices() const noexcept;
    /** Note the voice is currently aiming at (-1 when silent). */
    int voiceNote (int voice) const noexcept { return voices[(size_t) voice].lastTargetNote; }
    float voicePan (int voice) const noexcept { return voices[(size_t) voice].pan; }
    /** Peak level of the voice over the last processed chunk (linear, after gain). */
    float voiceLevel (int voice) const noexcept { return voices[(size_t) voice].meter; }

private:
    struct Voice : GrainController
    {
        ChoirEngine* engine = nullptr;
        int index = 0;
        PsolaVoice psola;
        SmoothRandom pitchDrift, timingDrift, levelDrift;
        float staticDetune = 0.0f, staticTiming = 0.0f, staticFormant = 0.0f, staticTone = 0.0f;
        float vibratoRate = 5.5f, vibratoPhase = 0.0f;
        int64_t lastGrainTime = 0;
        float activeGain = 0.0f; ///< Fades voices in/out when the singer count changes
        bool active = false;
        float pan = 0.0f;
        int lastTargetNote = -1;
        float toneState = 0.0f;
        float meter = 0.0f;

        double timingOffset (int64_t synthTime) noexcept override;
        GrainShape shapeFor (const AnalysisMark& mark, int64_t synthTime) noexcept override;
    };

    void processChunk (const float* input, float* outL, float* outR, int n) noexcept;
    void applyMidiEventsUpTo (int64_t time) noexcept;
    int midiNoteForPart (int part) const noexcept;
    void updateVoiceLayout() noexcept;

    double sr = 48000.0;
    int maxBlock = 512;
    int latency = 0;
    int64_t now = 0;

    VocalAnalyzer analysis;
    std::array<Voice, kMaxVoices> voices;
    std::vector<float> voiceBuf;
    ChoirSettings settings;
    int layoutArrangement = -1, layoutSingers = -1;
    float layoutWidth = -1.0f;

    float leadGain = 1.0f, choirGain = 0.5f;

    // MIDI: timestamped events applied when the delayed timeline reaches them.
    struct MidiEvent
    {
        int64_t time;
        int note;
        bool on;
    };
    static constexpr int kMidiQueue = 256;
    std::array<MidiEvent, kMidiQueue> midiQueue {};
    int midiHead = 0, midiCount = 0;
    std::bitset<128> heldNotes;
    std::array<int, 128> sortedHeld {};
    int numHeld = 0;
};
} // namespace vcs
