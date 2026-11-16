#include "TaleaEditor.h"

using namespace talea;
using namespace alea::gui;

static juce::String patLetter (int p) { return juce::String::charToString ((juce::juce_wchar) ('A' + p)); }
static const juce::Colour tealCol { 0xff35d0c3 };

TaleaView::TaleaView (TaleaAudioProcessor& p)
    : proc (p),
      dragMidi ([&p] { return p.exportMidi (p.editPattern(), 4); }),
      roll (p), lanes (p),
      scope (p.scopeBuf, TaleaAudioProcessor::kScopeLen, p.scopeVersion, [] { return tealCol; })
{
    lnf.setColourScheme ({ col::bg, col::panelHi, col::panel, col::line, col::text, col::panelHi, juce::Colours::white, tealCol, col::text });
    lnf.setColour (juce::TextButton::buttonOnColourId, tealCol);
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::outlineColourId, col::line);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, col::line);
    lnf.setColour (juce::Slider::trackColourId, tealCol.withAlpha (0.6f));
    lnf.setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    lnf.setColour (juce::TextEditor::backgroundColourId, col::panelHi);
    lnf.setColour (juce::TextEditor::outlineColourId, col::line);
    lnf.setColour (juce::ToggleButton::tickColourId, tealCol);
    setLookAndFeel (&lnf);
    auto& ap = proc.apvts;
    auto pat = [this] { return proc.editPattern(); };

    // ---------------- barre du haut
    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id <= TaleaAudioProcessor::factoryPresetNames().size()) proc.loadFactoryPreset (id - 1);
        else if (id >= 100 && id - 100 < userPresets.size()) proc.loadPresetFile (userPresets[id - 100]);
        else return;
        refreshAfterStateChange();
    };
    action (presetPrev, *this, false, [this] { stepPreset (-1); });
    action (presetNext, *this, false, [this] { stepPreset (1); });
    action (presetSave, *this, false, [this]
    {
        auto dir = TaleaAudioProcessor::userPresetDir(); dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Enregistrer le preset", dir.getChildFile (proc.currentPresetName + ".talea"), "*.talea");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f == juce::File()) return;
                                                                      proc.savePresetFile (f.withFileExtension ("talea")); refreshPresetList(); });
    });
    refreshPresetList();
    action (undoBtn, *this, false, [this] { if (proc.undo()) refreshAfterStateChange(); });
    action (redoBtn, *this, false, [this] { if (proc.redo()) refreshAfterStateChange(); });
    attachToggle (midiOutBtn, *this, "midiout");
    midiOutBtn.setTooltip ("Envoie les notes de basse en MIDI (avec les slides en legato)");
    addAndMakeVisible (dragMidi);
    dragMidi.setTooltip ("Glisser vers une piste MIDI : 4 mesures de la ligne de basse");
    rndEditBtn.setClickingTogglesState (true);
    rndEditBtn.setColour (juce::TextButton::buttonOnColourId, col::rnd);
    rndEditBtn.onClick = [this] { RndKnob::editRandomMode = rndEditBtn.getToggleState(); repaint(); };
    addAndMakeVisible (rndEditBtn);
    action (diceBtnTop, *this, true, [this] { proc.diceSound (false); });
    diceBtnTop.setTooltip ("De sur le son (parametres non verrouilles)");
    for (auto* s : { &bpmSlider, &seedSlider })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 22);
        addAndMakeVisible (*s);
    }
    sliderAtts.push_back (std::make_unique<juce::SliderParameterAttachment> (*ap.getParameter ("bpm"), bpmSlider));
    sliderAtts.push_back (std::make_unique<juce::SliderParameterAttachment> (*ap.getParameter ("seed"), seedSlider));
    bpmSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " BPM"; };
    seedSlider.textFromValueFunction = [] (double v) { return "Seed " + juce::String ((int) v); };
    for (auto* s : { &bpmSlider, &seedSlider })
    {
        s->valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue(); };
        s->updateText();
    }
    attachCombo (rootBox, *this, "root", rootNames());
    rootBox.setTooltip ("Tonique (partagee avec Alea et les autres Talea via AleaLink)");
    attachCombo (scaleBox, *this, "scale", alea::scaleNames());
    scaleBox.setTooltip ("Gamme : les notes, les generateurs et l'alea de hauteur restent dedans");
    attachToggle (playBtn, *this, "play");
    for (int i = 0; i < kPatterns; ++i)
    {
        auto& b = patBtns[i];
        b.setButtonText (patLetter (i));
        b.onClick = [this, i] { proc.setParamReal ("pattern", (float) i); };
        b.setTooltip ("Pattern " + patLetter (i) + " (MIDI : C1..G1). Suit les changements de pattern d'Alea si LINK > Pattern est coche");
        addAndMakeVisible (b);
    }
    copyPatBox.setTextWhenNothingSelected ("Copy to...");
    for (int i = 0; i < kPatterns; ++i) copyPatBox.addItem (patLetter (i), i + 1);
    copyPatBox.onChange = [this] { const int id = copyPatBox.getSelectedId(); if (id > 0) { proc.pushUndo(); proc.copyPattern (proc.editPattern(), id - 1); }
                                   copyPatBox.setSelectedId (0, juce::dontSendNotification); };
    addAndMakeVisible (copyPatBox);
    attachToggle (songBtn, *this, "songmode");
    chainEdit.setText (proc.getChainText(), false);
    chainEdit.setFont (font (13.0f));
    auto commit = [this] { const bool ok = proc.setChainText (chainEdit.getText()); chainEdit.applyColourToAllText (ok ? col::text : col::lock); };
    chainEdit.onReturnKey = commit; chainEdit.onFocusLost = commit;
    addAndMakeVisible (chainEdit);
    fillBtn.setColour (juce::TextButton::buttonColourId, col::rnd.darker (0.9f));
    fillBtn.onStateChange = [this] { const bool d = fillBtn.isDown(); if (d != fillWasDown) { fillWasDown = d; proc.setParamNorm ("fill", d ? 1.0f : 0.0f); } };
    addAndMakeVisible (fillBtn);
    attachCombo (rndModeBox, *this, "rndmode", { "Mode: Hit", "Mode: Bar", "Mode: Drift" });
    attachCombo (distBox, *this, "rnddist", { "Dist: Uniform", "Dist: Gauss", "Dist: Rare" });
    linkBtn.onClick = [this] { showTab (5); };
    linkBtn.setTooltip ("AleaLink : instances detectees et synchronisation");
    addAndMakeVisible (linkBtn);
    setupKnob (chaosKnob, *this, "chaos", "CHAOS", false, col::rnd);
    setupKnob (masterKnob, *this, "master", "Master", false, tealCol);

    // ---------------- sequenceur
    addAndMakeVisible (roll);
    addAndMakeVisible (lanes);
    roll.onLockStep = [this] (int i) { setLockStep (i); };
    roll.setTooltip ("Clic : poser / effacer une note | glisser : dessiner | Maj : accent | Cmd-clic : p-locks | clic droit : menu du pas | Alt-clic : longueur");
    setupKnob (swingK, *this, "swing", "Swing", false, tealCol);
    setupKnob (humanK, *this, "humanize", "Humanize", false, col::rnd);
    setupKnob (seqChaosK, *this, "seqchaos", "Seq Chaos", false, col::rnd);
    setupKnob (ratChaosK, *this, "ratchaos", "Ratch Chaos", false, col::rnd);
    setupKnob (densityK, *this, "density", "Density", true, col::rnd);
    setupKnob (noteChaosK, *this, "notechaos", "Note Chaos", false, col::rnd);
    noteChaosK.setTooltip ("Change des notes au vol (dans la gamme), sans modifier le pattern");

    for (auto* s : { &lenSlider, &hitsSlider })
    {
        s->setSliderStyle (juce::Slider::IncDecButtons);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 24);
        s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        addAndMakeVisible (*s);
    }
    lenSlider.setRange (1, kSteps, 1); lenSlider.setTextValueSuffix (" st");
    hitsSlider.setRange (1, kSteps, 1); hitsSlider.setTextValueSuffix (" hits"); hitsSlider.setValue (7, juce::dontSendNotification);
    lenSlider.onValueChange = [this] { editPat().length = (int8_t) lenSlider.getValue(); roll.repaint(); lanes.repaint(); };
    rateBox.addItemList (rateNames(), 1);
    rateBox.onChange = [this] { editPat().rate = (int8_t) (rateBox.getSelectedId() - 1); };
    dirBox.addItemList (dirNames(), 1);
    dirBox.onChange = [this] { editPat().dir = (int8_t) (dirBox.getSelectedId() - 1); };
    grooveBox.addItemList (grooveNames(), 1);
    grooveBox.onChange = [this] { editPat().groove = (int8_t) (grooveBox.getSelectedId() - 1); };
    for (auto* c : { &rateBox, &dirBox, &grooveBox }) addAndMakeVisible (*c);
    attachCombo (genStyleBox, *this, "genstyle", genStyleNames());
    genStyleBox.setTooltip ("Style du generateur de lignes (reglages fins dans l'onglet LAB)");
    action (genBtn, *this, true, [=] { proc.generatePattern (pat(), (int) proc.paramReal ("genstyle")); refreshTools(); });
    action (varyBtn, *this, true, [=] { proc.varyPattern (pat(), 0.35f); });
    action (mutNBtn, *this, true, [=] { proc.mutateNotes (pat(), 0.25f); });
    action (mutRBtn, *this, true, [=] { proc.mutateRhythm (pat(), 0.12f); });
    action (trDnBtn, *this, true, [=] { proc.transposePattern (pat(), -1); });
    action (trUpBtn, *this, true, [=] { proc.transposePattern (pat(), 1); });
    action (ocDnBtn, *this, true, [=] { proc.octavePattern (pat(), -1); });
    action (ocUpBtn, *this, true, [=] { proc.octavePattern (pat(), 1); });
    action (revBtn, *this, true, [=] { proc.reversePattern (pat()); });
    action (invBtn, *this, true, [=] { proc.invertPattern (pat()); });
    action (shlBtn, *this, true, [=] { proc.shiftPattern (pat(), -1); });
    action (shrBtn, *this, true, [=] { proc.shiftPattern (pat(), 1); });
    action (euclidBtn, *this, true, [=] { proc.euclidPattern (pat(), (int) hitsSlider.getValue(), 0); });
    action (clearBtn, *this, true, [=] { proc.clearPattern (pat()); });
    action (copyBtn, *this, false, [=] { proc.copyClip (pat()); });
    action (pasteBtn, *this, true, [=] { proc.pasteClip (pat()); refreshTools(); });
    trDnBtn.setTooltip ("Transpose d'un degre de gamme vers le bas"); trUpBtn.setTooltip ("Transpose d'un degre de gamme vers le haut");
    invBtn.setTooltip ("Inverse la melodie (miroir autour de la tonique)");
    varyBtn.setTooltip ("Variation apprise (Markov) : garde le style de la ligne");
    previewBtn.setClickingTogglesState (true);
    previewBtn.setToggleState (true, juce::dontSendNotification);
    previewBtn.setColour (juce::TextButton::buttonOnColourId, col::rnd);
    previewBtn.onClick = [this] { roll.showPreview = previewBtn.getToggleState(); roll.repaint(); };
    addAndMakeVisible (previewBtn);
    lockCondBox.addItemList (condNames(), 1);
    lockCondBox.onChange = [this] { if (lockI >= 0) proc.bank.pats[lockPat].steps[lockI].cond = (int8_t) (lockCondBox.getSelectedId() - 1); };
    addChildComponent (lockCondBox);
    action (clearLocksBtn, *this, true, [this] { if (lockI >= 0) proc.bank.pats[lockPat].steps[lockI].clearLocks(); });
    action (exitLockBtn, *this, false, [this] { setLockStep (-1); });
    clearLocksBtn.setVisible (false); exitLockBtn.setVisible (false);

    // ---------------- onglets
    const char* tabNames[kNumTabs] { "SYNTH", "MOD", "FX", "PERFORM", "LAB", "LINK", "AIDE" };
    for (int i = 0; i < kNumTabs; ++i)
    {
        tabBtns[i].setButtonText (tabNames[i]);
        tabBtns[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabBtns[i]);
    }
    linkPanel = std::make_unique<LinkPanel> (*proc.link, ap,
        "Talea lit les coups d'Alea a l'avance : sidechain (onglet FX), modulateur Drums (MOD), generateurs Follow / Avoid kick (LAB). "
        "Avec Color : Chord follow transpose la ligne sur les accords publies.");
    for (juce::Component* pn : { (juce::Component*) &synthPanel, (juce::Component*) &modPanel, (juce::Component*) &fxPanel,
                                 (juce::Component*) &perfPanel, (juce::Component*) &labPanel, (juce::Component*) linkPanel.get(), (juce::Component*) &helpPanel })
        addChildComponent (*pn);
    attachCombo (chordFollowBox, linkPanel->extraArea, "chordfollow", { "Chords: Off", "Chords: Root", "Chords: Chord tones" });
    chordFollowBox.setTooltip ("Suivi des accords de Color : Root = la ligne est transposee sur la basse de l'accord ; "
                               "Chord tones = chaque note est ramenee sur une note de l'accord");

    // ---------------- SYNTH
    attachCombo (osc1Box, synthPanel, "osc1type", osc1Names());
    attachCombo (osc2Box, synthPanel, "osc2type", osc2Names());
    attachCombo (subBox, synthPanel, "subtype", subNames());
    attachCombo (filtBox, synthPanel, "ftype", filterNames());
    attachCombo (driveBox, synthPanel, "drivetype", driveNames());
    attachCombo (chordBox, synthPanel, "chord", chordNames());
    attachCombo (glideBox, synthPanel, "glidemode", glideModeNames());
    osc1Box.setTooltip ("Oscillateur 1 (Shape : largeur d'impulsion, desaccord du SuperSaw, repliement, amorti du Pluck)");
    baseOctSlider.setSliderStyle (juce::Slider::IncDecButtons);
    baseOctSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 22);
    synthPanel.addAndMakeVisible (baseOctSlider);
    sliderAtts.push_back (std::make_unique<juce::SliderParameterAttachment> (*ap.getParameter ("baseoct"), baseOctSlider));
    baseOctSlider.textFromValueFunction = [] (double v) { return "Oct " + juce::String ((int) v); };
    baseOctSlider.updateText();
    action (keepBtn, synthPanel, true, [this] { proc.keepLastNote(); });
    action (diceBtn, synthPanel, true, [this] { proc.diceSound (false); });
    action (diceTypesBtn, synthPanel, true, [this] { proc.diceSound (true); });
    action (mutBtn, synthPanel, true, [this] { proc.mutateSound (0.08f); });
    action (rndAmtBtn, synthPanel, true, [this] { proc.randomizeJitter(); });
    action (zeroRndBtn, synthPanel, true, [this] { proc.clearJitter(); });
    action (auditionBtn, synthPanel, false, [this] { proc.guiTrig = true; });
    keepBtn.setColour (juce::TextButton::buttonColourId, col::plock.darker (1.2f));
    diceBtn.setColour (juce::TextButton::buttonColourId, col::rnd.darker (0.9f));
    diceTypesBtn.setColour (juce::TextButton::buttonColourId, col::rnd.darker (0.9f));
    lockTypesBtn.setClickingTogglesState (true);
    lockTypesBtn.setColour (juce::TextButton::buttonOnColourId, col::lock);
    lockTypesBtn.setToggleState (proc.locks[kNumTP].load(), juce::dontSendNotification);
    lockTypesBtn.onClick = [this] { proc.locks[kNumTP] = lockTypesBtn.getToggleState(); };
    synthPanel.addAndMakeVisible (lockTypesBtn);
    auto* chaosRaw = ap.getRawParameterValue ("chaos");
    for (int i = 0; i < kNumTP; ++i)
    {
        auto& k = tpKnobs[i];
        k.setProcessor (&proc);
        k.bind (ap.getParameter (tpId (i)), ap.getParameter (tpJit (i)), &proc.locks[i], &proc.lastNorm[i], tpInfo (i).label);
        k.setChaosSource (chaosRaw);
        k.setBipolar (tpBipolar (i));
        k.setAccentColour (i >= Cutoff && i <= FDrive ? juce::Colour (0xffff7a1a) : tealCol);
        k.onPlockEdited = [this] { roll.repaint(); };
        synthPanel.addAndMakeVisible (k);
    }
    setupKnob (velSensK, synthPanel, "velsens", "Vel Sens", false, tealCol);
    setupKnob (linkRndK, synthPanel, "link_rnd", "Rnd Link", false, col::rnd);
    synthPanel.addAndMakeVisible (scope);
    synthPanel.painter = [this] (juce::Graphics& g)
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (synthPanel.getLocalBounds().toFloat(), 6.0f);
        g.setColour (tealCol);
        g.setFont (font (12.0f, true));
        g.drawText ("SYNTH", 12, 6, 80, 20, juce::Justification::centredLeft);
        if (lockI >= 0)
        {
            g.setColour (col::plock);
            g.drawText ("P-LOCK pas " + juce::String (lockI + 1), 12, 26, 160, 14, juce::Justification::centredLeft);
        }
        g.setColour (col::textDim);
        g.setFont (font (10.5f));
        g.drawText ("OSCILLATEURS", 12, 40, 200, 12, juce::Justification::centredLeft);
        g.drawText ("FILTRE (orange)", 12 + 7 * 70, 40, 200, 12, juce::Justification::centredLeft);
        g.drawText ("ENVELOPPES / SORTIE", 12, 140, 200, 12, juce::Justification::centredLeft);
        g.drawFittedText ("Les potards agissent en continu sur la note tenue (ideal pour tourner le cutoff en direct). Accent : ouvre le filtre, "
                          "raccourcit sa decroissance et s'accumule sur les accents successifs (comme la 303). Glide : temps de slide.",
                          juce::Rectangle<int> (12, 250, 900, 40), juce::Justification::topLeft, 3);
    };

    // ---------------- MOD
    for (int m = 0; m < alea::kNumMods; ++m)
    {
        attachCombo (modRateBox[m], modPanel, "m" + juce::String (m + 1) + "rate", modRateNames());
        static const char* pn[alea::kNumMods] { "Chance", "Jump", "Change", "R", "Shape", "Release" };
        setupKnob (modPK[m], modPanel, "m" + juce::String (m + 1) + "p", pn[m], false, col::rnd);
        modDisp[m] = std::make_unique<ModDisplay> (proc.modValue[m]);
        modPanel.addAndMakeVisible (*modDisp[m]);
    }
    modRateBox[5].setVisible (false);
    turLenSlider.setSliderStyle (juce::Slider::IncDecButtons);
    turLenSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 22);
    modPanel.addAndMakeVisible (turLenSlider);
    sliderAtts.push_back (std::make_unique<juce::SliderParameterAttachment> (*ap.getParameter ("m3len"), turLenSlider));
    turLenSlider.setTextValueSuffix (" bits");
    attachCombo (followTrkBox, modPanel, "m6trk", [] { juce::StringArray a; for (int t = 0; t < 8; ++t) a.add ("Drums T" + juce::String (t + 1)); return a; }());
    setupKnob (modDepthK, modPanel, "moddepth", "Mod Depth", false, col::rnd);
    {
        juce::StringArray src { "Off", "S&H", "Smooth", "Turing", "Chaos", "LFO", "Drums" };
        juce::StringArray dst; for (int i = 0; i < kNumTP; ++i) dst.add (tpInfo (i).label);
        dst.addArray (gtNames());
        for (int s = 0; s < kMxSlots; ++s)
        {
            const juce::String pre = "mx" + juce::String (s + 1);
            attachCombo (mxSrc[s], modPanel, pre + "src", src);
            attachCombo (mxDst[s], modPanel, pre + "dst", dst);
            mxAmt[s].setSliderStyle (juce::Slider::LinearHorizontal);
            mxAmt[s].setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            mxAmt[s].setColour (juce::Slider::trackColourId, col::rnd.withAlpha (0.7f));
            modPanel.addAndMakeVisible (mxAmt[s]);
            sliderAtts.push_back (std::make_unique<juce::SliderParameterAttachment> (*ap.getParameter (pre + "amt"), mxAmt[s]));
            mxAmt[s].setDoubleClickReturnValue (true, 0.0);
        }
    }
    modPanel.painter = [this] (juce::Graphics& g)
    {
        static const char* names[alea::kNumMods] { "S&H", "Smooth", "Turing", "Chaos", "LFO", "Drums" };
        static const char* desc[alea::kNumMods] { "Valeur au hasard a chaque pas", "Marche aleatoire lissee", "Boucle qui mute",
                                                  "Logistique", "Sinus > tri > scie > carre > paliers", "Suit les coups d'Alea (AleaLink)" };
        for (int m = 0; m < alea::kNumMods; ++m)
        {
            auto r = juce::Rectangle<int> (m * 216, 0, 210, 150);
            g.setColour (col::panel); g.fillRoundedRectangle (r.toFloat(), 6.0f);
            g.setColour (col::rnd); g.setFont (font (12.0f, true));
            g.drawText ("MOD " + juce::String (m + 1) + "  " + names[m], r.getX() + 8, 4, 200, 20, juce::Justification::centredLeft);
            g.setColour (col::textDim); g.setFont (font (10.0f));
            g.drawText (desc[m], r.getX() + 8, 130, 200, 16, juce::Justification::centredLeft);
        }
        g.setColour (col::panel); g.fillRoundedRectangle (juce::Rectangle<float> (0, 158, 1296, 178), 6.0f);
        g.setColour (col::text); g.setFont (font (12.0f, true));
        g.drawText ("MATRICE DE MODULATION (continue : agit aussi sur la note tenue ; cibles G: = effets et macros)", 10, 162, 900, 18, juce::Justification::centredLeft);
    };

    // ---------------- FX
    setupKnob (chRateK, fxPanel, "chrate", "Rate", false, tealCol);
    setupKnob (chDepthK, fxPanel, "chdepth", "Depth", false, tealCol);
    setupKnob (chMixK, fxPanel, "chmix", "Mix", false, tealCol);
    attachCombo (dlyDivBox, fxPanel, "dlydiv", delayDivNames());
    attachToggle (pingBtn, fxPanel, "dlyping");
    setupKnob (dlyFbK, fxPanel, "dlyfb", "Feedback", false, tealCol);
    setupKnob (dlyToneK, fxPanel, "dlytone", "Tone", false, tealCol);
    setupKnob (dlyRetK, fxPanel, "dlyret", "Return", false, tealCol);
    setupKnob (dlyRndK, fxPanel, "dlyrnd", "Time Rnd", false, col::rnd);
    setupKnob (revSizeK, fxPanel, "revsize", "Size", false, tealCol);
    setupKnob (revDampK, fxPanel, "revdamp", "Damp", false, tealCol);
    setupKnob (revWidthK, fxPanel, "revwidth", "Width", false, tealCol);
    setupKnob (revRetK, fxPanel, "revret", "Return", false, tealCol);
    setupKnob (revPreK, fxPanel, "revpre", "Predelay", false, tealCol);
    attachCombo (duckSrcBox, fxPanel, "ducksrc", [] { juce::StringArray a { "Duck: Off" }; for (int t = 0; t < 8; ++t) a.add ("Duck by Alea T" + juce::String (t + 1)); return a; }());
    setupKnob (duckAmtK, fxPanel, "duckamt", "Amount", false, col::rnd);
    setupKnob (duckRelK, fxPanel, "duckrel", "Release", false, tealCol);
    setupKnob (glueK, fxPanel, "glue", "Glue", false, tealCol);
    setupKnob (mDriveK, fxPanel, "mdrive", "Drive", false, tealCol);
    fxPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> a[] { { 0, 0, 280, 150 }, { 290, 0, 420, 150 }, { 720, 0, 400, 150 }, { 1130, 0, 166, 150 }, { 0, 160, 560, 170 } };
        const char* t[] { "CHORUS", "DELAY", "REVERB (plate)", "MASTER", "SIDECHAIN par Alea (AleaLink)" };
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (col::panel); g.fillRoundedRectangle (a[i].toFloat(), 6.0f);
            g.setColour (i == 4 ? col::rnd : col::textDim); g.setFont (font (12.0f, true));
            g.drawText (t[i], a[i].getX() + 10, a[i].getY() + 4, 300, 20, juce::Justification::centredLeft);
        }
        auto m = juce::Rectangle<float> (480.0f, 196.0f, 30.0f, 100.0f);
        g.setColour (col::bg); g.fillRect (m);
        g.setColour (col::rnd); g.fillRect (m.withHeight (m.getHeight() * juce::jlimit (0.0f, 1.0f, proc.duckView.load())));
        g.setFont (font (11.0f));
        const int slot = proc.linkedDrums.load();
        g.setColour (slot >= 0 ? col::rnd : col::textDim);
        g.drawFittedText (slot >= 0 ? "Batterie liee : Alea #" + juce::String (slot + 1) + ". Les coups sont lus a l'avance : le ducking tombe pile."
                                    : juce::String ("Aucune instance d'Alea liee : ouvre Alea sur une autre piste (LINK actif)."),
                          juce::Rectangle<int> (10, 300, 540, 28), juce::Justification::topLeft, 2);
    };

    // ---------------- PERFORM
    setupKnob (morphK, perfPanel, "morph", "MORPH A>B", false, col::plock);
    action (storeBBtn, perfPanel, false, [this] { proc.storeB(); perfPanel.repaint(); });
    action (recallBBtn, perfPanel, true, [this] { proc.recallB(); });
    action (swapBBtn, perfPanel, true, [this] { proc.swapB(); });
    setupKnob (densityK2, perfPanel, "density", "Density", true, col::rnd);
    setupKnob (chaosK2, perfPanel, "chaos", "Chaos", false, col::rnd);
    setupKnob (seqChaosK2, perfPanel, "seqchaos", "Seq Chaos", false, col::rnd);
    setupKnob (modDepthK2, perfPanel, "moddepth", "Mod Depth", false, col::rnd);
    setupKnob (duckK2, perfPanel, "duckamt", "Duck", false, col::rnd);
    setupKnob (evoK2, perfPanel, "evoamt", "Evolve", false, col::rnd);
    attachToggle (fillLatchBtn, perfPanel, "fill");
    for (int n = 0; n < kScenes; ++n)
    {
        auto& b = sceneBtns[n];
        b.setButtonText (juce::String (n + 1));
        b.onClick = [this, n]
        {
            if (sceneStoreBtn.getToggleState() || juce::ModifierKeys::currentModifiers.isShiftDown())
            { proc.storeScene (n); sceneStoreBtn.setToggleState (false, juce::dontSendNotification); }
            else if (proc.sceneValid[n].load()) { proc.pushUndo(); proc.recallScene (n); }
        };
        b.setTooltip ("Scene " + juce::String (n + 1) + " : clic = rappel, Maj-clic = memoriser, MIDI C0+" + juce::String (n) + ". Synchronisee avec Alea (LINK > Scene)");
        perfPanel.addAndMakeVisible (b);
    }
    sceneStoreBtn.setClickingTogglesState (true);
    sceneStoreBtn.setColour (juce::TextButton::buttonOnColourId, col::lock);
    perfPanel.addAndMakeVisible (sceneStoreBtn);
    {
        juce::StringArray sc; for (int n = 0; n < kScenes; ++n) sc.add ("Scene " + juce::String (n + 1));
        attachCombo (sceneABox, perfPanel, "scenea", sc);
        attachCombo (sceneBBox, perfPanel, "sceneb", sc);
    }
    setupKnob (sceneXfK, perfPanel, "scenexf", "Crossfade", false, col::plock);
    attachToggle (sceneXfOnBtn, perfPanel, "sceneon");
    attachCombo (keysBox, perfPanel, "keysmode", keysModeNames());
    keysBox.setTooltip ("Transpose : une note (C3 = rien) transpose la ligne, comme la 303 | Play : le clavier joue la basse (legato = slide)");
    perfPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> a[] { { 0, 0, 300, 150 }, { 310, 0, 640, 150 }, { 960, 0, 336, 150 }, { 0, 160, 560, 170 }, { 570, 160, 726, 170 } };
        const char* t[] { "MORPH A / B", "MACROS (clic droit = MIDI learn)", "SCENES", "SONG MODE", "CLAVIER" };
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (col::panel); g.fillRoundedRectangle (a[i].toFloat(), 6.0f);
            g.setColour (i == 0 || i == 2 ? col::plock : col::textDim); g.setFont (font (12.0f, true));
            g.drawText (t[i], a[i].getX() + 10, a[i].getY() + 4, 400, 20, juce::Justification::centredLeft);
        }
        g.setFont (font (11.0f));
        g.setColour (proc.hasB.load() ? col::plock : col::textDim);
        g.drawText (proc.hasB.load() ? "B memorise" : "B vide : Store B d'abord", 120, 116, 170, 20, juce::Justification::centredLeft);
        const auto chain = juce::StringArray::fromTokens (proc.getChainText().toUpperCase(), " ,;-", "");
        int x = 12, idx = 0; const int pos = proc.songPos.load();
        for (auto& tok : chain)
        {
            if (tok.isEmpty()) continue;
            auto r = juce::Rectangle<int> (x, 190 + (idx / 11) * 32, 44, 26);
            g.setColour (idx == pos ? tealCol : col::panelHi); g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (idx == pos ? juce::Colours::black : col::text); g.drawText (tok, r, juce::Justification::centred);
            x += 48; if ((idx + 1) % 11 == 0) x = 12; ++idx;
        }
        g.setColour (col::textDim);
        g.drawFittedText ("Bouton Song + chaine dans la barre du haut (ex. A4 B4). Avec LINK > Pattern, Talea suit aussi les patterns d'Alea.",
                          juce::Rectangle<int> (12, 290, 540, 34), juce::Justification::topLeft, 2);
        g.drawFittedText ("MIDI : C0-G0 = scenes, C1-G1 = patterns A-H, a partir de C2 = clavier (Transpose ou Play). "
                          "Derniere note jouee : " + (proc.lastNote.load() >= 0 ? juce::MidiMessage::getMidiNoteName (proc.lastNote.load(), true, true, 3) : juce::String ("-")),
                          juce::Rectangle<int> (590, 230, 690, 60), juce::Justification::topLeft, 3);
    };

    // ---------------- LAB
    attachCombo (genStyleBox2, labPanel, "genstyle", genStyleNames());
    setupKnob (genDensK, labPanel, "gendens", "Density", false, tealCol);
    setupKnob (genRangeK, labPanel, "genrange", "Range", false, tealCol);
    setupKnob (genAccK, labPanel, "genacc", "Accents", false, tealCol);
    setupKnob (genSlideK, labPanel, "genslide", "Slides", false, tealCol);
    setupKnob (genOctK, labPanel, "genoct", "Oct jumps", false, tealCol);
    setupKnob (genRootK, labPanel, "genroot", "Root", false, tealCol);
    action (genBtn2, labPanel, true, [=] { proc.generatePattern (pat(), (int) proc.paramReal ("genstyle")); refreshTools(); });
    action (notesBtn, labPanel, true, [=] { proc.generateNotesOnly (pat()); });
    action (rhythmBtn, labPanel, true, [=] { proc.generateRhythmOnly (pat()); });
    action (followKickBtn, labPanel, true, [=] { proc.generatePattern (pat(), 6); refreshTools(); });
    action (avoidKickBtn, labPanel, true, [=] { proc.generatePattern (pat(), 7); refreshTools(); });
    genBtn2.setColour (juce::TextButton::buttonColourId, tealCol.darker (0.8f));
    for (int f = 0; f < kFavs; ++f)
    {
        favBtns[f].setButtonText (juce::String (f + 1));
        favBtns[f].onClick = [this, f]
        {
            const bool store = favStoreBtn.getToggleState() || juce::ModifierKeys::currentModifiers.isShiftDown() || ! proc.favs[f].valid;
            if (store) { proc.storeFav (f); favStoreBtn.setToggleState (false, juce::dontSendNotification); }
            else { proc.pushUndo(); proc.recallFav (f); }
            refreshLab();
        };
        labPanel.addAndMakeVisible (favBtns[f]);
    }
    favStoreBtn.setClickingTogglesState (true);
    favStoreBtn.setColour (juce::TextButton::buttonOnColourId, col::lock);
    labPanel.addAndMakeVisible (favStoreBtn);
    action (keepBtn2, labPanel, true, [this] { proc.keepLastNote(); });
    for (auto* c : { &breedA, &breedB }) { for (int f = 0; f < kFavs; ++f) c->addItem ("Fav " + juce::String (f + 1), f + 1); labPanel.addAndMakeVisible (*c); }
    breedA.setSelectedId (1, juce::dontSendNotification); breedB.setSelectedId (2, juce::dontSendNotification);
    action (breedVoiceBtn, labPanel, true, [this] { proc.breedVoice (breedA.getSelectedId() - 1, breedB.getSelectedId() - 1); });
    setupKnob (evoK, labPanel, "evoamt", "Evolve", false, col::rnd);
    attachCombo (evoBarsBox, labPanel, "evobars", { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" });
    attachCombo (evoTargetBox, labPanel, "evotarget", { "Evolve: Pattern", "Evolve: Sound", "Evolve: Both" });
    action (evoCommitBtn, labPanel, true, [this] { proc.commitEvolution(); });
    action (evoResetBtn, labPanel, true, [this] { proc.resetEvolution(); });
    for (auto* c : { &patA, &patB, &patDst }) { for (int p2 = 0; p2 < kPatterns; ++p2) c->addItem (patLetter (p2), p2 + 1); labPanel.addAndMakeVisible (*c); }
    patA.setSelectedId (1, juce::dontSendNotification); patB.setSelectedId (2, juce::dontSendNotification); patDst.setSelectedId (3, juce::dontSendNotification);
    action (breedPatBtn, labPanel, true, [this] { proc.breedPatterns (patA.getSelectedId() - 1, patB.getSelectedId() - 1, patDst.getSelectedId() - 1); });
    action (learnMidiBtn, labPanel, false, [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Apprendre une ligne de basse (MIDI)", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) proc.learnStyleFromMidi (f); labPanel.repaint(); });
    });
    action (styleGenBtn, labPanel, true, [=] { proc.generateFromStyle (pat()); refreshTools(); });
    labPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> a[] { { 0, 0, 760, 160 }, { 770, 0, 526, 160 }, { 0, 170, 420, 160 }, { 430, 170, 420, 160 }, { 860, 170, 436, 160 } };
        const char* t[] { "GENERATEUR DE LIGNES", "FAVORIS & CROISEMENT DE SONS", "EVOLUTION AUTOMATIQUE", "CROISEMENT DE PATTERNS", "STYLE APPRIS (MIDI)" };
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (col::panel); g.fillRoundedRectangle (a[i].toFloat(), 6.0f);
            g.setColour (col::rnd); g.setFont (font (12.0f, true));
            g.drawText (t[i], a[i].getX() + 10, a[i].getY() + 4, 500, 20, juce::Justification::centredLeft);
        }
        g.setColour (col::textDim); g.setFont (font (10.5f));
        g.drawFittedText ("Acid, Rolling, Driving 8ths, Dub, Arp, Euclid, Random - et avec Alea : Follow kick (sur le kick) / Avoid kick (entre les kicks). "
                          "Tout reste dans la tonique et la gamme choisies.", juce::Rectangle<int> (12, 132, 740, 26), juce::Justification::topLeft, 2);
        g.drawFittedText ("Case vide = memoriser, case pleine = rappel + ecoute, Maj-clic = ecraser.", juce::Rectangle<int> (782, 130, 500, 26), juce::Justification::topLeft, 2);
        g.setColour (col::text);
        g.drawText ("Changements : " + juce::String (proc.evolutionAmount()), 12, 300, 200, 18, juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.drawFittedText ("Des notes changent de degre (^ v), des pas basculent, le son derive. Commit ecrit, Reset annule.",
                          juce::Rectangle<int> (130, 268, 280, 56), juce::Justification::topLeft, 3);
        g.drawFittedText ("Rythme d'un parent, notes melangees, coupure au hasard.", juce::Rectangle<int> (442, 268, 400, 40), juce::Justification::topLeft, 2);
        g.setColour (proc.style.valid ? col::text : col::textDim);
        g.drawText (proc.style.valid ? "Style : " + proc.style.name + " (" + juce::String (proc.style.bars) + " mesures)" : juce::String ("Aucun style appris"),
                    872, 240, 410, 18, juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.drawFittedText ("Charge (ou glisse sur le plugin) un .mid de basse : rythme, accents, slides et intervalles sont appris.",
                          juce::Rectangle<int> (872, 266, 410, 50), juce::Justification::topLeft, 3);
    };

    // ---------------- AIDE
    helpPanel.painter = [this] (juce::Graphics& g)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (helpPanel.getLocalBounds().toFloat(), 6.0f);
        g.setColour (col::text); g.setFont (font (12.0f));
        g.drawFittedText (
            "TALEA - basse / synthe mono facon TB-303, satellite d'Alea (AleaLink).\n"
            "PIANO ROLL : clic = poser/effacer une note | glisser = dessiner | Maj = accent | Cmd-clic = p-locks | clic droit = menu | Alt-clic = longueur\n"
            "COULOIRS : Accent, Slide (glisse vers la note suivante), Tie (prolonge), Octave, Gate, Velocity, Probability, Ratchet, Condition, Nudge\n"
            "POTARDS : glisser = valeur | clic droit-glisser / Cmd-glisser / RND EDIT = alea | clic droit = MIDI learn | double-clic = defaut | cadenas = exclu du de\n"
            "SYNTH : 8 oscillateurs (dont SuperSaw, Fold, Pluck), osc 2 + FM, sub, filtre 303 a diodes / ladder 24 dB / SVF, accent 303, glide, accords, 5 saturations\n"
            "ALEA : Chaos, Mode, Dist, Seed, Rnd Link, Note Chaos (notes au vol dans la gamme), Seq / Ratch Chaos, Density, Humanize, modulateurs, evolution\n"
            "LAB : generateur de lignes (9 styles), favoris + Breed, evolution, croisement de patterns, style appris d'un .mid\n"
            "LINK : tempo, lecture, tonalite, pattern, fill, chaos, swing, scene... synchronises avec Alea ; sidechain et modulateur Drums depuis la batterie d'Alea\n"
            "MIDI : C0-G0 scenes, C1-G1 patterns, C2+ clavier (Transpose / Play) ; sortie MIDI des notes ; Drag MIDI = clip de 4 mesures",
            helpPanel.getLocalBounds().reduced (16, 12), juce::Justification::topLeft, 30);
    };

    showTab (0);
    startTimerHz (30);
}

