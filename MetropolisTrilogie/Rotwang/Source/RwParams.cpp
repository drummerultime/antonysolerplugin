#include "RwParams.h"
#include "Core.h"

namespace rw
{
const juce::StringArray& typeNames()
{
    static const juce::StringArray s { "- vide -", "Delay", "Scatter", "Stutter", "Granular", "Plate", "Shimmer", "Reverse",
                                       "Chorus", "Flanger", "Phaser", "Filtre", "Pitch", "Degrade", "Ring / Shift", "Gate", "Resonator" };
    return s;
}
const juce::StringArray& typeShort()
{
    static const juce::StringArray s { "-", "DLY", "SCT", "STU", "GRN", "PLT", "SHM", "REV", "CHO", "FLG", "PHS", "FLT", "PIT", "DEG", "RNG", "GAT", "RES" };
    return s;
}

const juce::StringArray& delayDivs()
{
    static const juce::StringArray s { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1 bar" };
    return s;
}
double delayDivBeats (int i)
{
    static const double v[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0 };
    return v[juce::jlimit (0, 11, i)];
}
const juce::StringArray& stutterDivs()
{
    static const juce::StringArray s { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2" };
    return s;
}
double stutterDivBeats (int i)
{
    static const double v[] { 1.0 / 16.0, 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0 };
    return v[juce::jlimit (0, 8, i)];
}
const juce::StringArray& segNames() { static const juce::StringArray s { "1/8", "1/4", "1/2", "1 bar", "2 bars" }; return s; }
double segBeats (int i) { static const double v[] { 0.5, 1.0, 2.0, 4.0, 8.0 }; return v[juce::jlimit (0, 4, i)]; }
const juce::StringArray& gateDivs() { static const juce::StringArray s { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4" }; return s; }
double gateDivBeats (int i) { static const double v[] { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0 }; return v[juce::jlimit (0, 5, i)]; }
const juce::StringArray& scatterRates() { static const juce::StringArray s { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "Aleatoire" }; return s; }
double scatterRateBeats (int i) { static const double v[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 0.0 }; return v[juce::jlimit (0, 6, i)]; }
const juce::StringArray& revLens() { static const juce::StringArray s { "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }; return s; }
double revLenBeats (int i) { static const double v[] { 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }; return v[juce::jlimit (0, 5, i)]; }
const juce::StringArray& filterTypes() { static const juce::StringArray s { "Passe-bas", "Passe-bande", "Passe-haut", "Notch", "Comb", "Formant" }; return s; }
const juce::StringArray& ringModes() { static const juce::StringArray s { "Ring", "Freq shift", "AM" }; return s; }
const juce::StringArray& shimmerIntervals() { static const juce::StringArray s { "+12", "+7", "+5", "+19", "+24", "-12", "+12/+7", "+7/+5" }; return s; }
float shimmerSemis (int i, int k)
{
    switch (juce::jlimit (0, 7, i))
    {
        case 0: return 12.0f;  case 1: return 7.0f;  case 2: return 5.0f;  case 3: return 19.0f;
        case 4: return 24.0f;  case 5: return -12.0f;
        case 6: return (k & 1) ? 7.0f : 12.0f;
        default: return (k & 1) ? 5.0f : 7.0f;
    }
}
const juce::StringArray& resScales()
{
    static const juce::StringArray s { "Majeur", "Mineur", "Dorien", "Phrygien", "Lydien", "Mixolydien", "Penta maj", "Penta min",
                                       "Par tons", "Hirajoshi", "Persan", "Octaves" };
    return s;
}
int resScaleShared (int i) { static const int v[] { 2, 3, 8, 9, 10, 11, 4, 5, 6, 20, 31, 7 }; return v[juce::jlimit (0, 11, i)]; }
const juce::StringArray& noteNames() { static const juce::StringArray s { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return s; }

const juce::StringArray& clockNames() { static const juce::StringArray s { "Off", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "Libre" }; return s; }
double clockBeats (int i) { static const double v[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 0.0 }; return v[juce::jlimit (0, 8, i)]; }
const juce::StringArray& evolveBarNames() { static const juce::StringArray s { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }; return s; }
int evolveBars (int i) { static const int v[] { 1, 2, 4, 8, 16 }; return v[juce::jlimit (0, 4, i)]; }

const juce::StringArray& modSourceNames()
{
    static const juce::StringArray s { "-", "S&H", "Smooth", "Turing", "Chaos", "LFO", "Enveloppe", "Horloge",
                                       "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    return s;
}
juce::StringArray modDestNames()
{
    juce::StringArray s { "-" };
    for (int sl = 0; sl < kNumSlots; ++sl)
        for (int p = 0; p < kNumSP; ++p)
            s.add ("S" + juce::String (sl + 1) + (p < 6 ? " P" + juce::String (p + 1) : juce::String (" Mix")));
    s.add ("Mix global"); s.add ("Chaos"); s.add ("Largeur");
    return s;
}

juce::String sid (int slot, const char* what) { return "s" + juce::String (slot + 1) + "_" + what; }
juce::String pid (int slot, int p) { return p < 6 ? "s" + juce::String (slot + 1) + "_p" + juce::String (p + 1) : sid (slot, "mix"); }
juce::String jid (int slot, int p) { return p < 6 ? "s" + juce::String (slot + 1) + "_j" + juce::String (p + 1) : sid (slot, "jmix"); }

// ---------------------------------------------------------------------------
juce::String paramLabel (int type, int p)
{
    if (p == 6) return "Mix";
    static const char* L[kNumTypes][6] = {
        { "P1", "P2", "P3", "P4", "P5", "P6" },
        { "Temps", "Feedback", "Tone", "Ping-pong", "Wobble", "Sync" },
        { "Portee", "Densite", "Feedback", "Grille", "Pan", "Cadence" },
        { "Division", "Chance", "Duree", "Pitch", "Reverse", "Thru" },
        { "Taille", "Densite", "Spray", "Pitch", "Pitch rnd", "Reverse" },
        { "Decay", "Taille", "Damp", "Predelay", "Mod", "Largeur" },
        { "Decay", "Taille", "Shimmer", "Intervalle", "Damp", "Mod" },
        { "Longueur", "Decay", "Taille", "Brut", "Swell", "Damp" },
        { "Vitesse", "Profond.", "Voix", "Retard", "Feedback", "Largeur" },
        { "Vitesse", "Profond.", "Feedback", "Manuel", "Stereo", "LFO rnd" },
        { "Vitesse", "Profond.", "Feedback", "Etages", "Centre", "Stereo" },
        { "Type", "Cutoff", "Reso", "Env", "LFO", "LFO vit." },
        { "Shift", "Fin", "Grain", "Feedback", "Rnd", "Detune" },
        { "Drive", "Bits", "Rate", "Wow", "Bruit", "Tone" },
        { "Mode", "Freq", "Rnd freq", "Stereo", "Feedback", "LFO" },
        { "Division", "Densite", "Mutation", "Forme", "Swing", "Pan rnd" },
        { "Tonique", "Gamme", "Decay", "Brillance", "Etendue", "Rnd notes" } };
    return L[juce::jlimit (0, kNumTypes - 1, type)][juce::jlimit (0, 5, p)];
}

static juce::String pct (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; }
static juce::String ms (float m) { return m < 100.0f ? juce::String (m, 1) + " ms" : (m < 1000.0f ? juce::String ((int) m) + " ms" : juce::String (m * 0.001f, 2) + " s"); }
static juce::String hz (float f) { return f < 10.0f ? juce::String (f, 2) + " Hz" : (f < 1000.0f ? juce::String ((int) f) + " Hz" : juce::String (f * 0.001f, 1) + " kHz"); }
static juce::String st (float s) { return (s > 0 ? "+" : "") + juce::String ((int) s) + " st"; }

int auxParamFor (int type, int p)
{
    if (type == TDelay && p == 0) return 5;
    if (type == TRing && p == 1) return 0;
    return -1;
}

static juce::String ringFreqText (float v, float mode)
{
    if (pick (mode, 3) == 1)
    {
        const float x = bip (v);
        const float f = (x < 0 ? -1.0f : 1.0f) * x * x * 2000.0f;
        return (f >= 0 ? "+" : "") + juce::String (f, std::abs (f) < 10 ? 2 : 0) + " Hz";
    }
    return hz (expMap (v, 0.1f, 4000.0f));
}

juce::String paramText (int type, int p, float v, float aux)
{
    if (p == 6) return pct (v);
    switch (type)
    {
        case TDelay:
            switch (p) { case 0: return aux >= 0.5f ? delayDivs()[pick (v, 12)] : ms (expMap (v, 10, 2000)); case 1: return pct (v * 1.1f); case 2: return "Tone " + juce::String ((int) std::round (bip (v) * 100)); case 5: return v >= 0.5f ? "Tempo" : "Libre"; default: return pct (v); }
        case TScatter:
            switch (p) { case 0: return ms (expMap (v, 50, 2000)); case 1: return juce::String (1 + (int) std::round (v * 7)) + " taps"; case 5: return scatterRates()[pick (v, 7)]; default: return pct (v); }
        case TStutter:
            switch (p) { case 0: return stutterDivs()[pick (v, 9)]; case 2: return segNames()[pick (v, 5)];
                         case 3: return juce::String (bip (v) * 3.0f, 1) + " st/rep"; default: return pct (v); }
        case TGranular:
            switch (p) { case 0: return ms (expMap (v, 10, 500)); case 1: return juce::String (expMap (v, 1, 80), 1) + " /s"; case 2: return ms (v * v * 3000.0f);
                         case 3: return st (semis48 (v)); default: return pct (v); }
        case TPlate:
            switch (p) { case 0: return juce::String (expMap (v, 0.3f, 20.0f), 1) + " s"; case 3: return ms (v * 250.0f); case 5: return pct (v * 2.0f); default: return pct (v); }
        case TShimmer:
            switch (p) { case 0: return juce::String (expMap (v, 0.5f, 30.0f), 1) + " s"; case 3: return shimmerIntervals()[pick (v, 8)]; default: return pct (v); }
        case TReverse:
            switch (p) { case 0: return revLens()[pick (v, 6)]; case 1: return juce::String (expMap (v, 0.3f, 12.0f), 1) + " s"; default: return pct (v); }
        case TChorus:
            switch (p) { case 0: return hz (expMap (v, 0.03f, 8.0f)); case 2: return juce::String (1 + pick (v, 4)) + " voix"; case 3: return ms (5.0f + v * 35.0f); default: return pct (v); }
        case TFlanger:
            switch (p) { case 0: return hz (expMap (v, 0.02f, 5.0f)); case 2: return pct (bip (v) * 0.95f); case 3: return ms (expMap (v, 0.1f, 10.0f));
                         case 4: return juce::String ((int) (v * 180)) + " deg"; default: return pct (v); }
        case TPhaser:
            switch (p) { case 0: return hz (expMap (v, 0.02f, 8.0f)); case 3: { static const int n[] { 2, 4, 6, 8, 12 }; return juce::String (n[pick (v, 5)]) + " etages"; }
                         case 4: return hz (expMap (v, 100, 4000)); case 5: return juce::String ((int) (v * 180)) + " deg"; default: return pct (v); }
        case TFilter:
            switch (p) { case 0: return filterTypes()[pick (v, 6)]; case 1: return hz (expMap (v, 20, 18000)); case 3: return pct (bip (v));
                         case 5: return hz (expMap (v, 0.02f, 10.0f)); default: return pct (v); }
        case TPitch:
            switch (p) { case 0: return st (semis48 (v)); case 1: return juce::String ((int) std::round (bip (v) * 50)) + " ct"; case 2: return ms (expMap (v, 15, 150));
                         case 5: return juce::String ((int) (v * 30)) + " ct"; default: return pct (v); }
        case TDegrade:
            switch (p) { case 0: return "+" + juce::String ((int) (v * 30)) + " dB"; case 1: return juce::String ((int) std::round (expMap (v, 16, 2))) + " bits";
                         case 2: return hz (expMap (v, 48000, 300)); case 5: return hz (expMap (v, 18000, 800)); default: return pct (v); }
        case TRing:
            switch (p) { case 0: return ringModes()[pick (v, 3)]; case 1: return ringFreqText (v, aux); default: return pct (v); }
        case TGate:
            switch (p) { case 0: return gateDivs()[pick (v, 6)]; case 3: return v < 0.5f ? "doux " + pct (1 - v * 2) : "sec " + pct (v * 2 - 1); default: return pct (v); }
        case TResonator:
            switch (p) { case 0: return noteNames()[pick (v, 12)]; case 1: return resScales()[pick (v, 12)]; case 4: return juce::String (1 + pick (v, 4)) + " oct"; default: return pct (v); }
        default: return pct (v);
    }
}

juce::String typeHelp (int type)
{
    switch (type)
    {
        case TDelay:     return "Delay stereo bande : temps libre ou au tempo, wobble = pleurage aleatoire.";
        case TScatter:   return "Multi-taps aleatoires : les echos sont redistribues a chaque cadence.";
        case TStutter:   return "Beat-repeat : capture et repete des tranches au tempo (Thru = son direct entre les repetitions).";
        case TGranular:  return "Nuage granulaire sur 4 s de memoire : textures, gels, pitch aleatoire musical.";
        case TPlate:     return "Plate a reseau de retards (FDN 8 lignes), decay jusqu'a 20 s.";
        case TShimmer:   return "Reverbe dont la queue est transposee et reinjectee (octave, quinte...).";
        case TReverse:   return "Reverbe inversee : chaque tranche ressort a l'envers (montee).";
        case TChorus:    return "Chorus / ensemble 1 a 4 voix avec derive aleatoire.";
        case TFlanger:   return "Flanger stereo, feedback positif ou negatif, LFO melange a du hasard lisse.";
        case TPhaser:    return "Phaser 2 a 12 etages avec feedback.";
        case TFilter:    return "Filtre multimode + suiveur d'enveloppe + LFO (Formant : Cutoff choisit la voyelle).";
        case TPitch:     return "Transposition par grains, feedback en cascade, intervalles aleatoires a chaque pas d'horloge.";
        case TDegrade:   return "Saturation, bits, sous-echantillonnage, pleurage de bande, souffle et craquements.";
        case TRing:      return "Modulation en anneau, decalage de frequence (barber-pole avec feedback) ou AM.";
        case TGate:      return "Hacheur rythmique : motif aleatoire de 16 pas qui mute a chaque mesure.";
        case TResonator: return "Banc de resonateurs accordes sur une gamme ; Rnd notes re-tire les notes a chaque pas d'horloge.";
        default:         return "Slot vide : choisis un moteur dans le menu.";
    }
}
} // namespace rw
