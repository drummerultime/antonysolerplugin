#include "Visuals.h"
#include <numeric>

using namespace pal;

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

//==============================================================================
// Outils de dessin
static void hatch (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float spacing = 7.0f)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (c);
    for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += spacing)
        g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);
}

static void chevrons (juce::Graphics& g, juce::Rectangle<float> r, bool reverse, float speed, juce::Colour c)
{
    if (r.getWidth() < 14.0f || r.getHeight() < 6.0f) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (c);
    const float h = juce::jmin (r.getHeight() * 0.5f, 9.0f);
    const float step = juce::jlimit (7.0f, 40.0f, 16.0f / juce::jmax (0.25f, speed));
    const float cy = r.getCentreY();
    for (float x = r.getX() + step * 0.5f; x < r.getRight(); x += step)
    {
        juce::Path p;
        const float d = h * 0.5f * (reverse ? -1.0f : 1.0f);
        p.startNewSubPath (x - d, cy - h * 0.5f);
        p.lineTo (x + d, cy);
        p.lineTo (x - d, cy + h * 0.5f);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
}

static void panelBackground (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour accent = theme::outline)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (accent.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
}

static void title (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Colour c = theme::dim)
{
    g.setColour (c);
    g.setFont (theme::font (11.5f, true));
    g.drawText (t, r.toNearestInt(), juce::Justification::centredLeft);
}

static void hint (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& t, juce::Justification j = juce::Justification::centred)
{
    g.setColour (theme::faint);
    g.setFont (theme::font (11.5f));
    g.drawFittedText (t, r.toNearestInt(), j, 2, 1.0f);
}

static juce::String noteName (int pc)
{
    static const char* names[] = { "Do", "Do#", u8"Ré", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
    return juce::String::fromUTF8 (names[((pc % 12) + 12) % 12]);
}

static juce::String timeText (float s)
{
    if (s >= 60.0f) return juce::String ((int) s / 60) + " min " + juce::String ((int) s % 60) + " s";
    return juce::String (s, s < 10.0f ? 1 : 0) + " s";
}

static juce::Array<int> dirSeq (const PalindromeProcessor& p)
{
    return PhraseEngine::parseDirectionSequence (p.getDirSequenceText());
}

// sens de chaque répétition selon le réglage (0 = normal, 1 = reverse, 2 = les deux, 3 = au hasard, 4 = silence)
static int directionFor (int order, int r, const juce::Array<int>& seq)
{
    switch (order)
    {
        case 7:
        {
            if (seq.isEmpty()) return (r % 2 == 0) ? 1 : 0;
            const int d = seq[r % seq.size()];
            return d == PhraseEngine::DirReverse ? 1 : d == PhraseEngine::DirBoth ? 2 : d == PhraseEngine::DirRest ? 4 : 0;
        }
        case 0: case 1: return (r % 2 == 0) ? 1 : 0;
        case 2:         return (r % 2 == 0) ? 0 : 1;
        case 3:         return 1;
        case 4:         return 0;
        case 5:         return 3;
        case 6:         return 2;
        default:        return 0;
    }
}

static float speedFor (int mode, int r, int reps)
{
    const float pos = reps > 1 ? (float) r / (float) (reps - 1) : 0.0f;
    switch (mode)
    {
        case 1: return 0.5f;
        case 2: return 2.0f;
        case 3: return (r % 2 == 0) ? 1.0f : 0.5f;
        case 4: return std::pow (0.5f, pos);
        case 5: return std::pow (2.0f, pos);
        default: return 1.0f;
    }
}

static void drawRepBlock (juce::Graphics& g, juce::Rectangle<float> b, int dir, float alpha, float bright, float speed, bool glow, bool withChevrons = true)
{
    auto fillDir = [&] (juce::Rectangle<float> r, bool rev)
    {
        const auto c = (rev ? theme::reverse : theme::normal).withMultipliedBrightness (bright);
        juce::ColourGradient gr (c.withAlpha (alpha), r.getX(), r.getY(), c.withAlpha (alpha * 0.55f), r.getX(), r.getBottom(), false);
        g.setGradientFill (gr);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (c.withAlpha (juce::jmin (1.0f, alpha + 0.3f)));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        if (withChevrons)
            chevrons (g, r.reduced (3.0f, 1.0f), rev, speed, theme::bg.withAlpha (0.45f * juce::jmin (1.0f, alpha * 1.4f)));
    };

    if (glow)
    {
        const auto c = dir == 1 ? theme::reverse : theme::normal;
        for (int i = 3; i > 0; --i)
        {
            g.setColour (c.withAlpha (0.07f));
            g.fillRoundedRectangle (b.expanded ((float) i * 2.5f), 6.0f);
        }
    }

    if (dir == 4)
    {
        // silence : contour pointillé
        juce::Path p; p.addRoundedRectangle (b, 4.0f);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (theme::faint.withAlpha (juce::jmin (1.0f, alpha + 0.2f)));
        g.fillPath (dashed);
    }
    else if (dir == 2)  { fillDir (b.withHeight (b.getHeight() * 0.5f), true); fillDir (b.withTrimmedTop (b.getHeight() * 0.5f), false); }
    else if (dir == 3)
    {
        fillDir (b, false);
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path tri; tri.addTriangle (b.getTopLeft(), b.getTopRight(), b.getBottomLeft());
        g.reduceClipRegion (tri);
        fillDir (b, true);
    }
    else fillDir (b, dir == 1);
}

//==============================================================================
// ACTIVITÉ
void ActivityView::paint (juce::Graphics& g)
{
    auto& e = processor.engine;
    auto r = getLocalBounds().toFloat();
    panelBackground (g, r);
    pulse += 0.12f;

    const int mode = (int) pv (ParamIDs::captureMode);
    const int capSt = e.captureState.load();
    const int flow = e.flowState.load();
    const bool hold = pv (ParamIDs::hold) > 0.5f;

    // --- état -----------------------------------------------------------------------
    juce::Colour dot = theme::faint;
    juce::String st;
    const int target = e.captureTarget.load();
    if (hold)                                  { dot = theme::hold; st = u8 ("HOLD — la phrase en cours tourne en boucle"); }
    else if (capSt == PhraseEngine::CapRecording)
    {
        dot = theme::record;
        st = target >= 0 ? (e.captureIsReplace.load() ? u8 ("Recapture de l'emplacement ") : u8 ("Capture → emplacement ")) + juce::String (target + 1)
                         : u8 ("Capture en cours");
        st << "  " << juce::String (e.captureElapsed.load(), 1) << " / " << juce::String (e.captureLength.load(), 1) << " s";
    }
    else if (capSt == PhraseEngine::CapArmed)  { dot = theme::record.withAlpha (0.5f + 0.5f * std::sin (pulse)); st = u8 ("Prêt : joue, la capture démarre au seuil"); }
    else if (flow == PhraseEngine::FlowSilence){ dot = theme::silence; st = u8 ("Silence — étape suivante dans ") + timeText (e.flowRemaining.load()); }
    else if (flow == PhraseEngine::FlowPlaying)
    {
        dot = theme::normal;
        const int bs = e.bankPlayingSlot.load();
        st = (mode == PhraseEngine::Bank && bs >= 0) ? u8 ("Banque — capture ") + juce::String (bs + 1) : u8 ("Répétitions en cours");
    }
    else if (mode == PhraseEngine::Manual)     st = u8 ("Prêt — clique sur Capturer");
    else                                        st = u8 ("En attente de signal");

    auto header = r.reduced (14.0f, 0).removeFromTop (34.0f);
    title (g, header.removeFromLeft (80.0f), u8 ("ACTIVITÉ"));
    {
        g.setFont (theme::font (13.5f, true));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), st) + 34.0f;
        auto chip = header.removeFromLeft (tw).withSizeKeepingCentre (tw, 24.0f);
        g.setColour (dot.withAlpha (0.14f));
        g.fillRoundedRectangle (chip, 12.0f);
        g.setColour (dot);
        g.fillEllipse (chip.getX() + 10.0f, chip.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (theme::text);
        g.drawText (st, chip.withTrimmedLeft (24.0f).toNearestInt(), juce::Justification::centredLeft);
    }
    {
        auto lg = header.removeFromRight (190.0f);
        auto item = [&] (juce::Rectangle<float> a, bool rev, const juce::String& t)
        {
            auto sw = a.removeFromLeft (26.0f).withSizeKeepingCentre (24.0f, 12.0f);
            drawRepBlock (g, sw, rev ? 1 : 0, 0.9f, 1.0f, 1.0f, false);
            g.setColour (theme::dim);
            g.setFont (theme::font (11.5f));
            g.drawText (t, a.withTrimmedLeft (6.0f).toNearestInt(), juce::Justification::centredLeft);
        };
        item (lg.removeFromLeft (95.0f), true, "reverse");
        item (lg, false, "normal");
    }

    // --- géométrie --------------------------------------------------------------------
    auto body = r.reduced (14.0f, 0).withTrimmedTop (36.0f).withTrimmedBottom (8.0f);
    auto gutter = body.removeFromLeft (64.0f);
    auto axis = body.removeFromBottom (16.0f);
    auto capLane = body.removeFromTop (22.0f);
    body.removeFromTop (6.0f);
    auto panArea = body;

    // échelle de temps qui s'adapte à ce qui est programmé
    float maxEnd = 0.0f;
    for (auto& v : e.passViews)
        if (v.active.load()) maxEnd = juce::jmax (maxEnd, v.startIn.load() + v.length.load());
    if (capSt == PhraseEngine::CapRecording) maxEnd = juce::jmax (maxEnd, e.captureLength.load() - e.captureElapsed.load());
    if (flow == PhraseEngine::FlowSilence)  maxEnd = juce::jmax (maxEnd, e.flowRemaining.load());
    const float targetRange = juce::jlimit (20.0f, 180.0f, maxEnd * 1.12f + 3.0f);
    displayRange += (targetRange - displayRange) * 0.08f;

    const float past = displayRange * 0.06f;
    const float x0 = panArea.getX(), x1 = panArea.getRight();
    auto xFor = [&] (float t) { return x0 + (x1 - x0) * (t + past) / (displayRange + past); };
    const float nowX = xFor (0.0f);

    g.setColour (theme::dim);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("CAPTURE", gutter.withY (capLane.getY()).withHeight (capLane.getHeight()).toNearestInt(), juce::Justification::centredLeft);
    g.setFont (theme::font (11.0f));
    g.setColour (theme::faint);
    g.drawText (u8 ("◀ G"), gutter.withY (panArea.getY()).withHeight (16.0f).toNearestInt(), juce::Justification::centredLeft);
    g.drawText (u8 ("centre"), gutter.withY (panArea.getCentreY() - 8.0f).withHeight (16.0f).toNearestInt(), juce::Justification::centredLeft);
    g.drawText (u8 ("D ▶"), gutter.withY (panArea.getBottom() - 16.0f).withHeight (16.0f).toNearestInt(), juce::Justification::centredLeft);

    // fond des pistes
    g.setColour (theme::bg.withAlpha (0.6f));
    g.fillRoundedRectangle (capLane, 5.0f);
    g.fillRoundedRectangle (panArea, 6.0f);
    {
        juce::Path centre;
        centre.startNewSubPath (x0, panArea.getCentreY());
        centre.lineTo (x1, panArea.getCentreY());
        const float dashes[] = { 3.0f, 5.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (centre, centre, dashes, 2);
        g.setColour (theme::outline);
        g.fillPath (centre);
    }

    // graduations
    const float steps[] = { 1, 2, 5, 10, 15, 30, 60 };
    float step = 60;
    for (auto s : steps) if (displayRange / s <= 9.0f) { step = s; break; }
    g.setFont (theme::font (10.5f));
    for (float t = step; t <= displayRange; t += step)
    {
        const float x = xFor (t);
        g.setColour (theme::outline.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, panArea.getY() + 2.0f, panArea.getBottom() - 2.0f);
        g.setColour (theme::faint);
        g.drawText ("+" + timeText (t), juce::Rectangle<float> (x - 30.0f, axis.getY(), 60.0f, axis.getHeight()).toNearestInt(), juce::Justification::centred);
    }

    // --- capture / silence -----------------------------------------------------------
    if (capSt == PhraseEngine::CapRecording)
    {
        const float el = e.captureElapsed.load(), len = e.captureLength.load();
        auto full = juce::Rectangle<float> (xFor (-el), capLane.getY() + 2.0f, xFor (len - el) - xFor (-el), capLane.getHeight() - 4.0f);
        g.setColour (theme::record.withAlpha (0.18f));
        g.fillRoundedRectangle (full, 4.0f);
        g.setColour (theme::record.withAlpha (0.6f));
        g.drawRoundedRectangle (full, 4.0f, 1.0f);
        g.setColour (theme::record.withAlpha (0.9f));
        g.fillRoundedRectangle (full.withRight (nowX), 4.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (11.5f, true));
        g.drawText (u8 ("● REC"), full.reduced (6.0f, 0).toNearestInt(), juce::Justification::centredLeft);
    }
    else if (capSt == PhraseEngine::CapArmed)
    {
        auto b = juce::Rectangle<float> (nowX, capLane.getY() + 2.0f, 150.0f, capLane.getHeight() - 4.0f);
        g.setColour (theme::record.withAlpha (0.35f + 0.35f * std::sin (pulse)));
        g.drawRoundedRectangle (b, 4.0f, 1.5f);
        g.setColour (theme::record);
        g.setFont (theme::font (11.5f));
        g.drawText (u8 ("attend le seuil…"), b.reduced (8.0f, 0).toNearestInt(), juce::Justification::centredLeft);
    }
    else if (flow == PhraseEngine::FlowSilence && ! hold)
    {
        const float rem = e.flowRemaining.load();
        auto b = juce::Rectangle<float> (nowX, capLane.getY() + 2.0f, juce::jmax (4.0f, xFor (rem) - nowX), capLane.getHeight() - 4.0f);
        g.setColour (theme::silence.withAlpha (0.16f));
        g.fillRoundedRectangle (b, 4.0f);
        hatch (g, b, theme::silence.withAlpha (0.35f));
        g.setColour (theme::silence);
        g.drawRoundedRectangle (b, 4.0f, 1.0f);
        g.setFont (theme::font (11.5f, true));
        g.drawText ("silence " + timeText (rem), b.reduced (8.0f, 0).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (theme::record);
        g.fillEllipse (b.getRight() - 4.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
    }
    else if (hold)
    {
        g.setColour (theme::hold);
        g.setFont (theme::font (11.5f, true));
        g.drawText (u8 ("∞ HOLD : pas de nouvelle capture"), capLane.withTrimmedLeft (nowX - x0 + 8.0f).toNearestInt(), juce::Justification::centredLeft);
    }

    // --- relectures -------------------------------------------------------------------
    const float maxH = juce::jmin (34.0f, panArea.getHeight() * 0.42f);
    for (int i = PhraseEngine::numViews - 1; i >= 0; --i)
    {
        const auto& v = e.passViews[(size_t) i];
        if (! v.active.load()) continue;

        const float start = v.startIn.load(), len = v.length.load();
        if (start > displayRange) continue;
        const float bx0 = juce::jmax (x0, xFor (start)), bx1 = juce::jmin (x1, xFor (start + len));
        if (bx1 - bx0 < 2.0f) continue;

        const float gain = juce::Decibels::decibelsToGain (v.levelDb.load());
        const float h = maxH * juce::jlimit (0.28f, 1.0f, std::sqrt (gain));
        const float pan = v.pan.load();
        const float cy = panArea.getCentreY() + pan * (panArea.getHeight() * 0.5f - maxH * 0.5f - 3.0f);
        const auto b = juce::Rectangle<float> (bx0, cy - h * 0.5f, bx1 - bx0, h);
        const bool playing = start <= 0.0f;
        const bool rev = v.reverse.load() != 0;

        drawRepBlock (g, b, rev ? 1 : 0, playing ? 0.95f : 0.42f, 1.0f, v.speed.load(), playing);

        if (b.getWidth() > 46.0f)
        {
            juce::String label;
            if (v.bankSlot.load() >= 0) label << "C" << (v.bankSlot.load() + 1) << "  ";
            if (v.repCount.load() > 1) label << v.repIndex.load() << "/" << v.repCount.load();
            const int tr = v.transpose.load();
            if (tr != 0) label << "  " << (tr > 0 ? "+" : "") << tr;
            const float sp = v.speed.load();
            if (std::abs (sp - 1.0f) > 0.01f) label << u8 ("  ×") << juce::String (sp, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            g.setFont (theme::font (11.0f, true));
            const float lw = juce::jmin (b.getWidth() - 6.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label) + 12.0f);
            auto pill = juce::Rectangle<float> (b.getX() + 3.0f, b.getCentreY() - 8.0f, lw, 16.0f);
            g.setColour (theme::bg.withAlpha (0.75f));
            g.fillRoundedRectangle (pill, 5.0f);
            g.setColour (theme::text);
            g.drawText (label, pill.reduced (6.0f, 0).toNearestInt(), juce::Justification::centredLeft, true);
        }
    }

    // maintenant
    g.setColour (theme::text.withAlpha (0.85f));
    g.drawLine (nowX, capLane.getY() - 2.0f, nowX, panArea.getBottom(), 1.5f);
    g.setFont (theme::font (10.5f, true));
    g.drawText ("maintenant", juce::Rectangle<float> (nowX - 40.0f, axis.getY(), 80.0f, axis.getHeight()).toNearestInt(), juce::Justification::centred);

    if (e.pendingCount.load() == 0 && capSt == PhraseEngine::CapIdle && flow != PhraseEngine::FlowSilence)
        hint (g, panArea, u8 ("Les relectures apparaîtront ici : position verticale = placement stéréo, hauteur = niveau de la répétition."));
}

//==============================================================================
// PHRASE
void PhraseView::refresh()
{
    auto& e = processor.engine;
    const int slot = e.displaySlot.load();
    progress = e.displayProgress.load();
    if (slot != shownSlot)
    {
        if (slot >= 0) hasInfo = e.getSlotInfo (slot, info) && ! info.peaks.empty();
        shownSlot = slot;
    }
    repaint();
}

void PhraseView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    panelBackground (g, r);
    auto inner = r.reduced (14.0f, 0);
    auto header = inner.removeFromTop (30.0f);
    title (g, header.removeFromLeft (150.0f), u8 ("PHRASE REJOUÉE"));

    if (! hasInfo)
    {
        hint (g, inner.withTrimmedBottom (8.0f), u8 ("La phrase découpée en notes s'affichera ici pendant sa relecture : ordre des notes, notes inversées, muettes, transpositions."));
        return;
    }

    const bool playing = shownSlot >= 0;
    const auto c = info.reverse ? theme::reverse : theme::normal;

    g.setFont (theme::font (12.5f, true));
    g.setColour (c);
    g.drawText ((info.reverse ? u8 ("◀ REVERSE") : u8 ("NORMAL ▶")) + u8 ("  ·  ") + juce::String (info.numNotes) + " notes"
                    + (playing ? juce::String() : u8 ("  ·  dernière relecture")),
                header.toNearestInt(), juce::Justification::centredLeft);

    auto wave = inner.withTrimmedBottom (10.0f).withTrimmedTop (16.0f);
    const float mid = wave.getCentreY(), hh = wave.getHeight() * 0.5f;

    // cellules de notes
    const int n = (int) info.noteStarts.size();
    for (int k = 0; k < n; ++k)
    {
        const float xa = wave.getX() + wave.getWidth() * info.noteStarts[(size_t) k];
        const float xb = k + 1 < n ? wave.getX() + wave.getWidth() * info.noteStarts[(size_t) k + 1] : wave.getRight();
        auto cell = juce::Rectangle<float> (xa, wave.getY(), xb - xa, wave.getHeight());
        const bool muted = info.sourceIndex[(size_t) k] < 0;
        const bool revNote = k < (int) info.noteReversed.size() && (info.noteReversed[(size_t) k] != 0);
        const auto cc = revNote ? theme::reverse : theme::normal;

        g.setColour ((k % 2 == 0 ? theme::bg : theme::panel2).withAlpha (0.55f));
        g.fillRect (cell);
        if (muted) hatch (g, cell, theme::faint.withAlpha (0.4f), 6.0f);

        if (cell.getWidth() > 18.0f)
        {
            auto pill = juce::Rectangle<float> (xa + 3.0f, wave.getY() - 15.0f, juce::jmin (cell.getWidth() - 6.0f, 64.0f), 14.0f);
            juce::String t = muted ? juce::String ("—") : juce::String (info.sourceIndex[(size_t) k] + 1);
            if (! muted && revNote != info.reverse) t = u8 ("⇄ ") + t;
            const float sh = info.shifts[(size_t) k];
            g.setColour ((muted ? theme::faint : cc).withAlpha (0.25f));
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (muted ? theme::faint : theme::text);
            g.setFont (theme::font (10.5f, true));
            g.drawText (t, pill.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft);
            if (! muted && std::abs (sh) > 0.05f)
            {
                g.setColour (sh > 0 ? theme::tonal : theme::reverse);
                const bool whole = std::abs (sh - std::round (sh)) < 0.05f;
                g.drawText ((sh > 0 ? "+" : "") + (whole ? juce::String (juce::roundToInt (sh)) : juce::String (sh, 1)),
                            pill.toNearestInt().withTrimmedRight (4), juce::Justification::centredRight);
            }
        }
    }

    // forme d'onde
    const int cols = (int) info.peaks.size();
    juce::Path p;
    p.startNewSubPath (wave.getX(), mid);
    for (int i = 0; i < cols; ++i)
        p.lineTo (wave.getX() + wave.getWidth() * (float) i / (float) (cols - 1), mid - info.peaks[(size_t) i] * hh * 0.95f);
    for (int i = cols - 1; i >= 0; --i)
        p.lineTo (wave.getX() + wave.getWidth() * (float) i / (float) (cols - 1), mid + info.peaks[(size_t) i] * hh * 0.95f);
    p.closeSubPath();
    juce::ColourGradient gr (c.withAlpha (playing ? 0.85f : 0.35f), 0, wave.getY(), c.withAlpha (playing ? 0.35f : 0.15f), 0, mid, true);
    g.setGradientFill (gr);
    g.fillPath (p);

    if (playing)
    {
        const float x = wave.getX() + wave.getWidth() * juce::jlimit (0.0f, 1.0f, progress);
        g.setColour (theme::text.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (wave.getX(), wave.getY(), x - wave.getX(), wave.getHeight()));
        g.setColour (theme::text);
        g.drawLine (x, wave.getY() - 2.0f, x, wave.getBottom() + 2.0f, 1.6f);
    }
}

//==============================================================================
// Cadenas (emplacements verrouillés)
void pal::drawLock (juce::Graphics& g, juce::Rectangle<float> r, bool locked, juce::Colour c)
{
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    auto b = r.withSizeKeepingCentre (s, s);
    auto body = b.withTrimmedTop (s * 0.45f);
    g.setColour (c);
    g.fillRoundedRectangle (body, s * 0.12f);
    juce::Path arc;
    const float aw = s * 0.56f, ah = s * 0.55f;
    const float ax = b.getCentreX() - aw * 0.5f, ay = b.getY() + s * 0.02f;
    arc.startNewSubPath (ax, body.getY());
    arc.lineTo (ax, ay + aw * 0.5f);
    arc.addCentredArc (b.getCentreX(), ay + aw * 0.5f, aw * 0.5f, aw * 0.5f, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
    if (locked) arc.lineTo (ax + aw, body.getY());
    else        arc.lineTo (ax + aw, ay + ah * 0.55f);
    g.strokePath (arc, juce::PathStrokeType (s * 0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (theme::bg);
    g.fillEllipse (body.getCentreX() - s * 0.07f, body.getCentreY() - s * 0.09f, s * 0.14f, s * 0.14f);
}

//==============================================================================
// BANQUE
BankPanel::BankPanel (PalindromeProcessor& p)
    : ParamView (p), orderControl (p.apvts, ParamIDs::bankOrder, { u8 ("Séquence"), "Hasard" }, theme::banque)
{
    sizeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 34, 24);
    sizeSlider.getProperties().set ("accent", (juce::int64) theme::banque.getARGB());
    sizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, ParamIDs::bankSize, sizeSlider);
    sizeSlider.setTooltip ("Nombre de captures dans la banque (2 à 8)");
    addAndMakeVisible (sizeSlider);

    orderControl.setTooltip (u8 ("Une fois la banque pleine : rejouer les captures dans l'ordre de la séquence, ou au hasard (jamais deux fois la même de suite)."));
    addAndMakeVisible (orderControl);

    sequenceEditor.setText (p.getSequenceText(), false);
    sequenceEditor.setTextToShowWhenEmpty ("ex. 3 2 4 1", theme::faint);
    sequenceEditor.setInputRestrictions (128, "0123456789 ,-");
    sequenceEditor.setFont (theme::font (14.0f, true));
    sequenceEditor.setTooltip (u8 ("Ordre de lecture des captures, ex. « 3 2 4 1 ». Les numéros absents ou vides sont ignorés."));
    sequenceEditor.onTextChange = [this] { processor.setSequenceText (sequenceEditor.getText()); };
    addAndMakeVisible (sequenceEditor);

    peakVersion.fill (-1);
    setTooltip (u8 ("Clic : choisir l'emplacement à recapturer.  Cadenas : protéger une capture (ni recapturée, ni vidée).  "
                    "Glisser un emplacement : l'exporter en WAV vers une piste.  Déposer un fichier audio sur un emplacement : le charger."));
}

juce::Rectangle<float> BankPanel::lockFor (int k) const
{
    auto c = cardFor (k);
    return { c.getRight() - 26.0f, c.getY() + 2.0f, 13.0f, 13.0f };
}

void BankPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 8);
    r.removeFromTop (24);
    sideArea = r.removeFromRight (230);
    r.removeFromRight (10);
    cardsArea = r;

    auto s = sideArea;
    auto row1 = s.removeFromTop (24);
    sizeSlider.setBounds (row1.removeFromRight (104));
    s.removeFromTop (4);
    orderControl.setBounds (s.removeFromTop (26));
    s.removeFromTop (4);
    sequenceEditor.setBounds (s.removeFromTop (26));
}

void BankPanel::refresh()
{
    for (int k = 0; k < PhraseEngine::maxBankSize; ++k)
    {
        const int v = processor.engine.bankVersion (k);
        if (v == peakVersion[(size_t) k]) continue;
        peakVersion[(size_t) k] = v;
        auto& pk = peaks[(size_t) k];
        pk.clear();
        juce::AudioBuffer<float> b;
        if (processor.engine.copyBankAudio (k, b))
        {
            const int cols = 48, n = b.getNumSamples();
            float mx = 1.0e-6f;
            for (int c = 0; c < cols; ++c)
            {
                const int s0 = c * n / cols, s1 = juce::jmax (s0 + 1, (c + 1) * n / cols);
                float m = 0.0f;
                for (int ch = 0; ch < b.getNumChannels(); ++ch) m = juce::jmax (m, b.getMagnitude (ch, s0, s1 - s0));
                pk.push_back (m);
                mx = juce::jmax (mx, m);
            }
            for (auto& v2 : pk) v2 /= mx;
        }
    }
    sequenceEditor.setEnabled (pv (ParamIDs::bankOrder) < 0.5f);
    repaint();
}

juce::Rectangle<float> BankPanel::cardFor (int k) const
{
    const float w = (float) cardsArea.getWidth() / (float) PhraseEngine::maxBankSize;
    return { cardsArea.getX() + w * (float) k + 3.0f, (float) cardsArea.getY(), w - 6.0f, (float) cardsArea.getHeight() };
}

int BankPanel::slotAt (juce::Point<float> p) const
{
    for (int k = 0; k < PhraseEngine::maxBankSize; ++k)
        if (cardFor (k).contains (p)) return k;
    return -1;
}

void BankPanel::paint (juce::Graphics& g)
{
    auto& e = processor.engine;
    auto r = getLocalBounds().toFloat();
    panelBackground (g, r, theme::banque);

    const int n = juce::roundToInt (pv (ParamIDs::bankSize));
    const int target = (int) pv (ParamIDs::targetSlot);
    const int playing = e.bankPlayingSlot.load();
    const int capturing = e.captureState.load() == PhraseEngine::CapRecording ? e.captureTarget.load() : -1;

    auto header = r.reduced (14.0f, 0).removeFromTop (28.0f);
    title (g, header.removeFromLeft (70.0f), "BANQUE", theme::banque);

    // séquence en pastilles
    const auto seq = PhraseEngine::parseSequence (processor.getSequenceText());
    const int seqPos = e.sequencePosView.load();
    if (pv (ParamIDs::bankOrder) < 0.5f && seq.size() > 0)
    {
        float x = header.getX();
        g.setFont (theme::font (11.5f, true));
        g.setColour (theme::faint);
        g.drawText (u8 ("séquence"), juce::Rectangle<float> (x, header.getY(), 60.0f, header.getHeight()).toNearestInt(), juce::Justification::centredLeft);
        x += 62.0f;
        for (int i = 0; i < seq.size() && x < header.getRight() - 30.0f; ++i)
        {
            auto chip = juce::Rectangle<float> (x, header.getCentreY() - 8.0f, 20.0f, 16.0f);
            const bool cur = i == seqPos;
            g.setColour (cur ? theme::banque : theme::panel2);
            g.fillRoundedRectangle (chip, 4.0f);
            g.setColour (cur ? theme::bg : theme::dim);
            g.drawText (juce::String (seq[i]), chip.toNearestInt(), juce::Justification::centred);
            x += 24.0f;
        }
    }
    else
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (11.5f));
        g.drawText (u8 ("ordre au hasard"), header.toNearestInt(), juce::Justification::centredLeft);
    }

    g.setColour (theme::dim);
    g.setFont (theme::font (11.5f));
    g.drawText ("Captures", sideArea.withHeight (24).toNearestInt(), juce::Justification::centredLeft);

    for (int k = 0; k < PhraseEngine::maxBankSize; ++k)
    {
        auto c = cardFor (k);
        const bool active = k < n;
        const float len = e.bankLengthView[(size_t) k].load();
        const bool filled = len > 0.0f;
        const float a = active ? 1.0f : 0.22f;

        g.setColour ((filled ? theme::banque.withAlpha (0.14f) : theme::bg.withAlpha (0.6f)).withMultipliedAlpha (a));
        g.fillRoundedRectangle (c, 6.0f);

        // mini forme d'onde
        auto wave = c.reduced (5.0f, 0).withTrimmedTop (16.0f).withTrimmedBottom (12.0f);
        const auto& pk = peaks[(size_t) k];
        if (! pk.empty())
        {
            g.setColour ((k == playing ? theme::normal : theme::banque).withAlpha (0.8f * a));
            const float bw = wave.getWidth() / (float) pk.size();
            for (size_t i = 0; i < pk.size(); ++i)
            {
                const float hh = juce::jmax (1.0f, pk[i] * wave.getHeight() * 0.5f);
                g.fillRect (wave.getX() + bw * (float) i, wave.getCentreY() - hh, juce::jmax (1.0f, bw - 0.6f), hh * 2.0f);
            }
        }
        else if (active)
        {
            g.setColour (theme::faint);
            g.setFont (theme::font (10.5f));
            g.drawText ("vide", wave.toNearestInt(), juce::Justification::centred);
        }

        if (k == capturing)
        {
            const float p = e.captureLength.load() > 0.0f ? e.captureElapsed.load() / e.captureLength.load() : 0.0f;
            g.setColour (theme::record.withAlpha (0.45f));
            g.fillRoundedRectangle (c.withWidth (c.getWidth() * juce::jlimit (0.0f, 1.0f, p)), 6.0f);
        }

        const bool isPlaying = k == playing && filled;
        g.setColour ((isPlaying ? theme::normal : (k == dropSlot ? theme::hold : theme::outline)).withMultipliedAlpha (a));
        g.drawRoundedRectangle (c.reduced (0.5f), 6.0f, isPlaying || k == dropSlot ? 2.0f : 1.0f);

        g.setColour (theme::text.withMultipliedAlpha (a));
        g.setFont (theme::font (12.0f, true));
        g.drawText (juce::String (k + 1), c.withHeight (16.0f).toNearestInt().withTrimmedLeft (6), juce::Justification::centredLeft);
        if (filled)
        {
            g.setColour (theme::dim.withMultipliedAlpha (a));
            g.setFont (theme::font (10.0f));
            g.drawText (juce::String (len, 1) + " s", c.withTrimmedTop (c.getHeight() - 13.0f).toNearestInt(), juce::Justification::centred);
        }
        if (k == target && active)
        {
            g.setColour (theme::record);
            g.fillEllipse (c.getRight() - 11.0f, c.getY() + 5.0f, 6.0f, 6.0f);
        }

        const bool locked = processor.isSlotLocked (k);
        if (active && (filled || locked))
        {
            if (locked)
            {
                g.setColour (theme::hold.withAlpha (0.9f));
                g.drawRoundedRectangle (c.reduced (1.5f), 6.0f, 1.0f);
            }
            pal::drawLock (g, lockFor (k).reduced (1.0f), locked, locked ? theme::hold : theme::faint.withAlpha (0.7f));
        }
    }
}