TaleaView::~TaleaView()
{
    stopTimer();
    sliderAtts.clear(); comboAtts.clear(); buttonAtts.clear();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void TaleaView::setupKnob (RndKnob& k, juce::Component& parent, const juce::String& paramId, const juce::String& label, bool bipolar, juce::Colour c)
{
    k.setProcessor (&proc);
    k.bind (proc.apvts.getParameter (paramId), nullptr, nullptr, nullptr, label);
    k.setBipolar (bipolar);
    k.setAccentColour (c);
    parent.addAndMakeVisible (k);
}

void TaleaView::action (juce::TextButton& b, juce::Component& parent, bool undoable, std::function<void()> fn)
{
    b.onClick = [this, fn, undoable] { if (undoable) proc.pushUndo(); fn(); roll.repaint(); lanes.repaint(); };
    parent.addAndMakeVisible (b);
}

void TaleaView::attachCombo (juce::ComboBox& c, juce::Component& parent, const juce::String& paramId, const juce::StringArray& items)
{
    c.addItemList (items, 1);
    parent.addAndMakeVisible (c);
    comboAtts.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (*proc.apvts.getParameter (paramId), c));
}

void TaleaView::attachToggle (juce::Button& b, juce::Component& parent, const juce::String& paramId)
{
    b.setClickingTogglesState (true);
    parent.addAndMakeVisible (b);
    buttonAtts.push_back (std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter (paramId), b));
}

