#pragma once

#include "Parameters.h"
#include "vcs/ChoirEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace vcsplugin
{
/** Values the audio thread publishes for the UI (lock-free, read at ~30 Hz). */
struct Telemetry
{
    std::atomic<float> leadHz { 0.0f }, leadConfidence { 0.0f };
    std::atomic<int> leadNote { -1 };
    std::atomic<int> keyRoot { 0 }, keyScale { 0 };
    std::atomic<bool> keyDetected { false }, keyAuto { true };
    std::atomic<float> inPeak { 0.0f }, outPeakL { 0.0f }, outPeakR { 0.0f };
    std::array<std::atomic<int>, vcs::ChoirEngine::kMaxVoices> voiceNote {};
    std::array<std::atomic<float>, vcs::ChoirEngine::kMaxVoices> voicePan {};
    std::array<std::atomic<float>, vcs::ChoirEngine::kMaxVoices> voiceLevel {};
    std::atomic<int> activeVoices { 0 };
    std::atomic<int> heldMidiNotes { 0 };
};

class VocalChoirProcessor final : public juce::AudioProcessor
{
public:
    VocalChoirProcessor();
    ~VocalChoirProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- A/B comparison --------------------------------------------------
    /** Stores the current settings in the active slot and recalls the other one. */
    void switchAB();
    /** Copies the active slot's settings into the other slot. */
    void copyActiveToOther();
    bool isSlotB() const noexcept { return slotB; }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    Telemetry telemetry;

    double getCpuLoad() const { return loadMeasurer.getLoadAsProportion(); }
    int getLatencyMs() const { return currentSampleRate > 0 ? juce::roundToInt (1000.0 * engine.latencySamples() / currentSampleRate) : 0; }

private:
    void updateEngineSettings() noexcept;
    void publishTelemetry() noexcept;

    vcs::ChoirEngine engine;
    vcs::ChoirSettings settings;
    std::vector<float> mono;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
    juce::AudioProcessLoadMeasurer loadMeasurer;
    double currentSampleRate = 0.0;

    juce::ValueTree otherSlot;
    bool slotB = false;

    // Cached parameter pointers (read on the audio thread)
    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pKeyRoot = nullptr;
    std::atomic<float>* pKeyScale = nullptr;
    std::atomic<float>* pArrangement = nullptr;
    std::atomic<float>* pSingers = nullptr;
    std::atomic<float>* pCorrection = nullptr;
    std::atomic<float>* pHumanize = nullptr;
    std::atomic<float>* pTiming = nullptr;
    std::atomic<float>* pPitchSpread = nullptr;
    std::atomic<float>* pVibrato = nullptr;
    std::atomic<float>* pFormantSpread = nullptr;
    std::atomic<float>* pFormant = nullptr;
    std::atomic<float>* pBrightness = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pLead = nullptr;
    std::atomic<float>* pChoir = nullptr;
    std::atomic<float>* pOutput = nullptr;

    int heldNotes = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChoirProcessor)
};
} // namespace vcsplugin
