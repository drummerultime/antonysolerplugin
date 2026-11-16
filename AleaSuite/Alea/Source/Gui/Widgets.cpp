#include "Widgets.h"

namespace alea::gui
{
static juce::PopupMenu::Options menuOptions (juce::Component* c)
{
    return juce::PopupMenu::Options().withTargetComponent (c);
}

// ===========================================================================
// TrackList
static void drawDie (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    r = r.withSizeKeepingCentre (14.0f, 14.0f);
    g.setColour (c);
    g.drawRoundedRectangle (r, 3.0f, 1.3f);
    const float d = 2.4f;
    for (auto p : { juce::Point<float> (0.28f, 0.28f), juce::Point<float> (0.72f, 0.72f), juce::Point<float> (0.5f, 0.5f),
                    juce::Point<float> (0.72f, 0.28f), juce::Point<float> (0.28f, 0.72f) })
        g.fillEllipse (r.getX() + p.x * r.getWidth() - d * 0.5f, r.getY() + p.y * r.getHeight() - d * 0.5f, d, d);
}

void TrackList::paint (juce::Graphics& g)
{
    const float rh = rowHeight();
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto r = juce::Rectangle<float> (0.0f, t * rh, (float) getWidth(), rh).reduced (0.0f, 1.5f);
        g.setColour (t == selected ? col::panelHi : col::panel);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (col::track (t));
        g.fillRoundedRectangle (r.removeFromLeft (4.0f), 2.0f);

        const bool muted = proc.apvts.getRawParameterValue (tid (t, "mute"))->load() > 0.5f;
        const bool sep = proc.apvts.getRawParameterValue (tid (t, "out"))->load() > 0.5f;
        auto dice = r.removeFromRight (24.0f);
        auto mute = r.removeFromRight (24.0f);
        auto play = r.removeFromRight (20.0f);

        drawDie (g, dice, col::rnd);

        auto mr = mute.withSizeKeepingCentre (18.0f, 16.0f);
        g.setColour (muted ? col::lock : col::line);
        g.fillRoundedRectangle (mr, 3.0f);
        g.setColour (muted ? juce::Colours::white : col::textDim);
        g.setFont (font (10.0f, true));
        g.drawText ("M", mr, juce::Justification::centred);

        juce::Path tri;
        auto pr = play.withSizeKeepingCentre (8.0f, 10.0f);
        tri.addTriangle (pr.getX(), pr.getY(), pr.getX(), pr.getBottom(), pr.getRight(), pr.getCentreY());
        g.setColour (col::textDim);
        g.fillPath (tri);

        const float fl = proc.flash[t].load();
        auto led = r.removeFromLeft (16.0f).withSizeKeepingCentre (7.0f, 7.0f);
        g.setColour (col::track (t).withAlpha (0.15f + 0.85f * fl));
        g.fillEllipse (led);

        g.setColour (muted ? col::textDim : col::text);
        g.setFont (font (12.0f, true));
        g.drawText (juce::String (t + 1) + " " + defaultKit (t).name, r.removeFromTop (r.getHeight() * 0.55f), juce::Justification::bottomLeft);
        g.setColour (col::textDim);
        g.setFont (font (9.5f));
        auto* ep = proc.apvts.getParameter (tid (t, "engine"));
        juce::String sub = ep->getCurrentValueAsText();
        if ((int) proc.apvts.getRawParameterValue (tid (t, "engine"))->load() == EngSample)
        {
            const auto sn = proc.sampleName (t);
            sub = sn.isEmpty() ? juce::String ("Sample (vide)") : sn;
        }
        if (sep) sub << "  > out " << (t + 2);
        g.drawText (sub, r, juce::Justification::topLeft);
    }
}