BPattern& TaleaView::editPat() { return proc.bank.pats[proc.editPattern()]; }

void TaleaView::showTab (int i)
{
    currentTab = i;
    juce::Component* panels[kNumTabs] { &synthPanel, &modPanel, &fxPanel, &perfPanel, &labPanel, linkPanel.get(), &helpPanel };
    for (int k = 0; k < kNumTabs; ++k)
    {
        panels[k]->setVisible (k == i);
        tabBtns[k].setColour (juce::TextButton::buttonColourId, k == i ? col::panelHi.brighter (0.25f) : col::panel);
        tabBtns[k].setColour (juce::TextButton::textColourOffId, k == i ? tealCol : col::textDim);
    }
    if (i == 4) refreshLab();
}

void TaleaView::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto fac = TaleaAudioProcessor::factoryPresetNames();
    presetBox.addSectionHeading ("Usine");
    for (int i = 0; i < fac.size(); ++i) presetBox.addItem (fac[i], i + 1);
    userPresets = TaleaAudioProcessor::userPresetDir().findChildFiles (juce::File::findFiles, false, "*.talea");
    userPresets.sort();
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("Utilisateur");
        for (int i = 0; i < userPresets.size(); ++i) presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 100 + i);
    }
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
}

void TaleaView::stepPreset (int delta)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i) ids.add (presetBox.getItemId (i));
    if (ids.isEmpty()) return;
    int cur = ids.indexOf (presetBox.getSelectedId());
    cur = cur < 0 ? 0 : (cur + delta + ids.size()) % ids.size();
    presetBox.setSelectedId (ids[cur], juce::sendNotificationSync);
}

