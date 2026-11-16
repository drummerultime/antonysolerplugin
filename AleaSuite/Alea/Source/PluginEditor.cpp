#include "PluginEditor.h"

using namespace alea;
using namespace alea::gui;

static juce::String patLetter (int p) { return juce::String::charToString ((juce::juce_wchar) ('A' + p)); }

// ===========================================================================
MainView::MainView (AleaAudioProcessor& p)
    : proc (p), dragMidi ([&p] { return p.exportMidi (p.editPattern(), 4); }), trackList (p), grid (p), lanes (p, 200.0f),
      scope (p.scopeBuf, AleaAudioProcessor::kScopeLen, p.scopeVersion, [&p] { return col::track (p.scopeTrack.load()); })
{
    lnf.setColourScheme ({ col::bg, col::panelHi, col::panel, col::line, col::text,
                           col::panelHi, juce::Colours::white, col::accent, col::text });
    lnf.setColour (juce::TextButton::buttonOnColourId, col::accent);
    lnf.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    lnf.setColour (juce::ComboBox::outlineColourId, col::line);
    lnf.setColour (juce::Slider::textBoxOutlineColourId, col::line);
    lnf.setColour (juce::Slider::trackColourId, col::accent.withAlpha (0.6f));
    lnf.setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    lnf.setColour (juce::TextEditor::backgroundColourId, col::panelHi);
    lnf.setColour (juce::TextEditor::outlineColourId, col::line);
    setLookAndFeel (&lnf);

    auto& ap = proc.apvts;

    // ---------------- barre du haut : presets / historique / MIDI
    addAndMakeVisible (presetBox);
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id >= 1 && id <= AleaAudioProcessor::factoryPresetNames().size()) proc.loadFactoryPreset (id - 1);
        else if (id >= 100 && id - 100 < userPresets.size()) proc.loadPresetFile (userPresets[id - 100]);
        else return;
        refreshAfterStateChange();
    };
    presetBox.setTooltip ("Presets d'usine et presets utilisateur (Documents/Alea/Presets)");
    action (presetPrev, *this, false, [this] { stepPreset (-1); });
    action (presetNext, *this, false, [this] { stepPreset (1); });
    action (presetSave, *this, false, [this]
    {
        auto dir = AleaAudioProcessor::userPresetDir();
        dir.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Enregistrer le preset", dir.getChildFile (proc.currentPresetName + ".alea"), "*.alea");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  proc.savePresetFile (f.withFileExtension ("alea"));
                                  refreshPresetList();
                              });
    });
    refreshPresetList();

    action (undoBtn, *this, false, [this] { if (proc.undo()) refreshAfterStateChange(); });
    action (redoBtn, *this, false, [this] { if (proc.redo()) refreshAfterStateChange(); });
    undoBtn.setTooltip ("Annuler (de, mutations, edition des pas, potards...)");

    attachToggle (midiOutBtn, *this, "midiout");
    midiOutBtn.setTooltip ("Envoie les declenchements du sequenceur en MIDI (notes 36-43) vers le DAW");
    addAndMakeVisible (dragMidi);
    dragMidi.setTooltip ("Glisser vers une piste MIDI de Live : 4 mesures du pattern en clip MIDI (avec probas et chaos). Clic : ouvrir le fichier");

    rndEditBtn.setClickingTogglesState (true);
    rndEditBtn.setColour (juce::TextButton::buttonOnColourId, col::rnd);
    rndEditBtn.onClick = [this] { RndKnob::editRandomMode = rndEditBtn.getToggleState(); repaint(); };
    rndEditBtn.setTooltip ("Mode edition de l'alea : les potards reglent l'amplitude aleatoire");
    addAndMakeVisible (rndEditBtn);
    action (diceAllBtn, *this, true, [this] { proc.diceAll(); });

    for (auto* s : { &bpmSlider, &seedSlider })
    {
        s->setSliderStyle (juce::Slider::LinearBar);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 80, 22);
        addAndMakeVisible (*s);
    }
    sliderAtts.push_back (std::make_unique<SA> (*ap.getParameter ("bpm"), bpmSlider));
    sliderAtts.push_back (std::make_unique<SA> (*ap.getParameter ("seed"), seedSlider));
    bpmSlider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " BPM"; };
    seedSlider.textFromValueFunction = [] (double v) { return "Seed " + juce::String ((int) v); };
    for (auto* s : { &bpmSlider, &seedSlider })
    {
        s->valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getDoubleValue(); };
        s->updateText();
    }
    seedSlider.setTooltip ("Graine du hasard : meme graine = memes variations");
    bpmSlider.setTooltip ("Tempo interne (dans un DAW, Alea suit le tempo de l'hote)");

    attachToggle (playBtn, *this, "play");
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto& b = patBtns[i];
        b.setButtonText (patLetter (i));
        b.onClick = [this, i] { proc.setParamReal ("pattern", (float) i); };
        b.setTooltip ("Pattern " + patLetter (i) + " (change a la mesure suivante). MIDI : C2..G2");
        addAndMakeVisible (b);
    }
    copyPatBox.setTextWhenNothingSelected ("Copy to...");
    for (int i = 0; i < kNumPatterns; ++i) copyPatBox.addItem (patLetter (i), i + 1);
    copyPatBox.onChange = [this]
    {
        const int id = copyPatBox.getSelectedId();
        if (id > 0) { proc.pushUndo(); proc.copyPattern (proc.editPattern(), id - 1); }
        copyPatBox.setSelectedId (0, juce::dontSendNotification);
    };
    addAndMakeVisible (copyPatBox);

    attachToggle (songBtn, *this, "songmode");
    songBtn.setTooltip ("Song mode : enchaine les patterns selon la chaine (ex. A4 B4 A2 C2)");
    chainEdit.setText (proc.getChainText(), false);
    chainEdit.setFont (font (13.0f));
    chainEdit.setTooltip ("Chaine du song mode : lettre = pattern, chiffre = nombre de mesures. Entree pour valider");
    auto commitChain = [this]
    {
        const bool ok = proc.setChainText (chainEdit.getText());
        chainEdit.setColour (juce::TextEditor::textColourId, ok ? col::text : col::lock);
        chainEdit.applyColourToAllText (ok ? col::text : col::lock);
    };
    chainEdit.onReturnKey = commitChain;
    chainEdit.onFocusLost = commitChain;
    addAndMakeVisible (chainEdit);

    fillBtn.setColour (juce::TextButton::buttonColourId, col::rnd.darker (0.9f));
    fillBtn.onStateChange = [this]
    {
        const bool down = fillBtn.isDown();
        if (down != fillWasDown) { fillWasDown = down; proc.setParamNorm ("fill", down ? 1.0f : 0.0f); }
    };
    fillBtn.setTooltip ("Maintenir : mode Fill (conditions Fill, plus de roulements et de densite)");
    addAndMakeVisible (fillBtn);

    attachCombo (rndModeBox, *this, "rndmode", { "Mode: Hit", "Mode: Bar", "Mode: Drift" });
    rndModeBox.setTooltip ("Hit : tirage a chaque coup | Bar : un tirage par boucle | Drift : marche aleatoire lente");
    attachCombo (distBox, *this, "rnddist", { "Dist: Uniform", "Dist: Gauss", "Dist: Rare" });
    distBox.setTooltip ("Forme du hasard : uniforme, gaussienne (surtout subtil), rare (petits ecarts et rares grands sauts)");
    attachCombo (scaleBox, *this, "scale", scaleNames());
    attachCombo (rootBox, *this, "root", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" });
    rootBox.setTooltip ("Tonique partagee avec Talea (AleaLink). Sert au Key lock (onglet LINK)");
    linkBtn.onClick = [this] { showTab (5); };
    linkBtn.setTooltip ("AleaLink : instances detectees et synchronisation");
    addAndMakeVisible (linkBtn);
    scaleBox.setTooltip ("L'accordage aleatoire (alea + modulation) retombe sur cette gamme");

    setupKnob (chaosKnob, *this, "chaos", "CHAOS", false, col::rnd);
    chaosKnob.setTooltip ("Multiplicateur global des amplitudes d'alea (0 = sons fixes) | clic droit : MIDI learn");
    setupKnob (masterKnob, *this, "master", "Master");

    // ---------------- sequenceur
    addAndMakeVisible (trackList);
    addAndMakeVisible (grid);
    addAndMakeVisible (lanes);
    trackList.onSelect = [this] (int t) { selectTrack (t); };
    grid.onSelect = [this] (int t) { selectTrack (t); };
    grid.onLockStep = [this] (int t, int i) { setLockStep (t, i); };
    grid.setTooltip ("Clic : pas | Maj : accent | Cmd-clic : p-locks | clic droit : menu du pas | Alt-clic : longueur");

    setupKnob (swingK, *this, "swing", "Swing");
    setupKnob (accentK, *this, "accent", "Accent");
    setupKnob (humanK, *this, "humanize", "Humanize", false, col::rnd);
    setupKnob (seqChaosK, *this, "seqchaos", "Seq Chaos", false, col::rnd);
    setupKnob (ratChaosK, *this, "ratchaos", "Ratch Chaos", false, col::rnd);
    setupKnob (densityK, *this, "density", "Density", true, col::rnd);

    for (auto* s : { &lenSlider, &hitsSlider, &rotSlider })
    {
        s->setSliderStyle (juce::Slider::IncDecButtons);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 24);
        s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        addAndMakeVisible (*s);
    }
    lenSlider.setRange (1, kMaxSteps, 1);   lenSlider.setTextValueSuffix (" st");
    hitsSlider.setRange (0, kMaxSteps, 1);  hitsSlider.setTextValueSuffix (" hits"); hitsSlider.setValue (5, juce::dontSendNotification);
    rotSlider.setRange (0, kMaxSteps - 1, 1); rotSlider.setTextValueSuffix (" rot");
    lenSlider.onValueChange = [this] { editTrack().length = (int8_t) lenSlider.getValue(); grid.repaint(); lanes.repaint(); };
    rateBox.addItemList (rateNames(), 1);
    rateBox.onChange = [this] { editTrack().rate = (int8_t) (rateBox.getSelectedId() - 1); };
    dirBox.addItemList (dirNames(), 1);
    dirBox.onChange = [this] { editTrack().dir = (int8_t) (dirBox.getSelectedId() - 1); };
    dirBox.setTooltip ("Sens de lecture de la piste");
    addAndMakeVisible (rateBox);
    addAndMakeVisible (dirBox);
    grooveBox.addItemList (grooveNames(), 1);
    grooveBox.onChange = [this] { editTrack().groove = (int8_t) (grooveBox.getSelectedId() - 1); };
    grooveBox.setTooltip ("Groove de la piste (Global = swing general)");
    addAndMakeVisible (grooveBox);
    previewBtn.setClickingTogglesState (true);
    previewBtn.setToggleState (true, juce::dontSendNotification);
    previewBtn.setColour (juce::TextButton::buttonOnColourId, col::rnd);
    previewBtn.onClick = [this] { grid.showPreview = previewBtn.getToggleState(); grid.repaint(); };
    previewBtn.setTooltip ("Apercu dans la grille de la mesure a venir : point turquoise = coup fantome ajoute, trait rouge = pas qui sautera");
    addAndMakeVisible (previewBtn);

    auto pat = [this] { return proc.editPattern(); };
    action (euclidBtn, *this, true, [=] { proc.euclidTrack (pat(), selected, (int) hitsSlider.getValue(), (int) rotSlider.getValue()); });
    action (rndPatBtn, *this, true, [=] { proc.randomizeTrackPattern (pat(), selected); });
    action (varyBtn,   *this, true, [=] { proc.varyTrack (pat(), selected, 0.35f); });
    action (mutPatBtn, *this, true, [=] { proc.mutateTrackPattern (pat(), selected, 0.12f); });
    action (shlBtn,    *this, true, [=] { proc.shiftTrack (pat(), selected, -1); });
    action (shrBtn,    *this, true, [=] { proc.shiftTrack (pat(), selected, 1); });
    action (clearBtn,  *this, true, [=] { proc.clearTrack (pat(), selected); });
    action (copyBtn,   *this, false, [=] { proc.copyTrack (pat(), selected); });
    action (pasteBtn,  *this, true, [=] { proc.pasteTrack (pat(), selected); refreshTrackTools(); });
    action (rndAllPatBtn, *this, true, [=] { for (int t = 0; t < kNumTracks; ++t) proc.randomizeTrackPattern (pat(), t); });
    action (varyAllBtn,   *this, true, [=] { for (int t = 0; t < kNumTracks; ++t) proc.varyTrack (pat(), t, 0.35f); });
    varyBtn.setTooltip ("Variation apprise : re-tire ~35 % des pas selon le style de la piste (modele de Markov)");
    varyAllBtn.setTooltip ("Variation apprise sur toutes les pistes");
    euclidBtn.setTooltip ("Rythme euclidien : N coups repartis + rotation");

    lockCondBox.addItemList (condNames(), 1);
    lockCondBox.onChange = [this]
    {
        if (lockT < 0) return;
        proc.bank.pats[lockPat].tracks[lockT].steps[lockI].cond = (int8_t) (lockCondBox.getSelectedId() - 1);
        grid.repaint(); lanes.repaint();
    };
    addChildComponent (lockCondBox);
    action (clearLocksBtn, *this, true, [this] { if (lockT >= 0) proc.bank.pats[lockPat].tracks[lockT].steps[lockI].clearLocks(); });
    action (exitLockBtn, *this, false, [this] { setLockStep (-1, -1); });
    clearLocksBtn.setVisible (false); exitLockBtn.setVisible (false);

    // ---------------- onglets
    const char* tabNames[] { "VOICE", "MOD", "FX", "PERFORM", "LAB", "LINK", "AIDE" };
    for (int i = 0; i < kNumTabs; ++i)
    {
        tabBtns[i].setButtonText (tabNames[i]);
        tabBtns[i].setClickingTogglesState (false);
        tabBtns[i].onClick = [this, i] { showTab (i); };
        addAndMakeVisible (tabBtns[i]);
    }
    for (auto* pn : { &voicePanel, &modPanel, &fxPanel, &perfPanel, &labPanel, &helpPanel }) addChildComponent (*pn);
    linkPanel = std::make_unique<LinkPanel> (*proc.link, proc.apvts,
        "Alea publie ses coups a l'avance : Talea s'en sert pour le sidechain, son modulateur Drums et ses lignes Follow / Avoid kick. "
        "Key lock : les voix tonales jouent dans la tonalite partagee.");
    addChildComponent (*linkPanel);
    attachCombo (keyLockBox, linkPanel->extraArea, "keylock", { "Key lock: Off", "Key lock: Melodic", "Key lock: All" });
    keyLockBox.setTooltip ("Melodic : FM, Modal, Pluck suivent la tonique et la gamme | All : aussi Kick, Snare, 909");
    keyChordBtn.setClickingTogglesState (true);
    linkPanel->extraArea.addAndMakeVisible (keyChordBtn);
    buttonAtts.push_back (std::make_unique<BA> (*proc.apvts.getParameter ("keychord"), keyChordBtn));
    keyChordBtn.setTooltip ("Avec Key lock actif : les voix tonales jouent les notes de l'accord en cours de Color");

    // ---------------- VOICE
    engineBox.addItemList (engineNames(), 1);
    ftypeBox.addItemList ({ "LP", "BP", "HP" }, 1);
    chokeBox.addItemList ({ "Choke off", "Choke A", "Choke B", "Choke C" }, 1);
    outBox.addItemList ({ "Out: Main", "Out: Separate" }, 1);
    fxTypeBox.addItemList ({ "FX: Off", "Fold", "Comb", "Ring", "Tape" }, 1);
    fxTypeBox.setTooltip ("Effet d'insertion de la voix (dose par le potard FX Amt)");
    voicePanel.addAndMakeVisible (fxTypeBox);
    action (keepBtn, voicePanel, true, [this] { proc.keepLastHit (selected); });
    keepBtn.setTooltip ("Keep : recopie dans les potards les valeurs reellement jouees au dernier coup (alea compris)");
    keepBtn.setColour (juce::TextButton::buttonColourId, col::plock.darker (1.2f));
    outBox.setTooltip ("Separate : la voix sort sur sa propre sortie stereo (Voice n) - a router dans Live");
    for (auto* c : { &engineBox, &ftypeBox, &chokeBox, &outBox }) voicePanel.addAndMakeVisible (*c);
    engLockBtn.setClickingTogglesState (true);
    engLockBtn.setColour (juce::TextButton::buttonOnColourId, col::lock);
    engLockBtn.onClick = [this] { proc.locks[selected][kNumVP] = engLockBtn.getToggleState(); };
    voicePanel.addAndMakeVisible (engLockBtn);
    action (diceBtn,     voicePanel, true, [this] { proc.diceVoice (selected, false); });
    action (diceEngBtn,  voicePanel, true, [this] { proc.diceVoice (selected, true); });
    action (mutVoiceBtn, voicePanel, true, [this] { proc.mutateVoice (selected, 0.08f); });
    action (rndJitBtn,   voicePanel, true, [this] { proc.randomizeJitter (selected); });
    action (clrJitBtn,   voicePanel, true, [this] { proc.clearJitter (selected); });
    action (loadSmpBtn,  voicePanel, false, [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Charger un echantillon", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile() && proc.loadSample (selected, f))
                                  {
                                      proc.setParamReal (tid (selected, "engine"), (float) EngSample);
                                      voicePanel.repaint();
                                  }
                              });
    });
    action (clearSmpBtn, voicePanel, false, [this] { proc.clearSample (selected); });
    loadSmpBtn.setTooltip ("Charge un son (wav/aif/flac/mp3, 10 s max) dans la voix - ou glisser un fichier sur la liste des pistes");
    diceBtn.setColour (juce::TextButton::buttonColourId, col::rnd.darker (0.9f));
    diceEngBtn.setColour (juce::TextButton::buttonColourId, col::rnd.darker (0.9f));

    auto* chaosRaw = ap.getRawParameterValue ("chaos");
    for (int vp = 0; vp < kNumVP; ++vp)
    {
        auto& k = voiceKnobs[vp];
        k.setProcessor (&proc);
        k.setChaosSource (chaosRaw);
        k.setBipolar (vpIsBipolar (vp));
        k.onPlockEdited = [this] { grid.repaint(); };
        voicePanel.addAndMakeVisible (k);
    }
    for (auto* k : { &velSensK, &linkK }) { k->setProcessor (&proc); voicePanel.addAndMakeVisible (*k); }
    voicePanel.addAndMakeVisible (scope);
    voicePanel.painter = [this] (juce::Graphics& g)
    {
        auto b = voicePanel.getLocalBounds();
        g.setColour (col::panel);
        g.fillRoundedRectangle (b.toFloat(), 6.0f);
        g.setColour (col::track (selected));
        g.setFont (font (12.0f, true));
        g.drawText ("VOICE " + juce::String (selected + 1) + "  " + juce::String (defaultKit (selected).name).toUpperCase(),
                    12, 6, 120, 20, juce::Justification::centredLeft);
        if (lockT >= 0)
        {
            g.setColour (col::plock);
            g.drawText ("P-LOCK : pas " + juce::String (lockI + 1) + " (pattern " + patLetter (lockPat) + ")", 12, 26, 260, 16,
                        juce::Justification::centredLeft);
        }
        g.setColour (col::textDim);
        g.setFont (font (11.0f));
        const auto sn = proc.sampleName (selected);
        g.drawFittedText (sn.isEmpty() ? juce::String ("(pas de sample)") : sn, scope.getX(), scope.getBottom() + 6, scope.getWidth(), 16,
                          juce::Justification::centredLeft, 1);
        g.drawFittedText ("Anneau turquoise = amplitude d'alea, point blanc = derniere valeur jouee. "
                          "Rnd Link : les parametres de la voix varient ensemble (alea correle). "
                          "Vel Sens : sensibilite a la velocite. FX Amt dose l'effet d'insertion, Smp Layer superpose l'echantillon charge a la synthese. "
                          "Keep fige le dernier coup joue. En mode p-lock, les potards editent le pas selectionne (double-clic = retirer le verrou).",
                          juce::Rectangle<int> (12, 262, 1270, 40), juce::Justification::topLeft, 3);
    };

    // ---------------- MOD
    for (int m = 0; m < kNumMods; ++m)
    {
        attachCombo (modRateBox[m], modPanel, modId (m, "rate"), modRateNames());
        setupKnob (modPK[m], modPanel, modId (m, "p").toRawUTF8(), modParamNames()[m], false, col::rnd);
        modDisp[m] = std::make_unique<ModDisplay> (proc.modValue[m]);
        modPanel.addAndMakeVisible (*modDisp[m]);
    }
    turLenSlider.setSliderStyle (juce::Slider::IncDecButtons);
    turLenSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 22);
    modPanel.addAndMakeVisible (turLenSlider);
    sliderAtts.push_back (std::make_unique<SA> (*ap.getParameter ("m3len"), turLenSlider));
    turLenSlider.setTextValueSuffix (" bits");
    attachCombo (followTrkBox, modPanel, "m6trk", [] { juce::StringArray a; for (int t = 0; t < kNumTracks; ++t) a.add ("Follow T" + juce::String (t + 1)); return a; }());
    setupKnob (modDepthK, modPanel, "moddepth", "Mod Depth", false, col::rnd);

    {
        juce::StringArray src { "Off" }; src.addArray (modNames());
        juce::StringArray trk { "All" }; for (int t = 0; t < kNumTracks; ++t) trk.add ("T" + juce::String (t + 1));
        juce::StringArray dst; for (int vp = 0; vp < kNumVP; ++vp) dst.add (vpInfo (vp).label);
        dst.addArray (globalTargetNames());
        for (int s = 0; s < kNumMxSlots; ++s)
        {
            attachCombo (mxSrc[s], modPanel, mxId (s, "src"), src);
            attachCombo (mxTrk[s], modPanel, mxId (s, "trk"), trk);
            attachCombo (mxDst[s], modPanel, mxId (s, "dst"), dst);
            mxAmt[s].setSliderStyle (juce::Slider::LinearHorizontal);
            mxAmt[s].setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
            mxAmt[s].setColour (juce::Slider::trackColourId, col::rnd.withAlpha (0.7f));
            modPanel.addAndMakeVisible (mxAmt[s]);
            sliderAtts.push_back (std::make_unique<SA> (*ap.getParameter (mxId (s, "amt")), mxAmt[s]));
            mxAmt[s].setDoubleClickReturnValue (true, 0.0);
        }
    }
    modPanel.painter = [this] (juce::Graphics& g)
    {
        static const char* desc[kNumMods] { "Valeur au hasard a chaque pas",
                                            "Marche aleatoire lissee",
                                            "Boucle qui mute (Change 0 = figee)",
                                            "Logistique : R bas = cycles, haut = chaos",
                                            "Sinus > tri > scie > carre > paliers",
                                            "Suit le volume d'une piste" };
        for (int m = 0; m < kNumMods; ++m)
        {
            auto r = juce::Rectangle<int> (m * 216, 0, 210, 150);
            g.setColour (col::panel);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            g.setColour (col::rnd);
            g.setFont (font (12.0f, true));
            g.drawText ("MOD " + juce::String (m + 1) + "  " + modNames()[m], r.getX() + 10, 4, 200, 20, juce::Justification::centredLeft);
            g.setColour (col::textDim);
            g.setFont (font (10.0f));
            g.drawText (desc[m], r.getX() + 8, 130, 200, 16, juce::Justification::centredLeft);
        }
        auto mr = juce::Rectangle<int> (0, 158, 1296, 176);
        g.setColour (col::panel);
        g.fillRoundedRectangle (mr.toFloat(), 6.0f);
        g.setColour (col::text);
        g.setFont (font (12.0f, true));
        g.drawText ("MATRICE DE MODULATION  (source -> piste -> parametre ; les cibles G: agissent en continu sur les effets et macros)", 10, 162, 900, 18, juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (font (10.0f));
        for (int s = 0; s < kNumMxSlots; ++s)
            g.drawText (juce::String (s + 1), (s / 4) * 600 + 10, 188 + (s % 4) * 36, 20, 26, juce::Justification::centredLeft);
    };

    // ---------------- FX
    attachCombo (dlyDivBox, fxPanel, "dlydiv", delayDivNames());
    attachCombo (stuDivBox, fxPanel, "studiv", { "Slice 1/8", "Slice 1/16", "Slice 1/32", "Slice 1/64", "Slice Rnd" });
    attachCombo (stuLenBox, fxPanel, "stulen", { "1/2 beat", "1 beat", "2 beats", "4 beats" });
    attachToggle (pingBtn, fxPanel, "dlyping");
    setupKnob (dlyFbK, fxPanel, "dlyfb", "Feedback");
    setupKnob (dlyToneK, fxPanel, "dlytone", "Tone");
    setupKnob (dlyRetK, fxPanel, "dlyret", "Return");
    setupKnob (dlyRndK, fxPanel, "dlyrnd", "Time Rnd", false, col::rnd);
    setupKnob (revSizeK, fxPanel, "revsize", "Size");
    setupKnob (revDampK, fxPanel, "revdamp", "Damp");
    setupKnob (revWidthK, fxPanel, "revwidth", "Width");
    setupKnob (revRetK, fxPanel, "revret", "Return");
    setupKnob (stuChanceK, fxPanel, "stuchance", "Chance", false, col::rnd);
    setupKnob (glueK, fxPanel, "glue", "Glue");
    setupKnob (mDriveK, fxPanel, "mdrive", "Drive");
    setupKnob (revPreK, fxPanel, "revpre", "Predelay");
    attachCombo (revTypeBox, fxPanel, "revtype", { "Room", "Plate" });
    attachToggle (hqBtn, fxPanel, "hq");
    hqBtn.setTooltip ("Sur-echantillonnage x2 des voix (moins de repliement sur FM, Zap, Metal, drive) - un peu plus de CPU");
    setupKnob (duckAmtK, fxPanel, "duckamt", "Amount", false, col::rnd);
    setupKnob (duckRelK, fxPanel, "duckrel", "Release");
    attachCombo (duckSrcBox, fxPanel, "ducksrc", [] { juce::StringArray a; for (int t = 0; t < kNumTracks; ++t) a.add ("Duck by T" + juce::String (t + 1)); return a; }());
    fxPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> areas[] { { 0, 0, 400, 150 }, { 410, 0, 380, 150 }, { 800, 0, 250, 150 }, { 1060, 0, 236, 150 }, { 0, 160, 500, 130 } };
        const char* titles[] { "DELAY", "REVERB", "STUTTER", "MASTER", "SIDECHAIN (ducking)" };
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (areas[i].toFloat(), 6.0f);
            g.setColour (i == 2 || i == 4 ? col::rnd : col::textDim);
            g.setFont (font (12.0f, true));
            g.drawText (titles[i], areas[i].getX() + 10, areas[i].getY() + 4, 220, 20, juce::Justification::centredLeft);
        }
        if (proc.stutterOn.load())
        {
            g.setColour (col::rnd);
            g.fillEllipse (880.0f, 9.0f, 10.0f, 10.0f);
        }
        {
            auto m = juce::Rectangle<float> (440.0f, 196.0f, 30.0f, 80.0f);
            g.setColour (col::bg); g.fillRect (m);
            g.setColour (col::rnd);
            g.fillRect (m.withHeight (m.getHeight() * juce::jlimit (0.0f, 1.0f, proc.duckView.load())));
            g.setColour (col::textDim);
            g.setFont (font (10.0f));
            g.drawText ("GR", 440, 278, 30, 12, juce::Justification::centred);
        }
        g.setColour (col::textDim);
        g.setFont (font (11.0f));
        g.drawFittedText ("Time Rnd : a chaque temps, probabilite que le delay saute vers une division au hasard (glissement facon bande).  "
                          "Stutter : a chaque temps, probabilite de declencher un beat-repeat.  "
                          "Les voix en sortie separee ne passent pas par le stutter, la glue et la saturation master.",
                          juce::Rectangle<int> (520, 170, 760, 60), juce::Justification::topLeft, 4);
    };

    // ---------------- PERFORM
    setupKnob (morphK, perfPanel, "morph", "MORPH A>B", false, col::plock);
    morphK.setTooltip ("Fondu entre les reglages actuels (A) et l'instantane B | clic droit : MIDI learn");
    action (storeBBtn, perfPanel, false, [this] { proc.storeB(); perfPanel.repaint(); });
    action (recallBBtn, perfPanel, true, [this] { proc.recallB(); });
    action (swapBBtn, perfPanel, true, [this] { proc.swapB(); });
    storeBBtn.setTooltip ("Memorise les sons actuels comme B. Lance ensuite le de pour creer A, puis joue avec Morph.");
    setupKnob (densityK2, perfPanel, "density", "Density", true, col::rnd);
    setupKnob (chaosK2, perfPanel, "chaos", "Chaos", false, col::rnd);
    setupKnob (seqChaosK2, perfPanel, "seqchaos", "Seq Chaos", false, col::rnd);
    setupKnob (modDepthK2, perfPanel, "moddepth", "Mod Depth", false, col::rnd);
    setupKnob (stuChanceK2, perfPanel, "stuchance", "Stutter", false, col::rnd);
    setupKnob (duckK2, perfPanel, "duckamt", "Duck", false, col::rnd);
    attachToggle (fillLatchBtn, perfPanel, "fill");

    // scenes
    for (int n = 0; n < kNumScenes; ++n)
    {
        auto& b = sceneBtns[n];
        b.setButtonText (juce::String (n + 1));
        b.onClick = [this, n]
        {
            if (sceneStoreBtn.getToggleState() || juce::ModifierKeys::currentModifiers.isShiftDown())
            {
                proc.storeScene (n);
                sceneStoreBtn.setToggleState (false, juce::dontSendNotification);
            }
            else if (proc.sceneValid[n].load()) { proc.pushUndo(); proc.recallScene (n); }
        };
        b.setTooltip ("Scene " + juce::String (n + 1) + " : clic = rappel | Maj-clic (ou Store puis clic) = memoriser | MIDI : note C0+" + juce::String (n));
        perfPanel.addAndMakeVisible (b);
    }
    sceneStoreBtn.setClickingTogglesState (true);
    sceneStoreBtn.setColour (juce::TextButton::buttonOnColourId, col::lock);
    perfPanel.addAndMakeVisible (sceneStoreBtn);
    {
        juce::StringArray sc;
        for (int n = 0; n < kNumScenes; ++n) sc.add ("Scene " + juce::String (n + 1));
        attachCombo (sceneABox, perfPanel, "scenea", sc);
        attachCombo (sceneBBox, perfPanel, "sceneb", sc);
    }
    setupKnob (sceneXfK, perfPanel, "scenexf", "Crossfade", false, col::plock);
    attachToggle (sceneXfOnBtn, perfPanel, "sceneon");
    sceneXfOnBtn.setTooltip ("Crossfader actif : le son est un melange de la scene A et de la scene B (les potards de voix sont ignores)");

    // clavier
    {
        juce::StringArray kt { "Keys: Off" };
        for (int t = 0; t < kNumTracks; ++t) kt.add ("Keys: T" + juce::String (t + 1));
        attachCombo (keysBox, perfPanel, "keystrack", kt);
    }
    keysBox.setTooltip ("Mode clavier : les notes MIDI a partir de C3 jouent cette voix de facon chromatique (legato = slide)");
    setupKnob (glideK, perfPanel, "glide", "Slide time");

    perfPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> areas[] { { 0, 0, 300, 150 }, { 310, 0, 560, 150 }, { 880, 0, 416, 150 },
                                             { 0, 160, 560, 170 }, { 570, 160, 300, 170 }, { 880, 160, 416, 170 } };
        const char* titles[] { "MORPH A / B", "MACROS (clic droit = MIDI learn)", "SCENES", "SONG MODE", "CLAVIER / SLIDE", "MIDI" };
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (areas[i].toFloat(), 6.0f);
            g.setColour (i == 0 || i == 2 ? col::plock : col::textDim);
            g.setFont (font (12.0f, true));
            g.drawText (titles[i], areas[i].getX() + 10, areas[i].getY() + 4, 400, 20, juce::Justification::centredLeft);
        }
        g.setFont (font (11.0f));
        g.setColour (proc.hasB.load() ? col::plock : col::textDim);
        g.drawText (proc.hasB.load() ? "B memorise" : "B vide : Store B d'abord", 120, 116, 170, 20, juce::Justification::centredLeft);

        // chaine du song mode
        const auto chain = juce::StringArray::fromTokens (proc.getChainText().toUpperCase(), " ,;-", "");
        int x = 12; const int y = 190;
        const int pos = proc.songPos.load();
        int idx = 0;
        for (auto& tok : chain)
        {
            if (tok.isEmpty()) continue;
            auto r = juce::Rectangle<int> (x, y + (idx / 11) * 32, 44, 26);
            g.setColour (idx == pos ? col::accent : col::panelHi);
            g.fillRoundedRectangle (r.toFloat(), 4.0f);
            g.setColour (idx == pos ? juce::Colours::black : col::text);
            g.drawText (tok, r, juce::Justification::centred);
            x += 48; if ((idx + 1) % 11 == 0) x = 12;
            ++idx;
        }
        g.setColour (col::textDim);
        g.drawFittedText ("Bouton Song + chaine dans la barre du haut (ex. A4 B4 A2 C2 : lettre = pattern, chiffre = mesures). La chaine boucle.",
                          juce::Rectangle<int> (12, 286, 540, 36), juce::Justification::topLeft, 2);
        g.drawFittedText ("Choisis une voix (Pluck, Modal, FM, Zap...) : les notes a partir de C3 la jouent chromatiquement. "
                          "Jeu legato = glissement de hauteur (slide). Le Slide time vaut aussi pour les pas marques Slide.",
                          juce::Rectangle<int> (582, 270, 280, 56), juce::Justification::topLeft, 4);
        g.drawFittedText ("C0-G0 : scenes 1-8\nC1-G1 : voix 1-8 (vel >= 120 = accent)\nC2-G2 : patterns A-H\nC3 et + : clavier\nCC : MIDI learn (clic droit sur un potard)\n"
                          "Sortie : notes 36-43 (bouton MIDI Out)",
                          juce::Rectangle<int> (892, 188, 400, 130), juce::Justification::topLeft, 8);
    };

    // ---------------- AIDE
    helpPanel.painter = [this] (juce::Graphics& g)
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (helpPanel.getLocalBounds().toFloat(), 6.0f);
        g.setColour (col::text);
        g.setFont (font (12.0f));
        g.drawFittedText (
            "POTARDS : glisser = valeur | clic droit-glisser ou Cmd-glisser (ou RND EDIT) = amplitude d'alea | Maj = fin | double-clic = defaut | "
            "clic droit sans glisser = menu (MIDI learn) | cadenas = exclu du de\n"
            "GRILLE : clic = pas | Maj-clic = accent | Cmd-clic = p-locks du pas (les potards de la voix editent le pas, jaune) | "
            "clic droit = menu (condition, proba, ratchet) | Alt-clic = longueur de piste\n"
            "COULOIRS : Probability, Ratchet, Nudge, Tune, Condition (1:2 = 1re de 2 boucles, Fill, First...) | double-clic = reset\n"
            "PISTES : triangle = ecouter | M = mute | de = de de la voix (Maj : avec le moteur) | glisser un fichier audio sur une piste = moteur Sample\n"
            "ALEA : Chaos (global), Mode (Hit/Bar/Drift), Dist (forme du hasard), Scale (accordage aleatoire sur une gamme), Seed (reproductible), "
            "Rnd Link (correlation par voix), modulateurs MOD, Density / Seq Chaos / Ratch Chaos / Humanize, delay et stutter aleatoires\n"
            "LINK : Alea et Talea (basse) se trouvent tout seuls et partagent tempo, lecture, tonique, gamme, pattern, fill, swing, scenes... "
            "Key lock = voix tonales dans la tonalite partagee\n"
            "GENERATION : Dice, Dice + Engine, Mutate, Rnd amounts, Euclid, Random, Vary (Markov), Rnd All, Vary All | Undo/Redo pour revenir en arriere\n"
            "SORTIES : Out Separate = sortie stereo dediee (Voice 1-8) | MIDI Out = notes 36-43 | Drag MIDI = clip de 4 mesures\n"
            "MIDI IN : C0-G0 = scenes, C1-G1 = voix 1-8 (velocite >= 120 = accent), C2-G2 = patterns A-H, C3+ = clavier, CC = MIDI learn\n"
            "LAB : Keep (fige le dernier coup), favoris + Breed, evolution automatique (Commit / Reset), croisement de patterns, style appris d'un .mid\n"
            "PERFORM : morph A/B, scenes (Maj-clic = memoriser) + crossfader, macros, song, clavier + slide | FX : sidechain, plate, HQ x2\n"
            "GRILLE (apercu) : point turquoise = coup fantome a venir, trait rouge = pas qui sautera, pointilles = ajoute par l'evolution, barre = retire",
            helpPanel.getLocalBounds().reduced (16, 12), juce::Justification::topLeft, 30);
    };

    setupLab();
    selectTrack (0);
    showTab (0);
    startTimerHz (30);
}

