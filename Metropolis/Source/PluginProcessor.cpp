#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstring>

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
}

MetropolisAudioProcessor::MetropolisAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "METROPOLIS_STATE", createParameterLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout MetropolisAudioProcessor::createParameterLayout()
{
    using Float = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterChoice>("engine", "Engine",
        juce::StringArray { "IMPACT", "RESONATOR", "REVERSE", "GRAIN", "STUTTER" }, 0));
    p.add(std::make_unique<juce::AudioParameterBool>("freeze", "Freeze Capture", false));
    p.add(std::make_unique<juce::AudioParameterBool>("sync", "Sync", true));
    p.add(std::make_unique<juce::AudioParameterChoice>("motionTarget", "Motion Target",
        juce::StringArray { "PITCH", "PAN" }, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>("filterMode", "Filter Mode",
        juce::StringArray { "LOW PASS", "BAND PASS", "HIGH PASS" }, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>("stutterRate", "Stutter Division",
        juce::StringArray { "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR" }, 2));
    p.add(std::make_unique<Float>("sensitivity", "Sensitivity", juce::NormalisableRange<float>(0.0f, 1.0f), 0.55f));
    p.add(std::make_unique<Float>("impact", "Impact", juce::NormalisableRange<float>(0.0f, 1.0f), 0.45f));
    p.add(std::make_unique<Float>("tune", "Tune", juce::NormalisableRange<float>(35.0f, 1200.0f, 0.1f, 0.42f), 145.0f, " Hz"));
    p.add(std::make_unique<Float>("decay", "Decay", juce::NormalisableRange<float>(0.05f, 4.0f, 0.001f, 0.45f), 0.55f, " s"));
    p.add(std::make_unique<Float>("material", "Material", juce::NormalisableRange<float>(0.0f, 1.0f), 0.35f));
    p.add(std::make_unique<Float>("grainSize", "Grain", juce::NormalisableRange<float>(20.0f, 2000.0f, 0.1f, 0.38f), 180.0f, " ms"));
    p.add(std::make_unique<Float>("stretch", "Stretch", juce::NormalisableRange<float>(0.25f, 4.0f, 0.001f, 0.55f), 1.0f, " x"));
    p.add(std::make_unique<Float>("pitch", "Pitch", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, " st"));
    p.add(std::make_unique<Float>("reverse", "Reverse", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.add(std::make_unique<Float>("density", "Density", juce::NormalisableRange<float>(0.05f, 1.0f), 0.42f));
    p.add(std::make_unique<Float>("scatter", "Scatter", juce::NormalisableRange<float>(0.0f, 1.0f), 0.22f));
    p.add(std::make_unique<Float>("stutterChance", "Stutter Chance", juce::NormalisableRange<float>(0.0f, 1.0f), 0.55f));
    p.add(std::make_unique<Float>("stutterTime", "Stutter Time", juce::NormalisableRange<float>(0.05f, 4.0f, 0.001f, 0.45f), 0.6f, " s"));
    p.add(std::make_unique<Float>("crush", "Crush", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.add(std::make_unique<Float>("feedback", "Feedback", juce::NormalisableRange<float>(0.0f, 0.92f), 0.08f));
    p.add(std::make_unique<Float>("reverb", "Space", juce::NormalisableRange<float>(0.0f, 1.0f), 0.12f));
    p.add(std::make_unique<Float>("tone", "Tone", juce::NormalisableRange<float>(100.0f, 18000.0f, 1.0f, 0.35f), 12000.0f, " Hz"));
    p.add(std::make_unique<Float>("filterEnv", "Filter Env", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.45f));
    p.add(std::make_unique<Float>("filterDecay", "Filter Decay", juce::NormalisableRange<float>(0.03f, 2.0f, 0.001f, 0.45f), 0.28f, " s"));
    p.add(std::make_unique<Float>("resonance", "Resonance", juce::NormalisableRange<float>(0.0f, 1.0f), 0.18f));
    p.add(std::make_unique<Float>("drive", "Drive", juce::NormalisableRange<float>(0.0f, 1.0f), 0.18f));
    p.add(std::make_unique<Float>("motion", "Motion", juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    p.add(std::make_unique<Float>("motionRate", "Motion Rate", juce::NormalisableRange<float>(0.05f, 8.0f, 0.001f, 0.4f), 0.22f, " Hz"));
    p.add(std::make_unique<Float>("mix", "Mix", juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));
    p.add(std::make_unique<Float>("width", "Width", juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f));
    p.add(std::make_unique<Float>("output", "Output", juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f));
    return p;
}

void MetropolisAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    historyCapacity = juce::jmax(4096, (int) std::ceil(sampleRate * 6.0));
    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax(1, samplesPerBlock), 1 };
    for (size_t ch = 0; ch < channelState.size(); ++ch)
    {
        auto& s = channelState[ch];
        s.history.assign((size_t) historyCapacity, 0.0f);
        s.stutterHistory.assign((size_t) juce::jmax(4096, (int) sampleRate * 4), 0.0f);
        s.scratch.assign((size_t) juce::jmax(1, samplesPerBlock), 0.0f);
        s.write = s.stutterWrite = s.cooldown = s.grainCountdown = 0;
        s.reverseAge = s.reverseLength = s.stutterAge = 0;
        s.stutterLength = 1; s.stutterPosition = s.stutterStart = 0;
        s.fastEnvelope = s.slowEnvelope = s.filterEnvelope = s.impactPhase = s.impactEnvelope = 0.0f;
        s.resonator1.fill(0.0f); s.resonator2.fill(0.0f);
        s.crushCounter = 0; s.crushHeld = 0.0f;
        s.motionPhase = ch == 0 ? 0.0f : juce::MathConstants<float>::halfPi;
        for (auto& g : s.grains) g = {};
        s.reverb.reset();
        s.toneFilter.prepare(spec);
        s.toneFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        s.toneFilter.reset();
    }
    inputMeter.store(0.0f); outputMeter.store(0.0f); activityMeter.store(0.0f); activeVoices.store(0);
}

void MetropolisAudioProcessor::releaseResources() {}

bool MetropolisAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto outputSet = layouts.getMainOutputChannelSet();
    return input == outputSet && (outputSet == juce::AudioChannelSet::mono() || outputSet == juce::AudioChannelSet::stereo());
}

void MetropolisAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin(2, buffer.getNumChannels());
    const int engine = (int) parameters.getRawParameterValue("engine")->load();
    const bool freeze = parameters.getRawParameterValue("freeze")->load() > 0.5f;
    const bool sync = parameters.getRawParameterValue("sync")->load() > 0.5f;
    const int motionTarget = (int) parameters.getRawParameterValue("motionTarget")->load();
    const int filterMode = (int) parameters.getRawParameterValue("filterMode")->load();
    const int division = (int) parameters.getRawParameterValue("stutterRate")->load();
    const float sensitivity = parameters.getRawParameterValue("sensitivity")->load();
    const float impact = parameters.getRawParameterValue("impact")->load();
    const float tune = parameters.getRawParameterValue("tune")->load();
    const float decay = parameters.getRawParameterValue("decay")->load();
    const float material = parameters.getRawParameterValue("material")->load();
    const float grainMs = parameters.getRawParameterValue("grainSize")->load();
    const float stretch = parameters.getRawParameterValue("stretch")->load();
    const float pitch = parameters.getRawParameterValue("pitch")->load();
    const float reverse = parameters.getRawParameterValue("reverse")->load();
    const float density = parameters.getRawParameterValue("density")->load();
    const float scatter = parameters.getRawParameterValue("scatter")->load();
    const float stutterChance = parameters.getRawParameterValue("stutterChance")->load();
    const float stutterTime = parameters.getRawParameterValue("stutterTime")->load();
    const float crush = parameters.getRawParameterValue("crush")->load();
    const float feedback = parameters.getRawParameterValue("feedback")->load();
    const float reverb = parameters.getRawParameterValue("reverb")->load();
    const float tone = parameters.getRawParameterValue("tone")->load();
    const float filterEnvAmount = parameters.getRawParameterValue("filterEnv")->load();
    const float filterDecay = parameters.getRawParameterValue("filterDecay")->load();
    const float resonance = parameters.getRawParameterValue("resonance")->load();
    const float drive = parameters.getRawParameterValue("drive")->load();
    const float motion = parameters.getRawParameterValue("motion")->load();
    const float motionRate = parameters.getRawParameterValue("motionRate")->load();
    const float mix = parameters.getRawParameterValue("mix")->load();
    const float width = parameters.getRawParameterValue("width")->load();
    const float outputGain = parameters.getRawParameterValue("output")->load();

    double bpm = 120.0;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
        {
            if (auto hostBpm = position->getBpm()) bpm = *hostBpm;
        }
    bpm = juce::jlimit(20.0, 300.0, bpm);
    const std::array<double, 6> divisions { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
    const double sliceBeats = divisions[(size_t) juce::jlimit(0, 5, division)];
    const int stutterSlice = juce::jlimit(32, (int) (sampleRateHz * 3.0), (int) (sampleRateHz * 60.0 * sliceBeats / bpm));
    const int grainLength = juce::jlimit(128, historyCapacity - 1, (int) (sampleRateHz * grainMs * 0.001));
    const float pitchRatio = std::pow(2.0f, pitch / 12.0f);
    const float threshold = 0.0015f + (1.0f - sensitivity) * (1.0f - sensitivity) * 0.06f;
    juce::Reverb::Parameters rv;
    rv.roomSize = 0.22f + reverb * 0.7f; rv.damping = 0.48f; rv.wetLevel = reverb * 0.72f;
    rv.dryLevel = 0.0f; rv.width = 1.0f; rv.freezeMode = 0.0f;

    float peakIn = 0.0f, peakOut = 0.0f, activity = 0.0f;
    int voiceCount = 0;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& s = channelState[(size_t) ch];
        s.reverb.setParameters(rv);
        const auto filterType = filterMode == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                              : filterMode == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                                : juce::dsp::StateVariableTPTFilterType::lowpass;
        s.toneFilter.setType(filterType);
        s.toneFilter.setResonance(0.1f + resonance * 0.85f);
        auto* data = buffer.getWritePointer(ch);
        int blockOffset = 0;
        while (blockOffset < numSamples)
        {
            const int count = juce::jmin((int) s.scratch.size(), numSamples - blockOffset);
            for (int i = 0; i < count; ++i)
            {
                const float input = data[blockOffset + i];
                peakIn = juce::jmax(peakIn, std::abs(input));
                const float absInput = std::abs(input);
                s.fastEnvelope += (absInput - s.fastEnvelope) * (absInput > s.fastEnvelope ? 0.25f : 0.009f);
                s.slowEnvelope += (absInput - s.slowEnvelope) * (absInput > s.slowEnvelope ? 0.012f : 0.0008f);
                const bool hit = s.cooldown <= 0 && s.fastEnvelope > threshold && s.fastEnvelope > s.slowEnvelope * 1.3f;
                if (s.cooldown > 0) --s.cooldown;
                if (hit)
                {
                    s.cooldown = (int) (sampleRateHz * 0.025);
                    const float hitDb = juce::Decibels::gainToDecibels(s.fastEnvelope, -72.0f);
                    const float hitStrength = juce::jlimit(0.0f, 1.0f, (hitDb + 42.0f) / 36.0f);
                    s.filterEnvelope = juce::jmax(s.filterEnvelope, hitStrength);
                }
                else
                    s.filterEnvelope *= std::exp(-1.0f / ((float) sampleRateHz * filterDecay));

                if (!freeze)
                {
                    const int feedbackDelay = juce::jmax(1, (int) (sampleRateHz * (0.08f + feedback * 0.35f)));
                    const int delayedIndex = (s.write - feedbackDelay + historyCapacity) % historyCapacity;
                    s.history[(size_t) s.write] = input + s.history[(size_t) delayedIndex] * feedback;
                    if (++s.write >= historyCapacity) s.write = 0;
                }
                if (engine != 4 || s.stutterAge == 0)
                {
                    s.stutterHistory[(size_t) s.stutterWrite] = input;
                    if (++s.stutterWrite >= (int) s.stutterHistory.size()) s.stutterWrite = 0;
                }

                const float lfo = std::sin(s.motionPhase);
                s.motionPhase += juce::MathConstants<float>::twoPi * motionRate / (float) sampleRateHz;
                if (s.motionPhase >= juce::MathConstants<float>::twoPi) s.motionPhase -= juce::MathConstants<float>::twoPi;
                activity = lfo;

                if (hit)
                {
                    s.impactEnvelope = 1.0f;
                    if (engine == 2)
                    {
                        s.reverseLength = juce::jlimit(128, historyCapacity - 1, (int) (sampleRateHz * (0.08f + reverse * 1.2f)));
                        s.reverseAge = -(int) (sampleRateHz * (0.035f + scatter * 0.28f));
                    }
                    if (engine == 4 && s.random.nextFloat() < stutterChance)
                    {
                        s.stutterLength = juce::jlimit(32, (int) s.stutterHistory.size() - 1, sync ? stutterSlice : (int) (sampleRateHz * 0.12f));
                        s.stutterPosition = 0;
                        s.stutterStart = (s.stutterWrite - s.stutterLength + (int) s.stutterHistory.size()) % (int) s.stutterHistory.size();
                        s.stutterAge = 1;
                    }
                }
                if (engine == 3 && (hit || s.grainCountdown <= 0))
                {
                    s.grainCountdown = (int) (sampleRateHz * (0.025f + (1.0f - density) * (1.0f - density) * 0.32f));
                    const int grains = 1 + (int) std::round(density * 4.0f);
                    for (int g = 0; g < grains; ++g)
                    {
                        GrainVoice* voice = nullptr;
                        for (auto& candidate : s.grains) if (!candidate.active) { voice = &candidate; break; }
                        if (!voice) break;
                        const int maxOffset = (int) (sampleRateHz * (0.015f + scatter * 0.7f));
                        const int offset = maxOffset > 0 ? s.random.nextInt(maxOffset + 1) : 0;
                        voice->active = true; voice->age = 0; voice->length = grainLength;
                        voice->position = (float) ((s.write - 1 - offset + historyCapacity) % historyCapacity);
                        const bool backwards = s.random.nextFloat() < reverse;
                        voice->speed = (backwards ? -1.0f : 1.0f) * stretch * pitchRatio;
                        voice->gain = 0.9f / std::sqrt((float) grains);
                    }
                }
                if (s.grainCountdown > 0) --s.grainCountdown;

                float generated = 0.0f;
                if (engine == 0)
                {
                    const float modulatedTune = tune * (motionTarget == 0 ? std::pow(2.0f, lfo * motion * 0.35f) : 1.0f);
                    s.impactPhase += juce::MathConstants<float>::twoPi * modulatedTune * (0.65f + s.impactEnvelope * 1.9f) / (float) sampleRateHz;
                    if (s.impactPhase >= juce::MathConstants<float>::twoPi) s.impactPhase -= juce::MathConstants<float>::twoPi;
                    s.impactEnvelope *= std::exp(-1.0f / ((float) sampleRateHz * (0.025f + decay * 0.9f)));
                    const float body = std::sin(s.impactPhase) * impact;
                    const float click = s.random.nextFloat() * 2.0f - 1.0f;
                    generated = input * 0.42f + s.impactEnvelope * (body + click * impact * 0.12f);
                }
                else if (engine == 1)
                {
                    const float freq = juce::jlimit(25.0f, (float) sampleRateHz * 0.42f,
                        tune * (motionTarget == 0 ? std::pow(2.0f, lfo * motion * 0.35f) : 1.0f));
                    const std::array<float, 3> ratios { 1.0f, 1.42f + material * 0.52f, 2.0f + material * 1.75f };
                    const std::array<float, 3> levels { 1.0f, 0.58f, 0.34f };
                    for (size_t mode = 0; mode < ratios.size(); ++mode)
                    {
                        const float modeFreq = juce::jlimit(25.0f, (float) sampleRateHz * 0.42f, freq * ratios[mode]);
                        const float modeDecay = decay * (1.0f - (float) mode * 0.13f);
                        const float radius = std::exp(-6.9078f / ((float) sampleRateHz * modeDecay));
                        const float coeff = 2.0f * radius * std::cos(juce::MathConstants<float>::twoPi * modeFreq / (float) sampleRateHz);
                        const float resonant = input * (0.18f + impact * 0.28f) + coeff * s.resonator1[mode] - radius * radius * s.resonator2[mode];
                        s.resonator2[mode] = s.resonator1[mode]; s.resonator1[mode] = juce::jlimit(-8.0f, 8.0f, resonant);
                        generated += resonant * levels[mode];
                    }
                    generated *= 0.23f;
                }
                else if (engine == 2 && s.reverseAge < 0)
                {
                    if (++s.reverseAge == 0) s.reversePosition = (float) ((s.write - 1 + historyCapacity) % historyCapacity);
                }
                else if (engine == 2 && s.reverseAge < s.reverseLength)
                {
                    const float phase = (float) s.reverseAge / (float) juce::jmax(1, s.reverseLength - 1);
                    const float window = 0.5f - 0.5f * (float) std::cos(twoPi * phase);
                    int read = (int) s.reversePosition;
                    read %= historyCapacity; if (read < 0) read += historyCapacity;
                    generated = s.history[(size_t) read] * window;
                    s.reversePosition -= 1.0f;
                    if (s.reversePosition < 0.0f) s.reversePosition += (float) historyCapacity;
                    ++s.reverseAge;
                }
                else if (engine == 3)
                {
                    for (auto& grain : s.grains) if (grain.active)
                    {
                        const float phase = (float) grain.age / (float) juce::jmax(1, grain.length - 1);
                        const float window = 0.5f - 0.5f * (float) std::cos(twoPi * phase);
                        int read = (int) grain.position; read %= historyCapacity; if (read < 0) read += historyCapacity;
                        generated += s.history[(size_t) read] * window * grain.gain;
                        const float speedMod = motionTarget == 0 ? std::pow(2.0f, lfo * motion * 1.5f) : 1.0f;
                        grain.position += grain.speed * speedMod;
                        if (grain.position >= (float) historyCapacity) grain.position -= (float) historyCapacity;
                        if (grain.position < 0.0f) grain.position += (float) historyCapacity;
                        if (++grain.age >= grain.length) grain.active = false;
                    }
                }
                else if (engine == 4 && s.stutterAge < (int) (stutterTime * sampleRateHz) && s.stutterAge > 0)
                {
                    generated = s.stutterHistory[(size_t) ((s.stutterStart + s.stutterPosition) % (int) s.stutterHistory.size())];
                    if (++s.stutterPosition >= s.stutterLength) s.stutterPosition = 0;
                    ++s.stutterAge;
                }
                else if (engine == 4 && s.stutterAge >= (int) (stutterTime * sampleRateHz))
                    s.stutterAge = 0;

                if (((blockOffset + i) & 15) == 0)
                {
                    const float modulatedCutoff = tone * std::pow(2.0f, s.filterEnvelope * filterEnvAmount * 5.0f);
                    s.toneFilter.setCutoffFrequency(juce::jlimit(35.0f, (float) sampleRateHz * 0.45f, modulatedCutoff));
                }
                generated = s.toneFilter.processSample(0, generated);
                if (crush > 0.001f)
                {
                    if (s.crushCounter <= 0)
                    {
                        s.crushHeld = generated;
                        s.crushCounter = 1 + (int) (crush * 42.0f);
                    }
                    else --s.crushCounter;
                    const float levels = std::pow(2.0f, 15.0f - crush * 13.0f);
                    generated = std::round(s.crushHeld * levels) / levels;
                }
                const float driveGain = 1.0f + drive * 10.0f;
                generated = std::tanh(generated * driveGain) / std::tanh(driveGain);
                s.scratch[(size_t) i] = generated;
            }

            s.reverb.processMono(s.scratch.data(), count);
            for (int i = 0; i < count; ++i)
            {
                const float dry = data[blockOffset + i];
                const float lfo = std::sin(s.motionPhase - juce::MathConstants<float>::twoPi * motionRate * (float) (count - i) / (float) sampleRateHz);
                const float panGain = motionTarget == 1 ? 1.0f + (ch == 0 ? -1.0f : 1.0f) * lfo * motion * 0.8f : 1.0f;
                const float wet = s.scratch[(size_t) i] * panGain;
                const float out = (dry * (1.0f - mix) + wet * mix) * outputGain;
                data[blockOffset + i] = juce::jlimit(-1.0f, 1.0f, out);
                peakOut = juce::jmax(peakOut, std::abs(data[blockOffset + i]));
            }
            blockOffset += count;
        }
        for (const auto& grain : s.grains) if (grain.active) ++voiceCount;
    }

    if (numChannels == 2)
    {
        auto* left = buffer.getWritePointer(0); auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float mid = 0.5f * (left[i] + right[i]);
            const float side = 0.5f * (left[i] - right[i]) * width;
            left[i] = juce::jlimit(-1.0f, 1.0f, mid + side);
            right[i] = juce::jlimit(-1.0f, 1.0f, mid - side);
        }
    }
    inputMeter.store(peakIn); outputMeter.store(peakOut); activityMeter.store(activity); activeVoices.store(voiceCount);
}

void MetropolisAudioProcessor::loadScene(int scene)
{
    auto setFloat = [this](const char* id, float value)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter(id)))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto setChoice = [this](const char* id, int value)
    {
        if (auto* p = parameters.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1((float) value));
    };
    auto setBool = [this](const char* id, bool value)
    {
        if (auto* p = parameters.getParameter(id)) p->setValueNotifyingHost(value ? 1.0f : 0.0f);
    };
    setBool("freeze", false); setBool("sync", true); setChoice("stutterRate", 2); setChoice("motionTarget", 0);
    setFloat("sensitivity", .55f); setFloat("impact", .45f); setFloat("tune", 145.0f); setFloat("decay", .55f);
    setFloat("material", .35f);
    setFloat("grainSize", 180.0f); setFloat("stretch", 1.0f); setFloat("pitch", 0.0f); setFloat("reverse", 0.0f);
    setFloat("density", .42f); setFloat("scatter", .22f); setFloat("stutterChance", .55f); setFloat("stutterTime", .6f);
    setFloat("crush", 0.0f);
    setFloat("feedback", .08f); setFloat("reverb", .12f); setFloat("tone", 12000.0f); setFloat("resonance", .18f);
    setFloat("filterEnv", .45f); setFloat("filterDecay", .28f);
    setFloat("drive", .18f); setFloat("motion", 0.0f); setFloat("motionRate", .22f); setFloat("mix", .5f);
    setFloat("width", 1.0f); setFloat("output", 1.0f);
    switch (scene)
    {
        case 0: // Steel Hit
            setChoice("engine", 0); setChoice("filterMode", 0); setFloat("filterEnv", .28f); setFloat("filterDecay", .16f); setFloat("mix", .62f); setFloat("impact", .82f); setFloat("tune", 92); setFloat("decay", .24f); setFloat("sensitivity", .58f); setFloat("drive", .3f); setFloat("tone", 9800); setFloat("reverb", .06f); setFloat("feedback", .04f); setFloat("motion", .03f); break;
        case 1: // Chrome Body
            setChoice("engine", 1); setChoice("filterMode", 1); setFloat("filterEnv", .52f); setFloat("filterDecay", .46f); setFloat("mix", .72f); setFloat("impact", .78f); setFloat("tune", 420); setFloat("decay", 1.15f); setFloat("material", .86f); setFloat("sensitivity", .55f); setFloat("resonance", .72f); setFloat("drive", .22f); setFloat("tone", 7600); setFloat("reverb", .19f); setFloat("feedback", .08f); setFloat("motion", .04f); break;
        case 2: // Reverse Rise
            setChoice("engine", 2); setChoice("filterMode", 2); setFloat("filterEnv", .38f); setFloat("filterDecay", .68f); setFloat("mix", .7f); setFloat("reverse", .72f); setFloat("scatter", .42f); setFloat("sensitivity", .6f); setFloat("decay", .8f); setFloat("tone", 4200); setFloat("reverb", .46f); setFloat("feedback", .12f); setFloat("motion", .08f); break;
        case 3: // Skyline Cloud
            setChoice("engine", 3); setChoice("filterMode", 0); setFloat("filterEnv", -.24f); setFloat("filterDecay", .42f); setFloat("mix", .58f); setFloat("grainSize", 260); setFloat("stretch", 1.65f); setFloat("pitch", 7); setFloat("density", .48f); setFloat("scatter", .62f); setFloat("reverse", .3f); setFloat("sensitivity", .53f); setFloat("tone", 14500); setFloat("reverb", .24f); setFloat("feedback", .09f); setFloat("motion", .34f); setChoice("motionTarget", 1); break;
        case 4: // Chrome Stutter
            setChoice("engine", 4); setChoice("filterMode", 1); setFloat("filterEnv", .68f); setFloat("filterDecay", .12f); setFloat("mix", .76f); setFloat("stutterChance", .78f); setFloat("stutterTime", .48f); setChoice("stutterRate", 1); setFloat("sensitivity", .6f); setFloat("drive", .42f); setFloat("crush", .32f); setFloat("tone", 8200); setFloat("reverb", .08f); setFloat("feedback", .08f); setFloat("motion", .06f); break;
        default: break;
    }
}