void TaleaView::refreshTools()
{
    auto& P = editPat();
    lenSlider.setValue (P.length.load(), juce::dontSendNotification);
    rateBox.setSelectedId (P.rate.load() + 1, juce::dontSendNotification);
    dirBox.setSelectedId (P.dir.load() + 1, juce::dontSendNotification);
    grooveBox.setSelectedId (P.groove.load() + 1, juce::dontSendNotification);
    roll.repaint(); lanes.repaint();
}

void TaleaView::refreshLab()
{
    for (int f = 0; f < kFavs; ++f)
    {
        const bool v = proc.favs[f].valid;
        favBtns[f].setColour (juce::TextButton::buttonColourId, v ? tealCol.darker (0.6f) : col::panelHi);
        favBtns[f].setColour (juce::TextButton::textColourOffId, v ? juce::Colours::white : col::textDim);
    }
    labPanel.repaint();
}

void TaleaView::refreshAfterStateChange()
{
    setLockStep (-1);
    chainEdit.setText (proc.getChainText(), false);
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
    lockTypesBtn.setToggleState (proc.locks[kNumTP].load(), juce::dontSendNotification);
    refreshTools(); refreshLab();
    for (auto& k : tpKnobs) k.repaint();
    repaint();
}

void TaleaView::setLockStep (int i)
{
    lockI = i; lockPat = proc.editPattern();
    roll.lockStep = i;
    for (int t = 0; t < kNumTP; ++t) tpKnobs[t].setPlockTarget (i >= 0 ? &proc.bank.pats[lockPat].steps[i].plock[t] : nullptr);
    const bool on = i >= 0;
    lockCondBox.setVisible (on); clearLocksBtn.setVisible (on); exitLockBtn.setVisible (on);
    if (on) { lockCondBox.setSelectedId (proc.bank.pats[lockPat].steps[i].cond + 1, juce::dontSendNotification); if (currentTab != 0) showTab (0); }
    roll.repaint(); synthPanel.repaint(); repaint (lockArea);
}

