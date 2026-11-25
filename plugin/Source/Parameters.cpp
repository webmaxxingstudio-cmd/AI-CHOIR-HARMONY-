#include "Parameters.h"

#include "vcs/ChoirEngine.h"

namespace vcsplugin
{
namespace
{
    using APF = juce::AudioParameterFloat;
    using APC = juce::AudioParameterChoice;
    using API = juce::AudioParameterInt;
    using Attr = juce::AudioParameterFloatAttributes;

    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String db (float v, int) { return v <= -59.9f ? juce::String ("Off") : juce::String (v, 1) + " dB"; }

    std::unique_ptr<APF> percent (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, 1.0f), def,
                                      Attr().withStringFromValueFunction (pct));
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<APC> (juce::ParameterID { ParamID::harmonyMode, 1 }, "Harmony Mode",
                                       juce::StringArray { "Auto (Key)", "MIDI" }, 0));

    juce::StringArray roots { "Auto" };
    for (int pc = 0; pc < 12; ++pc)
        roots.add (vcs::pitchClassName (pc));
    layout.add (std::make_unique<APC> (juce::ParameterID { ParamID::keyRoot, 1 }, "Key", roots, 0));

    juce::StringArray scales;
    for (int s = 0; s < (int) vcs::ScaleType::count; ++s)
        scales.add (vcs::scaleName ((vcs::ScaleType) s));
    layout.add (std::make_unique<APC> (juce::ParameterID { ParamID::keyScale, 1 }, "Scale", scales, 0));

    juce::StringArray arr;
    for (const auto& a : vcs::arrangements())
        arr.add (a.name);
    layout.add (std::make_unique<APC> (juce::ParameterID { ParamID::arrangement, 1 }, "Arrangement", arr, 3));

    layout.add (std::make_unique<API> (juce::ParameterID { ParamID::singers, 1 }, "Singers", 1, vcs::ChoirEngine::kMaxVoices, 6));

    layout.add (percent (ParamID::correction, "Harmony Correction", 0.6f));
    layout.add (percent (ParamID::humanize, "Humanize", 0.6f));

    layout.add (std::make_unique<APF> (juce::ParameterID { ParamID::timing, 1 }, "Timing Spread",
                                       juce::NormalisableRange<float> (0.0f, 40.0f, 0.1f), 18.0f,
                                       Attr().withLabel ("ms")));
    layout.add (std::make_unique<APF> (juce::ParameterID { ParamID::pitchSpread, 1 }, "Pitch Spread",
                                       juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f), 10.0f,
                                       Attr().withLabel ("ct")));
    layout.add (percent (ParamID::vibrato, "Vibrato Variation", 0.5f));
    layout.add (percent (ParamID::formantSpread, "Formant Variation", 0.5f));

    layout.add (std::make_unique<APF> (juce::ParameterID { ParamID::formant, 1 }, "Formant",
                                       juce::NormalisableRange<float> (-5.0f, 7.0f, 0.01f), 0.0f,
                                       Attr().withLabel ("st")));
    layout.add (std::make_unique<APF> (juce::ParameterID { ParamID::brightness, 1 }, "Brightness",
                                       juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                                       Attr().withStringFromValueFunction (pct)));
    layout.add (percent (ParamID::width, "Width", 0.8f));

    const auto levelRange = juce::NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f);
    layout.add (std::make_unique<APF> (juce::ParameterID { ParamID::leadLevel, 1 }, "Lead Level", levelRange, 0.0f,
                                       Attr().withStringFromValueFunction (db)));
    layout.add (std::make_unique<APF> (juce::ParameterID { ParamID::choirLevel, 1 }, "Choir Level", levelRange, -3.0f,
                                       Attr().withStringFromValueFunction (db)));
    layout.add (std::make_unique<APF> (juce::ParameterID { ParamID::output, 1 }, "Output",
                                       juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                       Attr().withStringFromValueFunction (db)));
    return layout;
}
} // namespace vcsplugin
