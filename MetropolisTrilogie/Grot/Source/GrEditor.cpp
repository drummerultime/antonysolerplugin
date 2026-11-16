#include "GrEditor.h"
#include "Gui/MetropolisArt.h"
#include "GrPresets.h"

namespace gr
{
namespace col = alea::gui::col;
using alea::gui::font;

static void bindK (alea::gui::RndKnob& k, GrotProcessor& p, const juce::String& id, const juce::String& label,
                   const juce::String& jitterId = {}, std::atomic<bool>* lock = nullptr, std::atomic<float>* live = nullptr)
{
    k.setProcessor (&p);
    k.bind (p.apvts.getParameter (id), jitterId.isEmpty() ? nullptr : p.apvts.getParameter (jitterId), lock, live, label);
}

static void styleButton (juce::TextButton& b, juce::Colour c)
{
    b.setColour (juce::TextButton::buttonColourId, col::panelHi);
    b.setColour (juce::TextButton::buttonOnColourId, c);
    b.setColour (juce::TextButton::textColourOffId, col::text);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}

// ===========================================================================
GrLookAndFeel::GrLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, col::panelHi);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::textDim);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent.withAlpha (0.8f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::TextButton::buttonColourId, col::panelHi);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::Slider::backgroundColourId, col::line);
    setColour (juce::Slider::trackColourId, col::rnd);
    setColour (juce::Slider::thumbColourId, col::text);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxBackgroundColourId, col::panelHi);
    setColour (juce::Slider::textBoxOutlineColourId, col::line);
    setColour (juce::TabbedButtonBar::tabTextColourId, col::textDim);
    setColour (juce::TabbedButtonBar::frontTextColourId, col::text);
    setColour (juce::TabbedComponent::backgroundColourId, col::panel);
    setColour (juce::TabbedComponent::outlineColourId, col::line);
    setColour (juce::TextEditor::backgroundColourId, col::panel);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
}

void GrLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (over) c = c.brighter (0.12f);
    if (down) c = c.brighter (0.25f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}
juce::Font GrLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (13.0f, (float) h * 0.55f), true); }
juce::Font GrLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (13.0f); }
juce::Font GrLookAndFeel::getPopupMenuFont() { return font (14.0f); }

// ===========================================================================
SlotPanel::SlotPanel (GrotProcessor& p, int slot) : proc (p), s (slot)
{
    type.addItemList (typeNames(), 1);
    addAndMakeVisible (type);
    typeAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, sid (s, "type"), type);
    type.setTooltip ("Moteur de ce slot");

    styleButton (on, col::track (s));
    on.setClickingTogglesState (true);
    onAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, sid (s, "on"), on);
    on.setTooltip ("Active / contourne le slot");
    addAndMakeVisible (on);

    styleButton (lockBtn, col::lock);
    lockBtn.setButtonText ("L");
    lockBtn.setClickingTogglesState (true);
    lockBtn.setTooltip ("Verrouille le type du slot (MUTATION ne le change pas)");
    lockBtn.onClick = [this] { proc.slotLock[s].store (lockBtn.getToggleState()); };
    addAndMakeVisible (lockBtn);

    styleButton (diceBtn, col::rnd);
    diceBtn.setTooltip ("De : re-tire les reglages du slot (Shift-clic : change aussi le moteur)");
    diceBtn.onClick = [this] { proc.dice (s, juce::ModifierKeys::getCurrentModifiers().isShiftDown()); };
    addAndMakeVisible (diceBtn);

    left.setTooltip ("Deplace le slot vers la gauche (plus tot dans la chaine)");
    right.setTooltip ("Deplace le slot vers la droite (plus tard dans la chaine)");
    left.onClick = [this] { proc.moveSlot (s, s - 1); };
    right.onClick = [this] { proc.moveSlot (s, s + 1); };
    left.setEnabled (s > 0); right.setEnabled (s < kNumSlots - 1);
    addAndMakeVisible (left); addAndMakeVisible (right);

    for (int k = 0; k < kNumSP; ++k)
    {
        knobs[k].setAccentColour (k == kMix ? col::text : (k == kChance ? col::plock : col::track (s)));
        knobs[k].setChaosSource (&proc.chaosView);
        addAndMakeVisible (knobs[k]);
    }
    activity.painter = [this] (juce::Graphics& g)
    {
        auto b = activity.getLocalBounds().toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (b, 4.0f);
        const int n = (int) hist.size();
        const float w = b.getWidth() / (float) n;
        g.setColour (col::track (s).withAlpha (0.85f));
        for (int i = 0; i < n; ++i)
        {
            const float v = hist[(size_t) ((histPos + i) % n)];
            const float h = juce::jlimit (0.0f, 1.0f, std::sqrt (v)) * (b.getHeight() - 4.0f);
            g.fillRect (b.getX() + (float) i * w, b.getBottom() - 2.0f - h, juce::jmax (1.0f, w - 0.5f), h);
        }
    };
    addAndMakeVisible (activity);
    rebind();
}

