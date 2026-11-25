// Host-side validation for the built VST3, loading it exactly as a DAW would.
//
//   vcs_host_check check  <plugin.vst3>             correctness / robustness checks
//   vcs_host_check demo   <plugin.vst3> <outDir>    renders demo WAVs (synthetic voice in, choir out)
//   vcs_host_check window <plugin.vst3> <seconds>   opens the editor while audio plays (for screenshots)

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "TestSignals.h"
#include "vcs/MusicTheory.h"
#include "vcs/YinPitchDetector.h"

#include <atomic>
#include <cstdio>
#include <random>
#include <thread>

namespace
{
int failures = 0;
#define HCHECK(cond, ...)                                                   \
    do {                                                                    \
        if (cond) { std::printf ("  ok    "); }                             \
        else { std::printf ("  FAIL  "); ++failures; }                      \
        std::printf (__VA_ARGS__);                                          \
        std::printf ("\n");                                                 \
    } while (0)

std::unique_ptr<juce::AudioPluginInstance> load (const juce::String& path, double sr, int block)
{
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, path);
    if (descs.isEmpty())
        return nullptr;
    juce::String err;
    auto p = fmt.createInstanceFromDescription (*descs[0], sr, block, err);
    if (! p)
        std::printf ("load error: %s\n", err.toRawUTF8());
    return p;
}

juce::AudioProcessorParameter* param (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* q : p.getParameters())
        if (q->getName (64) == name)
            return q;
    return nullptr;
}

void setParam (juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    if (auto* q = param (p, name))
        q->setValueNotifyingHost (normalised);
}

/** Sets a parameter from its display text (e.g. "C", "8-Part Choir"). */
void setParamText (juce::AudioPluginInstance& p, const juce::String& name, const juce::String& text)
{
    if (auto* q = param (p, name))
    {
        const int steps = juce::jlimit (2, 1000, q->getNumSteps());
        for (int i = 0; i < steps; ++i)
        {
            const float v = (float) i / (float) (steps - 1);
            if (q->getText (v, 64) == text)
            {
                q->setValueNotifyingHost (v);
                return;
            }
        }
        std::printf ("  (could not set %s to %s)\n", name.toRawUTF8(), text.toRawUTF8());
    }
}

/** A short sung phrase: C major melody with vibrato and small glides. */
std::vector<float> phrase (double sr, double seconds)
{
    const int notes[] = { 60, 62, 64, 65, 67, 67, 65, 64, 62, 64, 60, 60 };
    const double noteLen = seconds / 12.0;
    std::vector<double> track ((size_t) (seconds * sr));
    double cur = vcs::midiToHz (60);
    for (size_t i = 0; i < track.size(); ++i)
    {
        const int idx = std::min (11, (int) ((double) i / sr / noteLen));
        const double target = vcs::midiToHz ((float) notes[idx]);
        cur += (target - cur) * (1.0 - std::exp (-1.0 / (0.03 * sr))); // ~30 ms glide
        track[i] = cur;
    }
    vcstest::VowelSpec v;
    v.sampleRate = sr;
    v.seconds = seconds;
    v.vibratoHz = 5.3;
    v.vibratoCents = 25.0;
    auto x = vcstest::makeVowel (v, &track);
    // Gentle phrase envelope + short breaths between notes
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr;
        const double inNote = std::fmod (t, noteLen) / noteLen;
        const double env = std::min (1.0, inNote / 0.06) * std::min (1.0, (1.0 - inNote) / 0.08);
        x[i] *= (float) (0.4 + 0.6 * env);
    }
    return x;
}

struct RunResult
{
    juce::AudioBuffer<float> out;
    bool finite = true;
    float peak = 0.0f;
};

RunResult run (juce::AudioPluginInstance& p, const std::vector<float>& in, int block, bool randomBlocks = false)
{
    RunResult r;
    r.out.setSize (2, (int) in.size());
    juce::AudioBuffer<float> buf (2, juce::jmax (block, 4096));
    juce::MidiBuffer midi;
    std::mt19937 rng (3);
    int pos = 0;
    while (pos < (int) in.size())
    {
        int n = randomBlocks ? (int) (rng() % (unsigned) block) + 1 : block;
        n = juce::jmin (n, (int) in.size() - pos);
        buf.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, in.data() + pos, n);
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            r.out.copyFrom (ch, pos, buf, ch, 0, n);
        pos += n;
    }
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < r.out.getNumSamples(); ++i)
        {
            const float v = r.out.getSample (ch, i);
            r.finite = r.finite && std::isfinite (v);
            r.peak = std::max (r.peak, std::abs (v));
        }
    return r;
}

