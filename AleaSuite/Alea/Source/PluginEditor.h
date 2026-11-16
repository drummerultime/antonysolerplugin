#pragma once

#include "PluginProcessor.h"
#include "Gui/Widgets.h"
#include "../../Shared/Gui/LinkPanel.h"



// Toute l'interface, dessinee a taille fixe (W x H) puis mise a l'echelle par l'editeur
class MainView : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 1320, H = 840;

    explicit MainView (AleaAudioProcessor&);
    ~MainView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void selectTrack (int t);
    void setLockStep (int t, int i);
    bool dropFiles (const juce::StringArray& files, juce::Point<int> pos);
    void refreshAfterStateChange();

private:
    void timerCallback() override;
    void refreshTrackTools();
    void showTab (int i);
    void refreshPresetList();
    void stepPreset (int delta);
    void setupKnob (alea::gui::RndKnob& k, juce::Component& parent, const char* paramId, const juce::String& label,
                    bool bipolar = false, juce::Colour c = alea::gui::col::accent);
    void action (juce::TextButton& b, juce::Component& parent, bool undoable, std::function<void()> fn);
    void attachCombo (juce::ComboBox& c, juce::Component& parent, const juce::String& paramId, const juce::StringArray& items);
    void attachToggle (juce::Button& b, juce::Component& parent, const juce::String& paramId);
    void layoutVoice(); void layoutMod(); void layoutFx(); void layoutPerf(); void layoutLab();
    void setupLab();
    void refreshLab();
    alea::TrackPat& editTrack();

    AleaAudioProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 700 };
    int lastEp = -1, previewTick = 0;
    int selected = 0, lockT = -1, lockI = -1, lockPat = -1, currentTab = 0;
    juce::Array<juce::File> userPresets;

    using SA = juce::SliderParameterAttachment;
    using CA = juce::ComboBoxParameterAttachment;
    using BA = juce::ButtonParameterAttachment;
    std::vector<std::unique_ptr<SA>> sliderAtts;
    std::vector<std::unique_ptr<CA>> comboAtts;
    std::vector<std::unique_ptr<BA>> buttonAtts;
    std::unique_ptr<juce::FileChooser> chooser;

    // barre du haut
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "Save" }, undoBtn { "Undo" }, redoBtn { "Redo" },
                     midiOutBtn { "MIDI Out" }, rndEditBtn { "RND EDIT" }, diceAllBtn { "DICE ALL" },
                     playBtn { "Play" }, songBtn { "Song" }, fillBtn { "FILL" };
    juce::ComboBox presetBox, copyPatBox, rndModeBox, distBox, scaleBox;
    juce::Slider bpmSlider, seedSlider;
    juce::TextButton patBtns[alea::kNumPatterns];
    juce::TextEditor chainEdit;
    alea::gui::DragMidiButton dragMidi;
    alea::gui::RndKnob chaosKnob, masterKnob;
    bool fillWasDown = false;

    // sequenceur
    alea::gui::TrackList trackList;
    alea::gui::StepGrid grid;
    alea::gui::LaneEditor lanes;
    alea::gui::RndKnob swingK, accentK, humanK, seqChaosK, ratChaosK, densityK;
    juce::Slider lenSlider, hitsSlider, rotSlider;
    juce::ComboBox rateBox, dirBox, lockCondBox;
    juce::TextButton euclidBtn { "Euclid" }, rndPatBtn { "Random" }, varyBtn { "Vary" }, mutPatBtn { "Mutate" },
                     shlBtn { "<" }, shrBtn { ">" }, clearBtn { "Clear" }, copyBtn { "Copy" }, pasteBtn { "Paste" },
                     rndAllPatBtn { "Rnd All" }, varyAllBtn { "Vary All" }, clearLocksBtn { "Clear locks" }, exitLockBtn { "Done" };

    // onglets
    static constexpr int kNumTabs = 7;
    juce::TextButton tabBtns[kNumTabs];
    alea::gui::PaintPanel voicePanel, modPanel, fxPanel, perfPanel, labPanel, helpPanel;
    std::unique_ptr<alea::gui::LinkPanel> linkPanel;
    juce::ComboBox rootBox, keyLockBox;
    juce::ToggleButton keyChordBtn { "Key lock = accords de Color" };
    juce::TextButton linkBtn { "LINK" };
    juce::ComboBox grooveBox;
    juce::TextButton previewBtn { "Preview" };

    // VOICE
    juce::ComboBox engineBox, ftypeBox, chokeBox, outBox;
    std::unique_ptr<CA> engineAtt, ftypeAtt, chokeAtt, outAtt;
    juce::ComboBox fxTypeBox;
    std::unique_ptr<CA> fxTypeAtt;
    juce::TextButton keepBtn { "Keep" };
    juce::TextButton engLockBtn { "Lock" }, diceBtn { "Dice" }, diceEngBtn { "Dice + Engine" }, mutVoiceBtn { "Mutate" },
                     rndJitBtn { "Rnd amounts" }, clrJitBtn { "Zero rnd" }, loadSmpBtn { "Load sample" }, clearSmpBtn { "x" };
    alea::gui::RndKnob voiceKnobs[alea::kNumVP];
    alea::gui::RndKnob velSensK, linkK;
    alea::gui::Scope scope;

    // MOD
    juce::ComboBox modRateBox[alea::kNumMods];
    alea::gui::RndKnob modPK[alea::kNumMods], modDepthK;
    std::unique_ptr<alea::gui::ModDisplay> modDisp[alea::kNumMods];
    juce::Slider turLenSlider;
    juce::ComboBox followTrkBox;
    juce::ComboBox mxSrc[alea::kNumMxSlots], mxTrk[alea::kNumMxSlots], mxDst[alea::kNumMxSlots];
    juce::Slider mxAmt[alea::kNumMxSlots];

    // FX
    juce::ComboBox dlyDivBox, stuDivBox, stuLenBox;
    juce::TextButton pingBtn { "Ping-pong" };
    alea::gui::RndKnob dlyFbK, dlyToneK, dlyRetK, dlyRndK, revSizeK, revDampK, revWidthK, revRetK, stuChanceK, glueK, mDriveK;
    alea::gui::RndKnob revPreK, duckAmtK, duckRelK;
    juce::ComboBox revTypeBox, duckSrcBox;
    juce::TextButton hqBtn { "HQ x2" };

    // PERFORM
    alea::gui::RndKnob morphK, densityK2, chaosK2, seqChaosK2, modDepthK2, stuChanceK2, duckK2;
    juce::TextButton storeBBtn { "Store B" }, recallBBtn { "Recall B" }, swapBBtn { "Swap A/B" }, fillLatchBtn { "Fill (latch)" };
    juce::TextButton sceneBtns[alea::kNumScenes], sceneStoreBtn { "Store" }, sceneXfOnBtn { "XF on" };
    juce::ComboBox sceneABox, sceneBBox, keysBox;
    alea::gui::RndKnob sceneXfK, glideK;

    // LAB
    juce::TextButton favBtns[alea::kNumFavs], favStoreBtn { "Store" }, keepBtn2 { "Keep last hit" }, breedVoiceBtn { "Breed" },
                     evoCommitBtn { "Commit" }, evoResetBtn { "Reset" }, breedPatBtn { "Breed" },
                     learnMidiBtn { "Learn MIDI file..." }, styleTrackBtn { "Style > track" }, styleAllBtn { "Style > all" };
    juce::ComboBox breedA, breedB, evoBarsBox, evoTargetBox, patA, patB, patDst;
    alea::gui::RndKnob evoK;

    juce::Rectangle<int> seqArea, lockArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};

class AleaAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    explicit AleaAudioProcessorEditor (AleaAudioProcessor&);
    ~AleaAudioProcessorEditor() override = default;

    void resized() override;
    void selectTrack (int t) { view.selectTrack (t); }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    AleaAudioProcessor& proc;
    MainView view;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AleaAudioProcessorEditor)
};
