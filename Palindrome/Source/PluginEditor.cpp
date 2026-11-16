#include "PluginEditor.h"

using namespace pal;

static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

//==============================================================================
void TabBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, 9.0f);

    const float w = r.getWidth() / (float) std::max<size_t> (1, tabs.size());
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        auto t = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3.0f);
        const bool cur = (int) i == current;
        if (cur)
        {
            g.setColour (tabs[i].accent.withAlpha (0.16f));
            g.fillRoundedRectangle (t, 7.0f);
            g.setColour (tabs[i].accent);
            g.fillRoundedRectangle (t.withTop (t.getBottom() - 3.0f).reduced (14.0f, 0), 1.5f);
        }
        else if ((int) i == hover)
        {
            g.setColour (theme::panel2);
            g.fillRoundedRectangle (t, 7.0f);
        }

        g.setFont (theme::font (13.0f, cur));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tabs[i].name);
        const float x = t.getCentreX() - (tw + 14.0f) * 0.5f;
        g.setColour (tabs[i].accent.withAlpha (cur ? 1.0f : 0.6f));
        g.fillEllipse (x, t.getCentreY() - 3.5f, 7.0f, 7.0f);
        g.setColour (cur ? theme::text : theme::dim);
        g.drawText (tabs[i].name, juce::Rectangle<float> (x + 14.0f, t.getY(), tw + 4.0f, t.getHeight()).toNearestInt(), juce::Justification::centredLeft);
    }
}

void TabBar::mouseDown (const juce::MouseEvent& e)
{
    const int i = tabAt (e.position.x);
    if (i >= 0 && onChange) onChange (i);
}

//==============================================================================
void ABSwitch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme::panel2);
    g.fillRoundedRectangle (r, 9.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (r, 9.0f, 1.2f);
    const int cur = processor.getCurrentAB();
    for (int k = 0; k < 2; ++k)
    {
        auto c = r.withWidth (r.getWidth() * 0.5f).withX (r.getX() + r.getWidth() * 0.5f * (float) k).reduced (3.0f);
        if (k == cur)
        {
            g.setColour (theme::text.withAlpha (0.9f));
            g.fillRoundedRectangle (c, 7.0f);
        }
        g.setColour (k == cur ? theme::bg : theme::dim);
        g.setFont (theme::font (13.0f, true));
        g.drawText (k == 0 ? "A" : "B", c.toNearestInt(), juce::Justification::centred);
    }
}

void ABSwitch::mouseDown (const juce::MouseEvent& e)
{
    const int k = e.position.x < (float) getWidth() * 0.5f ? 0 : 1;
    if (k != processor.getCurrentAB()) processor.selectAB (k);
    repaint();
}

//==============================================================================
LearnOverlay::LearnOverlay (PalindromeUI& u) : ui (u)
{
    setWantsKeyboardFocus (true);
    forgetAll.onClick = [this] { ui.processor.forgetAllMidiMappings(); repaint(); };
    done.onClick = [this] { ui.setLearnMode (false); };
    addAndMakeVisible (forgetAll);
    addAndMakeVisible (done);
}

bool LearnOverlay::targetVisible (juce::Component* c) const
{
    if (c == nullptr) return false;
    for (auto* p = c; p != nullptr && p != &ui; p = p->getParentComponent())
        if (! p->isVisible()) return false;
    return true;
}

juce::Rectangle<int> LearnOverlay::banner() const
{
    return getLocalBounds().withSizeKeepingCentre (760, 54).withY (78);
}

void LearnOverlay::resized()
{
    auto b = banner().reduced (12, 12);
    done.setBounds (b.removeFromRight (96));
    b.removeFromRight (8);
    forgetAll.setBounds (b.removeFromRight (110));
}

void LearnOverlay::open()
{
    lastLearnVersion = ui.processor.learnVersion.load();
    setVisible (true);
    toFront (true);
    grabKeyboardFocus();
}

void LearnOverlay::close()
{
    ui.processor.cancelMidiLearn();
    setVisible (false);
}

void LearnOverlay::paint (juce::Graphics& g)
{
    // voile sombre, percé à l'endroit de chaque réglage assignable
    juce::Path veil;
    veil.addRectangle (getLocalBounds().toFloat());
    veil.setUsingNonZeroWinding (false);

    std::vector<std::pair<juce::Rectangle<float>, juce::String>> boxes;
    for (auto& t : ui.learnTargets)
    {
        if (! targetVisible (t.comp)) continue;
        auto rb = getLocalArea (t.comp, t.comp->getLocalBounds()).toFloat().expanded (3.0f);
        boxes.push_back ({ rb, t.id });
        veil.addRoundedRectangle (rb, 6.0f);
    }
    g.setColour (theme::bg.withAlpha (0.72f));
    g.fillPath (veil);

    const auto learning = ui.processor.getLearningTarget();
    const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.008f);

    for (auto& [rb, id] : boxes)
    {
        const auto mapping = ui.processor.getMidiMappingText (id);
        const bool sel = id == learning;
        const bool hov = id == hoverTarget;
        g.setColour (theme::midi.withAlpha (sel ? 0.25f + 0.2f * pulse : hov ? 0.16f : 0.07f));
        g.fillRoundedRectangle (rb, 6.0f);
        g.setColour (theme::midi.withAlpha (sel ? 1.0f : mapping.isNotEmpty() ? 0.9f : 0.45f));
        g.drawRoundedRectangle (rb, 6.0f, sel ? 2.5f : 1.2f);

        const auto text = sel ? juce::String (u8 ("bouge ta pédale…")) : mapping;
        if (text.isNotEmpty())
        {
            g.setFont (theme::font (10.5f, true));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12.0f;
            auto pill = juce::Rectangle<float> (rb.getCentreX() - tw * 0.5f, rb.getBottom() - 9.0f, tw, 16.0f);
            g.setColour (sel ? theme::midi : theme::midi.withAlpha (0.9f));
            g.fillRoundedRectangle (pill, 8.0f);
            g.setColour (theme::bg);
            g.drawText (text, pill.toNearestInt(), juce::Justification::centred);
        }
    }

    // bandeau d'explication
    auto bn = banner().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (bn, 10.0f);
    g.setColour (theme::midi);
    g.drawRoundedRectangle (bn.reduced (0.5f), 10.0f, 1.5f);
    auto t = bn.reduced (16.0f, 8.0f).withTrimmedRight (230.0f);
    g.setFont (theme::font (13.0f, true));
    g.drawText ("MIDI LEARN", t.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (theme::dim);
    g.setFont (theme::font (11.5f));
    g.drawText (u8 ("1. clique un réglage   2. bouge ta pédale ou joue une note   ·   clic droit : oublier   ·   Échap : terminer"),
                t.toNearestInt(), juce::Justification::centredLeft);
}

void LearnOverlay::mouseMove (const juce::MouseEvent& e)
{
    juce::String h;
    for (auto& t : ui.learnTargets)
        if (targetVisible (t.comp)
            && getLocalArea (t.comp, t.comp->getLocalBounds()).expanded (3).contains (e.getPosition()))
            h = t.id;
    if (h != hoverTarget) { hoverTarget = h; repaint(); }
}

void LearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    for (auto& t : ui.learnTargets)
    {
        if (! targetVisible (t.comp)) continue;
        if (! getLocalArea (t.comp, t.comp->getLocalBounds()).expanded (3).contains (e.getPosition())) continue;

        if (e.mods.isPopupMenu()) ui.processor.forgetMidiMapping (t.id);
        else                      ui.processor.startMidiLearn (t.id);
        repaint();
        return;
    }

    // les onglets restent utilisables pour atteindre les autres réglages
    auto local = e.getEventRelativeTo (&ui);
    if (auto* c = ui.getComponentAt (local.getPosition()))
        if (dynamic_cast<TabBar*> (c) != nullptr)
            c->mouseDown (local.getEventRelativeTo (c));
}

bool LearnOverlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { ui.setLearnMode (false); return true; }
    return false;
}

//==============================================================================
PalindromeEditor::PalindromeEditor (PalindromeProcessor& p)
    : AudioProcessorEditor (p), proc (p), ui (p)
{
    addAndMakeVisible (ui);
    setResizable (true, true);
    const float minScale = 0.55f, maxScale = 2.2f;
    setResizeLimits ((int) (PalindromeUI::baseWidth * minScale), (int) (PalindromeUI::baseHeight * minScale),
                     (int) (PalindromeUI::baseWidth * maxScale), (int) (PalindromeUI::baseHeight * maxScale * 1.4f));

    ui.onSizeChosen = [this] (float scale)
    {
        // garde les proportions actuelles de la fenêtre
        const float aspect = (float) getHeight() / (float) juce::jmax (1, getWidth());
        const int w = juce::roundToInt (PalindromeUI::baseWidth * scale);
        setSize (w, juce::jmax ((int) (PalindromeUI::baseHeight * scale), juce::roundToInt ((float) w * aspect)));
    };

    const auto sz = proc.getUiSize();
    setSize (juce::jlimit (getConstrainer()->getMinimumWidth(), getConstrainer()->getMaximumWidth(), sz.x),
             juce::jlimit (getConstrainer()->getMinimumHeight(), getConstrainer()->getMaximumHeight(), sz.y));
    ready = true;
}

void PalindromeEditor::resized()
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float scale = juce::jmin (w / (float) PalindromeUI::baseWidth, h / (float) PalindromeUI::baseHeight);
    const int lw = juce::roundToInt (w / scale), lh = juce::roundToInt (h / scale);
    ui.setTransform ({});
    ui.setBounds (0, 0, lw, lh);
    ui.setTransform (juce::AffineTransform::scale (scale));
    if (ready) proc.setUiSize ({ getWidth(), getHeight() });
}