void MetropolisAudioProcessor::randomize(float amount)
{
    amount = juce::jlimit(0.0f, 1.0f, amount);
    const std::array<const char*, 20> ids { "impact", "tune", "decay", "material", "grainSize", "stretch", "pitch", "reverse", "density", "scatter", "stutterChance", "stutterTime", "crush", "feedback", "reverb", "tone", "filterEnv", "filterDecay", "resonance", "drive" };
    for (const auto* id : ids)
    {
        auto* p = dynamic_cast<juce::AudioParameterFloat*>(parameters.getParameter(id));
        if (!p) continue;
        const auto range = p->getNormalisableRange();
        const float current = parameters.getRawParameterValue(id)->load();
        float target = range.start + uiRandom.nextFloat() * (range.end - range.start);
        if (std::strcmp(id, "feedback") == 0) target = uiRandom.nextFloat() * 0.42f;
        if (std::strcmp(id, "reverb") == 0) target = uiRandom.nextFloat() * 0.68f;
        p->setValueNotifyingHost(p->convertTo0to1(current + (target - current) * amount));
    }
    if (uiRandom.nextFloat() < amount * 0.45f)
        if (auto* p = parameters.getParameter("engine")) p->setValueNotifyingHost(p->convertTo0to1((float) uiRandom.nextInt(5)));
    if (uiRandom.nextFloat() < amount * 0.35f)
        if (auto* p = parameters.getParameter("filterMode")) p->setValueNotifyingHost(p->convertTo0to1((float) uiRandom.nextInt(3)));
}

juce::AudioProcessorEditor* MetropolisAudioProcessor::createEditor() { return new MetropolisAudioProcessorEditor(*this); }
void MetropolisAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, dest);
}
void MetropolisAudioProcessor::setStateInformation(const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(parameters.state.getType())) return;
    const auto incoming = juce::ValueTree::fromXml(*xml);
    auto merged = parameters.copyState();
    for (int i = 0; i < incoming.getNumChildren(); ++i)
    {
        const auto savedParameter = incoming.getChild(i);
        if (!savedParameter.hasType("PARAM")) continue;
        const auto savedId = savedParameter.getProperty("id").toString();
        for (int j = 0; j < merged.getNumChildren(); ++j)
        {
            auto currentParameter = merged.getChild(j);
            if (currentParameter.hasType("PARAM") && currentParameter.getProperty("id").toString() == savedId)
            {
                currentParameter.setProperty("value", savedParameter.getProperty("value"), nullptr);
                break;
            }
        }
    }
    parameters.replaceState(merged);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MetropolisAudioProcessor(); }
