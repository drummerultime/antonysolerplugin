#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "FragorLookAndFeel.h"
#include "../Params.h"

class FragorProcessor;

namespace fragor
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // Correspondance destination de modulation <-> paramètre affiché
    int destForParam (const juce::String& paramId);
    // Position normalisée du potentiomètre après ajout d'une modulation "delta" (unités de la destination)
    float modulatedNorm (int dest, juce::RangedAudioParameter& p, float baseNorm, float delta);
    float defaultModAmount (int dest);

    // ------------------------------------------------------------ Slider avec gestes de modulation
    class ModSlider : public juce::Slider
    {
    public:
        std::function<bool (const juce::MouseEvent&)> onModDown;  // true = géré
        std::function<void (const juce::MouseEvent&)> onModDrag;
        std::function<void()> onModUp;
        bool modGesture = false;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
    };

    // ------------------------------------------------------------ Potentiomètre + nom + anneau de modulation
    class Knob : public juce::Component, public juce::DragAndDropTarget
    {
    public:
        Knob (APVTS& s, const juce::String& paramId, const juce::String& label, juce::Colour accent = Col::acid);
        void attachMod (FragorProcessor* p, int dest);
        void resized() override;
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        bool hasMods() const;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails&) override { dropHover = true; repaint(); }
        void itemDragExit (const SourceDetails&) override  { dropHover = false; repaint(); }
        void itemDropped (const SourceDetails&) override;

        ModSlider slider;
        juce::String label;
    private:
        bool modDown (const juce::MouseEvent&);
        void modDrag (const juce::MouseEvent&);
        int lastSlotFor() const;
        APVTS& state;
        juce::String paramId;
        FragorProcessor* proc = nullptr;
        int dest = -1;
        int dragSlot = -1;
        float dragStartAmt = 0.0f;
        bool dropHover = false;
        std::unique_ptr<APVTS::SliderAttachment> att;
    };

    // ------------------------------------------------------------ Liste déroulante
    class Choice : public juce::Component
    {
    public:
        Choice (APVTS& s, const juce::String& paramId, const juce::String& label = {});
        void resized() override;
        void paint (juce::Graphics&) override;
        juce::ComboBox box;
        juce::String label;
    private:
        std::unique_ptr<APVTS::ComboBoxAttachment> att;
    };

    // ------------------------------------------------------------ Interrupteur
    class Toggle : public juce::Component
    {
    public:
        Toggle (APVTS& s, const juce::String& paramId, const juce::String& text, juce::Colour accent = Col::acid);
        void resized() override { button.setBounds (getLocalBounds()); }
        juce::ToggleButton button;
    private:
        std::unique_ptr<APVTS::ButtonAttachment> att;
    };

    // ------------------------------------------------------------ Panneau titré
    class Panel : public juce::Component
    {
    public:
        explicit Panel (const juce::String& title, juce::Colour accent = Col::acid) : title (title), accent (accent) {}
        void paint (juce::Graphics&) override;
        juce::String title;
        juce::Colour accent;
    };

    // ------------------------------------------------------------ Source de modulation à glisser
    class DragChip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        DragChip (const juce::String& text, int source, juce::Colour c);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        std::function<void()> onClick;
        bool selected = false;
        juce::String text;
    private:
        int source;
        juce::Colour colour;
        bool dragging = false;
    };

    // ------------------------------------------------------------ Affichage wavetable (pseudo-3D) + dépôt de WAV
    class WaveDisplay : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
    {
    public:
        WaveDisplay (FragorProcessor& p, const juce::String& prefix);
        void paint (juce::Graphics&) override;
        bool isInterestedInFileDrag (const juce::StringArray& files) override;
        void filesDropped (const juce::StringArray& files, int, int) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    private:
        void timerCallback() override;
        FragorProcessor& proc;
        juce::String prefix;
        float lastHash = -1.0f;
        bool hover = false;
    };

    // ------------------------------------------------------------ Oscilloscope
    class ScopeDisplay : public juce::Component, private juce::Timer
    {
    public:
        explicit ScopeDisplay (FragorProcessor& p);
        void paint (juce::Graphics&) override;
    private:
        void timerCallback() override { repaint(); }
        FragorProcessor& proc;
    };

    // ------------------------------------------------------------ Grille du séquenceur de vitesse (LFO 1)
    class SeqGrid : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit SeqGrid (FragorProcessor& p);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
    private:
        void timerCallback() override { repaint(); }
        void setFromMouse (const juce::MouseEvent&);
        FragorProcessor& proc;
    };

    // ------------------------------------------------------------ Forme du LFO (double-clic = éditeur)
    class LfoDisplay : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        LfoDisplay (FragorProcessor& p, int index);
        void setIndex (int i) { index = i; repaint(); }
        void paint (juce::Graphics&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onEdit) onEdit(); }
        std::function<void()> onEdit;
    private:
        void timerCallback() override { repaint(); }
        FragorProcessor& proc;
        int index;
    };

    // ------------------------------------------------------------ Slot de matrice
    class ModSlot : public juce::Component
    {
    public:
        ModSlot (APVTS& s, int slot);
        void resized() override;
        void paint (juce::Graphics&) override;
    private:
        int slot;
        juce::ComboBox src, dst;
        juce::Slider amt;
        std::unique_ptr<APVTS::ComboBoxAttachment> srcAtt, dstAtt;
        std::unique_ptr<APVTS::SliderAttachment> amtAtt;
    };

    // ------------------------------------------------------------ Chaîne d'effets réorganisable
    class FxChainStrip : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit FxChainStrip (FragorProcessor& p);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
    private:
        void timerCallback() override { repaint(); }
        int indexAt (float x) const;
        juce::Rectangle<float> chipBounds (int slot) const;
        FragorProcessor& proc;
        int dragSlot = -1;
        float dragX = 0.0f, grabOffset = 0.0f;
    };

    void layoutGrid (const std::vector<juce::Component*>& items, juce::Rectangle<int> area, int cols, int cellW, int cellH);
}