//==============================================================================
PalindromeUI::PalindromeUI (PalindromeProcessor& p)
    : processor (p), abSwitch (p),
      activity (p), phrase (p), bank (p),
      planView (p), tempoView (p), notesView (p), directionView (p), rhythmView (p), analysisView (p), keyboardView (p), transposeView (p), echoView (p),
      stereoView (p), diffusionView (p), duckView (p), envelopeView (p), textureView (p), outputView (p), overlay (*this)
{
    setLookAndFeel (&lnf);
    tooltips = std::make_unique<juce::TooltipWindow> (this, 600);

    //== en-tête
    captureButton.onClick = [this] { processor.engine.requestCapture(); };
    captureButton.setTooltip (u8 ("Lance une capture maintenant. En mode Banque : recapture l'emplacement choisi (point rouge)."));
    clearButton.onClick = [this] { processor.requestClear(); };
    clearButton.setTooltip (u8 ("Annule toutes les relectures (fondu court) et vide la banque, sauf les captures verrouillées."));
    nextButton.onClick = [this] { processor.engine.requestNext(); };
    nextButton.setTooltip (u8 ("Cycle / Banque : laisse finir la répétition en cours puis enchaîne l'étape suivante, sans silence."));
    holdButton.setClickingTogglesState (true);
    holdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, ParamIDs::hold, holdButton);
    holdButton.setTooltip (u8 ("Gèle la boucle : plus de nouvelle capture, la phrase en cours de relecture tourne indéfiniment."));
    prevPreset.onClick = [this] { stepPreset (-1); };
    nextPreset.onClick = [this] { stepPreset (1); };
    prevPreset.setTooltip (u8 ("Preset précédent"));
    nextPreset.setTooltip (u8 ("Preset suivant"));
    savePresetButton.onClick = [this] { savePresetDialog(); };
    savePresetButton.setTooltip (u8 ("Enregistrer les réglages actuels comme preset"));
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.getProperties().set ("accent", (juce::int64) theme::cycle.getARGB());
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id < 1000) processor.setCurrentProgram (id - 1);
        else if (juce::isPositiveAndBelow (id - 1000, userPresets.size())) processor.loadUserPreset (userPresets[id - 1000]);
    };
    undoButton.onClick = [this] { processor.undo(); };
    redoButton.onClick = [this] { processor.redo(); };
    undoButton.setTooltip ("Annuler le dernier changement de réglage");
    redoButton.setTooltip (u8 ("Rétablir"));
    abSwitch.setTooltip (u8 ("Comparer deux jeux de réglages : A et B. Le premier passage sur B copie A."));
    copyABButton.onClick = [this] { processor.copyToOtherAB(); };
    copyABButton.setTooltip (u8 ("Copier les réglages actuels dans l'autre mémoire (A -> B ou B -> A)"));
    midiButton.setClickingTogglesState (false);
    midiButton.onClick = [this] { setLearnMode (! isLearnMode()); };
    midiButton.setTooltip (u8 ("MIDI learn : assigner les réglages et les boutons à ton pédalier ou ton contrôleur (CC ou notes)."));
    sizeButton.onClick = [this] { showSizeMenu(); };
    sizeButton.setTooltip ("Taille de l'interface (on peut aussi tirer le coin de la fenêtre)");
    tapButton.onClick = [this] { processor.tapTempo(); };
    tapButton.setTooltip (u8 ("Tape le tempo (au moins 2 fois) : passe en horloge interne, le temps 1 tombe sur ton dernier tap."));

    for (auto* c : std::initializer_list<juce::Component*> { &holdButton, &nextButton, &captureButton, &clearButton,
                                                             &prevPreset, &nextPreset, &savePresetButton, &presetBox,
                                                             &undoButton, &redoButton, &abSwitch, &copyABButton, &midiButton, &sizeButton })
        addAndMakeVisible (c);
    addChildComponent (tapButton);
    refreshPresetList();

    addAndMakeVisible (activity);
    addAndMakeVisible (phrase);
    addChildComponent (bank);

    tabBar.setTabs ({ { "Cycle", theme::cycle }, { u8 ("Hasard"), theme::lecture }, { u8 ("Découpage"), theme::decoupe }, { u8 ("Tonalité"), theme::tonal },
                      { u8 ("Écho"), theme::echo }, { "Espace", theme::espace }, { "Violoning", theme::violon },
                      { "Texture", theme::texture }, { "Banque", theme::banque } });
    tabBar.onChange = [this] (int i) { showPage (i); };
    addAndMakeVisible (tabBar);

    auto isSync = [this] { return processor.param (ParamIDs::syncMode) > 0.5f; };
    auto isFree = [this] { return processor.param (ParamIDs::syncMode) < 0.5f; };

    //== 0. CYCLE
    const auto cyc = theme::cycle;
    segMode = makeSeg (ParamIDs::captureMode, { "Continue", "Seuil", "Manuelle", "Cycle", "Banque" }, "Mode de capture", cyc,
                       u8 ("Continue : captures à la suite, sans arrêt.\nSeuil : capture dès que tu joues.\nManuelle : capture au clic.\n"
                           "Cycle : une capture à la fois, rejouée puis silence, et on recommence.\nBanque : plusieurs captures rejouées dans un ordre choisi."));
    segUnit = makeSeg (ParamIDs::syncMode, { "Secondes", "Tempo du projet" }, u8 ("Unité de temps"), cyc,
                       u8 ("Secondes : durées libres.\nTempo : durées en temps, calées sur le tempo du projet."));
    segFrom = makeSeg (ParamIDs::delayFrom, { u8 ("début de capture"), "fin de capture" }, u8 ("Délai compté depuis"), cyc,
                       u8 ("Point de départ du délai avant la première répétition."));
    segGrid = makeSeg (ParamIDs::captureGrid, { "Libre", "Sur le temps", "Sur la mesure" }, u8 ("Départ des captures"), cyc,
                       u8 ("Cale le début de chaque capture sur le temps ou la mesure la plus proche (quand le projet joue)."));
    segGrid->visibleIf = isSync;
    segRecap = makeSeg (ParamIDs::recapture, { u8 ("Immédiate"), "Au seuil" }, u8 ("Capture suivante (Cycle / Banque)"), cyc,
                        u8 ("À la fin du silence : capturer tout de suite, ou attendre que tu rejoues au-dessus du seuil."));

    setupKnob (threshold,   ParamIDs::threshold,   "Seuil", cyc, u8 ("Niveau qui déclenche une capture (repère blanc sur le vumètre)."));
    setupKnob (phraseLen,   ParamIDs::phraseLen,   "Longueur", cyc, u8 ("Durée de chaque capture (1 à 20 s)."));
    setupKnob (phraseBeats, ParamIDs::phraseBeats, "Longueur", cyc, u8 ("Durée de chaque capture, en temps."));
    setupKnob (delay,       ParamIDs::delay,       u8 ("Délai"), cyc, u8 ("Attente avant la première répétition (1 à 60 s)."));
    setupKnob (delayBeats,  ParamIDs::delayBeats,  u8 ("Délai"), cyc, u8 ("Attente avant la première répétition, en temps."));
    phraseLen.visibleIf = isFree; delay.visibleIf = isFree; phraseBeats.visibleIf = isSync; delayBeats.visibleIf = isSync;

    setupKnob (repetitions, ParamIDs::repetitions, u8 ("Répétitions"), cyc, u8 ("Nombre de fois où chaque phrase est rejouée (1 à 32)."));
    setupKnob (repeatGap,   ParamIDs::repeatGap,   u8 ("Écart"), cyc, u8 ("Silence entre deux répétitions."));
    setupKnob (gapBeats,    ParamIDs::gapBeats,    u8 ("Écart"), cyc, u8 ("Silence entre deux répétitions, en temps."));
    setupKnob (silenceMin,  ParamIDs::silenceMin,  "Silence min", cyc, u8 ("Silence après les répétitions : tiré au hasard entre min et max (Cycle / Banque)."));
    setupKnob (silenceMax,  ParamIDs::silenceMax,  "Silence max", cyc, u8 ("Silence après les répétitions : tiré au hasard entre min et max (Cycle / Banque)."));
    setupKnob (silMinBeats, ParamIDs::silMinBeats, "Silence min", cyc, u8 ("Silence minimum, en temps."));
    setupKnob (silMaxBeats, ParamIDs::silMaxBeats, "Silence max", cyc, u8 ("Silence maximum, en temps."));
    repeatGap.visibleIf = isFree; silenceMin.visibleIf = isFree; silenceMax.visibleIf = isFree;
    gapBeats.visibleIf = isSync; silMinBeats.visibleIf = isSync; silMaxBeats.visibleIf = isSync;

    planView.setTooltip (u8 ("Le cycle tel que réglé : capture, délai, répétitions (hauteur = niveau, couleur = sens), silence. La phase en cours s'allume."));

    segTempo = makeSeg (ParamIDs::tempoSource, { "Projet", "Interne" }, "Source du tempo", cyc,
                        u8 ("Projet : tempo et mesures de Logic (horloge interne quand le projet est à l'arrêt).\nInterne : ton tempo, réglé au tap."));
    setupKnob (internalBpm, ParamIDs::internalBpm, "Tempo interne", cyc, u8 ("Tempo de l'horloge interne (40 à 240 BPM)."));
    tempoView.setTooltip (u8 ("Tempo utilisé et temps de la mesure (le premier temps en jaune)."));

    //== 1. HASARD
    const auto lec = theme::lecture;
    setupChoice (order, ParamIDs::order, "Sens des relectures", lec,
                 u8 ("Dans quel sens sont rejouées les répétitions : alterné, toujours reverse, toujours normal, au hasard, les deux superposés, "
                     "ou une séquence tapée (R N X -)."));
    dirSeqEditor.setText (processor.getDirSequenceText(), false);
    dirSeqEditor.setTextToShowWhenEmpty ("ex. R N N R", theme::faint);
    dirSeqEditor.setInputRestrictions (128, "RrNnXxSs+-._ ");
    dirSeqEditor.setFont (theme::font (14.0f, true));
    dirSeqEditor.setTooltip (u8 ("Sens de chaque répétition, dans l'ordre, en boucle :\nR = reverse, N = normal, X = les deux superposés, - = silence.\nEx. « R N N R » ou « R - X N »."));
    dirSeqEditor.onTextChange = [this] { processor.setDirSequenceText (dirSeqEditor.getText().toUpperCase()); };
    addChildComponent (dirSeqEditor);
    setupKnob (randomness,  ParamIDs::randomness,  u8 ("Désordre"), lec, u8 ("0 % : ordre d'origine des notes. 100 % : ordre totalement mélangé."));
    setupToggle (freeze, ParamIDs::freeze, u8 ("Même tirage à chaque répétition"), lec, u8 ("Activé : toutes les répétitions d'une phrase utilisent le même hasard."));
    setupKnob (noteReverse, ParamIDs::noteReverse, u8 ("Inversées"), lec, u8 ("Chance qu'une note soit lue à l'envers, seule."));
    setupKnob (skipProb,    ParamIDs::skipProb,    "Muettes", lec, u8 ("Chance qu'une note soit remplacée par un silence."));
    setupKnob (repeatProb,  ParamIDs::repeatProb,  u8 ("Répétées"), lec, u8 ("Chance qu'une note rejoue la précédente (bégaiement)."));
    setupKnob (octaveProb,  ParamIDs::octaveProb,  "Octaves", lec, u8 ("Chance qu'une note saute d'une octave, vers le haut ou le bas."));
    setupKnob (melodic,     ParamIDs::melodic,     u8 ("Mélodique"), lec, u8 ("Chance qu'une note soit déplacée d'un ou deux degrés dans la gamme."));
    segQuant = makeSeg (ParamIDs::quantGrid, { "Off", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" }, u8 ("Grille rythmique (tempo du projet)"), lec,
                        u8 ("Recale le début des notes sur une grille au tempo du projet."));
    setupKnob (quantAmount, ParamIDs::quantAmount, "Quantif.", lec, u8 ("Force du recalage sur la grille."));
    setupKnob (humanize,    ParamIDs::humanize,    "Flou", lec, u8 ("Décale chaque note au hasard dans le temps."));
    const auto dec = theme::decoupe;
    setupKnob (sensitivity, ParamIDs::sensitivity, "Attaques", dec, u8 ("Sensibilité aux attaques (médiator, doigts) : plus haut = plus de notes détectées."));
    setupKnob (legato,      ParamIDs::legato,      "Legato", dec, u8 ("Repère aussi les changements de note sans attaque : hammer-on, pull-off, slide, changement d'accord tenu. 0 = off."));
    setupKnob (fade,        ParamIDs::fade,        "Fondu", dec, u8 ("Fondu enchaîné aux coupes entre les notes (coupes calées sur un passage par zéro)."));
    segPlay = makeSeg (ParamIDs::playMode, { "Auto", "Notes seules", "Accords" }, "Ce que tu joues", dec,
                       u8 ("Auto : chaque morceau est reconnu comme note seule ou accord.\nNotes seules : transposition la plus précise pour les mélodies.\n"
                           "Accords : transposition polyphonique (vocodeur de phase) pour les accords et arpèges laissés sonner."));
    analysisView.setTooltip (u8 ("Découpage de ta dernière capture : traits blancs = attaques, pointillés = changements legato, "
                                 "points orange = accords. Le nom de chaque note détectée est affiché au-dessus."));
    notesView.setTooltip (u8 ("Aperçu du hasard sur une phrase de 8 notes, en lecture normale et reverse. Clique pour un nouveau tirage."));
    rhythmView.setTooltip (u8 ("En haut : les notes jouées. En bas : leur position une fois recalées sur la grille."));

    //== 2. TONALITÉ
    const auto ton = theme::tonal;
    setupChoice (scale, ParamIDs::scale, "Gamme", ton, u8 ("Gamme dans laquelle les notes sont recalées et transposées. Off : aucune correction."));
    setupKnob (correction, ParamIDs::correction, "Correction", ton, u8 ("Force du recalage de chaque note sur la gamme."));
    setupKnob (transpose,  ParamIDs::transpose,  "Transposition", ton, u8 ("Décalage de hauteur, en degrés de la gamme (+2 = une tierce), ou en demi-tons si la gamme est Off."), true);
    setupKnob (drift,      ParamIDs::drift,      u8 ("Dérive"), ton, u8 ("Transposition ajoutée à chaque répétition : l'écho monte ou descend dans la gamme."), true);
    keyboardView.setTooltip (u8 ("Clique sur une touche pour choisir la tonique. En vert : les notes de la gamme (T = tonique)."));
    transposeView.setTooltip (u8 ("Hauteur de chaque répétition par rapport à la note jouée."));

    //== 3. ÉCHO
    const auto ech = theme::echo;
    setupChoice (speedMode, ParamIDs::speedMode, "Vitesse de bande", ech, u8 ("Vitesse de lecture façon bande : la hauteur et la durée changent ensemble."));
    setupKnob (decay,    ParamIDs::decay,    u8 ("Décroissance"), ech, u8 ("Perte de niveau à chaque répétition."));
    setupKnob (damping,  ParamIDs::damping,  "Assombrir", ech, u8 ("Le son s'assombrit un peu plus à chaque répétition."));
    setupKnob (tone,     ParamIDs::tone,     "Couleur", ech, u8 ("Filtre passe-bas de départ des répétitions."));
    setupKnob (lowCut,   ParamIDs::lowCut,   "Coupe-bas", ech, u8 ("Filtre passe-haut des répétitions."));
    setupKnob (modDepth, ParamIDs::modDepth, "Modulation", ech, u8 ("Pleurage façon bande / chorus sur les répétitions."));
    setupKnob (modRate,  ParamIDs::modRate,  "Vitesse", ech, u8 ("Vitesse de la modulation."));
    setupKnob (drive,    ParamIDs::drive,    "Saturation", ech, u8 ("Saturation douce des répétitions."));
    setupKnob (feedback, ParamIDs::feedback, u8 ("Réinjection"), ech, u8 ("Sound on sound : la relecture est réinjectée dans la capture, les couches s'empilent."));
    echoView.setTooltip (u8 ("Chaque barre est une répétition : hauteur = niveau, luminosité = couleur du filtre, largeur = vitesse de bande."));

    //== 4. ESPACE
    const auto esp = theme::espace;
    segStereo = makeSeg (ParamIDs::stereoMode, { "Centre", "Rev. G", "Rev. D", "Ping-pong", "Hasard" }, u8 ("Placement stéréo"), esp,
                         u8 ("Centre · Reverse à gauche / normal à droite · Reverse à droite / normal à gauche · Ping-pong (alternance) · Au hasard."));
    setupKnob (width,      ParamIDs::width,      "Ouverture", esp, u8 ("Écartement du placement stéréo."));
    setupToggle (diffusionGrow, ParamIDs::diffusionGrow, u8 ("Croissante"), esp, u8 ("La diffusion augmente au fil des répétitions."));
    setupKnob (diffusion,  ParamIDs::diffusion,  "Diffusion", esp, u8 ("Réverbération envoyée sur les répétitions."));
    setupKnob (roomSize,   ParamIDs::roomSize,   "Taille", esp, u8 ("Taille de l'espace de réverbération."));
    setupKnob (duckAmount, ParamIDs::duckAmount, "Ducking", esp, u8 ("De combien la relecture baisse quand tu joues."));
    setupKnob (duckRelease, ParamIDs::duckRelease, "Retour", esp, u8 ("Temps de remontée de la relecture quand tu t'arrêtes."));
    stereoView.setTooltip (u8 ("Vue de dessus : où sont placées les lectures reverse (orange) et normales (cyan)."));

    //== 5. VIOLONING
    const auto vio = theme::violon;
    segScope = makeSeg (ParamIDs::attackScope, { "Chaque note", u8 ("Chaque répétition") }, u8 ("Portée"), vio,
                        u8 ("Faire gonfler chaque note, ou toute la répétition d'un seul geste."));
    segDir = makeSeg (ParamIDs::attackDir, { "Normales + reverse", "Normales", "Reverse" }, "Sur quelles lectures", vio,
                      u8 ("Appliquer le violoning aux lectures normales, reverse, ou aux deux."));
    segPattern = makeSeg (ParamIDs::attackPattern, { "Toutes", "1 sur 2", "1 sur 3", "1 sur 4", "Hasard" }, "Lesquelles", vio,
                          u8 ("Quelles notes (ou répétitions) gonflent."));
    setupKnob (attack,     ParamIDs::attack,     "Attaque", vio, u8 ("Durée de la montée de volume (0 à 2 s)."));
    setupKnob (attackProb, ParamIDs::attackProb, u8 ("Probabilité"), vio, u8 ("Part des notes concernées en mode Hasard."));
    envelopeView.setTooltip (u8 ("Forme de volume des notes normales et reverse : le point orange marque la fin de la montée."));

    //== 6. TEXTURE
    const auto tex = theme::texture;
    setupKnob (textureMix,   ParamIDs::textureMix,   "Texture", tex, u8 ("Part du nuage de grains dans la relecture."));
    setupKnob (grainSize,    ParamIDs::grainSize,    "Taille grains", tex, u8 ("Durée de chaque grain."));
    setupKnob (grainDensity, ParamIDs::grainDensity, u8 ("Densité"), tex, u8 ("Nombre de grains par seconde."));
    setupKnob (grainSpray,   ParamIDs::grainSpray,   "Dispersion", tex, u8 ("Éloignement des grains par rapport à la tête de lecture."));
    textureView.setTooltip (u8 ("Chaque tache est un grain : position dans la phrase (vertical) dans le temps (horizontal). Orange = grain inversé."));

    //== 7. BANQUE
    const auto bnk = theme::banque;
    for (int k = 0; k < PhraseEngine::maxBankSize; ++k)
    {
        setupChoice (slotOrder[(size_t) k], ParamIDs::slotOrder (k + 1), "Capture " + juce::String (k + 1), bnk,
                     u8 ("Sens propre à cette capture (Global = réglage général)."));
        setupKnob (slotTranspose[(size_t) k], ParamIDs::slotTranspose (k + 1), "Transpo.", bnk, u8 ("Transposition ajoutée pour cette capture."), true);
        setupKnob (slotReps[(size_t) k], ParamIDs::slotReps (k + 1), u8 ("Répét."), bnk, u8 ("Répétitions de cette capture (global = réglage général)."));
    }

    //== SORTIE
    setupKnob (mix,     ParamIDs::mix,     "Mix", theme::sortie, u8 ("50 % : direct et relecture à plein niveau. 0 % : direct seul. 100 % : relecture seule."));
    setupKnob (wetGain, ParamIDs::wetGain, "Niveau", theme::sortie, u8 ("Volume de la relecture."));
    setupToggle (killDry, ParamIDs::killDry, "Relecture seule", theme::sortie,
                 u8 ("Pour une piste auxiliaire : coupe le son direct et envoie la relecture à plein niveau (le Mix est ignoré)."));

    for (auto* v : std::initializer_list<juce::Component*> { &planView, &tempoView, &notesView, &directionView, &rhythmView, &analysisView, &keyboardView, &transposeView, &echoView,
                                                             &stereoView, &diffusionView, &duckView, &envelopeView, &textureView })
        addChildComponent (v);
    addAndMakeVisible (outputView);

    //== pages
    auto G = [] (juce::String t, int w, juce::Colour a) { Group g; g.title = std::move (t); g.width = w; g.accent = a; return g; };
    {
        auto c1 = G ("CAPTURE", 380, cyc);
        c1.segs = { segMode, segUnit, segFrom, segGrid };
        c1.knobs = { &threshold, &phraseLen, &phraseBeats, &delay, &delayBeats };
        auto c2 = G (u8 ("RÉPÉTITIONS & SILENCE"), 0, cyc);
        c2.segs = { segRecap };
        c2.visual = &planView;
        c2.knobs = { &repetitions, &repeatGap, &gapBeats, &silenceMin, &silMinBeats, &silenceMax, &silMaxBeats };
        auto c3 = G ("TEMPO", 200, cyc);
        c3.segs = { segTempo };
        c3.visual = &tempoView;
        c3.knobs = { &internalBpm };
        c3.button = &tapButton;
        pages[0] = { c1, c2, c3 };
    }
    {
        auto c1 = G ("SENS & ORDRE", 280, lec);
        c1.choices = { &order };
        c1.field = &dirSeqEditor;
        c1.fieldCaption = u8 ("R reverse · N normal · X les deux · - silence");
        c1.fieldVisibleIf = [this] { return (int) processor.param (ParamIDs::order) == PhraseEngine::TypedSequence; };
        c1.visual = &directionView; c1.knobs = { &randomness }; c1.toggle = &freeze;
        auto c2 = G ("HASARD DES NOTES", 0, lec);
        c2.visual = &notesView; c2.knobs = { &noteReverse, &skipProb, &repeatProb, &octaveProb, &melodic };
        auto c3 = G ("RYTHME", 300, lec);
        c3.segs = { segQuant }; c3.visual = &rhythmView; c3.knobs = { &quantAmount, &humanize };
        pages[1] = { c1, c2, c3 };
    }
    {
        auto c1 = G (u8 ("DÉCOUPAGE EN NOTES ET ACCORDS"), 0, dec);
        c1.segs = { segPlay };
        c1.visual = &analysisView;
        c1.knobs = { &sensitivity, &legato, &fade };
        pages[2] = { c1 };
    }
    {
        auto c1 = G ("GAMME & TONIQUE", 560, ton);
        c1.choices = { &scale }; c1.visual = &keyboardView; c1.knobs = { &correction };
        auto c2 = G ("TRANSPOSITION", 0, ton);
        c2.visual = &transposeView; c2.knobs = { &transpose, &drift };
        pages[3] = { c1, c2 };
    }
    {
        auto c1 = G (u8 ("ÉCHO, BANDE & SOUND ON SOUND"), 0, ech);
        c1.choices = { &speedMode }; c1.visual = &echoView;
        c1.knobs = { &decay, &damping, &tone, &lowCut, &modDepth, &modRate, &drive, &feedback };
        pages[4] = { c1 };
    }
    {
        auto c1 = G (u8 ("STÉRÉO"), 380, esp);
        c1.segs = { segStereo }; c1.visual = &stereoView; c1.knobs = { &width };
        auto c2 = G ("DIFFUSION", 330, esp);
        c2.visual = &diffusionView; c2.knobs = { &diffusion, &roomSize }; c2.toggle = &diffusionGrow;
        auto c3 = G ("DUCKING", 0, esp);
        c3.visual = &duckView; c3.knobs = { &duckAmount, &duckRelease };
        pages[5] = { c1, c2, c3 };
    }
    {
        auto c1 = G ("VIOLONING", 0, vio);
        c1.segs = { segScope, segDir, segPattern }; c1.segColumns = 3;
        c1.visual = &envelopeView; c1.knobs = { &attack, &attackProb };
        pages[6] = { c1 };
    }
    {
        auto c1 = G ("TEXTURE GRANULAIRE", 0, tex);
        c1.visual = &textureView; c1.knobs = { &textureMix, &grainSize, &grainDensity, &grainSpray };
        pages[7] = { c1 };
    }
    {
        auto c1 = G (u8 ("RÉGLAGES PAR CAPTURE DE LA BANQUE"), 0, bnk);
        for (auto& c : slotOrder) c1.choices.push_back (&c);
        for (auto& k : slotTranspose) c1.knobs.push_back (&k);
        for (auto& k : slotReps) c1.knobs.push_back (&k);
        c1.slotGrid = true;
        pages[8] = { c1 };
    }
    outputGroup = G ("SORTIE", 170, theme::sortie);
    outputGroup.visual = &outputView;
    outputGroup.knobs = { &mix, &wetGain };
    outputGroup.toggle = &killDry;

    // MIDI learn : boutons de l'en-tête
    learnTargets.push_back ({ &holdButton, ParamIDs::hold });
    learnTargets.push_back ({ &nextButton, MidiActions::next });
    learnTargets.push_back ({ &captureButton, MidiActions::capture });
    learnTargets.push_back ({ &clearButton, MidiActions::clear });
    learnTargets.push_back ({ &tapButton, MidiActions::tap });
    learnTargets.push_back ({ &undoButton, MidiActions::undo });
    learnTargets.push_back ({ &abSwitch, MidiActions::abSwap });
    addChildComponent (overlay);

    lastSync = processor.param (ParamIDs::syncMode) > 0.5f;
    lastMode = (int) processor.param (ParamIDs::captureMode);
    lastOrder = (int) processor.param (ParamIDs::order);
    setSize (baseWidth, baseHeight);
    showPage (0);
    sendLookAndFeelChange();   // recrée les zones de valeur des boutons avec le thème
    startTimerHz (30);
}

