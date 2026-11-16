#include "ColorEditor.h"

using namespace color;
using namespace alea::gui;
using color::gui::colorCol;
namespace th = color::theory;

static juce::String patLetter (int p) { return juce::String::charToString ((juce::juce_wchar) ('A' + p)); }

ColorView::ColorView (ColorAudioProcessor& p)
    : proc (p),
      dragMidi ([&p] { return p.exportMidi (p.editPattern(), 8); }),
      lane (p), voicingView (p), grid (p),
      scope (p.scopeBuf, ColorAudioProcessor::kScopeLen, p.scopeVersion, [] { return colorCol; })
{
    lnf.setColourScheme ({ col::bg, col::panelHi, col::panel, col::line, col::text, col::panelHi, juce::Colours::white, colorCol, col::text });
    lnf.setColour (juce::TextButton::buttonOnColourId, colorCol);
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::outlineColourId, col::line);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, col::line);
    lnf.setColour (juce::Slider::trackColourId, colorCol.withAlpha (0.6f));
    lnf.setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    lnf.setColour (juce::TextEditor::backgroundColourId, col::panelHi);
    lnf.setColour (juce::TextEditor::outlineColourId, col::line);
    lnf.setColour (juce::ToggleButton::tickColourId, colorCol);
    setLookAndFeel (&lnf);
    auto& ap = proc.apvts;
    auto pat = [this] { return proc.editPattern(); };

    // ---------------- barre du haut
    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id <= ColorAudioProcessor::factoryPresetNames().size()) proc.loadFactoryPreset (id - 1);
        else if (id >= 100 && id - 100 < userPresets.size()) proc.loadPresetFile (userPresets[id - 100]);
        else return;
        refreshAfterStateChange();
    };
    action (presetPrev, *this, false, [this] { stepPreset (-1); });
    action (presetNext, *this, false, [this] { stepPreset (1); });
    action (presetSave, *this, false, [this]
    {
        auto dir = ColorAudioProcessor::userPresetDir(); dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Enregistrer le preset", dir.getChildFile (proc.currentPresetName + ".color"), "*.color");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f == juce::File()) return;
                                                                      proc.savePresetFile (f.withFileExtension ("color")); refreshPresetList(); });
    });
    refreshPresetList();
    action (undoBtn, *this, false, [this] { if (proc.undo()) refreshAfterStateChange(); });
    action (redoBtn, *this, false, [this] { if (proc.redo()) refreshAfterStateChange(); });
    attachToggle (midiOutBtn, *this, "midiout");
    midiOutBtn.setTooltip ("Envoie les notes des accords en MIDI");
    addAndMakeVisible (dragMidi);
    dragMidi.setTooltip ("Glisser vers une piste MIDI : 8 mesures de ce que joue Color (accords, rythme, arpege)");
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
    rootBox.setTooltip ("Tonique (partagee avec Alea et Talea via AleaLink)");
    attachCombo (modeBox, *this, "mode", th::scaleNameList());
    modeBox.setTooltip ("Mode / gamme (38 gammes : modes diatoniques, mineur melodique et harmonique, symetriques, pentatoniques, bebop...). "
                        "Partage avec Alea et Talea sous forme simplifiee (majeur, mineur, penta...)");
    attachToggle (playBtn, *this, "play");
    for (int i = 0; i < kPatterns; ++i)
    {
        auto& b = patBtns[i];
        b.setButtonText (patLetter (i));
        b.onClick = [this, i] { proc.setParamReal ("pattern", (float) i); };
        b.setTooltip ("Pattern " + patLetter (i) + " (progression + rythme ; MIDI C1..G1). Suit Alea si LINK > Pattern est coche");
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
    linkBtn.onClick = [this] { showTab (7); };
    linkBtn.setTooltip ("AleaLink : instances detectees et synchronisation");
    addAndMakeVisible (linkBtn);
    setupKnob (chaosKnob, *this, "chaos", "CHAOS", false, col::rnd);
    setupKnob (masterKnob, *this, "master", "Master", false, colorCol);

    // ---------------- harmonie
    addAndMakeVisible (lane);
    lane.setTooltip ("Clic = ecouter | clic droit = substitutions, reharmonisation, qualite, basse, duree, suite probable | glisser le bord droit = duree");
    lane.onChanged = [this] { harmonyChanged(); };
    lane.onSelect = [this] (int i) { voicingView.shownChord = i; if (theoryView != nullptr) { theoryView->selected = i; theoryView->refresh(); } };
    addAndMakeVisible (voicingView);
    attachCombo (styleBox, *this, "style", th::styleNames());
    styleBox.setTooltip ("Style du generateur de progressions (reglages fins : onglet HARMONIE)");
    action (genProgBtn, *this, true, [=] { proc.generateProgression (pat()); harmonyChanged(); });
    genProgBtn.setColour (juce::TextButton::buttonColourId, colorCol.darker (0.9f));
    reharmBox.addItemList (th::reharmNames(), 1);
    reharmBox.setSelectedId (1, juce::dontSendNotification);
    reharmBox.setTooltip ("Techniques de reharmonisation (LaVerne / Dobbins) appliquees a un accord au hasard");
    addAndMakeVisible (reharmBox);
    action (reharmBtn, *this, true, [=] { proc.reharmonize (pat(), reharmBox.getSelectedId() - 1); harmonyChanged(); });
    action (varyProgBtn, *this, true, [=] { proc.varyProgression (pat(), 0.5f); harmonyChanged(); });
    action (simplifyBtn, *this, true, [=] { proc.simplifyProgression (pat()); harmonyChanged(); });
    action (enrichBtn, *this, true, [=] { proc.enrichProgression (pat()); harmonyChanged(); });
    action (trDnBtn, *this, true, [=] { proc.transposeProgression (pat(), -1); harmonyChanged(); });
    action (trUpBtn, *this, true, [=] { proc.transposeProgression (pat(), 1); harmonyChanged(); });
    action (rotLBtn, *this, true, [=] { proc.rotateProgression (pat(), 1); harmonyChanged(); });
    action (rotRBtn, *this, true, [=] { proc.rotateProgression (pat(), -1); harmonyChanged(); });
    action (revProgBtn, *this, true, [=] { proc.reverseProgression (pat()); harmonyChanged(); });
    varyProgBtn.setTooltip ("Change les couleurs (extensions, renversements) en gardant les fondamentales");
    simplifyBtn.setTooltip ("Ramene chaque accord a sa triade / 7e de base");
    enrichBtn.setTooltip ("Ajoute des extensions (7e > 9e > 11e / 13e)");
    trDnBtn.setTooltip ("Transposition diatonique d'un degre (les qualites suivent le mode)");
    cadenceBox.setTextWhenNothingSelected ("Cadence...");
    cadenceBox.addItemList ({ "Parfaite V7-I", "Plagale IV-I", "Rompue V-vi", "Demi-cadence ii-V", "Backdoor iv-bVII7-I", "Modale bVII-I" }, 1);
    cadenceBox.onChange = [this, pat] { const int id = cadenceBox.getSelectedId(); if (id > 0) { proc.pushUndo(); proc.addCadence (pat(), id - 1); harmonyChanged(); }
                                        cadenceBox.setSelectedId (0, juce::dontSendNotification); };
    cadenceBox.setTooltip ("Remplace la fin de la progression par une cadence");
    addAndMakeVisible (cadenceBox);
    progEdit.setFont (font (14.0f));
    progEdit.setTooltip ("Tape une grille : Am9 Fmaj7 C/E G6  ou  Dm7:2 G7:2 Cmaj7 (:N = duree en temps). Entree pour valider.");
    auto commitProg = [this]
    {
        proc.pushUndo();
        const bool ok = proc.setProgressionText (proc.editPattern(), progEdit.getText());
        progEdit.applyColourToAllText (ok ? col::text : col::lock);
        if (ok) harmonyChanged();
    };
    progEdit.onReturnKey = commitProg;
    addAndMakeVisible (progEdit);
    keyShiftSlider.setSliderStyle (juce::Slider::IncDecButtons);
    keyShiftSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 24);
    keyShiftSlider.setRange (-11, 11, 1);
    keyShiftSlider.textFromValueFunction = [] (double v) { return "Key " + juce::String (v > 0 ? "+" : "") + juce::String ((int) v); };
    keyShiftSlider.onValueChange = [this] { editPat().keyShift = (int8_t) keyShiftSlider.getValue(); harmonyChanged(); };
    keyShiftSlider.setTooltip ("Tonalite propre a ce pattern (modulation) : demi-tons au-dessus de la tonique. La chaine Song accepte aussi A4+5");
    addAndMakeVisible (keyShiftSlider);
    action (copyHarmBtn, *this, false, [=] { proc.copyClip (pat()); });
    action (pasteHarmBtn, *this, true, [=] { proc.pasteHarmony (pat()); harmonyChanged(); });
    copyHarmBtn.setTooltip ("Copie le pattern (progression + rythme)"); pasteHarmBtn.setTooltip ("Colle seulement la progression");
    attachCombo (playModeBox, *this, "playmode", playModeNames());
    playModeBox.setTooltip ("Pad : accords tenus (le rythme est ignore) | Comp : accords sur le rythme | Strum : accords grattes | Arp : arpege sur le rythme");
    attachCombo (voicingBox, *this, "voicing", th::voicingNames());
    voicingBox.setTooltip ("Voicing (Dobbins) : position serree, drop 2, drop 2+4, ecarte, shell (3-7), rootless A/B, quartes, upper structure, cluster, quintes, auto");

    // ---------------- rythme
    addAndMakeVisible (grid);
    grid.onLockStep = [this] (int i) { setLockStep (i); };
    grid.setTooltip ("Clic = poser / effacer (glisser = duree) | Alt-glisser = duree | Maj = accent | Cmd-clic = p-locks | clic droit = menu du pas");
    setupKnob (swingK, *this, "swing", "Swing");
    setupKnob (humanK, *this, "humanize", "Humanize", false, col::rnd);
    setupKnob (seqChaosK, *this, "seqchaos", "Seq Chaos", false, col::rnd);
    setupKnob (ratChaosK, *this, "ratchaos", "Ratch Chaos", false, col::rnd);
    setupKnob (densityK, *this, "density", "Density", true, col::rnd);
    setupKnob (voicChaosK, *this, "notechaos", "Voic. Chaos", false, col::rnd);
    voicChaosK.setTooltip ("Deplace / omet des notes du voicing au vol (sans modifier l'accord)");
    for (auto* s : { &lenSlider, &hitsSlider })
    {
        s->setSliderStyle (juce::Slider::IncDecButtons);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 24);
        s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        addAndMakeVisible (*s);
    }
    lenSlider.setRange (1, kSteps, 1); lenSlider.setTextValueSuffix (" st");
    hitsSlider.setRange (1, kSteps, 1); hitsSlider.setTextValueSuffix (" hits"); hitsSlider.setValue (5, juce::dontSendNotification);
    lenSlider.onValueChange = [this] { editPat().length = (int8_t) lenSlider.getValue(); grid.repaint(); };
    rateBox.addItemList (rateNames(), 1);
    rateBox.onChange = [this] { editPat().rate = (int8_t) (rateBox.getSelectedId() - 1); };
    dirBox.addItemList (dirNames(), 1);
    dirBox.onChange = [this] { editPat().dir = (int8_t) (dirBox.getSelectedId() - 1); };
    grooveBox.addItemList (grooveNames(), 1);
    grooveBox.onChange = [this] { editPat().groove = (int8_t) (grooveBox.getSelectedId() - 1); };
    for (auto* c : { &rateBox, &dirBox, &grooveBox }) addAndMakeVisible (*c);
    attachCombo (rhythmBox, *this, "rhythm", rhythmNames());
    action (rhythmApplyBtn, *this, true, [=] { proc.applyRhythm (pat(), (int) proc.paramReal ("rhythm")); refreshTools(); });
    action (rhythmGenBtn, *this, true, [=] { proc.generateRhythm (pat()); refreshTools(); });
    rhythmGenBtn.setTooltip ("Rythme idiomatique du style choisi (comping jazz, house, gospel...)");
    action (mutRBtn, *this, true, [=] { proc.mutateRhythm (pat(), 0.15f); });
    action (shlBtn, *this, true, [=] { proc.shiftPattern (pat(), -1); });
    action (shrBtn, *this, true, [=] { proc.shiftPattern (pat(), 1); });
    action (euclidBtn, *this, true, [=] { proc.euclidPattern (pat(), (int) hitsSlider.getValue(), 0); });
    action (clearBtn, *this, true, [=] { proc.clearRhythm (pat()); });
    action (copyBtn, *this, false, [=] { proc.copyClip (pat()); });
    action (pasteBtn, *this, true, [=] { proc.pasteClip (pat()); refreshTools(); harmonyChanged(); });
    previewBtn.setClickingTogglesState (true);
    previewBtn.setToggleState (true, juce::dontSendNotification);
    previewBtn.setColour (juce::TextButton::buttonOnColourId, col::rnd);
    previewBtn.onClick = [this] { grid.showPreview = previewBtn.getToggleState(); grid.repaint(); };
    addAndMakeVisible (previewBtn);
    lockCondBox.addItemList (condNames(), 1);
    lockCondBox.onChange = [this] { if (lockI >= 0) proc.bank.pats[lockPat].steps[lockI].cond = (int8_t) (lockCondBox.getSelectedId() - 1); };
    addChildComponent (lockCondBox);
    action (clearLocksBtn, *this, true, [this] { if (lockI >= 0) proc.bank.pats[lockPat].steps[lockI].clearLocks(); });
    action (exitLockBtn, *this, false, [this] { setLockStep (-1); });
    clearLocksBtn.setVisible (false); exitLockBtn.setVisible (false);

    // ---------------- onglets
    const char* tabNames[kNumTabs] { "SYNTH", "HARMONIE", "MOD", "FX", "PERFORM", "LAB", "THEORIE", "LINK", "AIDE" };
    for (int i = 0; i < kNumTabs; ++i)
    {
        tabBtns[i].setButtonText (tabNames[i]);
        tabBtns[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabBtns[i]);
    }
    theoryView = std::make_unique<color::gui::TheoryView> (proc);
    theoryView->onChanged = [this] { harmonyChanged(); };
    linkPanel = std::make_unique<LinkPanel> (*proc.link, ap,
        "Color publie ses accords (a l'avance) : Talea peut les suivre (onglet LINK de Talea > Chord follow), Alea peut accorder ses "
        "percussions tonales sur l'accord (Key lock > Chord). Color lit les coups d'Alea : sidechain (FX) et modulateur Drums (MOD).");
    for (juce::Component* pn : { (juce::Component*) &synthPanel, (juce::Component*) &harmPanel, (juce::Component*) &modPanel, (juce::Component*) &fxPanel,
                                 (juce::Component*) &perfPanel, (juce::Component*) &labPanel, (juce::Component*) theoryView.get(),
                                 (juce::Component*) linkPanel.get(), (juce::Component*) &helpPanel })
        addChildComponent (*pn);

    // ---------------- SYNTH
    attachCombo (osc1Box, synthPanel, "osc1type", osc1Names());
    attachCombo (osc2Box, synthPanel, "osc2type", osc2Names());
    attachCombo (subBox, synthPanel, "subtype", subNames());
    attachCombo (filtBox, synthPanel, "ftype", filterNames());
    attachCombo (driveBox, synthPanel, "drivetype", driveNames());
    attachCombo (glideBox, synthPanel, "glidemode", glideModeNames());
    attachCombo (layerBox, synthPanel, "layer", layerNames());
    layerBox.setTooltip ("Couche ajoutee a chaque voix : cloche FM, verre (harmoniques impaires), souffle (bruit accorde), octave");
    setupKnob (layerLvlK, synthPanel, "layerlvl", "Layer Lvl");
    setupKnob (layerDecK, synthPanel, "layerdec", "Layer Dec");
    osc1Box.setTooltip ("Oscillateur 1 (Shape : largeur d'impulsion, desaccord du SuperSaw, repliement, amorti du Pluck)");
    glideBox.setTooltip ("Voice lead : les voix glissent vers les notes du nouvel accord (conduite des voix audible) | Always : portamento a chaque note");
    action (keepBtn, synthPanel, true, [this] { proc.keepLastNote(); });
    action (diceBtn, synthPanel, true, [this] { proc.diceSound (false); });
    action (diceTypesBtn, synthPanel, true, [this] { proc.diceSound (true); });
    action (mutBtn, synthPanel, true, [this] { proc.mutateSound (0.08f); });
    action (rndAmtBtn, synthPanel, true, [this] { proc.randomizeJitter(); });
    action (zeroRndBtn, synthPanel, true, [this] { proc.clearJitter(); });
    action (auditionBtn, synthPanel, false, [this] { proc.auditionChord = juce::jmax (0, lane.selected); });
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
        k.setAccentColour (i >= Cutoff && i <= FDrive ? juce::Colour (0xffff7a1a) : colorCol);
        k.onPlockEdited = [this] { grid.repaint(); };
        synthPanel.addAndMakeVisible (k);
    }
    setupKnob (velSensK, synthPanel, "velsens", "Vel Sens");
    setupKnob (vdriftK, synthPanel, "vdrift", "Voice Drift", false, col::rnd);
    vdriftK.setTooltip ("Desaccord propre a chaque voix (polyphonie analogique)");
    setupKnob (linkRndK, synthPanel, "link_rnd", "Rnd Link", false, col::rnd);
    setupKnob (vspreadK, synthPanel, "vspread", "Voice Spread", false, col::rnd);
    vspreadK.setTooltip ("Chaque voix a son propre caractere : filtre plus ou moins ouvert, niveau (comme un vrai synthe polyphonique analogique)");
    synthPanel.addAndMakeVisible (scope);
    synthPanel.painter = [this] (juce::Graphics& g)
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (synthPanel.getLocalBounds().toFloat(), 6.0f);
        g.setColour (colorCol);
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
        g.drawFittedText ("8 voix polyphoniques, sur-echantillonnees x2. Vel>Filt : la velocite ouvre le filtre. Width : etalement stereo des voix "
                          "(basse au centre). Les potards agissent sur les notes tenues : ideal pour ouvrir un pad en direct.",
                          juce::Rectangle<int> (240, 262, 660, 40), juce::Justification::topLeft, 3);
    };

    // ---------------- HARMONIE
    attachCombo (styleBox2, harmPanel, "style", th::styleNames());
    attachCombo (cycleBox, harmPanel, "cycle", th::cycleNames());
    cycleBox.setTooltip ("Mouvements de fondamentale preferes (Persichetti) : quintes (fonctionnel), tierces (mediantes), secondes (modal / parallele)");
    attachInc (nchordsSlider, harmPanel, "nchords", " accords");
    attachCombo (chordLenBox, harmPanel, "chordlen", chordLenNames());
    setupKnob (complexK, harmPanel, "complex", "Complexity");
    setupKnob (tensionK, harmPanel, "tension", "Tension");
    setupKnob (chromaK, harmPanel, "chroma", "Chroma");
    complexK.setTooltip ("Triades > 7e > 9e, 11e, 13e");
    tensionK.setTooltip ("Alterations (b9, #9, #11, b13), emprunts, dominantes secondaires");
    chromaK.setTooltip ("Substitutions chromatiques (triton, mediantes, accords d'approche)");
    attachCombo (voicingBox2, harmPanel, "voicing", th::voicingNames());
    attachInc (voicesSlider, harmPanel, "voices", " voix");
    attachCombo (bassModeBox, harmPanel, "bassmode", bassModeNames());
    bassModeBox.setTooltip ("Root/Inv : basse sur la fondamentale ou la note de renversement / slash | Pedal : tonique tenue sous tous les accords");
    setupKnob (registerK, harmPanel, "register", "Register");
    setupKnob (spreadK, harmPanel, "spread", "Spread");
    setupKnob (smoothK, harmPanel, "smooth", "Voice Lead");
    smoothK.setTooltip ("Conduite des voix : poids du plus petit mouvement entre deux accords (notes communes gardees)");
    setupKnob (bassLvlK, harmPanel, "basslvl", "Bass Lvl");
    setupKnob (voicChaosK2, harmPanel, "notechaos", "Voic. Chaos", false, col::rnd);
    attachCombo (playModeBox2, harmPanel, "playmode", playModeNames());
    attachCombo (arpModeBox, harmPanel, "arpmode", arpModeNames());
    arpModeBox.setTooltip ("Lead (scale) : melodie improvisee dans la gamme parente de chaque accord (Russell)");
    attachInc (arpOctSlider, harmPanel, "arpoct", " oct");
    setupKnob (strumK, harmPanel, "strum", "Strum");
    attachCombo (keysBox2, harmPanel, "keysmode", keysModeNames());
    keysBox2.setTooltip ("Clavier (a partir de C2) : Transpose | Chord select (touches blanches = accords 1, 2, 3...) | "
                         "Harmonize (la note jouee est harmonisee) | Play (une touche = accord diatonique, plusieurs = accord reconnu)");
    setupKnob (liveSubK, harmPanel, "livesub", "Live Sub", false, col::rnd);
    setupKnob (approachK, harmPanel, "approach", "Approach", false, col::rnd);
    setupKnob (outgoingK, harmPanel, "outgoing", "Outgoing");
    liveSubK.setTooltip ("Substitutions au vol (triton, couleurs, alterations) sans modifier la progression");
    approachK.setTooltip ("Accord d'approche sur le dernier temps (subV, dominante, diminue)");
    outgoingK.setTooltip ("Russell : ingoing (gamme du mode) > outgoing (gammes plus lointaines) pour les arpeges Lead et les tensions");
    harmPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> a[] { { 0, 0, 620, 190 }, { 630, 0, 666, 190 }, { 0, 200, 620, 196 }, { 630, 200, 666, 196 } };
        const char* t[] { "PROGRESSION (generateur)", "VOICING & CONDUITE DES VOIX (Dobbins / Persichetti)", "JEU & CLAVIER", "EN DIRECT (LaVerne / Russell)" };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (col::panel); g.fillRoundedRectangle (a[i].toFloat(), 6.0f);
            g.setColour (colorCol); g.setFont (font (12.0f, true));
            g.drawText (t[i], a[i].getX() + 10, a[i].getY() + 4, 600, 20, juce::Justification::centredLeft);
        }
        g.setColour (col::textDim); g.setFont (font (10.5f));
        g.drawFittedText ("Pop, fonctionnel (T-S-D), jazz II-V, modal (accords caracteristiques du mode), cinematique (mediantes), techno/deep "
                          "(accords mineurs 7/9 et quartes), gospel (passing chords), blues 12 mesures, impressionniste (planing), atonal "
                          "(set-classes, circulation des 12 sons), lydien (gravite tonale de Russell), aleatoire.",
                          juce::Rectangle<int> (12, 142, 600, 44), juce::Justification::topLeft, 3);
        g.drawFittedText ("Regles : 3ce et 7e toujours presentes, pas de 9e mineure entre voix, pas de notes graves trop serrees, "
                          "doublures evitees, notes a eviter (11 sur majeur / dominant) penalisees, mouvement minimal entre les accords.",
                          juce::Rectangle<int> (642, 142, 640, 44), juce::Justification::topLeft, 3);
        g.drawFittedText ("Pad : accords tenus, notes communes conservees. Comp / Strum / Arp : suivent le rythme. Parties par pas : tout, "
                          "note du haut, basse, sans basse. MIDI : C0-G0 scenes, C1-G1 patterns, C2+ clavier.",
                          juce::Rectangle<int> (12, 344, 600, 44), juce::Justification::topLeft, 3);
        g.drawFittedText ("Les substitutions en direct et les approches ne modifient pas la progression : elles sont tirees a chaque passage "
                          "(seed), et visibles en jaune sur la progression. Outgoing eloigne les gammes d'arpege du centre tonal.",
                          juce::Rectangle<int> (642, 344, 640, 44), juce::Justification::topLeft, 3);
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
        g.setColour (col::panel); g.fillRoundedRectangle (juce::Rectangle<float> (0, 158, 1296, 236), 6.0f);
        g.setColour (col::text); g.setFont (font (12.0f, true));
        g.drawText ("MATRICE DE MODULATION (cibles G: = effets et macros, dont Tension, Complexity, Spread, Register, Live Sub)", 10, 162, 1000, 18, juce::Justification::centredLeft);
    };

    // ---------------- FX
    setupKnob (chRateK, fxPanel, "chrate", "Rate");
    setupKnob (chDepthK, fxPanel, "chdepth", "Depth");
    setupKnob (chMixK, fxPanel, "chmix", "Mix");
    attachCombo (dlyDivBox, fxPanel, "dlydiv", delayDivNames());
    attachToggle (pingBtn, fxPanel, "dlyping");
    setupKnob (dlyFbK, fxPanel, "dlyfb", "Feedback");
    setupKnob (dlyToneK, fxPanel, "dlytone", "Tone");
    setupKnob (dlyRetK, fxPanel, "dlyret", "Return");
    setupKnob (dlyRndK, fxPanel, "dlyrnd", "Time Rnd", false, col::rnd);
    setupKnob (revSizeK, fxPanel, "revsize", "Size");
    setupKnob (revDampK, fxPanel, "revdamp", "Damp");
    setupKnob (revWidthK, fxPanel, "revwidth", "Width");
    setupKnob (revRetK, fxPanel, "revret", "Return");
    setupKnob (revPreK, fxPanel, "revpre", "Predelay");
    attachCombo (duckSrcBox, fxPanel, "ducksrc", [] { juce::StringArray a { "Duck: Off" }; for (int t = 0; t < 8; ++t) a.add ("Duck by Alea T" + juce::String (t + 1)); return a; }());
    setupKnob (duckAmtK, fxPanel, "duckamt", "Amount", false, col::rnd);
    setupKnob (duckRelK, fxPanel, "duckrel", "Release");
    setupKnob (glueK, fxPanel, "glue", "Glue");
    setupKnob (mDriveK, fxPanel, "mdrive", "Drive");
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
        g.drawFittedText (slot >= 0 ? "Batterie liee : Alea #" + juce::String (slot + 1) + ". Les coups sont lus a l'avance : le pompage tombe pile."
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
    setupKnob (liveSubK2, perfPanel, "livesub", "Live Sub", false, col::rnd);
    setupKnob (tensionK2, perfPanel, "tension", "Tension");
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
        b.setTooltip ("Scene " + juce::String (n + 1) + " : clic = rappel, Maj-clic = memoriser, MIDI C0+" + juce::String (n) + ". Synchronisee avec Alea / Talea (LINK > Scene)");
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
    keysBox.setTooltip (keysBox2.getTooltip());
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
        const auto chain = juce::StringArray::fromTokens (proc.getChainText().toUpperCase(), " ,;", "");
        int x = 12, idx = 0; const int pos = proc.songPos.load();
        for (auto& tok : chain)
        {
            if (tok.isEmpty()) continue;
            auto r = juce::Rectangle<int> (x, 190 + (idx / 11) * 32, 44, 26);
            g.setColour (idx == pos ? colorCol : col::panelHi); g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (idx == pos ? juce::Colours::black : col::text); g.drawText (tok, r, juce::Justification::centred);
            x += 48; if ((idx + 1) % 11 == 0) x = 12; ++idx;
        }
        g.setColour (col::textDim);
        g.drawFittedText ("Bouton Song + chaine dans la barre du haut (ex. A4 B4 : 4 mesures de A, 4 de B). Chaque pattern a sa progression.",
                          juce::Rectangle<int> (12, 290, 540, 34), juce::Justification::topLeft, 2);
        juce::String keyInfo = "Accord du clavier : ";
        const int kp = proc.keyChordPacked.load();
        keyInfo << (kp != 0 ? th::chordName (unpackChord (kp), proc.keyRoot()) : juce::String ("-"));
        g.drawFittedText ("MIDI : C0-G0 = scenes, C1-G1 = patterns A-H, a partir de C2 = clavier. Transpose : C3 = rien. Chord select : touches "
                          "blanches depuis C3 = accords 1, 2, 3... Harmonize : la note est harmonisee (Dobbins). Play : une touche = accord diatonique "
                          "du degre, plusieurs touches = accord reconnu (et publie aux autres instruments).\n" + keyInfo,
                          juce::Rectangle<int> (590, 224, 690, 100), juce::Justification::topLeft, 6);
    };

    // ---------------- LAB
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
    action (evoCommitBtn, labPanel, true, [this] { proc.commitEvolution(); harmonyChanged(); });
    action (evoResetBtn, labPanel, true, [this] { proc.resetEvolution(); });
    for (auto* c : { &patA, &patB, &patDst }) { for (int p2 = 0; p2 < kPatterns; ++p2) c->addItem (patLetter (p2), p2 + 1); labPanel.addAndMakeVisible (*c); }
    patA.setSelectedId (1, juce::dontSendNotification); patB.setSelectedId (2, juce::dontSendNotification); patDst.setSelectedId (3, juce::dontSendNotification);
    action (breedPatBtn, labPanel, true, [this] { proc.breedPatterns (patA.getSelectedId() - 1, patB.getSelectedId() - 1, patDst.getSelectedId() - 1); harmonyChanged(); });
    action (learnMidiBtn, labPanel, false, [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Analyser les accords d'un fichier MIDI", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) { proc.learnChordsFromMidi (f, proc.editPattern()); harmonyChanged(); } });
    });
    reharmAmtK.setProcessor (&proc);
    setupKnob (reharmAmtK, labPanel, "chroma", "Dose");
    action (reharmAmtBtn, labPanel, true, [this] { proc.reharmonizeAmount (proc.editPattern(), juce::jmax (0.15f, proc.paramReal ("chroma"))); harmonyChanged(); });
    action (genAllBtn, labPanel, true, [this] { for (int p2 = 0; p2 < kPatterns; ++p2) proc.generateProgression (p2); harmonyChanged(); });
    action (lengthsBtn, labPanel, true, [this]
    {
        const int cl = (int) proc.paramReal ("chordlen");
        proc.setChordLengths (proc.editPattern(), cl == 0 ? 2 : (cl == 2 ? 8 : 4));
        harmonyChanged();
    });
    modTargetBox.addItemList ({ "vers +1 (demi-ton, 'truck driver')", "vers +2 (ton)", "vers +5 (sous-dominante)", "vers +7 (dominante)",
                                "vers -3 (relatif mineur)", "vers +3 (relatif majeur)", "vers +4 (mediante)", "vers +8 (sus-mediante)" }, 1);
    modTargetBox.setSelectedId (3, juce::dontSendNotification);
    modMethodBox.addItemList ({ "par la dominante (V7)", "par ii-V", "par accord pivot", "directe" }, 1);
    modMethodBox.setSelectedId (2, juce::dontSendNotification);
    for (auto* b : { &modTargetBox, &modMethodBox }) labPanel.addAndMakeVisible (*b);
    action (modBtn, labPanel, true, [this]
    {
        static const int semis[] { 1, 2, 5, 7, -3, 3, 4, 8 };
        proc.modulateToNext (proc.editPattern(), semis[juce::jlimit (0, 7, modTargetBox.getSelectedId() - 1)], modMethodBox.getSelectedId() - 1);
        harmonyChanged(); refreshTools();
    });
    modBtn.setTooltip ("Copie la grille dans le pattern suivant, dans la nouvelle tonalite, et termine ce pattern par une transition "
                       "(dominante, ii-V, accord pivot commun aux deux tonalites, ou modulation directe). Enchaine les deux en song mode.");
    action (exportBtn, labPanel, false, [this] { auto f = proc.exportMidi (proc.editPattern(), 8); f.revealToUser(); });
    action (melodyBtn, labPanel, false, [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Harmoniser une melodie (MIDI)", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) { proc.harmonizeMelodyFile (f, proc.editPattern()); harmonyChanged(); } });
    });
    melodyBtn.setTooltip ("Charge une melodie : Color estime la tonalite et choisit les accords (notes de l'accord sur les temps forts, "
                          "extensions, dominantes secondaires, enchainements par quintes)");
    drumRhythmBox.addItemList ({ "Entre les kicks (T1)", "Avec la caisse claire (T2)", "Sur le charleston (T3)" }, 1);
    drumRhythmBox.setSelectedId (1, juce::dontSendNotification);
    labPanel.addAndMakeVisible (drumRhythmBox);
    action (drumRhythmBtn, labPanel, true, [this] { proc.rhythmFromDrums (proc.editPattern(), drumRhythmBox.getSelectedId() - 1); refreshTools(); });
    drumRhythmBtn.setTooltip ("Construit le rythme des accords d'apres le pattern d'Alea (AleaLink)");
    labPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> a[] { { 0, 0, 640, 160 }, { 650, 0, 646, 160 }, { 0, 170, 420, 226 }, { 430, 170, 420, 226 }, { 860, 170, 436, 226 } };
        const char* t[] { "HARMONIE : OUTILS", "FAVORIS & CROISEMENT DE SONS", "EVOLUTION AUTOMATIQUE", "CROISEMENT DE PATTERNS", "ANALYSE MIDI / EXPORT" };
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (col::panel); g.fillRoundedRectangle (a[i].toFloat(), 6.0f);
            g.setColour (col::rnd); g.setFont (font (12.0f, true));
            g.drawText (t[i], a[i].getX() + 10, a[i].getY() + 4, 500, 20, juce::Justification::centredLeft);
        }
        g.setColour (col::textDim); g.setFont (font (10.5f));
        g.drawFittedText ("Reharmoniser (dose) applique plusieurs techniques de LaVerne au hasard, dosees par le potard. Generer A-H remplit les 8 patterns "
                          "avec le style courant. Durees = reglage remet chaque accord a la duree de l'onglet HARMONIE. Rythme d'Alea : comping construit sur la batterie liee.",
                          juce::Rectangle<int> (12, 132, 620, 26), juce::Justification::topLeft, 2);
        g.drawFittedText ("Case vide = memoriser, case pleine = rappel + ecoute, Maj-clic = ecraser.", juce::Rectangle<int> (662, 130, 600, 26), juce::Justification::topLeft, 2);
        g.setColour (col::text);
        g.drawText ("Changements : " + juce::String (proc.evolutionAmount()), 12, 300, 200, 18, juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.drawFittedText ("Pattern : des coups basculent et des accords sont substitues (triton, couleurs) au fil des mesures. Son : les reglages derivent. "
                          "Commit ecrit, Reset annule.", juce::Rectangle<int> (12, 322, 400, 60), juce::Justification::topLeft, 4);
        g.drawFittedText ("Rythme d'un parent avec des pas de l'autre, debut de progression de A, fin de B.", juce::Rectangle<int> (442, 240, 400, 40), juce::Justification::topLeft, 2);
        g.drawFittedText ("Glisse un .mid sur le plugin : un fichier d'accords est analyse (tonalite par profils de Krumhansl, accords temps par temps), "
                          "une melodie seule est harmonisee. L'export rend ce que joue Color (accords, rythme, strum, arpege).",
                          juce::Rectangle<int> (872, 310, 410, 80), juce::Justification::topLeft, 5);
    };

    // ---------------- AIDE
    helpPanel.painter = [this] (juce::Graphics& g)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (helpPanel.getLocalBounds().toFloat(), 6.0f);
        g.setColour (col::text); g.setFont (font (12.0f));
        g.drawFittedText (
            "COLOR - instrument harmonique polyphonique (8 voix), troisieme membre de la suite Alea / Talea (AleaLink).\n"
            "PROGRESSION : clic = ecouter | clic droit = substitutions, reharmonisation, qualite, fondamentale, renversement, basse, duree, suite probable | bord droit = duree.\n"
            "  Saisie texte : Am9 Fmaj7 C/E G6, ou Dm7:2 G7:2 Cmaj7 (:N = temps). Generate / Reharm / Vary / Simplify / Enrich / T-+ (diatonique) / rotation / Rev / Cadence.\n"
            "RYTHME : clic = coup (glisser = duree) | Maj = accent | Cmd-clic = p-locks | clic droit = menu | couloirs : velocity, gate, proba, partie (tout / haut / basse / sans basse),\n"
            "  strum, octave, ratchet, condition, nudge. Presets : ronde, noires, contretemps, charleston, clave, anticipations, arpege, house, euclide, aleatoire.\n"
            "JEU : Pad (tenu, notes communes conservees) | Comp | Strum | Arp (Up, Down, Up-Down, Random, Lead dans la gamme parente). Glide Voice lead : les voix glissent.\n"
            "THEORIE : analyse (degres, fonctions, tension, gravite lydienne, gammes parentes), cycle des quintes, suite probable.\n"
            "POTARDS : glisser = valeur | clic droit-glisser / Cmd-glisser / RND EDIT = alea | clic droit = MIDI learn | double-clic = defaut | cadenas = exclu du de.\n"
            "LINK : tempo, lecture, tonalite, gamme, pattern, scene... ; Color publie ses accords : Talea les suit (Chord follow), Alea s'y accorde (Key lock Chord).\n"
            "MIDI : C0-G0 scenes, C1-G1 patterns, C2+ clavier (Transpose / Chord select / Harmonize / Play) ; sortie MIDI ; Drag MIDI = 8 mesures.\n"
            "Sources : Russell (Lydian Chromatic Concept), Persichetti (Twentieth-Century Harmony), Dobbins (Jazz Piano Harmony), LaVerne (Reharmonization),\n"
            "  Boss (Schoenberg's Twelve-Tone Music), Kadmon (grimoires de gammes et d'accords).",
            helpPanel.getLocalBounds().reduced (16, 12), juce::Justification::topLeft, 40);
    };

    refreshProgText();
    showTab (0);
    startTimerHz (30);
}

