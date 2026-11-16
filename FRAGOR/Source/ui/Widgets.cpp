#include "Widgets.h"
#include "../PluginProcessor.h"
#include "../dsp/Wavetables.h"
#include "../dsp/Warp.h"
#include "../dsp/Engine.h"

namespace fragor
{
namespace
{
    juce::Font font (float h, bool bold = true) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    constexpr float kRotStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float kRotEnd   = juce::MathConstants<float>::pi * 2.75f;

    struct DestEntry { const char* id; int dest; };
    const DestEntry kDestMap[] = {
        { "aPos", dstAPos }, { "aWarp", dstAWarp }, { "aLevel", dstALevel }, { "aSemi", dstAPitch },
        { "bPos", dstBPos }, { "bWarp", dstBWarp }, { "bLevel", dstBLevel }, { "bSemi", dstBPitch },
        { "aDetune", dstDetune }, { "bDetune", dstDetune },
        { "subLevel", dstSubLevel }, { "noiseLevel", dstNoiseLevel },
        { "fltCutoff", dstCutoff }, { "fltReso", dstReso }, { "fltMorph", dstMorph }, { "fltDrive", dstFltDrive }, { "fltMix", dstFltMix },
        { "f2Cutoff", dstCutoff2 }, { "f2Reso", dstReso2 }, { "f2Morph", dstMorph2 },
        { "l1Rate", dstLfo1Rate }, { "l2Rate", dstLfo2Rate },
        { "distDrive", dstDistDrive }, { "distMix", dstDistMix }, { "dist2Drive", dstDist2Drive }, { "dist2Mix", dstDist2Mix },
        { "crushRate", dstCrush }, { "ottDepth", dstOttDepth }, { "dlyMix", dstDelayMix }, { "revMix", dstReverbMix },
    };

    void setParam (APVTS& s, const juce::String& id, float realValue)
    {
        if (auto* p = s.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (realValue));
            p->endChangeGesture();
        }
    }
}

int destForParam (const juce::String& id)
{
    for (auto& e : kDestMap) if (id == e.id) return e.dest;
    return -1;
}

float defaultModAmount (int dest)
{
    switch (dest)
    {
        case dstCutoff: case dstCutoff2: return 0.4f;
        case dstAPitch: case dstBPitch:  return 0.5f / 24.0f * 12.0f; // une octave
        case dstLfo1Rate: case dstLfo2Rate: return 0.25f;
        default: return 0.5f;
    }
}

float modulatedNorm (int dest, juce::RangedAudioParameter& p, float baseNorm, float delta)
{
    const float real = p.convertFrom0to1 (baseNorm);
    const auto& r = p.getNormalisableRange();
    float v;
    switch (dest)
    {
        case dstCutoff: case dstCutoff2:
        {
            const float n = std::log2 (std::max (20.0f, real) / 20.0f) / 9.965784f + delta;
            v = 20.0f * std::exp2 (juce::jlimit (0.0f, 1.0f, n) * 9.965784f);
            break;
        }
        case dstLfo1Rate: case dstLfo2Rate: v = real * std::exp2 (delta * 4.0f); break;
        case dstAPitch: case dstBPitch:     v = real + delta * 24.0f; break;
        default:                            v = real + delta * (r.end - r.start); break;
    }
    return p.convertTo0to1 (juce::jlimit (r.start, r.end, v));
}

// ============================================================================ ModSlider
void ModSlider::mouseDown (const juce::MouseEvent& e)
{
    if (onModDown && onModDown (e)) { modGesture = true; return; }
    modGesture = false;
    Slider::mouseDown (e);
}
void ModSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (modGesture) { if (onModDrag) onModDrag (e); return; }
    Slider::mouseDrag (e);
}
void ModSlider::mouseUp (const juce::MouseEvent& e)
{
    if (modGesture) { modGesture = false; if (onModUp) onModUp(); return; }
    Slider::mouseUp (e);
}