MainView::~MainView()
{
    stopTimer();
    engineAtt.reset(); ftypeAtt.reset(); chokeAtt.reset(); outAtt.reset(); fxTypeAtt.reset();
    sliderAtts.clear(); comboAtts.clear(); buttonAtts.clear();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void MainView::setupKnob (RndKnob& k, juce::Component& parent, const char* paramId, const juce::String& label, bool bipolar, juce::Colour c)
{
    k.setProcessor (&proc);
    k.bind (proc.apvts.getParameter (paramId), nullptr, nullptr, nullptr, label);
    k.setBipolar (bipolar);
    k.setAccentColour (c);
    parent.addAndMakeVisible (k);
}

void MainView::action (juce::TextButton& b, juce::Component& parent, bool undoable, std::function<void()> fn)
{
    b.onClick = [this, fn, undoable]
    {
        if (undoable) proc.pushUndo();
        fn();
        grid.repaint(); lanes.repaint(); trackList.repaint();
    };
    parent.addAndMakeVisible (b);
}

void MainView::attachCombo (juce::ComboBox& c, juce::Component& parent, const juce::String& paramId, const juce::StringArray& items)
{
    c.addItemList (items, 1);
    parent.addAndMakeVisible (c);
    comboAtts.push_back (std::make_unique<CA> (*proc.apvts.getParameter (paramId), c));
}

void MainView::attachToggle (juce::Button& b, juce::Component& parent, const juce::String& paramId)
{
    b.setClickingTogglesState (true);
    parent.addAndMakeVisible (b);
    buttonAtts.push_back (std::make_unique<BA> (*proc.apvts.getParameter (paramId), b));
}

TrackPat& MainView::editTrack() { return proc.bank.pats[proc.editPattern()].tracks[selected]; }

void MainView::showTab (int i)
{
    currentTab = i;
    juce::Component* panels[] { &voicePanel, &modPanel, &fxPanel, &perfPanel, &labPanel, linkPanel.get(), &helpPanel };
    for (int k = 0; k < kNumTabs; ++k)
    {
        panels[k]->setVisible (k == i);
        tabBtns[k].setColour (juce::TextButton::buttonColourId, k == i ? col::panelHi.brighter (0.25f) : col::panel);
        tabBtns[k].setColour (juce::TextButton::textColourOffId, k == i ? col::accent : col::textDim);
    }
}

void MainView::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto fac = AleaAudioProcessor::factoryPresetNames();
    presetBox.addSectionHeading ("Usine");
    for (int i = 0; i < fac.size(); ++i) presetBox.addItem (fac[i], i + 1);
    userPresets = AleaAudioProcessor::userPresetDir().findChildFiles (juce::File::findFiles, false, "*.alea");
    userPresets.sort();
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("Utilisateur");
        for (int i = 0; i < userPresets.size(); ++i) presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 100 + i);
    }
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
}

