#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../KnobHost.h"

// Elements d'interface partages par Alea et Talea
namespace alea::gui
{
// Panneau dont le dessin est fourni par une fonction
struct PaintPanel : public juce::Component
{
    std::function<void (juce::Graphics&)> painter;
    void paint (juce::Graphics& g) override { if (painter) painter (g); }
};

namespace col
{
    const juce::Colour bg        { 0xff141519 };
    const juce::Colour panel     { 0xff1d1f26 };
    const juce::Colour panelHi   { 0xff262933 };
    const juce::Colour line      { 0xff32353f };
    const juce::Colour text      { 0xffd8dae0 };
    const juce::Colour textDim   { 0xff7f8490 };
    const juce::Colour accent    { 0xffff7a1a };   // valeur
    const juce::Colour rnd       { 0xff35d0c3 };   // alea
    const juce::Colour lock      { 0xffe2464b };
    const juce::Colour plock     { 0xfff5e663 };   // parameter lock
    juce::Colour track (int t);
}

juce::Font font (float size, bool bold = false);

// ---------------------------------------------------------------------------
// Potentiometre a double fonction : valeur (arc) + amplitude d'alea (anneau turquoise).
// Glisser = valeur ; clic droit-glisser / Cmd-glisser (ou mode RND EDIT) = alea ;
// clic droit sans glisser = menu (MIDI learn...) ; double-clic = defaut ;
// cadenas en haut a droite = exclure du de. En mode p-lock, le potard edite le verrou du pas.
class RndKnob : public juce::Component, public juce::SettableTooltipClient
{
public:
    RndKnob() = default;
    void setProcessor (KnobHost* p) { proc = p; }
    void bind (juce::RangedAudioParameter* value, juce::RangedAudioParameter* jitter,
               std::atomic<bool>* lock, std::atomic<float>* live, const juce::String& label);
    void setChaosSource (std::atomic<float>* c) { chaos = c; }
    void setAccentColour (juce::Colour c) { accentCol = c; repaint(); }
    void setBipolar (bool b) { bipolar = b; repaint(); }
    void setPlockTarget (std::atomic<int16_t>* target) { plock = target; repaint(); }
    void refreshLive();
    std::function<void()> onPlockEdited;

    static inline bool editRandomMode = false;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

private:
    juce::Rectangle<float> lockArea() const;
    int paramIdx() const;
    void showMenu();

    KnobHost* proc = nullptr;
    juce::RangedAudioParameter* param = nullptr;
    juce::RangedAudioParameter* jparam = nullptr;
    std::unique_ptr<juce::ParameterAttachment> att, jatt;
    std::atomic<bool>* lock = nullptr;
    std::atomic<float>* live = nullptr;
    std::atomic<float>* chaos = nullptr;
    std::atomic<int16_t>* plock = nullptr;
    float value = 0, jitter = 0, liveShown = -1;
    juce::String label;
    juce::Colour accentCol = col::accent;
    bool hover = false, dragging = false, dragJitter = false, dragPlock = false, ignoreDrag = false, bipolar = false;
    bool rightDown = false, moved = false, learnBlink = false;
    float dragStart = 0;
};

// ---------------------------------------------------------------------------
// Oscilloscope : forme d'onde du dernier coup de la piste selectionnee
class Scope : public juce::Component
{
public:
    Scope (const float* buffer, int length, std::atomic<int>& versionCounter, std::function<juce::Colour()> colourFn)
        : buf (buffer), len (length), version (versionCounter), colour (std::move (colourFn)) {}
    void refresh() { const int v = version.load(); if (v != lastVersion) { lastVersion = v; repaint(); } }
    void paint (juce::Graphics&) override;
private:
    const float* buf;
    int len;
    std::atomic<int>& version;
    std::function<juce::Colour()> colour;
    int lastVersion = -1;
};

// ---------------------------------------------------------------------------
// Afficheur d'un modulateur (valeur + historique)
class ModDisplay : public juce::Component
{
public:
    explicit ModDisplay (std::atomic<float>& v) : value (v) {}
    void push();
    void paint (juce::Graphics&) override;
private:
    std::atomic<float>& value;
    std::array<float, 120> hist {};
    int pos = 0;
};

// ---------------------------------------------------------------------------
// Bouton a glisser : exporte le pattern en .mid et le depose dans le DAW
class DragMidiButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragMidiButton (std::function<juce::File()> exportFn) : exporter (std::move (exportFn)) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { dragged = false; }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    std::function<juce::File()> exporter;
    bool dragged = false;
};

} // namespace alea::gui
