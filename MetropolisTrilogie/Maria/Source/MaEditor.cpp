#include "MaEditor.h"
#include "Gui/MetropolisArt.h"
#include "MaPresets.h"

namespace ma
{
namespace col = alea::gui::col;
using alea::gui::font;

static void bindK (alea::gui::RndKnob& k, MariaProcessor& p, const juce::String& id, const juce::String& label,
                   const juce::String& jitterId = {}, std::atomic<bool>* lock = nullptr, std::atomic<float>* live = nullptr)
{
    k.setProcessor (&p);
    k.bind (p.apvts.getParameter (id), jitterId.isEmpty() ? nullptr : p.apvts.getParameter (jitterId), lock, live, label);
}

static const juce::Colour kMotifCol[4] { juce::Colour (0xffffc857), juce::Colour (0xff7ee081), juce::Colour (0xff6fb7ff), juce::Colour (0xffff7aa8) };

static void styleButton (juce::TextButton& b, juce::Colour c)
{
    b.setColour (juce::TextButton::buttonColourId, col::panelHi);
    b.setColour (juce::TextButton::buttonOnColourId, c);
    b.setColour (juce::TextButton::textColourOffId, col::text);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}

// ===========================================================================
MaLookAndFeel::MaLookAndFeel()
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

void MaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
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
juce::Font MaLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (13.0f, (float) h * 0.55f), true); }
juce::Font MaLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (13.0f); }
juce::Font MaLookAndFeel::getPopupMenuFont() { return font (14.0f); }

// ===========================================================================
SlotPanel::SlotPanel (MariaProcessor& p, int slot) : proc (p), s (slot)
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
    if (! juce::approximatelyEqual (knobs[0].getAlpha(), alpha)) { for (auto& k : knobs) k.setAlpha (alpha); activity.setAlpha (alpha); repaint(); }
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
    int x = 28;
    const int tw = juce::jmin (150, getWidth() - 214);
    type.setBounds (x, 4, tw, 22); x += tw + 4;
    on.setBounds (x, 4, 36, 22); x += 39;
    lockBtn.setBounds (x, 4, 22, 22); x += 25;
    diceBtn.setBounds (x, 4, 32, 22); x += 35;
    left.setBounds (x, 4, 22, 22); x += 24;
    right.setBounds (x, 4, 22, 22);
    const int kw = (getWidth() - 16) / 4, kh = 82;
    for (int k = 0; k < 4; ++k) knobs[k].setBounds (8 + k * kw, 32, kw, kh);
    for (int k = 4; k < 8; ++k) knobs[k].setBounds (8 + (k - 4) * kw, 32 + kh + 2, kw, kh);
    activity.setBounds (juce::jmin (x + 28, getWidth() - 20), 6, juce::jmax (8, getWidth() - x - 36), 18);
}

// ===========================================================================
ModPage::ModPage (MariaProcessor& p) : proc (p)
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
    bindK (hitDec, proc, "hitdec", "Decay seg.");
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
    static const char* names[8] { "S&H", "SMOOTH", "TURING", "CHAOS", "LFO", "ENVELOPPE", "HORLOGE", "SEGMENT" };
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
MacroPage::MacroPage (MariaProcessor& p) : proc (p)
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
ScenePage::ScenePage (MariaProcessor& p) : proc (p)
{
    for (int i = 0; i < kSceneBank; ++i)
    {
        styleButton (scene[i], col::accent);
        scene[i].setTooltip ("Clic : rappelle la scene (morph) | en mode MEMORISER : enregistre l'etat actuel | note MIDI " + juce::MidiMessage::getMidiNoteName (36 + i, true, true, 3) + " (dans la banque affichee)");
        scene[i].onClick = [this, i]
        {
            if (storeBtn.getToggleState()) { proc.storeScene (sceneAt (i)); storeBtn.setToggleState (false, juce::dontSendNotification); }
            else proc.recallScene (sceneAt (i));
        };
        addAndMakeVisible (scene[i]);
    }
    constexpr int nb = kNumScenes / kSceneBank;
    for (auto* b : { &bankPrev, &bankNext }) { styleButton (*b, col::accent); addAndMakeVisible (*b); }
    bankPrev.setTooltip ("Banque precedente (8 scenes)");
    bankNext.setTooltip ("Banque suivante (8 scenes) : 48 scenes en tout, une par sequence du film");
    bankPrev.onClick = [this] { proc.sceneBank.store ((proc.sceneBank.load() + nb - 1) % nb); refresh(); };
    bankNext.onClick = [this] { proc.sceneBank.store ((proc.sceneBank.load() + 1) % nb); refresh(); };
    bankLbl.setJustificationType (juce::Justification::centred);
    bankLbl.setFont (font (13.0f));
    bankLbl.setColour (juce::Label::textColourId, col::text);
    addAndMakeVisible (bankLbl);
    styleButton (storeBtn, col::lock);
    storeBtn.setClickingTogglesState (true);
    storeBtn.setTooltip ("Arme l'enregistrement : le prochain clic sur une scene y memorise l'etat actuel");
    addAndMakeVisible (storeBtn);
    styleButton (varBtn, col::rnd);
    varBtn.setTooltip ("1re scene de la banque = etat actuel, les 7 suivantes = variations de plus en plus eloignees (selon les cadenas)");
    varBtn.onClick = [this] { proc.generateSceneVariations(); };
    addAndMakeVisible (varBtn);
    bindK (morph, proc, "morph", "Morph");
    morph.setAccentColour (col::accent);
    addAndMakeVisible (morph);
    refresh();
}

void ScenePage::refresh()
{
    const int a = proc.activeScene();
    const int bank = proc.sceneBank.load();
    if (bank != shownBank)
    {
        shownBank = bank;
        for (int i = 0; i < kSceneBank; ++i) scene[i].setButtonText (juce::String (sceneAt (i) + 1));
        bankLbl.setText ("Banque " + juce::String (bank + 1) + "\nscenes " + juce::String (bank * kSceneBank + 1) + "-" + juce::String ((bank + 1) * kSceneBank),
                         juce::dontSendNotification);
    }
    for (int i = 0; i < kSceneBank; ++i)
    {
        const int s = sceneAt (i);
        const bool st = proc.sceneStored (s);
        scene[i].setColour (juce::TextButton::buttonColourId, s == a ? col::accent.withAlpha (0.85f) : (st ? col::panelHi.brighter (0.15f) : col::bg));
        scene[i].setColour (juce::TextButton::textColourOffId, s == a ? juce::Colours::black : (st ? col::text : col::textDim));
    }
    morph.refreshLive();
}

void ScenePage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::textDim);
    g.setFont (font (12.0f));
    g.drawFittedText ("48 scenes (6 banques de 8, une par sequence) : effets, improvisatrice, hasard global, nappe douce. Le rappel glisse pendant le temps de Morph. "
                      "MIDI : 36-43 = scenes de la banque affichee, CC 23 en retour = numero de scene, 44 = DE, 45 = MUTATION, 46 = FREEZE, 48 = PHRASE, 49 = SILENCE, 50 = ECOUTE, 51 = EFFACER, 52-55 / 56-59 = leitmotivs.",
                      10, getHeight() - 48, getWidth() - 20, 42, juce::Justification::centredLeft, 2);
}

void ScenePage::resized()
{
    const int w = 96, x0 = 10 + 34 + 6;
    bankPrev.setBounds (10, 14, 34, 72);
    for (int i = 0; i < kSceneBank; ++i) scene[i].setBounds (x0 + i * (w + 8), 14, w, 72);
    const int xe = x0 + kSceneBank * (w + 8);
    bankNext.setBounds (xe, 14, 34, 72);
    bankLbl.setBounds (160 + 150 + 10, 100, 150, 30);
    storeBtn.setBounds (10, 100, 140, 30);
    varBtn.setBounds (160, 100, 150, 30);
    morph.setBounds (xe + 34 + 10, 10, 90, 100);
}


// ===========================================================================

EnsemblePage::EnsemblePage (MariaProcessor& p) : proc (p)
{
    const juce::Colour vio (0xffd56bff), gold (0xffffc857), cyan (0xff5ee0f2);
    bindK (harm, proc, "im_harm", "Harmonie");  harm.setAccentColour (vio);
    harm.setTooltip ("Maria choisit (et transpose) des fragments qui vont avec l'harmonie que jouent les musiciens en ce moment");
    harmMode.addItemList (juce::StringArray { "Choisir", "Choisir + transposer" }, 1);
    harmModeAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "im_harmmode", harmMode);
    harmMode.setTooltip ("Choisir : seulement des fragments deja dans l'harmonie | + transposer : elle decale aussi un fragment (jusqu'a une quarte) pour qu'il tombe juste");

    bindK (motVar, proc, "im_motvar", "Variation"); motVar.setAccentColour (gold);
    motVar.setTooltip ("0 % : le theme est rejoue tel quel | 100 % : il est varie, recombine, et Maria s'en ecarte avant d'y revenir");
    for (int k = 0; k < MariaProcessor::kNumMotifs; ++k)
    {
        const juce::String L = juce::String::charToString ((juce::juce_wchar) ('A' + k));
        markBtn[k].setButtonText ("MARQUER " + L);
        styleButton (markBtn[k], kMotifCol[k]);
        markBtn[k].setTooltip ("Garde la phrase qui vient d'etre jouee (ou celle en cours) comme leitmotiv " + L + " - note MIDI " + juce::String (52 + k));
        markBtn[k].onClick = [this, k] { proc.markMotif (k); };
        playBtn[k].setButtonText ("JOUER " + L);
        styleButton (playBtn[k], kMotifCol[k]);
        playBtn[k].setTooltip ("Maria rejoue et varie le leitmotiv " + L + " tout de suite - note MIDI " + juce::String (56 + k));
        playBtn[k].onClick = [this, k] { proc.triggerMotif (k); };
        delBtn[k].setButtonText ("x");
        styleButton (delBtn[k], col::lock);
        delBtn[k].setTooltip ("Oublie le leitmotiv " + L);
        delBtn[k].onClick = [this, k] { proc.clearMotif (k); };
        for (auto* b : { &markBtn[k], &playBtn[k], &delBtn[k] }) addAndMakeVisible (*b);
    }

    const juce::StringArray srcs { "Piste", "Sidechain", "Piste + sidechain" };
    scLearn.addItemList (srcs, 1);
    scLearnAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "sc_learn", scLearn);
    scLearn.setTooltip ("Ce que Maria apprend : le son de la piste, celui de la sidechain (un autre musicien), ou les deux");
    scReact.addItemList (srcs, 1);
    scReactAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "sc_react", scReact);
    scReact.setTooltip ("Qui Maria ecoute pour savoir quand jouer, quelle harmonie suivre, quelle energie contraster");

    bindK (corpusMix, proc, "corpus_mix", "Corpus"); corpusMix.setAccentColour (cyan);
    corpusMix.setTooltip ("0 : Maria joue seulement ce qu'elle a appris en direct | 100 % : seulement le corpus | entre les deux : chaque phrase puise dans l'un ou l'autre");
    corpusSrc.addItemList (sourceNames(), 1);
    corpusSrcAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "corpus_src", corpusSrc);
    corpusSrc.setTooltip ("Nature du corpus (reglage de son analyse) - a choisir avant de le charger");
    styleButton (corpusLoad, cyan);
    corpusLoad.setTooltip ("Un enregistrement prepare (sons industriels, repetition, ancienne session...) devient une 2e memoire, independante du direct");
    corpusLoad.onClick = [this]
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Maria Memoires");
        dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Charger un corpus pour Maria", dir, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.requestCorpusLoad (fc.getResult()); });
    };
    styleButton (corpusClear, col::lock);
    corpusClear.setTooltip ("Retire le corpus");
    corpusClear.onClick = [this] { proc.clearCorpus(); };

    outRoute.addItemList (juce::StringArray { "Melange stereo", "Voix 1 a gauche / voix 2 a droite" }, 1);
    outRouteAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "out_route", outRoute);
    outRoute.setTooltip ("Separees : la voix 1 (avec les effets de Maria) sort a gauche, la voix 2 (sans effets) a droite, pour les traiter a part dans Live");

    for (juce::Component* c : { (juce::Component*) &harm, (juce::Component*) &harmMode, (juce::Component*) &motVar, (juce::Component*) &scLearn,
                                (juce::Component*) &scReact, (juce::Component*) &corpusMix, (juce::Component*) &corpusSrc, (juce::Component*) &corpusLoad,
                                (juce::Component*) &corpusClear, (juce::Component*) &outRoute })
        addAndMakeVisible (c);
}

