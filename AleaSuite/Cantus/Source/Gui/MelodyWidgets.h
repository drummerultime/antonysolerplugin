#pragma once

#include "../../../Shared/Gui/GuiCommon.h"
#include "../CantusProcessor.h"

namespace cantus::gui
{
using namespace alea::gui;

constexpr float kKeysW = 70.0f;   // colonne clavier / libelles (alignement roll + couloirs)

// Piano roll facon 303 : une note par pas, 25 lignes (2 octaves + 1) au-dessus de la tonique
class PianoRoll : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PianoRoll (CantusAudioProcessor& p) : proc (p) {}
    std::function<void (int)> onLockStep;           // -1 = sortie
    int lockStep = -1;
    CantusAudioProcessor::Preview preview;
    bool showPreview = true;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    bool cellAt (juce::Point<float> p, int& step, int& row) const;
    void stepMenu (int step);
    CantusAudioProcessor& proc;
    int paintMode = 0;     // 1 = pose des notes, 2 = efface
    int lastDragStep = -1;
};

// Couloirs par pas : accent, slide, tie, octave, gate, velocite, proba, ratchet, condition, nudge
class BassLanes : public juce::Component
{
public:
    explicit BassLanes (CantusAudioProcessor& p) : proc (p) {}
    enum Lane { Acc = 0, Sld, Tie, Oct, Gate, Vel, Prob, Rat, Cond, Nudge, kNumLanes };
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
private:
    void edit (const juce::MouseEvent&, bool reset);
    CantusAudioProcessor& proc;
    int dragLane = -1, toggleValue = 1;
    bool toggling = false;
};

} // namespace cantus::gui