void SlotPanel::rebind()
{
    shownType = proc.slotType (s);
    for (int k = 0; k < kNumSP; ++k)
        bindK (knobs[k], proc, pid (s, k), paramLabel (shownType, k), jid (s, k), &proc.lockP[s][k], &proc.liveP[s][k]);
    const float alpha = (shownType == 0 || ! on.getToggleState()) ? 0.35f : 1.0f;
    for (auto& k : knobs) k.setAlpha (alpha);
    repaint();
}

void SlotPanel::refresh()
{
    if (proc.slotType (s) != shownType) rebind();
    for (auto& k : knobs) k.refreshLive();
    hist[(size_t) histPos] = proc.slotMeter[s].load();
    histPos = (histPos + 1) % (int) hist.size();
    activity.repaint();
    const float alpha = (shownType == 0 || ! on.getToggleState()) ? 0.35f : 1.0f;
    if (knobs[0].getAlpha() != alpha) { for (auto& k : knobs) k.setAlpha (alpha); activity.setAlpha (alpha); repaint(); }
    const bool lk = proc.slotLock[s].load();
    if (lk != lastLock || lockBtn.getToggleState() != lk) { lastLock = lk; lockBtn.setToggleState (lk, juce::dontSendNotification); }
}

void SlotPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (col::track (s));
    g.fillRoundedRectangle (b.getX(), b.getY(), 4.0f, b.getHeight(), 2.0f);
    g.setColour (col::text);
    g.setFont (font (16.0f, true));
    g.drawText (juce::String (s + 1), juce::Rectangle<float> (8, 4, 20, 22), juce::Justification::centred);
    g.setColour (col::textDim);
    g.setFont (font (11.0f));
    g.drawFittedText (typeHelp (shownType), juce::Rectangle<int> (10, getHeight() - 36, getWidth() - 20, 32), juce::Justification::centredLeft, 2);
    if (shownType == 0 || ! on.getToggleState())
    {
        g.setColour (col::bg.withAlpha (0.45f));
        g.fillRoundedRectangle (b.withTrimmedTop (30.0f), 6.0f);
    }
}

void SlotPanel::resized()
{
    int x = 30;
    type.setBounds (x, 4, 150, 22); x += 154;
    on.setBounds (x, 4, 40, 22); x += 44;
    lockBtn.setBounds (x, 4, 24, 22); x += 28;
    diceBtn.setBounds (x, 4, 36, 22); x += 40;
    left.setBounds (x, 4, 24, 22); x += 26;
    right.setBounds (x, 4, 24, 22);
    const int kw = (getWidth() - 16) / 4, kh = 86;
    for (int k = 0; k < 4; ++k) knobs[k].setBounds (8 + k * kw, 32, kw, kh);
    for (int k = 4; k < 8; ++k) knobs[k].setBounds (8 + (k - 4) * kw, 32 + kh + 2, kw, kh);
    activity.setBounds (x + 30, 6, getWidth() - x - 38, 18);
}

// ===========================================================================
ModPage::ModPage (GrotProcessor& p) : proc (p)
{
    for (int m = 0; m < 8; ++m)
    {
        disp[m] = std::make_unique<alea::gui::ModDisplay> (proc.modView[m]);
        addAndMakeVisible (*disp[m]);
    }
    for (int m = 0; m < kNumRMods; ++m)
    {
        rate[m].addItemList (alea::modRateNames(), 1);
        addAndMakeVisible (rate[m]);
        rateAtt[m] = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "modrate" + juce::String (m + 1), rate[m]);
        bindK (modP[m], proc, "modp" + juce::String (m + 1), alea::modParamNames()[m]);
        modP[m].setAccentColour (col::rnd);
        addAndMakeVisible (modP[m]);
    }
    bindK (envSens, proc, "envsens", "Sensib.");
    bindK (envRel, proc, "envrel", "Release");
    envSens.setAccentColour (col::rnd); envRel.setAccentColour (col::rnd);
    bindK (hitDec, proc, "hitdec", "Decay coup");
    hitDec.setAccentColour (col::rnd);
    addAndMakeVisible (hitDec);
    addAndMakeVisible (envSens); addAndMakeVisible (envRel);

    for (int r = 0; r < kNumRows; ++r)
    {
        const juce::String n = "row" + juce::String (r + 1);
        src[r].addItemList (modSourceNames(), 1);
        dst[r].addItemList (modDestNames(), 1);
        addAndMakeVisible (src[r]); addAndMakeVisible (dst[r]);
        srcAtt[r] = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, n + "_src", src[r]);
        dstAtt[r] = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, n + "_dst", dst[r]);
        amt[r].setSliderStyle (juce::Slider::LinearBar);
        amt[r].setTextBoxStyle (juce::Slider::TextBoxLeft, false, 0, 0);
        amt[r].setColour (juce::Slider::trackColourId, col::rnd.withAlpha (0.6f));
        addAndMakeVisible (amt[r]);
        amtAtt[r] = std::make_unique<APVTS::SliderAttachment> (proc.apvts, n + "_amt", amt[r]);
        amt[r].setTooltip ("Quantite de modulation (double-clic : 0)");
        amt[r].setDoubleClickReturnValue (true, 0.0);
    }
    updateDestNames();
}

