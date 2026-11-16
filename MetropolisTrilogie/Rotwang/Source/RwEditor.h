#pragma once

#include "RwProcessor.h"
#include "Gui/GuiCommon.h"

namespace rw
{
using APVTS = juce::AudioProcessorValueTreeState;

class RwLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RwLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

// ---------------------------------------------------------------------------
class SlotPanel : public juce::Component
{
public:
    SlotPanel (RotwangProcessor&, int slot);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebind();
    RotwangProcessor& proc;
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
    explicit ModPage (RotwangProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void updateDestNames();
private:
    RotwangProcessor& proc;
    std::unique_ptr<alea::gui::ModDisplay> disp[7];
    juce::ComboBox rate[kNumRMods];
    alea::gui::RndKnob modP[kNumRMods], envSens, envRel;
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
    explicit MacroPage (RotwangProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    RotwangProcessor& proc;
    alea::gui::RndKnob knobs[kNumMacros];
    juce::String routing[kNumMacros];
};

class ScenePage : public juce::Component
{
public:
    explicit ScenePage (RotwangProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    RotwangProcessor& proc;
    juce::TextButton scene[kNumScenes], storeBtn { "MEMORISER" }, varBtn { "8 VARIATIONS" };
    alea::gui::RndKnob morph;
};

// ---------------------------------------------------------------------------
class RotwangView : public juce::Component, private juce::Timer
{
public:
    explicit RotwangView (RotwangProcessor&);
    ~RotwangView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void fillPresetBox();
    void savePreset();
    void loadPresetFile (const juce::File&);
    juce::File presetDir() const;

    RotwangProcessor& proc;
    RwLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox presetBox;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RotwangView)
};

// ---------------------------------------------------------------------------
// Fenetre du plugin : contient la vue a sa taille de reference et la met a l'echelle (redimensionnable)
class RotwangEditor : public juce::AudioProcessorEditor
{
public:
    explicit RotwangEditor (RotwangProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (alea::gui::col::bg); }
private:
    RotwangProcessor& proc;
    RotwangView view;
    int baseW = 1, baseH = 1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RotwangEditor)
};
} // namespace rw