// ============================================================================ Knob
Knob::Knob (APVTS& s, const juce::String& id, const juce::String& lbl, juce::Colour accent) : label (lbl), state (s), paramId (id)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setPopupDisplayEnabled (true, true, nullptr, 1500);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setRotaryParameters (kRotStart, kRotEnd, true);
    slider.setMouseDragSensitivity (220);
    addAndMakeVisible (slider);
    att = std::make_unique<APVTS::SliderAttachment> (s, id, slider);
    if (auto* p = s.getParameter (id))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (p->getName (64));
    }
    slider.onModDown = [this] (const juce::MouseEvent& e) { return modDown (e); };
    slider.onModDrag = [this] (const juce::MouseEvent& e) { modDrag (e); };
    slider.onModUp   = [this]
    {
        if (dragSlot >= 0)
            if (auto* p = state.getParameter ("m" + juce::String (dragSlot + 1) + "Amt")) p->endChangeGesture();
        dragSlot = -1;
    };
}

void Knob::attachMod (FragorProcessor* p, int d)
{
    proc = p;
    dest = d;
    if (dest >= 0)
        slider.setTooltip (slider.getTooltip() + juce::String::fromUTF8 ("  —  glisse une source ici pour moduler, Alt+glisser = quantité, clic droit = modulations"));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (14);
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (Col::dim.brighter (0.4f));
    g.setFont (font (10.5f));
    g.drawText (label, getLocalBounds().removeFromBottom (14), juce::Justification::centred, false);
}

int Knob::lastSlotFor() const
{
    int found = -1;
    for (int s = 0; s < kNumMatrixSlots; ++s)
    {
        const juce::String p = "m" + juce::String (s + 1);
        if ((int) state.getRawParameterValue (p + "Dst")->load() == dest && (int) state.getRawParameterValue (p + "Src")->load() > 0)
            found = s;
    }
    return found;
}

bool Knob::hasMods() const { return dest >= 0 && lastSlotFor() >= 0; }

void Knob::paintOverChildren (juce::Graphics& g)
{
    if (dropHover)
    {
        g.setColour (Col::hot.withAlpha (0.25f));
        g.fillEllipse (slider.getBounds().toFloat().reduced (2.0f));
    }
    if (dest < 0 || proc == nullptr) return;
    auto* p = state.getParameter (paramId);
    if (p == nullptr) return;

    float lo = 0.0f, hi = 0.0f;
    bool any = false;
    const auto srcNames = modSourceNames();
    for (int s = 0; s < kNumMatrixSlots; ++s)
    {
        const juce::String id = "m" + juce::String (s + 1);
        if ((int) state.getRawParameterValue (id + "Dst")->load() != dest) continue;
        const int src = (int) state.getRawParameterValue (id + "Src")->load();
        if (src <= 0) continue;
        const float a = state.getRawParameterValue (id + "Amt")->load();
        const float sMin = src == srcNote ? -1.0f : 0.0f;
        lo += std::min ({ 0.0f, a * sMin, a });
        hi += std::max ({ 0.0f, a * sMin, a });
        any = true;
    }
    if (! any) return;

    const float base = p->getValue();
    const float nLo = modulatedNorm (dest, *p, base, lo);
    const float nHi = modulatedNorm (dest, *p, base, hi);
    const auto b = slider.getBounds().toFloat().reduced (3.0f);
    const float rr = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f + 1.5f;
    const auto c = b.getCentre();
    auto ang = [] (float n) { return kRotStart + n * (kRotEnd - kRotStart); };

    juce::Path range;
    range.addCentredArc (c.x, c.y, rr, rr, 0.0f, ang (nLo), ang (nHi), true);
    g.setColour (Col::violet.withAlpha (0.85f));
    g.strokePath (range, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (proc->activeVoices.load() > 0)
    {
        const float live = proc->liveMod[dest].load();
        const float a = ang (modulatedNorm (dest, *p, base, live));
        const float x = c.x + std::sin (a) * rr, y = c.y - std::cos (a) * rr;
        g.setColour (Col::hot);
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
}

bool Knob::modDown (const juce::MouseEvent& e)
{
    if (dest < 0 || proc == nullptr) return false;
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader ("MODULATIONS > " + label);
        const auto srcNames = modSourceNames();
        bool any = false;
        for (int s = 0; s < kNumMatrixSlots; ++s)
        {
            const juce::String id = "m" + juce::String (s + 1);
            if ((int) state.getRawParameterValue (id + "Dst")->load() != dest) continue;
            const int src = (int) state.getRawParameterValue (id + "Src")->load();
            if (src <= 0) continue;
            const int pct = (int) std::lround (state.getRawParameterValue (id + "Amt")->load() * 100.0f);
            m.addItem (100 + s, "Retirer " + srcNames[src] + "  (" + juce::String (pct) + " %)");
            any = true;
        }
        if (! any) m.addItem (-1, "Glisse une source (LFO, ENV, MACRO, VEL...) sur ce bouton", false);
        juce::PopupMenu add;
        for (int s = 1; s < srcNames.size(); ++s) add.addItem (200 + s, srcNames[s]);
        m.addSeparator();
        m.addSubMenu ("Ajouter une source", add);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
        {
            if (r >= 200) proc->addModulation (r - 200, dest, defaultModAmount (dest));
            else if (r >= 100)
            {
                const juce::String id = "m" + juce::String (r - 100 + 1);
                setParam (state, id + "Src", 0.0f);
                setParam (state, id + "Dst", 0.0f);
                setParam (state, id + "Amt", 0.0f);
            }
            repaint();
        });
        return true;
    }
    if (e.mods.isAltDown())
    {
        dragSlot = lastSlotFor();
        if (dragSlot < 0) return false;
        auto* p = state.getParameter ("m" + juce::String (dragSlot + 1) + "Amt");
        dragStartAmt = state.getRawParameterValue ("m" + juce::String (dragSlot + 1) + "Amt")->load();
        p->beginChangeGesture();
        return true;
    }
    return false;
}

