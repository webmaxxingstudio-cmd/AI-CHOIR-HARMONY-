#include "TestFramework.h"
#include "TestSignals.h"

#include "vcs/ChoirEngine.h"
#include "vcs/MusicTheory.h"

#include <atomic>

namespace vcstest
{
extern std::atomic<long> gAllocations;
}
using namespace vcstest;
using vcs::Key;
using vcs::ScaleType;

// ---------------------------------------------------------------- theory
TEST_CASE ("Diatonic transposition follows the key")
{
    const Key cMajor { 0, ScaleType::Major };
    CHECK (vcs::diatonicTranspose (cMajor, 60, 2) == 64);  // C -> E (major 3rd)
    CHECK (vcs::diatonicTranspose (cMajor, 62, 2) == 65);  // D -> F (minor 3rd)
    CHECK (vcs::diatonicTranspose (cMajor, 71, 2) == 74);  // B -> D
    CHECK (vcs::diatonicTranspose (cMajor, 64, -3) == 59); // E -> B below (4th below)
    CHECK (vcs::diatonicTranspose (cMajor, 60, -7) == 48); // octave down
    CHECK (vcs::diatonicTranspose (cMajor, 67, 4) == 74);  // G -> D (5th)
    CHECK (vcs::diatonicTranspose (cMajor, 61, 2) == 65);  // C# (passing) moves with C: -> F (E + 1)

    const Key aMinor { 9, ScaleType::NaturalMinor };
    CHECK (vcs::diatonicTranspose (aMinor, 69, 2) == 72); // A -> C
    CHECK (vcs::diatonicTranspose (aMinor, 64, 2) == 67); // E -> G

    const Key aHarm { 9, ScaleType::HarmonicMinor };
    CHECK (vcs::diatonicTranspose (aHarm, 64, 2) == 68); // E -> G#

    const Key gMajor { 7, ScaleType::Major };
    CHECK (vcs::diatonicTranspose (gMajor, 71, 2) == 74); // B -> D
    CHECK (vcs::diatonicTranspose (gMajor, 72, 2) == 76); // C -> E
    CHECK (vcs::diatonicTranspose (gMajor, 74, 2) == 78); // D -> F#

    const Key chrom { 0, ScaleType::Chromatic };
    CHECK (vcs::diatonicTranspose (chrom, 62, 2) == 66); // fixed major 3rd
    CHECK (vcs::diatonicTranspose (chrom, 62, -3) == 57); // fixed 4th below
}

static void feedMelody (vcs::KeyDetector& k, std::initializer_list<int> notes, float noteSeconds = 0.4f)
{
    for (int n : notes)
        k.addObservation ((float) n + 0.1f, noteSeconds);
}

TEST_CASE ("Key detection: major, minor, harmonic minor")
{
    {
        vcs::KeyDetector k; // "Ode to Joy" style melody in G major, ends on the tonic
        feedMelody (k, { 71, 71, 72, 74, 74, 72, 71, 69, 67, 67, 69, 71, 71, 69, 69,
                         71, 71, 72, 74, 74, 72, 71, 69, 67, 67, 69, 71, 69, 67, 67, 66, 67 });
        const auto r = k.estimate();
        INFO ("G major melody -> %s %s (score %.2f)", vcs::pitchClassName (r.key.root), vcs::scaleName (r.key.scale), r.score);
        CHECK (r.key.root == 7 && r.key.scale == ScaleType::Major);
    }
    {
        vcs::KeyDetector k; // D minor melody, tonic and dominant emphasised
        feedMelody (k, { 62, 62, 65, 69, 69, 67, 65, 64, 62, 62, 57, 62, 65, 64, 62, 60, 62, 69, 65, 62 });
        const auto r = k.estimate();
        INFO ("D minor melody -> %s %s", vcs::pitchClassName (r.key.root), vcs::scaleName (r.key.scale));
        CHECK (r.key.root == 2 && r.key.scale == ScaleType::NaturalMinor);
    }
    {
        vcs::KeyDetector k; // E minor with raised leading tone D#
        feedMelody (k, { 64, 66, 67, 71, 67, 66, 63, 64, 64, 71, 72, 71, 67, 66, 63, 64, 59, 64, 67, 64 });
        const auto r = k.estimate();
        INFO ("E harmonic minor melody -> %s %s", vcs::pitchClassName (r.key.root), vcs::scaleName (r.key.scale));
        CHECK (r.key.root == 4 && r.key.scale == ScaleType::HarmonicMinor);
    }
}

// ---------------------------------------------------------------- choir engine
static std::vector<double> stepTrack (std::initializer_list<int> notes, double secondsEach, double sr)
{
    std::vector<double> t;
    for (int n : notes)
        for (int i = 0; i < (int) (secondsEach * sr); ++i)
            t.push_back (vcs::midiToHz ((float) n));
    return t;
}

