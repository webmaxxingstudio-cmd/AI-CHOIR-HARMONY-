#include "Widgets.h"

#include "vcs/MusicTheory.h"

namespace vcsui
{
namespace
{
    juce::String noteName (int midi)
    {
        if (midi < 0)
            return "-";
        return juce::String (vcs::pitchClassName (midi % 12)) + juce::String (midi / 12 - 1);
    }

    float gainToMeter (float g)
    {
        const float db = juce::Decibels::gainToDecibels (g, -60.0f);
        return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 54.0f); // -48..+6 dBFS
    }
} // namespace

// ---------------------------------------------------------------- Knob
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& cap,
            const juce::String& tooltip)
    : caption (cap)
{
    param = state.getParameter (paramId);
    jassert (param != nullptr);
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * -0.75f, juce::MathConstants<float>::pi * 0.75f, true);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (30);
    const int side = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (side, side));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().removeFromBottom (30);
    g.setFont (type::caption());
    g.setColour (colour::textDim);
    g.drawText (caption.toUpperCase(), r.removeFromTop (14), juce::Justification::centred);

    auto text = param->getText (param->convertTo0to1 ((float) slider.getValue()), 0);
    if (param->getLabel().isNotEmpty())
        text << " " << param->getLabel();
    g.setFont (type::value());
    g.setColour (slider.isMouseOverOrDragging() ? colour::accent : colour::text);
    g.drawText (text, r, juce::Justification::centred);
}

// ---------------------------------------------------------------- Choice
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& cap,
                const juce::String& tooltip)
    : caption (cap)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        combo.addItemList (p->choices, 1);
    combo.setTooltip (tooltip);
    addAndMakeVisible (combo);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramId, combo);
}

void Choice::resized() { combo.setBounds (getLocalBounds().withTrimmedTop (16)); }

