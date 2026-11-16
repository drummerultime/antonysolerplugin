#include "MelodyWidgets.h"

namespace cantus::gui
{
static const juce::Colour bassCol { 0xfff2b84b };
static const juce::Colour bassAcc { 0xffff7a1a };

// 16 colonnes pour un pattern de 16 pas ou moins (plus lisible), 32 sinon
static int visSteps (const CantusAudioProcessor& p) { return p.bank.pats[p.editPattern()].length.load() <= 16 ? 16 : kSteps; }

static bool inScale (int semis, int scale)
{
    const int pc = ((semis % 12) + 12) % 12;
    return std::abs (alea::quantizeSemis ((float) pc, scale <= 0 ? 1 : scale) - (float) pc) < 0.01f;
}

bool PianoRoll::cellAt (juce::Point<float> p, int& step, int& row) const
{
    const float cw = ((float) getWidth() - kKeysW) / (float) visSteps (proc), rh = (float) getHeight() / 25.0f;
    if (p.x < kKeysW) return false;
    step = (int) ((p.x - kKeysW) / cw);
    row = 24 - (int) (p.y / rh);
    return step >= 0 && step < kSteps && row >= 0 && row <= 24;
}

void PianoRoll::paint (juce::Graphics& g)
{
    const int pat = proc.editPattern();
    const auto& P = proc.bank.pats[pat];
    const int len = P.length;
    const float cw = ((float) getWidth() - kKeysW) / (float) visSteps (proc), rh = (float) getHeight() / 25.0f;
    const int root = (int) proc.paramReal ("root");
    const int scale = (int) proc.paramReal ("scale");
    const int baseOct = (int) proc.paramReal ("baseoct");
    const bool showPlay = proc.currentPattern.load() == pat;
    const int ps = showPlay ? proc.playStep.load() : -1;
    const uint32_t evo = proc.evoMask[pat].load();

    // fond : lignes de la gamme, tonique marquee
    for (int row = 0; row <= 24; ++row)
    {
        const float y = (24 - row) * rh;
        const bool isRoot = row % 12 == 0, sc = inScale (row, scale);
        g.setColour (isRoot ? col::panelHi.brighter (0.12f) : (sc ? col::panelHi : col::panel.darker (0.25f)));
        g.fillRect (kKeysW, y, (float) getWidth() - kKeysW, rh - 0.5f);
        g.setColour (isRoot ? bassCol : (sc ? col::text : col::textDim.withAlpha (0.6f)));
        g.setFont (font (8.5f, isRoot));
        const int midi = 24 + 12 * baseOct + root + row;
        g.drawText (rootNames()[(root + row) % 12] + juce::String (midi / 12 - 2), 4, (int) y, (int) kKeysW - 8, (int) rh, juce::Justification::centredRight);
    }
    // colonnes
    for (int i = 0; i < visSteps (proc); ++i)
    {
        const float x = kKeysW + i * cw;
        if (i >= len) { g.setColour (col::bg.withAlpha (0.75f)); g.fillRect (x, 0.0f, cw, (float) getHeight()); continue; }
        if (i % 4 == 0) { g.setColour (col::line); g.drawVerticalLine ((int) x, 0.0f, (float) getHeight()); }
        if (i == ps) { g.setColour (juce::Colours::white.withAlpha (0.08f)); g.fillRect (x, 0.0f, cw, (float) getHeight()); }
    }

    // notes
    float prevX = -1, prevY = -1;
    bool prevSlide = false;
    for (int i = 0; i < len; ++i)
    {
        const auto& s = P.steps[i];
        const bool flipped = ((evo >> i) & 1u) != 0;
        const bool on = (s.on != 0) != flipped;
        const float x = kKeysW + i * cw;
        if (! on) { prevSlide = false; prevX = -1; continue; }
        const int row = juce::jlimit (0, 24, (int) s.note);
        const float y = (24 - row) * rh;
        auto r = juce::Rectangle<float> (x + 1.0f, y + 0.5f, cw - 2.0f, rh - 1.0f);
        if (s.tie && prevX >= 0)
        {
            g.setColour (bassCol.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (prevX, prevY + 0.5f, x + cw - 1.0f - prevX, rh - 1.0f));
        }
        else
        {
            const float alpha = 0.35f + 0.65f * (float) s.prob / 100.0f;
            g.setColour ((s.acc ? bassAcc : bassCol).withAlpha (alpha));
            g.fillRoundedRectangle (r, 2.0f);
        }
        if (prevSlide && prevX >= 0)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawLine (prevX + cw - 2.0f, prevY + rh * 0.5f, x + 1.0f, y + rh * 0.5f, 1.5f);
        }
        if (s.oct != 0)
        {
            g.setColour (juce::Colours::white);
            g.setFont (font (8.0f, true));
            g.drawText ((s.oct > 0 ? "+" : "") + juce::String ((int) s.oct), r.translated (0, s.oct > 0 ? -rh : rh), juce::Justification::centred);
        }
        if (flipped && s.on)   // retire par l'evolution
        {
            g.setColour (col::bg); g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 1.5f);
        }
        if (flipped && ! s.on) { g.setColour (juce::Colours::white); g.drawRoundedRectangle (r, 2.0f, 1.0f); }
        const int en = proc.evoNote[pat][i].load();
        if (en != 0)
        {
            g.setColour (juce::Colours::white);
            g.setFont (font (8.0f, true));
            g.drawText (en > 0 ? "^" : "v", r, juce::Justification::centredRight);
        }
        if (s.hasLocks()) { g.setColour (col::plock); g.fillEllipse (r.getRight() - 4.0f, r.getY(), 3.5f, 3.5f); }
        if (s.cond != 0) { g.setColour (col::rnd); g.fillRect (r.getX(), r.getBottom() - 2.0f, 4.0f, 2.0f); }
        if (s.rat > 1) { g.setColour (col::bg); g.setFont (font (8.0f, true)); g.drawText ("x" + juce::String ((int) s.rat), r, juce::Justification::centredLeft); }
        prevX = s.tie ? prevX : x; prevY = s.tie ? prevY : y;
        if (! s.tie) { prevX = x + 1.0f; prevY = y; }
        prevSlide = s.slide != 0;
    }

    // apercu de la mesure a venir
    if (showPreview)
        for (int i = 0; i < len; ++i)
        {
            const float x = kKeysW + i * cw + cw * 0.5f;
            if ((preview.ghost >> i) & 1u) { g.setColour (col::rnd); g.fillEllipse (x - 2.5f, 1.0f, 5.0f, 5.0f); }
            else if ((preview.skip >> i) & 1u) { g.setColour (col::lock); g.fillRect (x - 3.0f, 2.0f, 6.0f, 2.0f); }
        }
    if (lockStep >= 0 && lockStep < len)
    {
        g.setColour (col::plock);
        g.drawRect (juce::Rectangle<float> (kKeysW + lockStep * cw, 0.0f, cw, (float) getHeight()), 2.0f);
    }
}