struct Rendered
{
    std::vector<float> l, r;
};

static Rendered runEngine (vcs::ChoirEngine& e, const std::vector<float>& x, int block = 480)
{
    Rendered out;
    out.l.resize (x.size());
    out.r.resize (x.size());
    for (size_t i = 0; i < x.size(); i += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, x.size() - i);
        e.process (x.data() + i, out.l.data() + i, out.r.data() + i, n);
    }
    return out;
}

TEST_CASE ("Choir: lead path is a sample-exact delayed copy when the choir is muted")
{
    vcs::ChoirEngine e;
    e.prepare (48000, 512);
    vcs::ChoirSettings s;
    s.choirGainDb = -120.0f;
    s.leadGainDb = 0.0f;
    e.setSettings (s);
    e.reset();
    VowelSpec v;
    const auto x = makeVowel (v);
    const auto y = runEngine (e, x);
    const int L = e.latencySamples();
    double err = 0;
    for (size_t i = (size_t) L + 1000; i < x.size(); ++i)
        err = std::max (err, (double) std::abs (y.l[i] - x[i - (size_t) L]));
    CHECK (err < 1e-6);
}

TEST_CASE ("Choir: 2-part harmony in C major puts a diatonic third above the lead")
{
    vcs::ChoirEngine e;
    e.prepare (48000, 512);
    vcs::ChoirSettings s;
    s.arrangement = 1; // 3rd above, then unison
    s.singers = 1;
    s.autoKey = false;
    s.manualKey = { 0, ScaleType::Major };
    s.leadGainDb = -120.0f;
    s.choirGainDb = 0.0f;
    s.humanize = 0.0f;
    s.correction = 1.0f;
    e.setSettings (s);
    e.reset();

    // Sing D4 (62) then E4 (64): harmony must be F4 (minor 3rd) then G4 (major 3rd).
    VowelSpec v;
    v.seconds = 2.0;
    const auto track = stepTrack ({ 62, 64 }, 1.0, v.sampleRate);
    const auto x = makeVowel (v, &track);
    const auto y = runEngine (e, x);

    vcs::YinPitchDetector yin;
    yin.prepare ({});
    const auto p1 = yin.analyse (y.l.data() + 30000);
    const auto p2 = yin.analyse (y.l.data() + 80000);
    INFO ("harmony over D4: %.1f Hz (F4 = 349.2), over E4: %.1f Hz (G4 = 392.0)", p1.f0Hz, p2.f0Hz);
    CHECK (std::abs (vcs::hzToMidi (p1.f0Hz) - 65.0f) < 0.1f);
    CHECK (std::abs (vcs::hzToMidi (p2.f0Hz) - 67.0f) < 0.1f);
}

TEST_CASE ("Choir: auto key detection drives the harmony")
{
    vcs::ChoirEngine e;
    e.prepare (48000, 512);
    vcs::ChoirSettings s;
    s.arrangement = 1;
    s.singers = 1;
    s.autoKey = true;
    s.humanize = 0.0f;
    e.setSettings (s);
    e.reset();
    // A melody in A minor (A C D E C B A E ...), repeated.
    VowelSpec v;
    v.seconds = 8.0;
    std::vector<double> track;
    const int mel[] = { 69, 72, 74, 76, 72, 71, 69, 64, 69, 72, 71, 69, 67, 69, 76, 69 };
    while (track.size() < (size_t) (v.seconds * v.sampleRate))
        for (int n : mel)
            for (int i = 0; i < 24000 && track.size() < (size_t) (v.seconds * v.sampleRate); ++i)
                track.push_back (vcs::midiToHz ((float) n));
    const auto x = makeVowel (v, &track);
    runEngine (e, x);
    const auto k = e.activeKey();
    INFO ("detected: %s %s", vcs::pitchClassName (k.root), vcs::scaleName (k.scale));
    CHECK (e.keyIsDetected());
    CHECK (k.root == 9 && (k.scale == ScaleType::NaturalMinor || k.scale == ScaleType::HarmonicMinor));
}

