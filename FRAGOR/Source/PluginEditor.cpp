#include "PluginEditor.h"

using namespace fragor;

namespace
{
    juce::Font font (float h, bool bold = true) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    constexpr int KW = 56;
}

// ---------------------------------------------------------------- helpers
Knob* FragorEditor::knob (juce::Component& parent, const juce::String& id, const juce::String& label, juce::Colour c)
{
    auto* k = own (std::make_unique<Knob> (proc.apvts, id, label, c), parent);
    const int d = destForParam (id);
    if (d >= 0) { k->attachMod (&proc, d); modKnobs.push_back (k); }
    return k;
}

Choice* FragorEditor::choice (juce::Component& parent, const juce::String& id, const juce::String& label)
{
    return own (std::make_unique<Choice> (proc.apvts, id, label), parent);
}

Toggle* FragorEditor::toggle (juce::Component& parent, const juce::String& id, const juce::String& text, juce::Colour c)
{
    return own (std::make_unique<Toggle> (proc.apvts, id, text, c), parent);
}

Panel* FragorEditor::panel (juce::Component& parent, const juce::String& title, juce::Rectangle<int> b, juce::Colour c)
{
    auto* p = own (std::make_unique<Panel> (title, c), parent);
    p->setBounds (b);
    return p;
}

DragChip* FragorEditor::chip (juce::Component& parent, const juce::String& text, int source, juce::Rectangle<int> b, juce::Colour c)
{
    auto* ch = own (std::make_unique<DragChip> (text, source, c), parent);
    ch->setBounds (b);
    return ch;
}

// ---------------------------------------------------------------- construction
FragorEditor::FragorEditor (FragorProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    root.setLookAndFeel (&lnf);
    addAndMakeVisible (root);
    root.setBounds (0, 0, kW, kH);

    buildHeader();
    buildOsc (0);
    buildOsc (1);
    buildSubVoice();
    buildFilterEnv();
    buildMacros();

    modPage.setBounds (0, 564, kW, 220);
    fxPage.setBounds (0, 564, kW, 220);
    root.addAndMakeVisible (modPage);
    root.addChildComponent (fxPage);
    buildModPage();
    buildFxPage();

    for (auto* t : { &modTab, &fxTab })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (77);
        root.addAndMakeVisible (t);
    }
    modTab.setBounds (8, 536, 140, 24);
    fxTab.setBounds (152, 536, 140, 24);
    modTab.onClick = [this] { showPage (0); };
    fxTab.onClick  = [this] { showPage (1); };
    modTab.setToggleState (true, juce::dontSendNotification);

    auto hint = std::make_unique<juce::Label>();
    hint->setText (juce::String::fromUTF8 ("Glisse une source (puces à poignée) sur un bouton pour le moduler  -  Alt+glisser : quantité  -  clic droit : modulations"),
                   juce::dontSendNotification);
    hint->setFont (font (10.5f, false));
    hint->setColour (juce::Label::textColourId, Col::dim);
    hint->setJustificationType (juce::Justification::centredRight);
    hint->setBounds (300, 536, 892, 24);
    own (std::move (hint), root);

    keyboard = std::make_unique<juce::MidiKeyboardComponent> (proc.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
    keyboard->setAvailableRange (24, 96);
    keyboard->setLowestVisibleKey (24);
    keyboard->setKeyWidth ((kW - 16) / 43.0f);
    keyboard->setOctaveForMiddleC (4);
    keyboard->setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffcfd3db));
    keyboard->setColour (juce::MidiKeyboardComponent::blackNoteColourId, Col::panel2);
    keyboard->setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, Col::acid);
    keyboard->setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Col::acid.withAlpha (0.3f));
    keyboard->setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard->setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, Col::bg);
    root.addAndMakeVisible (*keyboard);
    keyboard->setBounds (8, 792, kW - 16, 42);

    // fenêtres superposées
    curveOverlay = std::make_unique<CurveOverlay> (proc);
    browser      = std::make_unique<PresetBrowser> (proc);
    settings     = std::make_unique<SettingsOverlay> (proc);
    saveOverlay  = std::make_unique<SaveOverlay> (proc);
    for (juce::Component* o : { (juce::Component*) curveOverlay.get(), (juce::Component*) browser.get(),
                                (juce::Component*) settings.get(), (juce::Component*) saveOverlay.get() })
    {
        root.addChildComponent (o);
        o->setBounds (0, 0, kW, kH);
    }
    curveOverlay->onClose = [this] { if (lfoDisplay) lfoDisplay->repaint(); };

    setResizable (true, true);
    setResizeLimits (kW * 6 / 10, kH * 6 / 10, kW * 2, kH * 2);
    getConstrainer()->setFixedAspectRatio ((double) kW / (double) kH);
    setSize (kW, kH);

    refreshNames();
    startTimerHz (30);
}

