#include "vcs/MusicTheory.h"

#include <algorithm>
#include <cmath>

namespace vcs
{
namespace
{
    constexpr std::array<std::array<int, 7>, (size_t) ScaleType::count> kIntervals { {
        { 0, 2, 4, 5, 7, 9, 11 }, // Major
        { 0, 2, 3, 5, 7, 8, 10 }, // Natural minor
        { 0, 2, 3, 5, 7, 8, 11 }, // Harmonic minor
        { 0, 2, 3, 5, 7, 9, 11 }, // Melodic minor (ascending)
        { 0, 2, 3, 5, 7, 9, 10 }, // Dorian
        { 0, 1, 3, 5, 7, 8, 10 }, // Phrygian
        { 0, 2, 4, 6, 7, 9, 11 }, // Lydian
        { 0, 2, 4, 5, 7, 9, 10 }, // Mixolydian
        { 0, 2, 4, 5, 7, 9, 11 }, // Chromatic -> parallel major-shaped intervals
    } };

    // Krumhansl & Kessler (1982) probe-tone profiles.
    constexpr std::array<float, 12> kMajorProfile { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    constexpr std::array<float, 12> kMinorProfile { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    int floorDiv (int a, int b) noexcept { return (a >= 0) ? a / b : -((-a + b - 1) / b); }
    int mod (int a, int b) noexcept { return ((a % b) + b) % b; }

    float pearson (const std::array<float, 12>& h, const std::array<float, 12>& profile, int root) noexcept
    {
        float mh = 0, mp = 0;
        for (int i = 0; i < 12; ++i) { mh += h[(size_t) i]; mp += profile[(size_t) i]; }
        mh /= 12.0f; mp /= 12.0f;
        float num = 0, dh = 0, dp = 0;
        for (int pc = 0; pc < 12; ++pc)
        {
            const float a = h[(size_t) pc] - mh;
            const float b = profile[(size_t) mod (pc - root, 12)] - mp;
            num += a * b; dh += a * a; dp += b * b;
        }
        const float den = std::sqrt (dh * dp);
        return den > 1e-12f ? num / den : 0.0f;
    }
} // namespace

const char* scaleName (ScaleType s) noexcept
{
    switch (s)
    {
        case ScaleType::Major: return "Major";
        case ScaleType::NaturalMinor: return "Minor";
        case ScaleType::HarmonicMinor: return "Harmonic Minor";
        case ScaleType::MelodicMinor: return "Melodic Minor";
        case ScaleType::Dorian: return "Dorian";
        case ScaleType::Phrygian: return "Phrygian";
        case ScaleType::Lydian: return "Lydian";
        case ScaleType::Mixolydian: return "Mixolydian";
        case ScaleType::Chromatic: return "Chromatic";
        default: return "?";
    }
}

const char* pitchClassName (int pc) noexcept
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[mod (pc, 12)];
}

const std::array<int, 7>& scaleIntervals (ScaleType s) noexcept
{
    return kIntervals[(size_t) std::clamp ((int) s, 0, (int) ScaleType::count - 1)];
}

bool Key::contains (int midiNote) const noexcept
{
    if (scale == ScaleType::Chromatic)
        return true;
    const int rel = mod (midiNote - root, 12);
    const auto& iv = scaleIntervals (scale);
    return std::find (iv.begin(), iv.end(), rel) != iv.end();
}

int diatonicTranspose (const Key& key, int midiNote, int steps) noexcept
{
    const auto& iv = scaleIntervals (key.scale);

    if (key.scale == ScaleType::Chromatic)
    {
        // Fixed interval: distance of `steps` degrees from the root of a major scale.
        const int octave = floorDiv (steps, 7);
        return midiNote + octave * 12 + iv[(size_t) mod (steps, 7)];
    }

    // Find the scale degree at or just below the note.
    const int rel = midiNote - key.root;
    const int octave = floorDiv (rel, 12);
    const int pc = mod (rel, 12);
    int degree = 0;
    for (int d = 0; d < 7; ++d)
        if (iv[(size_t) d] <= pc)
            degree = d;
    const int chromaticOffset = pc - iv[(size_t) degree];

    const int target = degree + steps;
    const int targetOctave = octave + floorDiv (target, 7);
    return key.root + targetOctave * 12 + iv[(size_t) mod (target, 7)] + chromaticOffset;
}

// ---------------------------------------------------------------------------

void KeyDetector::reset() noexcept { hist.fill (0.0f); }

void KeyDetector::addObservation (float midiPitch, float weight) noexcept
{
    if (weight <= 0.0f || ! std::isfinite (midiPitch))
        return;
    const int pc = mod ((int) std::lround (midiPitch), 12);
    hist[(size_t) pc] += weight;
}

void KeyDetector::decay (float factor) noexcept
{
    for (auto& v : hist)
        v *= factor;
}

KeyDetector::Result KeyDetector::estimate() const noexcept
{
    Result r;
    for (auto v : hist)
        r.totalWeight += v;
    if (r.totalWeight <= 0.0f)
        return r;

    float best = -2.0f, second = -2.0f;
    for (int root = 0; root < 12; ++root)
    {
        for (int minor = 0; minor < 2; ++minor)
        {
            const float s = pearson (hist, minor ? kMinorProfile : kMajorProfile, root);
            if (s > best)
            {
                second = best;
                best = s;
                r.key = { root, minor ? ScaleType::NaturalMinor : ScaleType::Major };
            }
            else if (s > second)
            {
                second = s;
            }
        }
    }

    // A minor key whose raised 7th is sung more than its flat 7th is harmonic minor.
    if (r.key.scale == ScaleType::NaturalMinor)
    {
        const float flat7 = hist[(size_t) mod (r.key.root + 10, 12)];
        const float raised7 = hist[(size_t) mod (r.key.root + 11, 12)];
        if (raised7 > flat7)
            r.key.scale = ScaleType::HarmonicMinor;
    }

    r.score = best;
    r.margin = best - second;
    return r;
}

// ---------------------------------------------------------------------------

void NoteTracker::reset() noexcept
{
    note = -1;
    candidate = -1;
    candidateTime = 0.0f;
}

int NoteTracker::update (float midiPitch, bool voiced, float durationSeconds) noexcept
{
    if (! voiced || ! std::isfinite (midiPitch))
    {
        candidate = -1;
        candidateTime = 0.0f;
        return note; // hold the last note through consonants
    }

    const int nearest = (int) std::lround (midiPitch);
    if (note < 0)
    {
        note = nearest;
        return note;
    }

    if (std::abs (midiPitch - (float) note) <= 0.5f + hysteresisSemitones)
    {
        candidate = -1;
        candidateTime = 0.0f;
        return note;
    }

    if (nearest != candidate)
    {
        candidate = nearest;
        candidateTime = 0.0f;
    }
    candidateTime += durationSeconds;
    if (candidateTime >= minSwitchSeconds)
    {
        note = candidate;
        candidate = -1;
        candidateTime = 0.0f;
    }
    return note;
}
} // namespace vcs