PalindromeUI::~PalindromeUI()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

bool PalindromeUI::bankMode() const { return (int) processor.param (ParamIDs::captureMode) == PhraseEngine::Bank; }

void PalindromeUI::setupKnob (Knob& k, const juce::String& id, const juce::String& name, juce::Colour accent,
                              const juce::String& tip, bool bipolar)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.getProperties().set ("accent", (juce::int64) accent.getARGB());
    if (bipolar) k.slider.getProperties().set ("bipolar", true);
    k.slider.setTooltip (tip);
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (theme::font (12.5f));
    k.label.setColour (juce::Label::textColourId, theme::dim);
    k.label.setTooltip (tip);
    addChildComponent (k.slider);
    addChildComponent (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, id, k.slider);
    learnTargets.push_back ({ &k.slider, id });
}

void PalindromeUI::setupChoice (Choice& c, const juce::String& id, const juce::String& name, juce::Colour accent, const juce::String& tip)
{
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter (id)))
        c.box.addItemList (param->choices, 1);
    c.box.getProperties().set ("accent", (juce::int64) accent.getARGB());
    c.box.setTooltip (tip);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setFont (theme::font (12.0f));
    c.label.setColour (juce::Label::textColourId, theme::dim);
    addChildComponent (c.box);
    addChildComponent (c.label);
    c.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, id, c.box);
    learnTargets.push_back ({ &c.box, id });
}

