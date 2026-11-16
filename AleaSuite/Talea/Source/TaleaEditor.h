#pragma once

#include "TaleaProcessor.h"
#include "Gui/BassWidgets.h"
#include "../../Shared/Gui/LinkPanel.h"

class TaleaView : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 1320, H = 880;
    explicit TaleaView (TaleaAudioProcessor&);
    ~TaleaView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool dropFiles (const juce::StringArray& files);
    void refreshAfterStateChange();
    void setLockStep (int i);
    void showTab (int i);

private:
    void timerCallback() override;
    void refreshTools();
    void refreshPresetList();
    void stepPreset (int delta);
    void refreshLab();
    void setupKnob (alea::gui::RndKnob& k, juce::Component& parent, const juce::String& paramId, const juce::String& label,
                    bool bipolar = false, juce::Colour c = alea::gui::col::accent);
    void action (juce::TextButton& b, juce::Component& parent, bool undoable, std::function<void()> fn);
    void attachCombo (juce::ComboBox& c, juce::Component& parent, const juce::String& paramId, const juce::StringArray& items);
    void attachToggle (juce::Button& b, juce::Component& parent, const juce::String& paramId);
    talea::BPattern& editPat();

    TaleaAudioProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 700 };
    int currentTab = 0, lastEp = -1, previewTick = 0, lockI = -1, lockPat = -1;
    juce::Array<juce::File> userPresets;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAtts;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAtts;

    // barre du haut
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "Save" }, undoBtn { "Undo" }, redoBtn { "Redo" },
                     midiOutBtn { "MIDI Out" }, rndEditBtn { "RND EDIT" }, diceBtnTop { "DICE" },
                     playBtn { "Play" }, songBtn { "Song" }, fillBtn { "FILL" }, linkBtn { "LINK" };
    juce::ComboBox presetBox, copyPatBox, rndModeBox, distBox, rootBox, scaleBox, chordFollowBox;
    juce::Slider bpmSlider, seedSlider;
    juce::TextButton patBtns[talea::kPatterns];
    juce::TextEditor chainEdit;
    alea::gui::DragMidiButton dragMidi;
    alea::gui::RndKnob chaosKnob, masterKnob;
    bool fillWasDown = false;

    // sequenceur
    talea::gui::PianoRoll roll;
    talea::gui::BassLanes lanes;
    alea::gui::RndKnob swingK, humanK, seqChaosK, ratChaosK, densityK, noteChaosK;
    juce::Slider lenSlider, hitsSlider;
    juce::ComboBox rateBox, dirBox, grooveBox, genStyleBox, lockCondBox;
    juce::TextButton genBtn { "Generate" }, varyBtn { "Vary" }, mutNBtn { "Mut notes" }, mutRBtn { "Mut rhythm" },
                     trDnBtn { "T-" }, trUpBtn { "T+" }, ocDnBtn { "Oct-" }, ocUpBtn { "Oct+" }, revBtn { "Rev" }, invBtn { "Inv" },
                     shlBtn { "<" }, shrBtn { ">" }, clearBtn { "Clear" }, copyBtn { "Copy" }, pasteBtn { "Paste" }, euclidBtn { "Euclid" },
                     previewBtn { "Preview" }, clearLocksBtn { "Clear locks" }, exitLockBtn { "Done" };

    // onglets
    static constexpr int kNumTabs = 7;
    juce::TextButton tabBtns[kNumTabs];
    alea::gui::PaintPanel synthPanel, modPanel, fxPanel, perfPanel, labPanel, helpPanel;
    std::unique_ptr<alea::gui::LinkPanel> linkPanel;

    // SYNTH
    juce::ComboBox osc1Box, osc2Box, subBox, filtBox, driveBox, chordBox, glideBox;
    juce::Slider baseOctSlider;
    juce::TextButton keepBtn { "Keep" }, diceBtn { "Dice" }, diceTypesBtn { "Dice + Types" }, mutBtn { "Mutate" },
                     rndAmtBtn { "Rnd amounts" }, zeroRndBtn { "Zero rnd" }, lockTypesBtn { "Lock types" }, auditionBtn { "Play note" };
    alea::gui::RndKnob tpKnobs[talea::kNumTP];
    alea::gui::RndKnob velSensK, linkRndK;
    alea::gui::Scope scope;

    // MOD
    juce::ComboBox modRateBox[alea::kNumMods], followTrkBox;
    alea::gui::RndKnob modPK[alea::kNumMods], modDepthK;
    std::unique_ptr<alea::gui::ModDisplay> modDisp[alea::kNumMods];
    juce::Slider turLenSlider;
    juce::ComboBox mxSrc[talea::kMxSlots], mxDst[talea::kMxSlots];
    juce::Slider mxAmt[talea::kMxSlots];

    // FX
    juce::ComboBox dlyDivBox, duckSrcBox;
    juce::TextButton pingBtn { "Ping-pong" };
    alea::gui::RndKnob chRateK, chDepthK, chMixK, dlyFbK, dlyToneK, dlyRetK, dlyRndK, revSizeK, revDampK, revWidthK, revRetK, revPreK,
                       duckAmtK, duckRelK, glueK, mDriveK;

    // PERFORM
    alea::gui::RndKnob morphK, densityK2, chaosK2, seqChaosK2, modDepthK2, duckK2, evoK2, sceneXfK;
    juce::TextButton storeBBtn { "Store B" }, recallBBtn { "Recall B" }, swapBBtn { "Swap A/B" }, fillLatchBtn { "Fill (latch)" },
                     sceneBtns[talea::kScenes], sceneStoreBtn { "Store" }, sceneXfOnBtn { "XF on" };
    juce::ComboBox sceneABox, sceneBBox, keysBox;

    // LAB
    alea::gui::RndKnob genDensK, genRangeK, genAccK, genSlideK, genOctK, genRootK, evoK;
    juce::TextButton genBtn2 { "GENERATE" }, notesBtn { "Notes only" }, rhythmBtn { "Rhythm only" },
                     favBtns[talea::kFavs], favStoreBtn { "Store" }, keepBtn2 { "Keep last note" }, breedVoiceBtn { "Breed" },
                     evoCommitBtn { "Commit" }, evoResetBtn { "Reset" }, breedPatBtn { "Breed" },
                     learnMidiBtn { "Learn MIDI file..." }, styleGenBtn { "Style > pattern" },
                     followKickBtn { "Follow kick" }, avoidKickBtn { "Avoid kick" };
    juce::ComboBox genStyleBox2, breedA, breedB, evoBarsBox, evoTargetBox, patA, patB, patDst;

    juce::Rectangle<int> seqArea, lockArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TaleaView)
};

class TaleaAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    explicit TaleaAudioProcessorEditor (TaleaAudioProcessor&);
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override { view.dropFiles (files); }
    TaleaView& getView() { return view; }
private:
    TaleaAudioProcessor& proc;
    TaleaView view;
};