void EnsemblePage::refresh()
{
    for (auto* k : { &harm, &motVar, &corpusMix }) k->refreshLive();
    // ne redessine que si l'affichage change
    juce::String key;
    const auto& L = proc.listener.state;
    key << L.chord.load() << (L.harmValid.load() ? "h" : "-") << proc.impro.harmShifts.load() / 4 << "|";
    for (int k = 0; k < MariaProcessor::kNumMotifs; ++k) key << (int) (proc.motifSeconds (k) * 10.0f) << ",";
    key << (proc.impro.motifPlaying() ? "P" : "") << proc.motifLastPlayed.load() << "|" << (int) proc.sidechainConnected()
        << "|" << (int) proc.corpusReady.load() << (int) proc.corpusLoading.load() << proc.corpusName() << (int) proc.impro.playingCorpus();
    if (key != stateKey && isShowing()) { stateKey = key; repaint(); }
    for (int k = 0; k < MariaProcessor::kNumMotifs; ++k)
    {
        const bool has = proc.motifSeconds (k) > 0.0f;
        playBtn[k].setEnabled (has); delBtn[k].setEnabled (has);
    }
    scLearn.setAlpha (proc.sidechainConnected() ? 1.0f : 0.5f);
    scReact.setAlpha (proc.sidechainConnected() ? 1.0f : 0.5f);
    corpusClear.setEnabled (proc.corpusReady.load());
}

void EnsemblePage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    const juce::Colour vio (0xffd56bff), gold (0xffffc857), cyan (0xff5ee0f2);
    auto head = [&] (const juce::String& t, int x, juce::Colour c) { g.setFont (font (12.0f, true)); g.setColour (c); g.drawText (t, x, 8, 260, 16, juce::Justification::centredLeft); };
    auto txt = [&] (const juce::String& t, int x, int y, int w, int h, juce::Colour c = col::textDim) { g.setFont (font (11.0f)); g.setColour (c); g.drawFittedText (t, x, y, w, h, juce::Justification::topLeft, 6); };
    head ("HARMONIE", 12, vio);
    head ("LEITMOTIVS", 246, gold);
    head ("ECOUTE CROISEE", 660, col::lock);
    head ("CORPUS EXTERNE", 870, cyan);
    head ("SORTIES", 1110, col::text);
    for (int x : { 236, 650, 860, 1100 }) { g.setColour (col::bg); g.fillRect (x, 8, 1, getHeight() - 16); }

    // harmonie
    const auto& L = proc.listener.state;
    const int ch = L.chord.load();
    g.setFont (font (18.0f, true));
    g.setColour (L.harmValid.load() ? col::text : col::textDim);
    g.drawText (L.harmValid.load() && ch >= 0 ? chordName (ch) : juce::String ("-"), 96, 66, 130, 24, juce::Justification::centredLeft);
    txt ("entendu maintenant", 96, 90, 130, 14);
    txt ("Maria privilegie les fragments qui sonnent avec l'harmonie des musiciens ; en 'transposer', elle les decale aussi pour tomber juste. "
         "Transpositions : " + juce::String (proc.impro.harmShifts.load()), 12, 122, 216, 80);

    // leitmotivs
    for (int k = 0; k < MariaProcessor::kNumMotifs; ++k)
    {
        const float sec = proc.motifSeconds (k);
        const int x = 246 + k * 84;
        const bool playing = proc.impro.motifPlaying() && proc.motifLastPlayed.load() == k;
        g.setColour (playing ? kMotifCol[k] : (sec > 0 ? kMotifCol[k].withAlpha (0.8f) : col::textDim));
        g.setFont (font (11.0f, playing));
        g.drawText (sec > 0 ? (playing ? juce::String ("joue... ") : juce::String()) + juce::String (sec, 1) + " s" : juce::String ("vide"), x, 92, 58, 14, juce::Justification::centredLeft);
    }
    txt ("MARQUER garde la phrase en cours ou la derniere phrase jouee (jusqu'a 20 s). JOUER la fait revenir, plus ou moins variee. "
         "MIDI : 52-55 marquer A-D, 56-59 jouer A-D. Les leitmotivs sont gardes avec le set.", 246, 120, 400, 70);

    // ecoute croisee
    txt ("Apprend de", 660, 30, 190, 14, col::text);
    txt ("Reagit a", 660, 78, 190, 14, col::text);
    txt (proc.sidechainConnected() ? "Sidechain connectee." : "Pas de sidechain : dans Live, choisir la piste du musicien dans le menu Sidechain du plugin (Logic : menu Side Chain).",
         660, 128, 190, 80, proc.sidechainConnected() ? col::lock : col::textDim);

    // corpus
    juce::String cs;
    if (proc.corpusLoading.load()) cs = "analyse du corpus...";
    else if (proc.corpusReady.load())
        cs << "'" << proc.corpusName() << "' : " << (int) (proc.corpusSeconds() / 60) << ":" << juce::String ((int) proc.corpusSeconds() % 60).paddedLeft ('0', 2)
           << ", " << juce::jmax (0, proc.corpus.numStates() - proc.corpus.firstValidState()) << " evenements" << (proc.impro.playingCorpus() ? "  -  JOUE" : "");
    else cs = "aucun corpus";
    txt (cs, 870, 62, 220, 30, proc.corpusReady.load() ? cyan : col::textDim);
    txt ("Nature du corpus :", 944, 100, 146, 14);
    txt ("Une 2e memoire preparee a l'avance (6 min max), gardee avec le set. Chaque phrase puise dans le direct ou dans le corpus.",
         870, 182, 222, 44);

    // sorties
    txt ("Separees : dans Live, un Audio Effect Rack apres Maria avec deux chaines (Utility : Left / Right) pour traiter chaque voix a part. "
         "La voix 2 sort sans les effets de Maria.", 1110, 62, 160, 140);
}

void EnsemblePage::resized()
{
    harm.setBounds (12, 26, 74, 90);
    harmMode.setBounds (96, 34, 132, 24);
    for (int k = 0; k < MariaProcessor::kNumMotifs; ++k)
    {
        const int x = 246 + k * 84;
        markBtn[k].setBounds (x, 30, 80, 26);
        playBtn[k].setBounds (x, 60, 80, 26);
        delBtn[k].setBounds (x + 60, 90, 20, 18);
    }
    motVar.setBounds (582, 26, 64, 86);
    scLearn.setBounds (660, 46, 190, 24);
    scReact.setBounds (660, 94, 190, 24);
    corpusLoad.setBounds (870, 30, 150, 26);
    corpusClear.setBounds (1024, 30, 66, 26);
    corpusMix.setBounds (870, 92, 66, 84);
    corpusSrc.setBounds (944, 118, 146, 24);
    outRoute.setBounds (1110, 30, 160, 24);
}


