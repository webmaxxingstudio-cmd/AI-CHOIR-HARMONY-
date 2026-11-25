#include "vcs/YinPitchDetector.h"

#include <algorithm>
#include <cmath>

namespace vcs
{
void YinPitchDetector::prepare (const Config& config)
{
    cfg = config;
    maxLag = (int) std::ceil (cfg.sampleRate / cfg.minF0) + 2;
    minLag = std::max (2, (int) std::floor (cfg.sampleRate / cfg.maxF0));
    // Integrate over one longest period: enough for a stable estimate at the
    // lowest pitch while keeping latency low at high pitches.
    integrationWindow = maxLag;

    int order = 1;
    while ((1 << order) < integrationWindow + maxLag)
        ++order;
    fft.prepare (order);

    bufA.assign ((size_t) fft.size(), {});
    bufB.assign ((size_t) fft.size(), {});
    prefixEnergy.assign ((size_t) (integrationWindow + maxLag + 1), 0.0);
    diff.assign ((size_t) maxLag, 0.0f);
    cmndf.assign ((size_t) maxLag, 1.0f);
}

PitchEstimate YinPitchDetector::analyse (const float* x) noexcept
{
    const int W = integrationWindow;
    const int total = W + maxLag;
    const int N = fft.size();

    // Energy prefix sums -> sum_{j<W} x[j+tau]^2 in O(1) per lag.
    prefixEnergy[0] = 0.0;
    for (int i = 0; i < total; ++i)
        prefixEnergy[(size_t) i + 1] = prefixEnergy[(size_t) i] + (double) x[i] * (double) x[i];

    const double rms = std::sqrt (prefixEnergy[(size_t) total] / total);
    const double silence = std::pow (10.0, cfg.silenceDb / 20.0);
    if (rms < silence)
        return {};

    // Cross term c(tau) = sum_{j<W} x[j] x[j+tau] via circular correlation.
    // With N >= W + maxLag no wrap-around reaches the lags we use.
    for (int i = 0; i < N; ++i)
    {
        bufA[(size_t) i] = { i < W ? x[i] : 0.0f, 0.0f };
        bufB[(size_t) i] = { i < total ? x[i] : 0.0f, 0.0f };
    }
    fft.forward (bufA.data());
    fft.forward (bufB.data());
    for (int i = 0; i < N; ++i)
        bufB[(size_t) i] = std::conj (bufA[(size_t) i]) * bufB[(size_t) i];
    fft.inverse (bufB.data());

    const double e0 = prefixEnergy[(size_t) W];
    diff[0] = 0.0f;
    cmndf[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau < maxLag; ++tau)
    {
        const double eTau = prefixEnergy[(size_t) (tau + W)] - prefixEnergy[(size_t) tau];
        const double d = std::max (0.0, e0 + eTau - 2.0 * (double) bufB[(size_t) tau].real());
        diff[(size_t) tau] = (float) d;
        running += d;
        cmndf[(size_t) tau] = running > 0.0 ? (float) (d * tau / running) : 1.0f;
    }

    // First dip under the threshold, then walk down to its local minimum.
    int best = -1;
    for (int tau = minLag; tau < maxLag - 1; ++tau)
    {
        if (cmndf[(size_t) tau] < cfg.threshold)
        {
            while (tau + 1 < maxLag - 1 && cmndf[(size_t) tau + 1] < cmndf[(size_t) tau])
                ++tau;
            best = tau;
            break;
        }
    }

    bool passedThreshold = best >= 0;
    if (! passedThreshold)
    {
        best = minLag;
        for (int tau = minLag + 1; tau < maxLag - 1; ++tau)
            if (cmndf[(size_t) tau] < cmndf[(size_t) best])
                best = tau;
    }

    // Parabolic interpolation around the minimum for sub-sample lag accuracy.
    double lag = best;
    if (best > 1 && best < maxLag - 1)
    {
        const double a = cmndf[(size_t) best - 1], b = cmndf[(size_t) best], c = cmndf[(size_t) best + 1];
        const double denom = a - 2.0 * b + c;
        if (std::abs (denom) > 1e-12)
            lag += std::clamp (0.5 * (a - c) / denom, -0.5, 0.5);
    }

    PitchEstimate e;
    e.confidence = std::clamp (1.0f - cmndf[(size_t) best], 0.0f, 1.0f);
    // Without a threshold crossing we still accept clearly periodic frames
    // (breathy voice), but with a stricter bound.
    e.voiced = passedThreshold || cmndf[(size_t) best] < 0.3f;
    e.f0Hz = e.voiced ? (float) (cfg.sampleRate / lag) : 0.0f;
    return e;
}
} // namespace vcs