void MainView::stepPreset (int delta)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i) ids.add (presetBox.getItemId (i));
    if (ids.isEmpty()) return;
    int cur = ids.indexOf (presetBox.getSelectedId());
    cur = cur < 0 ? 0 : (cur + delta + ids.size()) % ids.size();
    presetBox.setSelectedId (ids[cur], juce::sendNotificationSync);
}

void MainView::selectTrack (int t)
{
    selected = juce::jlimit (0, kNumTracks - 1, t);
    if (lockT >= 0 && lockT != selected) setLockStep (-1, -1);
    trackList.selected = grid.selected = lanes.track = selected;
    proc.scopeTrack = selected;

    auto& ap = proc.apvts;
    engineAtt.reset(); ftypeAtt.reset(); chokeAtt.reset(); outAtt.reset(); fxTypeAtt.reset();
    fxTypeAtt = std::make_unique<CA> (*ap.getParameter (tid (selected, "fxtype")), fxTypeBox);
    engineAtt = std::make_unique<CA> (*ap.getParameter (tid (selected, "engine")), engineBox);
    ftypeAtt  = std::make_unique<CA> (*ap.getParameter (tid (selected, "ftype")), ftypeBox);
    chokeAtt  = std::make_unique<CA> (*ap.getParameter (tid (selected, "choke")), chokeBox);
    outAtt    = std::make_unique<CA> (*ap.getParameter (tid (selected, "out")), outBox);
    engLockBtn.setToggleState (proc.locks[selected][kNumVP].load(), juce::dontSendNotification);

    for (int vp = 0; vp < kNumVP; ++vp)
    {
        voiceKnobs[vp].bind (ap.getParameter (vpId (selected, vp)), ap.getParameter (jitId (selected, vp)),
                             &proc.locks[selected][vp], &proc.lastNorm[selected][vp], vpInfo (vp).label);
        voiceKnobs[vp].setAccentColour (col::track (selected));
    }
    velSensK.bind (ap.getParameter (tid (selected, "velsens")), nullptr, nullptr, nullptr, "Vel Sens");
    linkK.bind (ap.getParameter (tid (selected, "link")), nullptr, nullptr, nullptr, "Rnd Link");
    linkK.setAccentColour (col::rnd);
    refreshTrackTools();
    refreshLab();
    repaint();
}