// ===========================================================================
FormePage::FormePage (MariaProcessor& p) : proc (p)
{
    const juce::Colour gold (0xffffc857), vio (0xffd56bff), cyan (0xff5ee0f2), red (0xffe2464b);
    for (auto* b : { &prevChap, &nextChap }) styleButton (*b, gold);
    prevChap.onClick = [this] { proc.prevChapter(); };
    nextChap.onClick = [this] { proc.nextChapter(); };
    nextChap.setTooltip ("Chapitre suivant (note MIDI 60) - le chapitre est aussi automatisable dans Live (parametre 'Chapter')");
    chapPlay.addItemList (juce::StringArray { "Toute la memoire", "Chapitre en cours", "Chapitre choisi", "Les autres chapitres" }, 1);
    chapPlayAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "chap_play", chapPlay);
    chapPlay.setTooltip ("Ou Maria puise : toute sa memoire, le chapitre en cours, un chapitre precis (reminiscence d'une scene), ou tout sauf le chapitre en cours");
    juce::StringArray nums; for (int i = 1; i <= 16; ++i) nums.add ("Chapitre " + juce::String (i));
    chapPick.addItemList (nums, 1);
    chapPickAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "chap_pick", chapPick);

    bindK (arcGoal, proc, "arc_goal", "Objectif");   arcGoal.setAccentColour (red); arcGoal.setBipolar (true);
    bindK (arcTime, proc, "arc_time", "Duree");      arcTime.setAccentColour (red);
    bindK (arcDepth, proc, "arc_depth", "Profondeur"); arcDepth.setAccentColour (red);
    arcGoal.setTooltip ("Vers quoi Maria evolue : calme (gauche), neutre (milieu), intense (droite)");
    arcTime.setTooltip ("Temps pour atteindre l'objectif (2 s a 10 min)");
    arcDepth.setTooltip ("Ampleur de l'arc : activite, longueur des phrases, silences, niveau, chaos des effets");
    for (auto* b : { &upBtn, &downBtn, &neutralBtn }) styleButton (*b, red);
    upBtn.onClick = [this] { proc.arcTo (1.0f); };      upBtn.setTooltip ("Monte vers l'intense (note MIDI 61)");
    downBtn.onClick = [this] { proc.arcTo (0.0f); };    downBtn.setTooltip ("Redescend vers le calme (note MIDI 62)");
    neutralBtn.onClick = [this] { proc.arcTo (0.5f); }; neutralBtn.setTooltip ("Revient au neutre (note MIDI 63)");

    styleButton (pulseBtn, vio);
    pulseBtn.setTooltip ("Maria cale ses phrases sur la pulsation que jouent les musiciens (mode de tempo 'Pulsation musiciens')");
    pulseBtn.onClick = [this]
    {
        const bool on = (int) std::round (proc.apvts.getRawParameterValue ("im_tempo")->load()) == 3;
        proc.setParamReal ("im_tempo", on ? 0.0f : 3.0f);
    };

    juce::StringArray ch { "Off" }, ch2;
    for (int i = 1; i <= 16; ++i) { ch.add ("Canal " + juce::String (i)); ch2.add ("Canal " + juce::String (i)); }
    midiLearnBox.addItemList (ch, 1);
    midiLearnAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "midi_learn", midiLearnBox);
    midiLearnBox.setTooltip ("Canal du clavier a apprendre : ses notes deviennent de la matiere (et ne declenchent plus de commandes)");
    styleButton (mvBtn, cyan);
    mvBtn.setClickingTogglesState (true);
    mvAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "mv_on", mvBtn);
    mvBtn.setTooltip ("Une voix MIDI improvise avec ce que le clavier a joue et l'envoie au DAW et au port virtuel 'Maria'");
    mvMode.addItemList (imModes(), 1);
    mvModeAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "mv_mode", mvMode);
    mvCh.addItemList (ch2, 1);
    mvChAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "mv_ch", mvCh);
    mvCh.setTooltip ("Canal de sortie de la voix MIDI");
    styleButton (midiClear, col::lock);
    midiClear.onClick = [this] { proc.clearMidiMemory(); };

    for (juce::Component* c : { (juce::Component*) &prevChap, (juce::Component*) &nextChap, (juce::Component*) &chapPlay, (juce::Component*) &chapPick,
                                (juce::Component*) &arcGoal, (juce::Component*) &arcTime, (juce::Component*) &arcDepth, (juce::Component*) &upBtn,
                                (juce::Component*) &downBtn, (juce::Component*) &neutralBtn, (juce::Component*) &pulseBtn, (juce::Component*) &midiLearnBox,
                                (juce::Component*) &mvBtn, (juce::Component*) &mvMode, (juce::Component*) &mvCh, (juce::Component*) &midiClear })
        addAndMakeVisible (c);
}

void FormePage::refresh()
{
    for (auto* k : { &arcGoal, &arcTime, &arcDepth }) k->refreshLive();
    chapPick.setEnabled ((int) std::round (proc.apvts.getRawParameterValue ("chap_play")->load()) == 2);
    const bool pulseMode = (int) std::round (proc.apvts.getRawParameterValue ("im_tempo")->load()) == 3;
    pulseBtn.setToggleState (pulseMode, juce::dontSendNotification);
    juce::String key;
    key << proc.currentChapter() << "|" << proc.numRegions() << "|" << (int) (proc.arcView.load() * 200) << "|" << (int) proc.pulse.bpmView.load()
        << (int) proc.pulse.validView.load() << (int) (proc.pulse.confView.load() * 20) << (int) pulseMode << "|" << proc.midiLearned.load()
        << (int) proc.keyboardActive() << (int) proc.improMidi.isPlaying() << "|" << (int) (proc.mem.writePos() / (int64_t) (proc.mem.sampleRate() * 2));
    if (key != stateKey && isShowing()) { stateKey = key; repaint(); }
}

void FormePage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    const juce::Colour gold (0xffffc857), vio (0xffd56bff), cyan (0xff5ee0f2), red (0xffe2464b);
    auto head = [&] (const juce::String& t, int x, juce::Colour c) { g.setFont (font (12.0f, true)); g.setColour (c); g.drawText (t, x, 8, 300, 16, juce::Justification::centredLeft); };
    auto txt = [&] (const juce::String& t, int x, int y, int w, int h, juce::Colour c = col::textDim) { g.setFont (font (11.0f)); g.setColour (c); g.drawFittedText (t, x, y, w, h, juce::Justification::topLeft, 6); };
    head ("CHAPITRES", 12, gold);
    head ("ARC D'INTENSITE", 420, red);
    head ("PULSATION", 790, vio);
    head ("VOIX MIDI (CLAVIER)", 1000, cyan);
    for (int x : { 410, 780, 990 }) { g.setColour (col::bg); g.fillRect (x, 8, 1, getHeight() - 16); }

    // chapitres
    g.setFont (font (22.0f, true)); g.setColour (gold);
    g.drawText ("CHAPITRE " + juce::String (proc.currentChapter()), 44, 28, 190, 30, juce::Justification::centred);
    {
        int x = 12, y = 100;
        g.setFont (font (11.0f));
        for (int c = 1; c <= 16; ++c)
        {
            const double sec = proc.chapterSeconds (c);
            if (sec <= 0.05 && c != proc.currentChapter()) continue;
            g.setColour (c == proc.currentChapter() ? gold : col::text);
            g.drawText (juce::String (c) + " : " + juce::String ((int) sec / 60) + ":" + juce::String ((int) sec % 60).paddedLeft ('0', 2), x, y, 74, 14, juce::Justification::centredLeft);
            x += 78; if (x > 330) { x = 12; y += 16; }
        }
    }
    txt ("Un nouveau chapitre (bouton >, note MIDI 60 ou automation 'Chapter' dans Live) ouvre une nouvelle partie de la memoire. "
         "Revenir a un numero deja utilise le prolonge. Maria peut puiser dans le chapitre en cours, dans une scene ancienne, ou partout.", 12, 166, 390, 60);

    // arc
    {
        const auto bar = juce::Rectangle<float> (420.0f, 130.0f, 350.0f, 12.0f);
        g.setColour (col::bg); g.fillRoundedRectangle (bar, 4.0f);
        const float a = proc.arcView.load(), goal = proc.apvts.getRawParameterValue ("arc_goal")->load();
        const float cx = bar.getCentreX();
        const float xa = bar.getX() + a * bar.getWidth();
        g.setColour (red.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (cx, xa), bar.getY() + 2, std::abs (xa - cx), bar.getHeight() - 4));
        g.setColour (col::text);
        g.fillRect (bar.getX() + goal * bar.getWidth() - 1.0f, bar.getY() - 3, 2.0f, bar.getHeight() + 6);
        txt ("calme", 420, 146, 60, 14); txt ("intense", 726, 146, 60, 14); txt ("neutre", 578, 146, 60, 14);
        txt ("Maria glisse vers l'objectif pendant la duree choisie : activite, phrases, silences, niveau et chaos des effets suivent.", 420, 170, 350, 50);
    }

    // pulsation
    {
        const bool ok = proc.pulse.validView.load();
        const bool mode = (int) std::round (proc.apvts.getRawParameterValue ("im_tempo")->load()) == 3;
        g.setFont (font (20.0f, true)); g.setColour (ok ? vio : col::textDim);
        g.drawText (ok ? juce::String (proc.pulse.bpmView.load(), 1) + " BPM" : juce::String ("-"), 790, 64, 190, 26, juce::Justification::centredLeft);
        txt (ok ? "pulsation entendue (confiance " + juce::String ((int) (proc.pulse.confView.load() * 100)) + " %)" : "pas de pulsation nette : rythme libre", 790, 92, 190, 28, ok ? col::text : col::textDim);
        txt (mode ? "Maria suit la pulsation des musiciens : ses phrases partent sur le temps, ses evenements sont cales sur la grille."
                  : "Clique pour que Maria cale ses phrases sur votre pulsation (sans clic ni DAW).", 790, 130, 190, 80, mode ? vio : col::textDim);
    }

    // voix MIDI
    txt ("Apprend du canal", 1000, 30, 120, 14, col::text);
    txt ("Attitude / sortie", 1000, 116, 140, 14, col::text);
    const int nev = juce::jmax (0, proc.midiMem.numStates() - proc.midiMem.firstValidState());
    txt ("memoire MIDI : " + juce::String (proc.midiLearned.load()) + " notes, " + juce::String (nev) + " evenements"
         + (proc.keyboardActive() ? "  -  le clavier joue" : "") + (proc.improMidi.isPlaying() ? "  -  JOUE" : ""), 1000, 166, 270, 28, nev > 0 ? cyan : col::textDim);
    txt ("Notes rejouees sur le canal choisi, vers le DAW et le port virtuel 'Maria'. Gardee avec le set.", 1000, 196, 270, 30);
}

void FormePage::resized()
{
    prevChap.setBounds (12, 30, 30, 28);
    nextChap.setBounds (236, 30, 30, 28);
    chapPlay.setBounds (12, 68, 170, 24);
    chapPick.setBounds (190, 68, 120, 24);
    arcGoal.setBounds (420, 24, 66, 86);
    arcTime.setBounds (490, 24, 66, 86);
    arcDepth.setBounds (560, 24, 66, 86);
    upBtn.setBounds (636, 30, 134, 24);
    downBtn.setBounds (636, 58, 134, 24);
    neutralBtn.setBounds (636, 86, 134, 24);
    pulseBtn.setBounds (790, 30, 180, 26);
    midiLearnBox.setBounds (1000, 46, 130, 24);
    mvBtn.setBounds (1140, 44, 128, 28);
    midiClear.setBounds (1140, 78, 128, 24);
    mvMode.setBounds (1000, 132, 130, 24);
    mvCh.setBounds (1140, 132, 128, 24);
}