FragorEditor::~FragorEditor()
{
    stopTimer();
    root.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void FragorEditor::paint (juce::Graphics& g) { g.fillAll (Col::bg); }

void FragorEditor::resized()
{
    root.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));
}

void FragorEditor::Root::paint (juce::Graphics& g)
{
    g.fillAll (Col::bg);
    g.setGradientFill (juce::ColourGradient (Col::panel2, 0, 0, Col::bg, 0, 52, false));
    g.fillRect (0, 0, getWidth(), 52);
    g.setColour (Col::border);
    g.drawHorizontalLine (52, 0.0f, (float) getWidth());

    g.setFont (font (30.0f));
    g.setColour (Col::acid.withAlpha (0.25f));
    g.drawText ("FRAGOR", 15, 7, 200, 36, juce::Justification::centredLeft);
    g.setColour (Col::acid);
    g.drawText ("FRAGOR", 14, 6, 200, 36, juce::Justification::centredLeft);
    g.setFont (font (9.5f));
    g.setColour (Col::hot);
    g.drawText ("DUBSTEP DESTRUCTION SYNTH  1.1", 16, 36, 220, 12, juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- en-tête
void FragorEditor::buildHeader()
{
    for (auto* b : { &prevBtn, &nextBtn, &presetBtn, &saveBtn, &loadBtn, &undoBtn, &redoBtn, &genBtn, &mutateBtn, &setBtn })
        root.addAndMakeVisible (b);

    prevBtn.setBounds (230, 12, 28, 28);
    presetBtn.setBounds (262, 12, 220, 28);
    nextBtn.setBounds (486, 12, 28, 28);
    saveBtn.setBounds (520, 12, 52, 28);
    loadBtn.setBounds (576, 12, 52, 28);
    undoBtn.setBounds (634, 12, 52, 28);
    redoBtn.setBounds (690, 12, 52, 28);

    genStyle.addItemList (generatorStyles(), 1);
    genStyle.setSelectedId (1, juce::dontSendNotification);
    root.addAndMakeVisible (genStyle);
    genStyle.setBounds (750, 12, 100, 28);
    genBtn.setBounds (854, 12, 92, 28);
    mutateBtn.setBounds (950, 12, 74, 28);
    setBtn.setBounds (1028, 12, 44, 28);
    genBtn.setColour (juce::TextButton::buttonColourId, Col::acid.withAlpha (0.85f));
    genBtn.setColour (juce::TextButton::textColourOffId, Col::bg);
    mutateBtn.setColour (juce::TextButton::buttonColourId, Col::hot.withAlpha (0.8f));
    mutateBtn.setColour (juce::TextButton::textColourOffId, Col::bg);
    genBtn.setTooltip ("Fabrique un patch complet dans le style choisi");
    mutateBtn.setTooltip ("Fait muter le patch courant");
    presetBtn.setTooltip ("Navigateur de presets (tags, recherche, favoris)");
    undoBtn.setTooltip ("Annuler (preset, GENERATE, MUTATE, import)");
    redoBtn.setTooltip (juce::String::fromUTF8 ("Rétablir"));
    setBtn.setTooltip (juce::String::fromUTF8 ("Réglages : qualité, MPE, sortie MIDI, auto gain"));

    voicesLabel.setFont (font (10.5f));
    voicesLabel.setColour (juce::Label::textColourId, Col::dim);
    voicesLabel.setJustificationType (juce::Justification::centredRight);
    root.addAndMakeVisible (voicesLabel);
    voicesLabel.setBounds (1076, 12, 116, 28);

    prevBtn.onClick = [this] { const int n = proc.getNumPrograms(); proc.loadFactoryPreset ((proc.getCurrentProgram() - 1 + n) % n); };
    nextBtn.onClick = [this] { proc.loadFactoryPreset ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    presetBtn.onClick = [this] { browser->open(); };
    genBtn.onClick    = [this] { proc.generate (genStyle.getSelectedItemIndex()); };
    mutateBtn.onClick = [this] { proc.mutate (0.5f); };
    undoBtn.onClick   = [this] { proc.undo(); };
    redoBtn.onClick   = [this] { proc.redo(); };
    setBtn.onClick    = [this] { settings->setVisible (true); settings->toFront (true); };
    saveBtn.onClick   = [this] { saveOverlay->open(); };
    loadBtn.onClick   = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Charger un preset", FragorProcessor::userPresetFolder(), "*.fragor");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile()) proc.loadUserPreset (f);
                              });
    };
}