bool TaleaView::dropFiles (const juce::StringArray& files)
{
    if (files.isEmpty()) return false;
    juce::File f (files[0]);
    if (f.hasFileExtension ("talea")) { if (proc.loadPresetFile (f)) refreshAfterStateChange(); return true; }
    if (f.hasFileExtension ("mid;midi")) { const bool ok = proc.learnStyleFromMidi (f); showTab (4); return ok; }
    return false;
}

void TaleaView::timerCallback()
{
    proc.flash = proc.flash.load() * 0.82f;
    for (auto& k : tpKnobs) k.refreshLive();
    for (auto* k : { &chaosKnob, &morphK, &densityK }) k->refreshLive();
    const int ep = proc.editPattern();
    if (lockI >= 0 && ep != lockPat) setLockStep (-1);
    if (ep != lastEp) { lastEp = ep; refreshTools(); }
    else if (! lenSlider.isMouseButtonDown() && (int) lenSlider.getValue() != editPat().length.load())
        lenSlider.setValue (editPat().length.load(), juce::dontSendNotification);
    for (int i = 0; i < kPatterns; ++i)
    {
        patBtns[i].setColour (juce::TextButton::buttonColourId, i == ep ? tealCol : col::panelHi);
        patBtns[i].setColour (juce::TextButton::textColourOffId, i == ep ? juce::Colours::black : col::text);
    }
    if (++previewTick % 3 == 0 && roll.showPreview) roll.preview = proc.previewNextBar();
    if (currentTab == 3)
        for (int n = 0; n < kScenes; ++n)
        {
            const bool v = proc.sceneValid[n].load(), cur = proc.lastRecalledScene.load() == n;
            sceneBtns[n].setColour (juce::TextButton::buttonColourId, cur ? col::plock : (v ? col::panelHi.brighter (0.2f) : col::panel));
            sceneBtns[n].setColour (juce::TextButton::textColourOffId, cur ? juce::Colours::black : (v ? col::text : col::textDim));
        }
    const int peers = proc.link != nullptr ? proc.link->countLinkedPeers() : 0;
    linkBtn.setButtonText (peers > 0 ? "LINK " + juce::String (peers) : juce::String ("LINK"));
    linkBtn.setColour (juce::TextButton::buttonColourId, peers > 0 ? col::rnd.darker (0.6f) : col::panelHi);
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    roll.repaint();
    if (currentTab == 0) scope.refresh();
    if (currentTab == 1) for (auto& d : modDisp) d->push();
    if (currentTab == 2 || currentTab == 3) (currentTab == 2 ? fxPanel : perfPanel).repaint();
    if (currentTab == 4 && previewTick % 10 == 0) labPanel.repaint();
    repaint (0, 0, W, 84);
}

