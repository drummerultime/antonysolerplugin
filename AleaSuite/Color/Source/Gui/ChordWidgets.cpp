#include "ChordWidgets.h"

namespace color::gui
{
namespace th = color::theory;

juce::Colour functionColour (int fn)
{
    switch (fn)
    {
        case 0:  return juce::Colour (0xff4fd1a5);   // tonique : repos
        case 1:  return juce::Colour (0xff5aa0ff);   // sous-dominante : depart
        case 2:  return juce::Colour (0xffff8a4a);   // dominante : tension
        default: return colorCol;                    // couleur / chromatique
    }
}

juce::String functionLetter (int fn)
{
    switch (fn) { case 0: return "T"; case 1: return "S"; case 2: return "D"; default: return "C"; }
}

// suggestions de suite : doublons retires, probabilites normalisees
static std::vector<std::pair<th::Chord, float>> cleanSuggestions (std::vector<std::pair<th::Chord, float>> in)
{
    std::vector<std::pair<th::Chord, float>> out;
    for (auto& s : in)
    {
        bool dup = false;
        for (auto& o : out) if (o.first.root == s.first.root && o.first.qual == s.first.qual) { o.second += s.second; dup = true; break; }
        if (! dup) out.push_back (s);
    }
    float sum = 0.0f; for (auto& o : out) sum += o.second;
    if (sum > 0.0f) for (auto& o : out) o.second /= sum;
    std::sort (out.begin(), out.end(), [] (const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

static int keyLydianTonic (int mode)
{
    const int sc = th::scaleSize (mode) == 7 ? mode : 5;
    return th::lydianTonic (th::diatonic (sc, 0, 4));
}

// ===========================================================================
// ChordLane
juce::Rectangle<float> ChordLane::blockRect (int i) const
{
    const auto& P = proc.bank.pats[proc.editPattern()];
    const int n = P.chordCount();
    const float total = (float) P.totalBeats();
    const float w = (float) getWidth() - 8.0f;
    float acc = 0.0f;
    for (int k = 0; k < i && k < n; ++k) acc += P.chord (k).beats;
    const float x = 4.0f + w * acc / total;
    const float bw = w * (float) P.chord (i).beats / total;
    return { x + 1.5f, 18.0f, bw - 3.0f, (float) getHeight() - 22.0f };
}

int ChordLane::chordAt (juce::Point<float> p, bool& onEdge) const
{
    const auto& P = proc.bank.pats[proc.editPattern()];
    onEdge = false;
    for (int i = 0; i < P.chordCount(); ++i)
    {
        const auto r = blockRect (i);
        if (p.x >= r.getX() - 1.5f && p.x <= r.getRight() + 1.5f)
        {
            onEdge = p.x > r.getRight() - 7.0f;
            return i;
        }
    }
    return -1;
}

void ChordLane::paint (juce::Graphics& g)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const int pat = proc.editPattern();
    const auto& P = proc.bank.pats[pat];
    const int n = P.chordCount();
    const int key = proc.keyRoot(), mode = proc.mode();
    const bool playing = proc.isRunning.load() && proc.currentPattern.load() == pat;
    const int cur = playing ? proc.curChordIndex.load() : -1;

    g.setFont (font (11.0f, true));
    g.setColour (colorCol);
    g.drawText ("PROGRESSION", 10, 2, 120, 15, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (10.0f));
    g.drawText (rootNames()[key] + " " + th::scale (mode).name + "  -  " + juce::String (P.totalBeats() / 4.0, 1) + " mesures",
                130, 2, 400, 15, juce::Justification::centredLeft);

    for (int i = 0; i < n; ++i)
    {
        const auto c = P.chord (i);
        const auto r = blockRect (i);
        const int fn = th::chordFunction (c, mode);
        const auto fc = functionColour (fn);
        const bool isCur = i == cur, isSel = i == selected;
        g.setColour (isCur ? fc.withAlpha (0.42f) : col::panelHi);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (fc.withAlpha (isCur ? 1.0f : 0.8f));
        g.fillRoundedRectangle (r.withHeight (4.0f), 2.0f);
        if (isSel) { g.setColour (juce::Colours::white.withAlpha (0.8f)); g.drawRoundedRectangle (r, 5.0f, 1.5f); }

        // tension (Persichetti) : petite barre a gauche
        const float t = th::chordTension (c);
        g.setColour (col::lock.withAlpha (0.25f + 0.6f * t));
        g.fillRect (r.getX() + 3.0f, r.getBottom() - 4.0f - (r.getHeight() - 14.0f) * t, 3.0f, (r.getHeight() - 14.0f) * t);

        const auto inner = r.reduced (8.0f, 6.0f).withTrimmedTop (2.0f);
        g.setColour (col::text);
        const float big = juce::jlimit (11.0f, 22.0f, r.getWidth() / 5.0f);
        g.setFont (font (big, true));
        g.drawFittedText (th::chordName (c, key), inner.toNearestInt().withHeight ((int) big + 6), juce::Justification::topLeft, 1, 0.7f);
        g.setFont (font (11.0f));
        g.setColour (fc);
        g.drawFittedText (th::romanNumeral (c, mode), inner.toNearestInt().withTrimmedTop ((int) big + 8).withHeight (16), juce::Justification::topLeft, 1, 0.7f);
        g.setColour (col::textDim);
        g.setFont (font (9.5f));
        g.drawText (functionLetter (fn) + "  " + juce::String ((int) c.beats) + (c.beats > 1 ? " temps" : " temps"),
                    inner.toNearestInt().withTrimmedTop (inner.toNearestInt().getHeight() - 13), juce::Justification::bottomLeft);

        if (isCur && (proc.curChordSub.load() || proc.curChordApproach.load()))
        {
            const auto eff = unpackChord (proc.curChordPacked.load());
            g.setColour (col::plock);
            g.setFont (font (10.0f, true));
            g.drawText ((proc.curChordApproach.load() ? "approche > " : "sub > ") + th::chordName (eff, key),
                        r.toNearestInt().withTrimmedTop ((int) r.getHeight() - 16).withTrimmedRight (6), juce::Justification::centredRight);
        }
    }

    // tete de lecture
    if (playing)
    {
        const double total = P.totalBeats();
        double pos = std::fmod (proc.playPpq.load() - proc.patStartShared.load(), total);
        if (pos < 0) pos += total;
        const float x = 4.0f + ((float) getWidth() - 8.0f) * (float) (pos / total);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillRect (x, 18.0f, 1.5f, (float) getHeight() - 22.0f);
    }
}

void ChordLane::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    chordAt (e.position, edge);
    setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void ChordLane::mouseDown (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = chordAt (e.position, edge);
    didResize = false;
    if (i < 0) return;
    selected = i;
    if (onSelect) onSelect (i);
    if (e.mods.isPopupMenu()) { chordMenu (i); repaint(); return; }
    if (edge)
    {
        proc.pushUndo();
        resizing = i; resizeStartBeats = proc.bank.pats[proc.editPattern()].chord (i).beats; resizeStartX = e.position.x;
        return;
    }
    proc.auditionChord = i;
    repaint();
}

void ChordLane::mouseDrag (const juce::MouseEvent& e)
{
    if (resizing < 0) return;
    auto& P = proc.bank.pats[proc.editPattern()];
    const float pxPerBeat = ((float) getWidth() - 8.0f) / (float) P.totalBeats();
    const int nb = juce::jlimit (1, 32, resizeStartBeats + (int) std::round ((e.position.x - resizeStartX) / juce::jmax (4.0f, pxPerBeat)));
    auto c = P.chord (resizing);
    if (c.beats != nb) { c.beats = (int8_t) nb; P.setChord (resizing, c); didResize = true; repaint(); }
}

void ChordLane::mouseUp (const juce::MouseEvent&)
{
    if (resizing >= 0 && didResize && onChanged) onChanged();
    resizing = -1;
}

void ChordLane::chordMenu (int i)
{
    const int pat = proc.editPattern();
    auto& P = proc.bank.pats[pat];
    const auto c = P.chord (i);
    const auto next = P.chord ((i + 1) % P.chordCount());
    const int key = proc.keyRoot(), mode = proc.mode();

    juce::PopupMenu m, subs, reh, qual, root, inv, bass, dur, sugg;
    m.addSectionHeader (th::chordName (c, key) + "   (" + th::romanNumeral (c, mode) + ")   gamme : " + th::parentScaleName (c, proc.paramReal ("outgoing"), key));
    m.addItem (1, "Ecouter");
    auto subList = th::substitutesFor (c, &next, mode);
    for (int k = 0; k < (int) subList.size() && k < 24; ++k)
        subs.addItem (1000 + k, th::chordName (subList[(size_t) k], key) + "   " + th::romanNumeral (subList[(size_t) k], mode));
    m.addSubMenu ("Substitutions", subs, ! subList.empty());
    const auto rn = th::reharmNames();
    for (int k = 0; k < rn.size(); ++k) reh.addItem (2000 + k, rn[k]);
    m.addSubMenu ("Reharmoniser (cet accord)", reh);
    auto sn = cleanSuggestions (th::suggestNext (c, mode, (int) proc.paramReal ("style")));
    for (int k = 0; k < (int) sn.size() && k < 12; ++k)
        sugg.addItem (3000 + k, th::chordName (sn[(size_t) k].first, key) + "   " + th::romanNumeral (sn[(size_t) k].first, mode)
                                    + "   " + juce::String ((int) std::round (sn[(size_t) k].second * 100.0f)) + " %");
    m.addSubMenu ("Inserer l'accord suivant probable", sugg, ! sn.empty());
    m.addSeparator();
    {
        juce::PopupMenu tert, ext, alt, other;
        for (int q = 0; q < th::numQualities(); ++q)
        {
            const auto& qd = th::quality (q);
            auto& target = q <= th::QSix || q == th::QMin6 || q == th::QFive ? tert
                         : (q <= th::QMin11 || q == th::QDom13 || q == th::QSus9 || q == th::QSixNine ? ext
                         : (q <= th::QDom7b13 || q == th::QDom7b5 ? alt : other));
            target.addItem (4000 + q, th::chordName ({ c.root, (int8_t) q, 0, -1, 4 }, key), true, q == c.qual);
            juce::ignoreUnused (qd);
        }
        qual.addSubMenu ("Triades / 6", tert); qual.addSubMenu ("7e, 9e, 11e, 13e", ext); qual.addSubMenu ("Alterees", alt);
        qual.addSubMenu ("Quartes, clusters, post-tonal", other);
    }
    m.addSubMenu ("Qualite", qual);
    for (int r = 0; r < 12; ++r) root.addItem (5000 + r, rootNames()[(key + r) % 12] + "   (" + th::romanNumeral ({ (int8_t) r, c.qual, 0, -1, 4 }, mode) + ")", true, r == c.root);
    m.addSubMenu ("Fondamentale", root);
    static const char* invN[] { "Etat fondamental", "1er renversement", "2e renversement", "3e renversement" };
    for (int k = 0; k < 4; ++k) inv.addItem (6000 + k, invN[k], true, c.inversion == k && c.bass < 0);
    m.addSubMenu ("Renversement", inv);
    bass.addItem (7000, "Basse = fondamentale / renversement", true, c.bass < 0);
    for (int r = 0; r < 12; ++r) bass.addItem (7001 + r, "/" + rootNames()[(key + r) % 12], true, c.bass == r);
    m.addSubMenu ("Basse (slash chord, pedale)", bass);
    for (int b : { 1, 2, 3, 4, 6, 8, 12, 16 }) dur.addItem (8000 + b, juce::String (b) + " temps", true, c.beats == b);
    m.addSubMenu ("Duree", dur);
    m.addSeparator();
    m.addItem (9001, "Dupliquer", P.chordCount() < kMaxChords);
    m.addItem (9002, "Supprimer", P.chordCount() > 1);
    m.addItem (9003, "Inserer un accord diatonique au hasard apres", P.chordCount() < kMaxChords);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
        [this, i, pat, c, subList, sn, mode] (int r)
        {
            if (r <= 0) return;
            if (r == 1) { proc.auditionChord = i; return; }
            proc.pushUndo();
            auto x = c;
            if (r >= 1000 && r < 2000) { x = subList[(size_t) (r - 1000)]; x.beats = c.beats; proc.setChordAt (pat, i, x); }
            else if (r >= 2000 && r < 3000) proc.reharmonize (pat, r - 2000, i);
            else if (r >= 3000 && r < 4000) { auto s = sn[(size_t) (r - 3000)].first; s.beats = c.beats; proc.insertChord (pat, i + 1, s); }
            else if (r >= 4000 && r < 5000) { x.qual = (int8_t) (r - 4000); proc.setChordAt (pat, i, x); }
            else if (r >= 5000 && r < 6000) { x.root = (int8_t) (r - 5000); proc.setChordAt (pat, i, x); }
            else if (r >= 6000 && r < 7000) { x.inversion = (int8_t) (r - 6000); x.bass = -1; proc.setChordAt (pat, i, x); }
            else if (r == 7000) { x.bass = -1; proc.setChordAt (pat, i, x); }
            else if (r > 7000 && r < 8000) { x.bass = (int8_t) (r - 7001); if (x.bass == x.root) x.bass = -1; proc.setChordAt (pat, i, x); }
            else if (r >= 8000 && r < 9000) { x.beats = (int8_t) (r - 8000); proc.setChordAt (pat, i, x); }
            else if (r == 9001) proc.insertChord (pat, i + 1, c);
            else if (r == 9002) proc.removeChord (pat, i);
            else if (r == 9003)
            {
                auto d = th::diatonic (th::scaleSize (mode) == 7 ? mode : 5, juce::Random::getSystemRandom().nextInt (7), 4);
                d.beats = c.beats;
                proc.insertChord (pat, i + 1, d);
            }
            if (r != 9001 && r != 9003 && r < 3000 && r >= 1000) proc.auditionChord = i;
            if (onChanged) onChanged();
            repaint();
        });
}

// ===========================================================================
// RhythmGrid
int RhythmGrid::visSteps() const { return proc.bank.pats[proc.editPattern()].length.load() <= 16 ? 16 : kSteps; }
float RhythmGrid::cellW() const { return ((float) getWidth() - kLabelW) / (float) visSteps(); }
int RhythmGrid::stepAt (float x) const { return x < kLabelW ? -1 : juce::jlimit (0, kSteps - 1, (int) ((x - kLabelW) / cellW())); }
int RhythmGrid::laneAt (float y) const
{
    if (y < kHitsH) return -1;
    const float lh = ((float) getHeight() - kHitsH - 2.0f) / (float) kNumLanes;
    return juce::jlimit (0, kNumLanes - 1, (int) ((y - kHitsH - 2.0f) / lh));
}

void RhythmGrid::paint (juce::Graphics& g)
{
    const int pat = proc.editPattern();
    const auto& P = proc.bank.pats[pat];
    const int len = P.length, vs = visSteps();
    const float cw = cellW();
    const bool showPlay = proc.currentPattern.load() == pat;
    const int ps = showPlay ? proc.playStep.load() : -1;
    const uint32_t evo = proc.evoMask[pat].load();
    g.fillAll (col::panel);

    // couloir des coups
    g.setColour (col::textDim);
    g.setFont (font (10.0f, true));
    g.drawText ("COUPS", 4, 0, (int) kLabelW - 8, (int) kHitsH, juce::Justification::centredRight);
    for (int i = 0; i < vs; ++i)
    {
        const float x = kLabelW + i * cw;
        g.setColour (i >= len ? col::bg : ((i / 4) % 2 ? col::panelHi : col::panelHi.brighter (0.06f)));
        g.fillRect (x + 0.5f, 2.0f, cw - 1.0f, kHitsH - 4.0f);
        if (i == ps) { g.setColour (juce::Colours::white.withAlpha (0.12f)); g.fillRect (x, 0.0f, cw, (float) getHeight()); }
    }
    for (int i = 0; i < len && i < vs; ++i)
    {
        const auto& s = P.steps[i];
        const bool on = (s.on != 0) != (((evo >> i) & 1u) != 0);
        if (! on) continue;
        const float x = kLabelW + i * cw;
        const float w = juce::jmin ((float) s.len, (float) (vs - i)) * cw;
        const float v = (float) s.vel / 127.0f;
        auto r = juce::Rectangle<float> (x + 1.5f, 6.0f, w - 3.0f, kHitsH - 12.0f);
        g.setColour (colorCol.withAlpha (0.25f + 0.55f * v));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (colorCol.brighter (0.3f));
        g.fillRoundedRectangle (r.withWidth (juce::jmax (3.0f, r.getWidth() * (float) s.gate / 100.0f)).withHeight (3.0f).withY (r.getBottom() - 3.0f), 1.0f);
        g.fillRect (r.withWidth (3.0f));
        if (s.acc) { g.setColour (col::accent); g.fillEllipse (x + 5.0f, 9.0f, 6.0f, 6.0f); }
        if (s.hasLocks()) { g.setColour (col::plock); g.fillRect (x + cw - 7.0f, 8.0f, 4.0f, 4.0f); }
        if (((evo >> i) & 1u) != 0) { g.setColour (col::rnd); g.drawRoundedRectangle (r, 3.0f, 1.0f); }
        static const char* partS[] { "", "top", "bass", "-b" };
        if (s.part != 0) { g.setColour (col::text); g.setFont (font (8.5f)); g.drawText (partS[(int) s.part], (int) x + 2, (int) kHitsH - 18, (int) cw, 10, juce::Justification::centredLeft); }
    }
    if (showPreview && showPlay)
        for (int i = 0; i < len && i < vs; ++i)
        {
            const float x = kLabelW + i * cw;
            if ((preview.ghost >> i) & 1u) { g.setColour (col::rnd); g.fillEllipse (x + cw * 0.5f - 3.0f, 1.0f, 6.0f, 6.0f); }
            if ((preview.skip >> i) & 1u) { g.setColour (col::lock); g.fillRect (x + 3.0f, kHitsH - 4.0f, cw - 6.0f, 2.0f); }
        }
    if (lockStep >= 0)
    {
        g.setColour (col::plock);
        g.drawRect (kLabelW + lockStep * cw, 1.0f, cw, kHitsH - 2.0f, 2.0f);
    }

    // couloirs
    static const char* names[kNumLanes] { "Velocity", "Gate", "Proba", "Partie", "Strum", "Octave", "Ratchet", "Cond", "Nudge" };
    const float lh = ((float) getHeight() - kHitsH - 2.0f) / (float) kNumLanes;
    for (int l = 0; l < kNumLanes; ++l)
    {
        const float y = kHitsH + 2.0f + l * lh;
        g.setColour (col::textDim);
        g.setFont (font (9.0f));
        g.drawText (names[l], 4, (int) y, (int) kLabelW - 8, (int) lh, juce::Justification::centredRight);
        for (int i = 0; i < vs; ++i)
        {
            const float x = kLabelW + i * cw;
            const auto& s = P.steps[i];
            const bool active = i < len && s.on != 0;
            g.setColour (i >= len ? col::bg : col::panelHi.withAlpha (active ? 1.0f : 0.5f));
            g.fillRect (x + 0.5f, y + 0.5f, cw - 1.0f, lh - 1.0f);
            if (i >= len) continue;
            const auto c = active ? colorCol : col::textDim.withAlpha (0.4f);
            g.setColour (c);
            g.setFont (font (8.5f));
            switch (l)
            {
                case Vel:   { const float v = (float) s.vel / 127.0f; g.fillRect (x + 2.0f, y + lh - 1.0f - (lh - 2.0f) * v, cw - 4.0f, (lh - 2.0f) * v); break; }
                case Gate:  { const float v = (float) s.gate / 100.0f; g.fillRect (x + 2.0f, y + lh - 1.0f - (lh - 2.0f) * v, cw - 4.0f, (lh - 2.0f) * v); break; }
                case Prob:  { const float v = (float) s.prob / 100.0f; g.fillRect (x + 2.0f, y + lh - 1.0f - (lh - 2.0f) * v, cw - 4.0f, (lh - 2.0f) * v); break; }
                case Part:  { static const char* t[] { ".", "T", "B", "-B" }; g.drawText (t[(int) s.part], (int) x, (int) y, (int) cw, (int) lh, juce::Justification::centred); break; }
                case Strum: { static const char* t[] { ".", "^", "v" }; g.drawText (t[(int) s.strum], (int) x, (int) y, (int) cw, (int) lh, juce::Justification::centred); break; }
                case Oct:   g.drawText (s.oct == 0 ? juce::String (".") : (s.oct > 0 ? "+1" : "-1"), (int) x, (int) y, (int) cw, (int) lh, juce::Justification::centred); break;
                case Rat:   g.drawText (s.rat <= 1 ? juce::String (".") : "x" + juce::String ((int) s.rat), (int) x, (int) y, (int) cw, (int) lh, juce::Justification::centred); break;
                case Cond:  g.drawText (s.cond == 0 ? juce::String (".") : condNames()[(int) s.cond].substring (0, 4), (int) x, (int) y, (int) cw, (int) lh, juce::Justification::centred); break;
                case Nudge:
                {
                    const float v = (float) s.nudge / 50.0f;
                    const float mid = x + cw * 0.5f;
                    g.fillRect (v >= 0 ? mid : mid + v * (cw * 0.5f - 2.0f), y + 2.0f, std::abs (v) * (cw * 0.5f - 2.0f) + 1.0f, lh - 4.0f);
                    break;
                }
                default: break;
            }
        }
    }
    for (int i = 0; i <= vs; i += 4)
    {
        g.setColour (col::line);
        g.drawVerticalLine ((int) (kLabelW + i * cw), 0.0f, (float) getHeight());
    }
}

void RhythmGrid::mouseDown (const juce::MouseEvent& e)
{
    const int st = stepAt (e.position.x);
    if (st < 0) return;
    auto& P = proc.bank.pats[proc.editPattern()];
    if (st >= P.length.load()) return;
    auto& s = P.steps[st];
    const int lane = laneAt (e.position.y);
    if (e.mods.isPopupMenu()) { stepMenu (st); return; }
    if (lane < 0)
    {
        if (e.mods.isCommandDown()) { if (onLockStep) onLockStep (lockStep == st ? -1 : st); return; }
        proc.pushUndo();
        if (e.mods.isShiftDown()) { if (! s.on) s.on = 1; s.acc = (int8_t) (s.acc ? 0 : 1); repaint(); return; }
        if (e.mods.isAltDown() && s.on) { dragMode = 1; dragStep = st; return; }
        if (s.on) { s.on = 0; dragMode = 4; }
        else { s.on = 1; s.len = 1; dragMode = 1; dragStep = st; }
        repaint();
        return;
    }
    proc.pushUndo();
    dragMode = 2; dragLane = lane;
    editLane (lane, st, e.position.y, false);
}

void RhythmGrid::mouseDrag (const juce::MouseEvent& e)
{
    auto& P = proc.bank.pats[proc.editPattern()];
    const int st = stepAt (e.position.x);
    if (st < 0) return;
    if (dragMode == 1 && dragStep >= 0)
    {
        P.steps[dragStep].len = (int8_t) juce::jlimit (1, kSteps, st - dragStep + 1);
        repaint();
    }
    else if (dragMode == 2) editLane (dragLane, st, e.position.y, false);
    else if (dragMode == 4 && st < P.length.load()) { P.steps[st].on = 0; repaint(); }
}

void RhythmGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int st = stepAt (e.position.x), lane = laneAt (e.position.y);
    if (st >= 0 && lane >= 0) editLane (lane, st, e.position.y, true);
}

void RhythmGrid::editLane (int lane, int step, float y, bool reset)
{
    auto& P = proc.bank.pats[proc.editPattern()];
    if (step >= P.length.load()) return;
    auto& s = P.steps[step];
    const float lh = ((float) getHeight() - kHitsH - 2.0f) / (float) kNumLanes;
    const float top = kHitsH + 2.0f + lane * lh;
    const float v = juce::jlimit (0.0f, 1.0f, 1.0f - (y - top) / lh);
    switch (lane)
    {
        case Vel:   s.vel = (int8_t) (reset ? 100 : juce::jlimit (1, 127, (int) (v * 127.0f))); break;
        case Gate:  s.gate = (int8_t) (reset ? 90 : juce::jlimit (5, 100, (int) (v * 100.0f))); break;
        case Prob:  s.prob = (int8_t) (reset ? 100 : juce::jlimit (0, 100, (int) (v * 100.0f))); break;
        case Part:  if (dragMode == 2 && ! reset) { s.part = (int8_t) ((s.part + 1) % 4); dragMode = 0; } else s.part = 0; break;
        case Strum: if (dragMode == 2 && ! reset) { s.strum = (int8_t) ((s.strum + 1) % 3); dragMode = 0; } else s.strum = 0; break;
        case Oct:   if (dragMode == 2 && ! reset) { s.oct = (int8_t) (s.oct == 1 ? -1 : s.oct + 1); dragMode = 0; } else s.oct = 0; break;
        case Rat:   if (dragMode == 2 && ! reset) { s.rat = (int8_t) (s.rat % 4 + 1); dragMode = 0; } else s.rat = 1; break;
        case Cond:  if (dragMode == 2 && ! reset) { s.cond = (int8_t) ((s.cond + 1) % kNumConds); dragMode = 0; } else s.cond = 0; break;
        case Nudge: s.nudge = (int8_t) (reset ? 0 : juce::jlimit (-50, 50, (int) ((v - 0.5f) * 100.0f))); break;
        default: break;
    }
    repaint();
}

void RhythmGrid::stepMenu (int step)
{
    auto& s = proc.bank.pats[proc.editPattern()].steps[step];
    juce::PopupMenu m, part, strum, oct, cond, len, prob, rat;
    m.addSectionHeader ("Pas " + juce::String (step + 1));
    m.addItem (1, "Actif", true, s.on != 0);
    m.addItem (2, "Accent", true, s.acc != 0);
    static const char* pn[] { "Tout l'accord", "Note du haut (melodie)", "Basse seule", "Sans la basse" };
    for (int k = 0; k < 4; ++k) part.addItem (100 + k, pn[k], true, s.part == k);
    m.addSubMenu ("Partie jouee", part);
    static const char* sn[] { "Strum : global", "Strum : montant", "Strum : descendant" };
    for (int k = 0; k < 3; ++k) strum.addItem (200 + k, sn[k], true, s.strum == k);
    m.addSubMenu ("Strum", strum);
    for (int k = -1; k <= 1; ++k) oct.addItem (300 + k + 1, k == 0 ? "0" : (k > 0 ? "+1 octave" : "-1 octave"), true, s.oct == k);
    m.addSubMenu ("Octave", oct);
    for (int k : { 1, 2, 3, 4, 6, 8, 12, 16, 32 }) len.addItem (400 + k, juce::String (k) + " pas", true, s.len == k);
    m.addSubMenu ("Duree", len);
    for (int k = 0; k < kNumConds; ++k) cond.addItem (500 + k, condNames()[k], true, s.cond == k);
    m.addSubMenu ("Condition", cond);
    for (int k : { 100, 75, 50, 25, 10 }) prob.addItem (600 + k, juce::String (k) + " %", true, s.prob == k);
    m.addSubMenu ("Probabilite", prob);
    for (int k = 1; k <= 4; ++k) rat.addItem (800 + k, k == 1 ? "Aucun" : "x" + juce::String (k), true, s.rat == k);
    m.addSubMenu ("Ratchet", rat);
    m.addItem (3, "P-locks (valeurs du son pour ce pas)...");
    m.addItem (4, "Effacer les p-locks", s.hasLocks());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this, step] (int r)
    {
        if (r <= 0) return;
        auto& st = proc.bank.pats[proc.editPattern()].steps[step];
        if (r == 3) { if (onLockStep) onLockStep (step); return; }
        proc.pushUndo();
        if (r == 1) st.on = (int8_t) (st.on ? 0 : 1);
        else if (r == 2) st.acc = (int8_t) (st.acc ? 0 : 1);
        else if (r == 4) st.clearLocks();
        else if (r >= 100 && r < 200) st.part = (int8_t) (r - 100);
        else if (r >= 200 && r < 300) st.strum = (int8_t) (r - 200);
        else if (r >= 300 && r < 400) st.oct = (int8_t) (r - 301);
        else if (r >= 400 && r < 500) st.len = (int8_t) (r - 400);
        else if (r >= 500 && r < 600) st.cond = (int8_t) (r - 500);
        else if (r >= 600 && r < 800) st.prob = (int8_t) (r - 600);
        else if (r >= 800) st.rat = (int8_t) (r - 800);
        repaint();
    });
}

