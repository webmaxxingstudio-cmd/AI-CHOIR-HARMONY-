#include "PluginEditor.h"

namespace vcsplugin
{
using namespace vcsui;

VocalChoirEditor::VocalChoirEditor (VocalChoirProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      mode (p.apvts, ParamID::harmonyMode, "Harmony source",
            "Auto: harmonies follow the key. MIDI: each part sings a note you hold on the MIDI input."),
      keyRoot (p.apvts, ParamID::keyRoot, "Key", "Auto detects the key from your singing, or pick it."),
      keyScale (p.apvts, ParamID::keyScale, "Scale", "Scale used when the key is set manually."),
      arrangement (p.apvts, ParamID::arrangement, "Arrangement", "Which harmony parts the singers are spread over."),
      singers (p.apvts, ParamID::singers, "Singers", "Number of generated singers (1-16)."),
      correction (p.apvts, ParamID::correction, "Correction",
                  "0%: harmonies copy your scoops and vibrato. 100%: they sing the exact notes."),
      humanize (p.apvts, ParamID::humanize, "Humanize", "Master amount of per-singer variation."),
      timing (p.apvts, ParamID::timing, "Timing", "How late each singer may be against the lead."),
      pitchSpread (p.apvts, ParamID::pitchSpread, "Pitch spread", "Per-singer tuning drift."),
      vibrato (p.apvts, ParamID::vibrato, "Vibrato var.", "Each singer's own vibrato depth."),
      formantSpread (p.apvts, ParamID::formantSpread, "Formant var.", "Per-singer vocal tract size variation."),
      formant (p.apvts, ParamID::formant, "Formant", "Moves the choir's formants: lower = bigger, darker voices."),
      brightness (p.apvts, ParamID::brightness, "Brightness", "Tone tilt of the choir."),
      width (p.apvts, ParamID::width, "Width", "Stereo spread of the singers."),
      leadLevel (p.apvts, ParamID::leadLevel, "Lead", "Level of your original voice."),
      choirLevel (p.apvts, ParamID::choirLevel, "Choir", "Level of the generated choir."),
      output (p.apvts, ParamID::output, "Output", "Final output gain."),
      pitchView (p.telemetry),
      stage (p.telemetry)
{
    setLookAndFeel (&lnf);

    for (auto* c : std::initializer_list<juce::Component*> { &mode, &keyRoot, &keyScale, &arrangement, &singers,
                                                             &correction, &humanize, &timing, &pitchSpread, &vibrato,
                                                             &formantSpread, &formant, &brightness, &width, &leadLevel,
                                                             &choirLevel, &output, &inMeter, &outLMeter, &outRMeter,
                                                             &pitchView, &stage, &undoButton, &redoButton, &abButton,
                                                             &copyButton })
        addAndMakeVisible (c);

    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };
    undoButton.setTooltip ("Undo the last change");
    redoButton.setTooltip ("Redo");
    abButton.setTooltip ("Switch between two settings snapshots (A/B comparison)");
    copyButton.setTooltip ("Copy the current settings into the other slot");
    abButton.onClick = [this] {
        proc.switchAB();
        abButton.setButtonText (proc.isSlotB() ? "B" : "A");
        copyButton.setButtonText (proc.isSlotB() ? "COPY TO A" : "COPY TO B");
    };
    copyButton.onClick = [this] { proc.copyActiveToOther(); };
    abButton.setButtonText (proc.isSlotB() ? "B" : "A");

    // Start an undo transaction at the beginning of every gesture.
    addMouseListener (this, true);

    setSize (1080, 680);
    updateEnablement();
    startTimerHz (30);
}

VocalChoirEditor::~VocalChoirEditor()
{
    setLookAndFeel (nullptr);
}

void VocalChoirEditor::mouseDown (const juce::MouseEvent&)
{
    proc.undoManager.beginNewTransaction();
}

void VocalChoirEditor::updateEnablement()
{
    // Controls the engine ignores in the current mode are greyed out, never hidden behind fake behaviour.
    const bool midiMode = proc.apvts.getRawParameterValue (ParamID::harmonyMode)->load() > 0.5f;
    const bool autoKey = proc.apvts.getRawParameterValue (ParamID::keyRoot)->load() < 0.5f;
    keyRoot.setEnabled (! midiMode);
    keyScale.setEnabled (! midiMode && ! autoKey);
    arrangement.setEnabled (! midiMode);
    keyScale.setAlpha (keyScale.isEnabled() ? 1.0f : 0.4f);
    keyRoot.setAlpha (keyRoot.isEnabled() ? 1.0f : 0.4f);
    arrangement.setAlpha (arrangement.isEnabled() ? 1.0f : 0.4f);
    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());
}