void ModPage::updateDestNames()
{
    int key = 0;
    for (int s = 0; s < kNumSlots; ++s) key = key * 17 + proc.slotType (s);
    if (key == typesKey) return;
    typesKey = key;
    for (int r = 0; r < kNumRows; ++r)
        for (int s = 0; s < kNumSlots; ++s)
        {
            const int t = proc.slotType (s);
            for (int p = 0; p < kNumSP; ++p)
                dst[r].changeItemText (2 + s * kNumSP + p, "S" + juce::String (s + 1) + " " + (t == 0 ? juce::String ("(vide)") : typeShort()[t]) + " " + paramLabel (t, p));
        }
    for (int r = 0; r < kNumRows; ++r)
        if (dst[r].getSelectedItemIndex() >= 0) dst[r].setText (dst[r].getItemText (dst[r].getSelectedItemIndex()), juce::dontSendNotification);
}

void ModPage::refresh()
{
    for (auto& d : disp) d->push();
    updateDestNames();
}

void ModPage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::text);
    g.setFont (font (12.0f, true));
    static const char* names[8] { "S&H", "SMOOTH", "TURING", "CHAOS", "LFO", "ENVELOPPE", "HORLOGE", "COUP" };
    for (int m = 0; m < 8; ++m) g.drawText (names[m], 8 + m * 78, 4, 76, 16, juce::Justification::centredLeft);
    g.drawText ("MATRICE DE MODULATION  (source -> destination, quantite)", 640, 4, 560, 16, juce::Justification::centredLeft);
    g.setColour (col::line);
    g.drawVerticalLine (628, 4.0f, (float) getHeight() - 4.0f);
}

void ModPage::resized()
{
    for (int m = 0; m < 8; ++m) disp[m]->setBounds (8 + m * 78, 22, 72, 46);
    for (int m = 0; m < kNumRMods; ++m)
    {
        rate[m].setBounds (8 + m * 78, 72, 72, 22);
        modP[m].setBounds (8 + m * 78 + 2, 98, 68, 80);
    }
    envSens.setBounds (8 + 5 * 78 - 4, 72, 50, 70);
    envRel.setBounds (8 + 5 * 78 + 40, 72, 50, 70);
    hitDec.setBounds (8 + 7 * 78 - 2, 72, 68, 80);
    const int x0 = 640, colW = (getWidth() - x0 - 8) / 2;
    for (int r = 0; r < kNumRows; ++r)
    {
        const int cx = x0 + (r / 4) * colW, y = 24 + (r % 4) * 40;
        src[r].setBounds (cx, y, 100, 24);
        dst[r].setBounds (cx + 104, y, colW - 200, 24);
        amt[r].setBounds (cx + colW - 92, y, 84, 24);
    }
}

// ===========================================================================
MacroPage::MacroPage (GrotProcessor& p) : proc (p)
{
    for (int m = 0; m < kNumMacros; ++m)
    {
        bindK (knobs[m], proc, "macro" + juce::String (m + 1), "Macro " + juce::String (m + 1));
        knobs[m].setAccentColour (col::track (m));
        addAndMakeVisible (knobs[m]);
    }
}

void MacroPage::refresh()
{
    for (auto& k : knobs) k.refreshLive();
    const auto dn = modDestNames();
    juce::String r[kNumMacros];
    for (int row = 0; row < kNumRows; ++row)
    {
        const int si = (int) std::round (proc.apvts.getRawParameterValue ("row" + juce::String (row + 1) + "_src")->load());
        const int di = (int) std::round (proc.apvts.getRawParameterValue ("row" + juce::String (row + 1) + "_dst")->load());
        const float a = proc.apvts.getRawParameterValue ("row" + juce::String (row + 1) + "_amt")->load();
        if (si >= kSrcMacro && di > 0 && std::abs (a) > 0.001f)
        {
            juce::String d = dn[di];
            if (di >= 1 && di <= kNumSlots * kNumSP)
            {
                const int s = (di - 1) / kNumSP, p = (di - 1) % kNumSP, t = proc.slotType (s);
                d = "S" + juce::String (s + 1) + " " + paramLabel (t, p);
            }
            r[si - kSrcMacro] << (r[si - kSrcMacro].isEmpty() ? "" : ", ") << d << (a >= 0 ? " +" : " ") << juce::String ((int) std::round (a * 100)) << "%";
        }
    }
    bool changed = false;
    for (int m = 0; m < kNumMacros; ++m) if (r[m] != routing[m]) { routing[m] = r[m]; changed = true; }
    if (changed) repaint();
}

void MacroPage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::textDim);
    g.setFont (font (12.0f));
    g.drawText ("Clic droit sur une macro -> MIDI Learn, puis bouge un fader / potard de la Wing. Les macros agissent via la matrice (onglet MODULATION).",
                10, getHeight() - 26, getWidth() - 20, 20, juce::Justification::centredLeft);
    const int w = getWidth() / kNumMacros;
    g.setFont (font (11.0f));
    for (int m = 0; m < kNumMacros; ++m)
    {
        g.setColour (routing[m].isEmpty() ? col::textDim.withAlpha (0.5f) : col::rnd);
        g.drawFittedText (routing[m].isEmpty() ? juce::String ("(non routee)") : "-> " + routing[m],
                          m * w + 6, 128, w - 12, 50, juce::Justification::centredTop, 3);
    }
}

