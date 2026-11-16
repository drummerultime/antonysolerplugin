#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MetropolisAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MetropolisAudioProcessorEditor(MetropolisAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    MetropolisAudioProcessor& audioProcessor;
    juce::Label title, subtitle;
    juce::ComboBox engineBox, sceneBox, filterModeBox;
    juce::Label filterModeLabel;
    juce::ToggleButton freezeButton { "HOLD" }, syncButton { "SYNC" };
    juce::TextButton randomButton { "GENERATE" };
    juce::Slider randomAmount;
    juce::Label randomLabel;
    std::array<std::unique_ptr<juce::Slider>, 26> knobs;
    std::array<std::unique_ptr<juce::Label>, 26> labels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 26> attachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> engineAttachment, filterModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment, syncAttachment;
    float inputLevel = 0.0f, outputLevel = 0.0f;
    int voices = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetropolisAudioProcessorEditor)
};