void Knob::modDrag (const juce::MouseEvent& e)
{
    if (dragSlot < 0) return;
    const float amt = juce::jlimit (-1.0f, 1.0f, dragStartAmt - (float) e.getDistanceFromDragStartY() / 150.0f);
    if (auto* p = state.getParameter ("m" + juce::String (dragSlot + 1) + "Amt"))
        p->setValueNotifyingHost (p->convertTo0to1 (amt));
    repaint();
}

bool Knob::isInterestedInDragSource (const SourceDetails& d)
{
    return dest >= 0 && proc != nullptr && d.description.toString().startsWith ("modsrc:");
}

void Knob::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (proc != nullptr && src > 0)
    {
        if (proc->addModulation (src, dest, defaultModAmount (dest)) < 0)
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "FRAGOR", "Les 12 slots de la matrice sont pleins.");
    }
    repaint();
}

// ============================================================================ Choice / Toggle / Panel
Choice::Choice (APVTS& s, const juce::String& id, const juce::String& lbl) : label (lbl)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    att = std::make_unique<APVTS::ComboBoxAttachment> (s, id, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    if (label.isNotEmpty()) r.removeFromTop (13);
    box.setBounds (r);
}

void Choice::paint (juce::Graphics& g)
{
    if (label.isEmpty()) return;
    g.setColour (Col::dim.brighter (0.4f));
    g.setFont (font (10.5f));
    g.drawText (label, getLocalBounds().removeFromTop (13), juce::Justification::centredLeft, false);
}

Toggle::Toggle (APVTS& s, const juce::String& id, const juce::String& text, juce::Colour accent)
{
    button.setButtonText (text);
    button.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (button);
    att = std::make_unique<APVTS::ButtonAttachment> (s, id, button);
}

void Panel::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Col::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Col::border);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (juce::Rectangle<float> (r.getX() + 8.0f, r.getY() + 9.0f, 3.0f, 12.0f), 1.5f);
    g.setColour (Col::text);
    g.setFont (font (13.0f));
    g.drawText (title, juce::Rectangle<float> (r.getX() + 16.0f, r.getY() + 5.0f, r.getWidth() - 20.0f, 20.0f),
                juce::Justification::centredLeft, false);
}

// ============================================================================ DragChip
DragChip::DragChip (const juce::String& t, int src, juce::Colour c) : text (t), source (src), colour (c)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Clic-glisser sur un bouton pour le moduler par " + t);
}

void DragChip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? colour : Col::panel2);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (selected ? colour : colour.withAlpha (0.7f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    // poignée
    g.setColour (selected ? Col::bg : colour);
    for (int i = 0; i < 3; ++i)
        g.fillEllipse (r.getX() + 4.0f, r.getCentreY() - 4.5f + i * 3.5f, 2.0f, 2.0f);
    g.setColour (selected ? Col::bg : Col::text);
    g.setFont (font (juce::jmin (12.0f, r.getHeight() * 0.62f)));
    g.drawText (text, r.withTrimmedLeft (8.0f), juce::Justification::centred, false);
}

void DragChip::mouseDown (const juce::MouseEvent&) { dragging = false; }

void DragChip::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() > 4)
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            c->startDragging ("modsrc:" + juce::String (source), this);
        }
}