ColorView::~ColorView()
{
    stopTimer();
    sliderAtts.clear(); comboAtts.clear(); buttonAtts.clear();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void ColorView::setupKnob (RndKnob& k, juce::Component& parent, const juce::String& paramId, const juce::String& label, bool bipolar, juce::Colour c)
{
    k.setProcessor (&proc);
    k.bind (proc.apvts.getParameter (paramId), nullptr, nullptr, nullptr, label);
    k.setBipolar (bipolar);
    k.setAccentColour (c);
    parent.addAndMakeVisible (k);
}

void ColorView::action (juce::TextButton& b, juce::Component& parent, bool undoable, std::function<void()> fn)
{
    b.onClick = [this, fn, undoable] { if (undoable) proc.pushUndo(); fn(); grid.repaint(); lane.repaint(); };
    parent.addAndMakeVisible (b);
}

void ColorView::attachCombo (juce::ComboBox& c, juce::Component& parent, const juce::String& paramId, const juce::StringArray& items)
{
    c.addItemList (items, 1);
    parent.addAndMakeVisible (c);
    comboAtts.push_back (std::make_unique<juce::ComboBoxParameterAttachment> (*proc.apvts.getParameter (paramId), c));
}

void ColorView::attachToggle (juce::Button& b, juce::Component& parent, const juce::String& paramId)
{
    b.setClickingTogglesState (true);
    parent.addAndMakeVisible (b);
    buttonAtts.push_back (std::make_unique<juce::ButtonParameterAttachment> (*proc.apvts.getParameter (paramId), b));
}

void ColorView::attachInc (juce::Slider& s, juce::Component& parent, const juce::String& paramId, const juce::String& suffix)
{
    s.setSliderStyle (juce::Slider::IncDecButtons);
    s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 70, 22);
    s.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    parent.addAndMakeVisible (s);
    sliderAtts.push_back (std::make_unique<juce::SliderParameterAttachment> (*proc.apvts.getParameter (paramId), s));
    s.setTextValueSuffix (suffix);
    s.updateText();
}