// ===========================================================================
MachinePage::MachinePage (MariaProcessor& p) : proc (p)
{
    const juce::Colour vio (0xffd56bff), gold (0xffffc857), cyan (0xff5ee0f2), green (0xff7ee081);
    styleButton (machBtn, gold);
    machBtn.setTooltip ("Passe Maria en mode Machine (ou revient en Dialogue)");
    machBtn.onClick = [this]
    {
        const bool on = (int) std::round (proc.apvts.getRawParameterValue ("im_mode")->load()) == MMachine;
        proc.setParamReal ("im_mode", on ? (float) MDialogue : (float) MMachine);
    };
    machLen.addItemList (juce::StringArray { "1 temps", "2 temps", "1 mesure", "2 mesures", "4 mesures" }, 1);
    machLenAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "mach_len", machLen);
    machLen.setTooltip ("Longueur de la boucle (calee sur la grille quand Maria suit un tempo)");
    bindK (machMut, proc, "mach_mut", "Mutation"); machMut.setAccentColour (gold);
    machMut.setTooltip ("Probabilite qu'un element de la boucle change a chaque tour (l'arc d'intensite l'augmente)");

    styleButton (droneBtn, cyan);
    droneBtn.setClickingTogglesState (true);
    droneAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "drone_on", droneBtn);
    droneBtn.setTooltip ("Nappes : des fragments de la memoire geles en sons tenus, choisis dans l'harmonie entendue, fondus les uns dans les autres");
    bindK (droneLevel, proc, "drone_level", "Niveau"); droneLevel.setAccentColour (cyan);
    bindK (droneTime, proc, "drone_time", "Renouvel."); droneTime.setAccentColour (cyan);
    droneTime.setTooltip ("Toutes les combien de secondes la nappe change de fragment (fondu sur la moitie)");
    droneReg.addItemList (juce::StringArray { "-2 octaves", "-1 octave", "Original", "+1 octave" }, 1);
    droneRegAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "drone_reg", droneReg);

    juce::StringArray ch { "Off" }; for (int i = 1; i <= 16; ++i) ch.add ("Canal " + juce::String (i));
    fbCh.addItemList (ch, 1);
    fbChAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "fb_ch", fbCh);
    fbCh.setTooltip ("Canal sur lequel Maria renvoie son etat (notes allumees / eteintes, CC)");
    fbDev.onChange = [this]
    {
        const int i = fbDev.getSelectedId();
        proc.setFeedbackDevice (i <= 1 || i - 2 >= devices.size() ? juce::String() : devices[i - 2].identifier);
    };
    fbDev.setTooltip ("Ou envoyer l'etat : vers le DAW et le port virtuel 'Maria', ou directement vers une sortie MIDI (la Wing, un controleur)");
    styleButton (refreshBtn, green);
    refreshBtn.onClick = [this] { fillDevices(); };
    fillDevices();

    styleButton (cleanBtn, col::lock);
    cleanBtn.setTooltip ("Met a la corbeille les memoires de session de plus de 30 jours (sauf celle de cette Maria)");
    cleanBtn.onClick = [this]
    {
        const int n = MariaProcessor::cleanupSessions (30, proc.memoryId());
        cleanMsg = juce::String (n) + " fichier(s) mis a la corbeille";
        infoTick = 0; repaint();
    };
    folderBtn.onClick = [] { auto d = MariaProcessor::sessionsFolder(); d.createDirectory(); d.startAsProcess(); };
    for (juce::Component* c : { (juce::Component*) &machBtn, (juce::Component*) &machLen, (juce::Component*) &machMut, (juce::Component*) &droneBtn,
                                (juce::Component*) &droneLevel, (juce::Component*) &droneTime, (juce::Component*) &droneReg, (juce::Component*) &fbCh,
                                (juce::Component*) &fbDev, (juce::Component*) &refreshBtn, (juce::Component*) &cleanBtn, (juce::Component*) &folderBtn })
        addAndMakeVisible (c);
}

void MachinePage::fillDevices()
{
    devices = juce::MidiOutput::getAvailableDevices();
    fbDev.clear (juce::dontSendNotification);
    fbDev.addItem ("DAW + port virtuel 'Maria'", 1);
    const auto cur = proc.feedbackDevice();
    int sel = 1;
    for (int i = 0; i < devices.size(); ++i)
    {
        if (devices[i].name.startsWith ("Maria")) continue;     // pas de boucle sur nos propres ports
        fbDev.addItem (devices[i].name, i + 2);
        if (devices[i].identifier == cur) sel = i + 2;
    }
    fbDev.setSelectedId (sel, juce::dontSendNotification);
}

void MachinePage::refresh()
{
    for (auto* k : { &machMut, &droneLevel, &droneTime }) k->refreshLive();
    const bool machMode = (int) std::round (proc.apvts.getRawParameterValue ("im_mode")->load()) == MMachine;
    machBtn.setToggleState (machMode, juce::dontSendNotification);
    if (--infoTick <= 0) { infoTick = 90; MariaProcessor::sessionsFolderInfo (folderFiles, folderBytes); }
    juce::String key;
    key << (int) machMode << proc.impro.machineLength.load() << "|" << proc.impro.machineCycles.load() << (int) proc.impro.machinePlaying() << "|"
        << proc.drone.changes.load() << (int) proc.drone.active() << "|" << proc.feedbackSent.load() / 8 << "|" << folderFiles << "|" << (int) (folderBytes >> 20) << cleanMsg;
    if (key != stateKey && isShowing()) { stateKey = key; repaint(); }
}

void MachinePage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    const juce::Colour gold (0xffffc857), cyan (0xff5ee0f2), green (0xff7ee081);
    auto head = [&] (const juce::String& t, int x, juce::Colour c) { g.setFont (font (12.0f, true)); g.setColour (c); g.drawText (t, x, 8, 300, 16, juce::Justification::centredLeft); };
    auto txt = [&] (const juce::String& t, int x, int y, int w, int h, juce::Colour c = col::textDim) { g.setFont (font (11.0f)); g.setColour (c); g.drawFittedText (t, x, y, w, h, juce::Justification::topLeft, 7); };
    head ("MACHINE (OSTINATO)", 12, gold);
    head ("NAPPES", 340, cyan);
    head ("RETOUR D'ETAT MIDI", 680, green);
    head ("FICHIERS DE SESSION", 1010, col::text);
    for (int x : { 330, 670, 1000 }) { g.setColour (col::bg); g.fillRect (x, 8, 1, getHeight() - 16); }

    const bool mp = proc.impro.machinePlaying();
    txt (mp ? "tourne : boucle de " + juce::String (proc.impro.machineLength.load()) + " evenements, " + juce::String (proc.impro.machineCycles.load()) + " tours"
            : juce::String ("en attente"), 12, 120, 310, 16, mp ? gold : col::textDim);
    txt ("Maria choisit un fragment de ta memoire et le repete en boucle, calee sur la grille (Synchro DAW, tempo manuel ou pulsation des musiciens). "
         "La boucle mute lentement ; une nouvelle machine demarre a chaque phrase. Mode aussi disponible pour la 2e voix et la voix MIDI.", 12, 142, 310, 80);

    txt (proc.drone.active() ? "joue - " + juce::String (proc.drone.changes.load()) + " fragments" + (proc.droneFromCorpus.load() ? " (corpus)" : "") : juce::String ("eteinte"),
         340, 120, 320, 16, proc.drone.active() ? cyan : col::textDim);
    txt ("Des fragments geles en sons tenus, choisis dans l'harmonie entendue (et dans le chapitre / le corpus selon les reglages), fondus les uns dans les autres. "
         "Elles passent dans les effets de Maria.", 340, 142, 320, 80);

    txt ("Canal", 680, 30, 100, 14, col::text);
    txt ("Vers", 790, 30, 100, 14, col::text);
    txt ("Notes allumees (127) / eteintes (0) : 36-43 scene active, 46 freeze, 47 panique, 48 Maria joue, 49 Maria se tait, 50 ecoute, "
         "52-55 leitmotivs marques, 56-59 leitmotiv joue, 61-63 arc. CC 20 chapitre, CC 21 intensite, CC 22 tempo trouve (BPM - 40). "
         "Messages envoyes : " + juce::String (proc.feedbackSent.load()), 680, 90, 310, 100);
    txt ("Attention : ne renvoie pas ce retour vers l'entree MIDI de Maria (boucle).", 680, 196, 310, 30, col::lock);

    const double mb = (double) folderBytes / 1.0e6;
    txt (juce::String (folderFiles) + " fichiers, " + juce::String (mb, mb < 10 ? 1 : 0) + " Mo", 1010, 30, 260, 16, col::text);
    txt ("Memoires en FLAC (environ moitie moins lourd qu'en WAV). Une sauvegarde de secours est ecrite toutes les 2 minutes : "
         "si Live plante, Maria la propose a la reouverture." + (cleanMsg.isNotEmpty() ? "\n" + cleanMsg : juce::String()), 1010, 112, 260, 110);
}

void MachinePage::resized()
{
    machBtn.setBounds (12, 30, 140, 28);
    machLen.setBounds (12, 66, 140, 24);
    machMut.setBounds (170, 24, 70, 88);
    droneBtn.setBounds (340, 30, 110, 28);
    droneReg.setBounds (340, 66, 110, 24);
    droneLevel.setBounds (460, 24, 66, 88);
    droneTime.setBounds (530, 24, 66, 88);
    fbCh.setBounds (680, 46, 100, 24);
    fbDev.setBounds (790, 46, 200, 24);
    refreshBtn.setBounds (900, 22, 90, 20);
    cleanBtn.setBounds (1010, 52, 180, 26);
    folderBtn.setBounds (1010, 82, 180, 24);
}

