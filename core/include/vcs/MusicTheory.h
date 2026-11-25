#pragma once
// Scales, keys, diatonic transposition, key detection and stable-note tracking.
// Everything here is allocation-free and cheap enough for the audio thread.

#include <array>
#include <cstdint>

namespace vcs
{
enum class ScaleType : int
{
    Major = 0,
    NaturalMinor,
    HarmonicMinor,
    MelodicMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Chromatic, ///< No key: harmonies are fixed (parallel) intervals
    count
};

const char* scaleName (ScaleType s) noexcept;
const char* pitchClassName (int pc) noexcept; ///< "C", "C#", ... "B"

/** Semitone offsets of the 7 degrees from the root (Chromatic maps to Major shape). */
const std::array<int, 7>& scaleIntervals (ScaleType s) noexcept;

struct Key
{
    int root = 0; ///< Pitch class 0..11 (0 = C)
    ScaleType scale = ScaleType::Major;

    bool contains (int midiNote) const noexcept;
    bool operator== (const Key& o) const noexcept { return root == o.root && scale == o.scale; }
};

/** Moves a note by a number of scale degrees inside the key.
    steps = +2 is "a third above", -3 "a fourth below", +7 an octave.
    Out-of-scale (chromatic) notes are moved in parallel with the scale note
    just below them, so passing tones stay passing tones. In Chromatic mode the
    interval is fixed: the major-scale distance of that many steps from the root. */
int diatonicTranspose (const Key& key, int midiNote, int steps) noexcept;

/** Duration-weighted pitch-class histogram matched against the Krumhansl–Kessler
    key profiles (Pearson correlation over the 24 major/minor keys). */
class KeyDetector
{
public:
    struct Result
    {
        Key key;
        float score = 0.0f;  ///< Correlation of the best key (-1..1)
        float margin = 0.0f; ///< Best score minus the runner-up's
        float totalWeight = 0.0f;
    };

    void reset() noexcept;

    /** Adds `weight` (e.g. seconds, scaled by confidence) of a sung pitch. */
    void addObservation (float midiPitch, float weight) noexcept;

    /** Multiplies the histogram by `factor` (0..1) so old material fades out. */
    void decay (float factor) noexcept;

    Result estimate() const noexcept;

    const std::array<float, 12>& histogram() const noexcept { return hist; }

private:
    std::array<float, 12> hist {};
};

/** Converts a continuous pitch track into stable note decisions, with
    hysteresis and a minimum duration so vibrato and scoops don't flip notes. */
class NoteTracker
{
public:
    void reset() noexcept;

    /** Feed one pitch observation covering `durationSeconds`. Returns the stable note (-1 if none yet). */
    int update (float midiPitch, bool voiced, float durationSeconds) noexcept;

    int currentNote() const noexcept { return note; }

    float hysteresisSemitones = 0.2f;  ///< Beyond +-0.5 st needed to change note
    float minSwitchSeconds = 0.045f;   ///< Candidate must persist this long

private:
    int note = -1;
    int candidate = -1;
    float candidateTime = 0.0f;
};

inline float hzToMidi (float hz) noexcept;
inline float midiToHz (float midi) noexcept;
} // namespace vcs

#include <cmath>
namespace vcs
{
inline float hzToMidi (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float midi) noexcept { return 440.0f * std::exp2 ((midi - 69.0f) / 12.0f); }
} // namespace vcs