// ---------------------------------------------------------------- oscillateurs
void FragorEditor::buildOsc (int o)
{
    const juce::String p = o == 0 ? "a" : "b";
    auto* pn = panel (root, o == 0 ? "OSC A" : "OSC B", { 8 + o * 408, 60, 400, 240 });

    toggle (*pn, p + "On", "ON")->setBounds (56, 6, 44, 20);
    auto* tbl = choice (*pn, p + "Table");
    tbl->setBounds (104, 5, 96, 22);
    tbl->box.setTooltip ("Wavetable (USER A / USER B = tes imports)");
    auto* wm = choice (*pn, p + "WarpMode");
    wm->setBounds (204, 5, 86, 22);
    wm->box.setTooltip (juce::String::fromUTF8 ("Mode de warp (déformation de phase)"));
    toggle (*pn, p + "RandPhase", "RND", Col::violet)->setBounds (294, 6, 46, 20);

    auto* wav = own (std::make_unique<juce::TextButton> ("WAV"), *pn);
    wav->setBounds (344, 5, 48, 22);
    wav->setTooltip (juce::String::fromUTF8 ("Importer un WAV comme wavetable (format wavetable 2048 ou son quelconque : détection de période). "
                     "Tu peux aussi glisser le fichier sur l'affichage."));
    wav->onClick = [this, o]
    {
        chooser = std::make_unique<juce::FileChooser> ("Importer une wavetable", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                       "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, o] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  juce::String err;
                                  if (f.existsAsFile() && ! proc.importWavetable (o, f, err))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import wavetable", err);
                              });
    };

    own (std::make_unique<WaveDisplay> (proc, p), *pn)->setBounds (8, 34, 154, 196);

    std::vector<juce::Component*> ks {
        knob (*pn, p + "Pos", "WT POS"), knob (*pn, p + "Warp", "WARP", Col::hot),
        knob (*pn, p + "Level", "LEVEL"), knob (*pn, p + "Pan", "PAN"),
        knob (*pn, p + "Uni", "UNISON", Col::violet), knob (*pn, p + "Detune", "DETUNE", Col::violet),
        knob (*pn, p + "Blend", "BLEND", Col::violet), knob (*pn, p + "Spread", "STEREO", Col::violet),
        knob (*pn, p + "Oct", "OCT", Col::text), knob (*pn, p + "Semi", "SEMI", Col::text),
        knob (*pn, p + "Fine", "FINE", Col::text) };
    layoutGrid (ks, { 168, 34, 224, 196 }, 4, KW, 65);
}

void FragorEditor::buildSubVoice()
{
    auto* sp = panel (root, "SUB / NOISE", { 824, 60, 170, 240 }, Col::violet);
    toggle (*sp, "subOn", "ON", Col::violet)->setBounds (8, 30, 60, 22);
    toggle (*sp, "subDirect", "PURE", Col::violet)->setBounds (72, 30, 90, 22);
    choice (*sp, "subShape", "SHAPE")->setBounds (8, 56, 74, 36);
    choice (*sp, "subOct", "OCTAVE")->setBounds (88, 56, 74, 36);
    layoutGrid ({ knob (*sp, "subLevel", "SUB", Col::violet) }, { 8, 98, 154, 66 }, 1, 154, 66);
    layoutGrid ({ knob (*sp, "noiseLevel", "NOISE"), knob (*sp, "noiseColor", "COLOR") }, { 8, 168, 154, 66 }, 2, 77, 66);

    auto* vp = panel (root, "VOICE", { 1002, 60, 190, 240 }, Col::text);
    choice (*vp, "voiceMode", "MODE")->setBounds (8, 28, 84, 36);
    auto* q = choice (*vp, "quality", "QUALITY");
    q->setBounds (98, 28, 84, 36);
    q->box.setTooltip (juce::String::fromUTF8 ("Sur-échantillonnage des voix : 2x/4x = moins de repliement sur les sync, FM et aigus"));
    std::vector<juce::Component*> ks {
        knob (*vp, "glide", "GLIDE", Col::text), knob (*vp, "bendRange", "BEND", Col::text), knob (*vp, "ampVel", "VELO", Col::text),
        knob (*vp, "penvAmt", "P.ENV", Col::hot), knob (*vp, "penvDecay", "P.DECAY", Col::hot), knob (*vp, "masterTune", "TUNE", Col::text) };
    layoutGrid (ks, { 8, 70, 174, 160 }, 3, 58, 80);
}

