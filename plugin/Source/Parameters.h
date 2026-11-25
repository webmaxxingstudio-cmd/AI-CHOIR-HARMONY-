#pragma once
// Every automatable parameter of the plugin. Each one is wired to the engine
// in PluginProcessor::updateEngineSettings(); there are no UI-only controls.

#include <juce_audio_processors/juce_audio_processors.h>

namespace vcsplugin
{
namespace ParamID
{
    inline constexpr const char* harmonyMode = "harmonyMode";
    inline constexpr const char* keyRoot = "keyRoot";
    inline constexpr const char* keyScale = "keyScale";
    inline constexpr const char* arrangement = "arrangement";
    inline constexpr const char* singers = "singers";
    inline constexpr const char* correction = "correction";
    inline constexpr const char* humanize = "humanize";
    inline constexpr const char* timing = "timing";
    inline constexpr const char* pitchSpread = "pitchSpread";
    inline constexpr const char* vibrato = "vibrato";
    inline constexpr const char* formantSpread = "formantSpread";
    inline constexpr const char* formant = "formant";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* width = "width";
    inline constexpr const char* leadLevel = "leadLevel";
    inline constexpr const char* choirLevel = "choirLevel";
    inline constexpr const char* output = "output";
} // namespace ParamID

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace vcsplugin
