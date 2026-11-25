#include "TestFramework.h"
#include "TestSignals.h"

#include "vcs/Fft.h"
#include "vcs/PsolaVoice.h"
#include "vcs/VocalAnalyzer.h"
#include "vcs/YinPitchDetector.h"

#include <random>

using namespace vcstest;

// ---------------------------------------------------------------- FFT
TEST_CASE ("FFT round trip and single-bin sinusoid")
{
    vcs::Fft fft;
    fft.prepare (10);
    const int n = fft.size();
    std::vector<std::complex<float>> d ((size_t) n), orig;
    std::mt19937 rng (1);
    std::uniform_real_distribution<float> u (-1.f, 1.f);
    for (auto& v : d)
        v = { u (rng), u (rng) };
    orig = d;
    fft.forward (d.data());
    fft.inverse (d.data());
    double err = 0;
    for (int i = 0; i < n; ++i)
        err = std::max (err, (double) std::abs (d[(size_t) i] - orig[(size_t) i]));
    CHECK (err < 1e-5);

    for (int i = 0; i < n; ++i)
        d[(size_t) i] = { (float) std::cos (2 * M_PI * 37 * i / n), 0.0f };
    fft.forward (d.data());
    CHECK_NEAR (std::abs (d[37]), n / 2.0, 1e-2 * n);
    CHECK (std::abs (d[36]) < 1e-3 * n);
}

// ---------------------------------------------------------------- YIN
static vcs::PitchEstimate yinOn (const std::vector<float>& x, double sr, int offset = 4000)
{
    vcs::YinPitchDetector yin;
    vcs::YinPitchDetector::Config c;
    c.sampleRate = sr;
    yin.prepare (c);
    return yin.analyse (x.data() + offset);
}

TEST_CASE ("YIN: sine accuracy across the vocal range and sample rates")
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        for (double f : { 82.0, 110.0, 220.0, 440.0, 660.0, 1000.0 })
        {
            const auto e = yinOn (makeSine (sr, f, 0.3), sr);
            CHECK (e.voiced);
            const double cents = 1200.0 * std::log2 (e.f0Hz / f);
            if (std::abs (cents) > 3.0)
                INFO ("sr=%g f=%g got %g (%g cents)", sr, f, e.f0Hz, cents);
            CHECK (std::abs (cents) < 3.0);
        }
    }
}

TEST_CASE ("YIN: harmonic-rich vowel has no octave errors")
{
    for (double f : { 98.0, 147.0, 220.0, 330.0, 523.0 })
    {
        VowelSpec s;
        s.f0 = f;
        s.seconds = 0.3;
        const auto e = yinOn (makeVowel (s), s.sampleRate);
        CHECK (e.voiced);
        const double cents = 1200.0 * std::log2 (e.f0Hz / f);
        if (std::abs (cents) > 5.0)
            INFO ("f=%g got %g", f, e.f0Hz);
        CHECK (std::abs (cents) < 5.0);
    }
}

TEST_CASE ("YIN: silence and white noise are unvoiced")
{
    std::vector<float> silence (20000, 0.0f);
    CHECK (! yinOn (silence, 48000).voiced);

    std::mt19937 rng (7);
    std::normal_distribution<float> g (0.f, 0.2f);
    std::vector<float> noise (20000);
    for (auto& v : noise)
        v = g (rng);
    CHECK (! yinOn (noise, 48000).voiced);
}

// ---------------------------------------------------------------- Analyzer
static void runAnalyzer (vcs::VocalAnalyzer& a, const std::vector<float>& x, int block = 256)
{
    for (size_t i = 0; i < x.size(); i += (size_t) block)
        a.push (x.data() + i, (int) std::min<size_t> ((size_t) block, x.size() - i));
}

