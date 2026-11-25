// Renders the real plugin editor to a PNG while the real processor plays a
// synthetic vocal phrase, so the screenshot shows live engine data.
// Links the plugin's shared code directly (no window, no host).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "TestSignals.h"
#include "vcs/MusicTheory.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    if (argc < 2)
        return 2;
    juce::ScopedJuceInitialiser_GUI init;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    const double sr = 48000;
    const int block = 256;
    proc->setPlayConfigDetails (2, 2, sr, block);
    proc->prepareToPlay (sr, block);

    // 4-part, 8 singers, key left on Auto so detection shows in the header.
    for (auto* p : proc->getParameters())
    {
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->getParameterID() == "arrangement")
                rp->setValueNotifyingHost (rp->convertTo0to1 (4.0f));
            if (rp->getParameterID() == "singers")
                rp->setValueNotifyingHost (rp->convertTo0to1 (8.0f));
        }
    }

    // A melody in A minor with vibrato, long enough for key detection.
    const int mel[] = { 69, 72, 74, 76, 72, 71, 69, 64, 69, 72, 71, 69, 67, 69, 76, 74 };
    vcstest::VowelSpec v;
    v.seconds = 9.0;
    v.vibratoHz = 5.4;
    v.vibratoCents = 30.0;
    std::vector<double> track;
    double cur = vcs::midiToHz (69);
    while (track.size() < (size_t) (v.seconds * sr))
        for (int n : mel)
            for (int i = 0; i < 26000 && track.size() < (size_t) (v.seconds * sr); ++i)
            {
                cur += (vcs::midiToHz ((float) n) - cur) * 0.0008;
                track.push_back (cur);
            }
    const auto x = vcstest::makeVowel (v, &track);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    auto* timer = dynamic_cast<juce::Timer*> (ed.get());
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    const int blocksPerFrame = (int) (sr / 30.0 / block);
    int frame = 0;
    for (size_t pos = 0; pos + block <= x.size(); pos += block)
    {
        for (int ch = 0; ch < 2; ++ch)
            buf.copyFrom (ch, 0, x.data() + pos, block);
        proc->processBlock (buf, midi);
        if (++frame % blocksPerFrame == 0 && timer)
            timer->timerCallback();
    }
    const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    const juce::File out { juce::String (argv[1]) };
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    return png.writeImageToStream (img, os) ? 0 : 1;
}
