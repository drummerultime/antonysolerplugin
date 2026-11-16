#include "GuiCommon.h"

namespace alea::gui
{
juce::Colour col::track (int t)
{
    static const juce::uint32 c[8] { 0xffff6b35, 0xfff7c548, 0xffe85d9e, 0xff5ec8f2,
                                              0xff4f8cf7, 0xff9b7bf2, 0xff58d68d, 0xffc6e05a };
    return juce::Colour (c[juce::jlimit (0, 7, t)]);
}

juce::Font font (float size, bool bold)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

static juce::PopupMenu::Options menuOptions (juce::Component* c)
{
    return juce::PopupMenu::Options().withTargetComponent (c);
}

// ===========================================================================
// RndKnob
void RndKnob::bind (juce::RangedAudioParameter* v, juce::RangedAudioParameter* j,
                    std::atomic<bool>* l, std::atomic<float>* lv, const juce::String& lab)
{
    att.reset(); jatt.reset();
    param = v; jparam = j; lock = l; live = lv; label = lab; liveShown = -1.0f;
    jitter = 0.0f;
    if (param != nullptr)
    {
        att = std::make_unique<juce::ParameterAttachment> (*param, [this] (float x) { value = param->convertTo0to1 (x); repaint(); });
        att->sendInitialUpdate();
    }
    if (jparam != nullptr)
    {
        jatt = std::make_unique<juce::ParameterAttachment> (*jparam, [this] (float x) { jitter = jparam->convertTo0to1 (x); repaint(); });
        jatt->sendInitialUpdate();
        setTooltip (label + " | glisser : valeur | clic droit-glisser ou Cmd-glisser : alea | clic droit : menu (MIDI learn) | double-clic : defaut");
    }
    else if (param != nullptr)
        setTooltip (param->getName (64) + " | clic droit : MIDI learn");
    repaint();
}

int RndKnob::paramIdx() const
{
    if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (param)) return wp->getParameterIndex();
    return -1;
}

void RndKnob::refreshLive()
{
    if (proc != nullptr && param != nullptr)
    {
        const bool learning = proc->learningParam() == paramIdx() && paramIdx() >= 0;
        if (learning || learnBlink) { learnBlink = learning; repaint(); }
    }
    if (live == nullptr || jitter <= 0.0f) return;
    const float v = live->load (std::memory_order_relaxed);
    if (std::abs (v - liveShown) > 0.002f) { liveShown = v; repaint(); }
}

juce::Rectangle<float> RndKnob::lockArea() const
{
    return { (float) getWidth() - 13.0f, 1.0f, 12.0f, 12.0f };
}