void MainView::setLockStep (int t, int i)
{
    lockT = t; lockI = i; lockPat = proc.editPattern();
    grid.lockTrack = t; grid.lockStep = i;
    if (t >= 0 && t != selected) selectTrack (t);
    for (int vp = 0; vp < kNumVP; ++vp)
        voiceKnobs[vp].setPlockTarget (t >= 0 ? &proc.bank.pats[lockPat].tracks[t].steps[i].plock[vp] : nullptr);
    const bool on = t >= 0;
    lockCondBox.setVisible (on); clearLocksBtn.setVisible (on); exitLockBtn.setVisible (on);
    if (on)
    {
        lockCondBox.setSelectedId (proc.bank.pats[lockPat].tracks[t].steps[i].cond + 1, juce::dontSendNotification);
        if (currentTab != 0) showTab (0);
    }
    grid.repaint(); voicePanel.repaint(); repaint (lockArea);
}

void MainView::refreshTrackTools()
{
    auto& tp = editTrack();
    lenSlider.setValue (tp.length.load(), juce::dontSendNotification);
    rateBox.setSelectedId (tp.rate.load() + 1, juce::dontSendNotification);
    dirBox.setSelectedId (tp.dir.load() + 1, juce::dontSendNotification);
    grooveBox.setSelectedId (tp.groove.load() + 1, juce::dontSendNotification);
    grid.repaint(); lanes.repaint();
}

