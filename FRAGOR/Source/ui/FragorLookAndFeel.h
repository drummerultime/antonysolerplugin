#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace fragor
{
    namespace Col
    {
        const juce::Colour bg       { 0xff0a0b0f };
        const juce::Colour panel    { 0xff13151b };
        const juce::Colour panel2   { 0xff1a1d25 };
        const juce::Colour border   { 0xff262a35 };
        const juce::Colour acid     { 0xffa6ff00 };   // vert acide
        const juce::Colour hot      { 0xffff2e63 };   // rose/rouge
        const juce::Colour violet   { 0xff9b5cff };
        const juce::Colour text     { 0xffd9dde6 };
        const juce::Colour dim      { 0xff6b7280 };
    }

    class FragorLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        FragorLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int h) override;
        juce::Font getPopupMenuFont() override;
        juce::Label* createSliderTextBox (juce::Slider&) override;
    };
}
