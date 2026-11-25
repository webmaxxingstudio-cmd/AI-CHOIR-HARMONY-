#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace vcsplugin
{
VocalChoirProcessor::VocalChoirProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "VocalChoirStudio", createParameterLayout())
{
    pMode = apvts.getRawParameterValue (ParamID::harmonyMode);
    pKeyRoot = apvts.getRawParameterValue (ParamID::keyRoot);
    pKeyScale = apvts.getRawParameterValue (ParamID::keyScale);
    pArrangement = apvts.getRawParameterValue (ParamID::arrangement);
    pSingers = apvts.getRawParameterValue (ParamID::singers);
    pCorrection = apvts.getRawParameterValue (ParamID::correction);
    pHumanize = apvts.getRawParameterValue (ParamID::humanize);
    pTiming = apvts.getRawParameterValue (ParamID::timing);
    pPitchSpread = apvts.getRawParameterValue (ParamID::pitchSpread);
    pVibrato = apvts.getRawParameterValue (ParamID::vibrato);
    pFormantSpread = apvts.getRawParameterValue (ParamID::formantSpread);
    pFormant = apvts.getRawParameterValue (ParamID::formant);
    pBrightness = apvts.getRawParameterValue (ParamID::brightness);
    pWidth = apvts.getRawParameterValue (ParamID::width);
    pLead = apvts.getRawParameterValue (ParamID::leadLevel);
    pChoir = apvts.getRawParameterValue (ParamID::choirLevel);
    pOutput = apvts.getRawParameterValue (ParamID::output);
}

bool VocalChoirProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VocalChoirProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const int block = juce::jmax (32, samplesPerBlock);
    updateEngineSettings();
    engine.prepare (sampleRate, block);
    engine.setSettings (settings);
    engine.reset();
    mono.assign ((size_t) block, 0.0f);
    setLatencySamples (engine.latencySamples());

    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    loadMeasurer.reset (sampleRate, block);
    heldNotes = 0;
}

void VocalChoirProcessor::updateEngineSettings() noexcept
{
    settings.mode = pMode->load() > 0.5f ? vcs::HarmonyMode::Midi : vcs::HarmonyMode::Diatonic;
    const int root = juce::roundToInt (pKeyRoot->load());
    settings.autoKey = root == 0;
    settings.manualKey = { juce::jmax (0, root - 1), (vcs::ScaleType) juce::roundToInt (pKeyScale->load()) };
    settings.arrangement = juce::roundToInt (pArrangement->load());
    settings.singers = juce::roundToInt (pSingers->load());
    settings.correction = pCorrection->load();
    settings.humanize = pHumanize->load();
    settings.timingSpreadMs = pTiming->load();
    settings.pitchSpreadCents = pPitchSpread->load();
    settings.vibratoVariation = pVibrato->load();
    settings.formantSpread = pFormantSpread->load();
    settings.formantShift = pFormant->load();
    settings.brightness = pBrightness->load();
    settings.width = pWidth->load();
    const float lead = pLead->load(), choir = pChoir->load();
    settings.leadGainDb = lead <= -59.9f ? -120.0f : lead;
    settings.choirGainDb = choir <= -59.9f ? -120.0f : choir;
}

void VocalChoirProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::AudioProcessLoadMeasurer::ScopedTimer timer (loadMeasurer, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int numIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    updateEngineSettings();
    engine.setSettings (settings);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            engine.noteOn (m.getNoteNumber(), meta.samplePosition);
            ++heldNotes;
        }
        else if (m.isNoteOff())
        {
            engine.noteOff (m.getNoteNumber(), meta.samplePosition);
            heldNotes = juce::jmax (0, heldNotes - 1);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            engine.allNotesOff();
            heldNotes = 0;
        }
    }

    float inPeak = 0.0f;
    const int chunkMax = (int) mono.size();
    for (int start = 0; start < numSamples; start += chunkMax)
    {
        const int n = juce::jmin (chunkMax, numSamples - start);
        const float scale = numIn > 0 ? 1.0f / (float) numIn : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int ch = 0; ch < numIn; ++ch)
                s += buffer.getReadPointer (ch)[start + i];
            mono[(size_t) i] = s * scale;
            inPeak = juce::jmax (inPeak, std::abs (mono[(size_t) i]));
        }
        engine.process (mono.data(), buffer.getWritePointer (0) + start, buffer.getWritePointer (1) + start, n);
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    outputGain.applyGain (buffer, numSamples);

    telemetry.inPeak.store (juce::jmax (inPeak, telemetry.inPeak.load() * 0.9f));
    telemetry.outPeakL.store (juce::jmax (buffer.getMagnitude (0, 0, numSamples), telemetry.outPeakL.load() * 0.9f));
    telemetry.outPeakR.store (juce::jmax (buffer.getMagnitude (1, 0, numSamples), telemetry.outPeakR.load() * 0.9f));
    publishTelemetry();
}

void VocalChoirProcessor::publishTelemetry() noexcept
{
    const auto& a = engine.analyzer();
    const auto& e = a.lastEstimate();
    telemetry.leadHz.store (e.voiced ? e.f0Hz : 0.0f);
    telemetry.leadConfidence.store (e.confidence);
    telemetry.leadNote.store (a.stableNote());
    const auto key = engine.activeKey();
    telemetry.keyRoot.store (key.root);
    telemetry.keyScale.store ((int) key.scale);
    telemetry.keyDetected.store (engine.keyIsDetected());
    telemetry.keyAuto.store (settings.autoKey);
    for (int v = 0; v < vcs::ChoirEngine::kMaxVoices; ++v)
    {
        telemetry.voiceNote[(size_t) v].store (engine.voiceNote (v));
        telemetry.voicePan[(size_t) v].store (engine.voicePan (v));
        telemetry.voiceLevel[(size_t) v].store (engine.voiceLevel (v));
    }
    telemetry.activeVoices.store (engine.activeVoices());
    telemetry.heldMidiNotes.store (heldNotes);
}

// ---------------------------------------------------------------- state
void VocalChoirProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalChoirProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void VocalChoirProcessor::switchAB()
{
    auto current = apvts.copyState();
    if (otherSlot.isValid())
        apvts.replaceState (otherSlot.createCopy());
    otherSlot = current;
    slotB = ! slotB;
    undoManager.clearUndoHistory(); // undo history belongs to one slot
}

void VocalChoirProcessor::copyActiveToOther()
{
    otherSlot = apvts.copyState();
}

juce::AudioProcessorEditor* VocalChoirProcessor::createEditor()
{
    return new VocalChoirEditor (*this);
}
} // namespace vcsplugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new vcsplugin::VocalChoirProcessor();
}
