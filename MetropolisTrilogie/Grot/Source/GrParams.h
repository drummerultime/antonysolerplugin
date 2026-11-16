#pragma once

// GROT - definitions communes : sources, moteurs, parametres, textes.
#include <juce_audio_processors/juce_audio_processors.h>

namespace gr
{
constexpr int kNumSlots   = 6;
constexpr int kNumP       = 6;     // parametres propres au moteur
constexpr int kChance     = 6;     // chance par coup
constexpr int kMix        = 7;     // mix du slot
constexpr int kNumSP      = 8;
constexpr int kNumMacros  = 8;
constexpr int kNumRows    = 8;
constexpr int kNumRMods   = 5;
constexpr int kNumScenes  = 8;
constexpr int kSub        = 32;

enum Type
{
    TOff = 0, TTransient, TSmash, TDist, TDegrade, TSub, TNoise, TBody, TResonator, TFilter, TPitch,
    TRing, TMod, TGate, TStutter, TGranular, TDelay, TScatter, TReverb, TReverse,
    kNumTypes
};

// Comment la "chance par coup" agit sur un moteur
enum Kind { KInsert = 0, KSend = 1, KTrigger = 2 };
Kind typeKind (int type);

const juce::StringArray& typeNames();
const juce::StringArray& typeShort();
juce::String paramLabel (int type, int p);
juce::String paramText (int type, int p, float v, float aux = 0.0f, float fund = 100.0f);
int auxParamFor (int type, int p);
juce::String typeHelp (int type);

// ---------------------------------------------------------------------------
// Sources (profils de detection et d'accord)
struct SourceProfile
{
    const char* name;
    float lo, hi;          // bande de detection (Hz)
    float fund;            // fondamentale typique (Hz)
    float body;            // frequence de "corps" (separation grave / aigu)
    float refracMs;        // intervalle minimal entre deux coups
    float decayMs;         // duree typique d'un coup
    int family;            // 0 kick, 1 caisse, 2 tom, 3 metal, 4 peaux main, 5 bus, 6 gong
};
constexpr int kNumSources = 19;
const SourceProfile& sourceProfile (int i);
juce::StringArray sourceNames();

inline int pick (float v, int n) noexcept { return juce::jlimit (0, n - 1, (int) (v * (float) n)); }
inline float expMap (float v, float lo, float hi) noexcept { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); }
inline float semis48 (float v) noexcept { return std::round ((v - 0.5f) * 48.0f); }
inline float bip (float v) noexcept { return v * 2.0f - 1.0f; }
float subTuneSemis (float v);
float bodyTuneSemis (float v);

const juce::StringArray& delayDivs();      double delayDivBeats (int i);
const juce::StringArray& stutterDivs();    double stutterDivBeats (int i);
const juce::StringArray& segNames();       double segBeats (int i);
const juce::StringArray& gateDivs();       double gateDivBeats (int i);
const juce::StringArray& scatterRates();   double scatterRateBeats (int i);
const juce::StringArray& revLens();        double revLenBeats (int i);
const juce::StringArray& filterTypes();
const juce::StringArray& ringModes();
const juce::StringArray& distModes();
const juce::StringArray& noiseModes();
const juce::StringArray& bodyMaterials();
const juce::StringArray& modModes();
const juce::StringArray& verbModes();
const juce::StringArray& bandNames();
const juce::StringArray& resScales();      int resScaleShared (int i);
const juce::StringArray& noteNames();
juce::StringArray resRoots();              // 12 notes + Auto

// Horloge aleatoire
const juce::StringArray& clockNames();     double clockBeats (int i);
constexpr int kClockFree = 8, kClockHit = 9, kClockHit2 = 10, kClockHit4 = 11, kNumClocks = 12;
const juce::StringArray& evolveBarNames(); int evolveBars (int i);

// Matrice
const juce::StringArray& modSourceNames();
constexpr int kNumModSrc = 18;
constexpr int kSrcHit = 8, kSrcVel = 9, kSrcMacro = 10;
juce::StringArray modDestNames();
constexpr int kNumDests = 1 + kNumSlots * kNumSP + 3;
constexpr int kDestMix = 1 + kNumSlots * kNumSP, kDestChaos = kDestMix + 1, kDestWidth = kDestMix + 2;

juce::String sid (int slot, const char* what);
juce::String pid (int slot, int p);
juce::String jid (int slot, int p);
} // namespace gr
