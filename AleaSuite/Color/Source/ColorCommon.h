#pragma once

#include "../../Shared/Core.h"
#include "ColorTheory.h"

// COLOR : instrument harmonique (accords, nappes, stabs, arpeges), troisieme membre de la suite Alea / Talea.
namespace color
{
using namespace alea;   // briques communes (hasard, subdivisions, conditions...)

constexpr int kSteps    = 32;
constexpr int kPatterns = 8;
constexpr int kScenes   = 8;
constexpr int kFavs     = 8;
constexpr int kMxSlots  = 8;
constexpr int kChain    = 32;
constexpr int kMaxChords = 16;
constexpr int kVoices   = 8;

// Parametres continus de la voix (memes positions que Talea pour partager le code ; Accent -> Vel Filter, Pan -> Width)
enum TP
{
    Shape = 0, O2Level, O2Semi, O2Fine, FM, SubLevel, NoiseLvl,
    Cutoff, Reso, EnvMod, FDecay, VelFilt, KeyTrack, FDrive,
    Attack, Decay, Sustain, Release, Glide,
    Drive, Level, Width, SendDly, SendRev,
    kNumTP
};

struct TPInfo { const char* id; const char* label; float def; float jit; };
const TPInfo& tpInfo (int i);
inline bool tpBipolar (int i) { return i == O2Semi || i == O2Fine; }
inline juce::String tpId (int i)  { return tpInfo (i).id; }
inline juce::String tpJit (int i) { return tpId (i) + "_rnd"; }
const std::array<int, kNumTP>& tpDisplayOrder();

inline const juce::StringArray& osc1Names()   { static const juce::StringArray s { "Saw", "Square", "Pulse", "Triangle", "Sine", "SuperSaw", "Fold", "Pluck" }; return s; }
inline const juce::StringArray& osc2Names()   { static const juce::StringArray s { "Osc2: Off", "Saw", "Square", "Sine", "Triangle", "Noise" }; return s; }
inline const juce::StringArray& subNames()    { static const juce::StringArray s { "Sub: Sine -1", "Sub: Square -1", "Sub: Sine -2" }; return s; }
inline const juce::StringArray& filterNames() { static const juce::StringArray s { "303 Diode", "Ladder 24", "SVF LP 12", "SVF BP", "SVF HP", "Filter Off" }; return s; }
inline const juce::StringArray& driveNames()  { static const juce::StringArray s { "Soft", "Hard", "Fold", "Tube", "Crush" }; return s; }
inline const juce::StringArray& glideModeNames() { static const juce::StringArray s { "Glide: Off", "Glide: Voice lead", "Glide: Always" }; return s; }
inline const juce::StringArray& keysModeNames()  { static const juce::StringArray s { "Keys: Off", "Keys: Transpose", "Keys: Chord select", "Keys: Harmonize", "Keys: Play" }; return s; }
inline const juce::StringArray& playModeNames()  { static const juce::StringArray s { "Play: Pad", "Play: Comp", "Play: Strum", "Play: Arp" }; return s; }
inline const juce::StringArray& arpModeNames()   { static const juce::StringArray s { "Arp: Up", "Arp: Down", "Arp: Up-Down", "Arp: Random", "Arp: Lead (scale)" }; return s; }
inline const juce::StringArray& bassModeNames()  { static const juce::StringArray s { "Bass: Off", "Bass: Root/Inv", "Bass: Pedal" }; return s; }
inline const juce::StringArray& chordLenNames()  { static const juce::StringArray s { "2 beats", "1 bar", "2 bars" }; return s; }
inline const juce::StringArray& layerNames()     { static const juce::StringArray s { "Layer: Off", "Layer: Bell (FM)", "Layer: Glass", "Layer: Breath", "Layer: Octave" }; return s; }
inline const juce::StringArray& rootNames() { static const juce::StringArray s { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return s; }
inline const juce::StringArray& partNames() { static const juce::StringArray s { "Full", "Top", "Bass", "No bass" }; return s; }
inline const juce::StringArray& rhythmNames()
{
    static const juce::StringArray s { "Rhythm: Whole", "Rhythm: Quarters", "Rhythm: Offbeat stabs", "Rhythm: Charleston", "Rhythm: Clave 3-2",
                                       "Rhythm: Pushes", "Rhythm: Arp 16ths", "Rhythm: House chops", "Rhythm: Euclid", "Rhythm: Random" };
    return s;
}

// Cibles globales de la matrice
enum GT { GDlyFb = 0, GDlyRet, GRevSize, GRevRet, GChorus, GDensity, GChaos, GSeqChaos, GRatChaos, GSwing, GDuck, GMDrive, GEvo,
          GTension, GComplex, GSpread, GRegister, GLiveSub, kNumGT };
inline const juce::StringArray& gtNames()
{
    static const juce::StringArray s { "G:Dly Fb", "G:Dly Ret", "G:Rev Size", "G:Rev Ret", "G:Chorus", "G:Density", "G:Chaos",
                                       "G:Seq Chaos", "G:Ratch Chaos", "G:Swing", "G:Duck", "G:M.Drive", "G:Evolve",
                                       "G:Tension", "G:Complexity", "G:Spread", "G:Register", "G:Live Sub" };
    return s;
}

namespace tmap
{
    inline float semis (float n)      { return std::round ((n - 0.5f) * 48.0f); }
    inline float cents (float n)      { return (n - 0.5f) * 100.0f; }
    inline float cutoffHz (float n)   { return 20.0f * std::pow (900.0f, n); }
    inline float envOct (float n)     { return n * 7.0f; }
    inline float fdecaySec (float n)  { return 0.01f * std::pow (300.0f, n); }
    inline float attackSec (float n)  { return n * n * 3.0f; }
    inline float decaySec (float n)   { return 0.01f * std::pow (500.0f, n); }
    inline float releaseSec (float n) { return 0.005f * std::pow (1200.0f, n); }
    inline float glideSec (float n)   { return 0.005f * std::pow (100.0f, n); }
    inline float levelGain (float n)  { return n <= 0.001f ? 0.0f : juce::Decibels::decibelsToGain (60.0f * n - 48.0f); }
    inline float registerMidi (float n) { return 48.0f + n * 28.0f; }
}

juce::String tpToText (int tp, float n);
juce::AudioProcessorValueTreeState::ParameterLayout createColorLayout();
void tpDiceRange (int tp, float& lo, float& hi);

inline const int* chordIntervals (int, int& n) { static const int z[1] { 0 }; n = 1; return z; }   // la voix de COLOR joue une note

} // namespace color