// ===========================================================================
SourcePage::SourcePage (MariaProcessor& p) : proc (p)
{
    bindK (sens, proc, "sens", "Sensibilite");
    sens.setAccentColour (juce::Colour (0xffd56bff));
    bindK (hitDec, proc, "hitdec", "Decay segm.");
    hitDec.setAccentColour (col::rnd);
    styleButton (adapt, juce::Colour (0xffd56bff));
    adapt.setTooltip ("Regle l'improvisatrice et une chaine d'effets de depart pour la source (+ 8 scenes)");
    adapt.onClick = [this] { proc.adaptToSource(); };
    loadBtn.setTooltip ("Ajoute un enregistrement (wav, aiff, mp3...) a la memoire de Maria : elle pourra improviser avec");
    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Charger un fichier audio dans la memoire de Maria", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.requestLoadFile (fc.getResult()); });
    };
    saveBtn.setTooltip ("Exporte la memoire actuelle en WAV (rechargeable plus tard avec CHARGER)");
    saveBtn.onClick = [this]
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Maria Memoires");
        dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Sauver la memoire de Maria", dir.getChildFile ("Memoire " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %Hh%M") + ".wav"), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) proc.saveMemory (fc.getResult().withFileExtension ("wav")); });
    };
    styleButton (clearBtn, col::lock);
    clearBtn.setTooltip ("Oublie tout ce qui a ete appris (note MIDI 51)");
    clearBtn.onClick = [this] { proc.clearMemory(); };
    for (juce::Component* c : { (juce::Component*) &sens, (juce::Component*) &hitDec, (juce::Component*) &adapt,
                                (juce::Component*) &loadBtn, (juce::Component*) &saveBtn, (juce::Component*) &clearBtn }) addAndMakeVisible (c);
}

void SourcePage::refresh()
{
    sens.refreshLive();
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const int hc = proc.hitCountView.load();
    if (rateStart <= 0.0 || hc < rateHits) { rateStart = now; rateHits = hc; }
    else if (now - rateStart >= 5.0)
    {
        hitsPerMin = (float) ((hc - rateHits) * 60.0 / (now - rateStart));
        rateStart = now; rateHits = hc;
    }
    if (isShowing()) repaint();
}

void SourcePage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    auto sc = juce::Rectangle<float> (10.0f, 10.0f, (float) getWidth() - 640.0f, (float) getHeight() - 20.0f);
    g.setColour (col::bg);
    g.fillRoundedRectangle (sc, 5.0f);
    const int N = (int) proc.scopePk.size();
    const int w0 = proc.scopeW.load();
    const float bw = sc.getWidth() / (float) N;
    const float gateY = (proc.gateDb() + 60.0f) / 60.0f * (sc.getHeight() - 8.0f);
    for (int i = 0; i < N; ++i)
    {
        const int k = (w0 + 1 + i) % N;
        const float v = proc.scopePk[(size_t) k].load();
        const float db = juce::jlimit (-60.0f, 0.0f, juce::Decibels::gainToDecibels (v + 1.0e-6f));
        const float hgt = (db + 60.0f) / 60.0f * (sc.getHeight() - 8.0f);
        const float x = sc.getX() + (float) i * bw;
        g.setColour ((hgt > gateY ? col::rnd : col::textDim).withAlpha (0.75f));
        g.fillRect (x, sc.getBottom() - 4.0f - hgt, juce::jmax (1.0f, bw - 0.4f), hgt);
        if (proc.scopeHit[(size_t) k].load() > 0.0f)
        {
            g.setColour (juce::Colour (0xffd56bff));
            g.fillRect (x, sc.getY() + 2.0f, 1.5f, sc.getHeight() - 4.0f);
        }
    }
    g.setColour (col::accent.withAlpha (0.8f));
    g.drawHorizontalLine ((int) (sc.getBottom() - 4.0f - gateY), sc.getX(), sc.getRight());
    g.setColour (col::textDim);
    g.setFont (font (11.0f));
    g.drawText ("ton jeu (~2 s) | ligne orange = seuil d'ecoute | traits violets = attaques", sc.reduced (8.0f, 4.0f), juce::Justification::topLeft);

    const auto& pr = sourceProfile (proc.sourceIndex());
    static const char* modeN[] { "timbre (frappes, bruits)", "hauteur (notes)", "hybride (notes ou timbre)" };
    auto r = juce::Rectangle<int> (getWidth() - 620, 12, 250, getHeight() - 24);
    g.setColour (col::text);
    g.setFont (font (16.0f, true));
    g.drawText (pr.name, r.removeFromTop (22), juce::Justification::centredLeft);
    g.setFont (font (12.0f));
    g.setColour (col::textDim);
    auto line = [&] (const juce::String& s) { g.drawText (s, r.removeFromTop (17), juce::Justification::centredLeft); };
    line (juce::String ("symboles : ") + modeN[juce::jlimit (0, 2, pr.symMode)]);
    if (pr.fmax > 0.0f) line ("hauteurs : " + juce::String ((int) pr.fmin) + " - " + juce::String ((int) pr.fmax) + " Hz");
    line ("evenements : " + juce::String ((int) pr.minSegMs) + " ms a " + juce::String (pr.maxSegMs / 1000.0f, 1) + " s");
    line ("seuil d'ecoute : " + juce::String (proc.gateDb(), 1) + " dBFS");
    const float lp = proc.mem.lastPitch.load();
    if (lp > 0.0f) line ("derniere note : " + juce::MidiMessage::getMidiNoteName ((int) std::round (lp), true, true, 3));
    if (pr.symMode == 2 && proc.mem.lastChord.load() >= 0) line ("dernier accord : " + chordName (proc.mem.lastChord.load()));
    g.setColour (col::text);
    line ("attaques detectees : " + juce::String (proc.hitCountView.load())
          + (hitsPerMin >= 0.0f ? "  (" + juce::String (juce::roundToInt (hitsPerMin)) + " / min)" : juce::String()));
    // memoire : duree et remplissage (concert entier : 30 min d'audio, 262 144 evenements)
    const double memMin = (double) (proc.mem.writePos() - proc.mem.oldestValidPos()) / juce::jmax (1.0, proc.mem.sampleRate()) / 60.0;
    const float fill = (float) proc.mem.numStates() / (float) Memory::kMaxStates;
    g.setColour (fill > 0.9f ? juce::Colour (0xffff6b6b) : col::text);
    line ("memoire : " + juce::String (juce::jmax (0.0, memMin), 1) + " / " + juce::String ((int) (Memory::kLiveSeconds / 60.0)) + " min, "
          + juce::String (proc.mem.numStates() - 1) + " evenements (" + juce::String (juce::roundToInt (fill * 100.0f)) + " %)");
    if (proc.loading.load()) { g.setColour (juce::Colour (0xffd56bff)); line ("chargement du fichier..."); }
}

void SourcePage::resized()
{
    const int x = getWidth() - 360;
    sens.setBounds (x, 8, 74, 90);
    hitDec.setBounds (x + 76, 8, 74, 90);
    adapt.setBounds (x + 160, 14, 190, 34);
    loadBtn.setBounds (x, 104, 350, 28);
    saveBtn.setBounds (x, 136, 172, 28);
    clearBtn.setBounds (x + 178, 136, 172, 28);
}

// ===========================================================================
OptionsPage::OptionsPage (MariaProcessor& p) : proc (p)
{
    const juce::Colour vio (0xffd56bff), cyan (0xff5ee0f2);
    styleButton (v2Btn, cyan);
    v2Btn.setClickingTogglesState (true);
    v2Att = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "v2_on", v2Btn);
    v2Btn.setTooltip ("Une deuxieme Maria improvise aussi avec ta memoire : trio avec toi-meme");
    v2Mode.addItemList (imModes(), 1);
    v2ModeAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "v2_mode", v2Mode);
    v2Mode.setTooltip ("Attitude de la 2e voix (les autres reglages sont partages avec la 1re)");
    bindK (v2Level, proc, "v2_level", "Niveau 2");   v2Level.setAccentColour (cyan);
    bindK (v2Transp, proc, "v2_transp", "Transpo 2"); v2Transp.setAccentColour (cyan); v2Transp.setBipolar (true);
    bindK (v2Pan, proc, "v2_pan", "Ecart stereo");  v2Pan.setAccentColour (cyan);
    tmode.addItemList (juce::StringArray { "Bande (vitesse)", "Hauteur seule", "Aleatoire" }, 1);
    tmodeAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "im_tmode", tmode);
    tmode.setTooltip ("Bande : transposer change aussi la vitesse | Hauteur seule : la duree des evenements est gardee | Aleatoire : tirage bande / hauteur seule a chaque segment (memorise dans les scenes)");
    selfBox.addItemList (juce::StringArray { "Apprend tout", "Ignore sa voix (auto)", "Pause pendant ses phrases" }, 1);
    selfAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "im_self", selfBox);
    selfBox.setTooltip ("Anti-repiquage : evite que Maria apprenne (et prenne pour toi) sa propre voix captee par le micro");
    juce::StringArray ch { "Off" };
    for (int i = 1; i <= 16; ++i) ch.add ("Canal " + juce::String (i));
    midiBox.addItemList (ch, 1);
    midiAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "midi_out", midiBox);
    midiBox.setTooltip ("Maria envoie en MIDI les notes (et accords) qu'elle joue ; la 2e voix sort sur le canal suivant");
    styleButton (autosaveBtn, vio);
    autosaveBtn.setClickingTogglesState (true);
    autosaveAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "autosave", autosaveBtn);
    autosaveBtn.setTooltip ("La memoire est sauvee avec le set Live (fichier WAV a part) et rechargee a l'ouverture");
    folderBtn.onClick = [this] { auto d = proc.sessionMemoryFile().getParentDirectory(); d.createDirectory(); d.startAsProcess(); };
    for (juce::Component* k : { (juce::Component*) &v2Btn, (juce::Component*) &v2Mode, (juce::Component*) &v2Level, (juce::Component*) &v2Transp,
                                (juce::Component*) &v2Pan, (juce::Component*) &tmode, (juce::Component*) &selfBox, (juce::Component*) &midiBox,
                                (juce::Component*) &autosaveBtn, (juce::Component*) &folderBtn })
        addAndMakeVisible (k);
}

void OptionsPage::refresh()
{
    for (auto* k : { &v2Level, &v2Transp, &v2Pan }) k->refreshLive();
    const int n = proc.midiNotesSent.load();
    if (n != lastNotes && isShowing()) { lastNotes = n; repaint(); }
}