void DragChip::mouseUp (const juce::MouseEvent&)
{
    if (! dragging && onClick) onClick();
    dragging = false;
}

// ============================================================================ WaveDisplay
WaveDisplay::WaveDisplay (FragorProcessor& p, const juce::String& pre) : proc (p), prefix (pre) { startTimerHz (30); }

void WaveDisplay::timerCallback()
{
    auto& s = proc.apvts;
    const float h = s.getRawParameterValue (prefix + "Table")->load() * 1000.0f
                  + s.getRawParameterValue (prefix + "Pos")->load() * 100.0f
                  + s.getRawParameterValue (prefix + "WarpMode")->load() * 10.0f
                  + s.getRawParameterValue (prefix + "Warp")->load()
                  + (float) proc.uiChangeCounter.load() * 7.0f;
    if (h != lastHash) { lastHash = h; repaint(); }
}

bool WaveDisplay::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac"))
            return true;
    return false;
}

void WaveDisplay::filesDropped (const juce::StringArray& files, int, int)
{
    hover = false;
    juce::String err;
    if (! proc.importWavetable (prefix == "a" ? 0 : 1, juce::File (files[0]), err))
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import wavetable", err);
    repaint();
}

void WaveDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);

    auto& s = proc.apvts;
    const int table = (int) s.getRawParameterValue (prefix + "Table")->load();
    const float pos = s.getRawParameterValue (prefix + "Pos")->load();
    const int mode  = (int) s.getRawParameterValue (prefix + "WarpMode")->load();
    const float warp = s.getRawParameterValue (prefix + "Warp")->load();

    const Wavetable* wtp = nullptr;
    juce::String userName;
    if (table >= WavetableBank::kNumBuiltin)
    {
        const int slot = juce::jlimit (0, kNumUserTables - 1, table - WavetableBank::kNumBuiltin);
        wtp = proc.getUserTable (slot);
        userName = proc.userTableName[slot];
    }
    else wtp = &WavetableBank::instance().table (table);

    if (wtp == nullptr)
    {
        g.setColour (Col::dim);
        g.setFont (font (11.0f));
        g.drawFittedText ("Glisse un fichier WAV ici\n(ou bouton WAV)", getLocalBounds().reduced (6), juce::Justification::centred, 3);
    }
    else
    {
        const auto& wt = *wtp;
        const int N = 160;
        const float depthX = r.getWidth() * 0.18f, depthY = r.getHeight() * 0.28f;
        const float w = r.getWidth() - depthX - 8.0f, h = r.getHeight() - depthY - 10.0f;

        for (int f = kFrames - 1; f >= 0; f -= 2)
        {
            const float t = (float) f / (float) (kFrames - 1);
            const float ox = r.getX() + 4.0f + depthX * (1.0f - t), oy = r.getY() + 5.0f + depthY * (1.0f - t);
            juce::Path p;
            for (int i = 0; i <= N; ++i)
            {
                const float ph = (float) i / (float) N;
                const float y = WavetableBank::read (wt, std::min (ph, 0.9999f), t, 3);
                const float px = ox + w * ph, py = oy + h * 0.5f - y * h * 0.45f;
                if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            g.setColour (Col::violet.withAlpha (0.12f + 0.1f * (1.0f - std::abs (t - pos))));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }

        const float ox = r.getX() + 4.0f + depthX * (1.0f - pos), oy = r.getY() + 5.0f + depthY * (1.0f - pos);
        juce::Path p;
        for (int i = 0; i <= N * 2; ++i)
        {
            const float ph = std::min ((float) i / (float) (N * 2), 0.9999f);
            const float mod = std::sin (ph * juce::MathConstants<float>::twoPi);
            float y = WavetableBank::read (wt, warpPhase (mode, ph, warp, mod), pos, 2);
            if (mode == (int) Warp::Ring) y *= 1.0f - warp + warp * mod;
            const float px = ox + w * ph, py = oy + h * 0.5f - y * h * 0.45f;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (Col::acid.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (4.0f));
        g.setColour (Col::acid);
        g.strokePath (p, juce::PathStrokeType (1.6f));

        g.setColour (Col::dim);
        g.setFont (font (10.0f));
        g.drawText (juce::String ((int) std::round (pos * 100.0f)) + "%", r.reduced (4.0f), juce::Justification::topRight, false);
        if (userName.isNotEmpty())
        {
            g.setColour (Col::hot);
            g.drawText (userName, r.reduced (4.0f), juce::Justification::bottomLeft, true);
        }
    }
    if (hover)
    {
        g.setColour (Col::hot);
        g.drawRoundedRectangle (r.reduced (1.0f), 4.0f, 2.0f);
    }
}

// ============================================================================ Scope
ScopeDisplay::ScopeDisplay (FragorProcessor& p) : proc (p) { startTimerHz (30); }

void ScopeDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (Col::border);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    constexpr int S = FragorProcessor::kScopeSize;
    float buf[S];
    const int w = proc.scopeWrite.load();
    for (int i = 0; i < S; ++i) buf[i] = proc.scope[(w + i) % S];

    const int len = 900;
    int start = S - len;
    for (int i = S - len - 1; i > 1; --i)
        if (buf[i - 1] <= 0.0f && buf[i] > 0.0f) { start = i; break; }

    juce::Path p;
    float peak = 0.0f;
    for (int i = 0; i < len; ++i)
    {
        const float v = juce::jlimit (-1.2f, 1.2f, buf[start + i]);
        peak = std::max (peak, std::abs (v));
        const float x = r.getX() + r.getWidth() * (float) i / (float) (len - 1);
        const float y = r.getCentreY() - v * r.getHeight() * 0.42f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    const auto c = peak > 0.99f ? Col::hot : Col::acid;
    g.setColour (c.withAlpha (0.2f));
    g.strokePath (p, juce::PathStrokeType (3.5f));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.2f));
}

