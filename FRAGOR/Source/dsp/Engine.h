#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>
#include "../Params.h"
#include "Wavetables.h"

namespace fragor
{
    constexpr int kLfoTableSize = 512;

    // ---------------------------------------------------------------- Instantané des paramètres
    struct OscP  { bool on; int table; float pos; int warpMode; float warp; int uni; float detune, blend, spread;
                   int oct, semi; float fine, level, pan; bool randPhase; };
    struct EnvP  { float a, d, s, r; };
    struct LfoP  { int shape; bool sync; int div; float rate; bool retrig; float phase, curve, smooth; };
    struct FiltP { bool on; int type; float cutoff, reso, drive, morph, key, mix, envAmt; };

    struct SynthParams
    {
        OscP osc[2];
        bool subOn; int subShape, subOct; float subLevel; bool subDirect; float noiseLevel, noiseColor;
        FiltP flt[2]; int routing;
        EnvP amp; float ampVel; EnvP e2, e3; float penvAmt, penvDecay;
        int voiceMode; float glide; int bendRange; float masterTune;
        LfoP lfo[3];
        bool seqOn; int seqSteps, seqLen; bool seqReset; int seq[kNumSeqSteps];
        int mSrc[kNumMatrixSlots], mDst[kNumMatrixSlots]; float mAmt[kNumMatrixSlots];
        float macro[kNumMacros];
        const float* lfoTable[3] { nullptr, nullptr, nullptr };       // formes CUSTOM
        const Wavetable* userTable[kNumUserTables] { nullptr, nullptr };
    };

    struct VoiceCtx
    {
        double sr = 44100.0, bpm = 140.0;
        double globalBeats = 0.0;   // position (noires) au début du sous-bloc
        double globalTime  = 0.0;   // secondes
        float  modWheel = 0.0f, bendSemis = 0.0f, chanPressure = 0.0f, slide = 0.0f;
        bool   mpe = false;
    };

    // ---------------------------------------------------------------- Enveloppe ADSR
    struct Envelope
    {
        enum Stage { Idle, Attack, Decay, Sustain, Release };
        Stage stage = Idle;
        float v = 0.0f;
        double sr = 44100.0;

        void gateOn()  { stage = Attack; }
        void gateOff() { if (stage != Idle) stage = Release; }
        void reset()   { stage = Idle; v = 0.0f; }
        bool active() const { return stage != Idle; }

        inline float next (const EnvP& p) noexcept
        {
            switch (stage)
            {
                case Attack:
                {
                    const float a = std::max (0.0005f, p.a);
                    v += (float) (1.0 / (a * sr));
                    if (v >= 1.0f) { v = 1.0f; stage = Decay; }
                    break;
                }
                case Decay:
                {
                    const float c = (float) std::exp (-4.6 / (std::max (0.001f, p.d) * sr));
                    v = p.s + (v - p.s) * c;
                    if (std::abs (v - p.s) < 1.0e-4f) { v = p.s; stage = Sustain; }
                    break;
                }
                case Sustain: v = p.s; if (v <= 0.0f) { stage = Idle; v = 0.0f; } break;
                case Release:
                {
                    const float c = (float) std::exp (-4.6 / (std::max (0.001f, p.r) * sr));
                    v *= c;
                    if (v < 1.0e-4f) { v = 0.0f; stage = Idle; }
                    break;
                }
                case Idle: default: v = 0.0f; break;
            }
            return v;
        }
    };

    // ---------------------------------------------------------------- LFO
    struct Lfo
    {
        double phase = 0.0, prevPh = 0.0;
        float smoothed = 0.0f;
        float sh = 0.0f, drift0 = 0.0f, drift1 = 0.0f;
        int64_t lastStepAbs = -1;
        bool first = true;

        static float shapeValue (int shape, double ph, float sh, float d0, float d1, const float* table = nullptr) noexcept;
        static float applyCurve (float y, float curve) noexcept;
    };

    // ---------------------------------------------------------------- Filtre SVF (TPT / Cytomic)
    struct Svf
    {
        float ic1[2] { 0, 0 }, ic2[2] { 0, 0 };
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
        struct Out { float lp, bp, hp; };
        inline Out tick (int ch, float v0, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            const float v3 = v0 - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            return { v2, v1, v0 - k * v1 - v2 };
        }
    };

    // Filtre multimode complet d'une voix (LP/BP/HP/Notch/Vowel/Comb)
    class VoiceFilter
    {
    public:
        void allocate (int combSize) { for (auto& c : comb) c.assign ((size_t) combSize, 0.0f); }
        void reset();
        // cutNorm : 0..1 (20 Hz .. 20 kHz log)
        void process (float& l, float& r, int type, float cutNorm, float reso, float drive, float morph, float mix, float fs) noexcept;
    private:
        Svf svf1, svf2, formant[3];
        std::vector<float> comb[2];
        int combPos = 0;
        float combLp[2] {};
    };

