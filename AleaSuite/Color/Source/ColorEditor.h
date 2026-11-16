#pragma once

#include "ColorProcessor.h"
#include "Gui/ChordWidgets.h"
#include "../../Shared/Gui/LinkPanel.h"

class ColorView : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 1320, H = 900;
    explicit ColorView (ColorAudioProcessor&);
    ~ColorView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool dropFiles (const juce::StringArray& files);
    void refreshAfterStateChange();
    void setLockStep (int i);
    void showTab (int i);

private:
    void timerCallback() override;
    void refreshTools();
    void refreshProgText();
    void refreshPresetList();
    void stepPreset (int delta);
    void refreshLab();
    void harmonyChanged();
    void setupKnob (alea::gui::RndKnob& k, juce::Component& parent, const juce::String& paramId, const juce::String& label,
                    bool bipolar = false, juce::Colour c = color::gui::colorCol);
    void action (juce::TextButton& b, juce::Component& parent, bool undoable, std::function<void()> fn);
    void attachCombo (juce::ComboBox& c, juce::Component& parent, const juce::String& paramId, const juce::StringArray& items);
    void attachToggle (juce::Button& b, juce::Component& parent, const juce::String& paramId);
    void attachInc (juce::Slider& s, juce::Component& parent, const juce::String& paramId, const juce::String& suffix);
    color::CPattern& editPat();

    ColorAudioProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    juce::TooltipWindow tooltips { this, 700 };
    int currentTab = 0, lastEp = -1, previewTick = 0, lockI = -1, lockPat = -1;
    int lastProgHash = 0;
    juce::Array<juce::File> userPresets;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAtts;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboAtts;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAtts;

    // barre du haut
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "Save" }, undoBtn { "Undo" }, redoBtn { "Redo" },
                     midiOutBtn { "MIDI Out" }, rndEditBtn { "RND EDIT" }, diceBtnTop { "DICE" },
                     playBtn { "Play" }, songBtn { "Song" }, fillBtn { "FILL" }, linkBtn { "LINK" };
    juce::ComboBox presetBox, copyPatBox, rndModeBox, distBox, rootBox, modeBox;
    juce::Slider bpmSlider, seedSlider;
    juce::TextButton patBtns[color::kPatterns];
    juce::TextEditor chainEdit;
    alea::gui::DragMidiButton dragMidi;
    alea::gui::RndKnob chaosKnob, masterKnob;
    bool fillWasDown = false;

    // harmonie (zone principale)
    color::gui::ChordLane lane;
    color::gui::VoicingView voicingView;
    juce::TextEditor progEdit;
    juce::Slider keyShiftSlider;
    juce::ComboBox styleBox, reharmBox, cadenceBox, playModeBox, voicingBox;
    juce::TextButton genProgBtn { "Generate" }, reharmBtn { "Reharm" }, varyProgBtn { "Vary" }, simplifyBtn { "Simplify" }, enrichBtn { "Enrich" },
                     trDnBtn { "T-" }, trUpBtn { "T+" }, rotLBtn { "<" }, rotRBtn { ">" }, revProgBtn { "Rev" },
                     copyHarmBtn { "Copy" }, pasteHarmBtn { "Paste" };

    // rythme
    color::gui::RhythmGrid grid;
    alea::gui::RndKnob swingK, humanK, seqChaosK, ratChaosK, densityK, voicChaosK;
    juce::Slider lenSlider, hitsSlider;
    juce::ComboBox rateBox, dirBox, grooveBox, rhythmBox, lockCondBox;
    juce::TextButton rhythmApplyBtn { "Apply" }, rhythmGenBtn { "Gen rhythm" }, mutRBtn { "Mutate" }, shlBtn { "<" }, shrBtn { ">" },
                     euclidBtn { "Euclid" }, clearBtn { "Clear" }, copyBtn { "Copy pat" }, pasteBtn { "Paste pat" },
                     previewBtn { "Preview" }, clearLocksBtn { "Clear locks" }, exitLockBtn { "Done" };

    // onglets
    static constexpr int kNumTabs = 9;
    juce::TextButton tabBtns[kNumTabs];
    alea::gui::PaintPanel synthPanel, harmPanel, modPanel, fxPanel, perfPanel, labPanel, helpPanel;
    std::unique_ptr<color::gui::TheoryView> theoryView;
    std::unique_ptr<alea::gui::LinkPanel> linkPanel;

    // SYNTH
    juce::ComboBox osc1Box, osc2Box, subBox, filtBox, driveBox, glideBox, layerBox;
    alea::gui::RndKnob layerLvlK, layerDecK;
    juce::TextButton keepBtn { "Keep" }, diceBtn { "Dice" }, diceTypesBtn { "Dice + Types" }, mutBtn { "Mutate" },
                     rndAmtBtn { "Rnd amounts" }, zeroRndBtn { "Zero rnd" }, lockTypesBtn { "Lock types" }, auditionBtn { "Play chord" };
    alea::gui::RndKnob tpKnobs[color::kNumTP];
    alea::gui::RndKnob velSensK, vdriftK, vspreadK, linkRndK;
    alea::gui::Scope scope;

    // HARMONIE (onglet)
    juce::ComboBox styleBox2, cycleBox, chordLenBox, voicingBox2, bassModeBox, playModeBox2, arpModeBox, keysBox2;
    juce::Slider nchordsSlider, voicesSlider, arpOctSlider;
    alea::gui::RndKnob complexK, tensionK, chromaK, outgoingK, liveSubK, approachK, registerK, spreadK, smoothK, bassLvlK, strumK, voicChaosK2;

    // MOD
    juce::ComboBox modRateBox[alea::kNumMods], followTrkBox;
    alea::gui::RndKnob modPK[alea::kNumMods], modDepthK;
    std::unique_ptr<alea::gui::ModDisplay> modDisp[alea::kNumMods];
    juce::Slider turLenSlider;
    juce::ComboBox mxSrc[color::kMxSlots], mxDst[color::kMxSlots];
    juce::Slider mxAmt[color::kMxSlots];

    // FX
    juce::ComboBox dlyDivBox, duckSrcBox;
    juce::TextButton pingBtn { "Ping-pong" };
    alea::gui::RndKnob chRateK, chDepthK, chMixK, dlyFbK, dlyToneK, dlyRetK, dlyRndK, revSizeK, revDampK, revWidthK, revRetK, revPreK,
                       duckAmtK, duckRelK, glueK, mDriveK;

    // PERFORM
    alea::gui::RndKnob morphK, densityK2, chaosK2, liveSubK2, tensionK2, duckK2, evoK2, sceneXfK;
    juce::TextButton storeBBtn { "Store B" }, recallBBtn { "Recall B" }, swapBBtn { "Swap A/B" }, fillLatchBtn { "Fill (latch)" },
                     sceneBtns[color::kScenes], sceneStoreBtn { "Store" }, sceneXfOnBtn { "XF on" };
    juce::ComboBox sceneABox, sceneBBox, keysBox;

    // LAB
    alea::gui::RndKnob evoK, reharmAmtK;
    juce::TextButton favBtns[color::kFavs], favStoreBtn { "Store" }, keepBtn2 { "Keep last" }, breedVoiceBtn { "Breed" },
                     evoCommitBtn { "Commit" }, evoResetBtn { "Reset" }, breedPatBtn { "Breed" },
                     learnMidiBtn { "Analyser un fichier MIDI..." }, reharmAmtBtn { "Reharmoniser (dose)" }, genAllBtn { "Generer A-H" },
                     lengthsBtn { "Durees = reglage" }, exportBtn { "Exporter MIDI (8 mesures)" },
                     melodyBtn { "Harmoniser une melodie MIDI..." }, drumRhythmBtn { "Rythme d'Alea" };
    juce::ComboBox drumRhythmBox;
    juce::ComboBox breedA, breedB, evoBarsBox, evoTargetBox, patA, patB, patDst, modTargetBox, modMethodBox;
    juce::TextButton modBtn { "Moduler > pattern suivant" };

    juce::Rectangle<int> knobArea, lockArea;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorView)
};

class ColorAudioProcessorEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget
{
public:
    explicit ColorAudioProcessorEditor (ColorAudioProcessor&);
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override { view.dropFiles (files); }
    ColorView& getView() { return view; }
private:
    ColorAudioProcessor& proc;
    ColorView view;
};
