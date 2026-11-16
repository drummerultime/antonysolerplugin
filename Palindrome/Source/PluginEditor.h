#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "PluginProcessor.h"
#include "Visuals.h"

//==============================================================================
// Barre d'onglets colorés
class TabBar : public juce::Component
{
public:
    struct Tab { juce::String name; juce::Colour accent; };
    void setTabs (std::vector<Tab> t) { tabs = std::move (t); repaint(); }
    void setCurrent (int i) { current = i; repaint(); }
    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent& e) override { hover = tabAt (e.position.x); repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { hover = -1; repaint(); }
private:
    int tabAt (float x) const { return tabs.empty() ? -1 : juce::jlimit (0, (int) tabs.size() - 1, (int) (x / ((float) getWidth() / (float) tabs.size()))); }
    std::vector<Tab> tabs;
    int current = 0, hover = -1;
};

//==============================================================================
// Comparaison A / B
class ABSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ABSwitch (PalindromeProcessor& p) : processor (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    PalindromeProcessor& processor;
};

class PalindromeUI;

//==============================================================================
// Mode MIDI learn : chaque réglage assignable est entouré ; clic = choisir, puis bouger la pédale
class LearnOverlay : public juce::Component
{
public:
    explicit LearnOverlay (PalindromeUI& u);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void open();
    void close();
    int lastLearnVersion = 0;

private:
    juce::Rectangle<int> banner() const;
    bool targetVisible (juce::Component*) const;
    PalindromeUI& ui;
    juce::TextButton forgetAll { u8"Tout oublier" }, done { "Terminer" };
    juce::String hoverTarget;
    static juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
};

//==============================================================================
// Toute l'interface, dessinée à une taille logique puis mise à l'échelle.
// La taille logique s'adapte aux proportions de la fenêtre (les zones souples s'agrandissent).
class PalindromeUI : public juce::Component, private juce::Timer
{
public:
    explicit PalindromeUI (PalindromeProcessor&);
    ~PalindromeUI() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    static constexpr int baseWidth = 1280, baseHeight = 820;

    void showPage (int page);
    void refreshAll() { timerCallback(); }
    void setLearnMode (bool on);
    bool isLearnMode() const { return overlay.isVisible(); }

    std::function<void (float)> onSizeChosen;   // échelle choisie dans le menu

    // cibles MIDI learn visibles
    struct LearnTarget { juce::Component* comp; juce::String id; };
    std::vector<LearnTarget> learnTargets;
    PalindromeProcessor& processor;

private:
    friend class LearnOverlay;
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::function<bool()> visibleIf;
        bool shown() const { return ! visibleIf || visibleIf(); }
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Group
    {
        juce::String title;
        int width = 0;                                  // 0 = place restante
        juce::Colour accent;
        std::vector<SegmentedControl*> segs;
        std::vector<Choice*> choices;
        juce::TextEditor* field = nullptr;              // champ texte (sous les listes)
        juce::String fieldCaption;
        std::function<bool()> fieldVisibleIf;
        std::vector<Knob*> knobs;
        Toggle* toggle = nullptr;
        juce::Button* button = nullptr;                 // bouton en bas du groupe
        ParamView* visual = nullptr;
        bool slotGrid = false;
        std::vector<juce::Rectangle<int>> cards;        // colonnes de la grille par emplacement
        int segColumns = 1;
        juce::Rectangle<int> area;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> captions;   // titres des segments
        bool fieldShown() const { return field != nullptr && (! fieldVisibleIf || fieldVisibleIf()); }
    };

    void setupKnob (Knob&, const juce::String& paramID, const juce::String& name, juce::Colour accent,
                    const juce::String& tip, bool bipolar = false);
    void setupChoice (Choice&, const juce::String& paramID, const juce::String& name, juce::Colour accent, const juce::String& tip);
    void setupToggle (Toggle&, const juce::String& paramID, const juce::String& name, juce::Colour accent, const juce::String& tip);
    SegmentedControl* makeSeg (const juce::String& paramID, juce::StringArray labels, const juce::String& caption,
                               juce::Colour accent, const juce::String& tip);
    void layoutGroup (Group&);
    void refreshPresetList();
    void savePresetDialog();
    void stepPreset (int delta);
    void showSizeMenu();
    bool bankMode() const;
    static juce::Rectangle<float> cardLock (juce::Rectangle<int> card);