void VocalChoirEditor::timerCallback()
{
    pitchView.tick();
    stage.tick();
    inMeter.setLevel (proc.telemetry.inPeak.load());
    outLMeter.setLevel (proc.telemetry.outPeakL.load());
    outRMeter.setLevel (proc.telemetry.outPeakR.load());
    updateEnablement();
    repaint (headerArea);
}

void VocalChoirEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::background);
    paintHeader (g, headerArea);
    drawPanel (g, harmonyArea, "Harmony");
    drawPanel (g, designerArea, "Voice Designer");
    drawPanel (g, outputArea, "Levels");
    g.setFont (type::caption());
    g.setColour (colour::textDim);
    g.drawText ("LEAD PITCH  /  CHOIR NOTES", pitchArea.withHeight (18).translated (0, -20), juce::Justification::centredLeft);
    g.drawText ("CHOIR STAGE", stageArea.withHeight (18).translated (0, -20), juce::Justification::centredLeft);
}

void VocalChoirEditor::paintHeader (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (colour::panel);
    g.fillRect (r);
    g.setColour (colour::line);
    g.drawHorizontalLine (r.getBottom() - 1, 0.0f, (float) getWidth());

    auto left = r.reduced (space::l, 0);
    g.setColour (colour::text);
    g.setFont (type::title());
    g.drawText ("VOCAL CHOIR STUDIO", left.removeFromLeft (220), juce::Justification::centredLeft);

    // Key and note readouts
    const auto& t = proc.telemetry;
    const bool midiMode = proc.apvts.getRawParameterValue (ParamID::harmonyMode)->load() > 0.5f;
    juce::String keyText, keyState;
    if (midiMode)
    {
        keyText = "MIDI";
        keyState = juce::String (t.heldMidiNotes.load()) + " notes held";
    }
    else if (! t.keyAuto.load())
    {
        keyText = juce::String (vcs::pitchClassName (t.keyRoot.load())) + " " + vcs::scaleName ((vcs::ScaleType) t.keyScale.load());
        keyState = "manual";
    }
    else if (t.keyDetected.load())
    {
        keyText = juce::String (vcs::pitchClassName (t.keyRoot.load())) + " " + vcs::scaleName ((vcs::ScaleType) t.keyScale.load());
        keyState = "detected";
    }
    else
    {
        keyText = "Listening";
        keyState = "sing a few phrases";
    }

    auto readout = [&] (juce::Rectangle<int> box, const juce::String& cap, const juce::String& value,
                        const juce::String& sub, juce::Colour valueColour) {
        g.setFont (type::caption());
        g.setColour (colour::textFaint);
        g.drawText (cap, box.removeFromLeft (38), juce::Justification::centredLeft);
        g.setFont (type::readout());
        g.setColour (valueColour);
        g.drawText (value, box.removeFromLeft (juce::jmax (40, type::readout().getStringWidth (value) + 12)), juce::Justification::centredLeft);
        g.setFont (type::label());
        g.setColour (colour::textDim);
        g.drawText (sub, box, juce::Justification::centredLeft);
    };

    auto centre = left.removeFromLeft (560);
    readout (centre.removeFromLeft (300), "KEY", keyText, keyState, colour::text);

    const float hz = t.leadHz.load();
    juce::String noteText = "-", centsText;
    if (hz > 0.0f)
    {
        const float m = vcs::hzToMidi (hz);
        const int n = juce::roundToInt (m);
        noteText = juce::String (vcs::pitchClassName (n % 12)) + juce::String (n / 12 - 1);
        const int cents = juce::roundToInt ((m - (float) n) * 100.0f);
        centsText = (cents >= 0 ? "+" : "") + juce::String (cents) + " ct";
    }
    readout (centre, "LEAD", noteText, centsText, colour::lead);

    // Status: CPU and latency
    auto status = r.reduced (space::l, 0).removeFromRight (150);
    g.setFont (type::label());
    g.setColour (colour::textDim);
    const auto cpu = juce::roundToInt (proc.getCpuLoad() * 100.0);
    g.drawText ("CPU " + juce::String (cpu) + "%   " + juce::String (proc.getLatencyMs()) + " ms",
                status, juce::Justification::centredRight);
}

void VocalChoirEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (space::headerHeight);

    // Header buttons, right side before the status text
    auto hb = headerArea.reduced (space::l, 12).withTrimmedRight (160).removeFromRight (290);
    copyButton.setBounds (hb.removeFromRight (90));
    hb.removeFromRight (space::xs);
    abButton.setBounds (hb.removeFromRight (34));
    hb.removeFromRight (space::m);
    redoButton.setBounds (hb.removeFromRight (56));
    hb.removeFromRight (space::xs);
    undoButton.setBounds (hb.removeFromRight (56));

    r.reduce (space::m, space::m);
    outputArea = r.removeFromBottom (128);
    r.removeFromBottom (space::m);

    harmonyArea = r.removeFromLeft (250);
    r.removeFromLeft (space::m);
    designerArea = r.removeFromRight (290);
    r.removeFromRight (space::m);

    // Centre: pitch view and choir stage with captions above each
    auto centre = r;
    centre.removeFromTop (20);
    pitchArea = centre.removeFromTop ((centre.getHeight() - 20) / 2 - 4);
    centre.removeFromTop (28);
    stageArea = centre;
    pitchView.setBounds (pitchArea);
    stage.setBounds (stageArea);

    // Harmony panel
    {
        auto a = harmonyArea.reduced (space::m).withTrimmedTop (30);
        auto row = [&] (juce::Component& c) {
            c.setBounds (a.removeFromTop (44));
            a.removeFromTop (space::s);
        };
        row (mode);
        row (keyRoot);
        row (keyScale);
        row (arrangement);
        a.removeFromTop (space::s);
        auto knobs = a.removeFromTop (110);
        singers.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
        correction.setBounds (knobs);
    }

    // Voice designer: 2 columns x 4 rows
    {
        auto a = designerArea.reduced (space::m).withTrimmedTop (34);
        const int rowH = a.getHeight() / 4;
        vcsui::Knob* grid[4][2] = { { &humanize, &timing },
                                    { &pitchSpread, &vibrato },
                                    { &formantSpread, &formant },
                                    { &brightness, &width } };
        for (auto& rowKnobs : grid)
        {
            auto rowArea = a.removeFromTop (rowH);
            rowKnobs[0]->setBounds (rowArea.removeFromLeft (rowArea.getWidth() / 2).reduced (4));
            rowKnobs[1]->setBounds (rowArea.reduced (4));
        }
    }

    // Levels
    {
        auto a = outputArea.reduced (space::m).withTrimmedTop (30);
        auto meters = a.removeFromRight (100);
        inMeter.setBounds (meters.removeFromLeft (26));
        meters.removeFromLeft (space::l);
        outLMeter.setBounds (meters.removeFromLeft (26));
        meters.removeFromLeft (space::xs);
        outRMeter.setBounds (meters.removeFromLeft (26));
        a.removeFromRight (space::l);
        const int w = 96;
        leadLevel.setBounds (a.removeFromLeft (w));
        choirLevel.setBounds (a.removeFromLeft (w));
        output.setBounds (a.removeFromRight (w));
    }
}
} // namespace vcsplugin