void TaleaView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (tealCol);
    g.setFont (font (32.0f, true));
    g.drawText ("TALEA", 16, 6, 130, 38, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (10.0f));
    g.drawText ("random bass synth - AleaLink", 16, 44, 150, 14, juce::Justification::centredLeft);
    {
        const int cp = proc.currentPattern.load();
        auto r = patBtns[cp].getBounds();
        g.setColour (juce::Colours::white);
        g.fillRect (r.getX() + 4, r.getBottom() + 2, r.getWidth() - 8, 3);
    }
    {
        const float pk = juce::jlimit (0.0f, 1.0f, proc.outPeak.load());
        auto m = juce::Rectangle<float> ((float) masterKnob.getRight() + 2.0f, 10.0f, 5.0f, 58.0f);
        g.setColour (col::line); g.fillRect (m);
        g.setColour (pk > 0.95f ? col::lock : tealCol);
        g.fillRect (m.withTop (m.getBottom() - m.getHeight() * pk));
        g.setColour (tealCol.withAlpha (0.2f + 0.8f * proc.flash.load()));
        g.fillEllipse (150.0f, 12.0f, 8.0f, 8.0f);
    }
    g.setColour (col::panel);
    g.fillRoundedRectangle (seqArea.toFloat(), 6.0f);
    g.setColour (tealCol);
    g.setFont (font (11.0f, true));
    g.drawText ("SEQUENCE", seqArea.getX() + 10, seqArea.getY() + 4, 200, 16, juce::Justification::centredLeft);

    g.setColour (col::panel);
    g.fillRoundedRectangle (lockArea.toFloat(), 6.0f);
    g.setFont (font (11.0f, true));
    if (lockI >= 0)
    {
        g.setColour (col::plock);
        g.drawText ("P-LOCK  pas " + juce::String (lockI + 1), lockArea.getX() + 10, lockArea.getY() + 4, 260, 16, juce::Justification::centredLeft);
        g.setColour (col::textDim); g.setFont (font (10.0f));
        g.drawFittedText ("Les potards SYNTH n'agissent que sur ce pas.", lockArea.getX() + 10, lockArea.getY() + 64, 380, 20, juce::Justification::topLeft, 2);
    }
    else
    {
        g.setColour (col::textDim);
        g.drawText ("PAS", lockArea.getX() + 10, lockArea.getY() + 4, 200, 16, juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.drawFittedText ("Cmd-clic sur un pas : p-locks (valeurs propres au pas).\nClic droit : accent, slide, tie, octave, gate, condition, proba, ratchet.\n"
                          "Apercu : point turquoise = note fantome a venir, trait rouge = pas qui sautera.",
                          lockArea.withTrimmedTop (24).reduced (10, 0), juce::Justification::topLeft, 6);
    }
    if (RndKnob::editRandomMode && currentTab == 0)
    {
        g.setColour (col::rnd.withAlpha (0.6f));
        g.drawRoundedRectangle (synthPanel.getBounds().toFloat().expanded (1.0f), 6.0f, 1.5f);
    }
}

