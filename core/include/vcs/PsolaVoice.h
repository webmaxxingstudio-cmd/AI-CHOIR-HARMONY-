#pragma once
// Time-domain pitch-synchronous overlap-add (TD-PSOLA) voice.
//
// Each synthesis grain is a two-period, Hann-windowed slice of the input
// centred on an analysis mark (one glottal pulse). Grains are re-spaced at the
// new period to change pitch. Because each pulse keeps its own shape, the
// spectral envelope (the formants, i.e. the "vowel" and "who is singing") is
// preserved: no chipmunk effect. Formants can be moved independently by
// reading each grain faster or slower (formantFactor > 1 raises them).
//
// This is the established technique for monophonic voice harmonisers; it has
// much lower latency and fewer "phasey" artifacts on voice than a phase vocoder.
//
// The voice renders on the same absolute timeline as the analyser, delayed by
// a fixed latency so that every grain it needs is already available.

#include "vcs/VocalAnalyzer.h"

#include <cstdint>
#include <vector>

namespace vcs
{
struct GrainShape
{
    float pitchRatio = 1.0f;    ///< Output/input frequency ratio (voiced grains only)
    float formantFactor = 1.0f; ///< Spectral envelope scale (1 = preserved)
    float gain = 1.0f;          ///< Linear grain gain
};

/** Decides, per grain, where to read from and how to shape it. */
class GrainController
{
public:
    virtual ~GrainController() = default;
    /** Extra delay (samples, >= 0) applied to the analysis read position: timing humanisation. */
    virtual double timingOffset (int64_t synthTime) noexcept = 0;
    virtual GrainShape shapeFor (const AnalysisMark& mark, int64_t synthTime) noexcept = 0;
};

class PsolaVoice
{
public:
    static constexpr float kMinFormant = 0.75f; ///< about -5 semitones
    static constexpr float kMaxFormant = 1.5f;  ///< about +7 semitones

    /** Latency (samples) the voice needs behind the analyser's write position. */
    static int requiredLatency (const VocalAnalyzer& analyzer, int hop) noexcept;

    void prepare (const VocalAnalyzer& analyzer, int latencySamples);
    void reset() noexcept;

    /** Adds this voice's output for absolute time [t0, t0 + numSamples) into `out` (mono).
        The analyser must already contain input up to t0 + numSamples. */
    void render (const VocalAnalyzer& analyzer, int64_t t0, float* out, int numSamples,
                 GrainController& controller) noexcept;

private:
    void addGrain (const VocalAnalyzer& analyzer, const AnalysisMark& mark, double centre,
                   const GrainShape& shape) noexcept;

    std::vector<float> acc;
    int64_t accMask = 0;
    int latency = 0;
    int maxHalf = 0;
    double nextSynth = 0.0;
    bool started = false;
};
} // namespace vcs