void MacroPage::resized()
{
    const int w = getWidth() / kNumMacros;
    for (int m = 0; m < kNumMacros; ++m) knobs[m].setBounds (m * w + (w - 100) / 2, 14, 100, 110);
}

// ===========================================================================
ScenePage::ScenePage (GrotProcessor& p) : proc (p)
{
    for (int i = 0; i < kNumScenes; ++i)
    {
        scene[i].setButtonText (juce::String (i + 1));
        styleButton (scene[i], col::accent);
        scene[i].setTooltip ("Clic : rappelle la scene (morph) | en mode MEMORISER : enregistre l'etat actuel | note MIDI " + juce::MidiMessage::getMidiNoteName (36 + i, true, true, 3));
        scene[i].onClick = [this, i]
        {
            if (storeBtn.getToggleState()) { proc.storeScene (i); storeBtn.setToggleState (false, juce::dontSendNotification); }
            else proc.recallScene (i);
        };
        addAndMakeVisible (scene[i]);
    }
    styleButton (storeBtn, col::lock);
    storeBtn.setClickingTogglesState (true);
    storeBtn.setTooltip ("Arme l'enregistrement : le prochain clic sur une scene y memorise l'etat actuel");
    addAndMakeVisible (storeBtn);
    styleButton (varBtn, col::rnd);
    varBtn.setTooltip ("Scene 1 = etat actuel, scenes 2 a 8 = variations de plus en plus eloignees (selon les cadenas)");
    varBtn.onClick = [this] { proc.generateSceneVariations(); };
    addAndMakeVisible (varBtn);
    bindK (morph, proc, "morph", "Morph");
    morph.setAccentColour (col::accent);
    addAndMakeVisible (morph);
}

void ScenePage::refresh()
{
    const int a = proc.activeScene();
    for (int i = 0; i < kNumScenes; ++i)
    {
        const bool st = proc.sceneStored (i);
        scene[i].setColour (juce::TextButton::buttonColourId, i == a ? col::accent.withAlpha (0.85f) : (st ? col::panelHi.brighter (0.15f) : col::bg));
        scene[i].setColour (juce::TextButton::textColourOffId, i == a ? juce::Colours::black : (st ? col::text : col::textDim));
    }
    morph.refreshLive();
}

void ScenePage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::textDim);
    g.setFont (font (12.0f));
    g.drawFittedText ("Les scenes memorisent les 6 slots + le hasard global. Le rappel glisse en douceur pendant le temps de Morph "
                      "(les moteurs changent instantanement en fondu). MIDI : notes C1 a G1 (36-43) = scenes 1 a 8, "
                      "G#1 (44) = DE, A1 (45) = MUTATION, A#1 (46) = FREEZE on/off.",
                      10, getHeight() - 48, getWidth() - 20, 42, juce::Justification::centredLeft, 2);
}

void ScenePage::resized()
{
    const int w = 112;
    for (int i = 0; i < kNumScenes; ++i) scene[i].setBounds (10 + i * (w + 8), 14, w, 72);
    storeBtn.setBounds (10, 100, 140, 30);
    varBtn.setBounds (160, 100, 150, 30);
    morph.setBounds (10 + 8 * (w + 8) + 10, 10, 90, 100);
}

// ===========================================================================
SourcePage::SourcePage (GrotProcessor& p) : proc (p)
{
    bindK (sens, proc, "sens", "Sensibilite");
    sens.setAccentColour (juce::Colour (0xffe2464b));
    bindK (hitDec, proc, "hitdec", "Decay coup");
    hitDec.setAccentColour (col::rnd);
    styleButton (adapt, juce::Colour (0xffe2464b));
    adapt.setTooltip ("Construit une chaine de depart pensee pour la source choisie (+ 8 scenes de variations)");
    adapt.onClick = [this] { proc.adaptToSource(); };
    for (juce::Component* c : { (juce::Component*) &sens, (juce::Component*) &hitDec, (juce::Component*) &adapt }) addAndMakeVisible (c);
    rateStart = juce::Time::getMillisecondCounterHiRes();
}

void SourcePage::refresh()
{
    sens.refreshLive();
    const int h = proc.hitCountView.load();
    if (h != lastHits) { rateHits += h - lastHits; lastHits = h; }
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now - rateStart > 4000.0)
    {
        hitsPerMin = (float) rateHits * 60000.0f / (float) (now - rateStart);
        rateHits = 0; rateStart = now;
    }
    if (isShowing()) repaint();
}