CPattern& ColorView::editPat() { return proc.bank.pats[proc.editPattern()]; }

void ColorView::showTab (int i)
{
    currentTab = i;
    juce::Component* panels[kNumTabs] { &synthPanel, &harmPanel, &modPanel, &fxPanel, &perfPanel, &labPanel, theoryView.get(), linkPanel.get(), &helpPanel };
    for (int k = 0; k < kNumTabs; ++k)
    {
        panels[k]->setVisible (k == i);
        tabBtns[k].setColour (juce::TextButton::buttonColourId, k == i ? col::panelHi.brighter (0.25f) : col::panel);
        tabBtns[k].setColour (juce::TextButton::textColourOffId, k == i ? colorCol : col::textDim);
    }
    if (i == 5) refreshLab();
    if (i == 6) { theoryView->selected = lane.selected; theoryView->refresh(); }
}

void ColorView::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto fac = ColorAudioProcessor::factoryPresetNames();
    presetBox.addSectionHeading ("Usine");
    for (int i = 0; i < fac.size(); ++i) presetBox.addItem (fac[i], i + 1);
    userPresets = ColorAudioProcessor::userPresetDir().findChildFiles (juce::File::findFiles, false, "*.color");
    userPresets.sort();
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("Utilisateur");
        for (int i = 0; i < userPresets.size(); ++i) presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 100 + i);
    }
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
}

