#pragma once

#include "GrProcessor.h"
#include "Gui/GuiCommon.h"

namespace gr
{
using APVTS = juce::AudioProcessorValueTreeState;

class GrLookAndFeel : public juce::LookAndFeel_V4
{
public:
    GrLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

// ---------------------------------------------------------------------------
class SlotPanel : public juce::Component
{
public:
    SlotPanel (GrotProcessor&, int slot);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void rebind();
    GrotProcessor& proc;
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
    explicit ModPage (GrotProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void updateDestNames();
private:
    GrotProcessor& proc;
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
    explicit MacroPage (GrotProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    GrotProcessor& proc;
    alea::gui::RndKnob knobs[kNumMacros];
    juce::String routing[kNumMacros];
};

class ScenePage : public juce::Component
{
public:
    explicit ScenePage (GrotProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    GrotProcessor& proc;
    juce::TextButton scene[kNumScenes], storeBtn { "MEMORISER" }, varBtn { "8 VARIATIONS" };
    alea::gui::RndKnob morph;
};

class SourcePage : public juce::Component
{
public:
    explicit SourcePage (GrotProcessor&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    GrotProcessor& proc;
    alea::gui::RndKnob sens, hitDec;
    juce::TextButton adapt { "ADAPTER LA CHAINE A LA SOURCE" };
    int lastHits = 0;
    double rateStart = 0; int rateHits = 0; float hitsPerMin = 0;
};

// ---------------------------------------------------------------------------
class GrotView : public juce::Component, private juce::Timer
{
public:
    explicit GrotView (GrotProcessor&);
    ~GrotView() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void fillPresetBox();
    void savePreset();
    void loadPresetFile (const juce::File&);
    juce::File presetDir() const;

    GrotProcessor& proc;
    GrLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::ComboBox presetBox, sourceBox;
    std::unique_ptr<APVTS::ComboBoxAttachment> sourceAtt;
    juce::TextButton adaptBtn { "ADAPTER" };
    float hitFlash = 0; int lastHits = 0;
    SourcePage* sourcePage = nullptr;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrotView)
};

// ---------------------------------------------------------------------------
// Fenetre du plugin : contient la vue a sa taille de reference et la met a l'echelle (redimensionnable)
class GrotEditor : public juce::AudioProcessorEditor
{
public:
    explicit GrotEditor (GrotProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (alea::gui::col::bg); }
private:
    GrotProcessor& proc;
    GrotView view;
    int baseW = 1, baseH = 1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrotEditor)
};
} // namespace gr