void SourcePage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    auto sc = juce::Rectangle<float> (10.0f, 10.0f, (float) getWidth() - 420.0f, (float) getHeight() - 20.0f);
    g.setColour (col::bg);
    g.fillRoundedRectangle (sc, 5.0f);
    const int N = (int) proc.scopePk.size();
    const int w0 = proc.scopeW.load();
    const float bw = sc.getWidth() / (float) N;
    for (int i = 0; i < N; ++i)
    {
        const int k = (w0 + 1 + i) % N;
        const float v = proc.scopePk[(size_t) k].load();
        const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (v + 1.0e-6f));
        const float hgt = (db + 60.0f) / 60.0f * (sc.getHeight() - 8.0f);
        const float x = sc.getX() + (float) i * bw;
        g.setColour (col::rnd.withAlpha (0.7f));
        g.fillRect (x, sc.getCentreY() - hgt * 0.5f, juce::jmax (1.0f, bw - 0.4f), hgt);
        const float hv = proc.scopeHit[(size_t) k].load();
        if (hv > 0.0f)
        {
            g.setColour (juce::Colour (0xffe2464b));
            g.fillRect (x, sc.getY() + 2.0f, 2.0f, sc.getHeight() - 4.0f);
            g.fillEllipse (x - 3.0f, sc.getY() + 4.0f + (1.0f - hv) * 30.0f, 8.0f, 8.0f);
        }
    }
    g.setColour (col::textDim);
    g.setFont (font (11.0f));
    g.drawText ("entree (~2 s)  |  traits rouges = coups detectes (hauteur du point = velocite)", sc.reduced (8.0f, 4.0f), juce::Justification::bottomLeft);

    const auto& pr = sourceProfile (proc.sourceIndex());
    static const char* fam[] { "grosse caisse", "caisse claire", "tom", "metal", "peau jouee a la main", "bus / micros d'ensemble", "gong / bol" };
    auto r = juce::Rectangle<int> (getWidth() - 400, 12, 230, getHeight() - 24);
    g.setColour (col::text);
    g.setFont (font (16.0f, true));
    g.drawText (pr.name, r.removeFromTop (22), juce::Justification::centredLeft);
    g.setFont (font (12.0f));
    g.setColour (col::textDim);
    auto line = [&] (const juce::String& s) { g.drawText (s, r.removeFromTop (17), juce::Justification::centredLeft); };
    line (juce::String ("famille : ") + fam[juce::jlimit (0, 6, pr.family)]);
    line ("detection : " + juce::String ((int) pr.lo) + " Hz - " + juce::String (pr.hi / 1000.0f, 1) + " kHz");
    line ("fondamentale : " + juce::String ((int) pr.fund) + " Hz (Sub, Corps, Resonator)");
    line ("corps / separation : " + juce::String ((int) pr.body) + " Hz");
    line ("anti-rebond : " + juce::String ((int) pr.refracMs) + " ms");
    g.setColour (col::text);
    line ("coups : " + juce::String (lastHits) + "   (" + juce::String ((int) hitsPerMin) + " / min)");
}

void SourcePage::resized()
{
    sens.setBounds (getWidth() - 160, 10, 74, 90);
    hitDec.setBounds (getWidth() - 84, 10, 74, 90);
    adapt.setBounds (getWidth() - 166, 120, 156, 44);
}

// ===========================================================================
static const char* kHelp =
"GROT - sound design pour batterie et percussions (micro de grosse caisse, caisse claire, overheads, mains, metal...)\n\n"
"SOURCE : choisis ce qui entre dans le micro. Le profil regle la detection des coups (bande, anti-rebond) et accorde les moteurs "
"(Sub, Corps, Resonator, separations grave / aigu). ADAPTER construit une chaine de depart pensee pour la source + 8 scenes. "
"Onglet SOURCE : visualisation des coups detectes et SENSIBILITE.\n\n"
"CHAINE : 6 slots en serie (1-2-3 puis 4-5-6). Mix du slot = dosage. CHANCE/COUP : a chaque coup detecte, le slot est active ou non "
"au hasard (100 % = toujours). Sur un delay ou une reverbe c'est un envoi (les queues continuent) : envois dub aleatoires ; "
"sur un traitement (distorsion, filtre...) le coup passe traite ou non ; sur Sub et Couche bruit c'est le declenchement.\n\n"
"POTARDS : glisser = valeur ; clic droit-glisser ou Cmd-glisser (ou RND EDIT) = quantite de hasard (anneau turquoise) ; point blanc = valeur jouee ; "
"cadenas = protege du DE / MUTATION / variations / evolve ; clic droit = MIDI Learn ; double-clic = defaut.\n\n"
"HASARD : CHAOS multiplie les anneaux. HORLOGE : Chaque coup / 1 coup sur 2 / 1 sur 4 = nouveaux tirages sur les frappes (le son change a chaque coup), "
"ou au tempo (1/16 a 4 bars), ou Libre. GLIDE adoucit. SEED = suite de hasard reproductible. EVOLVE = derive lente. "
"Les reglages 'Rnd coup' des moteurs ajoutent leur propre variation a chaque frappe.\n\n"
"FREEZE fige les moteurs temporels (delay, scatter, stutter, granular, reverbs, reverse, resonator).\n\n"
"MODULATION : S&H, Smooth, Turing, Chaos, LFO, Enveloppe, Horloge, COUP (enveloppe declenchee par chaque frappe), VELOCITE, 8 macros (Wing).\n\n"
"MOTEURS - fins : Transient, Smash (compression parallele), Filtre, Modulation, Reverb Room. "
"Organiques : Corps modal (membrane, bois, metal, cloche, verre), Resonator, Granular, Reverse. "
"Electroniques : Sub 808, Couche bruit (timbre, metal 808), Ring / Shift, Gate, Stutter, Scatter, Reverb gated 80s / non-lin. "
"Extremes : Distorsion x2 (tape, tube, fuzz, fold, rectifier), Degrade, Pitch.\n\n"
"MIDI : notes 36-43 = scenes, 44 = DE, 45 = MUTATION, 46 = FREEZE ; CC par MIDI Learn.\n";

