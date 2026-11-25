#pragma once
// YIN fundamental-frequency estimator (de Cheveigné & Kawahara, 2002).
//
// The squared-difference function is computed with an FFT cross-correlation, so
// one analysis costs three FFTs instead of O(W * tauMax) multiply-adds. All
// memory is allocated in prepare(); analyse() is real-time safe.

#include "vcs/Fft.h"

#include <complex>
#include <vector>

namespace vcs
{
struct PitchEstimate
{
    float f0Hz = 0.0f;       ///< Estimated fundamental (0 when unvoiced)
    float confidence = 0.0f; ///< 1 - normalised difference at the chosen lag (0..1)
    bool voiced = false;
};

class YinPitchDetector
{
public:
    struct Config
    {
        double sampleRate = 48000.0;
        float minF0 = 80.0f;     ///< Lowest pitch tracked (defines the longest lag)
        float maxF0 = 1100.0f;   ///< Highest pitch tracked
        float threshold = 0.15f; ///< YIN absolute threshold on the CMNDF
        float silenceDb = -55.0f; ///< Frames quieter than this (RMS) are unvoiced
    };

    void prepare (const Config& config);

    /** Number of samples analyse() reads: integration window + longest lag. */
    int frameLength() const noexcept { return integrationWindow + maxLag; }

    int maxLagSamples() const noexcept { return maxLag; }

    /** Analyses frameLength() consecutive samples. */
    PitchEstimate analyse (const float* frame) noexcept;

    /** Cumulative-mean-normalised difference from the last analyse() (for tests/visualisation). */
    const std::vector<float>& lastCmndf() const noexcept { return cmndf; }

private:
    Config cfg;
    int integrationWindow = 0;
    int maxLag = 0;
    int minLag = 0;

    Fft fft;
    std::vector<std::complex<float>> bufA, bufB;
    std::vector<double> prefixEnergy;
    std::vector<float> diff, cmndf;
};
} // namespace vcs