void BankPanel::mouseDown (const juce::MouseEvent& ev)
{
    dragging = false;
    const int k = slotAt (ev.position);
    if (k < 0) return;
    if (lockFor (k).expanded (3.0f).contains (ev.position))
    {
        processor.setSlotLocked (k, ! processor.isSlotLocked (k));
        repaint();
        return;
    }
    if (auto* param = processor.apvts.getParameter (ParamIDs::targetSlot))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) k));
        param->endChangeGesture();
    }
}

void BankPanel::mouseDrag (const juce::MouseEvent& ev)
{
    if (dragging || ev.getDistanceFromDragStart() < 8) return;
    const int k = slotAt (ev.mouseDownPosition);
    if (k < 0 || processor.engine.bankLengthView[(size_t) k].load() <= 0.0f) return;
    dragging = true;
    const auto file = processor.exportSlotToFile (k);
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

bool BankPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;caf;m4a")) return true;
    return false;
}

void BankPanel::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int k = slotAt ({ (float) x, (float) y });
    dropSlot = -1;
    if (k >= 0 && files.size() > 0) processor.importFileToSlot (k, juce::File (files[0]));
    repaint();
}

//==============================================================================
// PLAN DU CYCLE
void CyclePlanView::paint (juce::Graphics& g)
{
    auto& e = processor.engine;
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);

    const int mode = (int) pv (ParamIDs::captureMode);
    const bool flowMode = mode == PhraseEngine::Cycle || mode == PhraseEngine::Bank;
    const bool sync = pv (ParamIDs::syncMode) > 0.5f;
    const float beat = 60.0f / (float) juce::jmax (20.0, processor.hostBpm.load());
    auto sec = [&] (const char* idS, const char* idB) { return sync ? pv (idB) * beat : pv (idS); };

    const float L = sec (ParamIDs::phraseLen, ParamIDs::phraseBeats);
    const float D = sec (ParamIDs::delay, ParamIDs::delayBeats);
    const float gap = sec (ParamIDs::repeatGap, ParamIDs::gapBeats);
    float sMin = sec (ParamIDs::silenceMin, ParamIDs::silMinBeats), sMax = sec (ParamIDs::silenceMax, ParamIDs::silMaxBeats);
    if (sMax < sMin) std::swap (sMin, sMax);
    const bool fromEnd = pv (ParamIDs::delayFrom) > 0.5f;
    const int reps = juce::roundToInt (pv (ParamIDs::repetitions));
    const int order = (int) pv (ParamIDs::order);
    const int speedMode = (int) pv (ParamIDs::speedMode);
    const float decay = pv (ParamIDs::decay), damping = pv (ParamIDs::damping) * 0.01f, tone = pv (ParamIDs::tone);
    const float attack = pv (ParamIDs::attack);
    const bool attackPerNote = pv (ParamIDs::attackScope) < 0.5f;
    const int transpose = juce::roundToInt (pv (ParamIDs::transpose)), drift = juce::roundToInt (pv (ParamIDs::drift));

    const float firstRep = fromEnd ? L + D : juce::jmax (D, L + 0.5f);
    const int shown = juce::jmin (reps, 12);
    std::vector<std::pair<float, float>> repSeg;
    float t = firstRep;
    for (int i = 0; i < shown; ++i)
    {
        const float len = L / speedFor (speedMode, i, reps);
        repSeg.push_back ({ t, len });
        t += len + gap;
    }
    const float repsEnd = t - gap;
    const float total = flowMode ? repsEnd + sMax + L * 0.35f : repsEnd + L * 0.2f;

    auto area = r.reduced (12.0f, 8.0f);
    auto footer = area.removeFromBottom (18.0f);
    auto labels = area.removeFromTop (16.0f);
    auto lane = area.withTrimmedTop (2.0f);
    auto xFor = [&] (float tt) { return lane.getX() + lane.getWidth() * tt / total; };
    const float base = lane.getBottom();
    const float laneH = lane.getHeight();

    // phase en cours
    const int capSt = e.captureState.load();
    const int flow = e.flowState.load();
    int playingRep = -1; float playingProg = 0.0f, firstStart = 1.0e9f;
    for (auto& v : e.passViews)
    {
        if (! v.active.load()) continue;
        const float s = v.startIn.load();
        if (s <= 0.0f && (playingRep < 0 || v.repIndex.load() - 1 < playingRep))
        {
            playingRep = v.repIndex.load() - 1;
            playingProg = -s / juce::jmax (0.01f, v.length.load());
        }
        firstStart = juce::jmin (firstStart, s);
    }
    const bool inDelay = playingRep < 0 && e.pendingCount.load() > 0 && firstStart > 0.0f;

    // capture
    auto cap = juce::Rectangle<float> (xFor (0), base - laneH * 0.8f, xFor (L) - xFor (0), laneH * 0.8f);
    g.setColour (theme::record.withAlpha (0.25f));
    g.fillRoundedRectangle (cap, 4.0f);
    if (capSt == PhraseEngine::CapRecording)
    {
        const float p = e.captureLength.load() > 0 ? e.captureElapsed.load() / e.captureLength.load() : 0.0f;
        g.setColour (theme::record.withAlpha (0.85f));
        g.fillRoundedRectangle (cap.withWidth (cap.getWidth() * juce::jlimit (0.0f, 1.0f, p)), 4.0f);
    }
    g.setColour (theme::record);
    g.drawRoundedRectangle (cap, 4.0f, capSt == PhraseEngine::CapRecording ? 2.0f : 1.0f);
    g.setFont (theme::font (cap.getWidth() > 70.0f ? 11.0f : 9.5f, true));
    g.drawText (cap.getWidth() > 54.0f ? "CAPTURE" : "REC", cap.reduced (2.0f, 0).toNearestInt(), juce::Justification::centred, false);
    g.setColour (theme::dim);
    g.setFont (theme::font (10.5f));
    g.drawText (timeText (L), juce::Rectangle<float> (cap.getX(), labels.getY(), cap.getWidth(), labels.getHeight()).toNearestInt(), juce::Justification::centred);

    // délai
    if (firstRep - L > 0.05f)
    {
        const float xa = xFor (L) + 3.0f, xb = xFor (firstRep) - 3.0f, y = base - laneH * 0.4f;
        juce::Path pth; pth.startNewSubPath (xa, y); pth.lineTo (xb, y);
        const float dash[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (pth, pth, dash, 2);
        g.setColour (inDelay ? theme::text : theme::faint);
        g.fillPath (pth);
        juce::Path arrow; arrow.addTriangle (xb, y, xb - 6.0f, y - 4.0f, xb - 6.0f, y + 4.0f);
        g.fillPath (arrow);
        g.setColour (theme::dim);
        g.drawText (u8 ("délai ") + timeText (fromEnd ? D : firstRep), juce::Rectangle<float> (xa, labels.getY(), xb - xa, labels.getHeight()).toNearestInt(), juce::Justification::centred);
    }

    // répétitions
    const bool scaleOn = pv (ParamIDs::scale) > 0.5f;
    for (int i = 0; i < (int) repSeg.size(); ++i)
    {
        const float sp = speedFor (speedMode, i, reps);
        const float gainLin = juce::Decibels::decibelsToGain (decay * (float) i);
        const float h = laneH * juce::jlimit (0.22f, 1.0f, std::sqrt (gainLin));
        const float cut = juce::jmax (200.0f, tone * std::pow (1.0f - 0.5f * damping, (float) i));
        const float bright = 0.45f + 0.55f * (std::log (cut / 200.0f) / std::log (100.0f));
        auto b = juce::Rectangle<float> (xFor (repSeg[(size_t) i].first), base - h, xFor (repSeg[(size_t) i].first + repSeg[(size_t) i].second) - xFor (repSeg[(size_t) i].first), h).reduced (1.0f, 0);
        const bool active = i == playingRep;
        drawRepBlock (g, b, directionFor (order, i, dirSeq (processor)), active ? 0.95f : 0.6f, juce::jlimit (0.35f, 1.0f, bright), sp, active);
        if (active)
        {
            g.setColour (theme::text);
            const float x = b.getX() + b.getWidth() * juce::jlimit (0.0f, 1.0f, playingProg);
            g.drawLine (x, b.getY() - 3.0f, x, b.getBottom() + 3.0f, 1.5f);
        }

        // violoning
        if (attack > 0.5f)
        {
            g.setColour (theme::bg.withAlpha (0.22f));
            const int teeth = attackPerNote ? juce::jmax (1, (int) (b.getWidth() / 14.0f)) : 1;
            const float tw = b.getWidth() / (float) teeth;
            const float ramp = juce::jmin (1.0f, attack / (attackPerNote ? 300.0f : 1500.0f));
            for (int k = 0; k < teeth; ++k)
            {
                juce::Path tri;
                const float xs = b.getX() + tw * (float) k;
                tri.addTriangle (xs, b.getY(), xs, b.getBottom(), xs + tw * ramp, b.getY());
                g.fillPath (tri);
            }
        }

        // hauteur
        const int tr = transpose + drift * i;
        if (tr != 0 || scaleOn)
        {
            g.setColour (tr == 0 ? theme::faint : theme::tonal);
            g.setFont (theme::font (10.5f, true));
            g.drawText ((tr > 0 ? "+" : "") + juce::String (tr), juce::Rectangle<float> (b.getX(), b.getY() - 14.0f, b.getWidth(), 13.0f).toNearestInt(), juce::Justification::centred);
        }
    }
    if (reps > shown)
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (11.0f, true));
        g.drawText (u8 ("+") + juce::String (reps - shown) + u8 (" …"), juce::Rectangle<float> (xFor (repsEnd), base - 20.0f, 40.0f, 18.0f).toNearestInt(), juce::Justification::centredLeft);
    }
    if (! repSeg.empty())
    {
        g.setColour (theme::dim);
        g.setFont (theme::font (10.5f));
        g.drawText (juce::String (reps) + (reps > 1 ? u8 (" répétitions") : u8 (" répétition")),
                    juce::Rectangle<float> (xFor (firstRep), labels.getY(), xFor (repsEnd) - xFor (firstRep), labels.getHeight()).toNearestInt(), juce::Justification::centred);
    }

    // silence puis capture suivante
    juce::String sentence;
    if (flowMode)
    {
        auto sil = juce::Rectangle<float> (xFor (repsEnd) + 4.0f, base - laneH * 0.55f, xFor (repsEnd + sMax) - xFor (repsEnd) - 4.0f, laneH * 0.55f);
        const float minW = xFor (repsEnd + sMin) - xFor (repsEnd);
        const bool silNow = flow == PhraseEngine::FlowSilence;
        g.setColour (theme::silence.withAlpha (silNow ? 0.35f : 0.18f));
        g.fillRoundedRectangle (sil.withWidth (juce::jmax (2.0f, minW - 4.0f)), 4.0f);
        hatch (g, sil, theme::silence.withAlpha (0.35f));
        g.setColour (theme::silence.withAlpha (silNow ? 1.0f : 0.7f));
        g.drawRoundedRectangle (sil, 4.0f, silNow ? 2.0f : 1.0f);
        if (silNow && e.flowSilenceTotal.load() > 0.0f)
        {
            const float done = 1.0f - e.flowRemaining.load() / e.flowSilenceTotal.load();
            const float x = sil.getX() + minW * 0.0f + (xFor (repsEnd + e.flowSilenceTotal.load()) - sil.getX()) * juce::jlimit (0.0f, 1.0f, done);
            g.setColour (theme::text);
            g.drawLine (x, sil.getY() - 3.0f, x, sil.getBottom() + 3.0f, 1.5f);
        }
        g.setColour (theme::dim);
        g.drawText (u8 ("silence ") + (std::abs (sMax - sMin) < 0.05f ? timeText (sMin) : timeText (sMin) + u8 (" à ") + timeText (sMax)),
                    juce::Rectangle<float> (sil.getX(), labels.getY(), juce::jmax (90.0f, sil.getWidth()), labels.getHeight()).toNearestInt(), juce::Justification::centred);

        // capture suivante
        auto next = juce::Rectangle<float> (xFor (repsEnd + sMax) + 6.0f, base - laneH * 0.8f, juce::jmax (18.0f, xFor (L * 0.3f) - xFor (0)), laneH * 0.8f);
        g.setColour (theme::record.withAlpha (0.6f));
        juce::Path dashed; dashed.addRoundedRectangle (next, 4.0f);
        const float dash[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, dashed, dash, 2);
        g.fillPath (dashed);
        g.setFont (theme::font (14.0f, true));
        g.drawText (u8 ("↺"), next.toNearestInt(), juce::Justification::centred);

        sentence = u8 ("Capture ") + timeText (L) + u8 ("  →  délai  →  ") + juce::String (reps) + u8 (" × relecture  →  silence  →  ")
                 + (mode == PhraseEngine::Bank ? u8 ("capture suivante tant que la banque n'est pas pleine, puis la séquence")
                                               : u8 ("nouvelle capture, et ainsi de suite"));
    }
    else
    {
        sentence = mode == PhraseEngine::Continuous ? u8 ("Les captures s'enchaînent sans arrêt : chacune est rejouée ") + juce::String (reps) + u8 (" fois, les relectures se superposent.")
                 : mode == PhraseEngine::Threshold  ? u8 ("Chaque fois que tu joues au-dessus du seuil, une phrase est capturée puis rejouée ") + juce::String (reps) + u8 (" fois.")
                                                    : u8 ("Une phrase est capturée à chaque clic sur Capturer, puis rejouée ") + juce::String (reps) + u8 (" fois.");
    }

    g.setColour (theme::dim);
    g.setFont (theme::font (11.5f));
    g.drawFittedText (sentence, footer.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
}