void PalindromeUI::setupToggle (Toggle& t, const juce::String& id, const juce::String& name, juce::Colour accent, const juce::String& tip)
{
    t.button.setButtonText (name);
    t.button.getProperties().set ("accent", (juce::int64) accent.getARGB());
    t.button.setTooltip (tip);
    addChildComponent (t.button);
    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id, t.button);
    learnTargets.push_back ({ &t.button, id });
}

SegmentedControl* PalindromeUI::makeSeg (const juce::String& id, juce::StringArray labels, const juce::String& caption,
                                                         juce::Colour accent, const juce::String& tip)
{
    auto s = std::make_unique<SegmentedControl> (processor.apvts, id, std::move (labels), accent);
    s->caption = caption;
    s->setTooltip (tip);
    addChildComponent (s.get());
    learnTargets.push_back ({ s.get(), id });
    segs.push_back (std::move (s));
    return segs.back().get();
}

//==============================================================================
void PalindromeUI::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("Presets d'usine");
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem (processor.getProgramName (i), i + 1);

    userPresets = processor.getUserPresets();
    if (! userPresets.isEmpty())
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Mes presets");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 1000 + i);
    }
}

void PalindromeUI::stepPreset (int delta)
{
    juce::Array<int> ids;
    for (int i = 0; i < presetBox.getNumItems(); ++i) ids.add (presetBox.getItemId (i));
    if (ids.isEmpty()) return;
    int idx = ids.indexOf (presetBox.getSelectedId());
    idx = idx < 0 ? 0 : (idx + delta + ids.size()) % ids.size();
    presetBox.setSelectedId (ids[idx], juce::sendNotification);
}

