#pragma once

#include "MaProcessor.h"
#include "Gui/GuiCommon.h"

namespace ma
{
using APVTS = juce::AudioProcessorValueTreeState;

class MaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MaLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

// ---------------------------------------------------------------------------
class SlotPanel : public juce::Component
{
public:
    SlotPanel (MariaProcessor&, int slot);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebind();
    MariaProcessor& proc;
    const int s;
    juce::ComboBox type;
    juce::TextButton on { "ON" }, lockBtn, diceBtn { "DE" }, left { "<" }, right { ">" };
    std::unique_ptr<APVTS::ComboBoxAttachment> typeAtt;
    std::unique_ptr<APVTS::ButtonAttachment> onAtt;
    alea::gui::RndKnob knobs[kNumSP];
    alea::gui::PaintPanel activity;
    std::array<float, 80> hist {};
    int histPos = 0, shownType = -1;
    bool lastLock = false;
};

// ---------------------------------------------------------------------------
class ModPage : public juce::Component
{
public:
    explicit ModPage (MariaProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void updateDestNames();
private:
    MariaProcessor& proc;
    std::unique_ptr<alea::gui::ModDisplay> disp[8];
    juce::ComboBox rate[kNumRMods];
    alea::gui::RndKnob modP[kNumRMods], envSens, envRel, hitDec;
    std::unique_ptr<APVTS::ComboBoxAttachment> rateAtt[kNumRMods];
    juce::ComboBox src[kNumRows], dst[kNumRows];
    juce::Slider amt[kNumRows];
    std::unique_ptr<APVTS::ComboBoxAttachment> srcAtt[kNumRows], dstAtt[kNumRows];
    std::unique_ptr<APVTS::SliderAttachment> amtAtt[kNumRows];
    int typesKey = -1;
};

class MacroPage : public juce::Component
{
public:
    explicit MacroPage (MariaProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    MariaProcessor& proc;
    alea::gui::RndKnob knobs[kNumMacros];
    juce::String routing[kNumMacros];
};

class ScenePage : public juce::Component
{
public:
    explicit ScenePage (MariaProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    MariaProcessor& proc;
    juce::TextButton scene[kSceneBank], storeBtn { "MEMORISER" }, varBtn { "8 VARIATIONS" }, bankPrev { "<" }, bankNext { ">" };
    juce::Label bankLbl;
    int shownBank = -1;
    int sceneAt (int i) const { return proc.sceneBank.load() * kSceneBank + i; }
    alea::gui::RndKnob morph;
};

// Vue de la memoire : forme d'onde, evenements, arcs de l'oracle, tete de lecture de Maria
class OracleView : public juce::Component
{
public:
    explicit OracleView (MariaProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void refresh() { repaint(); }
private:
    MariaProcessor& proc;
    std::vector<std::pair<int, int>> trail;   // derniers sauts
    int lastJumpTo = -1;
};

class ImproPanel : public juce::Component
{
public:
    explicit ImproPanel (MariaProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    MariaProcessor& proc;
    juce::ComboBox mode;
    std::unique_ptr<APVTS::ComboBoxAttachment> modeAtt;
    juce::TextButton learnBtn { "ECOUTE" }, phraseBtn { "PHRASE !" }, silenceBtn { "SILENCE" }, clearBtn { "EFFACER" };
    std::unique_ptr<APVTS::ButtonAttachment> learnAtt;
    juce::ComboBox tempoBox, gridBox, alignBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> tempoAtt, gridAtt, alignAtt;
    juce::TextButton tapBtn { "TAP" };
    std::vector<double> taps;
    juce::String tempoText;
    alea::gui::RndKnob knobs[kNumIm];
};

class OptionsPage : public juce::Component
{
public:
    explicit OptionsPage (MariaProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    MariaProcessor& proc;
    juce::TextButton v2Btn { "2e VOIX" }, autosaveBtn { "MEMOIRE LIEE AU SET" }, folderBtn { "OUVRIR LE DOSSIER" };
    juce::ComboBox v2Mode, tmode, selfBox, midiBox;
    std::unique_ptr<APVTS::ButtonAttachment> v2Att, autosaveAtt;
    std::unique_ptr<APVTS::ComboBoxAttachment> v2ModeAtt, tmodeAtt, selfAtt, midiAtt;
    alea::gui::RndKnob v2Level, v2Transp, v2Pan;
    int lastNotes = -1;
};

// Jeu a plusieurs : harmonie, leitmotivs, ecoute croisee, corpus, sorties
class EnsemblePage : public juce::Component
{
public:
    explicit EnsemblePage (MariaProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    MariaProcessor& proc;
    alea::gui::RndKnob harm, motVar, corpusMix;
    juce::ComboBox harmMode, scLearn, scReact, corpusSrc, outRoute;
    std::unique_ptr<APVTS::ComboBoxAttachment> harmModeAtt, scLearnAtt, scReactAtt, corpusSrcAtt, outRouteAtt;
    juce::TextButton markBtn[MariaProcessor::kNumMotifs], playBtn[MariaProcessor::kNumMotifs], delBtn[MariaProcessor::kNumMotifs];
    juce::TextButton corpusLoad { "CHARGER UN CORPUS" }, corpusClear { "RETIRER" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String stateKey;
};

// Forme : chapitres, arc d'intensite, pulsation, voix MIDI
class FormePage : public juce::Component
{
public:
    explicit FormePage (MariaProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    MariaProcessor& proc;
    juce::TextButton prevChap { "<" }, nextChap { ">" }, upBtn { "MONTER" }, downBtn { "DESCENDRE" }, neutralBtn { "NEUTRE" },
                     pulseBtn { "SUIVRE LA PULSATION" }, mvBtn { "VOIX MIDI" }, midiClear { "EFFACER MIDI" };
    juce::ComboBox chapPlay, chapPick, midiLearnBox, mvMode, mvCh;
    std::unique_ptr<APVTS::ComboBoxAttachment> chapPlayAtt, chapPickAtt, midiLearnAtt, mvModeAtt, mvChAtt;
    std::unique_ptr<APVTS::ButtonAttachment> mvAtt;
    alea::gui::RndKnob arcGoal, arcTime, arcDepth;
    juce::String stateKey;
};

// Machine (ostinato), nappes, retour d'etat MIDI, fichiers de session
class MachinePage : public juce::Component
{
public:
    explicit MachinePage (MariaProcessor&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void fillDevices();
    MariaProcessor& proc;
    juce::TextButton machBtn { "MODE MACHINE" }, droneBtn { "NAPPES" }, cleanBtn { "NETTOYER (> 30 JOURS)" }, folderBtn { "OUVRIR LE DOSSIER" }, refreshBtn { "ACTUALISER" };
    juce::ComboBox machLen, droneReg, fbCh, fbDev;
    std::unique_ptr<APVTS::ComboBoxAttachment> machLenAtt, droneRegAtt, fbChAtt;
    std::unique_ptr<APVTS::ButtonAttachment> droneAtt;
    alea::gui::RndKnob machMut, droneLevel, droneTime;
    juce::Array<juce::MidiDeviceInfo> devices;
    juce::String stateKey, cleanMsg;
    int folderFiles = 0; int64_t folderBytes = 0; int infoTick = 0;
};

class SourcePage : public juce::Component
{
public:
    explicit SourcePage (MariaProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    MariaProcessor& proc;
    alea::gui::RndKnob sens, hitDec;
    juce::TextButton adapt { "ADAPTER A LA SOURCE" }, loadBtn { "CHARGER UN FICHIER AUDIO" }, saveBtn { "SAUVER LA MEMOIRE (WAV)" }, clearBtn { "EFFACER LA MEMOIRE" };
    std::unique_ptr<juce::FileChooser> chooser;
    double rateStart = 0; int rateHits = 0; float hitsPerMin = -1;     // rythme des attaques (fenetre de 5 s)
};

// ---------------------------------------------------------------------------
class MariaView : public juce::Component, private juce::Timer
{
public:
    explicit MariaView (MariaProcessor&);
    ~MariaView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void fillPresetBox();
    void savePreset();
    void loadPresetFile (const juce::File&);
    juce::File presetDir() const;

    MariaProcessor& proc;
    MaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox presetBox, sourceBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> sourceAtt;
    juce::TextButton adaptBtn { "ADAPTER" };
    float hitFlash = 0; int lastHits = 0, frame = 0;
    SourcePage* sourcePage = nullptr;
    std::unique_ptr<OracleView> oracle;
    OptionsPage* optionsPage = nullptr;
    EnsemblePage* ensemblePage = nullptr;
    FormePage* formePage = nullptr;
    MachinePage* machinePage = nullptr;
    juce::TextButton recoverBtn { "RECHARGER LA MEMOIRE DE SECOURS" }, dismissBtn { "IGNORER" };
    std::unique_ptr<ImproPanel> improPanel;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "SAUVER" }, loadBtn { "OUVRIR" }, undoBtn { "UNDO" };
    juce::TextButton diceBtn { "DE" }, mutateBtn { "MUTATION" }, rndEditBtn { "RND EDIT" }, freezeBtn { "FREEZE" }, seedBtn { "NOUVELLE SEED" }, panicBtn { "PANIQUE" };
    std::unique_ptr<APVTS::ButtonAttachment> freezeAtt;
    juce::ComboBox clockBox, evoBarsBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> clockAtt, evoAtt;
    juce::Slider seedSlider;
    std::unique_ptr<APVTS::SliderAttachment> seedAtt;
    alea::gui::RndKnob chaosK, rateK, glideK, evolveK, mixK, inK, outK, widthK, duckK, bpmK;

    std::unique_ptr<SlotPanel> slots[kNumSlots];
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    ModPage* modPage = nullptr;
    MacroPage* macroPage = nullptr;
    ScenePage* scenePage = nullptr;
    juce::TextEditor help;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<juce::File> userFiles;
    int lastProgram = -1;
    float meterIn = 0, meterOut = 0;
    int64_t lastTick = -1;
    float tickFlash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MariaView)
};

// ---------------------------------------------------------------------------
// Fenetre du plugin : contient la vue a sa taille de reference et la met a l'echelle (redimensionnable)
class MariaEditor : public juce::AudioProcessorEditor
{
public:
    explicit MariaEditor (MariaProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (alea::gui::col::bg); }
private:
    MariaProcessor& proc;
    MariaView view;
    int baseW = 1, baseH = 1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MariaEditor)
};
} // namespace ma
