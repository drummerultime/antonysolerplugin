#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Effets temps réel sur le signal relu : modulation façon bande / chorus (wow & flutter)
// et saturation douce, dans l'esprit des échos analogiques.
class WetFX
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) std::ceil (sr * 0.05) + 4;
        for (auto& l : lines) l.assign ((size_t) size, 0.0f);
        writePos = 0;
        phase = 0.0;
        flutterPhase = 0.0;
        depthSm.reset (sr, 0.1);
        driveSm.reset (sr, 0.1);
    }

    // depth 0..1, rateHz, drive 0..1
    void process (juce::AudioBuffer<float>& wet, int numSamples, float depth, float rateHz, float drive)
    {
        if (size == 0) return;

        depthSm.setTargetValue (depth);
        driveSm.setTargetValue (drive);

        const int numCh = juce::jmin (2, wet.getNumChannels());
        const double inc = rateHz / sr;
        const double flutterInc = (rateHz * 6.3) / sr;
        const double baseDelay = 0.006 * sr;           // 6 ms
        const double maxSwing  = 0.004 * sr;           // ±4 ms

        for (int i = 0; i < numSamples; ++i)
        {
            const float d = depthSm.getNextValue();
            const float dr = driveSm.getNextValue();
            const float g = 1.0f + dr * 9.0f;
            const float makeup = 1.0f / std::sqrt (g);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& line = lines[(size_t) ch];
                float x = wet.getSample (ch, i);
                line[(size_t) writePos] = x;

                // LFO principal (wow) + petit flutter, décalés entre gauche et droite
                const double ph = phase + (ch == 1 ? 0.25 : 0.0);
                const double lfo = std::sin (juce::MathConstants<double>::twoPi * ph)
                                 + 0.25 * std::sin (juce::MathConstants<double>::twoPi * (flutterPhase + ch * 0.37));
                const double delay = baseDelay + maxSwing * d * lfo / 1.25;

                double rp = (double) writePos - delay;
                while (rp < 0) rp += size;
                const int i0 = (int) rp;
                const int i1 = (i0 + 1) % size;
                const float frac = (float) (rp - i0);
                float y = line[(size_t) i0] + frac * (line[(size_t) i1] - line[(size_t) i0]);

                if (dr > 0.001f)
                    y = std::tanh (g * y) * makeup;

                wet.setSample (ch, i, y);
            }

            writePos = (writePos + 1) % size;
            phase += inc;          if (phase >= 1.0) phase -= 1.0;
            flutterPhase += flutterInc; if (flutterPhase >= 1.0) flutterPhase -= 1.0;
        }
    }

private:
    double sr = 48000.0;
    int size = 0, writePos = 0;
    std::vector<float> lines[2];
    double phase = 0.0, flutterPhase = 0.0;
    juce::SmoothedValue<float> depthSm, driveSm;
};