void PalindromeUI::savePresetDialog()
{
    auto* w = new juce::AlertWindow ("Enregistrer un preset", u8 ("Nom du preset :"), juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "", "");
    w->addButton ("Enregistrer", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Annuler", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result != 1) return;
        const auto name = w->getTextEditorContents ("name");
        if (processor.saveUserPreset (name))
        {
            refreshPresetList();
            for (int i = 0; i < userPresets.size(); ++i)
                if (userPresets[i].getFileNameWithoutExtension() == juce::File::createLegalFileName (name.trim()))
                    presetBox.setSelectedId (1000 + i, juce::dontSendNotification);
        }
    }), true);
}

//==============================================================================
void PalindromeUI::showPage (int page)
{
    currentPage = page;
    tabBar.setCurrent (page);

    for (int pg = 0; pg < numPages; ++pg)
        for (auto& g : pages[(size_t) pg])
        {
            const bool v = pg == page;
            for (auto* s : g.segs)    s->setVisible (v && s->shown());
            for (auto* c : g.choices) { c->box.setVisible (v); c->label.setVisible (v); }
            for (auto* k : g.knobs)   { const bool kv = v && k->shown(); k->slider.setVisible (kv); k->label.setVisible (kv); }
            if (g.toggle != nullptr) g.toggle->button.setVisible (v);
            if (g.button != nullptr) g.button->setVisible (v);
            if (g.field != nullptr)  g.field->setVisible (v && g.fieldShown());
            if (g.visual != nullptr) g.visual->setVisible (v);
        }

    for (auto* k : outputGroup.knobs) { k->slider.setVisible (true); k->label.setVisible (true); }
    outputGroup.toggle->button.setVisible (true);
    resized();
    repaint();
}

//==============================================================================
void PalindromeUI::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);

    // logo : les lettres passent du cyan (normal) à l'orange (reverse)
    {
        const juce::String name ("PALINDROME");
        auto area = headerArea.toFloat().withWidth (190.0f);
        g.setFont (theme::font (22.0f, true));
        float x = area.getX();
        for (int i = 0; i < name.length(); ++i)
        {
            const auto ch = name.substring (i, i + 1);
            const float t = (float) i / (float) (name.length() - 1);
            g.setColour (theme::normal.interpolatedWith (theme::reverse, t));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), ch);
            g.drawText (ch, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()).toNearestInt(), juce::Justification::centredLeft);
            x += w + 1.5f;
        }
        g.setColour (theme::faint);
        g.setFont (theme::font (10.5f));
        g.drawText (u8 ("capture · écho · reverse"), juce::Rectangle<float> (area.getX(), area.getBottom() - 12.0f, 190.0f, 12.0f).toNearestInt(), juce::Justification::centredLeft);
    }

    // témoin d'activité MIDI
    {
        g.setColour (theme::midi.withAlpha (0.15f + 0.85f * midiFlash));
        g.fillEllipse (midiLed);
        g.setColour (theme::midi.withAlpha (0.5f));
        g.drawEllipse (midiLed, 1.0f);
    }

    auto drawGroup = [&] (const Group& grp)
    {
        auto a = grp.area.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (a, 10.0f);
        g.setColour (grp.accent.withAlpha (0.28f));
        g.drawRoundedRectangle (a.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (grp.accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (a.getX() + 14.0f, a.getY() + 15.0f, 3.0f, 12.0f), 1.5f);
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f, true));
        g.drawText (grp.title, juce::Rectangle<float> (a.getX() + 23.0f, a.getY() + 10.0f, a.getWidth() - 30.0f, 22.0f).toNearestInt(), juce::Justification::centredLeft);

        g.setColour (theme::dim);
        g.setFont (theme::font (12.0f));
        for (auto& [r, t] : grp.captions)
            g.drawText (t, r, juce::Justification::centredLeft);

        // cartes de la banque : un emplacement par colonne
        const int bankN = juce::roundToInt (processor.param (ParamIDs::bankSize));
        const int playing = processor.engine.bankPlayingSlot.load();
        for (size_t k = 0; k < grp.cards.size(); ++k)
        {
            auto c = grp.cards[k].toFloat();
            const bool active = (int) k < bankN;
            const bool filled = processor.engine.bankLengthView[k].load() > 0.0f;
            g.setColour ((filled ? theme::banque.withAlpha (0.1f) : theme::bg.withAlpha (0.5f)).withMultipliedAlpha (active ? 1.0f : 0.4f));
            g.fillRoundedRectangle (c, 8.0f);
            g.setColour (((int) k == playing && filled ? theme::normal : theme::outline).withMultipliedAlpha (active ? 1.0f : 0.4f));
            g.drawRoundedRectangle (c.reduced (0.5f), 8.0f, (int) k == playing && filled ? 2.0f : 1.0f);
            g.setColour (theme::faint);
            g.setFont (theme::font (10.5f));
            const auto lockR = cardLock (grp.cards[k]);
            const bool locked = processor.isSlotLocked ((int) k);
            g.drawText (! active ? u8 ("hors banque") : filled ? juce::String (processor.engine.bankLengthView[k].load(), 1) + " s" : juce::String ("vide"),
                        c.removeFromBottom (18.0f).toNearestInt(), juce::Justification::centred);
            if (active)
            {
                if (locked)
                {
                    g.setColour (theme::hold.withAlpha (0.8f));
                    g.drawRoundedRectangle (grp.cards[k].toFloat().reduced (1.5f), 8.0f, 1.0f);
                }
                pal::drawLock (g, lockR, locked, locked ? theme::hold : theme::faint);
            }
        }
    };

    for (auto& grp : pages[(size_t) currentPage]) drawGroup (grp);
    drawGroup (outputGroup);
}