void MainView::refreshAfterStateChange()
{
    setLockStep (-1, -1);
    chainEdit.setText (proc.getChainText(), false);
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);
    selectTrack (selected);
    refreshLab();
    repaint();
}

bool MainView::dropFiles (const juce::StringArray& files, juce::Point<int> pos)
{
    if (files.isEmpty()) return false;
    juce::File f (files[0]);
    if (f.hasFileExtension ("alea")) { if (proc.loadPresetFile (f)) refreshAfterStateChange(); return true; }
    if (f.hasFileExtension ("mid;midi")) { const bool ok = proc.learnStyleFromMidi (f); showTab (4); return ok; }
    int t = selected;
    if (trackList.getBounds().contains (pos))
        t = juce::jlimit (0, kNumTracks - 1, (int) ((pos.y - trackList.getY()) / trackList.rowHeight()));
    if (! proc.loadSample (t, f)) return false;
    proc.setParamReal (tid (t, "engine"), (float) EngSample);
    selectTrack (t);
    return true;
}

// ---------------------------------------------------------------------------
void MainView::timerCallback()
{
    for (auto& f : proc.flash) f.store (f.load() * 0.82f);
    for (auto& k : voiceKnobs) k.refreshLive();
    for (auto* k : { &chaosKnob, &masterKnob, &morphK, &densityK, &densityK2 }) k->refreshLive();

    const int ep = proc.editPattern();
    if (lockT >= 0 && ep != lockPat) setLockStep (-1, -1);
    if (ep != lastEp) { lastEp = ep; refreshTrackTools(); }
    else if (! lenSlider.isMouseButtonDown() && (int) lenSlider.getValue() != editTrack().length.load())
        lenSlider.setValue (editTrack().length.load(), juce::dontSendNotification);

    for (int i = 0; i < kNumPatterns; ++i)
    {
        patBtns[i].setColour (juce::TextButton::buttonColourId, i == ep ? col::accent : col::panelHi);
        patBtns[i].setColour (juce::TextButton::textColourOffId, i == ep ? juce::Colours::black : col::text);
    }
    if (++previewTick % 3 == 0 && grid.showPreview) grid.preview = proc.previewNextBar();
    if (currentTab == 3)
        for (int n = 0; n < kNumScenes; ++n)
        {
            const bool v = proc.sceneValid[n].load();
            const bool cur = proc.lastRecalledScene.load() == n;
            sceneBtns[n].setColour (juce::TextButton::buttonColourId, cur ? col::plock : (v ? col::panelHi.brighter (0.2f) : col::panel));
            sceneBtns[n].setColour (juce::TextButton::textColourOffId, cur ? juce::Colours::black : (v ? col::text : col::textDim));
        }
    if (currentTab == 4 && previewTick % 10 == 0) labPanel.repaint();
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
    grid.repaint();
    trackList.repaint();
    {
        const int peers = proc.link != nullptr ? proc.link->countLinkedPeers() : 0;
        linkBtn.setButtonText (peers > 0 ? "LINK " + juce::String (peers) : juce::String ("LINK"));
        linkBtn.setColour (juce::TextButton::buttonColourId, peers > 0 ? col::rnd.darker (0.6f) : col::panelHi);
    }
    if (currentTab == 0) scope.refresh();
    if (currentTab == 1) for (auto& d : modDisp) d->push();
    if (currentTab == 2 || currentTab == 3) (currentTab == 2 ? fxPanel : perfPanel).repaint();
    repaint (0, 0, W, 80);
}