// ============================================================================ SeqGrid
SeqGrid::SeqGrid (FragorProcessor& p) : proc (p) { startTimerHz (30); }

void SeqGrid::paint (juce::Graphics& g)
{
    auto& s = proc.apvts;
    const auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);

    const bool on = s.getRawParameterValue ("seqOn")->load() > 0.5f;
    const int steps = (int) s.getRawParameterValue ("seqSteps")->load();
    const int cur = proc.seqStepView.load();
    const auto names = seqStepNames();
    const int maxIdx = names.size() - 1;
    const float sw = r.getWidth() / (float) kNumSeqSteps;
    const float barH = r.getHeight() - 18.0f;

    for (int i = 0; i < kNumSeqSteps; ++i)
    {
        const int v = (int) s.getRawParameterValue ("seq" + juce::String (i + 1))->load();
        const bool active = i < steps;
        const auto cell = juce::Rectangle<float> (r.getX() + i * sw + 2.0f, r.getY() + 3.0f, sw - 4.0f, barH);
        g.setColour (Col::panel2.withAlpha (active ? 1.0f : 0.4f));
        g.fillRoundedRectangle (cell, 2.0f);

        juce::Colour c = v == 1 ? Col::violet : Col::acid;
        if (! on || ! active) c = c.withSaturation (0.1f).withAlpha (0.35f);
        float frac = v == 0 ? 0.03f : (v == 1 ? 1.0f : (float) (v - 1) / (float) (maxIdx - 1));
        g.setColour (c);
        g.fillRoundedRectangle (cell.withTrimmedTop (cell.getHeight() * (1.0f - frac)), 2.0f);

        if (on && i == cur)
        {
            g.setColour (Col::hot);
            g.drawRoundedRectangle (cell.expanded (1.0f), 2.0f, 2.0f);
        }
        g.setColour (active ? Col::text : Col::dim);
        g.setFont (font (9.5f));
        g.drawText (names[v], juce::Rectangle<float> (r.getX() + i * sw, r.getBottom() - 15.0f, sw, 14.0f),
                    juce::Justification::centred, false);
    }
}

void SeqGrid::setFromMouse (const juce::MouseEvent& e)
{
    const auto r = getLocalBounds().toFloat();
    const float sw = r.getWidth() / (float) kNumSeqSteps;
    const int step = juce::jlimit (0, kNumSeqSteps - 1, (int) (e.position.x / sw));
    const float barH = r.getHeight() - 18.0f;
    const int maxIdx = seqStepNames().size() - 1;
    const float frac = juce::jlimit (0.0f, 1.0f, 1.0f - (e.position.y - 3.0f) / barH);
    int idx = frac < 0.04f ? 0 : 2 + (int) std::round (frac * (float) (maxIdx - 2));
    setParam (proc.apvts, "seq" + juce::String (step + 1), (float) juce::jlimit (0, maxIdx, idx));
}