//==============================================================================
// APERÇU DU HASARD SUR 8 NOTES
namespace
{
    struct SimNote { int src; bool mute, rev; int oct; int jit; };

    std::vector<SimNote> simulate (const PalindromeProcessor& p, bool reverse, juce::uint32 seed)
    {
        const int n = 8;
        std::vector<int> order (n);
        std::iota (order.begin(), order.end(), 0);
        if (reverse) std::reverse (order.begin(), order.end());

        juce::Random rng ((juce::int64) seed);
        const float a = p.param (ParamIDs::randomness) * 0.01f;
        std::vector<float> keys (n);
        for (int k = 0; k < n; ++k) keys[(size_t) k] = (1.0f - a) * (float) k + a * (float) n * rng.nextFloat();
        if (a > 0.0f)
        {
            std::vector<int> idx (n);
            std::iota (idx.begin(), idx.end(), 0);
            std::stable_sort (idx.begin(), idx.end(), [&] (int l, int r) { return keys[(size_t) l] < keys[(size_t) r]; });
            std::vector<int> sh (n);
            for (int k = 0; k < n; ++k) sh[(size_t) k] = order[(size_t) idx[(size_t) k]];
            order = sh;
        }

        std::vector<SimNote> out;
        int prev = -1;
        for (int k = 0; k < n; ++k)
        {
            const float rSkip = rng.nextFloat(), rRep = rng.nextFloat(), rRev = rng.nextFloat();
            const float rOct = rng.nextFloat(), rOctUp = rng.nextFloat();
            const float rJit = rng.nextFloat(), rJitUp = rng.nextFloat(), rJitBig = rng.nextFloat();
            SimNote s;
            s.src = order[(size_t) k];
            if (prev >= 0 && rRep < p.param (ParamIDs::repeatProb) * 0.01f) s.src = prev;
            s.mute = rSkip < p.param (ParamIDs::skipProb) * 0.01f;
            s.rev = reverse != (rRev < p.param (ParamIDs::noteReverse) * 0.01f);
            s.oct = rOct < p.param (ParamIDs::octaveProb) * 0.01f ? (rOctUp < 0.5f ? -1 : 1) : 0;
            s.jit = rJit < p.param (ParamIDs::melodic) * 0.01f ? (rJitUp < 0.5f ? -1 : 1) * (rJitBig < 0.3f ? 2 : 1) : 0;
            out.push_back (s);
            prev = s.src;
        }
        return out;
    }