GrotView::GrotView (GrotProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    alea::gui::RndKnob::editRandomMode = false;

    sourceBox.addItemList (sourceNames(), 1);
    sourceAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "source", sourceBox);
    sourceBox.setTooltip ("Source jouee dans le micro : regle la detection des coups et l'accord des moteurs");
    addAndMakeVisible (sourceBox);
    styleButton (adaptBtn, juce::Colour (0xffe2464b));
    adaptBtn.setTooltip ("Construit une chaine de depart pour cette source (+ 8 scenes de variations)");
    adaptBtn.onClick = [this] { proc.adaptToSource(); };
    addAndMakeVisible (adaptBtn);

    fillPresetBox();
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id <= numFactoryPresets()) { proc.loadFactory (id - 1); lastProgram = id - 1; }
        else if (id >= 1000 && id - 1000 < userFiles.size()) loadPresetFile (userFiles[id - 1000]);
    };
    presetBox.setTooltip ("Presets d'usine et presets utilisateur");
    addAndMakeVisible (presetBox);
    prevBtn.onClick = [this] { proc.loadFactory ((proc.getCurrentProgram() + numFactoryPresets() - 1) % numFactoryPresets()); };
    nextBtn.onClick = [this] { proc.loadFactory ((proc.getCurrentProgram() + 1) % numFactoryPresets()); };
    saveBtn.onClick = [this] { savePreset(); };
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Ouvrir un preset Grot", presetDir(), "*.grp");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadPresetFile (fc.getResult()); });
    };
    undoBtn.onClick = [this] { proc.undo(); };
    for (auto* b : { &prevBtn, &nextBtn, &saveBtn, &loadBtn, &undoBtn }) addAndMakeVisible (*b);

    styleButton (diceBtn, col::rnd);
    diceBtn.onClick = [this] { proc.dice (-1, false); };
    diceBtn.setTooltip ("Re-tire les reglages de toute la chaine (quantite selon CHAOS, cadenas respectes) - note MIDI 44");
    styleButton (mutateBtn, col::rnd);
    mutateBtn.onClick = [this] { proc.dice (-1, true); };
    mutateBtn.setTooltip ("Change aussi les moteurs (sauf slots verrouilles L) - note MIDI 45");
    styleButton (rndEditBtn, col::rnd);
    rndEditBtn.setClickingTogglesState (true);
    rndEditBtn.onClick = [this] { alea::gui::RndKnob::editRandomMode = rndEditBtn.getToggleState(); repaint(); };
    rndEditBtn.setTooltip ("Les potards reglent la quantite de hasard au lieu de la valeur");
    styleButton (freezeBtn, juce::Colour (0xff7fd3ff));
    freezeBtn.setClickingTogglesState (true);
    freezeAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "freeze", freezeBtn);
    freezeBtn.setTooltip ("Gele les moteurs temporels - note MIDI 46");
    styleButton (panicBtn, col::lock);
    panicBtn.setColour (juce::TextButton::buttonColourId, col::lock.withAlpha (0.55f));
    panicBtn.setTooltip ("Coupe en fondu toutes les queues, le freeze, puis repart a zero - note MIDI 47");
    panicBtn.onClick = [this] { proc.panic(); };
    addAndMakeVisible (panicBtn);
    styleButton (seedBtn, col::rnd);
    seedBtn.onClick = [this] { proc.newSeed(); };
    for (auto* b : { &diceBtn, &mutateBtn, &rndEditBtn, &freezeBtn, &seedBtn }) addAndMakeVisible (*b);

    clockBox.addItemList (clockNames(), 1);
    clockAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "clock", clockBox);
    clockBox.setTooltip ("Horloge du hasard : cadence des nouveaux tirages");
    evoBarsBox.addItemList (evolveBarNames(), 1);
    evoAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "evobars", evoBarsBox);
    evoBarsBox.setTooltip ("Periode de l'evolution");
    addAndMakeVisible (clockBox); addAndMakeVisible (evoBarsBox);

    seedSlider.setSliderStyle (juce::Slider::IncDecButtons);
    seedSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 22);
    seedAtt = std::make_unique<APVTS::SliderAttachment> (proc.apvts, "seed", seedSlider);
    seedSlider.setTooltip ("Seed du hasard : meme seed = meme suite de valeurs");
    addAndMakeVisible (seedSlider);

    bindK (chaosK, proc, "chaos", "Chaos");   chaosK.setAccentColour (col::rnd);
    bindK (rateK, proc, "rate", "Rate");      rateK.setAccentColour (col::rnd);
    bindK (glideK, proc, "glide", "Glide");   glideK.setAccentColour (col::rnd);
    bindK (evolveK, proc, "evolve", "Evolve"); evolveK.setAccentColour (col::rnd);
    bindK (mixK, proc, "mix", "Mix");
    bindK (inK, proc, "in", "In");
    bindK (outK, proc, "out", "Out");
    bindK (widthK, proc, "width", "Largeur");
    bindK (duckK, proc, "duck", "Duck");
    bindK (bpmK, proc, "bpm", "BPM int.");
    for (auto* k : { &chaosK, &rateK, &glideK, &evolveK, &mixK, &inK, &outK, &widthK, &duckK, &bpmK }) addAndMakeVisible (*k);

    for (int s = 0; s < kNumSlots; ++s) { slots[s] = std::make_unique<SlotPanel> (proc, s); addAndMakeVisible (*slots[s]); }

    modPage = new ModPage (proc);
    macroPage = new MacroPage (proc);
    scenePage = new ScenePage (proc);
    sourcePage = new SourcePage (proc);
    help.setMultiLine (true);
    help.setReadOnly (true);
    help.setScrollbarsShown (true);
    help.setFont (font (13.0f));
    help.setText (kHelp);
    tabs.addTab ("SOURCE / COUPS", col::panel, sourcePage, true);
    tabs.addTab ("MODULATION", col::panel, modPage, true);
    tabs.addTab ("MACROS (WING)", col::panel, macroPage, true);
    tabs.addTab ("SCENES", col::panel, scenePage, true);
    tabs.addTab ("AIDE", col::panel, &help, false);
    tabs.setTabBarDepth (26);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);

    setSize (1294, 892);
    startTimerHz (30);
}