    PalLookAndFeel lnf;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    // en-tête
    IconButton holdButton { "HOLD", IconButton::Icon::Hold, pal::theme::hold };
    IconButton nextButton { "SUIVANT", IconButton::Icon::Next, pal::theme::cycle };
    IconButton captureButton { "CAPTURER", IconButton::Icon::Record, pal::theme::record };
    IconButton clearButton { "VIDER", IconButton::Icon::Clear, pal::theme::dim };
    IconButton prevPreset { "", IconButton::Icon::Prev, pal::theme::dim };
    IconButton nextPreset { "", IconButton::Icon::Forward, pal::theme::dim };
    IconButton savePresetButton { "", IconButton::Icon::Save, pal::theme::dim };
    IconButton undoButton { "", IconButton::Icon::Undo, pal::theme::dim };
    IconButton redoButton { "", IconButton::Icon::Redo, pal::theme::dim };
    IconButton copyABButton { "", IconButton::Icon::Copy, pal::theme::dim };
    IconButton midiButton { "MIDI", IconButton::Icon::Midi, pal::theme::midi };
    IconButton sizeButton { "", IconButton::Icon::Size, pal::theme::dim };
    IconButton tapButton { "TAP", IconButton::Icon::Tap, pal::theme::cycle };
    ABSwitch abSwitch;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAttachment;
    juce::ComboBox presetBox;
    juce::Array<juce::File> userPresets;
    int lastMidiActivity = 0;
    float midiFlash = 0.0f;
    juce::Rectangle<float> midiLed;

    // scène
    ActivityView activity;
    PhraseView phrase;
    BankPanel bank;
    TabBar tabBar;

    // visuels des sections
    CyclePlanView planView;
    TempoView tempoView;
    NotesPreviewView notesView;
    DirectionView directionView;
    RhythmView rhythmView;
    AnalysisView analysisView;
    KeyboardView keyboardView;
    TransposeView transposeView;
    EchoView echoView;
    StereoView stereoView;
    DiffusionView diffusionView;
    DuckView duckView;
    EnvelopeView envelopeView;
    TextureView textureView;
    OutputView outputView;

    std::vector<std::unique_ptr<SegmentedControl>> segs;
    SegmentedControl *segMode = nullptr, *segUnit = nullptr, *segFrom = nullptr, *segGrid = nullptr, *segRecap = nullptr,
                     *segQuant = nullptr, *segStereo = nullptr, *segScope = nullptr, *segDir = nullptr, *segPattern = nullptr,
                     *segTempo = nullptr, *segPlay = nullptr;

    Choice order, scale, speedMode;
    juce::TextEditor dirSeqEditor;
    Knob threshold, phraseLen, delay, phraseBeats, delayBeats, repetitions, repeatGap, gapBeats,
         silenceMin, silenceMax, silMinBeats, silMaxBeats, internalBpm;
    Knob randomness, noteReverse, skipProb, repeatProb, octaveProb, melodic, quantAmount, humanize, sensitivity, legato, fade;
    Toggle freeze;
    Knob correction, transpose, drift;
    Knob decay, damping, tone, lowCut, modDepth, modRate, drive, feedback;
    Knob width, diffusion, roomSize, duckAmount, duckRelease;
    Toggle diffusionGrow;
    Knob attack, attackProb;
    Knob textureMix, grainSize, grainDensity, grainSpray;
    std::array<Choice, PhraseEngine::maxBankSize> slotOrder;
    std::array<Knob, PhraseEngine::maxBankSize> slotTranspose, slotReps;
    Knob mix, wetGain;
    Toggle killDry;

    static constexpr int numPages = 9;
    std::array<std::vector<Group>, numPages> pages;
    Group outputGroup;
    int currentPage = 0;
    int lastMode = -1, lastOrder = -1;
    bool lastSync = false;
    juce::Rectangle<int> headerArea, pageArea;

    LearnOverlay overlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PalindromeUI)
};

//==============================================================================
// Éditeur redimensionnable librement : l'interface est mise à l'échelle, et la taille est mémorisée.
class PalindromeEditor : public juce::AudioProcessorEditor
{
public:
    explicit PalindromeEditor (PalindromeProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (pal::theme::bg); }

private:
    PalindromeProcessor& proc;
    PalindromeUI ui;
    bool ready = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PalindromeEditor)
};
