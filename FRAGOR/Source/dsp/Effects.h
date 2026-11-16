#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../Params.h"

namespace fragor
{
    struct FxParams
    {
        int order[numFxModules];
        bool distOn;  int distMode; float distDrive, distBias, distTone, distMix;
        bool dist2On; int dist2Mode; float dist2Drive, dist2Bias, dist2Tone, dist2Mix;
        bool crushOn; float crushBits, crushRate, crushMix;
        bool ottOn;   float ottDepth, ottTime, ottUp, ottDown, ottOut;
        bool chorusOn;float chorusRate, chorusDepth, chorusMix;
        bool dlyOn;   int dlyDiv; float dlyFb, dlyTone; bool dlyPing; float dlyMix;
        bool revOn;   float revSize, revDamp, revWidth, revMix;
        bool subMono; float subMonoFreq, width, clip, masterGain;
    };

    // Rampe linéaire par sous-bloc pour éviter le "zipper"
    struct Ramp
    {
        float cur = 0.0f, step = 0.0f; bool init = false;
        void setTarget (float t, int n) { if (! init) { cur = t; init = true; step = 0.0f; return; } step = (t - cur) / (float) std::max (1, n); }
        float next() { cur += step; return cur; }
    };

    // ------------------------------------------------------------------ Distorsion sur-échantillonnée x4
    class Distortion
    {
    public:
        void prepare (double sr, int maxBlock);
        void reset();
        void process (float* L, float* R, int n, bool on, int mode, float driveNorm, float bias, float toneHz, float mix);
        int latency() const { return lat; }
    private:
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        double sr = 44100.0;
        int lat = 0;
        std::vector<float> dryBuf[2];
        int dryPos = 0;
        float driveSm = 1.0f, biasSm = 0.0f;
        float dcX[2] {}, dcY[2] {}, dcX2[2] {}, dcY2[2] {};
        float t1[2] {}, t2[2] {};
        Ramp mixR;
    };

    // ------------------------------------------------------------------ Bitcrusher
    class Crusher
    {
    public:
        void reset() { held[0] = held[1] = 0.0f; counter = 0.0f; }
        void process (float* L, float* R, int n, float bits, float rate, float mix);
    private:
        float held[2] {}, counter = 0.0f;
        Ramp mixR;
    };

    // ------------------------------------------------------------------ OTT : compression multibande haut/bas
    class Ott
    {
    public:
        void prepare (double sr, int maxBlock);
        void reset();
        void process (float* L, float* R, int n, float depth, float time, float up, float down, float outDb);
    private:
        double sr = 44100.0;
        juce::dsp::LinkwitzRileyFilter<float> xo1, xo2;
        float env[3] {};
        Ramp depthR;
    };

    // ------------------------------------------------------------------ Delay synchro (ping-pong)
    class SyncDelay
    {
    public:
        void prepare (double sr);
        void reset();
        void process (float* L, float* R, int n, double delaySamples, float fb, float toneHz, bool ping, float mix);
    private:
        double sr = 44100.0;
        std::vector<float> buf[2];
        int pos = 0;
        double dSm = -1.0;
        float lp[2] {};
        Ramp mixR;
    };

    // ------------------------------------------------------------------ Chaîne complète
    class FxChain
    {
    public:
        void prepare (double sr, int maxBlock);
        void reset();
        void process (float* L, float* R, int n, const FxParams& p, const float* globalMod, double bpm);
        int latency() const { return dist.latency() + dist2.latency(); }
    private:
        double sr = 44100.0;
        Distortion dist, dist2;
        Crusher crush;
        Ott ott;
        juce::dsp::Chorus<float> chorus;
        SyncDelay delay;
        juce::Reverb reverb;
        float sideIc1 = 0.0f, sideIc2 = 0.0f;
        float dcX[2] {}, dcY[2] {}, dcX2[2] {}, dcY2[2] {};
        Ramp widthR, gainR, revMixR;
        bool chorusWasOn = false, revWasOn = false;
    };
}