GrotView::~GrotView()
{
    stopTimer();
    alea::gui::RndKnob::editRandomMode = false;
    setLookAndFeel (nullptr);
}

juce::File GrotView::presetDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Grot Presets");
    d.createDirectory();
    return d;
}

void GrotView::fillPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < numFactoryPresets(); ++i) presetBox.addItem (factoryPresetName (i), i + 1);
    userFiles = presetDir().findChildFiles (juce::File::findFiles, false, "*.grp");
    userFiles.sort();
    if (! userFiles.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Mes presets");
        for (int i = 0; i < userFiles.size(); ++i) presetBox.addItem (userFiles[i].getFileNameWithoutExtension(), 1000 + i);
    }
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    lastProgram = proc.getCurrentProgram();
}

void GrotView::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Sauver le preset Grot", presetDir().getChildFile ("Mon preset.grp"), "*.grp");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension ("grp");
        if (auto xml = proc.captureState().createXml()) xml->writeTo (f);
        fillPresetBox();
        for (int i = 0; i < userFiles.size(); ++i) if (userFiles[i] == f) presetBox.setSelectedId (1000 + i, juce::dontSendNotification);
    });
}

void GrotView::loadPresetFile (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        proc.pushUndo();
        proc.restoreState (juce::ValueTree::fromXml (*xml));
    }
}

void GrotView::timerCallback()
{
    for (auto& s : slots) s->refresh();
    for (auto* k : { &chaosK, &rateK, &glideK, &evolveK, &mixK, &inK, &outK, &widthK, &duckK, &bpmK }) k->refreshLive();
    modPage->refresh();
    macroPage->refresh();
    scenePage->refresh();
    sourcePage->refresh();
    const int hc = proc.hitCountView.load();
    if (hc != lastHits) { lastHits = hc; hitFlash = 1.0f; }
    hitFlash *= 0.75f;
    undoBtn.setEnabled (proc.canUndo());
    if (proc.getCurrentProgram() != lastProgram && presetBox.getSelectedId() < 1000)
    {
        lastProgram = proc.getCurrentProgram();
        presetBox.setSelectedId (lastProgram + 1, juce::dontSendNotification);
    }
    rateK.setAlpha (clockBox.getSelectedId() == kClockFree + 1 ? 1.0f : 0.4f);
    const auto t = proc.tickView.load();
    if (t != lastTick) { lastTick = t; tickFlash = 1.0f; }
    tickFlash *= 0.8f;
    meterIn = proc.meterIn.load(); meterOut = proc.meterOut.load();
    repaint (0, 0, getWidth(), 132);
}