void ColorView::stepPreset (int delta)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i) ids.add (presetBox.getItemId (i));
    if (ids.isEmpty()) return;
    int cur = ids.indexOf (presetBox.getSelectedId());
    cur = cur < 0 ? 0 : (cur + delta + ids.size()) % ids.size();
    presetBox.setSelectedId (ids[cur], juce::sendNotificationSync);
}

void ColorView::refreshTools()
{
    auto& P = editPat();
    lenSlider.setValue (P.length.load(), juce::dontSendNotification);
    keyShiftSlider.setValue (P.keyShift.load(), juce::dontSendNotification);
    rateBox.setSelectedId (P.rate.load() + 1, juce::dontSendNotification);
    dirBox.setSelectedId (P.dir.load() + 1, juce::dontSendNotification);
    grooveBox.setSelectedId (P.groove.load() + 1, juce::dontSendNotification);
    grid.repaint(); lane.repaint();
}

void ColorView::refreshProgText()
{
    if (! progEdit.hasKeyboardFocus (true))
    {
        progEdit.setText (proc.progressionText (proc.editPattern()), false);
        progEdit.applyColourToAllText (col::text);
    }
    int h = proc.editPattern() * 7919 + proc.keyRoot() * 31;
    const auto& P = editPat();
    for (int i = 0; i < P.chordCount(); ++i) h = h * 131 + P.chords[i].load();
    lastProgHash = h;
}