// ---------------------------------------------------------------- filtres & enveloppes
void FragorEditor::buildFilterEnv()
{
    auto* fp = panel (root, "FILTER", { 8, 308, 360, 220 });
    for (int i = 0; i < 2; ++i)
    {
        auto& t = fltTabs[i];
        t.setButtonText ("F" + juce::String (i + 1));
        t.setClickingTogglesState (true);
        t.setRadioGroupId (88);
        t.setBounds (64 + i * 34, 5, 32, 22);
        t.onClick = [this, i] { selectFilter (i); };
        fp->addAndMakeVisible (t);

        const juce::String p = i == 0 ? "flt" : "f2";
        auto* on = toggle (*fp, p + "On", "ON");            on->setBounds (136, 6, 44, 20);
        auto* ty = choice (*fp, p + "Type");                 ty->setBounds (184, 5, 92, 22);
        auto* cut = knob (*fp, p + "Cutoff", "CUTOFF");      cut->setBounds (8, 40, 96, 120);
        auto* re = knob (*fp, p + "Reso", "RESO");
        auto* dr = knob (*fp, p + "Drive", "DRIVE", Col::hot);
        auto* mo = knob (*fp, p + "Morph", "VOWEL", Col::violet);
        auto* ke = knob (*fp, p + "Key", "KEY");
        auto* mi = knob (*fp, p + "Mix", "MIX");
        auto* en = knob (*fp, p + "EnvAmt", i == 0 ? "ENV 2" : "ENV 3", Col::hot);
        layoutGrid ({ re, dr, mo, ke, mi, en }, { 110, 34, 246, 180 }, 3, 82, 88);
        fltControls[i] = { on, ty, cut, re, dr, mo, ke, mi, en };
    }
    auto* routing = choice (*fp, "fltRouting");
    routing->setBounds (280, 5, 72, 22);
    routing->box.setTooltip (juce::String::fromUTF8 ("F1 > F2 en série, ou F1 + F2 en parallèle"));
    selectFilter (0);

    auto* ep = panel (root, "ENVELOPES", { 376, 308, 310, 220 }, Col::hot);
    const char* pre[] = { "amp", "e2", "e3" };
    for (int i = 0; i < 3; ++i)
    {
        if (i == 0)
        {
            auto l = std::make_unique<juce::Label>();
            l->setText ("AMP", juce::dontSendNotification);
            l->setFont (font (11.0f));
            l->setColour (juce::Label::textColourId, Col::acid);
            l->setBounds (6, 30, 46, 46);
            own (std::move (l), *ep);
        }
        else chip (*ep, i == 1 ? "ENV2" : "ENV3", i == 1 ? srcEnv2 : srcEnv3, { 4, 42 + i * 62, 48, 20 }, Col::hot);
        const juce::String p = pre[i];
        const auto c = i == 0 ? Col::acid : Col::hot;
        layoutGrid ({ knob (*ep, p + "A", "A", c), knob (*ep, p + "D", "D", c), knob (*ep, p + "S", "S", c), knob (*ep, p + "R", "R", c) },
                    { 54, 26 + i * 62, 252, 62 }, 4, 62, 62);
    }
}

void FragorEditor::selectFilter (int i)
{
    for (int k = 0; k < 2; ++k)
    {
        fltTabs[k].setToggleState (k == i, juce::dontSendNotification);
        for (auto* c : fltControls[k]) c->setVisible (k == i);
    }
}

