#pragma once

// MARIA - definitions communes : sources, moteurs d'effets, improvisatrice, textes.
#include <juce_audio_processors/juce_audio_processors.h>

namespace ma
{
constexpr int kNumSlots   = 4;
constexpr int kNumP       = 6;
constexpr int kChance     = 6;     // chance par segment
constexpr int kMix        = 7;
constexpr int kNumSP      = 8;
constexpr int kNumMacros  = 8;
constexpr int kNumRows    = 8;
constexpr int kNumRMods   = 5;
constexpr int kNumScenes  = 48;    // une scene par sequence du film (6 banques de 8)
constexpr int kSceneBank  = 8;
constexpr int kSub        = 32;

// --- Moteurs d'effets (voix de Maria)
enum Type
{
    TOff = 0, TDelay, TScatter, TStutter, TGranular, TReverb, TShimmer, TReverse, TMod, TFilter,
    TPitch, TDegrade, TDist, TRing, TGate, TResonator, TBody, TCross,
    kNumTypes,
    // moteurs de Grot non exposes dans Maria (gardes pour la compatibilite du code)
    TTransient = 100, TSmash, TSub, TNoise
};
enum Kind { KInsert = 0, KSend = 1, KTrigger = 2 };
Kind typeKind (int type);

const juce::StringArray& typeNames();
const juce::StringArray& typeShort();
juce::String paramLabel (int type, int p);
juce::String paramText (int type, int p, float v, float aux = 0.0f, float fund = 100.0f);
int auxParamFor (int type, int p);
juce::String typeHelp (int type);

// --- Improvisatrice : 12 parametres continus (avec hasard) + mode
enum ImParam { IAct = 0, IReact, IPhrase, IGap, ICont, ICtx, IMem, IFollow, ITransp, IRev, IPolite, ILevel, kNumIm };
juce::String imId (int i);
juce::String imJid (int i);
juce::String imLabel (int i);
juce::String imText (int i, float v);
float imDefault (int i);
const juce::StringArray& imModes();          // Dialogue, Duo, Echo, Contrepoint, Contraste, Machine
const juce::StringArray& tempoModes();       // Libre, Synchro DAW, Tempo manuel
const juce::StringArray& gridNames();        double gridBeats (int i);
const juce::StringArray& alignNames();       double alignBeats (int i);
enum ImMode { MDialogue = 0, MDuo, MEcho, MCounter, MContrast, MMachine, kNumImModes };
// conversions DSP
inline float imReactSec (float v)  { return 0.1f * std::pow (40.0f, v); }        // 0.1 .. 4 s
inline float imPhraseSec (float v) { return 0.5f * std::pow (40.0f, v); }        // 0.5 .. 20 s
inline float imGapSec (float v)    { return 0.2f * std::pow (50.0f, v); }        // 0.2 .. 10 s
inline int   imCtxMin (float v)    { return 1 + juce::jlimit (0, 7, (int) (v * 8.0f)); }
inline float imTranspSemis (float v) { return std::round ((v - 0.5f) * 48.0f); }

// --- Sources (profils d'ecoute et d'analyse)
struct SourceProfile
{
    const char* name;
    float lo, hi;           // bande de detection des attaques
    float fmin, fmax;       // plage de hauteur (0 = non accorde)
    int symMode;            // 0 timbre, 1 hauteur, 2 hybride
    float minSegMs, maxSegMs, refracMs, xfadeMs;
    float fund;             // fondamentale typique (accord des moteurs)
    float body;
};
constexpr int kNumSources = 18;
const SourceProfile& sourceProfile (int i);
juce::StringArray sourceNames();

inline int pick (float v, int n) noexcept { return juce::jlimit (0, n - 1, (int) (v * (float) n)); }

// --- Nappe douce : adoucit les reglages qui "tournent" (vitesse de modulation, reinjection, resonance,
// sauts aleatoires) sans toucher au caractere de l'effet. s = 0 : inchange, 1 : le plus doux.
// p : les 6 reglages normalises (0..1) du moteur, modifies sur place.
inline void softenParams (int type, float* p, float s) noexcept
{
    if (s <= 0.0f) return;
    s = juce::jmin (1.0f, s);
    auto cut  = [s] (float& v, float amt) { v *= 1.0f - amt * s; };                       // vers 0
    auto mid  = [s] (float& v, float amt) { v = 0.5f + (v - 0.5f) * (1.0f - amt * s); };  // vers le centre (reglage bipolaire)
    switch (type)
    {
        case TMod:                                   // chorus / flanger / phaser : lent, peu profond, peu de reinjection
            cut (p[1], 0.75f); cut (p[2], 0.5f);
            if (pick (p[0], 3) == 1) mid (p[3], 0.85f); else cut (p[3], 0.8f);
            break;
        case TFilter:   cut (p[2], 0.7f); mid (p[3], 0.7f); cut (p[4], 0.8f); break;   // resonance, sauts d'octave
        case TRing:     cut (p[2], 0.8f); cut (p[4], 0.8f); cut (p[5], 0.75f); break;  // freq aleatoire, reinjection, LFO
        case TShimmer:  cut (p[5], 0.7f); break;                                         // modulation
        case TReverb:   cut (p[5], 0.7f); break;                                         // gate / modulation
        case TDelay:    cut (p[4], 0.8f); break;                                         // wobble
        case TDegrade:  cut (p[3], 0.7f); break;                                         // wow
        case TPitch:    cut (p[4], 0.7f); cut (p[5], 0.5f); break;                       // hasard, desaccord
        default: break;
    }
}
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
const juce::StringArray& shimmerIntervals(); float shimmerSemis (int i, int k);
const juce::StringArray& resScales();      int resScaleShared (int i);
const juce::StringArray& noteNames();
juce::StringArray resRoots();

// Horloge aleatoire
const juce::StringArray& clockNames();     double clockBeats (int i);
constexpr int kClockFree = 8, kClockSeg = 9, kClockPhrase = 10, kNumClocks = 11;
const juce::StringArray& evolveBarNames(); int evolveBars (int i);

// Matrice
const juce::StringArray& modSourceNames();
constexpr int kNumModSrc = 18;
constexpr int kSrcSeg = 8, kSrcPlayer = 9, kSrcMacro = 10;
juce::StringArray modDestNames();
constexpr int kDestIm = 1 + kNumSlots * kNumSP;                 // parametres de l'improvisatrice
constexpr int kDestMix = kDestIm + kNumIm, kDestChaos = kDestMix + 1, kDestWidth = kDestMix + 2;
constexpr int kNumDests = kDestWidth + 1;

juce::String sid (int slot, const char* what);
juce::String pid (int slot, int p);
juce::String jid (int slot, int p);
} // namespace ma