void PianoRoll::stepMenu (int i)
{
    const int pat = proc.editPattern();
    auto& s = proc.bank.pats[pat].steps[i];
    juce::PopupMenu m, cond, prob, rat, oct, gate;
    m.addSectionHeader ("Pas " + juce::String (i + 1));
    m.addItem (1, "Pas actif", true, s.on != 0);
    m.addItem (2, "Accent", true, s.acc != 0);
    m.addItem (3, "Slide (glisse vers la note suivante)", true, s.slide != 0);
    m.addItem (4, "Tie (prolonge la note precedente)", true, s.tie != 0);
    m.addItem (5, "Editer les p-locks (potards SYNTH)");
    m.addItem (6, "Effacer les p-locks", s.hasLocks());
    for (int o = -2; o <= 2; ++o) oct.addItem (300 + o + 2, (o > 0 ? "+" : "") + juce::String (o), true, s.oct == o);
    m.addSubMenu ("Octave", oct);
    for (int gv : { 10, 25, 40, 55, 70, 85, 100 }) gate.addItem (400 + gv, juce::String (gv) + " %", true, s.gate == gv);
    m.addSubMenu ("Gate", gate);
    for (int c = 0; c < kNumConds; ++c) cond.addItem (100 + c, condNames()[c], true, s.cond == c);
    m.addSubMenu ("Condition", cond);
    for (int p : { 100, 87, 75, 50, 33, 25, 12 }) prob.addItem (200 + p, juce::String (p) + " %", true, s.prob == p);
    m.addSubMenu ("Probabilite", prob);
    for (int r = 1; r <= 4; ++r) rat.addItem (600 + r, juce::String (r) + (r == 1 ? " coup" : " coups"), true, s.rat == r);
    m.addSubMenu ("Ratchet", rat);
    juce::Component::SafePointer<PianoRoll> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, i, pat] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = *safe;
        auto& st = pr.proc.bank.pats[pat].steps[i];
        pr.proc.pushUndo();
        if (r == 1) st.on = (int8_t) (st.on ? 0 : 1);
        else if (r == 2) st.acc = (int8_t) (st.acc ? 0 : 1);
        else if (r == 3) st.slide = (int8_t) (st.slide ? 0 : 1);
        else if (r == 4) st.tie = (int8_t) (st.tie ? 0 : 1);
        else if (r == 5) { if (pr.onLockStep) pr.onLockStep (i); }
        else if (r == 6) st.clearLocks();
        else if (r >= 100 && r < 100 + kNumConds) st.cond = (int8_t) (r - 100);
        else if (r >= 200 && r <= 300) st.prob = (int8_t) (r - 200);
        else if (r >= 300 && r < 305) st.oct = (int8_t) (r - 302);
        else if (r >= 400 && r <= 500) st.gate = (int8_t) (r - 400);
        else if (r >= 601 && r <= 604) st.rat = (int8_t) (r - 600);
        pr.repaint();
    });
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    int i, row;
    paintMode = 0; lastDragStep = -1;
    if (! cellAt (e.position, i, row)) return;
    auto& P = proc.bank.pats[proc.editPattern()];
    if (e.mods.isAltDown()) { proc.pushUndo(); P.length = (int8_t) (i + 1); repaint(); return; }
    if (i >= P.length) return;
    if (e.mods.isPopupMenu()) { stepMenu (i); return; }
    if (e.mods.isCommandDown()) { if (onLockStep) onLockStep (lockStep == i ? -1 : i); repaint(); return; }
    proc.pushUndo();
    auto& s = P.steps[i];
    if (e.mods.isShiftDown()) { s.acc = (int8_t) (s.acc ? 0 : 1); if (s.acc) s.on = 1; repaint(); return; }
    if (s.on && ! s.tie && s.note == row) { s.on = 0; paintMode = 2; }
    else { s.on = 1; s.tie = 0; s.note = (int8_t) row; paintMode = 1; proc.guiTrig = false; }
    lastDragStep = i;
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    int i, row;
    if (paintMode == 0 || ! cellAt (e.position, i, row)) return;
    auto& P = proc.bank.pats[proc.editPattern()];
    if (i >= P.length) return;
    auto& s = P.steps[i];
    if (paintMode == 1) { s.on = 1; s.tie = 0; s.note = (int8_t) row; }
    else if (i != lastDragStep) s.on = 0;
    lastDragStep = i;
    repaint();
}