    const float noteWidths[8] = { 1.0f, 0.6f, 0.6f, 1.2f, 0.8f, 0.5f, 1.0f, 1.4f };
}

void NotesPreviewView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto area = r.reduced (12.0f, 8.0f);
    g.setColour (theme::faint);
    g.setFont (theme::font (11.0f));
    g.drawText (u8 ("clic = nouveau tirage"), area.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredRight);

    const float labelW = 110.0f;
    const float rowH = (area.getHeight() - 12.0f) / 3.0f;
    float totalW = 0.0f; for (float w : noteWidths) totalW += w;

    auto drawRow = [&] (juce::Rectangle<float> row, const juce::String& name, juce::Colour nameCol, const std::vector<SimNote>* sim, bool reverse)
    {
        g.setColour (nameCol);
        g.setFont (theme::font (11.5f, true));
        g.drawText (name, row.removeFromLeft (labelW).toNearestInt(), juce::Justification::centredLeft);

        float x = row.getX();
        for (int k = 0; k < 8; ++k)
        {
            const int slotNote = sim == nullptr ? k : (reverse ? 7 - k : k);
            juce::ignoreUnused (slotNote);
            const SimNote s = sim == nullptr ? SimNote { k, false, false, 0, 0 } : (*sim)[(size_t) k];
            const int widthIdx = sim == nullptr ? k : s.src;
            const float w = row.getWidth() * noteWidths[(size_t) widthIdx] / totalW;
            auto b = juce::Rectangle<float> (x + 2.0f, row.getY() + 3.0f, w - 4.0f, row.getHeight() - 6.0f);
            x += w;

            if (sim == nullptr)
            {
                g.setColour (theme::panel2);
                g.fillRoundedRectangle (b, 4.0f);
                g.setColour (theme::dim);
                g.setFont (theme::font (12.0f, true));
                g.drawText (juce::String (k + 1), b.toNearestInt(), juce::Justification::centred);
                continue;
            }

            if (s.mute)
            {
                juce::Path pth; pth.addRoundedRectangle (b, 4.0f);
                const float dash[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (pth, pth, dash, 2);
                g.setColour (theme::faint);
                g.fillPath (pth);
                continue;
            }

            drawRepBlock (g, b, s.rev ? 1 : 0, 0.85f, 1.0f, 1.0f, false, false);
            g.setColour (theme::bg);
            g.setFont (theme::font (12.5f, true));
            g.drawText (juce::String (s.src + 1), b.toNearestInt(), juce::Justification::centred);

            juce::String badge;
            if (s.oct != 0) badge << (s.oct > 0 ? u8 ("↑8") : u8 ("↓8"));
            if (s.jit != 0) badge << (badge.isEmpty() ? "" : " ") << (s.jit > 0 ? "+" : "") << s.jit;
            if (badge.isNotEmpty())
            {
                auto bb = juce::Rectangle<float> (b.getRight() - 30.0f, b.getY() - 6.0f, 30.0f, 13.0f);
                g.setColour (theme::lecture);
                g.fillRoundedRectangle (bb, 4.0f);
                g.setColour (theme::bg);
                g.setFont (theme::font (10.0f, true));
                g.drawText (badge, bb.toNearestInt(), juce::Justification::centred);
            }
        }
    };

    const auto normal = simulate (processor, false, seed);
    const auto rev = simulate (processor, true, seed ^ 0x9e3779b9u);
    auto row0 = area.removeFromTop (rowH); area.removeFromTop (6.0f);
    auto row1 = area.removeFromTop (rowH); area.removeFromTop (6.0f);
    auto row2 = area.removeFromTop (rowH);
    drawRow (row0, u8 ("phrase jouée"), theme::dim, nullptr, false);
    drawRow (row1, u8 ("NORMAL ▶"), theme::normal, &normal, false);
    drawRow (row2, u8 ("◀ REVERSE"), theme::reverse, &rev, true);
}