    // Décimateur demi-bande à phase linéaire (sur-échantillonnage des voix)
    class HalfbandDecimator
    {
    public:
        void design (int taps);
        void reset();
        // in : 2*n échantillons -> out : n échantillons
        void process (const float* in, float* out, int n) noexcept;
        float latencyIn() const { return (float) (N - 1) * 0.5f; } // en échantillons d'entrée
    private:
        int N = 0, centre = 0;
        std::vector<float> h, hist;  // hist doublé pour lecture contiguë
        std::vector<int> nz;
        int pos = 0;
    };

    // ---------------------------------------------------------------- Voix
    class Voice
    {
    public:
        void allocate (double maxRate);
        void setRate (double rate);
        void start (int note, float velocity, float glideFromNote, int channel, const SynthParams& p, const VoiceCtx& ctx, uint64_t order);
        void legatoTo (int note)  { targetNote = (float) note; noteNumber = note; }
        void release()            { gate = false; amp.gateOff(); env2.gateOff(); env3.gateOff(); }
        void kill()               { amp.reset(); env2.reset(); env3.reset(); active = false; gate = false; }

        void control (int numSamples, const SynthParams& p, const VoiceCtx& ctx);
        void render (float* L, float* R, int numSamples, const SynthParams& p);

        bool active = false, gate = false;
        int noteNumber = -1, channel = 1;
        uint64_t order = 0;
        float mods[numModDests] {};
        float mpeBend = 0.0f, pressure = 0.0f, slide = 0.0f;
        float getLfo (int i) const noexcept { return lfoOut[i]; }
        int64_t getSeqStepAbs() const noexcept { return lfo[0].lastStepAbs; }

    private:
        double sr = 44100.0;
        juce::Random rng;
        Envelope amp, env2, env3;
        Lfo lfo[3];
        float velocity = 1.0f, randomVal = 0.0f, pitchEnv = 0.0f;
        float currentNote = 60.0f, targetNote = 60.0f;
        double voiceBeats = 0.0, voiceTime = 0.0;
        float lfoOut[3] {};

        double phases[2][8] {};
        double subPhase = 0.0;
        float lastOsc[2] {}, lastSub = 0.0f;
        float syncPrev[2][8] {};
        float detRatio[2][8] {}, gainL[2][8] {}, gainR[2][8] {};
        int   mip[2] {};
        const Wavetable* tables[2] { nullptr, nullptr };
        uint32_t noiseSeed = 1;
        float noiseLp = 0.0f, noiseCoef = 0.5f; int noiseMode = 0;

        enum S { sIncA, sIncB, sIncSub, sPosA, sPosB, sWarpA, sWarpB, sLvlA, sLvlB, sSub, sNoise,
                 sCut, sReso, sMorph, sDrive, sMix, sCut2, sReso2, sMorph2, sDrive2, sMix2, sAmp, sPan, numSmooth };
        float target[numSmooth] {}, cur[numSmooth] {};
        float smoothCoef = 0.99f;
        bool snapNext = true;

        VoiceFilter filt[2];

        void computeUnison (int o, const OscP& op, float detune);
        inline float renderOsc (int o, const OscP& op, double inc, float pos, float warp, float fmIn, float ringIn, float& outL, float& outR) noexcept;
    };

    // ---------------------------------------------------------------- Moteur
    class Engine
    {
    public:
        static constexpr int kMaxVoices = 8;
        static constexpr int kMaxSub = 64;

        void prepare (double sampleRate);
        void setOversampling (int factor);   // 1, 2 ou 4
        int  getOversampling() const { return os; }
        float latencySamples() const;
        void reset();
        void noteOn (int note, float velocity, int channel, const SynthParams& p, const VoiceCtx& ctx);
        void noteOff (int note, int channel, const SynthParams& p);
        void sustainPedal (bool down, const SynthParams& p);
        void allNotesOff();

        // expression (MPE / aftertouch)
        void channelBend (int channel, float semis);
        void channelPressure (int channel, float v, bool mpe);
        void polyPressure (int note, float v);
        void channelSlide (int channel, float v, bool mpe);

        // Rendu d'un sous-bloc (<= kMaxSub échantillons)
        void render (float* L, float* R, int numSamples, const SynthParams& p, const VoiceCtx& ctx);

        float globalMod[numModDests] {};
        float liveMod[numModDests] {};
        float lfoMonitor[3] {};
        int seqMonitor = -1;
        int activeVoiceCount() const;

    private:
        std::array<Voice, kMaxVoices> voices;
        std::vector<int> monoStack;
        std::vector<int> sustained;
        bool pedal = false;
        uint64_t counter = 0;
        float lastNote = -1.0f;
        int lastMode = -1;
        double baseRate = 44100.0;
        int os = 1;
        HalfbandDecimator decA[2], decB[2];   // 4x->2x, 2x->1x
        std::vector<float> osL, osR, midL, midR;
    };
}