void OptionsPage::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setFont (font (12.0f, true));
    g.setColour (juce::Colour (0xff5ee0f2));
    g.drawText ("TRIO : 2e VOIX", 12, 8, 200, 16, juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffd56bff));
    g.drawText ("TRANSPOSITION", 470, 8, 200, 16, juce::Justification::centredLeft);
    g.drawText ("ANTI-REPIQUAGE", 470, 70, 200, 16, juce::Justification::centredLeft);
    g.drawText ("SORTIE MIDI", 720, 8, 200, 16, juce::Justification::centredLeft);
    g.drawText ("MEMOIRE ET SESSION", 970, 8, 200, 16, juce::Justification::centredLeft);
    g.setFont (font (11.0f));
    g.setColour (col::textDim);
    g.drawFittedText ("La 2e voix partage la memoire et les reglages de Maria, avec sa propre attitude, sa transposition et sa place dans le panoramique.",
                      12, 150, 440, 40, juce::Justification::topLeft, 3);
    g.drawFittedText ("'Ignore sa voix' : quand le micro entend surtout Maria (sono), elle n'apprend pas et ne te croit pas en train de jouer.",
                      470, 120, 230, 60, juce::Justification::topLeft, 4);
    g.drawFittedText ("Notes et accords joues par Maria (canal choisi ; 2e voix = canal suivant), envoyes au DAW et sur le port MIDI virtuel 'Maria'. "
                      "Notes envoyees : " + juce::String (proc.midiNotesSent.load())
                      + (proc.virtualMidiName().isNotEmpty() ? "\nPort virtuel : '" + proc.virtualMidiName() + "' (Logic, Live... : choisir ce port en entree)" : juce::String()), 720, 70, 230, 110, juce::Justification::topLeft, 6);
    g.drawFittedText ("Sauvee en FLAC (dossier Maria Memoires / Sessions) a chaque sauvegarde du set, rechargee a l'ouverture ; secours toutes les 2 min.\n"
                      "Identifiant : " + proc.memoryId().substring (0, 8), 970, 90, 300, 90, juce::Justification::topLeft, 5);
}

void OptionsPage::resized()
{
    v2Btn.setBounds (12, 30, 90, 26);
    v2Mode.setBounds (108, 32, 120, 22);
    v2Level.setBounds (236, 22, 70, 86);
    v2Transp.setBounds (308, 22, 70, 86);
    v2Pan.setBounds (380, 22, 70, 86);
    tmode.setBounds (470, 30, 220, 24);
    selfBox.setBounds (470, 90, 220, 24);
    midiBox.setBounds (720, 30, 140, 24);
    autosaveBtn.setBounds (970, 30, 180, 26);
    folderBtn.setBounds (970, 60, 180, 24);
}

// ===========================================================================
static juce::Colour symColour (int sym)
{
    if (sym >= 2000) { const int r = (sym - 2000) % 12; return juce::Colour::fromHSV ((float) r / 12.0f, 0.35f, 1.0f, 1.0f); }
    if (sym >= 1000) { const int b = (sym - 1000) / 3; return juce::Colour::fromHSV (0.08f, 0.35f, 0.45f + 0.09f * (float) b, 1.0f); }
    return juce::Colour::fromHSV ((float) (((sym % 12) + 12) % 12) / 12.0f, 0.65f, 0.95f, 1.0f);
}

void OracleView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (b, 6.0f);
    auto& mem = proc.mem;
    if (! mem.isPrepared()) return;
    const int64_t w = mem.writePos();
    const int64_t lo = mem.oldestValidPos();
    const double fs = mem.sampleRate();
    const auto area = b.reduced (10.0f, 8.0f).withTrimmedTop (16.0f);
    const float wavH = area.getHeight() * 0.42f;
    const auto wav = area.withHeight (wavH);
    const float tickY = wav.getBottom() + 2.0f;
    const float arcTop = tickY + 10.0f, arcH = area.getBottom() - arcTop;
    g.setColour (col::bg);
    g.fillRoundedRectangle (wav, 4.0f);

    // fenetre affichee : toute la memoire valide (au moins 20 s)
    const int64_t span = juce::jmax ((int64_t) (fs * 20.0), w - lo);
    const int64_t x0 = w - span;
    auto xOf = [&] (int64_t pos) { return area.getX() + (float) ((double) (pos - x0) / (double) span) * area.getWidth(); };

    // forme d'onde
    {
        const int cols = (int) area.getWidth();
        g.setColour (col::textDim.withAlpha (0.6f));
        for (int i = 0; i < cols; ++i)
        {
            const int64_t p0 = x0 + span * i / cols, p1 = x0 + span * (i + 1) / cols;
            if (p1 < lo) continue;
            float pk = 0;
            for (int64_t q = juce::jmax (p0, lo) / Memory::kOvBlock; q <= p1 / Memory::kOvBlock && q * Memory::kOvBlock < w; ++q)
                pk = juce::jmax (pk, mem.overviewAt (q));
            const float hh = juce::jmin (1.0f, std::sqrt (pk)) * wavH * 0.48f;
            g.fillRect (area.getX() + (float) i, wav.getCentreY() - hh, 1.0f, hh * 2.0f);
        }
    }

    // evenements et arcs
    const int n = mem.numStates();
    const int first = juce::jmax (mem.firstValidState(), n - 3000);
    const int ctxMin = imCtxMin (proc.effectiveIm (ICtx));
    for (int s = first; s < n; ++s)
    {
        const auto& sg = mem.segment (s);
        const float xa = xOf (sg.start), xb = xOf (sg.end);
        g.setColour (symColour (sg.sym));
        g.fillRect (xa, tickY, juce::jmax (1.0f, xb - xa - 0.5f), 7.0f);
        const int t = mem.sfx (s);
        const int L = mem.lrs (s);
        if (t >= first && t > 0 && L >= 1)
        {
            const float xt = xOf (mem.segment (t).end);
            const float xs = xOf (sg.end);
            const float hh = juce::jmin (arcH - 2.0f, (xs - xt) * 0.5f);
            juce::Path p;
            p.startNewSubPath (xt, arcTop);
            p.quadraticTo ((xt + xs) * 0.5f, arcTop + hh * 2.0f, xs, arcTop);
            g.setColour ((L >= ctxMin ? col::rnd : col::textDim).withAlpha (juce::jmin (0.85f, 0.12f + 0.1f * (float) L)));
            g.strokePath (p, juce::PathStrokeType (L >= ctxMin ? 1.2f : 0.6f));
        }
    }

    // sauts recents de Maria
    const int jt = proc.impro.jumpTo.load(), jf = proc.impro.jumpFrom.load();
    if (jt != lastJumpTo && jt > 0 && jf > 0) { lastJumpTo = jt; trail.push_back ({ jf, jt }); if (trail.size() > 6) trail.erase (trail.begin()); }
    for (size_t k = 0; k < trail.size(); ++k)
    {
        const auto [f, t] = trail[k];
        if (f >= n || t >= n || f < first || t < first) continue;
        const float xf = xOf (mem.segment (f).end), xt = xOf (mem.segment (t).start);
        juce::Path p;
        p.startNewSubPath (xf, wav.getY() + 4.0f);
        p.quadraticTo ((xf + xt) * 0.5f, wav.getY() - 14.0f, xt, wav.getY() + 4.0f);
        g.setColour (juce::Colour (0xffd56bff).withAlpha (0.25f + 0.12f * (float) k));
        g.strokePath (p, juce::PathStrokeType (1.6f));
    }
    const int cur = proc.impro.curState.load();
    if (cur >= first && cur < n && proc.impro.status.load() == Improviser::SPlay)
    {
        const auto& sg = mem.segment (cur);
        g.setColour (juce::Colour (0xffd56bff).withAlpha (0.35f));
        g.fillRect (juce::Rectangle<float> (xOf (sg.start), wav.getY(), juce::jmax (2.0f, xOf (sg.end) - xOf (sg.start)), wavH));
        g.setColour (juce::Colour (0xffd56bff));
        g.fillRect (xOf (sg.start), wav.getY(), 2.0f, area.getBottom() - wav.getY());
    }
    {
        const int cur2 = proc.impro2.curState.load();
        if (cur2 >= first && cur2 < n && proc.impro2.status.load() == Improviser::SPlay)
        {
            const auto& sg = mem.segment (cur2);
            g.setColour (juce::Colour (0xff5ee0f2).withAlpha (0.3f));
            g.fillRect (juce::Rectangle<float> (xOf (sg.start), wav.getY(), juce::jmax (2.0f, xOf (sg.end) - xOf (sg.start)), wavH));
            g.setColour (juce::Colour (0xff5ee0f2));
            g.fillRect (xOf (sg.start), wav.getY(), 2.0f, area.getBottom() - wav.getY());
        }
    }
    // chapitres
    {
        const int rc = proc.numRegions();
        for (int i = 0; i < rc; ++i)
        {
            const int64_t a = proc.regionStart (i), left = juce::jmax (x0, lo);
            const bool coversLeft = a < left && (i + 1 >= rc || proc.regionStart (i + 1) > left);
            if (a < left && ! coversLeft) continue;
            const float xa = juce::jmax (area.getX(), xOf (juce::jmax (a, left)));
            g.setColour (juce::Colour (0xffffc857).withAlpha (0.55f));
            g.fillRect (xa, wav.getY(), 1.0f, area.getBottom() - wav.getY());
            g.setFont (font (10.0f, true));
            g.drawText (juce::String (proc.regionChapter (i)), (int) xa + 2, (int) wav.getBottom() - 13, 20, 12, juce::Justification::centredLeft);
        }
    }
    // leitmotivs marques
    for (int k = 0; k < MariaProcessor::kNumMotifs; ++k)
    {
        int64_t ma = 0, mb = 0;
        if (! proc.motifRange (k, ma, mb) || mb < x0) continue;
        const float xa = juce::jmax (area.getX(), xOf (ma)), xb = xOf (mb);
        g.setColour (kMotifCol[k].withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (xa, wav.getY(), juce::jmax (2.0f, xb - xa), wavH));
        g.setColour (kMotifCol[k]);
        g.fillRect (juce::Rectangle<float> (xa, wav.getY(), juce::jmax (2.0f, xb - xa), 3.0f));
        g.setFont (font (11.0f, true));
        g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + k)), (int) xa + 3, (int) wav.getY() + 4, 14, 12, juce::Justification::centredLeft);
    }
    // tete d'ecriture
    g.setColour (proc.playerView.load() ? col::lock : col::lock.withAlpha (0.35f));
    g.fillRect (area.getRight() - 2.0f, wav.getY(), 2.0f, wavH);

    // texte d'etat
    static const char* st[] { "memoire vide : joue pour qu'elle apprenne", "t'ecoute", "JOUE", "attend" };
    const int stt = proc.impro.status.load();
    const double usedS = (double) (w - lo) / fs;
    juce::String s1;
    s1 << "MEMOIRE  " << (int) (usedS / 60) << ":" << juce::String ((int) usedS % 60).paddedLeft ('0', 2) << " / 6:00   |   "
       << juce::jmax (0, n - mem.firstValidState()) << " evenements   |   MARIA (" << imModes()[proc.imModeParam()] << ") : " << st[juce::jlimit (0, 3, stt)];
    if (proc.apvts.getRawParameterValue ("learn")->load() < 0.5f) s1 << "   |   apprentissage coupe";
    if (proc.impro.motifPlaying()) s1 << "   |   leitmotiv " << juce::String::charToString ((juce::juce_wchar) ('A' + juce::jmax (0, proc.motifLastPlayed.load())));
    else if (proc.impro.playingCorpus() && stt == Improviser::SPlay) s1 << "   |   joue le corpus";
    g.setFont (font (12.0f, true));
    g.setColour (stt == Improviser::SPlay ? juce::Colour (0xffd56bff) : col::text);
    g.drawText (s1, b.reduced (12.0f, 4.0f).withHeight (16.0f), juce::Justification::centredLeft);
}