TEST_CASE ("Analyzer: marks are one period apart on a steady vowel")
{
    vcs::VocalAnalyzer a;
    vcs::VocalAnalyzer::Config c;
    a.prepare (c);
    VowelSpec s;
    s.f0 = 196.0; // G3
    const auto x = makeVowel (s);
    runAnalyzer (a, x);

    const double T = 48000.0 / 196.0;
    vcs::AnalysisMark m1, m2;
    CHECK (a.nearestMark (30000, m1));
    CHECK (a.nearestMark (m1.pos + T, m2));
    CHECK (m1.voiced && m2.voiced);
    CHECK_NEAR ((double) (m2.pos - m1.pos), T, 2.0);
    CHECK_NEAR (m1.period, T, 0.5);
    CHECK (m1.note == 55);
    CHECK (a.stableNote() == 55);
}

TEST_CASE ("Analyzer: note tracker ignores vibrato but follows a real note change")
{
    vcs::VocalAnalyzer a;
    vcs::VocalAnalyzer::Config c;
    a.prepare (c);
    VowelSpec s;
    s.seconds = 2.0;
    s.vibratoHz = 5.5;
    s.vibratoCents = 60.0; // a wide, operatic vibrato
    std::vector<double> track ((size_t) (s.seconds * s.sampleRate));
    for (size_t i = 0; i < track.size(); ++i)
        track[i] = i < track.size() / 2 ? vcs::midiToHz (57) : vcs::midiToHz (60); // A3 -> C4
    const auto x = makeVowel (s, &track);

    int changesFirstHalf = 0, lastNote = -1;
    for (size_t i = 0; i < x.size(); i += 256)
    {
        a.push (x.data() + i, 256);
        const int n = a.stableNote();
        if (i < track.size() / 2 && i > 9600 && n != lastNote && lastNote >= 0)
            ++changesFirstHalf;
        lastNote = n;
    }
    CHECK (changesFirstHalf == 0);
    CHECK (a.stableNote() == 60);
}

// ---------------------------------------------------------------- PSOLA
struct FixedController : vcs::GrainController
{
    float ratio = 1.0f, formant = 1.0f;
    double timingOffset (int64_t) noexcept override { return 0.0; }
    vcs::GrainShape shapeFor (const vcs::AnalysisMark&, int64_t) noexcept override
    {
        return { ratio, formant, 1.0f };
    }
};

static std::vector<float> renderShift (const std::vector<float>& x, float ratio, float formant,
                                       double sr = 48000.0, int* latencyOut = nullptr)
{
    vcs::VocalAnalyzer a;
    vcs::VocalAnalyzer::Config c;
    c.sampleRate = sr;
    a.prepare (c);
    const int latency = vcs::PsolaVoice::requiredLatency (a, c.hop);
    if (latencyOut)
        *latencyOut = latency;
    vcs::PsolaVoice v;
    v.prepare (a, latency);
    FixedController ctl;
    ctl.ratio = ratio;
    ctl.formant = formant;

    std::vector<float> out (x.size(), 0.0f);
    const int block = 256;
    for (size_t i = 0; i < x.size(); i += block)
    {
        const int n = (int) std::min<size_t> (block, x.size() - i);
        a.push (x.data() + i, n);
        v.render (a, (int64_t) i, out.data() + i, n, ctl);
    }
    return out;
}

TEST_CASE ("PSOLA: shifts pitch accurately over +-12 semitones")
{
    VowelSpec s;
    s.f0 = 220.0;
    s.seconds = 1.0;
    const auto x = makeVowel (s);
    for (int semis : { -12, -7, -3, 3, 4, 7, 12 })
    {
        const float ratio = std::exp2 (semis / 12.0f);
        const auto y = renderShift (x, ratio, 1.0f);
        const auto e = yinOn (y, s.sampleRate, 30000);
        const double expected = s.f0 * ratio;
        const double cents = 1200.0 * std::log2 (e.f0Hz / expected);
        if (std::abs (cents) > 5.0)
            INFO ("semis=%d expected %g got %g", semis, expected, e.f0Hz);
        CHECK (e.voiced);
        CHECK (std::abs (cents) < 5.0);
    }
}