void FragorEditor::buildMacros()
{
    auto* mp = panel (root, "MACROS", { 694, 308, 250, 220 }, Col::hot);
    for (int i = 0; i < kNumMacros; ++i)
    {
        chip (*mp, "M" + juce::String (i + 1), srcMacro1 + i, { 14 + i * 59, 28, 46, 16 }, Col::hot);
        knob (*mp, "macro" + juce::String (i + 1), "", Col::hot)->setBounds (8 + i * 59, 46, 58, 78);
        auto& l = macroLabels[i];
        l.setEditable (false, true, false);
        l.setJustificationType (juce::Justification::centred);
        l.setFont (font (11.0f));
        l.setColour (juce::Label::textColourId, Col::text);
        l.setColour (juce::Label::backgroundColourId, Col::panel2);
        l.setBounds (10 + i * 59, 122, 54, 18);
        l.setTooltip ("Double-clic pour renommer");
        l.onTextChange = [this, i]
        {
            proc.macroNames.set (i, macroLabels[i].getText().toUpperCase());
            ++proc.uiChangeCounter;
        };
        mp->addAndMakeVisible (l);
    }

    for (int i = 0; i < kNumSnapshots; ++i)
    {
        auto& b = snapBtns[i];
        b.setButtonText (juce::String (i + 1));
        b.setBounds (13 + i * 28, 156, 26, 22);
        b.setTooltip (juce::String::fromUTF8 ("Snapshot de macros : clic = rappel (avec morphing), Maj+clic = mémoriser"));
        b.onClick = [this, i]
        {
            if (juce::ModifierKeys::currentModifiers.isShiftDown()) proc.storeSnapshot (i);
            else proc.recallSnapshot (i);
        };
        mp->addAndMakeVisible (b);
    }
    auto hint = std::make_unique<juce::Label>();
    hint->setText (juce::String::fromUTF8 ("SNAPSHOTS  -  Maj+clic : mémoriser"), juce::dontSendNotification);
    hint->setFont (font (10.0f, false));
    hint->setColour (juce::Label::textColourId, Col::dim);
    hint->setJustificationType (juce::Justification::centred);
    hint->setBounds (8, 184, 234, 20);
    own (std::move (hint), *mp);

    auto* sc = panel (root, "SCOPE", { 952, 308, 240, 220 }, Col::acid);
    own (std::make_unique<ScopeDisplay> (proc), *sc)->setBounds (8, 30, 224, 182);
}

