#include "vcs/PsolaVoice.h"

#include <algorithm>
#include <cmath>

namespace vcs
{
namespace
{
    int maxGrainHalf (const VocalAnalyzer& a) noexcept
    {
        return (int) std::ceil ((float) a.maxPeriod() / PsolaVoice::kMinFormant) + 2;
    }
} // namespace

int PsolaVoice::requiredLatency (const VocalAnalyzer& analyzer, int hop) noexcept
{
    // A grain is scheduled when its left edge (centre - maxHalf) enters the
    // block being rendered. At that moment the mark it reads (plus one period
    // to its right) must already be placed, and marks trail the input by one
    // max period plus up to one analysis hop.
    return maxGrainHalf (analyzer) + (3 * analyzer.maxPeriod()) / 2 + hop;
}

void PsolaVoice::prepare (const VocalAnalyzer& analyzer, int latencySamples)
{
    latency = latencySamples;
    maxHalf = maxGrainHalf (analyzer);
    int64_t size = 1;
    while (size < (int64_t) (4 * maxHalf + 8192))
        size <<= 1;
    acc.assign ((size_t) size, 0.0f);
    accMask = size - 1;
    reset();
}

void PsolaVoice::reset() noexcept
{
    std::fill (acc.begin(), acc.end(), 0.0f);
    started = false;
    nextSynth = 0.0;
}

void PsolaVoice::addGrain (const VocalAnalyzer& analyzer, const AnalysisMark& mark, double centre,
                           const GrainShape& shape) noexcept
{
    const float f = std::clamp (shape.formantFactor, kMinFormant, kMaxFormant);
    const double Ta = mark.period;
    const double half = std::min ((double) maxHalf, Ta / f);
    const auto first = (int64_t) std::ceil (centre - half);
    const auto last = (int64_t) std::floor (centre + half);
    const double invTa = M_PI / Ta;

    for (int64_t p = first; p <= last; ++p)
    {
        const double u = ((double) p - centre) * f; // offset in the input, samples
        const float w = 0.5f * (1.0f + (float) std::cos (u * invTa));
        const float x = analyzer.interpolatedAt ((double) mark.pos + u);
        acc[(size_t) (p & accMask)] += shape.gain * w * x;
    }
}

void PsolaVoice::render (const VocalAnalyzer& analyzer, int64_t t0, float* out, int numSamples,
                         GrainController& controller) noexcept
{
    const int64_t end = t0 + numSamples;
    if (! started)
    {
        nextSynth = (double) (t0 + maxHalf);
        started = true;
    }

    while (nextSynth - maxHalf < (double) end)
    {
        const double delay = std::max (0.0, controller.timingOffset ((int64_t) nextSynth));
        AnalysisMark mark;
        if (! analyzer.nearestMark (nextSynth - latency - delay, mark))
        {
            nextSynth += 64.0;
            continue;
        }

        GrainShape shape = controller.shapeFor (mark, (int64_t) nextSynth);
        double centre = nextSynth;
        const bool unison = mark.voiced && std::abs (shape.pitchRatio - 1.0f) < 3.0e-4f;

        if (unison)
        {
            // Unison: lock the synthesis grid to the analysis grid so the output
            // is a sample-accurate (delayed) copy of the input. Otherwise a
            // doubled voice would sit up to half a period out of phase with the
            // lead and comb-filter against it. Never move backwards in time:
            // grains left of nextSynth may already have been emitted.
            double aligned = (double) mark.pos + latency + delay;
            if (aligned < nextSynth - 0.5)
            {
                AnalysisMark next;
                if (analyzer.nearestMark ((double) mark.pos + mark.period, next) && next.pos > mark.pos)
                {
                    mark = next;
                    shape = controller.shapeFor (mark, (int64_t) nextSynth);
                    aligned = (double) mark.pos + latency + delay;
                }
            }
            if (aligned >= nextSynth - 0.5)
                centre = aligned;
        }

        const float f = std::clamp (shape.formantFactor, kMinFormant, kMaxFormant);
        double spacing;
        if (mark.voiced)
        {
            const float ratio = std::clamp (shape.pitchRatio, 0.25f, 4.0f);
            spacing = mark.period / ratio;
            // Overlap-add of pulse-synchronous grains raises power in
            // proportion to the pulse rate; compensate so loudness stays
            // constant when shifting (see docs/DSP.md, "PSOLA gain").
            shape.gain *= 1.0f / std::sqrt (ratio);
        }
        else
        {
            // Noise: keep the overlap of the (formant-scaled) grains constant
            // so there is no amplitude ripple at the grain rate.
            spacing = mark.period / f;
        }

        addGrain (analyzer, mark, centre, shape);
        // In unison mode only advance half a period: the next iteration then
        // snaps exactly onto the following analysis mark, whatever its spacing.
        nextSynth = centre + std::max (1.0, unison ? 0.5 * mark.period : spacing);
    }

    for (int64_t p = t0; p < end; ++p)
    {
        float& a = acc[(size_t) (p & accMask)];
        out[p - t0] += a;
        a = 0.0f;
    }
}
} // namespace vcs