//==============================================================================
// RYTHME
void RhythmView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto area = r.reduced (12.0f, 10.0f);

    static const float grids[] = { 0.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
    const float grid = grids[juce::jlimit (0, 6, (int) pv (ParamIDs::quantGrid))];
    const float amount = pv (ParamIDs::quantAmount) * 0.01f;
    const float human = pv (ParamIDs::humanize);
    const float beats = 4.0f;
    const float onsets[] = { 0.0f, 0.62f, 1.13f, 1.71f, 2.4f, 2.86f, 3.35f };
    const float beatMs = 60000.0f / (float) juce::jmax (20.0, processor.hostBpm.load());

    auto gutter = area.removeFromLeft (48.0f);
    auto xFor = [&] (float b) { return area.getX() + area.getWidth() * b / beats; };
    auto top = area.removeFromTop (area.getHeight() * 0.5f).reduced (0, 4.0f);
    auto bottom = area.reduced (0, 4.0f);

    if (grid > 0.0f)
        for (float b = 0.0f; b <= beats + 0.001f; b += grid)
        {
            const bool beatLine = std::abs (b - std::round (b)) < 0.001f;
            g.setColour ((beatLine ? theme::lecture.withAlpha (0.5f) : theme::outline));
            g.drawVerticalLine ((int) xFor (b), bottom.getY(), bottom.getBottom());
        }

    g.setColour (theme::faint);
    g.setFont (theme::font (10.5f));
    g.drawText (u8 ("joué"), gutter.withY (top.getY()).withHeight (top.getHeight()).toNearestInt(), juce::Justification::centredLeft);
    g.drawText (u8 ("rejoué"), gutter.withY (bottom.getY()).withHeight (bottom.getHeight()).toNearestInt(), juce::Justification::centredLeft);

    for (float o : onsets)
    {
        const float target = grid > 0.0f ? std::round (o / grid) * grid : o;
        const float q = o + amount * (target - o) * (grid > 0.0f ? 1.0f : 0.0f);
        g.setColour (theme::dim);
        g.fillRoundedRectangle (juce::Rectangle<float> (xFor (o) - 2.0f, top.getCentreY() - 7.0f, 4.0f, 14.0f), 2.0f);
        g.setColour (theme::lecture.withAlpha (0.45f));
        g.drawLine (xFor (o), top.getCentreY() + 7.0f, xFor (q), bottom.getCentreY() - 7.0f, 1.0f);
        if (human > 0.5f)
        {
            const float spread = (human / beatMs) * (area.getWidth() / beats);
            g.setColour (theme::lecture.withAlpha (0.25f));
            g.fillRoundedRectangle (juce::Rectangle<float> (xFor (q) - spread, bottom.getCentreY() - 3.0f, spread * 2.0f, 6.0f), 3.0f);
        }
        g.setColour (theme::lecture);
        g.fillRoundedRectangle (juce::Rectangle<float> (xFor (q) - 2.0f, bottom.getCentreY() - 7.0f, 4.0f, 14.0f), 2.0f);
    }

    if (grid <= 0.0f && human < 0.5f)
        hint (g, bottom, u8 ("grille off : rythme d'origine"));
}