// ===========================================================================
void BassLanes::paint (juce::Graphics& g)
{
    static const char* names[kNumLanes] { "Accent", "Slide", "Tie", "Octave", "Gate", "Velocity", "Probability", "Ratchet", "Condition", "Nudge" };
    const auto& P = proc.bank.pats[proc.editPattern()];
    const float lh = (float) getHeight() / (float) kNumLanes;
    const float cw = ((float) getWidth() - kKeysW) / (float) visSteps (proc);
    for (int l = 0; l < kNumLanes; ++l)
    {
        auto row = juce::Rectangle<float> (0.0f, l * lh, (float) getWidth(), lh).reduced (0.0f, 0.5f);
        g.setColour (col::panel);
        g.fillRect (row.withWidth (kKeysW - 3.0f));
        g.setColour (l == Cond ? col::rnd : col::textDim);
        g.setFont (font (9.5f));
        g.drawText (names[l], row.withWidth (kKeysW - 6.0f), juce::Justification::centredRight);
        for (int i = 0; i < P.length; ++i)
        {
            auto r = juce::Rectangle<float> (kKeysW + i * cw, row.getY(), cw, row.getHeight()).reduced (1.0f, 0.5f);
            const auto& s = P.steps[i];
            g.setColour (col::panel);
            g.fillRect (r);
            const auto c = s.on ? bassCol : col::textDim.withAlpha (0.4f);
            g.setColour (c);
            switch (l)
            {
                case Acc: if (s.acc) { g.setColour (bassAcc); g.fillRect (r); } break;
                case Sld: if (s.slide) g.drawLine (r.getX() + 1, r.getBottom() - 1, r.getRight() - 1, r.getY() + 1, 2.0f); break;
                case Tie: if (s.tie) g.fillRect (r.withSizeKeepingCentre (r.getWidth(), r.getHeight() * 0.4f)); break;
                case Oct:
                    if (s.oct != 0)
                    {
                        g.setFont (font (9.0f, true));
                        g.drawText ((s.oct > 0 ? "+" : "") + juce::String ((int) s.oct), r, juce::Justification::centred);
                    }
                    break;
                case Gate: g.fillRect (r.withWidth (r.getWidth() * (float) s.gate / 100.0f)); break;
                case Vel:  g.fillRect (r.withTop (r.getBottom() - r.getHeight() * (float) s.vel / 127.0f)); break;
                case Prob: g.fillRect (r.withTop (r.getBottom() - r.getHeight() * (float) s.prob / 100.0f)); break;
                case Rat:  g.fillRect (r.withTop (r.getBottom() - r.getHeight() * (float) s.rat / 4.0f)); break;
                case Cond:
                    if (s.cond != 0)
                    {
                        g.setColour (col::rnd); g.fillRect (r);
                        g.setColour (col::bg); g.setFont (font (8.0f, true));
                        g.drawFittedText (condShort()[s.cond], r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
                    }
                    break;
                case Nudge:
                {
                    const float v = s.nudge / 50.0f * r.getWidth() * 0.5f;
                    g.fillRect (v >= 0 ? r.getCentreX() : r.getCentreX() + v, r.getY(), std::abs (v), r.getHeight());
                    break;
                }
                default: break;
            }
        }
    }
}

void BassLanes::edit (const juce::MouseEvent& e, bool reset)
{
    const float lh = (float) getHeight() / (float) kNumLanes;
    const float cw = ((float) getWidth() - kKeysW) / (float) visSteps (proc);
    if (e.position.x < kKeysW || dragLane < 0) return;
    const int i = (int) ((e.position.x - kKeysW) / cw);
    auto& P = proc.bank.pats[proc.editPattern()];
    if (i < 0 || i >= P.length) return;
    auto& s = P.steps[i];
    const float y = juce::jlimit (0.0f, 1.0f, (e.position.y - dragLane * lh) / lh);
    const float xr = juce::jlimit (0.0f, 1.0f, (e.position.x - kKeysW - i * cw) / cw);
    auto toggle = [&] (std::atomic<int8_t>& f)
    {
        if (reset) { f = 0; return; }
        if (! toggling) { toggleValue = f ? 0 : 1; toggling = true; }
        f = (int8_t) toggleValue;
    };
    switch (dragLane)
    {
        case Acc: toggle (s.acc); break;
        case Sld: toggle (s.slide); break;
        case Tie: toggle (s.tie); if (s.tie) s.on = 1; break;
        case Oct:  s.oct = (int8_t) (reset ? 0 : juce::jlimit (-2, 2, (int) std::round ((0.5f - y) * 4.0f))); break;
        case Gate: s.gate = (int8_t) (reset ? 55 : juce::jlimit (5, 100, (int) std::round ((1.0f - y) * 19.0f) * 5 + 5)); break;
        case Vel:  s.vel = (int8_t) (reset ? 100 : juce::jlimit (1, 127, (int) std::round ((1.0f - y) * 127.0f))); break;
        case Prob: s.prob = (int8_t) (reset ? 100 : juce::jlimit (0, 100, (int) std::round ((1.0f - y) * 20.0f) * 5)); break;
        case Rat:  s.rat = (int8_t) (reset ? 1 : juce::jlimit (1, 4, 1 + (int) ((1.0f - y) * 4.0f))); break;
        case Cond: s.cond = (int8_t) (reset ? 0 : juce::jlimit (0, kNumConds - 1, (int) ((1.0f - y) * kNumConds))); break;
        case Nudge: s.nudge = (int8_t) (reset ? 0 : juce::jlimit (-50, 50, (int) std::round ((xr - 0.5f) * 100.0f))); break;
        default: break;
    }
    repaint();
}

void BassLanes::mouseDown (const juce::MouseEvent& e)
{
    dragLane = juce::jlimit (0, kNumLanes - 1, (int) (e.position.y / ((float) getHeight() / (float) kNumLanes)));
    toggling = false;
    proc.pushUndo();
    edit (e, false);
}
void BassLanes::mouseDrag (const juce::MouseEvent& e) { edit (e, false); }
void BassLanes::mouseDoubleClick (const juce::MouseEvent& e) { edit (e, true); }

} // namespace cantus::gui