void ColorView::harmonyChanged()
{
    lane.selected = juce::jlimit (0, editPat().chordCount() - 1, lane.selected);
    refreshProgText();
    lane.repaint(); voicingView.repaint();
    if (theoryView != nullptr) { theoryView->selected = lane.selected; theoryView->refresh(); }
}

void ColorView::refreshLab()
{
    for (int f = 0; f < kFavs; ++f)
    {
        const bool v = proc.favs[f].valid;
        favBtns[f].setColour (juce::TextButton::buttonColourId, v ? colorCol.darker (0.8f) : col::panelHi);
        favBtns[f].setColour (juce::TextButton::textColourOffId, v ? juce::Colours::white : col::textDim);
    }
    labPanel.repaint();
}

void ColorView::refreshAfterStateChange()
{
    setLockStep (-1);
    chainEdit.setText (proc.getChainText(), false);
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
    lockTypesBtn.setToggleState (proc.locks[kNumTP].load(), juce::dontSendNotification);
    refreshTools(); refreshLab(); harmonyChanged();
    for (auto& k : tpKnobs) k.repaint();
    repaint();
}

void ColorView::setLockStep (int i)
{
    lockI = i; lockPat = proc.editPattern();
    grid.lockStep = i;
    for (int t = 0; t < kNumTP; ++t) tpKnobs[t].setPlockTarget (i >= 0 ? &proc.bank.pats[lockPat].steps[i].plock[t] : nullptr);
    const bool on = i >= 0;
    lockCondBox.setVisible (on); clearLocksBtn.setVisible (on); exitLockBtn.setVisible (on);
    if (on) { lockCondBox.setSelectedId (proc.bank.pats[lockPat].steps[i].cond + 1, juce::dontSendNotification); if (currentTab != 0) showTab (0); }
    grid.repaint(); synthPanel.repaint(); repaint (lockArea);
}

