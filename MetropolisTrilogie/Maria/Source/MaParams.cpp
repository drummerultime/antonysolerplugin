#include "MaParams.h"
#include "Core.h"

namespace ma
{
const juce::StringArray& typeNames()
{
    static const juce::StringArray s { "- vide -", "Delay", "Scatter", "Stutter", "Granular", "Reverb", "Shimmer", "Reverse",
                                       "Modulation", "Filtre", "Pitch", "Degrade", "Distorsion", "Ring / Shift", "Gate", "Resonator", "Corps modal", "Cross-synthese" };
    return s;
}
const juce::StringArray& typeShort()
{
    static const juce::StringArray s { "-", "DLY", "SCT", "STU", "GRN", "VRB", "SHM", "REV", "MOD", "FLT", "PIT", "DEG", "DST", "RNG", "GAT", "RES", "BOD", "XSY" };
    return s;
}

Kind typeKind (int t)
{
    switch (t)
    {
        case TBody: case TResonator: case TGranular: case TDelay: case TScatter: case TReverb: case TShimmer: case TReverse: return KSend;
        default: return KInsert;
    }
}

// ---------------------------------------------------------------------------
const SourceProfile& sourceProfile (int i)
{
    //                       nom              lo    hi     fmin  fmax  sym  minSeg maxSeg refrac xfade fund  body
    static const SourceProfile p[kNumSources] = {
        { "Batterie",          40,  12000,  0,    0,    0,  30,   600,   35,   3,  60,   140 },
        { "Percussions",       80,  10000,  0,    0,    0,  30,   500,   30,   3,  200,  300 },
        { "Guitare",           80,  6000,   80,   1000, 2,  80,   1200,  40,   10, 110,  250 },
        { "Saxophone",         100, 5000,   100,  1000, 1,  80,   1500,  50,   20, 220,  400 },
        { "Basse",             40,  2500,   38,   400,  1,  80,   1200,  50,   15, 55,   120 },
        { "Contrebasse",       40,  2500,   38,   400,  1,  80,   1500,  60,   20, 55,   120 },
        { "Piano",             50,  6000,   50,   2000, 2,  80,   1500,  40,   15, 130,  300 },
        { "Voix",              100, 4000,   80,   1000, 1,  100,  1500,  70,   25, 200,  400 },
        { "Flute",             250, 6000,   240,  2500, 1,  80,   1500,  60,   25, 520,  800 },
        { "Clarinette",        140, 5000,   140,  1600, 1,  80,   1500,  60,   20, 290,  500 },
        { "Trompette",         160, 6000,   160,  1200, 1,  80,   1500,  50,   20, 350,  600 },
        { "Violon / alto",     190, 8000,   130,  3000, 1,  80,   1500,  60,   20, 440,  700 },
        { "Violoncelle",       60,  5000,   60,   1000, 1,  80,   1500,  60,   20, 130,  300 },
        { "Kalimba",           200, 8000,   200,  2000, 1,  60,   1200,  50,   8,  520,  800 },
        { "Synthe / electro",  40,  12000,  40,   2000, 2,  60,   1200,  40,   10, 110,  300 },
        { "Objets / bruitages",60,  12000,  0,    0,    0,  60,   900,   40,   8,  200,  400 },
        { "Vents (generique)", 100, 6000,   80,   2500, 1,  80,   1500,  60,   20, 300,  500 },
        { "Generique",         50,  10000,  50,   1500, 2,  70,   1200,  45,   12, 150,  300 } };
    return p[juce::jlimit (0, kNumSources - 1, i)];
}
juce::StringArray sourceNames()
{
    juce::StringArray s;
    for (int i = 0; i < kNumSources; ++i) s.add (sourceProfile (i).name);
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
const juce::StringArray& stutterDivs() { static const juce::StringArray s { "1/64", "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2" }; return s; }
double stutterDivBeats (int i) { static const double v[] { 1.0 / 16.0, 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 2.0 }; return v[juce::jlimit (0, 8, i)]; }
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
const juce::StringArray& distModes() { static const juce::StringArray s { "Tape", "Tube", "Fuzz", "Fold", "Rectifier" }; return s; }
const juce::StringArray& noiseModes() { static const juce::StringArray s { "Timbre", "Metal 808", "Crackle", "Souffle" }; return s; }
const juce::StringArray& bodyMaterials() { static const juce::StringArray s { "Membrane", "Bois", "Metal", "Cloche", "Verre" }; return s; }
const juce::StringArray& modModes() { static const juce::StringArray s { "Chorus", "Flanger", "Phaser" }; return s; }
const juce::StringArray& verbModes() { static const juce::StringArray s { "Room", "Plate", "Hall", "Gated 80s", "Non-lin" }; return s; }
const juce::StringArray& bandNames() { static const juce::StringArray s { "Plein", "Grave", "Aigu" }; return s; }
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
juce::StringArray resRoots() { auto s = noteNames(); s.add ("Auto"); return s; }

const juce::StringArray& clockNames()
{
    static const juce::StringArray s { "Off", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "Libre", "Chaque segment", "Chaque phrase" };
    return s;
}
double clockBeats (int i) { static const double v[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 0.0, 0.0, 0.0 }; return v[juce::jlimit (0, kNumClocks - 1, i)]; }
const juce::StringArray& evolveBarNames() { static const juce::StringArray s { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }; return s; }
int evolveBars (int i) { static const int v[] { 1, 2, 4, 8, 16 }; return v[juce::jlimit (0, 4, i)]; }

const juce::StringArray& modSourceNames()
{
    static const juce::StringArray s { "-", "S&H", "Smooth", "Turing", "Chaos", "LFO", "Enveloppe", "Horloge", "Segment", "Joueur",
                                       "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    return s;
}
juce::StringArray modDestNames()
{
    juce::StringArray s { "-" };
    for (int sl = 0; sl < kNumSlots; ++sl)
        for (int p = 0; p < kNumSP; ++p)
            s.add ("S" + juce::String (sl + 1) + (p < 6 ? " P" + juce::String (p + 1) : (p == kChance ? juce::String (" Chance") : juce::String (" Mix"))));
    for (int i = 0; i < kNumIm; ++i) s.add ("Maria " + imLabel (i));
    s.add ("Mix global"); s.add ("Chaos"); s.add ("Largeur");
    return s;
}

// ---------------------------------------------------------------------------
// Improvisatrice
const juce::StringArray& tempoModes() { static const juce::StringArray s { "Libre", "Synchro DAW", "Tempo manuel", "Pulsation musiciens" }; return s; }
const juce::StringArray& gridNames() { static const juce::StringArray s { "1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4", "1/2" }; return s; }
double gridBeats (int i) { static const double v[] { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.5, 1.0, 2.0 }; return v[juce::jlimit (0, 6, i)]; }
const juce::StringArray& alignNames() { static const juce::StringArray s { "Temps", "1/2 mesure", "Mesure" }; return s; }
double alignBeats (int i) { static const double v[] { 1.0, 2.0, 4.0 }; return v[juce::jlimit (0, 2, i)]; }
const juce::StringArray& imModes() { static const juce::StringArray s { "Dialogue", "Duo", "Echo", "Contrepoint", "Contraste", "Machine" }; return s; }
juce::String imId (int i)
{
    static const char* n[kNumIm] { "act", "react", "phrase", "gap", "cont", "ctx", "mem", "follow", "transp", "rev", "polite", "level" };
    return juce::String ("im_") + n[juce::jlimit (0, kNumIm - 1, i)];
}
juce::String imJid (int i) { return imId (i) + "_j"; }
juce::String imLabel (int i)
{
    static const char* n[kNumIm] { "Activite", "Reactivite", "Phrase", "Silences", "Continuite", "Contexte", "Memoire", "Suivi",
                                   "Transpo", "Reverse", "Politesse", "Niveau" };
    return n[juce::jlimit (0, kNumIm - 1, i)];
}
float imDefault (int i)
{
    static const float d[kNumIm] { 0.6f, 0.35f, 0.45f, 0.35f, 0.6f, 0.15f, 0.7f, 0.5f, 0.5f, 0.0f, 0.6f, 0.75f };
    return d[juce::jlimit (0, kNumIm - 1, i)];
}
juce::String imText (int i, float v)
{
    auto pctS = [] (float x) { return juce::String ((int) std::round (x * 100.0f)) + "%"; };
    switch (i)
    {
        case IReact:  return juce::String (imReactSec (v), 2) + " s";
        case IPhrase: return juce::String (imPhraseSec (v), 1) + " s";
        case IGap:    return juce::String (imGapSec (v), 1) + " s";
        case ICont:   return v < 0.3f ? "recombine " + pctS (1.0f - v) : (v > 0.7f ? "cite " + pctS (v) : pctS (v));
        case ICtx:    return juce::String (imCtxMin (v)) + " evt";
        case IMem:    return v < 0.15f ? "recent" : (v > 0.85f ? "tout" : pctS (v));
        case IFollow: { const float b = bip (v); return std::abs (b) < 0.1f ? "neutre" : (b > 0 ? "suit " : "contraste ") + pctS (std::abs (b)); }
        case ITransp: { const float s = imTranspSemis (v); return (s > 0 ? "+" : "") + juce::String ((int) s) + " st"; }
        case ILevel:  return juce::String (juce::Decibels::gainToDecibels (v * v * 2.0f + 1.0e-6f), 1) + " dB";
        default:      return pctS (v);
    }
}

juce::String sid (int slot, const char* what) { return "s" + juce::String (slot + 1) + "_" + what; }
juce::String pid (int slot, int p)
{
    if (p < 6) return "s" + juce::String (slot + 1) + "_p" + juce::String (p + 1);
    return sid (slot, p == kChance ? "chance" : "mix");
}
juce::String jid (int slot, int p)
{
    if (p < 6) return "s" + juce::String (slot + 1) + "_j" + juce::String (p + 1);
    return sid (slot, p == kChance ? "jchance" : "jmix");
}

// ---------------------------------------------------------------------------
juce::String paramLabel (int type, int p)
{
    if (p == kMix) return "Mix";
    if (p == kChance) return "Chance/seg.";
    static const char* L[kNumTypes][6] = {
        { "P1", "P2", "P3", "P4", "P5", "P6" },
        { "Temps", "Feedback", "Tone", "Ping-pong", "Wobble", "Sync" },
        { "Portee", "Densite", "Feedback", "Grille", "Pan", "Cadence" },
        { "Division", "Chance", "Duree", "Pitch", "Reverse", "Thru" },
        { "Taille", "Densite", "Spray", "Pitch", "Pitch rnd", "Sur segm." },
        { "Mode", "Decay", "Taille", "Damp", "Predelay", "Gate/Mod" },
        { "Decay", "Taille", "Shimmer", "Intervalle", "Damp", "Mod" },
        { "Longueur", "Decay", "Taille", "Brut", "Swell", "Damp" },
        { "Mode", "Vitesse", "Profond.", "Feedback", "Couleur", "Stereo" },
        { "Type", "Cutoff", "Reso", "Env segm.", "Rnd segm.", "Env decay" },
        { "Shift", "Fin", "Grain", "Feedback", "Rnd", "Detune" },
        { "Drive", "Bits", "Rate", "Wow", "Bruit", "Tone" },
        { "Mode", "Drive", "Tone", "Bias", "Focus", "Rnd segm." },
        { "Mode", "Freq", "Rnd freq", "Stereo", "Feedback", "LFO" },
        { "Division", "Densite", "Mutation", "Forme", "Swing", "Pan rnd" },
        { "Tonique", "Gamme", "Decay", "Brillance", "Etendue", "Rnd notes" },
        { "Matiere", "Accord", "Decay", "Brillance", "Inharm.", "Rnd segm." },
        { "Timbre de", "Sens", "Reponse", "Formants", "Precision", "Blanchir" } };
    if (type < 0 || type >= kNumTypes) return "P" + juce::String (p + 1);
    return L[type][juce::jlimit (0, 5, p)];
}

int auxParamFor (int type, int p)
{
    if (type == TDelay && p == 0) return 5;
    if (type == TRing && p == 1) return 0;
    if (type == TReverb && p == 5) return 0;
    return -1;
}

static juce::String pct (float v) { return juce::String ((int) std::round (v * 100.0f)) + "%"; }
static juce::String ms (float m) { return m < 100.0f ? juce::String (m, 1) + " ms" : (m < 1000.0f ? juce::String ((int) m) + " ms" : juce::String (m * 0.001f, 2) + " s"); }
static juce::String hz (float f) { return f < 10.0f ? juce::String (f, 2) + " Hz" : (f < 1000.0f ? juce::String ((int) f) + " Hz" : juce::String (f * 0.001f, 1) + " kHz"); }
static juce::String st (float s) { return (s > 0 ? "+" : "") + juce::String ((int) s) + " st"; }
static juce::String sgn (float v) { return (v >= 0 ? "+" : "") + juce::String ((int) std::round (v * 100.0f)) + "%"; }

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

// Parametres partages DSP / affichage
float subTuneSemis (float v) { return std::round ((v - 0.5f) * 48.0f); }      // -24..+24 autour de la fondamentale
float bodyTuneSemis (float v) { return (v - 0.5f) * 48.0f; }

juce::String paramText (int type, int p, float v, float aux, float fund)
{
    if (p == kMix || p == kChance) return pct (v);
    switch (type)
    {
        case TTransient:
            switch (p) { case 0: case 1: return sgn (bip (v)); case 2: return v < 0.5f ? "lente" : (v < 0.8f ? "moyenne" : "rapide");
                         case 3: return bandNames()[pick (v, 3)]; case 5: return v < 0.02f ? "off" : pct (v); default: return pct (v); }
        case TSmash:
            switch (p) { case 0: return juce::String (-v * 40.0f, 1) + " dB"; case 1: return ms (expMap (v, 0.1f, 30.0f)); case 2: return ms (expMap (v, 20.0f, 800.0f));
                         case 3: return juce::String (expMap (v, 1.5f, 30.0f), 1) + ":1"; case 4: return pct (v); case 5: return hz (expMap (v, 20.0f, 400.0f)); default: return pct (v); }
        case TDist:
            switch (p) { case 0: return distModes()[pick (v, 5)]; case 1: return "+" + juce::String ((int) (v * 48)) + " dB"; case 2: return sgn (bip (v));
                         case 3: return pct (v); case 4: return v < 0.4f ? "grave " + pct ((0.5f - v) * 2) : (v > 0.6f ? "aigu " + pct ((v - 0.5f) * 2) : "plein"); default: return pct (v); }
        case TDegrade:
            switch (p) { case 0: return "+" + juce::String ((int) (v * 30)) + " dB"; case 1: return juce::String ((int) std::round (expMap (v, 16, 2))) + " bits";
                         case 2: return hz (expMap (v, 48000, 300)); case 5: return hz (expMap (v, 18000, 800)); default: return pct (v); }
        case TSub:
            switch (p) { case 0: return hz (fund * std::pow (2.0f, subTuneSemis (v) / 12.0f)) + " (" + st (subTuneSemis (v)) + ")";
                         case 1: return ms (expMap (v, 30.0f, 2000.0f)); case 2: return "x" + juce::String (1.0f + v * 4.0f, 1); case 3: return ms (expMap (v, 5.0f, 300.0f)); default: return pct (v); }
        case TNoise:
            switch (p) { case 0: return noiseModes()[pick (v, 4)]; case 1: return hz (expMap (v, 300.0f, 14000.0f)); case 2: return ms (expMap (v, 10.0f, 1500.0f)); default: return pct (v); }
        case TCross:
            switch (p) { case 0: return juce::StringArray { "Sidechain", "Piste", "Bruit" }[pick (v, 3)];
                         case 1: return v < 0.5f ? "forme de Maria" : "timbre de Maria";
                         case 2: return ms (expMap (v, 2.0f, 120.0f)); case 3: return st (std::round (bip (v) * 12.0f));
                         case 4: return juce::String ((int) std::round (8 + v * 24)) + " bandes"; default: return pct (v); }
        case TBody:
            switch (p) { case 0: return bodyMaterials()[pick (v, 5)]; case 1: return hz (fund * std::pow (2.0f, bodyTuneSemis (v) / 12.0f));
                         case 2: return ms (expMap (v, 40.0f, 4000.0f)); default: return pct (v); }
        case TResonator:
            switch (p) { case 0: return resRoots()[pick (v, 13)]; case 1: return resScales()[pick (v, 12)]; case 4: return juce::String (1 + pick (v, 4)) + " oct"; default: return pct (v); }
        case TFilter:
            switch (p) { case 0: return filterTypes()[pick (v, 6)]; case 1: return hz (expMap (v, 20, 18000)); case 3: return sgn (bip (v)); case 5: return ms (expMap (v, 10.0f, 1000.0f)); default: return pct (v); }
        case TPitch:
            switch (p) { case 0: return st (semis48 (v)); case 1: return juce::String ((int) std::round (bip (v) * 50)) + " ct"; case 2: return ms (expMap (v, 15, 150));
                         case 5: return juce::String ((int) (v * 30)) + " ct"; default: return pct (v); }
        case TRing:
            switch (p) { case 0: return ringModes()[pick (v, 3)]; case 1: return ringFreqText (v, aux); default: return pct (v); }
        case TMod:
            switch (p) { case 0: return modModes()[pick (v, 3)]; case 1: return hz (expMap (v, 0.02f, 8.0f)); case 5: return juce::String ((int) (v * 180)) + " deg"; default: return pct (v); }
        case TGate:
            switch (p) { case 0: return gateDivs()[pick (v, 6)]; case 3: return v < 0.5f ? "doux " + pct (1 - v * 2) : "sec " + pct (v * 2 - 1); default: return pct (v); }
        case TStutter:
            switch (p) { case 0: return stutterDivs()[pick (v, 9)]; case 2: return segNames()[pick (v, 5)]; case 3: return juce::String (bip (v) * 3.0f, 1) + " st/rep"; default: return pct (v); }
        case TGranular:
            switch (p) { case 0: return ms (expMap (v, 10, 500)); case 1: return juce::String (expMap (v, 1, 80), 1) + " /s"; case 2: return ms (v * v * 3000.0f);
                         case 3: return st (semis48 (v)); case 5: return v < 0.02f ? "nuage" : pct (v); default: return pct (v); }
        case TDelay:
            switch (p) { case 0: return aux >= 0.5f ? delayDivs()[pick (v, 12)] : ms (expMap (v, 10, 2000)); case 1: return pct (v * 1.1f);
                         case 2: return "Tone " + juce::String ((int) std::round (bip (v) * 100)); case 5: return v >= 0.5f ? "Tempo" : "Libre"; default: return pct (v); }
        case TScatter:
            switch (p) { case 0: return ms (expMap (v, 50, 2000)); case 1: return juce::String (1 + (int) std::round (v * 7)) + " taps"; case 5: return scatterRates()[pick (v, 7)]; default: return pct (v); }
        case TReverb:
            switch (p) { case 0: return verbModes()[pick (v, 5)]; case 1: return juce::String (expMap (v, 0.15f, 12.0f), 2) + " s"; case 4: return ms (v * 200.0f);
                         case 5: return pick (aux, 5) >= 3 ? ms (expMap (v, 60.0f, 700.0f)) : pct (v); default: return pct (v); }
        case TShimmer:
            switch (p) { case 0: return juce::String (expMap (v, 0.5f, 30.0f), 1) + " s"; case 3: return shimmerIntervals()[pick (v, 8)]; default: return pct (v); }
        case TReverse:
            switch (p) { case 0: return revLens()[pick (v, 6)]; case 1: return juce::String (expMap (v, 0.3f, 12.0f), 1) + " s"; default: return pct (v); }
        default: return pct (v);
    }
}

juce::String typeHelp (int type)
{
    switch (type)
    {
        case TDelay:     return "Delay bande ; Chance/seg. < 100 % = seuls certains segments de Maria partent en echo.";
        case TScatter:   return "Multi-taps aleatoires redistribues a chaque cadence.";
        case TStutter:   return "Beat-repeat au tempo : rafales, begaiements.";
        case TGranular:  return "Nuage granulaire ; Sur segm. = les grains jaillissent a chaque evenement joue par Maria.";
        case TReverb:    return "Room, plate, hall, gated 80s ou non-lineaire.";
        case TShimmer:   return "Reverbe dont la queue est transposee et reinjectee (octave, quinte...).";
        case TReverse:   return "Reverbe inversee : chaque tranche ressort a l'envers.";
        case TMod:       return "Chorus, flanger ou phaser.";
        case TFilter:    return "Filtre multimode ouvert a chaque evenement de Maria, cutoff aleatoire par evenement.";
        case TPitch:     return "Transposition par grains, intervalles aleatoires a chaque pas d'horloge.";
        case TDegrade:   return "Lo-fi : bits, sous-echantillonnage, pleurage, souffle et craquements.";
        case TDist:      return "Distorsion anti-repliement sans latence : tape, tube, fuzz, wavefolder, redresseur.";
        case TRing:      return "Modulation en anneau, decalage de frequence ou AM.";
        case TGate:      return "Hacheur rythmique de 16 pas qui mute a chaque mesure.";
        case TResonator: return "Resonateurs accordes sur une gamme (Auto = fondamentale de la source).";
        case TBody:      return "Corps modal : membrane, bois, metal, cloche ou verre excites par la voix de Maria.";
        case TCross:     return "Cross-synthese (vocodeur) : la phrase de Maria joue avec le timbre d'un autre musicien (sidechain), de la piste ou d'un bruit - ou l'inverse.";
        default:         return "Slot vide : choisis un moteur dans le menu.";
    }
}
} // namespace ma