// ---------------------------------------------------------------- page modulation
void FragorEditor::buildModPage()
{
    auto* lp = panel (modPage, "LFO", { 8, 0, 560, 220 }, Col::violet);
    for (int i = 0; i < 3; ++i)
    {
        lfoTabs[i] = chip (*lp, "LFO " + juce::String (i + 1), srcLfo1 + i, { 46 + i * 56, 5, 54, 22 }, Col::violet);
        lfoTabs[i]->onClick = [this, i] { selectLfo (i); };

        const juce::String p = "l" + juce::String (i + 1);
        auto* shp = choice (*lp, p + "Shape");      shp->setBounds (216, 5, 96, 22);
        auto* syn = toggle (*lp, p + "Sync", "SYNC", Col::violet);   syn->setBounds (316, 6, 62, 20);
        auto* div = choice (*lp, p + "Div");        div->setBounds (382, 5, 76, 22);
        auto* ret = toggle (*lp, p + "Retrig", "RETRIG", Col::violet); ret->setBounds (462, 6, 90, 20);
        auto* rate = knob (*lp, p + "Rate", "RATE (Hz)", Col::violet);
        auto* ph   = knob (*lp, p + "Phase", "PHASE", Col::violet);
        auto* cv   = knob (*lp, p + "Curve", "CURVE", Col::violet);
        auto* sm   = knob (*lp, p + "Smooth", "SMOOTH", Col::violet);
        layoutGrid ({ rate, ph, cv, sm }, { 8, 32, 232, 64 }, 4, 58, 64);
        lfoControls[i] = { shp, syn, div, ret, rate, ph, cv, sm };
    }
    lfoDisplay = own (std::make_unique<LfoDisplay> (proc, 0), *lp);
    lfoDisplay->setBounds (246, 34, 306, 60);
    lfoDisplay->onEdit = [this] { curveOverlay->open (currentLfo); };
    auto* edit = own (std::make_unique<juce::TextButton> ("EDIT"), *lp);
    edit->setBounds (500, 37, 48, 18);
    edit->setTooltip (juce::String::fromUTF8 ("Éditeur de courbe (forme CUSTOM)"));
    edit->onClick = [this] { curveOverlay->open (currentLfo); };

    toggle (*lp, "seqOn", "RATE SEQ", Col::hot)->setBounds (8, 104, 112, 20);
    toggle (*lp, "seqReset", "PHASE RST", Col::hot)->setBounds (8, 128, 112, 20);
    auto* len = choice (*lp, "seqLen");
    len->setBounds (8, 152, 54, 22);
    len->box.setTooltip (juce::String::fromUTF8 ("Durée d'un pas"));
    knob (*lp, "seqSteps", "STEPS", Col::hot)->setBounds (64, 148, 56, 66);
    auto* grid = own (std::make_unique<SeqGrid> (proc), *lp);
    grid->setBounds (126, 102, 426, 112);
    grid->setTooltip (juce::String::fromUTF8 ("LFO 1 : vitesse par pas. Glisser = régler, clic droit = choisir. '--' = fermé, OPEN = ouvert"));

    selectLfo (0);

    auto* mp = panel (modPage, "MOD MATRIX", { 576, 0, 616, 220 }, Col::violet);
    struct C { const char* t; int s; };
    const C chips[] = { { "VEL", srcVelocity }, { "MW", srcModWheel }, { "AT", srcAftertouch }, { "SLIDE", srcSlide },
                        { "NOTE", srcNote }, { "RND", srcRandom }, { "P.ENV", srcPitchEnv } };
    int x = 128;
    for (auto& c : chips)
    {
        chip (*mp, c.t, c.s, { x, 5, 58, 20 }, Col::violet);
        x += 62;
    }
    for (int i = 0; i < kNumMatrixSlots; ++i)
        own (std::make_unique<ModSlot> (proc.apvts, i + 1), *mp)->setBounds (6 + (i / 6) * 304, 30 + (i % 6) * 30, 300, 28);
}

void FragorEditor::selectLfo (int i)
{
    currentLfo = i;
    for (int l = 0; l < 3; ++l)
    {
        if (lfoTabs[l]) { lfoTabs[l]->selected = l == i; lfoTabs[l]->repaint(); }
        for (auto* c : lfoControls[l]) c->setVisible (l == i);
    }
    if (lfoDisplay) lfoDisplay->setIndex (i);
}

