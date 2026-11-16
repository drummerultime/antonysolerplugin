#include "GrParams.h"
#include "Core.h"

namespace gr
{
const juce::StringArray& typeNames()
{
    static const juce::StringArray s { "- vide -", "Transient", "Smash (comp)", "Distorsion", "Degrade", "Sub 808", "Couche bruit",
                                       "Corps modal", "Resonator", "Filtre", "Pitch", "Ring / Shift", "Modulation", "Gate",
                                       "Stutter", "Granular", "Delay", "Scatter", "Reverb", "Reverse" };
    return s;
}
const juce::StringArray& typeShort()
{
    static const juce::StringArray s { "-", "TRN", "SMA", "DST", "DEG", "SUB", "NOI", "BOD", "RES", "FLT", "PIT", "RNG", "MOD", "GAT",
                                       "STU", "GRN", "DLY", "SCT", "VRB", "REV" };
    return s;
}

Kind typeKind (int t)
{
    switch (t)
    {
        case TBody: case TResonator: case TGranular: case TDelay: case TScatter: case TReverb: case TReverse: return KSend;
        case TSub: case TNoise: return KTrigger;
        default: return KInsert;
    }
}

// ---------------------------------------------------------------------------
const SourceProfile& sourceProfile (int i)
{
    static const SourceProfile p[kNumSources] = {
        { "Grosse caisse",  30,   400,   55,   90,  60,  300, 0 },
        { "Caisse claire",  120,  9000,  200,  260, 40,  250, 1 },
        { "Tom",            60,   3000,  110,  160, 50,  400, 2 },
        { "Overhead",       200,  16000, 180,  400, 30,  600, 5 },
        { "Drumkit (bus)",  40,   12000, 60,   140, 35,  300, 5 },
        { "Cymbales",       1500, 18000, 450,  700, 60,  1500, 3 },
        { "Charley",        2500, 18000, 500,  900, 25,  150, 3 },
        { "Percussion",     100,  10000, 250,  350, 30,  250, 4 },
        { "Conga",          80,   6000,  200,  300, 35,  300, 4 },
        { "Bongo",          150,  8000,  380,  450, 25,  200, 4 },
        { "Djembe",         60,   8000,  85,   350, 35,  300, 4 },
        { "Derbouka",       80,   12000, 120,  350, 25,  200, 4 },
        { "Tablas",         80,   8000,  300,  300, 30,  400, 4 },
        { "Tambourin",      1500, 16000, 600,  2000, 25, 200, 3 },
        { "Triangle",       2000, 18000, 1200, 3000, 50, 2500, 3 },
        { "Cajon",          50,   8000,  80,   160, 35,  250, 0 },
        { "Shaker / petites", 2000, 16000, 800, 4000, 20, 100, 3 },
        { "Gong / bol",     50,   12000, 110,  300, 200, 4000, 6 },
        { "Room (ambiance)", 60,  12000, 80,   250, 40,  500, 5 } };
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
    static const juce::StringArray s { "Off", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "Libre", "Chaque coup", "1 coup / 2", "1 coup / 4" };
    return s;
}
double clockBeats (int i) { static const double v[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 0.0, 0.0, 0.0, 0.0 }; return v[juce::jlimit (0, kNumClocks - 1, i)]; }
const juce::StringArray& evolveBarNames() { static const juce::StringArray s { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }; return s; }
int evolveBars (int i) { static const int v[] { 1, 2, 4, 8, 16 }; return v[juce::jlimit (0, 4, i)]; }

const juce::StringArray& modSourceNames()
{
    static const juce::StringArray s { "-", "S&H", "Smooth", "Turing", "Chaos", "LFO", "Enveloppe", "Horloge", "Coup", "Velocite",
                                       "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
    return s;
}
juce::StringArray modDestNames()
{
    juce::StringArray s { "-" };
    for (int sl = 0; sl < kNumSlots; ++sl)
        for (int p = 0; p < kNumSP; ++p)
            s.add ("S" + juce::String (sl + 1) + (p < 6 ? " P" + juce::String (p + 1) : (p == kChance ? juce::String (" Chance") : juce::String (" Mix"))));
    s.add ("Mix global"); s.add ("Chaos"); s.add ("Largeur");
    return s;
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
    if (p == kChance) return "Chance/coup";
    static const char* L[kNumTypes][6] = {
        { "P1", "P2", "P3", "P4", "P5", "P6" },
        { "Attaque", "Sustain", "Vitesse", "Bande", "Rnd coup", "Clip" },
        { "Seuil", "Attaque", "Release", "Ratio", "Drive", "Tone SC" },
        { "Mode", "Drive", "Tone", "Bias", "Focus", "Rnd coup" },
        { "Drive", "Bits", "Rate", "Wow", "Bruit", "Tone" },
        { "Accord", "Decay", "Pitch env", "Vit. env", "Drive", "Rnd coup" },
        { "Mode", "Couleur", "Decay", "Largeur", "Suivi", "Rnd coup" },
        { "Matiere", "Accord", "Decay", "Brillance", "Inharm.", "Rnd coup" },
        { "Tonique", "Gamme", "Decay", "Brillance", "Etendue", "Rnd notes" },
        { "Type", "Cutoff", "Reso", "Env coup", "Rnd coup", "Env decay" },
        { "Shift", "Fin", "Grain", "Feedback", "Rnd", "Detune" },
        { "Mode", "Freq", "Rnd freq", "Stereo", "Feedback", "LFO" },
        { "Mode", "Vitesse", "Profond.", "Feedback", "Couleur", "Stereo" },
        { "Division", "Densite", "Mutation", "Forme", "Swing", "Pan rnd" },
        { "Division", "Chance", "Duree", "Pitch", "Reverse", "Thru" },
        { "Taille", "Densite", "Spray", "Pitch", "Pitch rnd", "Sur coups" },
        { "Temps", "Feedback", "Tone", "Ping-pong", "Wobble", "Sync" },
        { "Portee", "Densite", "Feedback", "Grille", "Pan", "Cadence" },
        { "Mode", "Decay", "Taille", "Damp", "Predelay", "Gate/Mod" },
        { "Longueur", "Decay", "Taille", "Brut", "Swell", "Damp" } };
    return L[juce::jlimit (0, kNumTypes - 1, type)][juce::jlimit (0, 5, p)];
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
        case TReverse:
            switch (p) { case 0: return revLens()[pick (v, 6)]; case 1: return juce::String (expMap (v, 0.3f, 12.0f), 1) + " s"; default: return pct (v); }
        default: return pct (v);
    }
}

juce::String typeHelp (int type)
{
    switch (type)
    {
        case TTransient:  return "Transient shaper : attaque et sustain (+/-), par bande, avec variation aleatoire a chaque coup.";
        case TSmash:      return "Compresseur d'ecrasement (facon parallele NY) avec drive ; release cale sur la source.";
        case TDist:       return "Distorsion anti-repliement sans latence : tape, tube, fuzz, wavefolder, redresseur ; Focus = grave ou aigu seulement.";
        case TDegrade:    return "Lo-fi : bits, sous-echantillonnage, pleurage, souffle et craquements.";
        case TSub:        return "Sub 808 declenche par les coups, accorde sur la source, chute de pitch (ajoute au son).";
        case TNoise:      return "Couche de bruit declenchee : timbre de caisse, metal 808, crackle, souffle (ajoutee au son).";
        case TBody:       return "Corps modal : resonances de membrane, bois, metal, cloche ou verre excitees par les coups.";
        case TResonator:  return "Resonateurs accordes sur une gamme (Auto = fondamentale de la source).";
        case TFilter:     return "Filtre multimode ouvert par chaque coup (Env coup) avec cutoff aleatoire par coup.";
        case TPitch:      return "Transposition par grains, feedback en cascade, intervalles aleatoires a chaque pas d'horloge.";
        case TRing:       return "Modulation en anneau, decalage de frequence ou AM : metal, cloches, robots.";
        case TMod:        return "Chorus, flanger (jet) ou phaser.";
        case TGate:       return "Hacheur rythmique de 16 pas qui mute a chaque mesure.";
        case TStutter:    return "Beat-repeat au tempo : rafales, roulements, glitch.";
        case TGranular:   return "Granulaire sur 4 s ; Sur coups = les grains jaillissent a chaque frappe.";
        case TDelay:      return "Delay bande ; avec Chance/coup < 100 % = envois dub aleatoires.";
        case TScatter:    return "Multi-taps aleatoires redistribues a chaque cadence.";
        case TReverb:     return "Room, plate, hall, gated 80s (porte ouverte a chaque coup) ou non-lineaire.";
        case TReverse:    return "Reverbe inversee : chaque tranche ressort a l'envers.";
        default:          return "Slot vide : choisis un moteur dans le menu.";
    }
}
} // namespace gr