void TaleaView::resized()
{
    presetPrev.setBounds (170, 8, 24, 26); presetBox.setBounds (196, 8, 190, 26); presetNext.setBounds (388, 8, 24, 26); presetSave.setBounds (416, 8, 50, 26);
    undoBtn.setBounds (478, 8, 56, 26); redoBtn.setBounds (538, 8, 56, 26);
    midiOutBtn.setBounds (606, 8, 74, 26); dragMidi.setBounds (684, 8, 100, 26);
    rndEditBtn.setBounds (796, 8, 76, 26); diceBtnTop.setBounds (876, 8, 56, 26); seedSlider.setBounds (936, 8, 90, 26);
    rootBox.setBounds (1034, 8, 58, 26); scaleBox.setBounds (1096, 8, 92, 26);
    playBtn.setBounds (170, 44, 56, 28); bpmSlider.setBounds (230, 44, 96, 28);
    for (int i = 0; i < kPatterns; ++i) patBtns[i].setBounds (334 + i * 29, 44, 27, 28);
    copyPatBox.setBounds (570, 44, 84, 28); songBtn.setBounds (660, 44, 52, 28); chainEdit.setBounds (716, 44, 120, 28);
    fillBtn.setBounds (842, 44, 56, 28); rndModeBox.setBounds (904, 44, 106, 28); distBox.setBounds (1014, 44, 112, 28); linkBtn.setBounds (1130, 44, 58, 28);
    chaosKnob.setBounds (1194, 4, 58, 74); masterKnob.setBounds (1252, 4, 58, 74);

    const int y0 = 86;
    roll.setBounds (12, y0, 870, 225);
    seqArea = { 894, y0, 414, 225 };
    const int kx = seqArea.getX() + 8, kw = 132;
    swingK.setBounds (kx, y0 + 22, kw / 2 + 20, 98); humanK.setBounds (kx + 86, y0 + 22, 86, 98); noteChaosK.setBounds (kx + 172, y0 + 22, 86, 98);
    seqChaosK.setBounds (kx, y0 + 122, 86, 98); ratChaosK.setBounds (kx + 86, y0 + 122, 86, 98); densityK.setBounds (kx + 172, y0 + 122, 86, 98);
    previewBtn.setBounds (seqArea.getRight() - 90, y0 + 4, 80, 22);

    int x = 12; const int ty = y0 + 232, th = 26;
    auto place = [&] (juce::Component& c, int w) { c.setBounds (x, ty, w, th); x += w + 3; };
    place (lenSlider, 86); place (rateBox, 70); place (dirBox, 84); place (grooveBox, 108); x += 6;
    place (genStyleBox, 96); place (genBtn, 66); place (varyBtn, 44); place (mutNBtn, 64); place (mutRBtn, 70); x += 6;
    place (trDnBtn, 30); place (trUpBtn, 30); place (ocDnBtn, 38); place (ocUpBtn, 38); place (revBtn, 36); place (invBtn, 34);
    place (shlBtn, 22); place (shrBtn, 22); x += 6;
    place (hitsSlider, 84); place (euclidBtn, 52); x += 6;
    place (clearBtn, 44); place (copyBtn, 42); place (pasteBtn, 44);

    lanes.setBounds (12, ty + th + 6, 870, 160);
    lockArea = { 894, ty + th + 6, 414, 160 };
    lockCondBox.setBounds (lockArea.getX() + 10, lockArea.getY() + 26, 130, 24);
    clearLocksBtn.setBounds (lockArea.getX() + 146, lockArea.getY() + 26, 90, 24);
    exitLockBtn.setBounds (lockArea.getX() + 240, lockArea.getY() + 26, 50, 24);

    const int tabY = lanes.getBottom() + 6;
    for (int i = 0; i < kNumTabs; ++i) tabBtns[i].setBounds (12 + i * 96, tabY, 92, 24);
    const auto pb = juce::Rectangle<int> (12, tabY + 28, 1296, H - (tabY + 28) - 6);
    for (juce::Component* pn : { (juce::Component*) &synthPanel, (juce::Component*) &modPanel, (juce::Component*) &fxPanel,
                                 (juce::Component*) &perfPanel, (juce::Component*) &labPanel, (juce::Component*) linkPanel.get(), (juce::Component*) &helpPanel })
        pn->setBounds (pb);
    chordFollowBox.setBounds (0, 0, 200, 26);

    // SYNTH
    {
        int sx = 110; const int sy = 6;
        auto pl = [&] (juce::Component& c, int w) { c.setBounds (sx, sy, w, 22); sx += w + 4; };
        pl (osc1Box, 92); pl (osc2Box, 100); pl (subBox, 116); pl (filtBox, 100); pl (driveBox, 70); pl (chordBox, 98); pl (glideBox, 110);
        pl (baseOctSlider, 104);
        const auto& order = tpDisplayOrder();
        const int kw2 = 70, kh = 86;
        for (int i = 0; i < kNumTP; ++i)
            tpKnobs[order[(size_t) i]].setBounds (8 + (i % 13) * kw2, 52 + (i / 13) * (kh + 14), kw2, kh);
        velSensK.setBounds (8 + 11 * kw2, 52 + kh + 14, kw2, kh);
        linkRndK.setBounds (8 + 12 * kw2, 52 + kh + 14, kw2, kh);
        scope.setBounds (8 + 13 * kw2 + 10, 34, 1296 - (8 + 13 * kw2 + 10) - 10, 168);
        const int bw = (scope.getWidth() - 9) / 4;
        int bx = scope.getX(), by = scope.getBottom() + 6;
        for (auto* b : { &auditionBtn, &keepBtn, &diceBtn, &diceTypesBtn }) { b->setBounds (bx, by, bw, 24); bx += bw + 3; }
        bx = scope.getX(); by += 28;
        for (auto* b : { &mutBtn, &rndAmtBtn, &zeroRndBtn, &lockTypesBtn }) { b->setBounds (bx, by, bw, 24); bx += bw + 3; }
    }
    // MOD
    for (int m = 0; m < alea::kNumMods; ++m)
    {
        const int bx = m * 216;
        modRateBox[m].setBounds (bx + 8, 28, 76, 22);
        modPK[m].setBounds (bx + 8, 54, 76, 72);
        modDisp[m]->setBounds (bx + 90, 28, 112, 96);
    }
    turLenSlider.setBounds (2 * 216 + 104, 4, 100, 20);
    followTrkBox.setBounds (5 * 216 + 4, 28, 88, 22);
    modDepthK.setBounds (1296 - 84, 170, 76, 74);
    for (int s = 0; s < kMxSlots; ++s)
    {
        const int bx = (s / 4) * 600 + 20, by = 188 + (s % 4) * 36;
        mxSrc[s].setBounds (bx, by, 96, 26);
        mxDst[s].setBounds (bx + 100, by, 120, 26);
        mxAmt[s].setBounds (bx + 226, by, 340, 26);
    }
    // FX
    {
        const int ky = 28;
        int fx = 10; for (auto* k : { &chRateK, &chDepthK, &chMixK }) { k->setBounds (fx, ky, 86, 110); fx += 88; }
        dlyDivBox.setBounds (380, 4, 80, 20); pingBtn.setBounds (466, 4, 84, 20);
        fx = 296; for (auto* k : { &dlyFbK, &dlyToneK, &dlyRetK, &dlyRndK }) { k->setBounds (fx, ky, 100, 110); fx += 102; }
        fx = 726; for (auto* k : { &revSizeK, &revDampK, &revWidthK, &revRetK, &revPreK }) { k->setBounds (fx, ky, 76, 110); fx += 78; }
        glueK.setBounds (1136, ky, 76, 110); mDriveK.setBounds (1214, ky, 76, 110);
        duckSrcBox.setBounds (10, 196, 170, 24);
        duckAmtK.setBounds (190, 186, 120, 110); duckRelK.setBounds (320, 186, 120, 110);
    }
    // PERFORM
    morphK.setBounds (10, 26, 100, 116);
    storeBBtn.setBounds (120, 34, 80, 26); recallBBtn.setBounds (206, 34, 84, 26); swapBBtn.setBounds (120, 68, 170, 26);
    { int px = 316; for (auto* k : { &densityK2, &chaosK2, &seqChaosK2, &modDepthK2, &duckK2, &evoK2 }) { k->setBounds (px, 26, 86, 112); px += 88; }
      fillLatchBtn.setBounds (px + 4, 60, 90, 36); }
    for (int n = 0; n < kScenes; ++n) sceneBtns[n].setBounds (970 + (n % 4) * 40, 30 + (n / 4) * 34, 36, 28);
    sceneStoreBtn.setBounds (970, 100, 76, 24); sceneXfOnBtn.setBounds (1052, 100, 76, 24);
    sceneABox.setBounds (1134, 30, 100, 24); sceneBBox.setBounds (1134, 62, 100, 24);
    sceneXfK.setBounds (1236, 24, 58, 116);
    keysBox.setBounds (590, 192, 160, 26);
    // LAB
    genStyleBox2.setBounds (12, 30, 130, 26);
    genBtn2.setBounds (12, 62, 130, 30); notesBtn.setBounds (12, 96, 62, 26); rhythmBtn.setBounds (78, 96, 64, 26);
    { int gx = 150; for (auto* k : { &genDensK, &genRangeK, &genAccK, &genSlideK, &genOctK, &genRootK }) { k->setBounds (gx, 26, 76, 100); gx += 78; } }
    followKickBtn.setBounds (626, 34, 124, 28); avoidKickBtn.setBounds (626, 68, 124, 28);
    for (int f = 0; f < kFavs; ++f) favBtns[f].setBounds (782 + f * 44, 32, 40, 32);
    favStoreBtn.setBounds (782, 72, 60, 28); keepBtn2.setBounds (848, 72, 120, 28);
    breedA.setBounds (980, 72, 86, 28); breedB.setBounds (1070, 72, 86, 28); breedVoiceBtn.setBounds (1160, 72, 80, 28);
    evoK.setBounds (12, 196, 100, 96);
    evoBarsBox.setBounds (130, 200, 100, 26); evoTargetBox.setBounds (236, 200, 136, 26);
    evoCommitBtn.setBounds (130, 234, 100, 26); evoResetBtn.setBounds (236, 234, 100, 26);
    patA.setBounds (442, 200, 90, 28); patB.setBounds (538, 200, 90, 28); patDst.setBounds (634, 200, 90, 28); breedPatBtn.setBounds (730, 200, 100, 28);
    learnMidiBtn.setBounds (872, 200, 160, 30); styleGenBtn.setBounds (1038, 200, 140, 30);
}

// ===========================================================================
TaleaAudioProcessorEditor::TaleaAudioProcessorEditor (TaleaAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p)
{
    addAndMakeVisible (view);
    const float s = juce::jlimit (0.55f, 1.6f, proc.uiScale.load());
    setSize ((int) (TaleaView::W * s), (int) (TaleaView::H * s));
    setResizable (true, true);
    setResizeLimits ((int) (TaleaView::W * 0.55), (int) (TaleaView::H * 0.55), (int) (TaleaView::W * 1.6), (int) (TaleaView::H * 1.6));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) TaleaView::W / (double) TaleaView::H);
    setSize ((int) (TaleaView::W * s), (int) (TaleaView::H * s));
}

void TaleaAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) TaleaView::W;
    view.setBounds (0, 0, TaleaView::W, TaleaView::H);
    view.setTransform (juce::AffineTransform::scale (s));
    proc.uiScale = s;
}