//==============================================================================
// CLAVIER
juce::Rectangle<float> KeyboardView::keyRect (int pc, juce::Rectangle<float> a) const
{
    static const int whiteIndex[12] = { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
    static const float blackPos[12] = { 0, 1, 0, 2, 0, 0, 4, 0, 5, 0, 6, 0 };
    const float ww = a.getWidth() / 7.0f;
    if (whiteIndex[pc] >= 0) return { a.getX() + ww * (float) whiteIndex[pc], a.getY(), ww, a.getHeight() };
    const float bw = ww * 0.6f;
    return { a.getX() + ww * blackPos[pc] - bw * 0.5f, a.getY(), bw, a.getHeight() * 0.6f };
}

void KeyboardView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const int root = (int) pv (ParamIDs::keyRoot);
    const int scaleIdx = (int) pv (ParamIDs::scale);
    const bool on = scaleIdx > 0;
    const auto scale = (pal::Scale) scaleIdx;
    auto kb = r.reduced (2.0f, 2.0f).withTrimmedBottom (16.0f);

    auto degreeOf = [&] (int pc) -> int
    {
        if (! on || ! pal::inScale (pc - root, scale)) return 0;
        int d = 0;
        for (int i = 0; i <= ((pc - root + 12) % 12); ++i) if (pal::inScale (i, scale)) ++d;
        return d;
    };

    for (int pass = 0; pass < 2; ++pass)
        for (int pc = 0; pc < 12; ++pc)
        {
            const bool black = (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
            if ((pass == 0) == black) continue;
            auto k = keyRect (pc, kb).reduced (1.0f, 0);
            const int deg = degreeOf (pc);
            const bool isRoot = pc == root;

            juce::Colour fill = black ? theme::panel : theme::panel2.brighter (0.15f);
            if (deg > 0) fill = theme::tonal.withAlpha (isRoot ? 0.95f : (black ? 0.6f : 0.45f));
            else if (isRoot) fill = theme::tonal.withAlpha (0.25f);
            g.setColour (fill);
            g.fillRoundedRectangle (k, 4.0f);
            g.setColour (theme::bg);
            g.drawRoundedRectangle (k, 4.0f, 1.0f);

            g.setColour (deg > 0 ? theme::bg : theme::dim);
            g.setFont (theme::font (black ? 10.0f : 11.5f, isRoot));
            if (deg > 0)
                g.drawText (isRoot ? u8 ("T") : juce::String (deg), k.withTrimmedTop (k.getHeight() * 0.5f).toNearestInt(), juce::Justification::centred);
            if (! black)
            {
                g.setColour (isRoot ? theme::tonal : theme::dim);
                g.drawText (noteName (pc), juce::Rectangle<float> (k.getX(), r.getBottom() - 15.0f, k.getWidth(), 14.0f).toNearestInt(), juce::Justification::centred);
            }
        }

    if (! on)
    {
        g.setColour (theme::bg.withAlpha (0.55f));
        g.fillRoundedRectangle (kb, 4.0f);
        hint (g, kb, u8 ("Gamme Off : pas de recalage, transposition en demi-tons.\nChoisis une gamme, puis clique sur une touche pour la tonique."));
    }
}

void KeyboardView::mouseDown (const juce::MouseEvent& e)
{
    auto kb = getLocalBounds().toFloat().reduced (2.0f).withTrimmedBottom (16.0f);
    int hit = -1;
    for (int pc : { 1, 3, 6, 8, 10 }) if (keyRect (pc, kb).contains (e.position)) hit = pc;
    if (hit < 0) for (int pc : { 0, 2, 4, 5, 7, 9, 11 }) if (keyRect (pc, kb).contains (e.position)) hit = pc;
    if (hit < 0) return;
    if (auto* p = processor.apvts.getParameter (ParamIDs::keyRoot))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) hit));
        p->endChangeGesture();
    }
}

//==============================================================================
// TRANSPOSITION
void TransposeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto area = r.reduced (12.0f, 18.0f);

    const int root = (int) pv (ParamIDs::keyRoot);
    const int scaleIdx = (int) pv (ParamIDs::scale);
    const int transpose = juce::roundToInt (pv (ParamIDs::transpose)), drift = juce::roundToInt (pv (ParamIDs::drift));
    const int reps = juce::jmin (16, juce::roundToInt (pv (ParamIDs::repetitions)));

    std::vector<int> semis;
    int maxAbs = 12;
    for (int i = 0; i < reps; ++i)
    {
        const int steps = transpose + drift * i;
        const int s = scaleIdx > 0 ? pal::stepInScale (60 + root, steps, root, (pal::Scale) scaleIdx) - (60 + root) : steps;
        semis.push_back (s);
        maxAbs = juce::jmax (maxAbs, std::abs (s));
    }

    const float mid = area.getCentreY();
    g.setColour (theme::outline);
    g.drawHorizontalLine ((int) mid, area.getX(), area.getRight());
    g.setColour (theme::faint);
    g.setFont (theme::font (10.0f));
    g.drawText (scaleIdx > 0 ? u8 ("note d'origine") : u8 ("hauteur d'origine"), area.withHeight (12.0f).withY (mid + 2.0f).toNearestInt(), juce::Justification::topRight);

    const float w = area.getWidth() / (float) juce::jmax (1, reps);
    for (int i = 0; i < reps; ++i)
    {
        const float h = (area.getHeight() * 0.5f - 6.0f) * (float) semis[(size_t) i] / (float) maxAbs;
        auto b = juce::Rectangle<float> (area.getX() + w * (float) i + w * 0.2f, juce::jmin (mid, mid - h), w * 0.6f, juce::jmax (2.0f, std::abs (h)));
        g.setColour (theme::tonal.withAlpha (semis[(size_t) i] == 0 ? 0.3f : 0.85f));
        g.fillRoundedRectangle (b, 3.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (10.5f, true));
        const juce::String lab = scaleIdx > 0 ? noteName (root + semis[(size_t) i]) : ((semis[(size_t) i] > 0 ? "+" : "") + juce::String (semis[(size_t) i]));
        auto lb = semis[(size_t) i] >= 0 ? juce::Rectangle<float> (b.getX() - 8.0f, b.getY() - 14.0f, b.getWidth() + 16.0f, 13.0f)
                                         : juce::Rectangle<float> (b.getX() - 8.0f, b.getBottom() + 1.0f, b.getWidth() + 16.0f, 13.0f);
        g.drawText (lab, lb.toNearestInt(), juce::Justification::centred);
    }
    g.setColour (theme::faint);
    g.setFont (theme::font (10.5f));
    g.drawText (u8 ("répétition 1 → ") + juce::String (reps), r.reduced (12.0f, 2.0f).toNearestInt(), juce::Justification::bottomLeft);
}

//==============================================================================
// ÉCHO
void EchoView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto area = r.reduced (14.0f, 12.0f);

    const int reps = juce::jmin (16, juce::roundToInt (pv (ParamIDs::repetitions)));
    const int order = (int) pv (ParamIDs::order);
    const int speedMode = (int) pv (ParamIDs::speedMode);
    const float decay = pv (ParamIDs::decay), damping = pv (ParamIDs::damping) * 0.01f, tone = pv (ParamIDs::tone);
    const float mod = pv (ParamIDs::modDepth) * 0.01f, rate = pv (ParamIDs::modRate), drive = pv (ParamIDs::drive) * 0.01f;
    const float fb = pv (ParamIDs::feedback) * 0.01f;

    auto fbArea = area.removeFromBottom (22.0f);
    auto bars = area.withTrimmedTop (16.0f);

    float totalW = 0.0f;
    std::vector<float> widths;
    for (int i = 0; i < reps; ++i) { widths.push_back (1.0f / speedFor (speedMode, i, reps)); totalW += widths.back() + 0.25f; }

    float x = bars.getX();
    const float unit = bars.getWidth() / totalW;
    for (int i = 0; i < reps; ++i)
    {
        const float lvl = juce::Decibels::decibelsToGain (decay * (float) i);
        const float h = bars.getHeight() * juce::jlimit (0.03f, 1.0f, std::sqrt (lvl));
        const float cut = juce::jmax (200.0f, tone * std::pow (1.0f - 0.5f * damping, (float) i));
        const float bright = 0.45f + 0.55f * (std::log (cut / 200.0f) / std::log (100.0f));
        auto b = juce::Rectangle<float> (x, bars.getBottom() - h, widths[(size_t) i] * unit, h);
        drawRepBlock (g, b, directionFor (order, i, dirSeq (processor)), 0.85f, juce::jlimit (0.3f, 1.0f, bright), speedFor (speedMode, i, reps), false);
        if (drive > 0.01f)
        {
            g.setColour (theme::reverse.withAlpha (0.35f * drive));
            g.fillRoundedRectangle (b.withHeight (juce::jmin (b.getHeight(), 6.0f)), 3.0f);
        }
        if (i == 0 || i == reps - 1)
        {
            g.setColour (theme::dim);
            g.setFont (theme::font (10.5f, true));
            g.drawText (juce::String (juce::roundToInt (decay * (float) i)) + " dB", juce::Rectangle<float> (b.getX() - 10.0f, b.getY() - 15.0f, b.getWidth() + 20.0f, 13.0f).toNearestInt(), juce::Justification::centred);
        }
        x += (widths[(size_t) i] + 0.25f) * unit;
    }

    // modulation
    if (mod > 0.005f)
    {
        juce::Path wob;
        const float y0 = bars.getY() - 6.0f;
        bool firstPt = true;
        for (float xx = bars.getX(); xx <= bars.getRight(); xx += 2.0f)
        {
            const float yy = y0 + std::sin ((xx - bars.getX()) / bars.getWidth() * juce::MathConstants<float>::twoPi * (2.0f + rate * 3.0f)) * 5.0f * mod;
            if (firstPt) { wob.startNewSubPath (xx, yy); firstPt = false; } else wob.lineTo (xx, yy);
        }
        g.setColour (theme::echo.withAlpha (0.7f));
        g.strokePath (wob, juce::PathStrokeType (1.5f));
    }

    // réinjection
    if (fb > 0.005f)
    {
        juce::Path loop;
        loop.startNewSubPath (bars.getRight() - 6.0f, fbArea.getY());
        loop.quadraticTo (bars.getCentreX(), fbArea.getBottom() + 8.0f, bars.getX() + 6.0f, fbArea.getY());
        g.setColour (theme::echo.withAlpha (0.4f + 0.6f * fb));
        g.strokePath (loop, juce::PathStrokeType (1.0f + 4.0f * fb, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path head; head.addTriangle (bars.getX() + 6.0f, fbArea.getY() - 4.0f, bars.getX() + 1.0f, fbArea.getY() + 6.0f, bars.getX() + 13.0f, fbArea.getY() + 4.0f);
        g.fillPath (head);
        g.setColour (theme::text);
        g.setFont (theme::font (10.5f, true));
        g.drawText (u8 ("réinjection ") + juce::String (juce::roundToInt (fb * 100)) + " %", fbArea.toNearestInt(), juce::Justification::centred);
    }
}

//==============================================================================
// STÉRÉO
void StereoView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (16.0f, 12.0f);

    const int mode = (int) pv (ParamIDs::stereoMode);
    const float w = pv (ParamIDs::width) * 0.01f;
    const auto c = juce::Point<float> (a.getCentreX(), a.getBottom() - 6.0f);
    const float R = juce::jmin (a.getWidth() * 0.5f - 26.0f, a.getHeight() - 22.0f);
    const float maxAng = juce::MathConstants<float>::pi * 0.36f;

    juce::Path arc;
    arc.addCentredArc (c.x, c.y, R, R, 0.0f, -maxAng, maxAng, true);
    g.setColour (theme::outline);
    g.strokePath (arc, juce::PathStrokeType (2.0f));
    if (mode > 0)
    {
        juce::Path span;
        span.addCentredArc (c.x, c.y, R, R, 0.0f, -maxAng * w, maxAng * w, true);
        g.setColour (theme::espace.withAlpha (0.7f));
        g.strokePath (span, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (theme::dim);
    g.setFont (theme::font (11.5f, true));
    g.drawText ("G", juce::Rectangle<float> (c.x - R * std::sin (maxAng) - 32.0f, c.y - R * std::cos (maxAng) - 8.0f, 16.0f, 16.0f).toNearestInt(), juce::Justification::centred);
    g.drawText ("D", juce::Rectangle<float> (c.x + R * std::sin (maxAng) + 16.0f, c.y - R * std::cos (maxAng) - 8.0f, 16.0f, 16.0f).toNearestInt(), juce::Justification::centred);

    // auditeur
    g.setColour (theme::faint);
    g.fillEllipse (c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f);

    auto dot = [&] (float pan, juce::Colour col, const juce::String& lab, float rr = 1.0f)
    {
        const float ang = pan * maxAng;
        const auto p = juce::Point<float> (c.x + R * rr * std::sin (ang), c.y - R * rr * std::cos (ang));
        g.setColour (col.withAlpha (0.25f));
        g.fillEllipse (p.x - 13.0f, p.y - 13.0f, 26.0f, 26.0f);
        g.setColour (col);
        g.fillEllipse (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f);
        g.setColour (theme::bg);
        g.setFont (theme::font (10.0f, true));
        g.drawText (lab, juce::Rectangle<float> (p.x - 8.0f, p.y - 8.0f, 16.0f, 16.0f).toNearestInt(), juce::Justification::centred);
    };

    switch (mode)
    {
        case 1: dot (-w, theme::reverse, u8 ("◀")); dot (w, theme::normal, u8 ("▶")); break;
        case 2: dot (-w, theme::normal, u8 ("▶")); dot (w, theme::reverse, u8 ("◀")); break;
        case 3:
            for (int i = 0; i < 4; ++i)
                dot ((i % 2 == 0 ? -w : w), (i % 2 == 0 ? theme::reverse : theme::normal), juce::String (i + 1), 1.0f - 0.18f * (float) (i / 2));
            break;
        case 4:
        {
            juce::Random rnd (7);
            for (int i = 0; i < 6; ++i) dot ((rnd.nextFloat() * 2.0f - 1.0f) * w, i % 2 ? theme::normal : theme::reverse, "?", 0.65f + 0.35f * rnd.nextFloat());
            break;
        }
        default: dot (0.0f, theme::normal, u8 ("▶"), 1.0f); dot (0.0f, theme::reverse, u8 ("◀"), 0.72f); break;
    }
}

//==============================================================================
// DIFFUSION
void DiffusionView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (14.0f, 12.0f);

    const float diff = pv (ParamIDs::diffusion) * 0.01f;
    const float size = pv (ParamIDs::roomSize) * 0.01f;
    const bool grow = pv (ParamIDs::diffusionGrow) > 0.5f;
    const int n = 5;
    const float w = a.getWidth() / (float) n;

    for (int i = 0; i < n; ++i)
    {
        const float send = diff * (grow ? (float) (i + 1) / (float) n : 1.0f);
        auto b = juce::Rectangle<float> (a.getX() + w * (float) i + w * 0.18f, a.getCentreY() - 12.0f, w * 0.64f, 24.0f);
        if (send > 0.01f)
        {
            const float rad = (18.0f + 52.0f * size) * (0.55f + 0.45f * send);
            juce::ColourGradient halo (theme::espace.withAlpha (0.25f + 0.6f * send), b.getCentreX(), b.getCentreY(),
                                       theme::espace.withAlpha (0.0f), b.getCentreX() + rad, b.getCentreY(), true);
            g.setGradientFill (halo);
            g.fillEllipse (b.getCentreX() - rad, b.getCentreY() - rad * 0.8f, rad * 2.0f, rad * 1.6f);
        }
        drawRepBlock (g, b, 0, 0.85f, 1.0f, 1.0f, false);
        g.setColour (theme::faint);
        g.setFont (theme::font (10.5f));
        g.drawText (u8 ("rép. ") + juce::String (i + 1), juce::Rectangle<float> (b.getX(), b.getBottom() + 4.0f, b.getWidth(), 13.0f).toNearestInt(), juce::Justification::centred);
    }
    if (diff < 0.01f) hint (g, a.removeFromBottom (18.0f), u8 ("Diffusion à 0 : répétitions sèches"));
}

//==============================================================================
// DUCKING
void DuckView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (14.0f, 14.0f);

    const float amount = pv (ParamIDs::duckAmount);           // dB (négatif)
    const float release = pv (ParamIDs::duckRelease) / 1000.0f; // s
    const float span = 4.0f;                                   // secondes affichées
    auto playing = [] (float t) { return (t > 0.4f && t < 1.4f) || (t > 2.3f && t < 2.8f); };

    juce::Path in, wet;
    float env = 0.0f;
    const int steps = 240;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = span * (float) i / (float) steps;
        const float x = a.getX() + a.getWidth() * (float) i / (float) steps;
        const float target = playing (t) ? 1.0f : 0.0f;
        const float dt = span / (float) steps;
        env = juce::jmax (target, env * std::exp (-dt / juce::jmax (0.02f, release * 0.35f)));
        const float duckDb = amount * env;
        const float lvl = juce::Decibels::decibelsToGain (duckDb);
        const float yIn = a.getBottom() - a.getHeight() * 0.45f * target;
        const float yWet = a.getY() + a.getHeight() * (1.0f - lvl) * 0.9f;
        if (i == 0) { in.startNewSubPath (x, a.getBottom()); in.lineTo (x, yIn); wet.startNewSubPath (x, yWet); }
        else { in.lineTo (x, yIn); wet.lineTo (x, yWet); }
    }
    in.lineTo (a.getRight(), a.getBottom());
    in.closeSubPath();
    g.setColour (theme::dim.withAlpha (0.35f));
    g.fillPath (in);
    g.setColour (theme::normal);
    g.strokePath (wet, juce::PathStrokeType (2.0f));

    g.setFont (theme::font (10.5f, true));
    g.setColour (theme::dim);
    g.drawText ("ton jeu", a.removeFromBottom (14.0f).toNearestInt(), juce::Justification::bottomRight);
    g.setColour (theme::normal);
    g.drawText ("relecture", a.removeFromTop (14.0f).toNearestInt(), juce::Justification::topRight);
    if (amount > -0.05f) hint (g, a, u8 ("Ducking à 0 dB : désactivé"));
}