// ---------------------------------------------------------------------------
void MainView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);

    g.setColour (col::accent);
    g.setFont (font (32.0f, true));
    g.drawText ("ALEA", 16, 6, 120, 38, juce::Justification::centredLeft);
    g.setColour (col::textDim);
    g.setFont (font (10.0f));
    g.drawText ("random percussion synth  v4", 16, 44, 140, 14, juce::Justification::centredLeft);

    // pattern en lecture
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
        g.setColour (pk > 0.95f ? col::lock : col::track (6));
        g.fillRect (m.withTop (m.getBottom() - m.getHeight() * pk));
    }

    g.setColour (col::panel);
    g.fillRoundedRectangle (seqArea.toFloat(), 6.0f);
    g.setColour (col::rnd);
    g.setFont (font (11.0f, true));
    g.drawText ("SEQUENCE", seqArea.getX() + 10, seqArea.getY() + 4, 200, 16, juce::Justification::centredLeft);

    g.setColour (col::panel);
    g.fillRoundedRectangle (lockArea.toFloat(), 6.0f);
    g.setFont (font (11.0f, true));
    if (lockT >= 0)
    {
        g.setColour (col::plock);
        g.drawText ("P-LOCK  piste " + juce::String (lockT + 1) + "  pas " + juce::String (lockI + 1), lockArea.getX() + 10, lockArea.getY() + 4, 260, 16,
                    juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (font (10.0f));
        g.drawFittedText ("Tourne les potards de la voix : la valeur ne vaut que pour ce pas.", lockArea.getX() + 10, lockArea.getY() + 80, 260, 26,
                          juce::Justification::topLeft, 2);
    }
    else
    {
        g.setColour (col::textDim);
        g.drawText ("PAS", lockArea.getX() + 10, lockArea.getY() + 4, 200, 16, juce::Justification::centredLeft);
        g.setFont (font (10.5f));
        g.drawFittedText ("Cmd-clic sur un pas : parameter locks (valeurs propres au pas).\nClic droit : condition, probabilite, ratchet.",
                          lockArea.withTrimmedTop (24).reduced (10, 0), juce::Justification::topLeft, 4);
    }

    if (RndKnob::editRandomMode && currentTab == 0)
    {
        g.setColour (col::rnd.withAlpha (0.6f));
        g.drawRoundedRectangle (voicePanel.getBounds().toFloat().expanded (1.0f), 6.0f, 1.5f);
    }
}

