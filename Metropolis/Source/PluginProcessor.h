#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

class MetropolisAudioProcessor final : public juce::AudioProcessor
{
public:
    MetropolisAudioProcessor();
    ~MetropolisAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "METROPOLIS"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void loadScene(int scene);
    void randomize(float amount);

    std::atomic<float> inputMeter { 0.0f }, outputMeter { 0.0f }, activityMeter { 0.0f };
    std::atomic<int> activeVoices { 0 };

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    static constexpr int grainVoiceCount = 12;
    struct GrainVoice
    {
        bool active = false;
        float position = 0.0f, speed = 1.0f;
        int age = 0, length = 1;
        float gain = 0.0f;
    };
    struct ChannelState
    {
        std::vector<float> history, stutterHistory, scratch;
        std::array<GrainVoice, grainVoiceCount> grains{};
        juce::Reverb reverb;
        juce::dsp::StateVariableTPTFilter<float> toneFilter;
        juce::Random random;
        int write = 0, stutterWrite = 0, cooldown = 0, grainCountdown = 0;
        int reverseAge = 0, reverseLength = 0;
        float reversePosition = 0.0f;
        int stutterAge = 0, stutterLength = 1, stutterPosition = 0, stutterStart = 0;
        float fastEnvelope = 0.0f, slowEnvelope = 0.0f, filterEnvelope = 0.0f;
        float impactPhase = 0.0f, impactEnvelope = 0.0f;
        std::array<float, 3> resonator1{}, resonator2{};
        int crushCounter = 0;
        float crushHeld = 0.0f;
        float motionPhase = 0.0f;
    };

    std::array<ChannelState, 2> channelState;
    double sampleRateHz = 44100.0;
    int historyCapacity = 1;
    juce::Random uiRandom;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetropolisAudioProcessor)
};