// ===========================================================================
ImproPanel::ImproPanel (MariaProcessor& p) : proc (p)
{
    mode.addItemList (imModes(), 1);
    modeAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "im_mode", mode);
    mode.setTooltip ("Dialogue : repond quand tu te tais | Duo : joue avec toi | Echo : commente ta derniere phrase | Contrepoint : joue pendant que tu joues | Contraste : doux et rare quand ca joue fort, present dans le calme | Machine : ostinato tire de ta memoire");
    addAndMakeVisible (mode);
    styleButton (learnBtn, col::lock);
    learnBtn.setClickingTogglesState (true);
    learnAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "learn", learnBtn);
    learnBtn.setTooltip ("Apprentissage : Maria enregistre et analyse ton jeu (note MIDI 50)");
    styleButton (phraseBtn, juce::Colour (0xffd56bff));
    phraseBtn.setTooltip ("Lance une phrase tout de suite (note MIDI 48)");
    phraseBtn.onClick = [this] { proc.impro.forcePhrase(); };
    styleButton (silenceBtn, col::textDim);
    silenceBtn.setTooltip ("Fait taire Maria (note MIDI 49)");
    silenceBtn.onClick = [this] { proc.impro.forceSilence(); };
    styleButton (clearBtn, col::lock);
    clearBtn.setTooltip ("Efface la memoire (note MIDI 51)");
    clearBtn.onClick = [this] { proc.clearMemory(); };
    for (auto* b : { &learnBtn, &phraseBtn, &silenceBtn, &clearBtn }) addAndMakeVisible (*b);

    tempoBox.addItemList (tempoModes(), 1);
    tempoAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "im_tempo", tempoBox);
    tempoBox.setTooltip ("Libre : rythme naturel | Synchro DAW : calee sur le tempo et la position d'Ableton | Tempo manuel : BPM propre (TAP ou potard BPM) | Pulsation musiciens : calee sur la pulsation qu'elle entend, sans clic");
    gridBox.addItemList (gridNames(), 1);
    gridAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "im_grid", gridBox);
    gridBox.setTooltip ("Grille sur laquelle chaque evenement rejoue est cale (en mode tempo)");
    alignBox.addItemList (alignNames(), 1);
    alignAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "im_align", alignBox);
    alignBox.setTooltip ("Ses phrases demarrent sur le temps, la demi-mesure ou la mesure (en mode tempo)");
    styleButton (tapBtn, juce::Colour (0xffd56bff));
    tapBtn.setTooltip ("Tape le tempo (passe en Tempo manuel)");
    tapBtn.onClick = [this]
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
        taps.push_back (now);
        if (taps.size() > 5) taps.erase (taps.begin());
        if (taps.size() >= 2)
        {
            const double per = (taps.back() - taps.front()) / (double) (taps.size() - 1);
            proc.setParamReal ("bpm", (float) juce::jlimit (40.0, 240.0, 60.0 / per));
            if (tempoBox.getSelectedId() != 3) proc.setParamReal ("im_tempo", 2.0f);
        }
    };
    for (juce::Component* c : { (juce::Component*) &tempoBox, (juce::Component*) &gridBox, (juce::Component*) &alignBox, (juce::Component*) &tapBtn }) addAndMakeVisible (c);

    for (int i = 0; i < kNumIm; ++i)
    {
        bindK (knobs[i], proc, imId (i), imLabel (i), imJid (i), &proc.lockIm[i], &proc.liveIm[i]);
        knobs[i].setAccentColour (juce::Colour (0xffd56bff));
        knobs[i].setChaosSource (&proc.chaosView);
        if (i == IFollow || i == ITransp) knobs[i].setBipolar (true);
        addAndMakeVisible (knobs[i]);
    }
    knobs[IAct].setTooltip ("Envie de jouer : probabilite de lancer une phrase a chaque occasion");
    knobs[IReact].setTooltip ("Temps de silence (ou de jeu en Contrepoint) avant qu'elle reagisse");
    knobs[IPhrase].setTooltip ("Duree moyenne de ses phrases");
    knobs[IGap].setTooltip ("Silences entre ses phrases");
    knobs[ICont].setTooltip ("Haut : longues citations de ton jeu | Bas : recombine sans cesse des fragments");
    knobs[ICtx].setTooltip ("Contexte commun minimal (en evenements) pour sauter ailleurs dans la memoire : haut = plus coherent");
    knobs[IMem].setTooltip ("Bas : puise dans ce que tu viens de jouer | Haut : dans toute la memoire");
    knobs[IFollow].setTooltip ("Droite : choisit des notes / energies proches des tiennes | Gauche : contraste");
    knobs[ITransp].setTooltip ("Transposition de sa voix (vitesse de lecture)");
    knobs[IRev].setTooltip ("Probabilite de jouer un evenement a l'envers");
    knobs[IPolite].setTooltip ("Probabilite qu'elle s'arrete quand tu reprends (Dialogue / Echo)");
    knobs[ILevel].setTooltip ("Niveau de sa voix");
}

void ImproPanel::refresh()
{
    for (auto& k : knobs) k.refreshLive();
    const bool sync = tempoBox.getSelectedId() > 1;
    gridBox.setEnabled (sync); alignBox.setEnabled (sync);
    const bool pulseMode = tempoBox.getSelectedId() == 4;
    const juce::String t = pulseMode ? (proc.pulse.validView.load() ? "pulse " + juce::String (proc.hostBpm(), 0) + " BPM" : juce::String ("cherche la pulsation"))
                                     : (sync ? juce::String (proc.hostBpm(), 1) + " BPM" : juce::String ("rythme libre"));
    if (t != tempoText) { tempoText = t; repaint(); }
}