void Choice::paint (juce::Graphics& g)
{
    g.setFont (type::caption());
    g.setColour (colour::textDim);
    g.drawText (caption.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- Meter
void Meter::setLevel (float linear)
{
    level = linear;
    if (linear >= hold)
    {
        hold = linear;
        holdFrames = 45;
    }
    else if (--holdFrames <= 0)
    {
        hold *= 0.92f;
    }
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto labelArea = r.removeFromBottom (14);
    g.setFont (type::caption());
    g.setColour (colour::textDim);
    g.drawText (name, labelArea, juce::Justification::centred);

    const auto bar = r.toFloat();
    g.setColour (colour::well);
    g.fillRect (bar);
    g.setColour (colour::line);
    g.drawRect (bar, 1.0f);

    const auto inner = bar.reduced (2.0f);
    const float h = inner.getHeight() * gainToMeter (level);
    const float zeroDb = inner.getBottom() - inner.getHeight() * (48.0f / 54.0f);
    const float warnDb = inner.getBottom() - inner.getHeight() * (42.0f / 54.0f);
    const auto filled = inner.withTop (inner.getBottom() - h);

    g.setColour (colour::meterGreen);
    g.fillRect (filled.withTop (juce::jmax (filled.getY(), warnDb)));
    if (filled.getY() < warnDb)
    {
        g.setColour (colour::meterAmber);
        g.fillRect (filled.withTop (juce::jmax (filled.getY(), zeroDb)).withBottom (warnDb));
    }
    if (filled.getY() < zeroDb)
    {
        g.setColour (colour::warn);
        g.fillRect (filled.withBottom (zeroDb));
    }

    const float holdY = inner.getBottom() - inner.getHeight() * gainToMeter (hold);
    g.setColour (hold > 1.0f ? colour::warn : colour::text.withAlpha (0.7f));
    g.drawHorizontalLine ((int) holdY, inner.getX(), inner.getRight());
    g.setColour (colour::textFaint);
    g.drawHorizontalLine ((int) zeroDb, bar.getX(), bar.getRight());
}

// ---------------------------------------------------------------- PitchView
void PitchView::tick()
{
    const float hz = telemetry.leadHz.load();
    const float midi = hz > 0.0f ? vcs::hzToMidi (hz) : 0.0f;
    history[(size_t) writePos] = midi;
    writePos = (writePos + 1) % kHistory;
    if (midi > 0.0f)
        centreNote += (midi - centreNote) * 0.05f; // view follows the singer gently
    repaint();
}

void PitchView::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (colour::well);
    g.fillRoundedRectangle (area, space::radius);

    const float span = 26.0f; // semitones visible
    const float lo = std::round (centreNote) - span * 0.5f;
    const float rowH = area.getHeight() / span;
    auto yFor = [&] (float midi) { return area.getBottom() - (midi - lo) * rowH; };

    const bool keyKnown = telemetry.keyDetected.load() || ! telemetry.keyAuto.load();
    const vcs::Key key { telemetry.keyRoot.load(), (vcs::ScaleType) telemetry.keyScale.load() };

    g.setFont (type::caption());
    for (int n = (int) lo; n <= (int) (lo + span); ++n)
    {
        const float y = yFor ((float) n + 0.5f);
        const auto row = juce::Rectangle<float> (area.getX(), y, area.getWidth(), rowH);
        if (keyKnown && key.scale != vcs::ScaleType::Chromatic && key.contains (n))
        {
            g.setColour (colour::panelRaised.withAlpha (n % 12 == key.root ? 0.9f : 0.45f));
            g.fillRect (row.reduced (0.0f, 0.5f));
        }
        if (n % 12 == 0)
        {
            g.setColour (colour::textFaint);
            g.drawText (noteName (n), row.withWidth (30.0f).translated (4.0f, 0.0f), juce::Justification::centredLeft);
        }
    }

    // Choir target notes on the right edge
    for (int v = 0; v < vcs::ChoirEngine::kMaxVoices; ++v)
    {
        const int note = telemetry.voiceNote[(size_t) v].load();
        if (note < 0 || telemetry.voiceLevel[(size_t) v].load() < 1.0e-4f)
            continue;
        const float y = yFor ((float) note);
        g.setColour (colour::accent);
        g.fillRect (juce::Rectangle<float> (area.getRight() - 14.0f, y - 1.5f, 10.0f, 3.0f));
    }

    // Lead pitch trace
    const float xStep = (area.getWidth() - 20.0f) / (float) (kHistory - 1);
    juce::Path trace;
    bool drawing = false;
    for (int i = 0; i < kHistory; ++i)
    {
        const float m = history[(size_t) ((writePos + i) % kHistory)];
        const float x = area.getX() + (float) i * xStep;
        if (m <= 0.0f || m < lo - 2 || m > lo + span + 2)
        {
            drawing = false;
            continue;
        }
        if (! drawing)
            trace.startNewSubPath (x, yFor (m));
        else
            trace.lineTo (x, yFor (m));
        drawing = true;
    }
    g.setColour (colour::lead);
    g.strokePath (trace, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colour::line);
    g.drawRoundedRectangle (area.reduced (0.5f), space::radius, 1.0f);
}

// ---------------------------------------------------------------- ChoirStage
void ChoirStage::tick()
{
    for (int v = 0; v < vcs::ChoirEngine::kMaxVoices; ++v)
    {
        const float l = telemetry.voiceLevel[(size_t) v].load();
        smoothLevel[(size_t) v] = juce::jmax (l, smoothLevel[(size_t) v] * 0.85f);
    }
    repaint();
}

void ChoirStage::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (colour::well);
    g.fillRoundedRectangle (area, space::radius);

    // Stage floor in perspective: a few receding arcs, very low contrast.
    const auto floor = area.reduced (24.0f, 18.0f);
    g.setColour (colour::line.withAlpha (0.7f));
    for (int i = 0; i < 4; ++i)
    {
        const float t = (float) i / 3.0f;
        const float w = floor.getWidth() * (0.55f + 0.45f * t);
        const float y = floor.getY() + floor.getHeight() * (0.15f + 0.8f * t);
        juce::Path arc;
        arc.startNewSubPath (floor.getCentreX() - w * 0.5f, y);
        arc.quadraticTo (floor.getCentreX(), y + 14.0f * (0.5f + t), floor.getCentreX() + w * 0.5f, y);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
    }
    g.setFont (type::caption());
    g.setColour (colour::textFaint);
    g.drawText ("L", area.reduced (8.0f).removeFromLeft (12.0f), juce::Justification::centredLeft);
    g.drawText ("R", area.reduced (8.0f).removeFromRight (12.0f), juce::Justification::centredRight);

    const int leadNote = telemetry.leadNote.load();
    const float leadX = floor.getCentreX();
    const float leadY = floor.getBottom() + 6.0f;

    for (int v = 0; v < vcs::ChoirEngine::kMaxVoices; ++v)
    {
        if (v >= telemetry.activeVoices.load())
            break;
        const float pan = telemetry.voicePan[(size_t) v].load();
        const int note = telemetry.voiceNote[(size_t) v].load();
        const float rel = (note >= 0 && leadNote >= 0) ? juce::jlimit (-14.0f, 14.0f, (float) (note - leadNote)) : 0.0f;
        const float x = floor.getCentreX() + pan * floor.getWidth() * 0.47f;
        const float y = floor.getCentreY() - rel / 14.0f * floor.getHeight() * 0.42f;
        const float lvl = juce::jlimit (0.0f, 1.0f, gainToMeter (smoothLevel[(size_t) v] * 4.0f));
        const float r = 5.0f + 5.0f * lvl;

        g.setColour (colour::accent.withAlpha (0.12f + 0.25f * lvl));
        g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre ({ x, y }));
        g.setColour (colour::accent.withAlpha (0.35f + 0.65f * lvl));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre ({ x, y }));
        g.setColour (colour::textDim);
        g.drawText (noteName (note), juce::Rectangle<float> (40.0f, 12.0f).withCentre ({ x, y + r + 9.0f }),
                    juce::Justification::centred);
    }

    // Lead vocal, front and centre
    g.setColour (colour::lead);
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ leadX, leadY }));
    g.setColour (colour::lead.withAlpha (0.8f));
    g.drawText ("LEAD " + noteName (leadNote), juce::Rectangle<float> (leadX + 12.0f, leadY - 6.0f, 80.0f, 12.0f),
                juce::Justification::centredLeft);

    g.setColour (colour::line);
    g.drawRoundedRectangle (area.reduced (0.5f), space::radius, 1.0f);
}
} // namespace vcsui