// ===========================================================================
// VoicingView
void VoicingView::paint (juce::Graphics& g)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const int key = proc.keyRoot(), mode = proc.mode();
    const int pat = proc.editPattern();
    const auto& P = proc.bank.pats[pat];
    th::Chord c;
    bool live = false;
    const int packed = proc.curChordPacked.load();
    if ((proc.isRunning.load() || proc.numSounding.load() > 0) && packed != 0) { c = unpackChord (packed); live = true; }
    else c = P.chord (juce::jlimit (0, P.chordCount() - 1, shownChord < 0 ? 0 : shownChord));

    const int fn = th::chordFunction (c, mode);
    const float outgoing = proc.paramReal ("outgoing");
    g.setColour (colorCol);
    g.setFont (font (11.0f, true));
    g.drawText (live ? "EN COURS" : "ACCORD", 10, 4, 120, 16, juce::Justification::centredLeft);
    g.setColour (col::text);
    g.setFont (font (30.0f, true));
    g.drawFittedText (th::chordName (c, key), 10, 20, 230, 38, juce::Justification::centredLeft, 1, 0.6f);
    g.setColour (functionColour (fn));
    g.setFont (font (16.0f, true));
    g.drawText (th::romanNumeral (c, mode), 246, 22, 160, 20, juce::Justification::centredLeft);
    g.setFont (font (10.5f));
    static const char* fnName[] { "tonique (repos)", "sous-dominante (depart)", "dominante (tension)", "couleur / chromatique" };
    g.drawText (fnName[juce::jlimit (0, 3, fn)], 246, 42, 160, 14, juce::Justification::centredLeft);

    // gamme parente (Russell) et tension (Persichetti)
    g.setColour (col::textDim);
    g.setFont (font (10.5f));
    g.drawFittedText ("Gamme parente : " + th::parentScaleName (c, outgoing, key), 10, 60, 394, 14, juce::Justification::centredLeft, 1);
    const float t = th::chordTension (c);
    const float grav = th::chordGravity (c, keyLydianTonic (mode));
    auto bar = [&g] (int x, int y, const juce::String& name, float v, juce::Colour cc)
    {
        g.setColour (col::textDim); g.drawText (name, x, y, 70, 12, juce::Justification::centredLeft);
        g.setColour (col::bg); g.fillRect (x + 72, y + 2, 110, 8);
        g.setColour (cc); g.fillRect (x + 72, y + 2, (int) (110 * v), 8);
    };
    bar (10, 78, "Tension", t, col::lock);
    bar (206, 78, "Gravite", grav, colorCol);

    // clavier C2 .. C7
    const int lo = 36, hi = 96;
    int whites = 0; for (int n = lo; n <= hi; ++n) if (! juce::MidiMessage::isMidiNoteBlack (n)) ++whites;
    const auto kb = juce::Rectangle<float> (8.0f, 96.0f, (float) getWidth() - 16.0f, (float) getHeight() - 104.0f);
    const float ww = kb.getWidth() / (float) whites;
    bool sounding[128] {};
    bool bassNote[128] {};
    const int ns = proc.numSounding.load();
    for (int i = 0; i < ns && i < kVoices; ++i) { const int n = proc.sounding[i].load(); if (n >= 0 && n < 128) sounding[n] = true; }
    int voic[8]; int nv = 0;
    if (! live || ns == 0)
    {
        nv = proc.previewVoicing (c, voic, 8);
        for (int i = 0; i < nv; ++i) if (voic[i] >= 0 && voic[i] < 128) { sounding[voic[i]] = true; if (i == 0 && proc.paramReal ("bassmode") > 0.5f) bassNote[voic[i]] = true; }
    }
    // notes de la gamme de l'accord (points)
    int scl[64]; auto vo = proc.voiceOptions(); vo.keyRoot = key;
    const int nscl = th::chordScaleNotes (c, vo, outgoing, lo, hi, scl, 64);
    bool inScl[128] {};
    for (int i = 0; i < nscl; ++i) inScl[scl[i]] = true;
    const uint16_t cm = th::chordMask (c);

    int wi = 0;
    for (int n = lo; n <= hi; ++n)
    {
        if (juce::MidiMessage::isMidiNoteBlack (n)) continue;
        auto r = juce::Rectangle<float> (kb.getX() + wi * ww, kb.getY(), ww - 1.0f, kb.getHeight());
        g.setColour (sounding[n] ? (bassNote[n] ? col::accent : colorCol) : juce::Colour (0xffd9dbe2));
        g.fillRect (r);
        if (! sounding[n] && inScl[n])
        {
            const bool ct = (cm >> ((n - key + 120) % 12)) & 1u;
            g.setColour (ct ? colorCol : col::textDim);
            g.fillEllipse (r.getCentreX() - 2.5f, r.getBottom() - 9.0f, 5.0f, 5.0f);
        }
        if (n % 12 == 0) { g.setColour (col::textDim.darker()); g.setFont (font (8.0f)); g.drawText ("C" + juce::String (n / 12 - 2), r.toNearestInt().withTrimmedTop ((int) r.getHeight() - 22), juce::Justification::centredTop); }
        ++wi;
    }
    wi = 0;
    for (int n = lo; n <= hi; ++n)
    {
        if (juce::MidiMessage::isMidiNoteBlack (n))
        {
            auto r = juce::Rectangle<float> (kb.getX() + wi * ww - ww * 0.32f, kb.getY(), ww * 0.64f, kb.getHeight() * 0.6f);
            g.setColour (sounding[n] ? (bassNote[n] ? col::accent : colorCol) : juce::Colour (0xff22242b));
            g.fillRect (r);
            if (! sounding[n] && inScl[n])
            {
                const bool ct = (cm >> ((n - key + 120) % 12)) & 1u;
                g.setColour (ct ? colorCol : col::textDim);
                g.fillEllipse (r.getCentreX() - 2.0f, r.getBottom() - 7.0f, 4.0f, 4.0f);
            }
        }
        else ++wi;
    }
}