void ImproPanel::paint (juce::Graphics& g)
{
    g.setColour (col::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (juce::Colour (0xffd56bff));
    g.setFont (font (13.0f, true));
    g.drawText ("IMPROVISATRICE", 10, 6, 130, 18, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (11.0f));
    g.drawText (tempoText, getWidth() - 86, 6, 80, 18, juce::Justification::centredRight);
}

void ImproPanel::resized()
{
    mode.setBounds (130, 4, 112, 22);
    learnBtn.setBounds (246, 4, 66, 22);
    const int bw = (getWidth() - 16) / 3;
    phraseBtn.setBounds (8, 30, bw - 4, 24);
    silenceBtn.setBounds (8 + bw, 30, bw - 4, 24);
    clearBtn.setBounds (8 + 2 * bw, 30, bw - 4, 24);
    tempoBox.setBounds (8, 58, 124, 22);
    gridBox.setBounds (136, 58, 80, 22);
    alignBox.setBounds (220, 58, 112, 22);
    tapBtn.setBounds (336, 58, getWidth() - 344, 22);
    const int kw = (getWidth() - 12) / 6, kh = (getHeight() - 86) / 2;
    for (int i = 0; i < kNumIm; ++i) knobs[i].setBounds (6 + (i % 6) * kw, 84 + (i / 6) * kh, kw, kh);
}

// ===========================================================================
static const char* kHelp =
"MARIA - une improvisatrice qui joue avec ton propre son (dans l'esprit d'OMax, IRCAM)\n\n"
"COMMENT ELLE FONCTIONNE : quand ECOUTE est allume, Maria enregistre ce que tu joues (memoire glissante de 6 minutes, seuls les moments joues sont gardes), "
"le decoupe en evenements (notes, frappes, gestes) et les decrit (hauteur, energie, timbre) selon la SOURCE choisie. Elle construit en direct un Factor Oracle : "
"un graphe qui relie chaque moment a ceux qui partagent le meme contexte musical (les arcs turquoise). Pour improviser, elle rejoue ta memoire en suivant "
"ce graphe : tantot elle cite des passages entiers, tantot elle saute vers un autre moment qui commence comme celui qu'elle joue - d'ou des phrases neuves dans ton style.\n\n"
"MODES : DIALOGUE = elle repond quand tu te tais (et s'arrete quand tu reprends, selon la Politesse) ; DUO = elle joue en meme temps que toi ; "
"ECHO = elle commente ta derniere phrase avec des variations ; CONTREPOINT = elle joue pendant que tu joues et se tait avec toi ; "
"CONTRASTE = elle fait l'inverse des musiciens : douce, rare et en sons longs quand ca joue fort et dense, presente et energique dans le calme.\n\n"
"JEU A PLUSIEURS (onglet) : HARMONIE = elle choisit des fragments qui vont avec l'accord que jouent les musiciens (et les transpose pour tomber juste) ; "
"LEITMOTIVS = MARQUER garde la phrase qui vient d'etre jouee, JOUER la fait revenir plus ou moins variee (comme un theme de film) ; "
"ECOUTE CROISEE = avec la sidechain, Maria peut apprendre un musicien et reagir a un autre ; CORPUS = une 2e memoire preparee a l'avance "
"(sons industriels, repetition, ancienne session) melangee ou non au direct ; SORTIES = voix 1 a gauche, voix 2 a droite pour les traiter a part.\n\n"
"FORME / MIDI (onglet) : CHAPITRES = la memoire est decoupee comme le film, Maria puise dans le chapitre en cours, une scene ancienne ou partout ; "
"ARC = montee ou descente lente de son intensite ; PULSATION = elle se cale sur le tempo que vous jouez, sans clic (mode de tempo 'Pulsation musiciens') ; "
"VOIX MIDI = elle apprend un clavier MIDI et improvise en MIDI avec ses notes. Effet CROSS-SYNTHESE : la phrase de Maria avec le timbre d'un autre musicien (sidechain).\n\n"
"MACHINE / NAPPES (onglet) : mode MACHINE = un ostinato tire de ta memoire, cale sur la grille, qui mute lentement ; NAPPES = sons tenus geles a partir de la memoire, "
"dans l'harmonie entendue ; RETOUR D'ETAT = Maria renvoie son etat en MIDI (Wing, controleur) ; FICHIERS = taille du dossier, nettoyage. "
"Une SAUVEGARDE DE SECOURS est ecrite toutes les 2 minutes : apres un plantage, Maria propose de la recharger.\n\n"
"REGLAGES : ACTIVITE (envie de jouer), REACTIVITE (delai avant de reagir), PHRASE et SILENCES (durees), CONTINUITE (citations longues <-> recombinaison), "
"CONTEXTE (exigence de ressemblance pour sauter), MEMOIRE (ce que tu viens de jouer <-> tout), SUIVI (droite : notes / energie proches des tiennes ; gauche : contraste), "
"TRANSPO, REVERSE, POLITESSE, NIVEAU. Chaque potard a son anneau de hasard (clic droit-glisser) : avec l'HORLOGE sur 'Chaque segment' ou 'Chaque phrase', "
"les valeurs sont re-tirees a chaque evenement / phrase qu'elle joue.\n\n"
"SA VOIX passe dans 4 slots d'effets (delay, granular, reverbs, shimmer, reverse, modulation, filtre, pitch, degrade, distorsion, ring, gate, resonator, corps). "
"CHANCE/SEG. : chaque evenement joue par Maria passe dans l'effet ou non.\n\n"
"MEMOIRE : EFFACER pour repartir de zero ; onglet ECOUTE / MEMOIRE : CHARGER UN FICHIER AUDIO (un enregistrement de repetition, un autre instrument...) "
"et SAUVER LA MEMOIRE en WAV.\n\n"
"CONCERT : Maria sort uniquement sa voix (MIX 100 %), ton son direct reste sur la Wing. Attention au repiquage : si le micro capte la sono, Maria s'ecoute elle-meme ; "
"privilegie un micro proche / un retour casque, ou coupe ECOUTE pendant ses phrases.\n\n"
"MIDI : 36-43 scenes | 44 DE | 45 MUTATION | 46 FREEZE | 47 PANIQUE | 48 PHRASE ! | 49 SILENCE | 50 ECOUTE on/off | 51 EFFACER | "
"52-55 MARQUER leitmotiv A-D | 56-59 JOUER leitmotiv A-D | 60 CHAPITRE SUIVANT | 61 ARC MONTER | 62 ARC DESCENDRE | 63 ARC NEUTRE ; CC par MIDI Learn.\n";

MariaView::MariaView (MariaProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    alea::gui::RndKnob::editRandomMode = false;

    sourceBox.addItemList (sourceNames(), 1);
    sourceAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "source", sourceBox);
    sourceBox.setTooltip ("Ce que tu joues : regle l'analyse (notes ou timbres), la detection des attaques et la taille des evenements");
    addAndMakeVisible (sourceBox);
    styleButton (adaptBtn, juce::Colour (0xffe2464b));
    adaptBtn.setTooltip ("Regle Maria et ses effets pour cette source (+ 8 scenes)");
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
        chooser = std::make_unique<juce::FileChooser> ("Ouvrir un preset Maria", presetDir(), "*.map");
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
    panicBtn.setTooltip ("Coupe en fondu toutes les queues, le freeze et les phrases de Maria, puis repart a zero - note MIDI 47");
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
    bindK (bpmK, proc, "bpm", "BPM");
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
    oracle = std::make_unique<OracleView> (proc);
    improPanel = std::make_unique<ImproPanel> (proc);
    addAndMakeVisible (*oracle);
    addAndMakeVisible (*improPanel);
    tabs.addTab ("ECOUTE / MEMOIRE", col::panel, sourcePage, true);
    optionsPage = new OptionsPage (proc);
    tabs.addTab ("2e VOIX / MIDI / OPTIONS", col::panel, optionsPage, true);
    ensemblePage = new EnsemblePage (proc);
    tabs.addTab ("JEU A PLUSIEURS", col::panel, ensemblePage, true);
    formePage = new FormePage (proc);
    tabs.addTab ("FORME / MIDI", col::panel, formePage, true);
    machinePage = new MachinePage (proc);
    tabs.addTab ("MACHINE / NAPPES", col::panel, machinePage, true);
    styleButton (recoverBtn, juce::Colour (0xffffc857));
    recoverBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffffc857).withAlpha (0.8f));
    recoverBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    recoverBtn.onClick = [this] { proc.acceptRecovery(); };
    dismissBtn.onClick = [this] { proc.dismissRecovery(); };
    addChildComponent (recoverBtn); addChildComponent (dismissBtn);
    tabs.addTab ("MODULATION", col::panel, modPage, true);
    tabs.addTab ("MACROS (WING)", col::panel, macroPage, true);
    tabs.addTab ("SCENES", col::panel, scenePage, true);
    tabs.addTab ("AIDE", col::panel, &help, false);
    tabs.setTabBarDepth (26);
    tabs.setOutline (0);
    addAndMakeVisible (tabs);

    setSize (1294, 900);
    startTimerHz (30);
}

MariaView::~MariaView()
{
    stopTimer();
    alea::gui::RndKnob::editRandomMode = false;
    setLookAndFeel (nullptr);
}

juce::File MariaView::presetDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Maria Presets");
    d.createDirectory();
    return d;
}

void MariaView::fillPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < numFactoryPresets(); ++i) presetBox.addItem (factoryPresetName (i), i + 1);
    userFiles = presetDir().findChildFiles (juce::File::findFiles, false, "*.map");
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

void MariaView::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Sauver le preset Maria", presetDir().getChildFile ("Mon preset.map"), "*.map");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension ("map");
        if (auto xml = proc.captureState().createXml()) xml->writeTo (f);
        fillPresetBox();
        for (int i = 0; i < userFiles.size(); ++i) if (userFiles[i] == f) presetBox.setSelectedId (1000 + i, juce::dontSendNotification);
    });
}

void MariaView::loadPresetFile (const juce::File& f)
{
    if (auto xml = juce::XmlDocument::parse (f))
    {
        proc.pushUndo();
        proc.restoreState (juce::ValueTree::fromXml (*xml));
    }
}

void MariaView::timerCallback()
{
    for (auto& s : slots) s->refresh();
    for (auto* k : { &chaosK, &rateK, &glideK, &evolveK, &mixK, &inK, &outK, &widthK, &duckK, &bpmK }) k->refreshLive();
    modPage->refresh();
    macroPage->refresh();
    scenePage->refresh();
    sourcePage->refresh();
    improPanel->refresh();
    optionsPage->refresh();
    ensemblePage->refresh();
    formePage->refresh();
    machinePage->refresh();
    {
        const bool rec = proc.recoveryAvailable.load();
        if (recoverBtn.isVisible() != rec)
        {
            recoverBtn.setButtonText ("RECHARGER LA MEMOIRE DE SECOURS (" + proc.recoveryInfo + ")");
            recoverBtn.setVisible (rec); dismissBtn.setVisible (rec);
            if (rec) { recoverBtn.toFront (false); dismissBtn.toFront (false); }
        }
    }
    if (++frame % 2 == 0) oracle->refresh();
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

void MariaView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    // titre
    alea::gui::metro::drawSkyline (g, juce::Rectangle<float> (0.0f, 4.0f, (float) getWidth(), 48.0f), col::text.withAlpha (0.045f));
    {
        const bool plays = proc.impro.status.load() == Improviser::SPlay || proc.impro2.status.load() == Improviser::SPlay;
        alea::gui::metro::drawDoubleHalo (g, { 150.0f, 32.0f }, 18.0f, juce::Colour (0xffd56bff), juce::Colour (0xffb8c2cc), plays ? 0.6f + 0.4f * juce::jlimit (0.0f, 1.0f, meterOut * 2.0f) : 0.0f);
    }
    g.setColour (juce::Colour (0xffd56bff));
    g.setFont (font (30.0f, true));
    g.drawText ("MARIA", 14, 4, 140, 34, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (10.5f));
    g.drawText ("ton double improvisateur", 16, 36, 150, 14, juce::Justification::centredLeft);
    g.drawText ("SOURCE", 172, 1, 80, 12, juce::Justification::centredLeft);
    g.setColour (col::lock.withAlpha (0.15f + 0.85f * hitFlash));
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

void MariaView::resized()
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

    oracle->setBounds (8, 136, 856, 252);
    recoverBtn.setBounds (20, 340, 460, 32);
    dismissBtn.setBounds (488, 340, 90, 32);
    improPanel->setBounds (870, 136, getWidth() - 878, 252);
    const int sw = (getWidth() - 16 - 3 * 6) / 4, sh = 236, y0 = 394;
    for (int s = 0; s < kNumSlots; ++s) slots[s]->setBounds (8 + s * (sw + 6), y0, sw, sh);
    tabs.setBounds (8, y0 + sh + 6, getWidth() - 16, getHeight() - (y0 + sh + 6) - 8);
}

// ===========================================================================
MariaEditor::MariaEditor (MariaProcessor& p) : juce::AudioProcessorEditor (p), proc (p), view (p)
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

void MariaEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    view.setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
} // namespace ma
