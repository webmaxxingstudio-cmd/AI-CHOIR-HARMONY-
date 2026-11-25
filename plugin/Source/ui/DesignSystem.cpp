#include "DesignSystem.h"

namespace vcsui
{
LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colour::background);
    setColour (juce::Label::textColourId, colour::text);
    setColour (juce::ComboBox::backgroundColourId, colour::well);
    setColour (juce::ComboBox::textColourId, colour::text);
    setColour (juce::ComboBox::outlineColourId, colour::line);
    setColour (juce::ComboBox::arrowColourId, colour::textDim);
    setColour (juce::PopupMenu::backgroundColourId, colour::panelRaised);
    setColour (juce::PopupMenu::textColourId, colour::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::text);
    setColour (juce::TextButton::buttonColourId, colour::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, colour::accentDim);
    setColour (juce::TextButton::textColourOffId, colour::textDim);
    setColour (juce::TextButton::textColourOnId, colour::text);
    setColour (juce::Slider::textBoxTextColourId, colour::text);
    setColour (juce::Slider::textBoxBackgroundColourId, colour::well);
    setColour (juce::Slider::textBoxOutlineColourId, colour::line);
    setColour (juce::Slider::textBoxHighlightColourId, colour::accentDim);
    setColour (juce::TooltipWindow::backgroundColourId, colour::panelRaised);
    setColour (juce::TooltipWindow::textColourId, colour::text);
    setColour (juce::TooltipWindow::outlineColourId, colour::lineStrong);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                    float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float trackW = juce::jmax (2.0f, radius * 0.09f);
    const float arcR = radius - trackW;

    // Bipolar parameters (range symmetric around 0) draw their arc from the centre.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float zeroPos = bipolar ? (float) slider.valueToProportionOfLength (0.0) : 0.0f;
    const float angle = start + pos * (end - start);
    const float fromAngle = start + zeroPos * (end - start);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (colour::line);
    g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto accent = slider.isEnabled() ? colour::accent : colour::textFaint;
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Knob body: subtle vertical gradient, hairline rim.
    const float bodyR = arcR - trackW * 2.2f;
    juce::ColourGradient grad (colour::panelRaised.brighter (0.12f), centre.x, centre.y - bodyR,
                               colour::panel.darker (0.3f), centre.x, centre.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre));
    g.setColour (colour::lineStrong);
    g.drawEllipse (juce::Rectangle<float> (bodyR * 2, bodyR * 2).withCentre (centre), 1.0f);

    // Pointer
    const juce::Point<float> tip (centre.x + std::sin (angle) * (bodyR - 3.0f), centre.y - std::cos (angle) * (bodyR - 3.0f));
    const juce::Point<float> inner (centre.x + std::sin (angle) * bodyR * 0.35f, centre.y - std::cos (angle) * bodyR * 0.35f);
    g.setColour (colour::text);
    g.drawLine ({ inner, tip }, 1.8f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
    g.setColour (colour::well);
    g.fillRoundedRectangle (r, space::radius);
    g.setColour (box.hasKeyboardFocus (false) ? colour::accentDim : colour::line);
    g.drawRoundedRectangle (r, space::radius, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (colour::textDim);
    g.strokePath (arrow, juce::PathStrokeType (1.3f));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto fill = b.getToggleState() ? colour::accentDim : colour::panelRaised;
    if (down)
        fill = fill.brighter (0.15f);
    else if (over)
        fill = fill.brighter (0.07f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, space::radius);
    g.setColour (b.getToggleState() ? colour::accent.withAlpha (0.6f) : colour::line);
    g.drawRoundedRectangle (r, space::radius, 1.0f);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colour::panelRaised);
    g.setColour (colour::lineStrong);
    g.drawRect (0, 0, w, h, 1);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    const auto r = area.toFloat().reduced (0.5f);
    g.setColour (colour::panel);
    g.fillRoundedRectangle (r, space::radius);
    g.setColour (colour::line);
    g.drawRoundedRectangle (r, space::radius, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (colour::textDim);
        g.setFont (type::caption());
        g.drawText (title.toUpperCase(), area.reduced (space::m, 0).withHeight (30), juce::Justification::centredLeft);
        g.setColour (colour::line);
        g.drawHorizontalLine (area.getY() + 30, (float) area.getX() + space::m, (float) area.getRight() - space::m);
    }
}
} // namespace vcsui
