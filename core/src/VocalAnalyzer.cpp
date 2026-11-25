#include "vcs/VocalAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace vcs
{
namespace
{
    int64_t nextPow2 (int64_t v)
    {
        int64_t p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    float median3 (float a, float b, float c)
    {
        return std::max (std::min (a, b), std::min (std::max (a, b), c));
    }
} // namespace

void VocalAnalyzer::prepare (const Config& config)
{
    cfg = config;

    YinPitchDetector::Config yc;
    yc.sampleRate = cfg.sampleRate;
    yc.minF0 = cfg.minF0;
    yc.maxF0 = cfg.maxF0;
    yin.prepare (yc);

    maxPeriodSamples = (int) std::ceil (cfg.sampleRate / cfg.minF0);
    unvoicedSpacing = (int) std::lround (cfg.sampleRate * 0.005);

    const int64_t histLen = nextPow2 (std::max<int64_t> ((int64_t) (cfg.historySeconds * cfg.sampleRate),
                                                         (int64_t) yin.frameLength() * 4));
    history.assign ((size_t) histLen, 0.0f);
    historyMask = histLen - 1;
    frame.assign ((size_t) yin.frameLength(), 0.0f);

    const int minSpacing = std::max (8, (int) std::floor (cfg.sampleRate / cfg.maxF0) - 2);
    const int64_t markCap = nextPow2 (histLen / minSpacing + 64);
    marks.assign ((size_t) markCap, {});
    marksMask = markCap - 1;

    reset();
}

void VocalAnalyzer::reset()
{
    std::fill (history.begin(), history.end(), 0.0f);
    written = 0;
    rawCount = 0;
    smoothed = {};
    noteTracker.reset();
    marksPlaced = 0;
    haveLastMark = false;
    keyDet.reset();
    keyKnown = false;
    currentKey = {};
    candidateKey = {};
    candidateSeconds = 0.0f;
    keyEvalTimer = 0.0f;
}

float VocalAnalyzer::sampleAt (int64_t pos) const noexcept
{
    if (pos < 0 || pos >= written || pos < written - (int64_t) history.size())
        return 0.0f;
    return history[(size_t) (pos & historyMask)];
}

float VocalAnalyzer::interpolatedAt (double pos) const noexcept
{
    const double fl = std::floor (pos);
    const auto i = (int64_t) fl;
    const float t = (float) (pos - fl);
    const float y0 = sampleAt (i - 1), y1 = sampleAt (i), y2 = sampleAt (i + 1), y3 = sampleAt (i + 2);
    // Catmull-Rom spline
    const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c = -0.5f * y0 + 0.5f * y2;
    return ((a * t + b) * t + c) * t + y1;
}

void VocalAnalyzer::push (const float* input, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        history[(size_t) (written & historyMask)] = input[i];
        ++written;
        if (written % cfg.hop == 0)
        {
            runPitchAnalysis();
            updateKey ((float) cfg.hop / (float) cfg.sampleRate);
            placeMarks();
        }
    }
}

void VocalAnalyzer::runPitchAnalysis() noexcept
{
    const int len = yin.frameLength();
    if (written < len)
        return;
    const int64_t start = written - len;
    for (int i = 0; i < len; ++i)
        frame[(size_t) i] = history[(size_t) ((start + i) & historyMask)];

    raw[0] = raw[1];
    raw[1] = raw[2];
    raw[2] = yin.analyse (frame.data());
    rawCount = std::min (rawCount + 1, 3);

    // Median of three voiced estimates removes isolated octave errors.
    smoothed = raw[2];
    if (rawCount == 3 && raw[0].voiced && raw[1].voiced && raw[2].voiced)
        smoothed.f0Hz = median3 (raw[0].f0Hz, raw[1].f0Hz, raw[2].f0Hz);

    const float hopSec = (float) cfg.hop / (float) cfg.sampleRate;
    noteTracker.update (smoothed.voiced ? hzToMidi (smoothed.f0Hz) : 0.0f, smoothed.voiced, hopSec);
}

void VocalAnalyzer::updateKey (float hopSeconds) noexcept
{
    keyDet.decay (std::exp (-hopSeconds / (float) cfg.keyMemorySeconds));
    if (smoothed.voiced)
        keyDet.addObservation (hzToMidi (smoothed.f0Hz), hopSeconds * smoothed.confidence);

    keyEvalTimer += hopSeconds;
    if (keyEvalTimer < 0.25f)
        return;
    const float dt = keyEvalTimer;
    keyEvalTimer = 0.0f;

    const auto r = keyDet.estimate();
    // Need a few seconds of singing before trusting the histogram.
    if (r.totalWeight < 3.0f || r.margin < 0.01f)
        return;

    if (! keyKnown)
    {
        currentKey = r.key;
        keyKnown = true;
        return;
    }

    if (r.key == currentKey)
    {
        candidateSeconds = 0.0f;
        return;
    }

    // Hysteresis: a different key must win consistently for 2 s before switching.
    if (r.key == candidateKey)
        candidateSeconds += dt;
    else
    {
        candidateKey = r.key;
        candidateSeconds = dt;
    }
    if (candidateSeconds >= 2.0f)
    {
        currentKey = candidateKey;
        candidateSeconds = 0.0f;
    }
}

int64_t VocalAnalyzer::refinePeak (int64_t centre, int radius) const noexcept
{
    int64_t best = centre;
    float bestVal = sampleAt (centre);
    for (int64_t p = centre - radius; p <= centre + radius; ++p)
    {
        const float v = sampleAt (p);
        if (v > bestVal)
        {
            bestVal = v;
            best = p;
        }
    }
    return best;
}

void VocalAnalyzer::placeMarks() noexcept
{
    // A mark at p needs samples up to p + period for its grain.
    const int64_t horizon = written - maxPeriodSamples - 1;
    const bool voiced = smoothed.voiced;
    const float period = voiced ? (float) (cfg.sampleRate / smoothed.f0Hz) : (float) unvoicedSpacing;
    const float midi = voiced ? hzToMidi (smoothed.f0Hz) : 0.0f;

    for (;;)
    {
        int64_t pos;
        if (! haveLastMark)
        {
            pos = std::max<int64_t> (maxPeriodSamples, horizon - (int64_t) period);
            if (pos > horizon)
                return;
            if (voiced)
                pos = refinePeak (pos + (int64_t) (period * 0.5f), (int) (period * 0.5f));
        }
        else
        {
            const float step = voiced ? period : (lastMark.voiced ? std::min (lastMark.period, period) : period);
            int64_t predicted = lastMark.pos + std::max<int64_t> (1, (int64_t) std::lround (step));
            if (predicted > horizon)
                return;
            if (voiced)
            {
                // Entering a voiced region: lock onto the largest peak of the
                // first period. Inside one: small correction around the prediction.
                if (! lastMark.voiced)
                    predicted = refinePeak (predicted + (int64_t) (period * 0.5f), (int) (period * 0.5f));
                else
                    predicted = refinePeak (predicted, std::max (1, (int) (period * 0.125f)));
                predicted = std::max (predicted, lastMark.pos + std::max<int64_t> (1, (int64_t) (period * 0.5f)));
            }
            pos = predicted;
        }

        if (pos > horizon)
            return;

        AnalysisMark m;
        m.pos = pos;
        m.period = period;
        m.midi = midi;
        m.voiced = voiced;
        m.note = noteTracker.currentNote();
        marks[(size_t) (marksPlaced & marksMask)] = m;
        ++marksPlaced;
        lastMark = m;
        haveLastMark = true;
    }
}

bool VocalAnalyzer::nearestMark (double pos, AnalysisMark& out) const noexcept
{
    if (marksPlaced == 0)
        return false;

    const int64_t cap = (int64_t) marks.size();
    int64_t lo = std::max<int64_t> (0, marksPlaced - cap + 1); // oldest valid
    int64_t hi = marksPlaced - 1;

    const auto at = [this] (int64_t idx) -> const AnalysisMark& { return marks[(size_t) (idx & marksMask)]; };

    if (pos <= (double) at (lo).pos) { out = at (lo); return true; }
    if (pos >= (double) at (hi).pos) { out = at (hi); return true; }

    // Last mark with position <= pos.
    while (hi - lo > 1)
    {
        const int64_t mid = (lo + hi) / 2;
        if ((double) at (mid).pos <= pos)
            lo = mid;
        else
            hi = mid;
    }
    const auto& a = at (lo);
    const auto& b = at (hi);
    out = (pos - (double) a.pos <= (double) b.pos - pos) ? a : b;
    return true;
}
} // namespace vcs