void TrackList::mouseDown (const juce::MouseEvent& e)
{
    const int t = juce::jlimit (0, kNumTracks - 1, (int) (e.position.y / rowHeight()));
    const float x = e.position.x, w = (float) getWidth();
    if (x > w - 24.0f)              { proc.pushUndo(); proc.diceVoice (t, e.mods.isShiftDown()); }
    else if (x > w - 48.0f)
    {
        auto* p = proc.apvts.getParameter (tid (t, "mute"));
        proc.setParamNorm (tid (t, "mute"), p->getValue() > 0.5f ? 0.0f : 1.0f);
    }
    else if (x > w - 68.0f)         { proc.guiTrig[t] = true; }
    if (onSelect) onSelect (t);
    repaint();
}

// ===========================================================================
// StepGrid
bool StepGrid::cellAt (juce::Point<float> pos, int& t, int& i) const
{
    const float cw = (float) getWidth() / (float) kMaxSteps, ch = (float) getHeight() / (float) kNumTracks;
    t = (int) (pos.y / ch); i = (int) (pos.x / cw);
    return t >= 0 && t < kNumTracks && i >= 0 && i < kMaxSteps;
}

void StepGrid::paint (juce::Graphics& g)
{
    const int pat = proc.editPattern();
    const bool showPlay = proc.currentPattern.load() == pat;
    const float cw = (float) getWidth() / (float) kMaxSteps, ch = (float) getHeight() / (float) kNumTracks;

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = proc.bank.pats[pat].tracks[t];
        const int len = tp.length;
        const int ps = showPlay ? proc.playStep[t].load() : -1;
        const auto tc = col::track (t);
        const uint32_t evo = proc.evoMask[pat][t].load();

        if (t == selected)
        {
            g.setColour (col::panelHi.withAlpha (0.6f));
            g.fillRect (0.0f, t * ch, (float) getWidth(), ch);
        }
        for (int i = 0; i < kMaxSteps; ++i)
        {
            auto r = juce::Rectangle<float> (i * cw, t * ch, cw, ch).reduced (1.5f, 2.0f);
            if (i >= len)
            {
                g.setColour (col::bg.brighter (0.02f));
                g.fillRoundedRectangle (r, 2.5f);
                continue;
            }
            const auto& s = tp.steps[i];
            g.setColour (((i / 4) % 2 == 0) ? col::panelHi : col::panel);
            g.fillRoundedRectangle (r, 2.5f);
            const bool flipped = ((evo >> i) & 1u) != 0;

            if (flipped)
            {
                // pas modifie par l'evolution : contour pointille (ajoute) ou barre (retire)
                g.setColour (tc.withAlpha (0.9f));
                if (! s.on)
                {
                    juce::Path dash, out;
                    dash.addRoundedRectangle (r.reduced (1.5f), 2.5f);
                    const float lens[] { 3.0f, 2.5f };
                    juce::PathStrokeType (1.4f).createDashedStroke (out, dash, lens, 2);
                    g.fillPath (out);
                }
            }

            if (s.on)
            {
                const float alpha = 0.3f + 0.7f * (float) s.prob / 100.0f;
                g.setColour ((s.acc ? tc.brighter (0.35f) : tc.darker (0.15f)).withAlpha (alpha));
                g.fillRoundedRectangle (r, 2.5f);
                if (s.acc)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.fillRect (r.withHeight (3.0f).reduced (2.0f, 0.0f));
                }
                if (s.rat > 1)
                {
                    g.setColour (col::bg.withAlpha (0.8f));
                    for (int k = 0; k < s.rat; ++k)
                        g.fillEllipse (r.getX() + 3.0f + k * 4.5f, r.getBottom() - 6.0f, 3.0f, 3.0f);
                }
                if (s.tune != 0)
                {
                    g.setColour (col::bg);
                    g.setFont (font (9.0f, true));
                    g.drawText (juce::String ((int) s.tune), r.withTrimmedBottom (r.getHeight() * 0.3f), juce::Justification::centred);
                }
            }
            if (s.on && flipped)
            {
                g.setColour (col::bg.withAlpha (0.85f));
                g.drawLine (r.getX() + 3.0f, r.getBottom() - 3.0f, r.getRight() - 3.0f, r.getY() + 3.0f, 2.0f);
            }
            if (s.slide)
            {
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawLine (r.getRight() - 9.0f, r.getBottom() - 3.0f, r.getRight() - 2.0f, r.getBottom() - 9.0f, 1.6f);
            }
            if (showPreview)
            {
                const bool pl = (preview.play[t] >> i) & 1u, gh = (preview.ghost[t] >> i) & 1u, sk = (preview.skip[t] >> i) & 1u;
                if (gh) { g.setColour (col::rnd); g.fillEllipse (r.getCentreX() - 2.5f, r.getY() + 3.0f, 5.0f, 5.0f); }
                else if (sk) { g.setColour (col::lock.withAlpha (0.9f)); g.fillRect (r.getCentreX() - 3.0f, r.getY() + 4.0f, 6.0f, 1.6f); }
                else if (pl && ! s.on) { g.setColour (juce::Colours::white.withAlpha (0.7f)); g.fillEllipse (r.getCentreX() - 2.0f, r.getY() + 3.0f, 4.0f, 4.0f); }
            }
            if (s.cond != 0)
            {
                juce::Path tri;
                tri.addTriangle (r.getX(), r.getBottom(), r.getX() + 7.0f, r.getBottom(), r.getX(), r.getBottom() - 7.0f);
                g.setColour (col::rnd);
                g.fillPath (tri);
            }
            if (s.hasLocks())
            {
                g.setColour (col::plock);
                g.fillEllipse (r.getRight() - 6.0f, r.getY() + 2.0f, 4.0f, 4.0f);
            }
            if (i == ps)
            {
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.drawRoundedRectangle (r, 2.5f, 1.6f);
            }
            if (t == lockTrack && i == lockStep)
            {
                g.setColour (col::plock);
                g.drawRoundedRectangle (r.expanded (1.0f), 3.0f, 2.2f);
            }
        }
    }
    g.setColour (col::textDim.withAlpha (0.35f));
    for (int i = 16; i < kMaxSteps; i += 16)
        g.drawVerticalLine ((int) (i * cw), 0.0f, (float) getHeight());
}