void MainView::resized()
{
    // ligne 1
    presetPrev.setBounds (160, 8, 24, 26);
    presetBox.setBounds (186, 8, 200, 26);
    presetNext.setBounds (388, 8, 24, 26);
    presetSave.setBounds (416, 8, 50, 26);
    undoBtn.setBounds (478, 8, 56, 26);
    redoBtn.setBounds (538, 8, 56, 26);
    midiOutBtn.setBounds (606, 8, 74, 26);
    dragMidi.setBounds (684, 8, 104, 26);
    rndEditBtn.setBounds (800, 8, 76, 26);
    diceAllBtn.setBounds (880, 8, 76, 26);
    seedSlider.setBounds (962, 8, 80, 26);
    rootBox.setBounds (1046, 8, 48, 26);
    scaleBox.setBounds (1096, 8, 90, 26);
    // ligne 2
    playBtn.setBounds (160, 44, 56, 28);
    bpmSlider.setBounds (220, 44, 96, 28);
    for (int i = 0; i < kNumPatterns; ++i) patBtns[i].setBounds (324 + i * 29, 44, 27, 28);
    copyPatBox.setBounds (560, 44, 84, 28);
    songBtn.setBounds (650, 44, 52, 28);
    chainEdit.setBounds (706, 44, 150, 28);
    fillBtn.setBounds (862, 44, 60, 28);
    rndModeBox.setBounds (930, 44, 104, 28);
    distBox.setBounds (1038, 44, 96, 28);
    linkBtn.setBounds (1138, 44, 48, 28);
    chaosKnob.setBounds (1190, 4, 60, 74);
    masterKnob.setBounds (1252, 4, 60, 74);

    // sequenceur
    const int y0 = 86;
    trackList.setBounds (12, y0, 200, 224);
    grid.setBounds (212, y0, 800, 224);
    seqArea = { 1024, y0, 284, 224 };
    const int kx = seqArea.getX() + 6, kw = 90;
    swingK.setBounds    (kx,          y0 + 22, kw, 96);
    accentK.setBounds   (kx + kw,     y0 + 22, kw, 96);
    humanK.setBounds    (kx + 2 * kw, y0 + 22, kw, 96);
    seqChaosK.setBounds (kx,          y0 + 122, kw, 96);
    ratChaosK.setBounds (kx + kw,     y0 + 122, kw, 96);
    densityK.setBounds  (kx + 2 * kw, y0 + 122, kw, 96);

    int x = 12; const int ty = y0 + 232, th = 26;
    auto place = [&] (juce::Component& c, int w) { c.setBounds (x, ty, w, th); x += w + 4; };
    place (lenSlider, 92); place (rateBox, 76); place (dirBox, 96); place (grooveBox, 124); x += 10;
    place (hitsSlider, 100); place (rotSlider, 90); place (euclidBtn, 56); x += 10;
    place (rndPatBtn, 62); place (varyBtn, 50); place (mutPatBtn, 60); place (shlBtn, 24); place (shrBtn, 24);
    place (clearBtn, 48); place (copyBtn, 46); place (pasteBtn, 48); x += 10;
    place (rndAllPatBtn, 64); place (varyAllBtn, 70);

    lanes.setBounds (12, ty + th + 6, 1000, 115);
    lockArea = { 1024, ty + th + 6, 284, 115 };
    lockCondBox.setBounds (lockArea.getX() + 10, lockArea.getY() + 26, 130, 24);
    clearLocksBtn.setBounds (lockArea.getX() + 146, lockArea.getY() + 26, 80, 24);
    exitLockBtn.setBounds (lockArea.getX() + 230, lockArea.getY() + 26, 46, 24);

    // onglets
    const int tabY = lockArea.getBottom() + 8;
    for (int i = 0; i < kNumTabs; ++i) tabBtns[i].setBounds (12 + i * 96, tabY, 92, 24);
    const auto panelBounds = juce::Rectangle<int> (12, tabY + 28, 1296, H - (tabY + 28) - 8);
    for (auto* pn : { &voicePanel, &modPanel, &fxPanel, &perfPanel, &labPanel, &helpPanel }) pn->setBounds (panelBounds);
    linkPanel->setBounds (panelBounds);
    keyLockBox.setBounds (0, 0, 180, 26);
    keyChordBtn.setBounds (0, 32, 240, 24);
    layoutVoice(); layoutMod(); layoutFx(); layoutPerf(); layoutLab();
    previewBtn.setBounds (12 + kNumTabs * 96 + 10, tabY, 80, 24);
}

void MainView::layoutVoice()
{
    int x = 200; const int y = 6;
    auto place = [&] (juce::Component& c, int w) { c.setBounds (x, y, w, 22); x += w + 4; };
    place (engineBox, 100); place (engLockBtn, 40); place (ftypeBox, 60); place (chokeBox, 84); place (outBox, 108); place (fxTypeBox, 80); x += 6;
    place (keepBtn, 48); place (diceBtn, 48); place (diceEngBtn, 96); place (mutVoiceBtn, 58); x += 8;
    place (rndJitBtn, 86); place (clrJitBtn, 66); x += 8;
    place (loadSmpBtn, 88); place (clearSmpBtn, 22);

    const int kw = 76, kh = 88, ky = 40;
    const auto& order = vpDisplayOrder();
    for (int i = 0; i < kNumVP; ++i)
        voiceKnobs[order[(size_t) i]].setBounds (8 + (i % 11) * kw, ky + (i / 11) * (kh + 4), kw, kh);
    velSensK.setBounds (8 + 9 * kw, ky + kh + 4, kw, kh);
    linkK.setBounds (8 + 10 * kw, ky + kh + 4, kw, kh);
    scope.setBounds (8 + 11 * kw + 12, ky, 1296 - (8 + 11 * kw + 12) - 10, 2 * kh + 4);
}

void MainView::layoutMod()
{
    for (int m = 0; m < kNumMods; ++m)
    {
        const int bx = m * 216;
        modRateBox[m].setBounds (bx + 8, 28, 76, 22);
        modPK[m].setBounds (bx + 8, 54, 76, 72);
        modDisp[m]->setBounds (bx + 90, 28, 112, 96);
    }
    turLenSlider.setBounds (2 * 216 + 104, 4, 100, 20);
    followTrkBox.setBounds (5 * 216 + 8, 28, 76, 22);
    modRateBox[5].setVisible (false);
    modDepthK.setBounds (1296 - 84, 160, 76, 74);
    for (int s = 0; s < kNumMxSlots; ++s)
    {
        const int bx = (s / 4) * 600 + 30, by = 186 + (s % 4) * 36;
        mxSrc[s].setBounds (bx, by, 100, 26);
        mxTrk[s].setBounds (bx + 104, by, 64, 26);
        mxDst[s].setBounds (bx + 172, by, 96, 26);
        mxAmt[s].setBounds (bx + 274, by, 286, 26);
    }
}

void MainView::layoutFx()
{
    const int ky = 30;
    dlyDivBox.setBounds (80, 4, 80, 20);
    pingBtn.setBounds (168, 4, 84, 20);
    int x = 10;
    for (auto* k : { &dlyFbK, &dlyToneK, &dlyRetK, &dlyRndK }) { k->setBounds (x, ky, 90, 110); x += 96; }
    revTypeBox.setBounds (490, 4, 80, 20);
    x = 414;
    for (auto* k : { &revSizeK, &revDampK, &revWidthK, &revRetK, &revPreK }) { k->setBounds (x, ky, 74, 110); x += 74; }
    stuChanceK.setBounds (806, ky, 80, 110);
    stuDivBox.setBounds (892, ky + 14, 150, 26);
    stuLenBox.setBounds (892, ky + 52, 150, 26);
    glueK.setBounds (1064, ky, 76, 110);
    mDriveK.setBounds (1140, ky, 76, 110);
    hqBtn.setBounds (1222, ky + 40, 66, 26);
    duckSrcBox.setBounds (10, 196, 130, 24);
    duckAmtK.setBounds (150, 182, 90, 104);
    duckRelK.setBounds (250, 182, 90, 104);
}

void MainView::layoutPerf()
{
    morphK.setBounds (10, 26, 100, 116);
    storeBBtn.setBounds (120, 34, 80, 26);
    recallBBtn.setBounds (206, 34, 84, 26);
    swapBBtn.setBounds (120, 68, 170, 26);
    int x = 316;
    for (auto* k : { &densityK2, &chaosK2, &seqChaosK2, &modDepthK2, &stuChanceK2, &duckK2 }) { k->setBounds (x, 26, 76, 110); x += 76; }
    fillLatchBtn.setBounds (x + 2, 60, 86, 36);
    for (int n = 0; n < kNumScenes; ++n) sceneBtns[n].setBounds (890 + (n % 4) * 40, 30 + (n / 4) * 34, 36, 28);
    sceneStoreBtn.setBounds (890, 100, 76, 24);
    sceneXfOnBtn.setBounds (972, 100, 76, 24);
    sceneABox.setBounds (1056, 30, 110, 24);
    sceneBBox.setBounds (1056, 62, 110, 24);
    sceneXfK.setBounds (1176, 24, 110, 116);
    keysBox.setBounds (582, 190, 130, 26);
    glideK.setBounds (730, 182, 90, 86);
}