void PalindromeUI::layoutGroup (Group& grp)
{
    grp.captions.clear();
    auto a = grp.area.reduced (14, 10).withTrimmedTop (28);

    if (grp.slotGrid)
    {
        const int cols = (int) grp.choices.size();
        const int w = a.getWidth() / juce::jmax (1, cols);
        grp.cards.clear();
        for (int c = 0; c < cols; ++c)
        {
            auto card = a.withX (a.getX() + c * w).withWidth (w).reduced (4, 0);
            grp.cards.push_back (card);
            auto col = card.reduced (8, 8).withTrimmedBottom (16);
            auto* ch = grp.choices[(size_t) c];
            ch->label.setBounds (col.removeFromTop (18));
            ch->box.setBounds (col.removeFromTop (28));
            col.removeFromTop (12);
            for (int row = 0; row < 2; ++row)
            {
                auto* k = grp.knobs[(size_t) (row * cols + c)];
                auto cell = col.removeFromTop (juce::jmin (128, col.getHeight() / (2 - row)));
                k->label.setBounds (cell.removeFromTop (18));
                k->slider.setBounds (cell.reduced (10, 0));
            }
        }
        return;
    }

    // segments (en colonnes si demandé)
    std::vector<SegmentedControl*> visibleSegs;
    for (auto* s : grp.segs) if (s->shown()) visibleSegs.push_back (s);
    for (size_t i = 0; i < visibleSegs.size(); i += (size_t) grp.segColumns)
    {
        auto row = a.removeFromTop (48);
        a.removeFromTop (6);
        const int n = juce::jmin ((int) visibleSegs.size() - (int) i, grp.segColumns);
        const int w = row.getWidth() / juce::jmax (1, grp.segColumns);
        for (int c = 0; c < n; ++c)
        {
            auto cell = row.removeFromLeft (w).reduced (grp.segColumns > 1 ? 5 : 0, 0);
            auto* s = visibleSegs[i + (size_t) c];
            grp.captions.push_back ({ cell.removeFromTop (18), s->caption });
            s->setBounds (cell.removeFromTop (30));
        }
    }

    // listes
    if (! grp.choices.empty())
    {
        auto row = a.removeFromTop (48);
        a.removeFromTop (6);
        const int w = row.getWidth() / (int) grp.choices.size();
        for (auto* ch : grp.choices)
        {
            auto cell = row.removeFromLeft (w).reduced (grp.choices.size() > 1 ? 4 : 0, 0);
            ch->label.setBounds (cell.removeFromTop (18));
            ch->box.setBounds (cell.removeFromTop (28));
        }
    }

    if (grp.fieldShown())
    {
        auto row = a.removeFromTop (48);
        a.removeFromTop (6);
        grp.captions.push_back ({ row.removeFromTop (18), grp.fieldCaption });
        grp.field->setBounds (row.removeFromTop (28));
    }

    if (grp.toggle != nullptr)
        grp.toggle->button.setBounds (a.removeFromBottom (26));
    if (grp.button != nullptr)
    {
        grp.button->setBounds (a.removeFromBottom (30));
        a.removeFromBottom (6);
    }

    std::vector<Knob*> visible;
    for (auto* k : grp.knobs) if (k->shown()) visible.push_back (k);

    const int knobRowH = visible.empty() ? 0 : 108;
    juce::Rectangle<int> knobRow;

    if (grp.visual != nullptr)
    {
        knobRow = a.removeFromBottom (knobRowH);
        a.removeFromBottom (visible.empty() ? 0 : 8);
        grp.visual->setBounds (a);
    }
    else if (! visible.empty())
    {
        knobRow = a.withHeight (knobRowH).withY (a.getY() + juce::jmax (0, (a.getHeight() - knobRowH) / 3));
    }

    if (! visible.empty())
    {
        const int w = juce::jmin (112, knobRow.getWidth() / (int) visible.size());
        auto row = knobRow.withSizeKeepingCentre (w * (int) visible.size(), knobRow.getHeight());
        for (auto* k : visible)
        {
            auto cell = row.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (18));
            k->slider.setBounds (cell.reduced (6, 0));
        }
    }
}