void StepGrid::stepMenu (int t, int i)
{
    juce::PopupMenu m, cond, prob, rat;
    const int pat = proc.editPattern();
    auto& s = proc.bank.pats[pat].tracks[t].steps[i];
    m.addSectionHeader ("Piste " + juce::String (t + 1) + " - pas " + juce::String (i + 1));
    m.addItem (1, "Pas actif", true, s.on != 0);
    m.addItem (2, "Accent", true, s.acc != 0);
    m.addItem (5, "Slide (glissement depuis le coup precedent)", true, s.slide != 0);
    m.addItem (3, "Editer les p-locks (potards de la voix)");
    m.addItem (4, "Effacer les p-locks", s.hasLocks());
    for (int c = 0; c < kNumConds; ++c) cond.addItem (100 + c, condNames()[c], true, s.cond == c);
    m.addSubMenu ("Condition", cond);
    for (int p : { 100, 87, 75, 50, 33, 25, 12 }) prob.addItem (200 + p, juce::String (p) + " %", true, s.prob == p);
    m.addSubMenu ("Probabilite", prob);
    for (int r = 1; r <= 4; ++r) rat.addItem (400 + r, juce::String (r) + (r == 1 ? " coup" : " coups"), true, s.rat == r);
    m.addSubMenu ("Ratchet", rat);

    juce::Component::SafePointer<StepGrid> safe (this);
    m.showMenuAsync (menuOptions (this), [safe, t, i, pat] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& g = *safe;
        auto& st = g.proc.bank.pats[pat].tracks[t].steps[i];
        g.proc.pushUndo();
        if (r == 1) st.on = (int8_t) (st.on ? 0 : 1);
        else if (r == 2) { st.acc = (int8_t) (st.acc ? 0 : 1); if (st.acc) st.on = 1; }
        else if (r == 3) { if (g.onLockStep) g.onLockStep (t, i); }
        else if (r == 4) st.clearLocks();
        else if (r == 5) st.slide = (int8_t) (st.slide ? 0 : 1);
        else if (r >= 100 && r < 100 + kNumConds) st.cond = (int8_t) (r - 100);
        else if (r >= 200 && r <= 300) st.prob = (int8_t) (r - 200);
        else if (r >= 401 && r <= 404) st.rat = (int8_t) (r - 400);
        g.repaint();
    });
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int t, i;
    dragTrack = -1;
    if (! cellAt (e.position, t, i)) return;
    if (onSelect) onSelect (t);

    auto& tp = proc.bank.pats[proc.editPattern()].tracks[t];
    if (e.mods.isAltDown()) { proc.pushUndo(); tp.length = (int8_t) (i + 1); repaint(); return; }   // Alt-clic = longueur
    if (i >= tp.length) return;
    if (e.mods.isPopupMenu()) { stepMenu (t, i); return; }
    if (e.mods.isCommandDown())                                                                     // Cmd-clic = p-lock
    {
        if (onLockStep) { if (lockTrack == t && lockStep == i) onLockStep (-1, -1); else onLockStep (t, i); }
        repaint();
        return;
    }

    proc.pushUndo();
    auto& s = tp.steps[i];
    paintAccent = e.mods.isShiftDown();
    if (paintAccent) { paintValue = s.acc ? 0 : 1; s.acc = (int8_t) paintValue; if (paintValue) s.on = 1; }
    else             { paintValue = s.on ? 0 : 1;  s.on = (int8_t) paintValue; }
    dragTrack = t;
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    int t, i;
    if (dragTrack < 0 || ! cellAt (e.position, t, i) || t != dragTrack) return;
    auto& tp = proc.bank.pats[proc.editPattern()].tracks[t];
    if (i >= tp.length) return;
    auto& s = tp.steps[i];
    if (paintAccent) { s.acc = (int8_t) paintValue; if (paintValue) s.on = 1; }
    else             s.on = (int8_t) paintValue;
    repaint();
}