// ===========================================================================
// TheoryView
TheoryView::TheoryView (ColorAudioProcessor& p) : proc (p)
{
    for (int i = 0; i < 6; ++i)
    {
        addChildComponent (sugg[i]);
        sugg[i].onClick = [this, i]
        {
            if (i >= (int) suggChords.size()) return;
            const int pat = proc.editPattern();
            proc.pushUndo();
            auto c = suggChords[(size_t) i];
            c.beats = proc.bank.pats[pat].chord (selected).beats;
            proc.insertChord (pat, selected + 1, c);
            selected = juce::jmin (selected + 1, proc.bank.pats[pat].chordCount() - 1);
            proc.auditionChord = selected;
            refresh();
            if (onChanged) onChanged();
        };
    }
}

void TheoryView::resized()
{
    tableArea = { 0, 0, 760, getHeight() };
    circleArea = { 770, 0, getWidth() - 770, getHeight() - 96 };
    suggArea = { 770, getHeight() - 92, getWidth() - 770, 92 };
    const int bw = (suggArea.getWidth() - 20) / 3;
    for (int i = 0; i < 6; ++i) sugg[i].setBounds (suggArea.getX() + 6 + (i % 3) * (bw + 4), suggArea.getY() + 24 + (i / 3) * 32, bw, 28);
}

