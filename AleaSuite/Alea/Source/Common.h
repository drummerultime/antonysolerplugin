#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include "../../Shared/Core.h"

namespace alea
{
constexpr int kNumTracks   = 8;
constexpr int kMaxSteps    = 32;
constexpr int kNumPatterns = 8;
constexpr int kNumEngines  = 10;
constexpr int kNumMxSlots  = 8;
constexpr int kMaxChain    = 32;
constexpr int kNumScenes   = 8;
constexpr int kNumFavs     = 8;

enum Engine { EngKick = 0, EngSnare, EngMetal, EngClap, EngFM, EngModal, EngPluck, EngZap, EngHybrid, EngSample };

inline const juce::StringArray& engineNames()
{
    static const juce::StringArray s { "Kick", "Snare", "Metal", "Clap", "FM", "Modal", "Pluck", "Zap", "Hybrid 909", "Sample" };
    return s;
}

// Parametres continus d'une voix. Chacun possede un "jumeau" d'alea (_rnd).
// L'ordre de l'enum est l'ordre de stockage ; l'affichage utilise vpDisplayOrder().
enum VP
{
    Tune = 0, Decay, Attack, PitchAmt, PitchDecay,
    Timbre, Color, Noise,
    Cutoff, Reso, FEnv,
    Drive, Crush,
    Level, Pan, SendDly, SendRev,
    Punch, FxAmt, Layer,
    kNumVP
};

struct VPInfo { const char* id; const char* label; };

inline const VPInfo& vpInfo (int i)
{
    static const VPInfo t[kNumVP] = {
        { "tune", "Tune" },   { "decay", "Decay" }, { "attack", "Attack" },
        { "pamt", "P.Env" },  { "pdec", "P.Dec" },
        { "timbre", "Timbre" }, { "color", "Color" }, { "noise", "Noise" },
        { "cutoff", "Cutoff" }, { "reso", "Reso" }, { "fenv", "F.Env" },
        { "drive", "Drive" }, { "crush", "Crush" },
        { "level", "Level" }, { "pan", "Pan" }, { "sdly", "Dly Snd" }, { "srev", "Rev Snd" },
        { "punch", "Punch" }, { "fxamt", "FX Amt" }, { "layer", "Smp Layer" } };
    return t[i];
}

inline const std::array<int, kNumVP>& vpDisplayOrder()
{
    static const std::array<int, kNumVP> o { Tune, Decay, Attack, Punch, PitchAmt, PitchDecay, Timbre, Color, Noise, Layer,
                                             Cutoff, Reso, FEnv, Drive, Crush, FxAmt, Level, Pan, SendDly, SendRev };
    return o;
}

inline bool vpIsBipolar (int vp) { return vp == Tune || vp == Pan || vp == FEnv || vp == Punch; }

inline juce::String tid   (int t, const char* name) { return "t" + juce::String (t + 1) + "_" + name; }
inline juce::String vpId  (int t, int vp)           { return tid (t, vpInfo (vp).id); }
inline juce::String jitId (int t, int vp)           { return vpId (t, vp) + "_rnd"; }
inline juce::String mxId  (int s, const char* name) { return "mx" + juce::String (s + 1) + name; }
inline juce::String modId (int m, const char* name) { return "m" + juce::String (m + 1) + name; }

// Tous les parametres de voix sont normalises 0..1 ; conversions vers unites reelles :
namespace map
{
    inline float tuneSemis     (float n) { return (n - 0.5f) * 48.0f; }
    inline float decaySec      (float n) { return 0.005f * std::pow (600.0f, n); }
    inline float attackSec     (float n) { return n * n * n * 0.1f; }
    inline float pitchSemis    (float n) { return n * 48.0f; }
    inline float pitchDecaySec (float n) { return 0.001f * std::pow (400.0f, n); }
    inline float cutoffHz      (float n) { return 20.0f * std::pow (1000.0f, n); }
    inline float fenvOct       (float n) { return (n - 0.5f) * 12.0f; }
    inline float levelDb       (float n) { return 60.0f * n - 48.0f; }
    inline float levelGain     (float n) { return n <= 0.001f ? 0.0f : juce::Decibels::decibelsToGain (levelDb (n)); }
    inline float pan           (float n) { return (n - 0.5f) * 2.0f; }
    inline float punch         (float n) { return (n - 0.5f) * 2.0f; }
}

juce::String vpToText (int vp, float n);













// Cibles globales de la matrice (apres les kNumVP parametres de voix)
enum GlobalTarget { GDlyFb = 0, GDlyTone, GDlyRet, GRevSize, GRevRet, GDensity, GChaos, GSeqChaos, GRatChaos, GStutter, GSwing, GDuck, GMDrive, GEvo, kNumGT };
inline const juce::StringArray& globalTargetNames()
{
    static const juce::StringArray s { "G:Dly Fb", "G:Dly Tone", "G:Dly Ret", "G:Rev Size", "G:Rev Ret", "G:Density", "G:Chaos",
                                       "G:Seq Chaos", "G:Ratch Chaos", "G:Stutter", "G:Swing", "G:Duck", "G:M.Drive", "G:Evolve" };
    return s;
}

// ---------------------------------------------------------------------------
// Kit par defaut
struct KitVoice
{
    const char* name;
    int engine;
    float v[kNumVP];
    float j[kNumVP];
    int choke;
};
const KitVoice& defaultKit (int track);
float defaultVP (int track, int vp);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// Plages "musicales" utilisees par le de (dice) par moteur
void diceRange (int engine, int vp, float& lo, float& hi);

} // namespace alea