//==============================================================================
// VIOLONING
void EnvelopeView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (12.0f, 10.0f);

    const float attack = pv (ParamIDs::attack);
    const bool perNote = pv (ParamIDs::attackScope) < 0.5f;
    const int dir = (int) pv (ParamIDs::attackDir);
    const int pattern = (int) pv (ParamIDs::attackPattern);
    const float prob = pv (ParamIDs::attackProb) * 0.01f;

    auto hit = [&] (int idx, float rnd)
    {
        switch (pattern) { case 1: return idx % 2 == 0; case 2: return idx % 3 == 0; case 3: return idx % 4 == 0; case 4: return rnd < prob; default: return true; }
    };

    const float labelW = 100.0f;
    const float rowH = (a.getHeight() - 8.0f) * 0.5f;
    for (int row = 0; row < 2; ++row)
    {
        const bool rev = row == 1;
        auto line = a.removeFromTop (rowH);
        a.removeFromTop (8.0f);
        const auto col = rev ? theme::reverse : theme::normal;
        const bool dirOk = dir == 0 || (dir == 1 && ! rev) || (dir == 2 && rev);
        g.setColour (col);
        g.setFont (theme::font (11.5f, true));
        g.drawText (rev ? u8 ("◀ REVERSE") : u8 ("NORMAL ▶"), line.removeFromLeft (labelW).toNearestInt(), juce::Justification::centredLeft);

        const int n = perNote ? 8 : 4;
        juce::Random rnd (99 + row);
        float x = line.getX();
        float totalW = 0.0f; for (int k = 0; k < n; ++k) totalW += perNote ? noteWidths[k] : 1.0f;
        for (int k = 0; k < n; ++k)
        {
            const float w = line.getWidth() * (perNote ? noteWidths[k] : 1.0f) / totalW;
            auto b = juce::Rectangle<float> (x + 2.0f, line.getY() + 2.0f, w - 4.0f, line.getHeight() - 4.0f);
            x += w;
            const bool swell = attack > 0.5f && dirOk && hit (k, rnd.nextFloat());
            const float noteMs = perNote ? 350.0f * noteWidths[k] : 1600.0f;
            const float rampFrac = swell ? juce::jmin (1.0f, attack / noteMs) : 0.0f;

            juce::Path env;
            env.startNewSubPath (b.getX(), b.getBottom());
            if (rev)
            {
                // note inversée : elle monte naturellement, l'attaque adoucit encore le début
                env.lineTo (b.getX() + b.getWidth() * juce::jmax (0.02f, rampFrac), b.getBottom() - b.getHeight() * (swell ? 0.25f : 0.35f));
                env.quadraticTo (b.getRight() - b.getWidth() * 0.15f, b.getBottom() - b.getHeight() * 0.45f, b.getRight() - 2.0f, b.getY());
                env.lineTo (b.getRight(), b.getBottom());
            }
            else
            {
                env.lineTo (b.getX() + b.getWidth() * juce::jmax (0.02f, rampFrac), b.getY());
                env.quadraticTo (b.getX() + b.getWidth() * 0.75f, b.getY() + b.getHeight() * 0.3f, b.getRight(), b.getBottom() - b.getHeight() * 0.25f);
                env.lineTo (b.getRight(), b.getBottom());
            }
            env.closeSubPath();
            juce::ColourGradient gr (col.withAlpha (swell ? 0.9f : 0.4f), b.getX(), b.getY(), col.withAlpha (0.15f), b.getX(), b.getBottom(), false);
            g.setGradientFill (gr);
            g.fillPath (env);
            g.setColour (col.withAlpha (swell ? 1.0f : 0.5f));
            g.strokePath (env, juce::PathStrokeType (1.2f));
            if (swell)
            {
                g.setColour (theme::violon);
                g.fillEllipse (b.getX() + b.getWidth() * rampFrac - 3.0f, (rev ? b.getBottom() - b.getHeight() * 0.25f : b.getY()) - 3.0f, 6.0f, 6.0f);
            }
            if (! perNote)
            {
                g.setColour (theme::faint);
                g.setFont (theme::font (10.0f));
                g.drawText (u8 ("rép. ") + juce::String (k + 1), b.toNearestInt(), juce::Justification::centredTop);
            }
        }
    }
    if (attack < 0.5f)
        hint (g, getLocalBounds().toFloat().removeFromBottom (18.0f), u8 ("Attaque off : monte le bouton Attaque pour faire gonfler les notes"));
}

//==============================================================================
// TEXTURE
void TextureView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (14.0f, 12.0f);

    const float mix = pv (ParamIDs::textureMix) * 0.01f;
    const float size = pv (ParamIDs::grainSize);
    const float dens = pv (ParamIDs::grainDensity);
    const float spray = pv (ParamIDs::grainSpray) * 0.01f;
    const float revP = pv (ParamIDs::noteReverse) * 0.01f;
    const float span = 4.0f; // secondes représentées

    // tête de lecture
    g.setColour (theme::outline);
    g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.5f);
    g.setColour (theme::faint);
    g.setFont (theme::font (10.5f));
    g.drawText (u8 ("position dans la phrase ↑"), a.toNearestInt(), juce::Justification::topLeft);
    g.drawText (u8 ("temps →"), a.toNearestInt(), juce::Justification::bottomRight);

    juce::Random rnd (4242);
    const int count = juce::jlimit (4, 400, (int) (dens * span));
    const float gw = juce::jmax (3.0f, a.getWidth() * (size / 1000.0f) / span);
    for (int i = 0; i < count; ++i)
    {
        const float t = rnd.nextFloat();
        const float pos = juce::jlimit (0.0f, 1.0f, t + (rnd.nextFloat() - 0.5f) * 2.0f * spray);
        const bool rev = rnd.nextFloat() < revP;
        const auto col = rev ? theme::reverse : theme::texture;
        const float x = a.getX() + a.getWidth() * t, y = a.getBottom() - a.getHeight() * pos;
        juce::ColourGradient gr (col.withAlpha (0.25f + 0.65f * juce::jmax (0.15f, mix)), x, y, col.withAlpha (0.0f), x + gw * 0.5f, y, true);
        g.setGradientFill (gr);
        g.fillEllipse (x - gw * 0.5f, y - 5.0f, gw, 10.0f);
    }

    if (mix < 0.01f)
    {
        g.setColour (theme::bg.withAlpha (0.5f));
        g.fillRoundedRectangle (a, 6.0f);
        hint (g, a, u8 ("Texture à 0 % : la relecture reste normale.\nMonte « Texture » pour mélanger le nuage de grains."));
    }
}

