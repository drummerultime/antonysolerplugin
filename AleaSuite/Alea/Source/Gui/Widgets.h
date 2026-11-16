#pragma once

#include "../../../Shared/Gui/GuiCommon.h"
#include "../PluginProcessor.h"

namespace alea::gui
{
// ---------------------------------------------------------------------------
class TrackList : public juce::Component
{
public:
    explicit TrackList (AleaAudioProcessor& p) : proc (p) {}
    std::function<void (int)> onSelect;
    int selected = 0;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    float rowHeight() const { return (float) getHeight() / (float) kNumTracks; }
private:
    AleaAudioProcessor& proc;
};

// ---------------------------------------------------------------------------
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (AleaAudioProcessor& p) : proc (p) {}
    std::function<void (int)> onSelect;
    std::function<void (int, int)> onLockStep;      // (piste, pas) ; (-1,-1) = sortie
    int selected = 0, lockTrack = -1, lockStep = -1;
    AleaAudioProcessor::Preview preview;
    bool showPreview = true;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    bool cellAt (juce::Point<float> pos, int& t, int& i) const;
    void stepMenu (int t, int i);
    AleaAudioProcessor& proc;
    int dragTrack = -1, paintValue = 1;
    bool paintAccent = false;
};

// ---------------------------------------------------------------------------
// Couloirs de details par pas pour la piste selectionnee
class LaneEditor : public juce::Component
{
public:
    LaneEditor (AleaAudioProcessor& p, float labelWidth) : proc (p), labelW (labelWidth) {}
    int track = 0;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    enum Lane { Prob = 0, Ratchet, Nudge, TuneL, CondL, SlideL, kNumLanes };
private:
    void edit (const juce::MouseEvent&, bool reset);
    AleaAudioProcessor& proc;
    float labelW;
    int dragLane = -1, slideValue = 1;
    bool slidePainting = false;
};

} // namespace alea::gui