int check (const juce::String& path)
{
    std::printf ("Loading %s\n", path.toRawUTF8());
    auto p = load (path, 48000, 512);
    HCHECK (p != nullptr, "VST3 loads through the host API");
    if (! p)
        return 1;

    HCHECK (p->getName() == "Vocal Choir Studio", "name: %s", p->getName().toRawUTF8());
    HCHECK (p->acceptsMidi(), "accepts MIDI (for MIDI harmony mode)");
    // 17 engine parameters + the host bypass parameter the VST3 wrapper adds.
    HCHECK (p->getParameters().size() == 18, "%d automatable parameters", p->getParameters().size());

    juce::AudioProcessor::BusesLayout stereo;
    stereo.inputBuses.add (juce::AudioChannelSet::stereo());
    stereo.outputBuses.add (juce::AudioChannelSet::stereo());
    HCHECK (p->setBusesLayout (stereo), "stereo in / stereo out layout");

    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0 })
    {
        p->releaseResources();
        p->prepareToPlay (sr, 512);
        const int lat = p->getLatencySamples();
        const auto in = phrase (sr, 3.0);
        const auto r = run (*p, in, 512);

        // Harmony check: 2-part (3rd above) on C major input -> E over C.
        HCHECK (r.finite && r.peak > 0.05f && r.peak < 4.0f, "sr %.0f: output finite, peak %.2f, latency %d samples (%.1f ms)",
                sr, r.peak, lat, 1000.0 * lat / sr);
    }

    // Harmony through the host: manual C major, 1 singer, 3rd above, lead off.
    p->releaseResources();
    p->prepareToPlay (48000, 512);
    setParamText (*p, "Key", "C");
    setParamText (*p, "Scale", "Major");
    setParamText (*p, "Arrangement", "2-Part (3rd above)");
    setParamText (*p, "Singers", "1");
    setParamText (*p, "Lead Level", "Off");
    setParam (*p, "Humanize", 0.0f);
    setParam (*p, "Harmony Correction", 1.0f);
    {
        vcstest::VowelSpec v;
        v.f0 = vcs::midiToHz (60);
        v.seconds = 1.5;
        const auto in = vcstest::makeVowel (v);
        const auto r = run (*p, in, 512);
        vcs::YinPitchDetector yin;
        yin.prepare ({});
        const auto e = yin.analyse (r.out.getReadPointer (0) + 40000);
        HCHECK (e.voiced && std::abs (vcs::hzToMidi (e.f0Hz) - 64.0f) < 0.15f,
                "sung C4 -> choir sings E4: %.1f Hz (329.6 expected)", e.f0Hz);
    }

    // Odd block sizes (hosts do this) + automation every block.
    {
        p->releaseResources();
        p->prepareToPlay (48000, 4096);
        const auto in = phrase (48000, 2.0);
        const auto r = run (*p, in, 4096, true);
        HCHECK (r.finite, "random block sizes 1..4096: stable");

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        std::mt19937 rng (11);
        bool finite = true;
        for (int b = 0; b < 400; ++b)
        {
            for (auto* q : p->getParameters())
                if (rng() % 4 == 0 && ! q->getName (64).containsIgnoreCase ("bypass"))
                    q->setValue ((float) (rng() % 1000) / 999.0f);
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, in.data() + (b * 256) % 90000, 256);
            if (b % 50 == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60 + b % 12, (juce::uint8) 100), 10);
            p->processBlock (buf, midi);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    finite = finite && std::isfinite (buf.getSample (ch, i)) && std::abs (buf.getSample (ch, i)) < 50.0f;
        }
        HCHECK (finite, "random automation of all parameters + MIDI for 400 blocks: stable, no blow-ups");
    }

    // State round trip into a fresh instance.
    {
        setParamText (*p, "Arrangement", "5-Part");
        setParamText (*p, "Singers", "11");
        setParam (*p, "Width", 0.37f);
        juce::MemoryBlock state;
        p->getStateInformation (state);
        auto q = load (path, 48000, 512);
        q->setStateInformation (state.getData(), (int) state.getSize());
        bool same = true;
        for (int i = 0; i < p->getParameters().size(); ++i)
        {
            // Compare displayed values: the host may hold un-snapped values for
            // stepped parameters (e.g. 0.93 for a 2-way choice) that the plugin quantises.
            auto* pa = p->getParameters()[i];
            auto* qa = q->getParameters()[i];
            const auto a = pa->getText (pa->getValue(), 64), b = qa->getText (qa->getValue(), 64);
            if (a != b)
            {
                same = false;
                std::printf ("    %s: %s vs %s\n", pa->getName (64).toRawUTF8(), a.toRawUTF8(), b.toRawUTF8());
            }
        }
        HCHECK (same, "state save/restore reproduces every parameter (%d bytes)", (int) state.getSize());
    }

    // Performance: 16 singers, 48 kHz, 128-sample blocks.
    {
        setParamText (*p, "Arrangement", "8-Part Choir");
        setParamText (*p, "Singers", "16");
        p->releaseResources();
        p->prepareToPlay (48000, 128);
        const auto in = phrase (48000, 10.0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (*p, in, 128);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        HCHECK (ms < 10000.0 * 0.5, "16 singers: 10 s of audio processed in %.0f ms (%.1f%% of one core, this container)", ms,
                ms / 100.0);
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL HOST CHECKS PASSED" : "HOST CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (auto* w = wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0))
    {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer (w);
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
}

int demo (const juce::String& path, const juce::File& outDir)
{
    outDir.createDirectory();
    const double sr = 48000;
    const auto in = phrase (sr, 7.0);
    juce::AudioBuffer<float> inBuf (1, (int) in.size());
    inBuf.copyFrom (0, 0, in.data(), (int) in.size());
    writeWav (outDir.getChildFile ("00_input_synthetic_voice.wav"), inBuf, sr);

    struct Setting { const char* file; const char* arrangement; int singers; float humanize; float formant; };
    const Setting settings[] = {
        { "01_two_part_third_above.wav", "2-Part (3rd above)", 2, 0.5f, 0.0f },
        { "02_four_part_8_singers.wav", "4-Part", 8, 0.6f, 0.0f },
        { "03_8_part_16_singers_dark.wav", "8-Part Choir", 16, 0.8f, -2.5f },
    };
    for (const auto& s : settings)
    {
        auto p = load (path, sr, 512);
        p->prepareToPlay (sr, 512);
        setParamText (*p, "Key", "C");
        setParamText (*p, "Scale", "Major");
        setParamText (*p, "Arrangement", s.arrangement);
        setParamText (*p, "Singers", juce::String (s.singers));
        setParam (*p, "Humanize", s.humanize);
        if (auto* q = param (*p, "Formant"))
            q->setValueNotifyingHost (q->getValueForText (juce::String (s.formant)));
        auto r = run (*p, in, 512);
        // Remove the reported latency so the files line up with the input.
        const int lat = p->getLatencySamples();
        juce::AudioBuffer<float> aligned (2, r.out.getNumSamples() - lat);
        for (int ch = 0; ch < 2; ++ch)
            aligned.copyFrom (ch, 0, r.out, ch, lat, aligned.getNumSamples());
        // No limiter yet (that is the mastering phase): normalise demo files to -1 dBFS peak instead of clipping.
        const float peak = aligned.getMagnitude (0, aligned.getNumSamples());
        if (peak > 0.89f)
            aligned.applyGain (0.89f / peak);
        writeWav (outDir.getChildFile (s.file), aligned, sr);
        std::printf ("wrote %s (raw peak %.2f)\n", s.file, r.peak);
    }
    return 0;
}

int window (const juce::String& path, double seconds)
{
    const double sr = 48000;
    auto p = load (path, sr, 256);
    p->prepareToPlay (sr, 256);
    setParamText (*p, "Arrangement", "4-Part");
    setParamText (*p, "Singers", "8");
    const auto in = phrase (sr, 8.0);

    std::atomic<bool> running { true };
    std::thread audio ([&] {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        size_t pos = 0;
        while (running)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    buf.setSample (ch, i, in[(pos + (size_t) i) % in.size()]);
            p->processBlock (buf, midi);
            pos += 256;
            std::this_thread::sleep_for (std::chrono::microseconds (5333)); // real time
        }
    });

    auto* editor = p->createEditorIfNeeded();
    juce::DocumentWindow w ("Vocal Choir Studio", juce::Colours::black, 0, true);
    w.setUsingNativeTitleBar (false);
    w.setTitleBarHeight (0);
    w.setSize (editor->getWidth(), editor->getHeight());
    w.setTopLeftPosition (0, 0);
    w.setVisible (true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
    w.setContentNonOwned (editor, true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (seconds * 1000));
    running = false;
    audio.join();
    w.clearContentComponent();
    delete editor;
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::printf ("usage: vcs_host_check check|demo|window <plugin.vst3> [outDir|seconds]\n");
        return 2;
    }
    const juce::String mode (argv[1]), path (argv[2]);
    if (mode == "check")
        return check (path);
    if (mode == "demo" && argc > 3)
        return demo (path, juce::File (juce::String (argv[3])));
    if (mode == "window")
        return window (path, argc > 3 ? juce::String (argv[3]).getDoubleValue() : 6.0);
    return 2;
}
