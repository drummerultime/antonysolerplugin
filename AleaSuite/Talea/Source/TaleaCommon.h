#pragma once

#include "../../Shared/Core.h"

// TALEA : basse/synthe mono facon TB-303, satellite d'Alea.
namespace talea
{
using namespace alea;   // briques communes (hasard, gammes, subdivisions, conditions...)

constexpr int kSteps    = 32;
constexpr int kPatterns = 8;
constexpr int kScenes   = 8;
constexpr int kFavs     = 8;
constexpr int kMxSlots  = 8;
constexpr int kChain    = 32;

// Parametres continus de la voix (chacun a son jumeau d'alea _rnd)
enum TP
{
    Shape = 0, O2Level, O2Semi, O2Fine, FM, SubLevel, NoiseLvl,
    Cutoff, Reso, EnvMod, FDecay, AccentAmt, KeyTrack, FDrive,
    Attack, Decay, Sustain, Release, Glide,
    Drive, Level, Pan, SendDly, SendRev,
    kNumTP
};

struct TPInfo { const char* id; const char* label; float def; float jit; };
const TPInfo& tpInfo (int i);
inline bool tpBipolar (int i) { return i == O2Semi || i == O2Fine || i == Pan; }
inline juce::String tpId (int i)  { return tpInfo (i).id; }
inline juce::String tpJit (int i) { return tpId (i) + "_rnd"; }

const std::array<int, kNumTP>& tpDisplayOrder();

inline const juce::StringArray& osc1Names()   { static const juce::StringArray s { "Saw", "Square", "Pulse", "Triangle", "Sine", "SuperSaw", "Fold", "Pluck" }; return s; }
inline const juce::StringArray& osc2Names()   { static const juce::StringArray s { "Osc2: Off", "Saw", "Square", "Sine", "Triangle", "Noise" }; return s; }
inline const juce::StringArray& subNames()    { static const juce::StringArray s { "Sub: Sine -1", "Sub: Square -1", "Sub: Sine -2" }; return s; }
inline const juce::StringArray& filterNames() { static const juce::StringArray s { "303 Diode", "Ladder 24", "SVF LP 12", "SVF BP", "SVF HP", "Filter Off" }; return s; }
inline const juce::StringArray& driveNames()  { static const juce::StringArray s { "Soft", "Hard", "Fold", "Tube", "Crush" }; return s; }
inline const juce::StringArray& chordNames()  { static const juce::StringArray s { "Chord: Off", "Octave", "Fifth", "Minor", "Major", "Sus4", "Min7", "Maj7" }; return s; }
inline const juce::StringArray& glideModeNames() { static const juce::StringArray s { "Glide: Slide", "Glide: Always", "Glide: Off" }; return s; }
inline const juce::StringArray& keysModeNames()  { static const juce::StringArray s { "Keys: Off", "Keys: Transpose", "Keys: Play" }; return s; }
inline const juce::StringArray& rootNames() { static const juce::StringArray s { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return s; }
inline const juce::StringArray& genStyleNames()
{
    static const juce::StringArray s { "Acid", "Rolling", "Driving 8ths", "Dub", "Arp", "Euclid", "Follow kick", "Avoid kick", "Random", "Color chords" };
    return s;
}
constexpr int kNumGenStyles = 9;

// Cibles globales de la matrice
enum GT { GDlyFb = 0, GDlyRet, GRevSize, GRevRet, GChorus, GDensity, GChaos, GSeqChaos, GRatChaos, GSwing, GDuck, GMDrive, GEvo, kNumGT };
inline const juce::StringArray& gtNames()
{
    static const juce::StringArray s { "G:Dly Fb", "G:Dly Ret", "G:Rev Size", "G:Rev Ret", "G:Chorus", "G:Density", "G:Chaos",
                                       "G:Seq Chaos", "G:Ratch Chaos", "G:Swing", "G:Duck", "G:M.Drive", "G:Evolve" };
    return s;
}

inline const int* chordIntervals (int chord, int& n)
{
    static const int iv[8][4] { { 0 }, { 0, 12 }, { 0, 7 }, { 0, 3, 7 }, { 0, 4, 7 }, { 0, 5, 7 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 } };
    static const int cnt[8] { 1, 2, 2, 3, 3, 3, 4, 4 };
    chord = juce::jlimit (0, 7, chord);
    n = cnt[chord];
    return iv[chord];
}

namespace tmap
{
    inline float semis (float n)      { return std::round ((n - 0.5f) * 48.0f); }
    inline float cents (float n)      { return (n - 0.5f) * 100.0f; }
    inline float cutoffHz (float n)   { return 20.0f * std::pow (900.0f, n); }
    inline float envOct (float n)     { return n * 7.0f; }
    inline float fdecaySec (float n)  { return 0.01f * std::pow (300.0f, n); }
    inline float attackSec (float n)  { return n * n * 0.5f; }
    inline float decaySec (float n)   { return 0.01f * std::pow (500.0f, n); }
    inline float releaseSec (float n) { return 0.005f * std::pow (600.0f, n); }
    inline float glideSec (float n)   { return 0.005f * std::pow (100.0f, n); }
    inline float levelGain (float n)  { return n <= 0.001f ? 0.0f : juce::Decibels::decibelsToGain (60.0f * n - 48.0f); }
    inline float pan (float n)        { return (n - 0.5f) * 2.0f; }
}

juce::String tpToText (int tp, float n);

// position (en degres de gamme) d'un ecart en demi-tons, et deplacement de n degres dans la gamme
float scaleShift (float relSemis, int scale, int degrees);

juce::AudioProcessorValueTreeState::ParameterLayout createTaleaLayout();
void tpDiceRange (int tp, float& lo, float& hi);

} // namespace talea