void SeqGrid::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        const float sw = (float) getWidth() / (float) kNumSeqSteps;
        const int step = juce::jlimit (0, kNumSeqSteps - 1, (int) (e.position.x / sw));
        juce::PopupMenu m;
        const auto names = seqStepNames();
        for (int i = 0; i < names.size(); ++i) m.addItem (i + 1, names[i]);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, step] (int res) { if (res > 0) setParam (proc.apvts, "seq" + juce::String (step + 1), (float) (res - 1)); });
        return;
    }
    setFromMouse (e);
}

void SeqGrid::mouseDrag (const juce::MouseEvent& e) { if (! e.mods.isPopupMenu()) setFromMouse (e); }

// ============================================================================ LfoDisplay
LfoDisplay::LfoDisplay (FragorProcessor& p, int i) : proc (p), index (i)
{
    startTimerHz (30);
    setTooltip (juce::String::fromUTF8 ("Double-clic : éditeur de courbe"));
}

void LfoDisplay::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (Col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    const juce::String pre = "l" + juce::String (index + 1);
    auto& s = proc.apvts;
    const int shape = (int) s.getRawParameterValue (pre + "Shape")->load();
    const float curve = s.getRawParameterValue (pre + "Curve")->load();
    const float phase = s.getRawParameterValue (pre + "Phase")->load();
    const float* table = proc.lfoTableForUi (index);

    const auto in = r.reduced (6.0f, 8.0f);
    juce::Path p;
    juce::Random rnd (1234);
    float sh[8], dr[9];
    for (auto& v : sh) v = rnd.nextFloat();
    for (auto& v : dr) v = rnd.nextFloat();
    const int N = 300;
    for (int i = 0; i <= N; ++i)
    {
        const double x = (double) i / N * 2.0;
        const double ph = x - std::floor (x);
        const double ph2 = ph + phase - std::floor (ph + phase);
        const int cyc = (int) (x + phase) % 8;
        const float y = Lfo::applyCurve (Lfo::shapeValue (shape, std::min (ph2, 0.99999), sh[cyc], dr[cyc], dr[cyc + 1], table), curve);
        const float px = in.getX() + in.getWidth() * (float) i / N;
        const float py = in.getBottom() - y * in.getHeight();
        if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
    }
    g.setColour (Col::violet.withAlpha (0.25f));
    g.strokePath (p, juce::PathStrokeType (4.0f));
    g.setColour (Col::violet.brighter (0.3f));
    g.strokePath (p, juce::PathStrokeType (1.5f));

    if (proc.activeVoices.load() > 0)
    {
        const float v = proc.lfoView[index].load();
        const float y = in.getBottom() - v * in.getHeight();
        g.setColour (Col::hot);
        g.drawHorizontalLine ((int) y, r.getX() + 2.0f, r.getRight() - 2.0f);
        g.fillEllipse (r.getRight() - 12.0f, y - 4.0f, 8.0f, 8.0f);
    }
}

// ============================================================================ ModSlot
ModSlot::ModSlot (APVTS& s, int sl) : slot (sl)
{
    const juce::String p = "m" + juce::String (slot);
    src.addItemList (modSourceNames(), 1);
    dst.addItemList (modDestNames(), 1);
    amt.setSliderStyle (juce::Slider::LinearHorizontal);
    amt.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amt.setPopupDisplayEnabled (true, true, nullptr, 1200);
    amt.setColour (juce::Slider::rotarySliderFillColourId, Col::violet);
    amt.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (src);
    addAndMakeVisible (dst);
    addAndMakeVisible (amt);
    srcAtt = std::make_unique<APVTS::ComboBoxAttachment> (s, p + "Src", src);
    dstAtt = std::make_unique<APVTS::ComboBoxAttachment> (s, p + "Dst", dst);
    amtAtt = std::make_unique<APVTS::SliderAttachment> (s, p + "Amt", amt);
}

void ModSlot::resized()
{
    auto r = getLocalBounds().reduced (0, 2);
    r.removeFromLeft (20);
    const int w = r.getWidth();
    src.setBounds (r.removeFromLeft ((int) (w * 0.31f)).reduced (1, 0));
    r.removeFromLeft (14);
    dst.setBounds (r.removeFromLeft ((int) (w * 0.36f)).reduced (1, 0));
    amt.setBounds (r.reduced (4, 0));
}