void RndKnob::paint (juce::Graphics& g)
{
    if (param == nullptr) return;

    auto b = getLocalBounds().toFloat();
    const float labelH = 14.0f;
    auto knobArea = b.withTrimmedBottom (labelH).reduced (3.0f);
    const float size = juce::jmin (knobArea.getWidth(), knobArea.getHeight());
    const auto c = knobArea.getCentre();
    const float rad = size * 0.5f - 5.0f;
    constexpr float a0 = -2.4f, a1 = 2.4f;
    auto angle = [] (float n) { return a0 + juce::jlimit (0.0f, 1.0f, n) * (a1 - a0); };
    const juce::PathStrokeType stroke (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    const int16_t lk = plock != nullptr ? plock->load() : (int16_t) -1;
    const bool locked = lk >= 0;
    const float shown = locked ? (float) lk * 0.001f : value;

    juce::Path bgArc;
    bgArc.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
    g.setColour (col::line);
    g.strokePath (bgArc, stroke);

    // anneau d'alea
    if (jparam != nullptr)
    {
        if (editRandomMode)
        {
            juce::Path hint;
            hint.addCentredArc (c.x, c.y, rad + 4.5f, rad + 4.5f, 0.0f, a0, a1, true);
            g.setColour (col::rnd.withAlpha (0.18f));
            g.strokePath (hint, juce::PathStrokeType (2.0f));
        }
        if (jitter > 0.001f)
        {
            const float ch = chaos != nullptr ? chaos->load() * 2.0f : 1.0f;
            const float half = jitter * 0.5f * ch;
            const float lo = juce::jlimit (0.0f, 1.0f, shown - half), hi = juce::jlimit (0.0f, 1.0f, shown + half);
            juce::Path jr;
            jr.addCentredArc (c.x, c.y, rad + 4.5f, rad + 4.5f, 0.0f, angle (lo), angle (juce::jmax (hi, lo + 0.005f)), true);
            g.setColour (col::rnd.withAlpha (ch > 0.0f ? 0.9f : 0.3f));
            g.strokePath (jr, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

            if (liveShown >= 0.0f)
            {
                const float a = angle (liveShown);
                const float rr = rad + 4.5f;
                g.setColour (juce::Colours::white);
                g.fillEllipse (c.x + rr * std::sin (a) - 2.5f, c.y - rr * std::cos (a) - 2.5f, 5.0f, 5.0f);
            }
        }
    }

    // arc de valeur (depuis le centre si bipolaire)
    {
        const float from = bipolar ? 0.0f : a0;
        const float to = angle (shown);
        juce::Path va;
        va.addCentredArc (c.x, c.y, rad, rad, 0.0f, juce::jmin (from, to), juce::jmax (juce::jmax (from, to), juce::jmin (from, to) + 0.01f), true);
        g.setColour (locked ? col::plock : accentCol);
        g.strokePath (va, stroke);
    }

    // corps + index
    const float br = rad - 5.0f;
    g.setColour (col::panelHi.brighter (hover ? 0.15f : 0.0f));
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    if (plock != nullptr)
    {
        g.setColour (col::plock.withAlpha (locked ? 0.9f : 0.35f));
        g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.2f);
    }
    const float a = angle (shown);
    g.setColour (col::text);
    g.drawLine (c.x + br * 0.35f * std::sin (a), c.y - br * 0.35f * std::cos (a),
                c.x + br * 0.95f * std::sin (a), c.y - br * 0.95f * std::cos (a), 2.0f);

    // cadenas
    if (lock != nullptr)
    {
        const bool lockOn = lock->load();
        if (lockOn || hover)
        {
            auto la = lockArea().reduced (1.5f);
            g.setColour (lockOn ? col::lock : col::textDim.withAlpha (0.7f));
            g.fillRoundedRectangle (la.withTrimmedTop (la.getHeight() * 0.45f), 1.5f);
            juce::Path shackle;
            shackle.addCentredArc (la.getCentreX(), la.getY() + la.getHeight() * 0.45f, la.getWidth() * 0.28f, la.getHeight() * 0.35f,
                                   0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (shackle, juce::PathStrokeType (1.5f));
        }
    }

    // MIDI : point si mappe, cadre clignotant en apprentissage
    if (proc != nullptr)
    {
        const int idx = paramIdx();
        if (proc->learningParam() == idx && idx >= 0)
        {
            g.setColour (col::rnd.withAlpha ((juce::Time::getMillisecondCounter() / 250) % 2 ? 1.0f : 0.3f));
            g.drawRoundedRectangle (b.reduced (1.0f), 4.0f, 1.5f);
        }
        else if (proc->ccForParam (idx) >= 0)
        {
            g.setColour (col::rnd);
            g.fillEllipse (2.0f, 2.0f, 5.0f, 5.0f);
        }
    }

    // texte
    juce::String txt = label;
    juce::Colour tc = col::textDim;
    if (dragging || hover)
    {
        if (dragging && dragJitter) { txt = "rnd " + juce::String ((int) std::round (jitter * 100.0f)) + "%"; tc = col::rnd; }
        else if (locked && plock != nullptr) { txt = (juce::String) "L " + param->getText (shown, 32); tc = col::plock; }
        else { txt = param->getCurrentValueAsText(); tc = col::text; }
    }
    else if (locked) tc = col::plock;
    g.setColour (tc);
    g.setFont (font (11.0f));
    g.drawFittedText (txt, b.removeFromBottom (labelH).toNearestInt(), juce::Justification::centred, 1);
}

void RndKnob::mouseDown (const juce::MouseEvent& e)
{
    ignoreDrag = false; moved = false;
    rightDown = e.mods.isPopupMenu();
    if (param == nullptr) { ignoreDrag = true; return; }
    if (lock != nullptr && lockArea().contains (e.position))
    {
        lock->store (! lock->load());
        ignoreDrag = true;
        repaint();
        return;
    }
    if (proc != nullptr) proc->pushUndo();
    dragPlock = plock != nullptr && ! (jparam != nullptr && (editRandomMode || e.mods.isPopupMenu() || e.mods.isCommandDown()));
    dragJitter = ! dragPlock && jparam != nullptr && (editRandomMode || e.mods.isPopupMenu() || e.mods.isCommandDown());
    dragging = true;
    if (dragPlock)       dragStart = plock->load() >= 0 ? (float) plock->load() * 0.001f : value;
    else if (dragJitter) { dragStart = jitter; jatt->beginGesture(); }
    else                 { dragStart = value;  att->beginGesture(); }
    repaint();
}

void RndKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (ignoreDrag || ! dragging) return;
    if (e.getDistanceFromDragStart() > 2) moved = true;
    if (! moved) return;
    const float sens = e.mods.isShiftDown() ? 700.0f : 160.0f;
    const float v = juce::jlimit (0.0f, 1.0f, dragStart - (float) e.getDistanceFromDragStartY() / sens);
    if (dragPlock)
    {
        plock->store ((int16_t) std::round (v * 1000.0f));
        if (onPlockEdited) onPlockEdited();
        repaint();
    }
    else if (dragJitter) jatt->setValueAsPartOfGesture (jparam->convertFrom0to1 (v));
    else                 att->setValueAsPartOfGesture (param->convertFrom0to1 (v));
}

void RndKnob::mouseUp (const juce::MouseEvent&)
{
    if (ignoreDrag || ! dragging) return;
    if (! dragPlock) { if (dragJitter) jatt->endGesture(); else att->endGesture(); }
    dragging = false;
    if (rightDown && ! moved) showMenu();
    repaint();
}

void RndKnob::showMenu()
{
    if (proc == nullptr) return;
    const int idx = paramIdx();
    const int cc = proc->ccForParam (idx);
    juce::PopupMenu m;
    m.addSectionHeader (param->getName (40));
    m.addItem (1, "MIDI Learn (bouger un controleur)");
    m.addItem (2, cc >= 0 ? "Oublier le MIDI (CC " + juce::String (cc) + ")" : "Oublier le MIDI", cc >= 0);
    if (jparam != nullptr) m.addItem (3, "Alea a zero");
    if (lock != nullptr) m.addItem (4, lock->load() ? "Deverrouiller (de)" : "Verrouiller (de)");
    if (plock != nullptr) m.addItem (5, "Effacer le p-lock", plock->load() >= 0);
    juce::Component::SafePointer<RndKnob> safe (this);
    m.showMenuAsync (menuOptions (this), [safe, idx] (int r)
    {
        if (safe == nullptr) return;
        auto& k = *safe;
        switch (r)
        {
            case 1: k.proc->startLearn (idx); break;
            case 2: k.proc->clearMidiMapping (idx); break;
            case 3: k.proc->pushUndo(); k.jatt->setValueAsCompleteGesture (0.0f); break;
            case 4: k.lock->store (! k.lock->load()); break;
            case 5: k.plock->store (-1); if (k.onPlockEdited) k.onPlockEdited(); break;
            default: break;
        }
        k.repaint();
    });
}

void RndKnob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (param == nullptr || (lock != nullptr && lockArea().contains (e.position))) return;
    if (plock != nullptr && ! e.mods.isCommandDown() && ! editRandomMode)
    {
        plock->store (-1);
        if (onPlockEdited) onPlockEdited();
        repaint();
        return;
    }
    const bool j = jparam != nullptr && (editRandomMode || e.mods.isCommandDown());
    if (j) jatt->setValueAsCompleteGesture (0.0f);
    else   att->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

// ===========================================================================
void Scope::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (b, 5.0f);
    g.setColour (col::line);
    g.drawHorizontalLine ((int) b.getCentreY(), b.getX(), b.getRight());

    juce::Path p;
    float pk = 0.0f;
    for (int i = 0; i < len; ++i) pk = juce::jmax (pk, std::abs (buf[i]));
    const float gain = pk > 1.0e-4f ? 0.45f / pk : 1.0f;
    for (int i = 0; i < len; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / (float) (len - 1);
        const float y = b.getCentreY() - buf[i] * gain * b.getHeight();
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (colour());
    g.strokePath (p, juce::PathStrokeType (1.4f));
    g.setColour (col::textDim);
    g.setFont (font (10.0f));
    g.drawText ("dernier coup - 250 ms - crete " + juce::String (juce::Decibels::gainToDecibels (pk), 1) + " dB",
                b.reduced (8.0f, 4.0f), juce::Justification::topLeft);
}

// ===========================================================================
void ModDisplay::push()
{
    hist[(size_t) pos] = value.load();
    pos = (pos + 1) % (int) hist.size();
    repaint();
}

void ModDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::bg);
    g.fillRoundedRectangle (b, 4.0f);
    juce::Path p;
    const int n = (int) hist.size();
    for (int i = 0; i < n; ++i)
    {
        const float v = hist[(size_t) ((pos + i) % n)];
        const float x = b.getX() + b.getWidth() * (float) i / (float) (n - 1);
        const float y = b.getBottom() - 3.0f - v * (b.getHeight() - 6.0f);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col::rnd);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

// ===========================================================================
void DragMidiButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::panelHi);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (col::rnd);
    g.drawRoundedRectangle (b, 4.0f, 1.0f);
    g.setFont (font (11.5f, true));
    g.drawText ("Drag MIDI", b, juce::Justification::centred);
}

void DragMidiButton::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6) return;
    dragged = true;
    const auto f = exporter();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void DragMidiButton::mouseUp (const juce::MouseEvent&)
{
    if (dragged) return;
    const auto f = exporter();
    if (f.existsAsFile()) f.revealToUser();
}

} // namespace alea::gui