void PalindromeUI::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    overlay.setBounds (getLocalBounds());

    // en-tête
    headerArea = r.removeFromTop (46);
    {
        auto h = headerArea;
        h.removeFromLeft (196);
        auto presets = h.removeFromLeft (300).reduced (0, 6);
        prevPreset.setBounds (presets.removeFromLeft (32));
        presets.removeFromLeft (4);
        savePresetButton.setBounds (presets.removeFromRight (32));
        presets.removeFromRight (4);
        nextPreset.setBounds (presets.removeFromRight (32));
        presets.removeFromRight (4);
        presetBox.setBounds (presets);

        h.removeFromLeft (10);
        auto tools = h.reduced (0, 6);
        undoButton.setBounds (tools.removeFromLeft (32));
        tools.removeFromLeft (4);
        redoButton.setBounds (tools.removeFromLeft (32));
        tools.removeFromLeft (10);
        abSwitch.setBounds (tools.removeFromLeft (62));
        tools.removeFromLeft (4);
        copyABButton.setBounds (tools.removeFromLeft (32));
        tools.removeFromLeft (10);
        midiButton.setBounds (tools.removeFromLeft (76));
        midiLed = juce::Rectangle<float> ((float) midiButton.getRight() - 13.0f, (float) midiButton.getY() + 5.0f, 7.0f, 7.0f);
        tools.removeFromLeft (4);
        sizeButton.setBounds (tools.removeFromLeft (32));

        auto right = h.reduced (0, 4);
        clearButton.setBounds (right.removeFromRight (86));
        right.removeFromRight (6);
        captureButton.setBounds (right.removeFromRight (128));
        right.removeFromRight (6);
        nextButton.setBounds (right.removeFromRight (112));
        right.removeFromRight (6);
        holdButton.setBounds (right.removeFromRight (90));
    }
    r.removeFromTop (8);

    // place en plus (fenêtre plus haute que la base) : partagée entre l'activité et les réglages
    const int extra = juce::jmax (0, getHeight() - baseHeight);
    activity.setBounds (r.removeFromTop (186 + extra * 2 / 5));
    r.removeFromTop (8);

    auto row = r.removeFromTop (122 + extra / 6);
    if (bankMode())
    {
        bank.setVisible (true);
        bank.setBounds (row.removeFromRight (640));
        row.removeFromRight (8);
    }
    else bank.setVisible (false);
    phrase.setBounds (row);
    r.removeFromTop (8);

    tabBar.setBounds (r.removeFromTop (36));
    r.removeFromTop (8);

    pageArea = r;
    auto area = pageArea;
    const int gap = 10;

    outputGroup.area = area.removeFromRight (outputGroup.width);
    area.removeFromRight (gap);
    layoutGroup (outputGroup);

    auto& groups = pages[(size_t) currentPage];
    for (size_t i = 0; i < groups.size(); ++i)
    {
        auto& grp = groups[i];
        int fixedAfter = 0;
        for (size_t j = i + 1; j < groups.size(); ++j) fixedAfter += groups[j].width + gap;
        const int w = grp.width > 0 ? grp.width : area.getWidth() - fixedAfter;
        grp.area = area.removeFromLeft (juce::jmax (120, w));
        area.removeFromLeft (gap);
        layoutGroup (grp);
    }
}

//==============================================================================
void PalindromeUI::timerCallback()
{
    const int mode = (int) processor.param (ParamIDs::captureMode);
    const bool sync = processor.param (ParamIDs::syncMode) > 0.5f;
    const int ord = (int) processor.param (ParamIDs::order);
    if (sync != lastSync || mode != lastMode || ord != lastOrder)
    {
        lastSync = sync;
        lastMode = mode;
        lastOrder = ord;
        showPage (currentPage);
    }

    // annuler / A-B / séquence de sens (changée par un preset, A/B ou annuler)
    undoButton.setEnabled (processor.canUndo());
    redoButton.setEnabled (processor.canRedo());
    abSwitch.repaint();
    if (! dirSeqEditor.hasKeyboardFocus (true) && dirSeqEditor.getText() != processor.getDirSequenceText())
        dirSeqEditor.setText (processor.getDirSequenceText(), false);
    internalBpm.slider.setEnabled (processor.param (ParamIDs::tempoSource) > 0.5f);

    // MIDI
    const int act = processor.midiActivity.load();
    if (act != lastMidiActivity) { lastMidiActivity = act; midiFlash = 1.0f; }
    else midiFlash *= 0.8f;
    repaint (midiLed.expanded (2.0f).toNearestInt());
    midiButton.setToggleState (isLearnMode(), juce::dontSendNotification);
    if (isLearnMode())
    {
        overlay.repaint();
        if (processor.learnVersion.load() != overlay.lastLearnVersion) overlay.lastLearnVersion = processor.learnVersion.load();
    }

    const bool flowMode = mode == PhraseEngine::Cycle || mode == PhraseEngine::Bank;
    const bool recapThr = processor.param (ParamIDs::recapture) > 0.5f;
    threshold.slider.setEnabled (mode == PhraseEngine::Threshold || (flowMode && recapThr));
    for (auto* k : { &silenceMin, &silenceMax, &silMinBeats, &silMaxBeats }) k->slider.setEnabled (flowMode);
    segRecap->setEnabled (flowMode);
    quantAmount.slider.setEnabled (processor.param (ParamIDs::quantGrid) > 0.5f);
    correction.slider.setEnabled (processor.param (ParamIDs::scale) > 0.5f);
    width.slider.setEnabled (processor.param (ParamIDs::stereoMode) > 0.5f);
    attackProb.slider.setEnabled (processor.param (ParamIDs::attackPattern) > 3.5f);
    roomSize.slider.setEnabled (processor.param (ParamIDs::diffusion) > 0.05f);
    duckRelease.slider.setEnabled (processor.param (ParamIDs::duckAmount) < -0.05f);
    for (auto* k : { &grainSize, &grainDensity, &grainSpray }) k->slider.setEnabled (processor.param (ParamIDs::textureMix) > 0.05f);
    nextButton.setEnabled (flowMode);
    captureButton.setEnabled (mode != PhraseEngine::Cycle);
    captureButton.setButtonText (mode == PhraseEngine::Bank ? "RECAPTURER " + juce::String ((int) processor.param (ParamIDs::targetSlot) + 1)
                                                             : juce::String ("CAPTURER"));

    if (bankMode() && currentPage == 8) repaint (pageArea);
    activity.refresh();
    phrase.refresh();
    if (bank.isVisible()) bank.refresh();
    outputView.refresh();
    for (auto& grp : pages[(size_t) currentPage])
        if (grp.visual != nullptr) grp.visual->refresh();
}

//==============================================================================
juce::Rectangle<float> PalindromeUI::cardLock (juce::Rectangle<int> card)
{
    return { (float) card.getRight() - 24.0f, (float) card.getY() + 8.0f, 14.0f, 14.0f };
}

void PalindromeUI::mouseDown (const juce::MouseEvent& e)
{
    // cadenas des cartes de la page Banque
    for (auto& grp : pages[(size_t) currentPage])
        for (size_t k = 0; k < grp.cards.size(); ++k)
            if (cardLock (grp.cards[k]).expanded (4.0f).contains (e.position))
            {
                processor.setSlotLocked ((int) k, ! processor.isSlotLocked ((int) k));
                repaint();
                return;
            }
}

void PalindromeUI::setLearnMode (bool on)
{
    if (on) overlay.open();
    else    overlay.close();
    midiButton.setToggleState (on, juce::dontSendNotification);
}

void PalindromeUI::showSizeMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Taille de l'interface");
    const float current = (float) getTransform().mat00;
    const std::pair<const char*, float> sizes[] = { { "Petite (70 %)", 0.7f }, { "Moyenne (85 %)", 0.85f }, { "Normale (100 %)", 1.0f },
                                                    { "Grande (125 %)", 1.25f }, { u8"Très grande (150 %)", 1.5f } };
    int id = 1;
    for (auto& [name, sc] : sizes)
        m.addItem (id++, juce::String::fromUTF8 (name), true, std::abs (current - sc) < 0.02f);
    m.addSeparator();
    m.addItem (100, juce::String::fromUTF8 ("Astuce : tire le coin en bas à droite pour une taille libre"), false);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&sizeButton), [this] (int r)
    {
        static const float scales[] = { 0.7f, 0.85f, 1.0f, 1.25f, 1.5f };
        if (r >= 1 && r <= 5 && onSizeChosen) onSizeChosen (scales[r - 1]);
    });
}