bool ColorView::dropFiles (const juce::StringArray& files)
{
    if (files.isEmpty()) return false;
    juce::File f (files[0]);
    if (f.hasFileExtension ("color")) { if (proc.loadPresetFile (f)) refreshAfterStateChange(); return true; }
    if (f.hasFileExtension ("mid;midi"))
    {
        const bool ok = ColorAudioProcessor::isMelodyFile (f) ? proc.harmonizeMelodyFile (f, proc.editPattern())
                                                              : proc.learnChordsFromMidi (f, proc.editPattern());
        harmonyChanged(); showTab (6);
        return ok;
    }
    return false;
}

void ColorView::timerCallback()
{
    proc.flash = proc.flash.load() * 0.82f;
    for (auto& k : tpKnobs) k.refreshLive();
    for (auto* k : { &chaosKnob, &morphK, &densityK }) k->refreshLive();
    const int ep = proc.editPattern();
    if (lockI >= 0 && ep != lockPat) setLockStep (-1);
    if (ep != lastEp) { lastEp = ep; refreshTools(); harmonyChanged(); }
    else if (! lenSlider.isMouseButtonDown() && (int) lenSlider.getValue() != editPat().length.load())
        lenSlider.setValue (editPat().length.load(), juce::dontSendNotification);
    {
        // progression modifiee ailleurs (evolution commitee, preset, LINK...) : on resynchronise le texte
        int h = ep * 7919 + proc.keyRoot() * 31;
        const auto& P = editPat();
        for (int i = 0; i < P.chordCount(); ++i) h = h * 131 + P.chords[i].load();
        if (h != lastProgHash) harmonyChanged();
    }
    for (int i = 0; i < kPatterns; ++i)
    {
        patBtns[i].setColour (juce::TextButton::buttonColourId, i == ep ? colorCol : col::panelHi);
        patBtns[i].setColour (juce::TextButton::textColourOffId, i == ep ? juce::Colours::black : col::text);
    }
    if (++previewTick % 3 == 0 && grid.showPreview) grid.preview = proc.previewNextBar();
    if (currentTab == 4)
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
    grid.repaint(); lane.repaint();
    if (previewTick % 2 == 0) voicingView.repaint();
    if (currentTab == 0) scope.refresh();
    if (currentTab == 2) for (auto& d : modDisp) d->push();
    if (currentTab == 3 || currentTab == 4) (currentTab == 3 ? fxPanel : perfPanel).repaint();
    if (currentTab == 5 && previewTick % 10 == 0) labPanel.repaint();
    repaint (0, 0, W, 84);
}