TEST_CASE ("Choir: MIDI mode sings the held notes and is silent without them")
{
    vcs::ChoirEngine e;
    e.prepare (48000, 512);
    vcs::ChoirSettings s;
    s.mode = vcs::HarmonyMode::Midi;
    s.singers = 1;
    s.leadGainDb = -120.0f;
    s.choirGainDb = 0.0f;
    s.humanize = 0.0f;
    s.correction = 1.0f;
    e.setSettings (s);
    e.reset();

    VowelSpec v;
    v.seconds = 2.0;
    v.f0 = vcs::midiToHz (60);
    const auto x = makeVowel (v);
    Rendered y;
    y.l.resize (x.size());
    y.r.resize (x.size());
    for (size_t i = 0; i < x.size(); i += 480)
    {
        if (i == 48000)
            e.noteOn (67, 0); // G4 from 1 s
        e.process (x.data() + i, y.l.data() + i, y.r.data() + i, 480);
    }
    CHECK (rms (y.l.data() + 20000, 20000) < 1e-4); // nothing held yet
    vcs::YinPitchDetector yin;
    yin.prepare ({});
    const auto p = yin.analyse (y.l.data() + 80000);
    INFO ("MIDI G4 -> %.1f Hz (392.0)", p.f0Hz);
    CHECK (std::abs (vcs::hzToMidi (p.f0Hz) - 67.0f) < 0.1f);
}

TEST_CASE ("Choir: humanised singers are decorrelated, not copies")
{
    vcs::ChoirEngine e;
    e.prepare (48000, 512);
    vcs::ChoirSettings s;
    s.arrangement = 0; // unison doubles only
    s.singers = 2;
    s.width = 1.0f;
    s.leadGainDb = -120.0f;
    s.humanize = 1.0f;
    e.setSettings (s);
    e.reset();
    VowelSpec v;
    v.seconds = 3.0;
    const auto x = makeVowel (v);
    const auto y = runEngine (e, x);
    // The two singers are panned apart; with identical copies L and R would be
    // fully correlated. Real doubles drift in time and pitch -> low correlation.
    double lr = 0, ll = 0, rr = 0;
    for (size_t i = 60000; i < 140000; ++i)
    {
        lr += (double) y.l[i] * y.r[i];
        ll += (double) y.l[i] * y.l[i];
        rr += (double) y.r[i] * y.r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    INFO ("L/R correlation of two humanised doubles: %.2f", corr);
    CHECK (corr < 0.9);
    CHECK (ll > 0 && rr > 0);
}

TEST_CASE ("Choir: 16 singers, output level is sane and processing never allocates")
{
    vcs::ChoirEngine e;
    e.prepare (48000, 512);
    vcs::ChoirSettings s;
    s.arrangement = 6;
    s.singers = 16;
    s.mode = vcs::HarmonyMode::Diatonic;
    e.setSettings (s);
    e.reset();
    VowelSpec v;
    v.seconds = 2.0;
    v.vibratoHz = 5.0;
    v.vibratoCents = 40.0;
    const auto x = makeVowel (v);
    std::vector<float> l (x.size()), r (x.size());

    const long before = gAllocations.load();
    for (size_t i = 0; i < x.size(); i += 512)
    {
        const int n = (int) std::min<size_t> (512, x.size() - i);
        if (i == 48128)
        {
            s.singers = 9; // live change
            s.arrangement = 4;
            e.setSettings (s);
            e.noteOn (60, 3);
        }
        e.process (x.data() + i, l.data() + i, r.data() + i, n);
    }
    const long allocs = gAllocations.load() - before;
    CHECK (allocs == 0);

    float peak = 0;
    bool finite = true;
    for (size_t i = 0; i < l.size(); ++i)
    {
        peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
    }
    float inPeak = 0;
    for (float vx : x)
        inPeak = std::max (inPeak, std::abs (vx));
    INFO ("16 singers + lead: output peak %.2f, input peak %.2f", peak, inPeak);
    CHECK (finite);
    // Lead (0 dB) + level-normalised choir (-3 dB): stays within ~+8 dB of the input peak.
    CHECK (peak < inPeak * 2.5f);
    CHECK (e.activeVoices() == 9);
}

TEST_CASE ("Choir: sample rates 44.1 to 96 kHz")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        vcs::ChoirEngine e;
        e.prepare (sr, 1024);
        vcs::ChoirSettings s;
        s.arrangement = 1;
        s.singers = 1;
        s.autoKey = false;
        s.leadGainDb = -120.0f;
        s.humanize = 0.0f;
        s.correction = 1.0f;
        e.setSettings (s);
        e.reset();
        VowelSpec v;
        v.sampleRate = sr;
        v.f0 = vcs::midiToHz (60); // C4 -> E4 in C major
        const auto x = makeVowel (v);
        const auto y = runEngine (e, x, 1024);
        vcs::YinPitchDetector yin;
        vcs::YinPitchDetector::Config c;
        c.sampleRate = sr;
        yin.prepare (c);
        const auto p = yin.analyse (y.l.data() + (size_t) (0.6 * sr));
        INFO ("sr %.0f: latency %.1f ms, harmony %.1f Hz (E4 = 329.6)", sr, 1000.0 * e.latencySamples() / sr, p.f0Hz);
        CHECK (std::abs (vcs::hzToMidi (p.f0Hz) - 64.0f) < 0.1f);
    }
}
