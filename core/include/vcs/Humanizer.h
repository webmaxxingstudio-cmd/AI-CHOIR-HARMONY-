#pragma once
// Deterministic, allocation-free random sources used to make each generated
// singer behave differently (pitch drift, timing, vibrato, level).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vcs
{
/** xorshift32: tiny, fast, deterministic per seed. */
class Rng
{
public:
    explicit Rng (uint32_t seed = 1) noexcept : state (seed ? seed : 0x9E3779B9u) {}
    void seed (uint32_t s) noexcept { state = s ? s : 0x9E3779B9u; }
    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    /** Uniform in [0, 1). */
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    /** Uniform in [-1, 1). */
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }

private:
    uint32_t state;
};

/** Smooth random curve in [-1, 1]: cosine interpolation between random
    points whose spacing is itself random in [minSeconds, maxSeconds].
    Unlike filtered noise it is bounded and has no sudden jumps, so it can
    drive pitch and timing without audible artifacts. */
class SmoothRandom
{
public:
    void init (uint32_t seed, double sampleRate, float minSeconds, float maxSeconds) noexcept
    {
        rng.seed (seed);
        sr = sampleRate;
        minLen = minSeconds;
        maxLen = maxSeconds;
        from = rng.bipolar();
        to = rng.bipolar();
        pos = 0.0;
        newSegment();
    }

    /** Advances by `samples` and returns the current value. */
    float advance (double samples) noexcept
    {
        pos += samples;
        while (pos >= len)
        {
            pos -= len;
            from = to;
            to = rng.bipolar();
            newSegment();
        }
        const float t = (float) (pos / len);
        const float s = 0.5f - 0.5f * std::cos (t * 3.14159265f);
        return from + (to - from) * s;
    }

private:
    void newSegment() noexcept { len = std::max (1.0, sr * (minLen + (maxLen - minLen) * rng.uniform())); }

    Rng rng;
    double sr = 48000.0, pos = 0.0, len = 1.0;
    float minLen = 0.5f, maxLen = 1.0f;
    float from = 0.0f, to = 0.0f;
};
} // namespace vcs
