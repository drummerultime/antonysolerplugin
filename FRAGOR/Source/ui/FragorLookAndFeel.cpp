#include "FragorLookAndFeel.h"

namespace fragor
{
FragorLookAndFeel::FragorLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, Col::bg);
    setColour (juce::ComboBox::backgroundColourId, Col::panel2);
    setColour (juce::ComboBox::textColourId, Col::text);
    setColour (juce::ComboBox::outlineColourId, Col::border);
    setColour (juce::ComboBox::arrowColourId, Col::acid);
    setColour (juce::PopupMenu::backgroundColourId, Col::panel);
    setColour (juce::PopupMenu::textColourId, Col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Col::acid.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, Col::bg);
    setColour (juce::PopupMenu::headerTextColourId, Col::acid);
    setColour (juce::Label::textColourId, Col::text);
    setColour (juce::Slider::textBoxTextColourId, Col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::BubbleComponent::backgroundColourId, Col::bg.withAlpha (0.95f));
    setColour (juce::BubbleComponent::outlineColourId, Col::acid);
    setColour (juce::TooltipWindow::backgroundColourId, Col::panel2);
    setColour (juce::TooltipWindow::textColourId, Col::text);
    setColour (juce::TextButton::buttonColourId, Col::panel2);
    setColour (juce::TextButton::textColourOffId, Col::text);
    setColour (juce::TextButton::textColourOnId, Col::bg);
    setColour (juce::TextEditor::backgroundColourId, Col::panel2);
    setColour (juce::TextEditor::textColourId, Col::text);
    setColour (juce::TextEditor::outlineColourId, Col::border);
    setColour (juce::AlertWindow::backgroundColourId, Col::panel);
    setColour (juce::AlertWindow::textColourId, Col::text);
}

void FragorLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lw = juce::jmax (2.5f, r * 0.16f);
    const float arcR = r - lw * 0.5f;
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

    // fond
    g.setColour (Col::bg);
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f);
    g.setColour (Col::border);
    g.drawEllipse (c.x - r * 0.72f, c.y - r * 0.72f, r * 1.44f, r * 1.44f, 1.0f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (Col::border.brighter (0.1f));
    g.strokePath (track, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zeroAng = bipolar ? start + (end - start) * (float) s.valueToProportionOfLength (0.0) : start;
    const float ang = start + pos * (end - start);
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (zeroAng, ang), juce::jmax (zeroAng, ang), true);
    g.setColour (accent.withAlpha (s.isEnabled() ? 1.0f : 0.3f));
    g.strokePath (val, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointeur
    const float pr = r * 0.62f;
    juce::Path ptr;
    ptr.startNewSubPath (c.x + std::sin (ang) * pr * 0.25f, c.y - std::cos (ang) * pr * 0.25f);
    ptr.lineTo (c.x + std::sin (ang) * pr, c.y - std::cos (ang) * pr);
    g.setColour (Col::text);
    g.strokePath (ptr, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void FragorLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                          juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 3.0f, (float) w, 6.0f);
    g.setColour (Col::bg);
    g.fillRoundedRectangle (b, 3.0f);
    const float zero = (s.getMinimum() < 0.0) ? (float) x + (float) w * (float) s.valueToProportionOfLength (0.0) : (float) x;
    const float l = juce::jmin (zero, pos), r = juce::jmax (zero, pos);
    const bool neg = pos < zero;
    g.setColour (neg ? Col::hot : s.findColour (juce::Slider::rotarySliderFillColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> (l, b.getY(), juce::jmax (2.0f, r - l), b.getHeight()), 3.0f);
    g.setColour (Col::text);
    g.fillRect (juce::Rectangle<float> (pos - 1.0f, b.getY() - 3.0f, 2.0f, b.getHeight() + 6.0f));
}

void FragorLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (box.hasKeyboardFocus (false) ? Col::acid : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 11.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font FragorLookAndFeel::getComboBoxFont (juce::ComboBox& b)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) b.getHeight() * 0.6f), juce::Font::bold));
}

void FragorLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 1, box.getWidth() - 20, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void FragorLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    g.setColour (on ? accent.withAlpha (0.18f) : Col::panel2);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (on ? accent : (highlighted ? Col::dim.brighter() : Col::border));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const float led = juce::jmin (8.0f, r.getHeight() - 6.0f);
    const auto ledR = juce::Rectangle<float> (r.getX() + 5.0f, r.getCentreY() - led * 0.5f, led, led);
    g.setColour (on ? accent : Col::bg);
    g.fillEllipse (ledR);
    if (on)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.fillEllipse (ledR.expanded (2.5f));
    }
    g.setColour (on ? Col::text : Col::dim);
    g.setFont (juce::Font (juce::FontOptions (juce::jmin (12.0f, r.getHeight() * 0.62f), juce::Font::bold)));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (led + 9.0f), juce::Justification::centredLeft, false);
}

void FragorLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgc, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? Col::acid : bgc;
    if (down) c = c.brighter (0.3f);
    else if (highlighted) c = c.brighter (0.12f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (highlighted ? Col::acid : Col::border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

juce::Font FragorLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return juce::Font (juce::FontOptions (juce::jmin (13.0f, (float) h * 0.55f), juce::Font::bold));
}

juce::Font FragorLookAndFeel::getPopupMenuFont() { return juce::Font (juce::FontOptions (14.0f, juce::Font::bold)); }

juce::Label* FragorLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::Font (juce::FontOptions (11.0f)));
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return l;
}
}