void ModSlot::paint (juce::Graphics& g)
{
    g.setColour (Col::dim);
    g.setFont (font (10.0f));
    g.drawText (juce::String (slot), getLocalBounds().removeFromLeft (18), juce::Justification::centred, false);
    const auto r = getLocalBounds();
    const int x = 20 + (int) ((r.getWidth() - 20) * 0.31f) + 2;
    g.setColour (Col::violet);
    g.drawText (">", juce::Rectangle<int> (x, 0, 10, r.getHeight()), juce::Justification::centred, false);
}

// ============================================================================ FxChainStrip
FxChainStrip::FxChainStrip (FragorProcessor& p) : proc (p)
{
    startTimerHz (15);
    setTooltip (juce::String::fromUTF8 ("Glisser un module pour changer l'ordre de la chaîne. Clic = activer / couper."));
}

juce::Rectangle<float> FxChainStrip::chipBounds (int slot) const
{
    const float w = (float) getWidth() / (float) numFxModules;
    return { slot * w + 3.0f, 2.0f, w - 16.0f, (float) getHeight() - 4.0f };
}

int FxChainStrip::indexAt (float x) const
{
    const float w = (float) getWidth() / (float) numFxModules;
    return juce::jlimit (0, numFxModules - 1, (int) (x / w));
}

void FxChainStrip::paint (juce::Graphics& g)
{
    static const char* onIds[] = { "distOn", "dist2On", "crushOn", "ottOn", "chorusOn", "dlyOn", "revOn" };
    const auto order = proc.getFxOrder();
    const auto names = fxModuleNames();
    for (int s = 0; s < numFxModules; ++s)
    {
        auto r = chipBounds (s);
        const int m = order[(size_t) s];
        const bool on = proc.apvts.getRawParameterValue (onIds[m])->load() > 0.5f;
        if (s == dragSlot) r.setX (dragX - grabOffset);
        const auto c = m <= fxDist2 || m == fxCrush ? Col::hot : (m == fxOtt ? Col::acid : Col::violet);
        g.setColour (on ? c.withAlpha (0.22f) : Col::panel2);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? c : Col::border);
        g.drawRoundedRectangle (r, 4.0f, s == dragSlot ? 2.0f : 1.0f);
        g.setColour (on ? Col::text : Col::dim);
        g.setFont (font (11.5f));
        g.drawText (juce::String (s + 1) + ". " + names[m], r, juce::Justification::centred, false);
        if (s < numFxModules - 1)
        {
            g.setColour (Col::dim);
            g.drawText (">", juce::Rectangle<float> (chipBounds (s).getRight(), 0.0f, 13.0f, (float) getHeight()), juce::Justification::centred, false);
        }
    }
}

void FxChainStrip::mouseDown (const juce::MouseEvent& e)
{
    dragSlot = indexAt (e.position.x);
    grabOffset = e.position.x - chipBounds (dragSlot).getX();
    dragX = e.position.x;
}

void FxChainStrip::mouseDrag (const juce::MouseEvent& e) { dragX = e.position.x; repaint(); }

void FxChainStrip::mouseUp (const juce::MouseEvent& e)
{
    static const char* onIds[] = { "distOn", "dist2On", "crushOn", "ottOn", "chorusOn", "dlyOn", "revOn" };
    if (dragSlot < 0) return;
    auto order = proc.getFxOrder();
    if (e.getDistanceFromDragStart() < 4)
    {
        const int m = order[(size_t) dragSlot];
        if (auto* p = proc.apvts.getParameter (onIds[m]))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
            p->endChangeGesture();
        }
    }
    else
    {
        const int target = indexAt (e.position.x);
        const int m = order[(size_t) dragSlot];
        std::vector<int> v (order.begin(), order.end());
        v.erase (v.begin() + dragSlot);
        v.insert (v.begin() + target, m);
        for (int k = 0; k < numFxModules; ++k) order[(size_t) k] = v[(size_t) k];
        proc.setFxOrder (order);
    }
    dragSlot = -1;
    repaint();
}

// ============================================================================ layout
void layoutGrid (const std::vector<juce::Component*>& items, juce::Rectangle<int> area, int cols, int cellW, int cellH)
{
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int c = (int) i % cols, row = (int) i / cols;
        if (items[i] != nullptr)
            items[i]->setBounds (area.getX() + c * cellW, area.getY() + row * cellH, cellW, cellH);
    }
}
}