void GrotView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    // titre
    alea::gui::metro::drawSkyline (g, juce::Rectangle<float> (0.0f, 4.0f, (float) getWidth(), 48.0f), col::text.withAlpha (0.045f));
    alea::gui::metro::drawTenHourClock (g, { 128.0f, 24.0f }, 15.0f, juce::Colour (0xffe2464b), proc.hitCountView.load(), hitFlash);
    g.setColour (juce::Colour (0xffe2464b));
    g.setFont (font (30.0f, true));
    g.drawText ("GROT", 14, 4, 100, 34, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (10.5f));
    g.drawText ("percussions & batterie", 16, 36, 150, 14, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.drawText ("SOURCE", 172, 1, 80, 12, juce::Justification::centredLeft);
    // voyant de coups
    g.setColour (juce::Colour (0xffe2464b).withAlpha (0.15f + 0.85f * hitFlash));
    g.fillEllipse (428.0f, 19.0f, 12.0f, 12.0f);

    // zones
    g.setColour (col::panel);
    g.fillRoundedRectangle (8.0f, 54.0f, 640.0f, 76.0f, 5.0f);
    g.fillRoundedRectangle (656.0f, 54.0f, 630.0f, 76.0f, 5.0f);
    g.setColour (col::rnd);
    g.setFont (font (10.0f, true));
    g.drawText ("HASARD", 14, 56, 60, 12, juce::Justification::centredLeft);
    g.drawText ("HORLOGE", 82, 62, 80, 12, juce::Justification::centredLeft);
    g.drawText ("EVOLUTION", 360, 62, 80, 12, juce::Justification::centredLeft);
    g.drawText ("SEED", 454, 62, 60, 12, juce::Justification::centredLeft);
    g.setColour (col::text);
    g.drawText ("SORTIE", 662, 56, 60, 12, juce::Justification::centredLeft);

    // voyant d'horloge
    g.setColour (col::rnd.withAlpha (0.15f + 0.85f * tickFlash));
    g.fillEllipse (156.0f, 64.0f, 8.0f, 8.0f);
    // tempo
    g.setColour (col::textDim);
    g.drawText ("tempo " + juce::String (proc.hostBpm(), 1), 1134, 106, 100, 14, juce::Justification::centredLeft);

    // vu-metres
    auto meter = [&] (float x, float v, const char* lab)
    {
        const float h = 46.0f;
        const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (v + 1.0e-6f));
        const float fill = (db + 60.0f) / 60.0f * h;
        g.setColour (col::bg); g.fillRect (x, 62.0f, 10.0f, h);
        g.setColour (v > 0.95f ? col::lock : col::rnd);
        g.fillRect (x, 62.0f + h - fill, 10.0f, fill);
        g.setColour (col::textDim); g.setFont (font (9.0f));
        g.drawText (lab, (int) x - 6, 110, 22, 10, juce::Justification::centred);
    };
    meter (1250.0f, meterIn, "IN");
    meter (1266.0f, meterOut, "OUT");
}

void GrotView::resized()
{
    sourceBox.setBounds (170, 13, 170, 26);
    adaptBtn.setBounds (344, 13, 78, 26);
    presetBox.setBounds (448, 13, 236, 26);
    prevBtn.setBounds (688, 13, 26, 26);
    nextBtn.setBounds (716, 13, 26, 26);
    saveBtn.setBounds (746, 13, 64, 26);
    loadBtn.setBounds (814, 13, 64, 26);
    undoBtn.setBounds (882, 13, 54, 26);
    diceBtn.setBounds (944, 6, 54, 40);
    mutateBtn.setBounds (1002, 6, 86, 40);
    rndEditBtn.setBounds (1092, 6, 84, 40);
    freezeBtn.setBounds (1182, 6, 104, 40);
    panicBtn.setBounds (1132, 62, 108, 40);

    // hasard
    const int ky = 64, kh = 62;
    chaosK.setBounds (12, ky, 62, kh);
    clockBox.setBounds (80, 80, 86, 24);
    rateK.setBounds (176, ky, 58, kh);
    glideK.setBounds (236, ky, 58, kh);
    evolveK.setBounds (296, ky, 58, kh);
    evoBarsBox.setBounds (358, 80, 86, 24);
    seedSlider.setBounds (452, 80, 104, 24);
    seedBtn.setBounds (562, 78, 82, 28);
    // sortie
    int x = 716;
    for (auto* k : { &mixK, &inK, &outK, &widthK, &duckK, &bpmK }) { k->setBounds (x, ky, 62, kh); x += 68; }

    const int sw = 422, sh = 248, y0 = 136;
    for (int s = 0; s < kNumSlots; ++s)
        slots[s]->setBounds (8 + (s % 3) * (sw + 6), y0 + (s / 3) * (sh + 6), sw, sh);
    tabs.setBounds (8, y0 + 2 * (sh + 6), getWidth() - 16, getHeight() - (y0 + 2 * (sh + 6)) - 8);
}

// ===========================================================================
GrotEditor::GrotEditor (GrotProcessor& p) : juce::AudioProcessorEditor (p), proc (p), view (p)
{
    baseW = view.getWidth(); baseH = view.getHeight();
    addAndMakeVisible (view);
    setResizable (true, true);
    if (auto* cn = getConstrainer())
    {
        cn->setFixedAspectRatio ((double) baseW / (double) baseH);
        cn->setSizeLimits ((int) (baseW * 0.42), (int) (baseH * 0.42), (int) (baseW * 1.6), (int) (baseH * 1.6));
    }
    float s = proc.uiScale.load();
    if (s < 0.3f)
    {
        s = 1.0f;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            s = (float) juce::jmin (1.0, (d->userArea.getHeight() - 90.0) / baseH, (d->userArea.getWidth() - 40.0) / baseW);
        s = juce::jlimit (0.42f, 1.0f, s);
    }
    setSize (juce::roundToInt (baseW * s), juce::roundToInt (baseH * s));
}

void GrotEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    view.setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
} // namespace gr