// ---------------------------------------------------------------- page effets
void FragorEditor::buildFxPage()
{
    own (std::make_unique<FxChainStrip> (proc), fxPage)->setBounds (8, 0, kW - 16, 24);

    int x = 8;
    auto next = [&x] (int w) { juce::Rectangle<int> r (x, 28, w, 192); x += w + 8; return r; };

    for (int k = 0; k < 2; ++k)
    {
        const juce::String p = k == 0 ? "dist" : "dist2";
        auto* d = panel (fxPage, k == 0 ? "DIST 1" : "DIST 2", next (160), Col::hot);
        toggle (*d, p + "On", "ON", Col::hot)->setBounds (70, 6, 44, 20);
        choice (*d, p + "Mode")->setBounds (8, 30, 144, 22);
        layoutGrid ({ knob (*d, p + "Drive", "DRIVE", Col::hot), knob (*d, p + "Bias", "BIAS", Col::hot), knob (*d, p + "Tone", "TONE", Col::hot),
                      knob (*d, p + "Mix", "MIX", Col::hot) }, { 8, 56, 144, 132 }, 3, 48, 66);
    }

    auto* c = panel (fxPage, "CRUSH", next (105), Col::hot);
    toggle (*c, "crushOn", "ON", Col::hot)->setBounds (64, 6, 38, 20);
    layoutGrid ({ knob (*c, "crushBits", "BITS", Col::hot), knob (*c, "crushRate", "RATE", Col::hot), knob (*c, "crushMix", "MIX", Col::hot) },
                { 6, 32, 94, 152 }, 2, 46, 76);

    auto* o = panel (fxPage, "OTT", next (170), Col::acid);
    toggle (*o, "ottOn", "ON")->setBounds (50, 6, 44, 20);
    layoutGrid ({ knob (*o, "ottDepth", "DEPTH"), knob (*o, "ottTime", "TIME"), knob (*o, "ottOut", "OUT"),
                  knob (*o, "ottUp", "UP"), knob (*o, "ottDown", "DOWN") }, { 6, 32, 158, 152 }, 3, 52, 76);

    auto* ch = panel (fxPage, "CHORUS", next (115), Col::violet);
    toggle (*ch, "chorusOn", "ON", Col::violet)->setBounds (74, 6, 36, 20);
    layoutGrid ({ knob (*ch, "chorusRate", "RATE", Col::violet), knob (*ch, "chorusDepth", "DEPTH", Col::violet),
                  knob (*ch, "chorusMix", "MIX", Col::violet) }, { 6, 32, 94, 152 }, 2, 46, 76);

    auto* dl = panel (fxPage, "DELAY", next (150), Col::violet);
    toggle (*dl, "dlyOn", "ON", Col::violet)->setBounds (58, 6, 40, 20);
    toggle (*dl, "dlyPing", "PING", Col::violet)->setBounds (100, 6, 44, 20);
    choice (*dl, "dlyDiv")->setBounds (8, 30, 134, 22);
    layoutGrid ({ knob (*dl, "dlyFb", "FDBK", Col::violet), knob (*dl, "dlyTone", "TONE", Col::violet), knob (*dl, "dlyMix", "MIX", Col::violet) },
                { 8, 58, 135, 76 }, 3, 45, 76);

    auto* rv = panel (fxPage, "REVERB", next (125), Col::violet);
    toggle (*rv, "revOn", "ON", Col::violet)->setBounds (80, 6, 38, 20);
    layoutGrid ({ knob (*rv, "revSize", "SIZE", Col::violet), knob (*rv, "revDamp", "DAMP", Col::violet),
                  knob (*rv, "revWidth", "WIDTH", Col::violet), knob (*rv, "revMix", "MIX", Col::violet) }, { 8, 32, 110, 152 }, 2, 54, 76);

    auto* ms = panel (fxPage, "MASTER", next (135), Col::text);
    toggle (*ms, "subMono", "MONO BASS", Col::text)->setBounds (8, 30, 119, 20);
    layoutGrid ({ knob (*ms, "subMonoFreq", "FREQ", Col::text), knob (*ms, "width", "WIDTH", Col::text), knob (*ms, "clip", "CLIP", Col::hot),
                  knob (*ms, "masterGain", "GAIN", Col::acid) }, { 4, 54, 126, 132 }, 3, 42, 66);
}

void FragorEditor::showPage (int page)
{
    modPage.setVisible (page == 0);
    fxPage.setVisible (page == 1);
    modTab.setToggleState (page == 0, juce::dontSendNotification);
    fxTab.setToggleState (page == 1, juce::dontSendNotification);
}

// ---------------------------------------------------------------- rafraîchissement
void FragorEditor::refreshNames()
{
    presetBtn.setButtonText (proc.presetName);
    for (int i = 0; i < kNumMacros; ++i)
        if (! macroLabels[i].isBeingEdited())
            macroLabels[i].setText (proc.macroNames[i], juce::dontSendNotification);
    for (int i = 0; i < kNumSnapshots; ++i)
    {
        snapBtns[i].setColour (juce::TextButton::buttonColourId, proc.snapshotUsed (i) ? Col::hot.withAlpha (0.7f) : Col::panel2);
        snapBtns[i].setColour (juce::TextButton::textColourOffId, proc.snapshotUsed (i) ? Col::bg : Col::dim);
    }
    undoBtn.setEnabled (proc.canUndo());
    redoBtn.setEnabled (proc.canRedo());
}

void FragorEditor::timerCallback()
{
    const int c = proc.uiChangeCounter.load();
    if (c != lastUiCounter)
    {
        lastUiCounter = c;
        refreshNames();
        for (auto* k : modKnobs) k->repaint();
    }
    const bool playing = proc.activeVoices.load() > 0;
    static int tick = 0;
    if (playing || (++tick % 8) == 0)
        for (auto* k : modKnobs)
            if (k->isShowing() && k->hasMods()) k->repaint();

    voicesLabel.setText ("VOICES " + juce::String (proc.activeVoices.load()) + " | " + juce::String (proc.getSampleRate() / 1000.0, 1) + "k",
                         juce::dontSendNotification);
}
