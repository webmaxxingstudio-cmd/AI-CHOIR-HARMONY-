#pragma once
// Design system: colour, type and spacing tokens plus the custom LookAndFeel.
// Direction: dark, precise, restrained. One cool accent (choir), one warm
// accent (lead voice), thin strokes, no decorative chrome.

#include <juce_gui_basics/juce_gui_basics.h>

namespace vcsui
{
namespace colour
{
    inline const juce::Colour background { 0xff0d0f12 };
    inline const juce::Colour panel { 0xff14171c };
    inline const juce::Colour panelRaised { 0xff1a1e24 };
    inline const juce::Colour well { 0xff0a0c0f };      // recessed areas: displays, meters
    inline const juce::Colour line { 0xff262b33 };
    inline const juce::Colour lineStrong { 0xff353b45 };
    inline const juce::Colour text { 0xffe4e7eb };
    inline const juce::Colour textDim { 0xff8b929d };
    inline const juce::Colour textFaint { 0xff5a616b };
    inline const juce::Colour accent { 0xff6fd3c7 };     // choir / active controls
    inline const juce::Colour accentDim { 0xff2f5f5a };
    inline const juce::Colour lead { 0xffe3b35b };       // lead vocal
    inline const juce::Colour warn { 0xffe0685a };
    inline const juce::Colour meterGreen { 0xff5fcf8a };
    inline const juce::Colour meterAmber { 0xffe3b35b };
} // namespace colour

namespace space
{
    constexpr int xs = 4, s = 8, m = 12, l = 16, xl = 24;
    constexpr int headerHeight = 52;
    constexpr float radius = 3.0f; // deliberately small: panels, not "cards"
} // namespace space

namespace type
{
    inline juce::Font caption() { return juce::Font (juce::FontOptions (10.5f, juce::Font::bold).withKerningFactor (0.08f)); }
    inline juce::Font label() { return juce::Font (juce::FontOptions (12.0f)); }
    inline juce::Font value() { return juce::Font (juce::FontOptions (12.0f, juce::Font::bold)); }
    inline juce::Font readout() { return juce::Font (juce::FontOptions (17.0f, juce::Font::bold)); }
    inline juce::Font title() { return juce::Font (juce::FontOptions (15.0f, juce::Font::bold).withKerningFactor (0.12f)); }
} // namespace type

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return type::label(); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return type::caption(); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getPopupMenuFont() override { return type::label(); }
};

/** Draws a titled panel background (flat fill, hairline border, small caps title). */
void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title);
} // namespace vcsui