void TheoryView::refresh()
{
    const auto& P = proc.bank.pats[proc.editPattern()];
    selected = juce::jlimit (0, P.chordCount() - 1, selected);
    suggChords.clear();
    const auto sn = cleanSuggestions (th::suggestNext (P.chord (selected), proc.mode(), (int) proc.paramReal ("style")));
    for (int i = 0; i < 6; ++i)
    {
        sugg[i].setVisible (i < (int) sn.size());
        if (i >= (int) sn.size()) continue;
        suggChords.push_back (sn[(size_t) i].first);
        sugg[i].setButtonText (th::chordName (sn[(size_t) i].first, proc.keyRoot()) + "  " + th::romanNumeral (sn[(size_t) i].first, proc.mode())
                               + "  " + juce::String ((int) std::round (sn[(size_t) i].second * 100.0f)) + "%");
        sugg[i].setColour (juce::TextButton::buttonColourId, functionColour (th::chordFunction (sn[(size_t) i].first, proc.mode())).darker (1.4f));
    }
    repaint();
}

void TheoryView::paint (juce::Graphics& g)
{
    const auto& P = proc.bank.pats[proc.editPattern()];
    const int key = proc.keyRoot(), mode = proc.mode();
    const float outgoing = proc.paramReal ("outgoing");
    const int keyLT = keyLydianTonic (mode);
    const int n = P.chordCount();

    // tableau
    g.setColour (col::panel); g.fillRoundedRectangle (tableArea.toFloat(), 6.0f);
    g.setColour (colorCol); g.setFont (font (12.0f, true));
    g.drawText ("ANALYSE DE LA PROGRESSION  (" + rootNames()[key] + " " + th::scale (mode).name + ")", 10, 4, 600, 18, juce::Justification::centredLeft);
    const int cx[] { 10, 40, 120, 200, 236, 330, 424, 760 };
    const char* hd[] { "#", "Accord", "Degre", "Fn", "Tension", "Gravite", "Gamme parente (Russell) / notes" };
    g.setFont (font (10.0f, true)); g.setColour (col::textDim);
    for (int k = 0; k < 7; ++k) g.drawText (hd[k], cx[k], 24, cx[k + 1] - cx[k], 14, juce::Justification::centredLeft);
    const float rowH = juce::jmin (20.0f, (float) (tableArea.getHeight() - 110) / (float) juce::jmax (1, n));
    int counts[4] {}; float tSum = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const auto c = P.chord (i);
        const int y = 40 + (int) (i * rowH);
        const int fn = th::chordFunction (c, mode);
        counts[juce::jlimit (0, 3, fn)]++;
        const float t = th::chordTension (c), gr = th::chordGravity (c, keyLT);
        tSum += t;
        if (i == selected) { g.setColour (col::panelHi.brighter (0.15f)); g.fillRect (4, y, 752, (int) rowH); }
        g.setFont (font (11.0f)); g.setColour (col::textDim);
        g.drawText (juce::String (i + 1), cx[0], y, 30, (int) rowH, juce::Justification::centredLeft);
        g.setColour (col::text); g.setFont (font (11.5f, true));
        g.drawText (th::chordName (c, key), cx[1], y, 80, (int) rowH, juce::Justification::centredLeft);
        g.setColour (functionColour (fn)); g.setFont (font (11.0f));
        g.drawText (th::romanNumeral (c, mode), cx[2], y, 80, (int) rowH, juce::Justification::centredLeft);
        g.drawText (functionLetter (fn), cx[3], y, 30, (int) rowH, juce::Justification::centredLeft);
        g.setColour (col::bg); g.fillRect (cx[4], y + (int) rowH / 2 - 3, 84, 6); g.fillRect (cx[5], y + (int) rowH / 2 - 3, 84, 6);
        g.setColour (col::lock); g.fillRect (cx[4], y + (int) rowH / 2 - 3, (int) (84 * t), 6);
        g.setColour (colorCol); g.fillRect (cx[5], y + (int) rowH / 2 - 3, (int) (84 * gr), 6);
        // notes de la gamme parente
        const int ps = th::parentScale (c, outgoing), pr = th::parentScaleRoot (c, outgoing);
        juce::String notes;
        for (int d = 0; d < th::scaleSize (ps); ++d) notes << rootNames()[(key + pr + th::scaleDegreePc (ps, d)) % 12] << " ";
        g.setColour (col::textDim); g.setFont (font (10.0f));
        g.drawFittedText (th::parentScaleName (c, outgoing, key) + "  :  " + notes.trim(), cx[6], y, 330, (int) rowH, juce::Justification::centredLeft, 1, 0.8f);
    }
    // synthese
    {
        const int y = tableArea.getBottom() - 64;
        g.setColour (col::line); g.drawHorizontalLine (y - 4, 8.0f, 752.0f);
        g.setFont (font (10.5f)); g.setColour (col::text);
        juce::String s;
        s << "Fonctions : " << counts[0] << " T, " << counts[1] << " S, " << counts[2] << " D, " << counts[3] << " couleur.   "
          << "Tension moyenne : " << juce::String ((int) std::round (100.0f * tSum / (float) juce::jmax (1, n))) << " %.   ";
        int fifths = 0, thirds = 0, seconds = 0;
        for (int i = 0; i < n; ++i)
        {
            const int iv = (P.chord ((i + 1) % n).root - P.chord (i).root + 12) % 12;
            if (iv == 5 || iv == 7) ++fifths; else if (iv == 3 || iv == 4 || iv == 8 || iv == 9) ++thirds; else if (iv == 1 || iv == 2 || iv == 10 || iv == 11) ++seconds;
        }
        s << "Mouvements de fondamentale (Persichetti) : quintes " << fifths << ", tierces " << thirds << ", secondes " << seconds << ".";
        g.drawFittedText (s, juce::Rectangle<int> (10, y, 740, 30), juce::Justification::topLeft, 2);
        g.setColour (col::textDim); g.setFont (font (9.5f));
        g.drawFittedText ("Tension : intervalles de l'accord (secondes mineures, tritons > tierces > quintes). Gravite : distance au centre tonal selon "
                          "l'ordre lydien chromatique (Russell) - ingoing (bas) / outgoing (haut). Clic sur la progression = choisir l'accord analyse.",
                          juce::Rectangle<int> (10, y + 30, 740, 30), juce::Justification::topLeft, 2);
    }

    // cycle des quintes : chemin de la progression
    g.setColour (col::panel); g.fillRoundedRectangle (circleArea.toFloat(), 6.0f);
    g.setColour (colorCol); g.setFont (font (12.0f, true));
    g.drawText ("CYCLE DES QUINTES", circleArea.getX() + 10, 4, 300, 18, juce::Justification::centredLeft);
    const auto cc = circleArea.toFloat().withTrimmedTop (20.0f).reduced (14.0f);
    const float rad = juce::jmin (cc.getWidth(), cc.getHeight()) * 0.42f;
    const auto ctr = cc.getCentre();
    auto posOf = [&] (int pcAbs) { const int idx = (pcAbs * 7) % 12; const float a = juce::MathConstants<float>::twoPi * idx / 12.0f - juce::MathConstants<float>::halfPi;
                                   return juce::Point<float> (ctr.x + rad * std::cos (a), ctr.y + rad * std::sin (a)); };
    for (int pc = 0; pc < 12; ++pc)
    {
        const auto p = posOf (pc);
        const bool inK = th::inScale (mode, (pc - key + 12) % 12);
        g.setColour (inK ? col::panelHi.brighter (0.2f) : col::panelHi);
        g.fillEllipse (p.x - 15.0f, p.y - 15.0f, 30.0f, 30.0f);
        g.setColour (pc == key ? colorCol : (inK ? col::text : col::textDim));
        g.setFont (font (11.0f, pc == key));
        g.drawText (rootNames()[pc], juce::Rectangle<float> (p.x - 15.0f, p.y - 15.0f, 30.0f, 30.0f), juce::Justification::centred);
    }
    juce::Path path;
    for (int i = 0; i <= n; ++i)
    {
        const auto c = P.chord (i % n);
        const auto p = posOf ((key + c.root) % 12);
        const auto q = p + (ctr - p) * 0.22f;
        if (i == 0) path.startNewSubPath (q); else path.lineTo (q);
    }
    g.setColour (colorCol.withAlpha (0.7f));
    g.strokePath (path, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int i = 0; i < n; ++i)
    {
        const auto c = P.chord (i);
        const auto p = posOf ((key + c.root) % 12);
        const auto q = p + (ctr - p) * 0.22f;
        g.setColour (functionColour (th::chordFunction (c, mode)));
        const float r = i == selected ? 7.0f : 4.5f;
        g.fillEllipse (q.x - r, q.y - r, 2 * r, 2 * r);
    }

    g.setColour (col::panel); g.fillRoundedRectangle (suggArea.toFloat(), 6.0f);
    g.setColour (col::textDim); g.setFont (font (10.5f, true));
    g.drawText ("SUITE PROBABLE apres " + th::chordName (P.chord (selected), key) + " (clic = inserer)", suggArea.getX() + 8, suggArea.getY() + 4, 420, 16, juce::Justification::centredLeft);
}

} // namespace color::gui