void ColorView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (colorCol);
    g.setFont (font (32.0f, true));
    g.drawText ("COLOR", 16, 6, 130, 38, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (10.0f));
    g.drawText ("harmonic random synth - AleaLink", 16, 44, 150, 14, juce::Justification::centredLeft);
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
        g.setColour (pk > 0.95f ? col::lock : colorCol);
        g.fillRect (m.withTop (m.getBottom() - m.getHeight() * pk));
        g.setColour (colorCol.withAlpha (0.2f + 0.8f * proc.flash.load()));
        g.fillEllipse (150.0f, 12.0f, 8.0f, 8.0f);
    }
    g.setColour (col::panel);
    g.fillRoundedRectangle (knobArea.toFloat(), 6.0f);
    g.setFont (font (11.0f, true));
    if (lockI >= 0)
    {
        g.setColour (col::plock);
        g.drawText ("P-LOCK pas " + juce::String (lockI + 1), knobArea.getX() + 10, knobArea.getY() + 4, 120, 16, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (colorCol);
        g.drawText ("RYTHME & ALEA", knobArea.getX() + 10, knobArea.getY() + 4, 200, 16, juce::Justification::centredLeft);
    }
    if (RndKnob::editRandomMode && currentTab == 0)
    {
        g.setColour (col::rnd.withAlpha (0.6f));
        g.drawRoundedRectangle (synthPanel.getBounds().toFloat().expanded (1.0f), 6.0f, 1.5f);
    }
}