TEST_CASE ("PSOLA: ratio 1 is transparent and delayed by exactly the reported latency")
{
    VowelSpec s;
    s.f0 = 180.0;
    const auto x = makeVowel (s);
    int latency = 0;
    const auto y = renderShift (x, 1.0f, 1.0f, 48000.0, &latency);
    double err = 0, ref = 0;
    for (size_t i = 20000; i < 40000; ++i)
    {
        const double d = y[i] - x[i - (size_t) latency];
        err += d * d;
        ref += (double) x[i - (size_t) latency] * x[i - (size_t) latency];
    }
    const double snrDb = 10.0 * std::log10 (ref / std::max (err, 1e-20));
    INFO ("latency %d samples (%.1f ms), unity SNR %.1f dB", latency, latency / 48.0, snrDb);
    CHECK (snrDb > 30.0);
    CHECK (latency < 48 * 45); // under 45 ms at 48 kHz
}

TEST_CASE ("PSOLA: loudness is stable when shifting")
{
    VowelSpec s;
    const auto x = makeVowel (s);
    const double inRms = rms (x.data() + 20000, 20000);
    for (int semis : { -12, -5, 5, 12 })
    {
        const auto y = renderShift (x, std::exp2 (semis / 12.0f), 1.0f);
        const double db = 20.0 * std::log10 (rms (y.data() + 20000, 20000) / inRms);
        if (std::abs (db) > 2.0)
            INFO ("semis=%d level change %.2f dB", semis, db);
        CHECK (std::abs (db) < 2.0);
    }
}

// Power-weighted centroid of the harmonics between 300 and 1800 Hz: tracks
// where the first formant sits independently of f0.
static double formantCentroid (const std::vector<float>& y, double sr, double f0)
{
    double num = 0, den = 0;
    for (int h = 1; h * f0 < 1800.0; ++h)
    {
        if (h * f0 < 300.0)
            continue;
        const double m = toneMagnitude (y.data() + 20000, 16384, sr, h * f0);
        num += m * m * h * f0;
        den += m * m;
    }
    return num / den;
}

TEST_CASE ("PSOLA: formants are preserved when shifting, and movable on demand")
{
    VowelSpec s;
    s.f0 = 200.0;
    s.formants[0] = 900.0;
    s.formants[1] = 2600.0;
    s.formants[2] = 3500.0;
    const auto x = makeVowel (s);
    const double cIn = formantCentroid (x, s.sampleRate, s.f0);

    for (int semis : { -7, 5, 12 })
    {
        const float ratio = std::exp2 (semis / 12.0f);
        const double f0 = s.f0 * ratio;
        const auto y = renderShift (x, ratio, 1.0f);
        const double c = formantCentroid (y, s.sampleRate, f0);
        // Reference: the same vowel actually sung at the new pitch.
        VowelSpec r = s;
        r.f0 = f0;
        const double cRef = formantCentroid (makeVowel (r), s.sampleRate, f0);
        INFO ("F1 centroid after %+d st: %.0f Hz, same vowel sung at that pitch %.0f Hz, plain resampling ~%.0f Hz",
              semis, c, cRef, cIn * ratio);
        CHECK (std::abs (std::log2 (c / cRef)) < 0.1);
    }

    // Asking for +7 semitones of formant moves the envelope up by about 1.5x.
    const float ratio = std::exp2 (5.0f / 12.0f);
    const auto z = renderShift (x, ratio, std::exp2 (7.0f / 12.0f));
    const double c2 = formantCentroid (z, s.sampleRate, s.f0 * ratio);
    INFO ("with formant +7 st: %.0f Hz", c2);
    CHECK (c2 / cIn > 1.3 && c2 / cIn < 1.7);
}
