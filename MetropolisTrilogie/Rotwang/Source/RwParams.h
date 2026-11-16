#pragma once

// ROTWANG - definitions communes : moteurs, parametres, textes.
#include <juce_audio_processors/juce_audio_processors.h>

namespace rw
{
constexpr int kNumSlots   = 6;
constexpr int kNumP       = 6;     // parametres generiques par moteur
constexpr int kNumSP      = 7;     // + le mix du slot (index 6)
constexpr int kNumMacros  = 8;
constexpr int kNumRows    = 8;     // lignes de la matrice de modulation
constexpr int kNumRMods   = 5;     // S&H, Smooth, Turing, Chaos, LFO
constexpr int kNumScenes  = 8;
constexpr int kSub        = 32;    // sous-bloc de calcul des modulations

enum Type
{
    TOff = 0, TDelay, TScatter, TStutter, TGranular, TPlate, TShimmer, TReverse,
    TChorus, TFlanger, TPhaser, TFilter, TPitch, TDegrade, TRing, TGate, TResonator,
    kNumTypes
};

const juce::StringArray& typeNames();
const juce::StringArray& typeShort();
juce::String paramLabel (int type, int p);              // nom court du potard
juce::String paramText (int type, int p, float v, float aux = 0.0f);   // valeur affichee (aux : Sync du delay / Mode du ring)
int auxParamFor (int type, int p);                      // -1 si aucun
juce::String typeHelp (int type);                       // une ligne d'aide

// Listes discretes partagees DSP / affichage
inline int pick (float v, int n) noexcept { return juce::jlimit (0, n - 1, (int) (v * (float) n)); }

const juce::StringArray& delayDivs();      double delayDivBeats (int i);      // 12 valeurs
const juce::StringArray& stutterDivs();    double stutterDivBeats (int i);    // 9
const juce::StringArray& segNames();       double segBeats (int i);           // 5
const juce::StringArray& gateDivs();       double gateDivBeats (int i);       // 6
const juce::StringArray& scatterRates();   double scatterRateBeats (int i);   // 7
const juce::StringArray& revLens();        double revLenBeats (int i);        // 6
const juce::StringArray& filterTypes();
const juce::StringArray& ringModes();
const juce::StringArray& shimmerIntervals(); float shimmerSemis (int i, int k); // k : alternance
const juce::StringArray& resScales();      int resScaleShared (int i);
const juce::StringArray& noteNames();

// Durees exponentielles
inline float expMap (float v, float lo, float hi) noexcept { return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, v)); }
inline float semis48 (float v) noexcept { return std::round ((v - 0.5f) * 48.0f); }        // -24..+24
inline float bip (float v) noexcept { return v * 2.0f - 1.0f; }

// Horloge aleatoire
const juce::StringArray& clockNames();     double clockBeats (int i);         // 0 = Off, last = Libre
constexpr int kClockFree = 8;
const juce::StringArray& evolveBarNames(); int evolveBars (int i);

// Matrice de modulation
const juce::StringArray& modSourceNames();   // 0 = aucun
constexpr int kNumSources = 16;
juce::StringArray modDestNames();            // 0 = aucun, 1..42 slots, 43.. globaux
constexpr int kNumDests = 46;
constexpr int kDestMix = 43, kDestChaos = 44, kDestWidth = 45;

// Identifiants
juce::String sid (int slot, const char* what);
juce::String pid (int slot, int p);   // valeur p (6 = mix)
juce::String jid (int slot, int p);   // alea p
} // namespace rw