// ===========================================================================
// LaneEditor
void LaneEditor::paint (juce::Graphics& g)
{
    static const char* names[kNumLanes] { "Probability", "Ratchet", "Nudge", "Tune", "Condition", "Slide" };
    const int pat = proc.editPattern();
    const auto& tp = proc.bank.pats[pat].tracks[track];
    const float lh = (float) getHeight() / (float) kNumLanes;
    const float cw = ((float) getWidth() - labelW) / (float) kMaxSteps;
    const auto tc = col::track (track);

    for (int l = 0; l < kNumLanes; ++l)
    {
        auto row = juce::Rectangle<float> (0.0f, l * lh, (float) getWidth(), lh).reduced (0.0f, 1.0f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (row.withWidth (labelW - 4.0f), 3.0f);
        g.setColour (l == CondL ? col::rnd : col::textDim);
        g.setFont (font (11.0f));
        g.drawText (names[l], row.withWidth (labelW - 4.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft);

        for (int i = 0; i < kMaxSteps; ++i)
        {
            auto r = juce::Rectangle<float> (labelW + i * cw, row.getY(), cw, row.getHeight()).reduced (1.5f, 1.0f);
            if (i >= tp.length) continue;
            const auto& s = tp.steps[i];
            g.setColour (col::panel);
            g.fillRect (r);
            const auto c = s.on ? tc : col::textDim.withAlpha (0.45f);
            g.setColour (c);
            switch (l)
            {
                case Prob:
                    g.fillRect (r.withTop (r.getBottom() - r.getHeight() * (float) s.prob / 100.0f));
                    break;
                case Ratchet:
                {
                    const float bh = r.getHeight() / 4.0f;
                    for (int k = 0; k < s.rat; ++k)
                        g.fillRect (r.getX(), r.getBottom() - (k + 1) * bh + 1.0f, r.getWidth(), bh - 2.0f);
                    break;
                }
                case Nudge:
                case TuneL:
                {
                    const float v = l == Nudge ? s.nudge / 50.0f : s.tune / 24.0f;
                    const float mid = r.getCentreY();
                    g.setColour (col::line);
                    g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
                    g.setColour (c);
                    const float h = v * r.getHeight() * 0.5f;
                    if (h > 0) g.fillRect (r.getX(), mid - h, r.getWidth(), h);
                    else if (h < 0) g.fillRect (r.getX(), mid, r.getWidth(), -h);
                    if (l == TuneL && s.tune != 0)
                    {
                        g.setColour (col::text);
                        g.setFont (font (9.0f));
                        g.drawText (juce::String ((int) s.tune), r, v > 0 ? juce::Justification::centredBottom : juce::Justification::centredTop);
                    }
                    break;
                }
                case SlideL:
                    if (s.slide)
                    {
                        g.setColour (c);
                        g.drawLine (r.getX() + 2.0f, r.getBottom() - 2.0f, r.getRight() - 2.0f, r.getY() + 2.0f, 2.0f);
                    }
                    break;
                case CondL:
                    if (s.cond != 0)
                    {
                        g.setColour (col::rnd.withAlpha (s.on ? 0.9f : 0.4f));
                        g.fillRect (r);
                        g.setColour (col::bg);
                        g.setFont (font (8.5f, true));
                        g.drawFittedText (condShort()[s.cond], r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
                    }
                    break;
                default: break;
            }
        }
    }
}

void LaneEditor::edit (const juce::MouseEvent& e, bool reset)
{
    const float lh = (float) getHeight() / (float) kNumLanes;
    const float cw = ((float) getWidth() - labelW) / (float) kMaxSteps;
    if (e.position.x < labelW || dragLane < 0) return;
    const int i = (int) ((e.position.x - labelW) / cw);
    auto& tp = proc.bank.pats[proc.editPattern()].tracks[track];
    if (i < 0 || i >= tp.length) return;
    auto& s = tp.steps[i];
    const float y = juce::jlimit (0.0f, 1.0f, (e.position.y - dragLane * lh) / lh);   // 0 = haut
    switch (dragLane)
    {
        case Prob:    s.prob  = (int8_t) (reset ? 100 : juce::jlimit (0, 100, (int) std::round ((1.0f - y) * 20.0f) * 5)); break;
        case Ratchet: s.rat   = (int8_t) (reset ? 1   : juce::jlimit (1, 4, 1 + (int) ((1.0f - y) * 4.0f))); break;
        case Nudge:   s.nudge = (int8_t) (reset ? 0   : juce::jlimit (-50, 50, (int) std::round ((0.5f - y) * 100.0f))); break;
        case TuneL:   s.tune  = (int8_t) (reset ? 0   : juce::jlimit (-24, 24, (int) std::round ((0.5f - y) * 48.0f))); break;
        case CondL:   s.cond  = (int8_t) (reset ? 0   : juce::jlimit (0, kNumConds - 1, (int) ((1.0f - y) * kNumConds))); break;
        case SlideL:  if (! slidePainting) { slideValue = s.slide ? 0 : 1; slidePainting = true; }
                      s.slide = (int8_t) (reset ? 0 : slideValue); break;
        default: break;
    }
    repaint();
}

void LaneEditor::mouseDown (const juce::MouseEvent& e)
{
    dragLane = juce::jlimit (0, kNumLanes - 1, (int) (e.position.y / ((float) getHeight() / (float) kNumLanes)));
    slidePainting = false;
    proc.pushUndo();
    edit (e, false);
}
void LaneEditor::mouseDrag (const juce::MouseEvent& e)        { edit (e, false); }
void LaneEditor::mouseDoubleClick (const juce::MouseEvent& e) { edit (e, true); }

} // namespace alea::gui
