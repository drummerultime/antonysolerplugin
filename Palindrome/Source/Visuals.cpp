#include "Visuals.h"

using namespace pal;

juce::Font theme::font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

static juce::Colour accentOf (const juce::Component& c, juce::Colour fallback = theme::normal)
{
    const auto v = c.getProperties()["accent"];
    return v.isVoid() ? fallback : juce::Colour ((juce::uint32) (juce::int64) v);
}

//==============================================================================
PalLookAndFeel::PalLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::Slider::textBoxTextColourId, theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, theme::dim);
    setColour (juce::ComboBox::backgroundColourId, theme::panel2);
    setColour (juce::ComboBox::outlineColourId, theme::outline);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::arrowColourId, theme::dim);
    setColour (juce::PopupMenu::backgroundColourId, theme::panel2);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::outline);
    setColour (juce::PopupMenu::highlightedTextColourId, theme::text);
    setColour (juce::PopupMenu::headerTextColourId, theme::dim);
    setColour (juce::TextButton::buttonColourId, theme::panel2);
    setColour (juce::TextButton::buttonOnColourId, theme::outline);
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, theme::text);
    setColour (juce::TextEditor::backgroundColourId, theme::panel2);
    setColour (juce::TextEditor::outlineColourId, theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::banque);
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::ToggleButton::textColourId, theme::text);
    setColour (juce::AlertWindow::backgroundColourId, theme::panel2);
    setColour (juce::AlertWindow::textColourId, theme::text);
    setColour (juce::AlertWindow::outlineColourId, theme::outline);
    setColour (juce::TooltipWindow::backgroundColourId, theme::panel2);
    setColour (juce::TooltipWindow::textColourId, theme::text);
    setColour (juce::TooltipWindow::outlineColourId, theme::outline);
}

void PalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                       float start, float end, juce::Slider& s)
{
    const auto accent = accentOf (s);
    const float alpha = s.isEnabled() ? 1.0f : 0.3f;
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float thick = juce::jmax (3.5f, radius * 0.13f);
    const float arcR = radius - thick;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties()["bipolar"];
    const float from = bipolar ? (start + end) * 0.5f : start;

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (theme::outline.withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (accent.withAlpha (0.18f * alpha));
        g.strokePath (value, juce::PathStrokeType (thick * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent.withMultipliedAlpha (alpha));
        g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // corps
    const float bodyR = arcR - thick * 1.4f;
    juce::ColourGradient grad (theme::panel2.brighter (0.12f), c.x, c.y - bodyR, theme::panel.darker (0.2f), c.x, c.y + bodyR, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
    g.setColour (theme::outline.withMultipliedAlpha (alpha));
    g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);

    // repère
    const juce::Point<float> p1 (c.x + std::sin (angle) * bodyR * 0.35f, c.y - std::cos (angle) * bodyR * 0.35f);
    const juce::Point<float> p2 (c.x + std::sin (angle) * bodyR * 0.85f, c.y - std::cos (angle) * bodyR * 0.85f);
    g.setColour (theme::text.withMultipliedAlpha (alpha));
    g.drawLine ({ p1, p2 }, 2.2f);
}

juce::Label* PalLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (theme::font (12.5f));
    l->setColour (juce::Label::textColourId, theme::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::TextEditor::backgroundColourId, theme::panel2);
    l->setColour (juce::TextEditor::textColourId, theme::text);
    l->setColour (juce::TextEditor::highlightColourId, accentOf (s).withAlpha (0.4f));
    l->setJustificationType (juce::Justification::centred);
    return l;
}

juce::Slider::SliderLayout PalLookAndFeel::getSliderLayout (juce::Slider& s)
{
    auto layout = LookAndFeel_V4::getSliderLayout (s);
    if (s.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag)
    {
        auto b = s.getLocalBounds();
        layout.textBoxBounds = b.removeFromBottom (18);
        layout.sliderBounds = b;
    }
    return layout;
}

void PalLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    const auto accent = accentOf (box, theme::dim);
    g.setColour (theme::panel2.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour ((box.hasKeyboardFocus (true) || box.isMouseOver (true) ? accent.withAlpha (0.8f) : theme::outline));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.5f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (theme::dim.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font PalLookAndFeel::getComboBoxFont (juce::ComboBox&)  { return theme::font (13.0f); }
juce::Font PalLookAndFeel::getPopupMenuFont()                  { return theme::font (14.0f); }
juce::Font PalLookAndFeel::getLabelFont (juce::Label& l)       { return l.getFont(); }

void PalLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void PalLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour,
                                           bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgColour;
    if (down) c = c.brighter (0.15f);
    else if (highlighted) c = c.brighter (0.07f);
    if (! b.isEnabled()) c = c.withMultipliedAlpha (0.45f);

    g.setColour (c);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (theme::outline.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
}

juce::Font PalLookAndFeel::getTextButtonFont (juce::TextButton&, int h)
{
    return theme::font (juce::jmin (14.0f, h * 0.48f), true);
}

void PalLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto accent = accentOf (b);
    auto r = b.getLocalBounds().toFloat();
    auto sw = r.removeFromLeft (36.0f).withSizeKeepingCentre (34.0f, 18.0f);
    const bool on = b.getToggleState();

    g.setColour (on ? accent.withAlpha (0.85f) : theme::outline);
    g.fillRoundedRectangle (sw, 9.0f);
    const float kx = on ? sw.getRight() - 16.0f : sw.getX() + 2.0f;
    g.setColour (on ? theme::bg : (highlighted ? theme::text : theme::dim));
    g.fillEllipse (kx, sw.getY() + 2.0f, 14.0f, 14.0f);

    g.setColour (theme::text.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.setFont (theme::font (12.5f));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (6), juce::Justification::centredLeft, 2);
}

juce::Rectangle<int> PalLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    juce::AttributedString s;
    s.append (text, theme::font (13.0f), theme::text);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, 320.0f);
    const int w = (int) tl.getWidth() + 24, h = (int) tl.getHeight() + 18;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 12) : pos.y + 18, w, h)
               .constrainedWithin (parent);
}

void PalLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
    g.setColour (theme::panel2);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (theme::outline.brighter (0.2f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    juce::AttributedString s;
    s.append (text, theme::font (13.0f), theme::text);
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths (s, (float) w - 24.0f);
    tl.draw (g, r.reduced (12.0f, 9.0f));
}

//==============================================================================
SegmentedControl::SegmentedControl (juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
                                    juce::StringArray l, juce::Colour a)
    : param (*apvts.getParameter (id)), labels (std::move (l)), accent (a)
{
    attachment = std::make_unique<juce::ParameterAttachment> (param, [this] (float v)
    {
        index = juce::roundToInt (v);
        repaint();
    }, nullptr);
    attachment->sendInitialUpdate();
    setRepaintsOnMouseActivity (true);
}

int SegmentedControl::segmentAt (float x) const
{
    if (labels.isEmpty()) return -1;
    return juce::jlimit (0, labels.size() - 1, (int) (x / ((float) getWidth() / (float) labels.size())));
}

void SegmentedControl::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    const int k = segmentAt (e.position.x);
    if (k >= 0) attachment->setValueAsCompleteGesture ((float) k);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& e) { hover = segmentAt (e.position.x); repaint(); }
void SegmentedControl::mouseExit (const juce::MouseEvent&)    { hover = -1; repaint(); }

void SegmentedControl::paint (juce::Graphics& g)
{
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel2.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (theme::outline.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    const int n = labels.size();
    const float w = r.getWidth() / (float) juce::jmax (1, n);
    for (int i = 0; i < n; ++i)
    {
        auto seg = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight()).reduced (2.0f);
        if (i == index)
        {
            g.setColour (accent.withAlpha (0.92f * alpha));
            g.fillRoundedRectangle (seg, 5.5f);
        }
        else if (i == hover && isEnabled())
        {
            g.setColour (theme::outline);
            g.fillRoundedRectangle (seg, 5.5f);
        }
        else if (i > 0 && i != index + 1)
        {
            g.setColour (theme::outline.withMultipliedAlpha (alpha));
            g.drawVerticalLine ((int) seg.getX() - 2, seg.getY() + 5.0f, seg.getBottom() - 5.0f);
        }

        g.setColour ((i == index ? theme::bg : theme::text).withMultipliedAlpha (alpha));
        g.setFont (theme::font (12.5f, i == index));
        g.drawFittedText (labels[i], seg.toNearestInt().reduced (3, 0), juce::Justification::centred, 1, 0.8f);
    }
}