void ColorView::resized()
{
    presetPrev.setBounds (170, 8, 24, 26); presetBox.setBounds (196, 8, 190, 26); presetNext.setBounds (388, 8, 24, 26); presetSave.setBounds (416, 8, 50, 26);
    undoBtn.setBounds (478, 8, 56, 26); redoBtn.setBounds (538, 8, 56, 26);
    midiOutBtn.setBounds (606, 8, 74, 26); dragMidi.setBounds (684, 8, 100, 26);
    rndEditBtn.setBounds (796, 8, 76, 26); diceBtnTop.setBounds (876, 8, 56, 26); seedSlider.setBounds (936, 8, 90, 26);
    rootBox.setBounds (1034, 8, 54, 26); modeBox.setBounds (1092, 8, 98, 26);
    playBtn.setBounds (170, 44, 56, 28); bpmSlider.setBounds (230, 44, 96, 28);
    for (int i = 0; i < kPatterns; ++i) patBtns[i].setBounds (334 + i * 29, 44, 27, 28);
    copyPatBox.setBounds (570, 44, 84, 28); songBtn.setBounds (660, 44, 52, 28); chainEdit.setBounds (716, 44, 120, 28);
    fillBtn.setBounds (842, 44, 56, 28); rndModeBox.setBounds (904, 44, 106, 28); distBox.setBounds (1014, 44, 112, 28); linkBtn.setBounds (1130, 44, 58, 28);
    chaosKnob.setBounds (1194, 4, 58, 74); masterKnob.setBounds (1252, 4, 58, 74);

    // harmonie
    const int y0 = 86;
    lane.setBounds (12, y0, 870, 116);
    {
        int x = 12; const int ty = y0 + 120, th = 26;
        auto place = [&] (juce::Component& c, int w) { c.setBounds (x, ty, w, th); x += w + 3; };
        place (styleBox, 118); place (genProgBtn, 70); x += 4; place (reharmBox, 136); place (reharmBtn, 58); x += 4;
        place (varyProgBtn, 44); place (simplifyBtn, 62); place (enrichBtn, 56); x += 4;
        place (trDnBtn, 28); place (trUpBtn, 28); place (rotLBtn, 22); place (rotRBtn, 22); place (revProgBtn, 38); x += 4;
        place (cadenceBox, 100);
    }
    progEdit.setBounds (12, y0 + 150, 650, 24);
    keyShiftSlider.setBounds (666, y0 + 150, 122, 24);
    copyHarmBtn.setBounds (792, y0 + 150, 42, 24); pasteHarmBtn.setBounds (838, y0 + 150, 44, 24);
    grid.setBounds (12, y0 + 178, 870, 168);

    voicingView.setBounds (894, y0, 414, 200);
    playModeBox.setBounds (894, y0 + 204, 205, 24); voicingBox.setBounds (1103, y0 + 204, 205, 24);
    knobArea = { 894, y0 + 232, 414, 114 };
    {
        const int kw = 68; int kx = knobArea.getX() + 3;
        for (auto* k : { &swingK, &humanK, &densityK, &seqChaosK, &ratChaosK, &voicChaosK }) { k->setBounds (kx, knobArea.getY() + 22, kw, 90); kx += kw + 0; }
    }
    lockArea = knobArea;
    lockCondBox.setBounds (knobArea.getX() + 120, knobArea.getY() + 2, 120, 20);
    clearLocksBtn.setBounds (knobArea.getX() + 244, knobArea.getY() + 2, 90, 20);
    exitLockBtn.setBounds (knobArea.getX() + 338, knobArea.getY() + 2, 70, 20);

    // outils rythme (pleine largeur)
    {
        int x = 12; const int ty = y0 + 350, th = 26;
        auto place = [&] (juce::Component& c, int w) { c.setBounds (x, ty, w, th); x += w + 3; };
        place (lenSlider, 86); place (rateBox, 70); place (dirBox, 84); place (grooveBox, 108); x += 6;
        place (rhythmBox, 160); place (rhythmApplyBtn, 50); place (rhythmGenBtn, 84); place (mutRBtn, 60); x += 6;
        place (shlBtn, 22); place (shrBtn, 22); x += 6;
        place (hitsSlider, 84); place (euclidBtn, 52); x += 6;
        place (clearBtn, 44); place (copyBtn, 62); place (pasteBtn, 66); x += 6;
        place (previewBtn, 64);
    }

    const int tabY = y0 + 382;
    for (int i = 0; i < kNumTabs; ++i) tabBtns[i].setBounds (12 + i * 96, tabY, 92, 24);
    const auto pb = juce::Rectangle<int> (12, tabY + 28, 1296, H - (tabY + 28) - 6);
    for (juce::Component* pn : { (juce::Component*) &synthPanel, (juce::Component*) &harmPanel, (juce::Component*) &modPanel, (juce::Component*) &fxPanel,
                                 (juce::Component*) &perfPanel, (juce::Component*) &labPanel, (juce::Component*) theoryView.get(),
                                 (juce::Component*) linkPanel.get(), (juce::Component*) &helpPanel })
        pn->setBounds (pb);

    // SYNTH
    {
        int sx = 110; const int sy = 6;
        auto pl = [&] (juce::Component& c, int w) { c.setBounds (sx, sy, w, 22); sx += w + 4; };
        pl (osc1Box, 100); pl (osc2Box, 104); pl (subBox, 120); pl (filtBox, 104); pl (driveBox, 76); pl (glideBox, 140); pl (layerBox, 130);
        const auto& order = tpDisplayOrder();
        const int kw2 = 70, kh = 86;
        for (int i = 0; i < kNumTP; ++i)
            tpKnobs[order[(size_t) i]].setBounds (8 + (i % 13) * kw2, 52 + (i / 13) * (kh + 14), kw2, kh);
        velSensK.setBounds (8 + 11 * kw2, 52 + kh + 14, kw2, kh);
        vdriftK.setBounds (8 + 12 * kw2, 52 + kh + 14, kw2, kh);
        linkRndK.setBounds (8, 252, kw2, kh);
        vspreadK.setBounds (8 + kw2, 252, kw2, kh);
        layerLvlK.setBounds (8 + kw2, 252, kw2, kh); layerDecK.setBounds (8 + 2 * kw2, 252, kw2, kh);
        scope.setBounds (8 + 13 * kw2 + 10, 34, 1296 - (8 + 13 * kw2 + 10) - 10, 200);
        const int bw = (scope.getWidth() - 9) / 4;
        int bx = scope.getX(), by = scope.getBottom() + 8;
        for (auto* b : { &auditionBtn, &keepBtn, &diceBtn, &diceTypesBtn }) { b->setBounds (bx, by, bw, 26); bx += bw + 3; }
        bx = scope.getX(); by += 30;
        for (auto* b : { &mutBtn, &rndAmtBtn, &zeroRndBtn, &lockTypesBtn }) { b->setBounds (bx, by, bw, 26); bx += bw + 3; }
    }
    // HARMONIE
    {
        styleBox2.setBounds (12, 30, 150, 26); cycleBox.setBounds (12, 62, 150, 26);
        nchordsSlider.setBounds (170, 30, 140, 26); chordLenBox.setBounds (170, 62, 140, 26);
        int kx = 330; for (auto* k : { &complexK, &tensionK, &chromaK }) { k->setBounds (kx, 24, 90, 112); kx += 94; }
        voicingBox2.setBounds (642, 30, 150, 26); voicesSlider.setBounds (642, 62, 150, 26); bassModeBox.setBounds (642, 94, 150, 26);
        kx = 800; for (auto* k : { &registerK, &spreadK, &smoothK, &bassLvlK, &voicChaosK2 }) { k->setBounds (kx, 24, 94, 112); kx += 98; }
        playModeBox2.setBounds (12, 230, 150, 26); arpModeBox.setBounds (12, 262, 150, 26); arpOctSlider.setBounds (12, 294, 150, 26);
        keysBox2.setBounds (170, 230, 170, 26);
        strumK.setBounds (350, 224, 90, 112);
        kx = 642; for (auto* k : { &liveSubK, &approachK, &outgoingK }) { k->setBounds (kx, 224, 100, 112); kx += 104; }
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
    modDepthK.setBounds (1296 - 84, 186, 76, 74);
    for (int s = 0; s < kMxSlots; ++s)
    {
        const int bx = (s / 4) * 600 + 20, by = 190 + (s % 4) * 44;
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
    { int px = 316; for (auto* k : { &densityK2, &chaosK2, &liveSubK2, &tensionK2, &duckK2, &evoK2 }) { k->setBounds (px, 26, 86, 112); px += 88; }
      fillLatchBtn.setBounds (px + 4, 60, 90, 36); }
    for (int n = 0; n < kScenes; ++n) sceneBtns[n].setBounds (970 + (n % 4) * 40, 30 + (n / 4) * 34, 36, 28);
    sceneStoreBtn.setBounds (970, 100, 76, 24); sceneXfOnBtn.setBounds (1052, 100, 76, 24);
    sceneABox.setBounds (1134, 30, 100, 24); sceneBBox.setBounds (1134, 62, 100, 24);
    sceneXfK.setBounds (1236, 24, 58, 116);
    keysBox.setBounds (590, 190, 200, 26);
    // LAB
    reharmAmtK.setBounds (12, 26, 90, 84);
    reharmAmtBtn.setBounds (110, 34, 170, 28); genAllBtn.setBounds (110, 68, 170, 28);
    lengthsBtn.setBounds (290, 34, 160, 28);
    modTargetBox.setBounds (290, 68, 200, 26); modMethodBox.setBounds (494, 68, 140, 26); modBtn.setBounds (290, 100, 344, 28);
    for (int f = 0; f < kFavs; ++f) favBtns[f].setBounds (662 + f * 44, 32, 40, 32);
    favStoreBtn.setBounds (662, 72, 60, 28); keepBtn2.setBounds (728, 72, 100, 28);
    breedA.setBounds (840, 72, 86, 28); breedB.setBounds (930, 72, 86, 28); breedVoiceBtn.setBounds (1020, 72, 80, 28);
    evoK.setBounds (12, 196, 100, 96);
    evoBarsBox.setBounds (130, 200, 100, 26); evoTargetBox.setBounds (236, 200, 136, 26);
    evoCommitBtn.setBounds (130, 234, 100, 26); evoResetBtn.setBounds (236, 234, 100, 26);
    patA.setBounds (442, 200, 90, 28); patB.setBounds (538, 200, 90, 28); patDst.setBounds (634, 200, 90, 28); breedPatBtn.setBounds (730, 200, 100, 28);
    learnMidiBtn.setBounds (872, 198, 220, 28); melodyBtn.setBounds (872, 230, 220, 28); exportBtn.setBounds (872, 262, 220, 28);
    drumRhythmBox.setBounds (290, 68, 190, 28); drumRhythmBtn.setBounds (484, 68, 140, 28);
}

// ===========================================================================
ColorAudioProcessorEditor::ColorAudioProcessorEditor (ColorAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p)
{
    addAndMakeVisible (view);
    const float s = juce::jlimit (0.55f, 1.6f, proc.uiScale.load());
    setSize ((int) (ColorView::W * s), (int) (ColorView::H * s));
    setResizable (true, true);
    setResizeLimits ((int) (ColorView::W * 0.55), (int) (ColorView::H * 0.55), (int) (ColorView::W * 1.6), (int) (ColorView::H * 1.6));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) ColorView::W / (double) ColorView::H);
    setSize ((int) (ColorView::W * s), (int) (ColorView::H * s));
}

void ColorAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) ColorView::W;
    view.setBounds (0, 0, ColorView::W, ColorView::H);
    view.setTransform (juce::AffineTransform::scale (s));
    proc.uiScale = s;
}
