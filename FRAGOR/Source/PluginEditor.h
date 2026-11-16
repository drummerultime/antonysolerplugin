#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ui/FragorLookAndFeel.h"
#include "ui/Widgets.h"
#include "ui/Overlays.h"

class FragorEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer
{
public:
    explicit FragorEditor (FragorProcessor&);
    ~FragorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kW = 1200, kH = 840;

    // accès pour l'outil de capture
    void showPage (int page);
    void openCurveEditor (int lfo) { curveOverlay->open (lfo); }
    void openBrowser() { browser->open(); }

private:
    struct Root : public juce::Component
    {
        void paint (juce::Graphics&) override;
    };

    void timerCallback() override;
    void buildHeader();
    void buildOsc (int o);
    void buildSubVoice();
    void buildFilterEnv();
    void buildMacros();
    void buildModPage();
    void buildFxPage();
    void selectLfo (int i);
    void selectFilter (int i);
    void refreshNames();

    fragor::Knob*   knob   (juce::Component& parent, const juce::String& id, const juce::String& label, juce::Colour c = fragor::Col::acid);
    fragor::Choice* choice (juce::Component& parent, const juce::String& id, const juce::String& label = {});
    fragor::Toggle* toggle (juce::Component& parent, const juce::String& id, const juce::String& text, juce::Colour c = fragor::Col::acid);
    fragor::Panel*  panel  (juce::Component& parent, const juce::String& title, juce::Rectangle<int> bounds, juce::Colour c = fragor::Col::acid);
    fragor::DragChip* chip (juce::Component& parent, const juce::String& text, int source, juce::Rectangle<int> b, juce::Colour c);
    template <typename T> T* own (std::unique_ptr<T> c, juce::Component& parent)
    {
        auto* raw = c.get();
        parent.addAndMakeVisible (raw);
        owned.push_back (std::move (c));
        return raw;
    }

    FragorProcessor& proc;
    fragor::FragorLookAndFeel lnf;
    Root root;
    juce::TooltipWindow tooltips { &root, 700 };

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<fragor::Knob*> modKnobs;

    // en-tête
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, presetBtn, saveBtn { "SAVE" }, loadBtn { "LOAD" },
                     undoBtn { "UNDO" }, redoBtn { "REDO" }, setBtn { "SET" };
    juce::ComboBox genStyle;
    juce::TextButton genBtn { "GENERATE" }, mutateBtn { "MUTATE" };
    juce::Label voicesLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    // macros & snapshots
    juce::Label macroLabels[fragor::kNumMacros];
    juce::TextButton snapBtns[fragor::kNumSnapshots];

    // pages
    juce::TextButton modTab { "MODULATION" }, fxTab { "EFFECTS" };
    juce::Component modPage, fxPage;

    // LFO / filtres
    fragor::DragChip* lfoTabs[3] {};
    std::vector<juce::Component*> lfoControls[3];
    fragor::LfoDisplay* lfoDisplay = nullptr;
    int currentLfo = 0;
    juce::TextButton fltTabs[2];
    std::vector<juce::Component*> fltControls[2];

    std::unique_ptr<juce::MidiKeyboardComponent> keyboard;
    std::unique_ptr<fragor::CurveOverlay> curveOverlay;
    std::unique_ptr<fragor::PresetBrowser> browser;
    std::unique_ptr<fragor::SettingsOverlay> settings;
    std::unique_ptr<fragor::SaveOverlay> saveOverlay;
    int lastUiCounter = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FragorEditor)
};