//==============================================================================
IconButton::IconButton (const juce::String& t, Icon i, juce::Colour a) : juce::TextButton (t), icon (i), accent (a) {}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const bool on = getToggleState();
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    auto r = getLocalBounds().toFloat().reduced (0.5f);

    juce::Colour fill = on ? accent : theme::panel2;
    if (icon == Icon::Record && ! on) fill = accent.withAlpha (0.18f);
    if (down) fill = fill.brighter (0.2f); else if (highlighted) fill = fill.brighter (0.08f);
    g.setColour (fill.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour ((on || icon == Icon::Record ? accent : theme::outline).withMultipliedAlpha (alpha * (on ? 1.0f : 0.8f)));
    g.drawRoundedRectangle (r, 9.0f, 1.2f);

    const auto fg = (on ? theme::bg : (icon == Icon::Record ? accent : theme::text)).withMultipliedAlpha (alpha);
    const bool hasText = getButtonText().isNotEmpty();
    auto iconArea = hasText ? r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.3f)
                            : r.withSizeKeepingCentre (r.getHeight() * 0.42f, r.getHeight() * 0.42f);
    if (hasText) r.removeFromLeft (-4.0f);

    g.setColour (fg);
    juce::Path p;
    const auto ic = iconArea;
    switch (icon)
    {
        case Icon::Record: g.fillEllipse (ic.reduced (ic.getWidth() * 0.1f)); break;
        case Icon::Hold:
        {
            // boucle infinie
            const float cx = ic.getCentreX(), cy = ic.getCentreY(), rx = ic.getWidth() * 0.26f, ry = ic.getHeight() * 0.28f;
            p.addEllipse (cx - rx * 2.0f, cy - ry, rx * 2.0f, ry * 2.0f);
            p.addEllipse (cx, cy - ry, rx * 2.0f, ry * 2.0f);
            g.strokePath (p, juce::PathStrokeType (1.8f));
            break;
        }
        case Icon::Next:
            p.addTriangle (ic.getX(), ic.getY(), ic.getX(), ic.getBottom(), ic.getRight() - ic.getWidth() * 0.25f, ic.getCentreY());
            p.addRectangle (ic.getRight() - ic.getWidth() * 0.2f, ic.getY(), ic.getWidth() * 0.2f, ic.getHeight());
            g.fillPath (p);
            break;
        case Icon::Clear:
            g.drawLine (ic.getX(), ic.getY(), ic.getRight(), ic.getBottom(), 2.0f);
            g.drawLine (ic.getRight(), ic.getY(), ic.getX(), ic.getBottom(), 2.0f);
            break;
        case Icon::Save:
            p.addRoundedRectangle (ic, 2.0f);
            g.strokePath (p, juce::PathStrokeType (1.6f));
            g.fillRect (ic.reduced (ic.getWidth() * 0.28f, 0).withHeight (ic.getHeight() * 0.35f));
            break;
        case Icon::Prev:
            p.addTriangle (ic.getRight(), ic.getY(), ic.getRight(), ic.getBottom(), ic.getX(), ic.getCentreY());
            g.fillPath (p);
            break;
        case Icon::Forward:
            p.addTriangle (ic.getX(), ic.getY(), ic.getX(), ic.getBottom(), ic.getRight(), ic.getCentreY());
            g.fillPath (p);
            break;
        case Icon::Undo:
        case Icon::Redo:
        {
            // flèche courbe
            const bool undo = icon == Icon::Undo;
            const float cx = ic.getCentreX(), cy = ic.getCentreY() + ic.getHeight() * 0.1f, rad = ic.getWidth() * 0.42f;
            p.addCentredArc (cx, cy, rad, rad, 0.0f, undo ? -1.9f : 1.9f, undo ? 1.2f : -1.2f, true);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto tip = p.getCurrentPosition();
            juce::Path head;
            const float hs = ic.getWidth() * 0.32f;
            const float dir = undo ? -1.0f : 1.0f;
            const float sx = cx + dir * rad * std::sin (1.9f), sy = cy - rad * std::cos (1.9f);
            juce::ignoreUnused (tip);
            head.addTriangle (sx - hs * 0.6f, sy - hs * 0.9f, sx + hs * 0.6f, sy - hs * 0.9f + dir * 0.0f, sx, sy + hs * 0.2f);
            head.applyTransform (juce::AffineTransform::rotation (dir * 0.6f, sx, sy));
            g.fillPath (head);
            break;
        }
        case Icon::Midi:
        {
            // prise DIN 5 broches
            g.drawEllipse (ic.reduced (0.5f), 1.6f);
            const float cx = ic.getCentreX(), cy = ic.getCentreY(), rr = ic.getWidth() * 0.28f, d = ic.getWidth() * 0.13f;
            for (int k = 0; k < 5; ++k)
            {
                const float ang = juce::MathConstants<float>::pi * (1.0f + (float) k * 0.25f);
                g.fillEllipse (cx + rr * std::cos (ang) - d * 0.5f, cy - rr * std::sin (ang) * -1.0f - d * 0.5f, d, d);
            }
            break;
        }
        case Icon::Size:
        {
            const float m = ic.getWidth() * 0.35f;
            g.drawLine (ic.getX(), ic.getBottom(), ic.getRight(), ic.getY(), 1.6f);
            p.addTriangle (ic.getRight(), ic.getY(), ic.getRight() - m, ic.getY(), ic.getRight(), ic.getY() + m);
            p.addTriangle (ic.getX(), ic.getBottom(), ic.getX() + m, ic.getBottom(), ic.getX(), ic.getBottom() - m);
            g.fillPath (p);
            break;
        }
        case Icon::Tap:
        {
            g.fillEllipse (ic.reduced (ic.getWidth() * 0.3f));
            g.drawEllipse (ic.reduced (1.0f), 1.2f);
            break;
        }
        case Icon::Copy:
        {
            auto b1 = ic.withTrimmedRight (ic.getWidth() * 0.3f).withTrimmedBottom (ic.getHeight() * 0.3f);
            auto b2 = ic.withTrimmedLeft (ic.getWidth() * 0.3f).withTrimmedTop (ic.getHeight() * 0.3f);
            g.drawRoundedRectangle (b1, 1.5f, 1.4f);
            g.fillRoundedRectangle (b2, 1.5f);
            break;
        }
    }

    if (hasText)
    {
        g.setColour (fg);
        g.setFont (theme::font (13.5f, true));
        g.drawFittedText (getButtonText(), r.toNearestInt().withTrimmedRight (8), juce::Justification::centredLeft, 1);
    }
}
