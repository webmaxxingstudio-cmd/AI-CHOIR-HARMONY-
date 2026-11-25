#pragma once
// Synthetic test material: a band-limited "sung vowel" made of harmonics
// shaped by formant peaks, with optional vibrato. Using known formants lets the
// tests check formant preservation objectively.

#include <cmath>
#include <vector>

namespace vcstest
{
struct VowelSpec
{
    double sampleRate = 48000.0;
    double f0 = 220.0;
    double seconds = 1.0;
    double vibratoHz = 0.0;
    double vibratoCents = 0.0;
    double formants[3] = { 800.0, 1200.0, 2600.0 }; // roughly an "ah"
    double amplitude = 0.3;
};

inline double formantEnvelope (const VowelSpec& s, double freq)
{
    double env = 0.0;
    const double bw[3] = { 90.0, 110.0, 160.0 };
    const double g[3] = { 1.0, 0.6, 0.25 };
    for (int i = 0; i < 3; ++i)
    {
        const double d = (freq - s.formants[i]) / bw[i];
        env += g[i] / (1.0 + d * d);
    }
    return env + 0.02;
}

/** f0Track (optional) overrides the constant f0 sample-by-sample. */
inline std::vector<float> makeVowel (const VowelSpec& s, const std::vector<double>* f0Track = nullptr)
{
    const int n = (int) (s.seconds * s.sampleRate);
    std::vector<float> out ((size_t) n, 0.0f);
    std::vector<double> phase (64, 0.0);
    double norm = 0.0;
    for (int h = 1; h <= 40; ++h)
        norm += formantEnvelope (s, s.f0 * h);

    for (int i = 0; i < n; ++i)
    {
        const double t = i / s.sampleRate;
        double f0 = f0Track ? (*f0Track)[(size_t) i] : s.f0;
        if (s.vibratoHz > 0.0)
            f0 *= std::pow (2.0, s.vibratoCents / 1200.0 * std::sin (2.0 * M_PI * s.vibratoHz * t));
        double v = 0.0;
        for (int h = 1; h < 64; ++h)
        {
            const double fh = f0 * h;
            if (fh > s.sampleRate * 0.45)
                break;
            phase[(size_t) h] += 2.0 * M_PI * fh / s.sampleRate;
            v += formantEnvelope (s, fh) * std::sin (phase[(size_t) h]);
        }
        out[(size_t) i] = (float) (s.amplitude * v / norm * 4.0);
    }
    return out;
}

inline std::vector<float> makeSine (double sr, double freq, double seconds, double amp = 0.5)
{
    std::vector<float> out ((size_t) (seconds * sr));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (float) (amp * std::sin (2.0 * M_PI * freq * (double) i / sr));
    return out;
}

/** Magnitude of the DFT at one frequency (Goertzel), normalised by length. */
inline double toneMagnitude (const float* x, int n, double sr, double freq)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < n; ++i)
    {
        // Hann window to limit leakage
        const double w = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / (n - 1));
        const double ph = 2.0 * M_PI * freq * i / sr;
        re += w * x[i] * std::cos (ph);
        im -= w * x[i] * std::sin (ph);
    }
    return std::sqrt (re * re + im * im) / n;
}

inline double rms (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += (double) x[i] * x[i];
    return std::sqrt (s / std::max (1, n));
}
} // namespace vcstest
