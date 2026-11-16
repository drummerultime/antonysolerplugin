#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "PluginProcessor.h"

//==============================================================================
// Thème
namespace pal::theme
{
    const juce::Colour bg       { 0xff0e0f14 };
    const juce::Colour panel    { 0xff16181f };
    const juce::Colour panel2   { 0xff1d2029 };
    const juce::Colour outline  { 0xff2a2e3a };
    const juce::Colour text     { 0xffeceef3 };
    const juce::Colour dim      { 0xff8c90a1 };
    const juce::Colour faint    { 0xff545869 };

    const juce::Colour reverse  { 0xffff8a3d };   // orange : lecture reverse (partout)
    const juce::Colour normal   { 0xff3dc8ff };   // cyan   : lecture normale (partout)
    const juce::Colour record   { 0xffff4560 };
    const juce::Colour hold     { 0xffffc93d };
    const juce::Colour silence  { 0xff8b7cff };

    // une couleur par section
    const juce::Colour cycle    { 0xff8b7cff };
    const juce::Colour lecture  { 0xffe86bd8 };
    const juce::Colour tonal    { 0xff5bd69a };
    const juce::Colour echo     { 0xff3dc8ff };
    const juce::Colour espace   { 0xff6e9bff };
    const juce::Colour violon   { 0xffffb347 };
    const juce::Colour texture  { 0xffff6fa3 };
    const juce::Colour banque   { 0xffa78bfa };
    const juce::Colour sortie   { 0xffd7dae3 };
    const juce::Colour decoupe  { 0xff4fd1c5 };
    const juce::Colour midi     { 0xff5ee38a };

    juce::Font font (float size, bool bold = false);
}

namespace pal
{
    void drawLock (juce::Graphics&, juce::Rectangle<float> r, bool locked, juce::Colour c);
}

//==============================================================================
class PalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PalLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    juce::Font getLabelFont (juce::Label&) override;
};

//==============================================================================
// Choix court en boutons segmentés, relié à un paramètre "choix"
class SegmentedControl : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedControl (juce::AudioProcessorValueTreeState&, const juce::String& paramID,
                      juce::StringArray labels, juce::Colour accent);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    juce::String caption;                 // titre affiché au-dessus par le groupe
    std::function<bool()> visibleIf;
    bool shown() const { return ! visibleIf || visibleIf(); }

private:
    int segmentAt (float x) const;
    juce::RangedAudioParameter& param;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::StringArray labels;
    juce::Colour accent;
    int index = 0, hover = -1;
};

//==============================================================================
// Bouton avec pictogramme
class IconButton : public juce::TextButton
{
public:
    enum class Icon { Record, Hold, Next, Clear, Save, Prev, Forward, Undo, Redo, Midi, Size, Tap, Copy };
    IconButton (const juce::String& text, Icon icon, juce::Colour accent);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    Icon icon;
    juce::Colour accent;
};

//==============================================================================
// Base des visualisations : accès aux paramètres + rafraîchissement régulier
class ParamView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit ParamView (PalindromeProcessor& p) : processor (p) {}
    virtual void refresh() { repaint(); }
protected:
    float pv (const juce::String& id) const { return processor.param (id); }
    PalindromeProcessor& processor;
};

// Scène d'activité : capture, silence, relectures programmées dans le temps et dans l'espace stéréo
class ActivityView : public ParamView
{
public:
    using ParamView::ParamView;
    void paint (juce::Graphics&) override;
private:
    float displayRange = 30.0f;
    float pulse = 0.0f;
};

// Phrase en cours de relecture
class PhraseView : public ParamView
{
public:
    using ParamView::ParamView;
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    pal::RenderInfo info;
    int shownSlot = -2;
    float progress = 0.0f;
    bool hasInfo = false;
};

// Banque : emplacements (formes d'onde), taille, ordre, séquence ; glisser-déposer
class BankPanel : public ParamView, public juce::FileDragAndDropTarget
{
public:
    explicit BankPanel (PalindromeProcessor&);
    void refresh() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override { dropSlot = slotAt ({ (float) x, (float) y }); repaint(); }
    void fileDragMove (const juce::StringArray&, int x, int y) override  { dropSlot = slotAt ({ (float) x, (float) y }); repaint(); }
    void fileDragExit (const juce::StringArray&) override                { dropSlot = -1; repaint(); }
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    juce::Rectangle<float> cardFor (int k) const;
    juce::Rectangle<float> lockFor (int k) const;
    int slotAt (juce::Point<float> p) const;

    juce::Slider sizeSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sizeAttachment;
    SegmentedControl orderControl;
    juce::TextEditor sequenceEditor;
    juce::Rectangle<int> cardsArea, sideArea;

    std::array<std::vector<float>, PhraseEngine::maxBankSize> peaks;
    std::array<int, PhraseEngine::maxBankSize> peakVersion {};
    int dropSlot = -1;
    bool dragging = false;
};

// Plan du cycle : capture -> délai -> répétitions -> silence, tel que réglé, avec la phase en cours
class CyclePlanView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Aperçu du hasard sur 8 notes (clic = nouveau tirage)
class NotesPreviewView : public ParamView
{
public:
    using ParamView::ParamView;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { seed = (juce::uint32) juce::Random::getSystemRandom().nextInt(); repaint(); }
private:
    juce::uint32 seed = 12345;
};

// Sens de chaque répétition
class DirectionView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Rythme : grille et position des notes recalées
class RhythmView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Clavier : gamme et tonique (clic sur une touche = tonique)
class KeyboardView : public ParamView
{
public:
    using ParamView::ParamView;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    juce::Rectangle<float> keyRect (int pc, juce::Rectangle<float> area) const;
};

// Transposition + dérive : hauteur de chaque répétition
class TransposeView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Écho : niveau, couleur, vitesse de chaque répétition, modulation, réinjection
class EchoView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Champ stéréo : placement des lectures reverse / normales
class StereoView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Diffusion : halo de réverbération par répétition
class DiffusionView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Ducking : le jeu direct fait baisser la relecture
class DuckView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Violoning : enveloppes des notes normales / reverse
class EnvelopeView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Texture granulaire : nuage de grains autour de la tête de lecture
class TextureView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Tempo : BPM, source (projet / interne), temps de la mesure
class TempoView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };

// Découpage de la dernière capture : coupes (attaques / legato), notes et accords détectés
class AnalysisView : public ParamView
{
public:
    using ParamView::ParamView;
    void refresh() override;
    void paint (juce::Graphics&) override;
private:
    PhraseEngine::AnalysisSnapshot snap;
    int version = 0;
};

// Sortie : vumètre d'entrée et équilibre direct / relecture
class OutputView : public ParamView { public: using ParamView::ParamView; void paint (juce::Graphics&) override; };
