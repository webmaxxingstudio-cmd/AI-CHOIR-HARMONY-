#pragma once
// Streaming analysis of the lead vocal, shared by every generated voice.
//
//  * keeps a history buffer of the input (the voices read grains from it),
//  * runs YIN every `hop` samples,
//  * places pitch-synchronous analysis marks (one per glottal period when
//    voiced, a fixed 5 ms grid when unvoiced) that the PSOLA voices use,
//  * tracks the stable sung note and the musical key.
//
// Timeline: positions are absolute sample indices since reset(). A mark is only
// placed once at least maxPeriod() samples after it have arrived, so every mark
// can be used as the centre of a two-period grain immediately.

#include "vcs/MusicTheory.h"
#include "vcs/YinPitchDetector.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace vcs
{
struct AnalysisMark
{
    int64_t pos = 0;      ///< Absolute sample position of the grain centre
    float period = 0.0f;  ///< Local period in samples (grain half-length)
    float midi = 0.0f;    ///< Continuous sung pitch (0 when unvoiced)
    int note = -1;        ///< Stable note from the NoteTracker (-1 if none yet)
    bool voiced = false;
};

class VocalAnalyzer
{
public:
    struct Config
    {
        double sampleRate = 48000.0;
        float minF0 = 80.0f;
        float maxF0 = 1100.0f;
        int hop = 128;
        double historySeconds = 0.5;
        double keyMemorySeconds = 45.0; ///< Time constant of the key histogram
    };

    void prepare (const Config& config);
    void reset();

    /** Appends input samples, runs pitch analysis and places new marks. */
    void push (const float* input, int numSamples) noexcept;

    int64_t samplesWritten() const noexcept { return written; }
    int maxPeriod() const noexcept { return maxPeriodSamples; }
    int historyLength() const noexcept { return (int) history.size(); }
    double sampleRate() const noexcept { return cfg.sampleRate; }

    /** Raw history sample (0 outside the stored range). */
    float sampleAt (int64_t pos) const noexcept;

    /** Cubic (Catmull-Rom) interpolated read at a fractional position. */
    float interpolatedAt (double pos) const noexcept;

    /** Mark closest to `pos`. Returns false if no mark exists yet. */
    bool nearestMark (double pos, AnalysisMark& out) const noexcept;

    int64_t markCount() const noexcept { return marksPlaced; }

    // --- results for the UI / harmony engine ------------------------------
    const PitchEstimate& lastEstimate() const noexcept { return smoothed; }
    int stableNote() const noexcept { return noteTracker.currentNote(); }
    bool keyIsKnown() const noexcept { return keyKnown; }
    Key detectedKey() const noexcept { return currentKey; }
    const KeyDetector& keyDetector() const noexcept { return keyDet; }

private:
    void runPitchAnalysis() noexcept;
    void placeMarks() noexcept;
    void updateKey (float hopSeconds) noexcept;
    int64_t refinePeak (int64_t centre, int radius) const noexcept;

    Config cfg;
    YinPitchDetector yin;
    std::vector<float> history;
    int64_t historyMask = 0;
    std::vector<float> frame;
    int64_t written = 0;
    int maxPeriodSamples = 0;
    int unvoicedSpacing = 0;

    // Pitch state
    PitchEstimate raw[3] {};
    int rawCount = 0;
    PitchEstimate smoothed;
    NoteTracker noteTracker;

    // Marks
    std::vector<AnalysisMark> marks;
    int64_t marksMask = 0;
    int64_t marksPlaced = 0;
    AnalysisMark lastMark;
    bool haveLastMark = false;

    // Key
    KeyDetector keyDet;
    Key currentKey;
    bool keyKnown = false;
    Key candidateKey;
    float candidateSeconds = 0.0f;
    float keyEvalTimer = 0.0f;
};
} // namespace vcs
