#pragma once
// Reusable controls and live displays. Every display reads real engine data
// through the processor's Telemetry; nothing here is decorative.

#include "../PluginProcessor.h"
#include "DesignSystem.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace vcsui
{
/** Rotary knob bound to a parameter, with caption and live value text. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
          const juce::String& tooltip = {});
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String caption;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Combo box bound to a choice parameter, with caption. */
class Choice final : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
            const juce::String& tooltip = {});
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ComboBox& box() { return combo; }

private:
    juce::ComboBox combo;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Vertical peak meter with dB scale (-48..+6 dBFS) and a hold line. */
class Meter final : public juce::Component
{
public:
    explicit Meter (juce::String label) : name (std::move (label)) {}
    void setLevel (float linear);
    void paint (juce::Graphics&) override;

private:
    juce::String name;
    float level = 0.0f, hold = 0.0f;
    int holdFrames = 0;
};

/** Scrolling lead-pitch display over a piano-roll grid. Rows in the active key
    are lit; the current target notes of the choir are marked on the right edge. */
class PitchView final : public juce::Component
{
public:
    explicit PitchView (vcsplugin::Telemetry& t) : telemetry (t) {}
    void tick(); // call at the UI frame rate
    void paint (juce::Graphics&) override;

private:
    vcsplugin::Telemetry& telemetry;
    static constexpr int kHistory = 180; // ~6 s at 30 fps
    std::array<float, kHistory> history {};
    int writePos = 0;
    float centreNote = 60.0f;
};

/** Stereo stage: each active singer drawn at its pan position, height by the
    note it sings relative to the lead, brightness by its live level. */
class ChoirStage final : public juce::Component
{
public:
    explicit ChoirStage (vcsplugin::Telemetry& t) : telemetry (t) {}
    void tick();
    void paint (juce::Graphics&) override;

private:
    vcsplugin::Telemetry& telemetry;
    std::array<float, vcs::ChoirEngine::kMaxVoices> smoothLevel {};
};
} // namespace vcsui