void MainView::setupLab()
{
    for (int f = 0; f < kNumFavs; ++f)
    {
        auto& b = favBtns[f];
        b.setButtonText (juce::String (f + 1));
        b.onClick = [this, f]
        {
            const bool store = favStoreBtn.getToggleState() || juce::ModifierKeys::currentModifiers.isShiftDown()
                               || ! proc.favs[selected][f].valid;
            if (store) { proc.storeFav (selected, f); favStoreBtn.setToggleState (false, juce::dontSendNotification); }
            else       { proc.pushUndo(); proc.recallFav (selected, f); }
            refreshLab();
        };
        b.setTooltip ("Favori " + juce::String (f + 1) + " de la voix : case vide = memoriser, case pleine = rappel + ecoute, Maj-clic = ecraser");
        labPanel.addAndMakeVisible (b);
    }
    favStoreBtn.setClickingTogglesState (true);
    favStoreBtn.setColour (juce::TextButton::buttonOnColourId, col::lock);
    labPanel.addAndMakeVisible (favStoreBtn);
    action (keepBtn2, labPanel, true, [this] { proc.keepLastHit (selected); });
    keepBtn2.setColour (juce::TextButton::buttonColourId, col::plock.darker (1.2f));
    for (auto* c : { &breedA, &breedB })
    {
        for (int f = 0; f < kNumFavs; ++f) c->addItem ("Fav " + juce::String (f + 1), f + 1);
        labPanel.addAndMakeVisible (*c);
    }
    breedA.setSelectedId (1, juce::dontSendNotification);
    breedB.setSelectedId (2, juce::dontSendNotification);
    action (breedVoiceBtn, labPanel, true, [this] { proc.breedVoice (selected, breedA.getSelectedId() - 1, breedB.getSelectedId() - 1); });
    breedVoiceBtn.setTooltip ("Croise deux favoris : chaque parametre vient d'un parent, tire vers l'autre, avec une petite mutation");

    setupKnob (evoK, labPanel, "evoamt", "Evolve", false, col::rnd);
    attachCombo (evoBarsBox, labPanel, "evobars", { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" });
    attachCombo (evoTargetBox, labPanel, "evotarget", { "Evolve: Pattern", "Evolve: Sound", "Evolve: Both" });
    action (evoCommitBtn, labPanel, true, [this] { proc.commitEvolution(); });
    action (evoResetBtn, labPanel, true, [this] { proc.resetEvolution(); });
    evoCommitBtn.setTooltip ("Ecrit l'evolution dans les patterns et les potards (annulable)");
    evoResetBtn.setTooltip ("Revient au pattern et aux sons d'origine");

    for (auto* c : { &patA, &patB, &patDst })
    {
        for (int p = 0; p < kNumPatterns; ++p) c->addItem (patLetter (p), p + 1);
        labPanel.addAndMakeVisible (*c);
    }
    patA.setSelectedId (1, juce::dontSendNotification);
    patB.setSelectedId (2, juce::dontSendNotification);
    patDst.setSelectedId (3, juce::dontSendNotification);
    action (breedPatBtn, labPanel, true, [this] { proc.breedPatterns (patA.getSelectedId() - 1, patB.getSelectedId() - 1, patDst.getSelectedId() - 1); });

    action (learnMidiBtn, labPanel, false, [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Apprendre un style depuis un fichier MIDI", juce::File(), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.learnStyleFromMidi (f);
                                  labPanel.repaint();
                              });
    });
    action (styleTrackBtn, labPanel, true, [this] { proc.generateFromStyle (proc.editPattern(), selected); refreshTrackTools(); });
    action (styleAllBtn, labPanel, true, [this] { for (int t = 0; t < kNumTracks; ++t) proc.generateFromStyle (proc.editPattern(), t); refreshTrackTools(); });

    labPanel.painter = [this] (juce::Graphics& g)
    {
        const juce::Rectangle<int> areas[] { { 0, 0, 640, 150 }, { 650, 0, 646, 150 }, { 0, 160, 640, 170 }, { 650, 160, 646, 170 } };
        const char* titles[] { "FAVORIS & CROISEMENT DE SONS (voix selectionnee)", "EVOLUTION AUTOMATIQUE", "CROISEMENT DE PATTERNS", "STYLE APPRIS (fichier MIDI)" };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (areas[i].toFloat(), 6.0f);
            g.setColour (col::rnd);
            g.setFont (font (12.0f, true));
            g.drawText (titles[i], areas[i].getX() + 10, areas[i].getY() + 4, 600, 20, juce::Justification::centredLeft);
        }
        g.setColour (col::textDim);
        g.setFont (font (11.0f));
        g.drawFittedText ("Keep fige le dernier coup joue. Une case vide memorise le son, une case pleine le rappelle et le fait entendre. "
                          "Breed croise deux favoris pour inventer un descendant.",
                          juce::Rectangle<int> (12, 112, 616, 34), juce::Justification::topLeft, 2);
        g.drawFittedText ("Toutes les N mesures, des pas basculent (contour pointille = ajoute, barre = retire) et les sons derivent. "
                          "Non destructif : Commit ecrit le resultat, Reset revient a l'origine. Evolve est aussi une macro (Push, MIDI).",
                          juce::Rectangle<int> (662, 104, 620, 40), juce::Justification::topLeft, 3);
        g.setColour (col::text);
        g.drawText ("Changements en cours : " + juce::String (proc.evolutionAmount()), 1010, 36, 260, 20, juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.drawText ("Parent 1", 12, 190, 60, 24, juce::Justification::centredLeft);
        g.drawText ("Parent 2", 132, 190, 60, 24, juce::Justification::centredLeft);
        g.drawText ("Destination", 252, 190, 80, 24, juce::Justification::centredLeft);
        g.drawFittedText ("Chaque piste prend le debut d'un parent et la fin de l'autre (point de coupure au hasard), avec 4 % de mutation.",
                          juce::Rectangle<int> (12, 260, 616, 40), juce::Justification::topLeft, 2);
        g.setColour (proc.style.valid ? col::text : col::textDim);
        g.drawText (proc.style.valid ? "Style : " + proc.style.name + " (" + juce::String (proc.style.bars) + " mesures)" : juce::String ("Aucun style appris"),
                    662, 228, 620, 20, juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.drawFittedText ("Charge (ou glisse sur le plugin) un fichier .mid de batterie : Alea apprend ou tombent les coups de chaque instrument "
                          "(mapping General MIDI) et genere des patterns de 16 pas dans ce style.",
                          juce::Rectangle<int> (662, 256, 620, 60), juce::Justification::topLeft, 3);
    };
}

void MainView::layoutLab()
{
    for (int f = 0; f < kNumFavs; ++f) favBtns[f].setBounds (12 + f * 44, 32, 40, 32);
    favStoreBtn.setBounds (12 + kNumFavs * 44 + 6, 32, 60, 32);
    keepBtn2.setBounds (12, 72, 110, 30);
    breedA.setBounds (300, 72, 90, 30);
    breedB.setBounds (396, 72, 90, 30);
    breedVoiceBtn.setBounds (492, 72, 80, 30);
    evoK.setBounds (660, 24, 90, 80);
    evoBarsBox.setBounds (760, 34, 100, 26);
    evoTargetBox.setBounds (866, 34, 136, 26);
    evoCommitBtn.setBounds (760, 68, 100, 26);
    evoResetBtn.setBounds (866, 68, 100, 26);
    patA.setBounds (12, 214, 110, 28);
    patB.setBounds (132, 214, 110, 28);
    patDst.setBounds (252, 214, 110, 28);
    breedPatBtn.setBounds (372, 214, 90, 28);
    learnMidiBtn.setBounds (662, 190, 160, 30);
    styleTrackBtn.setBounds (830, 190, 110, 30);
    styleAllBtn.setBounds (946, 190, 110, 30);
}

void MainView::refreshLab()
{
    for (int f = 0; f < kNumFavs; ++f)
    {
        const bool v = proc.favs[selected][f].valid;
        favBtns[f].setColour (juce::TextButton::buttonColourId, v ? col::track (selected).darker (0.4f) : col::panelHi);
        favBtns[f].setColour (juce::TextButton::textColourOffId, v ? juce::Colours::white : col::textDim);
    }
    labPanel.repaint();
}

// ===========================================================================
AleaAudioProcessorEditor::AleaAudioProcessorEditor (AleaAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p)
{
    addAndMakeVisible (view);
    const float s = juce::jlimit (0.55f, 1.6f, proc.uiScale.load());
    setSize ((int) (MainView::W * s), (int) (MainView::H * s));
    setResizable (true, true);
    setResizeLimits ((int) (MainView::W * 0.55), (int) (MainView::H * 0.55), (int) (MainView::W * 1.6), (int) (MainView::H * 1.6));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) MainView::W / (double) MainView::H);
    setSize ((int) (MainView::W * s), (int) (MainView::H * s));
}

void AleaAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) MainView::W;
    view.setBounds (0, 0, MainView::W, MainView::H);
    view.setTransform (juce::AffineTransform::scale (s));
    proc.uiScale = s;
}

void AleaAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const float s = (float) getWidth() / (float) MainView::W;
    view.dropFiles (files, { (int) (x / s), (int) (y / s) });
}
