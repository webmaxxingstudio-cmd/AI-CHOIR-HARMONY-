#pragma once

#include "PluginProcessor.h"
#include "ui/DesignSystem.h"
#include "ui/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace vcsplugin
{
class VocalChoirEditor final : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    explicit VocalChoirEditor (VocalChoirProcessor&);
    ~VocalChoirEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void timerCallback() override; // public so offline tools can drive the UI refresh

private:
    void paintHeader (juce::Graphics&, juce::Rectangle<int>);
    void updateEnablement();

    VocalChoirProcessor& proc;
    vcsui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    // Header
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, abButton { "A" }, copyButton { "COPY TO B" };

    // Harmony
    vcsui::Choice mode, keyRoot, keyScale, arrangement;
    vcsui::Knob singers, correction;

    // Voice designer
    vcsui::Knob humanize, timing, pitchSpread, vibrato, formantSpread, formant, brightness, width;

    // Output
    vcsui::Knob leadLevel, choirLevel, output;
    vcsui::Meter inMeter { "IN" }, outLMeter { "L" }, outRMeter { "R" };

    vcsui::PitchView pitchView;
    vcsui::ChoirStage stage;

    juce::Rectangle<int> headerArea, harmonyArea, designerArea, outputArea, pitchArea, stageArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChoirEditor)
};
} // namespace vcsplugin
