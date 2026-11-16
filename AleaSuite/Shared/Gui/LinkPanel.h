#pragma once

#include "GuiCommon.h"
#include "../Link/AleaLink.h"

namespace alea::gui
{
// Onglet LINK : instances detectees + choix de ce qui est synchronise
class LinkPanel : public juce::Component, private juce::Timer
{
public:
    LinkPanel (alea::link::Link& l, juce::AudioProcessorValueTreeState& s, juce::String extraHelp);
    ~LinkPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Component extraArea;                  // controles propres au plugin (ex. source batterie de Talea)

private:
    void timerCallback() override { repaint (peersArea); }
    alea::link::Link& link;
    juce::AudioProcessorValueTreeState& state;
    juce::String help;
    juce::ToggleButton masterBtn { "LINK actif" };
    juce::ToggleButton fieldBtns[alea::link::kNumFields];
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> atts;
    juce::Rectangle<int> peersArea;
};
} // namespace alea::gui