//==============================================================================
// SORTIE
void OutputView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float mix = pv (ParamIDs::mix) * 0.01f;
    const float dry = juce::jmin (1.0f, 2.0f * (1.0f - mix));
    const float wet = juce::jmin (1.0f, 2.0f * mix) * juce::Decibels::decibelsToGain (pv (ParamIDs::wetGain), -60.0f);

    auto bar = [&] (juce::Rectangle<float> b, float v, juce::Colour c, const juce::String& t)
    {
        auto track = b.withTrimmedBottom (16.0f);
        g.setColour (theme::bg.withAlpha (0.6f));
        g.fillRoundedRectangle (track, 4.0f);
        const float h = track.getHeight() * juce::jlimit (0.0f, 1.0f, v / 1.5f);
        g.setColour (c);
        g.fillRoundedRectangle (track.withTop (track.getBottom() - h), 4.0f);
        g.setColour (theme::dim);
        g.setFont (theme::font (10.5f));
        g.drawText (t, b.withTop (b.getBottom() - 14.0f).toNearestInt(), juce::Justification::centred);
    };

    const float meter = processor.engine.inputPeak.load();
    const float mdb = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (meter, -60.0f) + 60.0f) / 60.0f);
    auto cols = r.reduced (6.0f, 0);
    const float w = cols.getWidth() / 3.0f;
    bar (cols.removeFromLeft (w).reduced (8.0f, 0), mdb * 1.5f, mdb > 0.95f ? theme::record : theme::dim, u8 ("entrée"));
    bar (cols.removeFromLeft (w).reduced (8.0f, 0), dry, theme::text.withAlpha (0.7f), "direct");
    bar (cols.reduced (8.0f, 0), wet, theme::normal, "relect.");
}

//==============================================================================
// SENS DES RÉPÉTITIONS
void DirectionView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (10.0f, 10.0f);

    const int order = (int) pv (ParamIDs::order);
    const int reps = juce::jlimit (1, 8, juce::roundToInt (pv (ParamIDs::repetitions)));
    g.setColour (theme::faint);
    g.setFont (theme::font (10.5f));
    g.drawText (u8 ("répétitions, dans l'ordre"), a.removeFromTop (14.0f).toNearestInt(), juce::Justification::centredLeft);
    a.removeFromTop (4.0f);

    const float w = a.getWidth() / (float) reps;
    for (int i = 0; i < reps; ++i)
    {
        auto b = juce::Rectangle<float> (a.getX() + w * (float) i + 2.0f, a.getY(), w - 4.0f, a.getHeight() - 16.0f);
        const int dir = directionFor (order, i, dirSeq (processor));
        drawRepBlock (g, b, dir, 0.85f, 1.0f, 1.0f, false, false);
        g.setColour (theme::bg);
        g.setFont (theme::font (14.0f, true));
        const juce::String sym = dir == 1 ? u8 ("◀") : dir == 0 ? u8 ("▶") : dir == 2 ? u8 ("◀▶") : dir == 4 ? u8 ("–") : "?";
        if (dir == 4) g.setColour (theme::faint);
        g.drawText (sym, b.toNearestInt(), juce::Justification::centred);
        g.setColour (theme::faint);
        g.setFont (theme::font (10.5f));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (b.getX(), b.getBottom() + 2.0f, b.getWidth(), 13.0f).toNearestInt(), juce::Justification::centred);
    }
}

//==============================================================================
// TEMPO
void TempoView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (10.0f, 8.0f);

    const double bpm = processor.currentBpm.load();
    const double ppq = processor.currentPpq.load();
    const int bpb = juce::jlimit (1, 12, (int) std::round (processor.currentBeatsPerBar.load()));
    const bool fromHost = processor.tempoFromHost.load();
    const bool internal = pv (ParamIDs::tempoSource) > 0.5f;

    g.setColour (theme::text);
    g.setFont (theme::font (24.0f, true));
    auto top = a.removeFromTop (30.0f);
    g.drawText (juce::String (bpm, 1), top.toNearestInt(), juce::Justification::centredLeft);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.0f));
    g.drawText ("BPM", top.withTrimmedLeft (juce::GlyphArrangement::getStringWidth (theme::font (24.0f, true), juce::String (bpm, 1)) + 6.0f).toNearestInt(),
                juce::Justification::centredLeft);

    g.setColour (fromHost ? theme::normal : theme::cycle);
    g.setFont (theme::font (10.5f));
    g.drawText (internal ? u8 ("horloge interne (tap)") : fromHost ? u8 ("calé sur le projet en lecture") : u8 ("projet à l'arrêt : horloge interne"),
                a.removeFromTop (15.0f).toNearestInt(), juce::Justification::centredLeft);
    a.removeFromTop (6.0f);

    const double beatPos = ppq - std::floor (ppq / bpb) * bpb;
    const int beat = juce::jlimit (0, bpb - 1, (int) std::floor (beatPos));
    const float frac = (float) (beatPos - std::floor (beatPos));
    const float d = juce::jmin (16.0f, (a.getWidth() - 4.0f * (float) (bpb - 1)) / (float) bpb);
    for (int i = 0; i < bpb; ++i)
    {
        auto dot = juce::Rectangle<float> (a.getX() + (float) i * (d + 4.0f), a.getY(), d, d);
        const bool cur = i == beat;
        const auto c = i == 0 ? theme::hold : theme::cycle;
        g.setColour (cur ? c.withAlpha (1.0f - 0.6f * frac) : theme::panel2);
        g.fillEllipse (dot);
        g.setColour (c.withAlpha (0.5f));
        g.drawEllipse (dot, 1.0f);
    }
}

//==============================================================================
// DÉCOUPAGE
void AnalysisView::refresh()
{
    if (processor.engine.getLatestAnalysis (snap, version))
        version = snap.version;
    repaint();
}

void AnalysisView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::bg.withAlpha (0.55f));
    g.fillRoundedRectangle (r, 8.0f);
    auto a = r.reduced (12.0f, 8.0f);

    if (snap.notes.empty() || snap.peaks.empty())
    {
        hint (g, a, u8 ("Joue une phrase : son découpage en notes apparaîtra ici.\nLes réglages à droite le modifient en direct."));
        return;
    }

    int legatoCuts = 0, chords = 0;
    for (auto& m : snap.notes) { legatoCuts += m.legato ? 1 : 0; chords += m.poly ? 1 : 0; }

    auto head = a.removeFromTop (16.0f);
    g.setFont (theme::font (11.0f));
    g.setColour (theme::dim);
    g.drawText (u8 ("dernière capture · ") + juce::String (snap.seconds, 1) + " s · " + juce::String ((int) snap.notes.size()) + " notes"
                + (legatoCuts > 0 ? juce::String (" · ") + juce::String (legatoCuts) + " legato" : juce::String())
                + (chords > 0 ? juce::String (" · ") + juce::String (chords) + (chords > 1 ? " accords" : " accord") : juce::String()),
                head.toNearestInt(), juce::Justification::centredLeft);

    // légende
    {
        auto leg = head.removeFromRight (260.0f);
        auto item = [&] (const juce::String& t, std::function<void (juce::Rectangle<float>)> icon)
        {
            auto box = leg.removeFromLeft (86.0f);
            icon (box.removeFromLeft (12.0f));
            g.setColour (theme::faint);
            g.drawText (t, box.withTrimmedLeft (4.0f).toNearestInt(), juce::Justification::centredLeft);
        };
        item ("attaque", [&] (juce::Rectangle<float> b) { g.setColour (theme::text.withAlpha (0.8f)); g.fillRect (b.getCentreX() - 1.0f, b.getY() + 2.0f, 2.0f, b.getHeight() - 4.0f); });
        item ("legato",  [&] (juce::Rectangle<float> b) { g.setColour (theme::decoupe); for (float y = b.getY() + 2.0f; y < b.getBottom() - 2.0f; y += 4.0f) g.fillRect (b.getCentreX() - 1.0f, y, 2.0f, 2.0f); });
        item ("accord",  [&] (juce::Rectangle<float> b) { g.setColour (theme::violon); for (int i = 0; i < 3; ++i) g.fillEllipse (b.getCentreX() - 2.0f, b.getY() + 2.0f + (float) i * 4.0f, 4.0f, 4.0f); });
    }
    a.removeFromTop (4.0f);

    auto labels = a.removeFromTop (16.0f);
    auto wave = a;

    // segments de notes
    for (size_t i = 0; i < snap.notes.size(); ++i)
    {
        const auto& m = snap.notes[i];
        const float x0 = wave.getX() + wave.getWidth() * m.start;
        const float x1 = wave.getX() + wave.getWidth() * juce::jmin (1.0f, m.start + m.length);
        g.setColour ((m.poly ? theme::violon : theme::decoupe).withAlpha (i % 2 == 0 ? 0.09f : 0.05f));
        g.fillRect (juce::Rectangle<float> (x0, wave.getY(), x1 - x0, wave.getHeight()));

        // étiquette : note, ou accord (note la plus grave)
        if (x1 - x0 > 22.0f)
        {
            juce::String t = m.midi >= 0.0f ? noteName ((int) std::round (m.midi)) + juce::String ((int) std::round (m.midi) / 12 - 1) : juce::String (u8 ("·"));
            auto lb = juce::Rectangle<float> (x0 + 2.0f, labels.getY(), x1 - x0 - 4.0f, labels.getHeight());
            if (m.poly)
            {
                g.setColour (theme::violon);
                for (int k = 0; k < 3; ++k) g.fillEllipse (lb.getX() + 1.0f, lb.getY() + 2.0f + (float) k * 4.5f, 3.5f, 3.5f);
                lb.removeFromLeft (8.0f);
            }
            g.setColour (m.poly ? theme::violon : theme::text.withAlpha (0.85f));
            g.setFont (theme::font (10.5f, true));
            g.drawText (t, lb.toNearestInt(), juce::Justification::centredLeft, true);
        }
    }

    // forme d'onde
    {
        const float bw = wave.getWidth() / (float) snap.peaks.size();
        g.setColour (theme::text.withAlpha (0.55f));
        for (size_t c = 0; c < snap.peaks.size(); ++c)
        {
            const float hh = juce::jmax (0.6f, snap.peaks[c] * wave.getHeight() * 0.46f);
            g.fillRect (wave.getX() + bw * (float) c, wave.getCentreY() - hh, juce::jmax (1.0f, bw - 0.3f), hh * 2.0f);
        }
    }

    // coupes
    for (size_t i = 1; i < snap.notes.size(); ++i)
    {
        const auto& m = snap.notes[i];
        const float x = wave.getX() + wave.getWidth() * m.start;
        if (m.legato)
        {
            g.setColour (theme::decoupe);
            for (float y = wave.getY(); y < wave.getBottom(); y += 6.0f) g.fillRect (x - 1.0f, y, 2.0f, 3.0f);
        }
        else
        {
            g.setColour (theme::text.withAlpha (0.85f));
            g.fillRect (x - 0.75f, wave.getY(), 1.5f, wave.getHeight());
        }
    }
}
