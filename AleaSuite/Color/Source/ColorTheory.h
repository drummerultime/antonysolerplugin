#pragma once

// COLOR - moteur harmonique.
// Synthese des lectures demandees (Russell, Persichetti, Dobbins, LaVerne, Boss, Kadmon) traduite en regles :
//  - gammes et modes (catalogue Kadmon/Persichetti, ordre du plus sombre au plus clair),
//  - qualites d'accords (tertiens, quartaux, sus, set-classes post-tonales),
//  - fonctions (tonique / sous-dominante / dominante), cycles de quintes, tierces, secondes (Persichetti),
//  - gravite tonale : tonique lydienne et ordre lydien chromatique (Russell),
//  - tension d'un accord d'apres ses intervalles (Persichetti),
//  - generateurs de progressions par style, reharmonisation (LaVerne, Dobbins),
//  - voicings et conduite des voix (Dobbins : drop 2, shells, rootless, upper structures ; Persichetti : quartes,
//    clusters, espacement facon serie harmonique).
// Les fonctions utilisees par le thread audio n'allouent pas de memoire.

#include "../../Shared/Core.h"
#include <vector>
#include <array>

namespace color::theory
{
// ---------------------------------------------------------------------------
// Gammes / modes
struct ScaleDef
{
    const char* name;
    uint16_t mask;      // bits des degres (0 = tonique)
    int coarse;         // equivalent dans la liste partagee AleaLink (alea::scaleNames) : 1 chrom, 2 maj, 3 min, 4 penta maj, 5 penta min, 6 tons
    int family;         // 0 diatonique, 1 mineur melodique, 2 mineur/majeur harmonique, 3 exotique, 4 symetrique, 5 pentatonique, 6 bebop
};
int numScales();
const ScaleDef& scale (int i);
juce::StringArray scaleNameList();
int scaleSize (int i);
int scaleDegreePc (int scaleIdx, int degree);      // degre (peut depasser) -> demi-tons depuis la tonique (avec octaves)
bool inScale (int scaleIdx, int relPc);
int nearestInScale (int scaleIdx, int relSemis);   // quantifie vers la note la plus proche de la gamme
int modeForCoarse (int coarse, int currentMode);   // garde le mode courant s'il est compatible, sinon le mode canonique
float scaleBrightness (int scaleIdx);              // Persichetti : locrien (sombre) ... lydien (clair)

// ---------------------------------------------------------------------------
// Qualites d'accords
enum Family { FMaj = 0, FMin, FDom, FHalfDim, FDim, FAug, FSus, FQuartal, FOther };
struct QualDef
{
    const char* sym;     // suffixe affiche (Dm7 -> "m7")
    int8_t iv[7];        // intervalles depuis la fondamentale (jusqu'a 2 octaves)
    int8_t n;
    int8_t family;
    int8_t triadSize;    // nombre de notes "fondamentales" (avant extensions)
};
int numQualities();
const QualDef& quality (int q);
juce::StringArray qualityNameList();
int findQuality (const char* sym);

// qualites utilisees par les generateurs (indices stables)
enum Q
{
    QMaj = 0, QMin, QDim, QAug, QSus2, QSus4, QFive, QSix, QMin6, QSixNine, QMaj7, QDom7, QMin7, QHalfDim, QDim7,
    QMinMaj7, QMaj7s5, QMaj7s11, QSus7, QAdd9, QMinAdd9, QMaj9, QDom9, QMin9, QMin11, QDom13, QDom7b9, QDom7s9, QAlt,
    QDom7s11, QDom7b13, QSus9, QQuartal3, QQuartal4, QSoWhat, Q419, QViennese, QCluster, QMystic, QHexatonic, QDom7b5,
    kNumQ
};

struct Chord
{
    int8_t root = 0;       // classe de hauteur RELATIVE a la tonique de la tonalite (0..11)
    int8_t qual = QMaj;
    int8_t inversion = 0;  // 0 = etat fondamental, 1 = 1er renversement...
    int8_t bass = -1;      // basse imposee (relative), -1 = selon le renversement
    int8_t beats = 4;      // duree en temps (noires)
    bool operator== (const Chord& o) const noexcept
    { return root == o.root && qual == o.qual && inversion == o.inversion && bass == o.bass && beats == o.beats; }
};

int chordTones (const Chord& c, int* out12);              // classes de hauteur relatives (sans doublons), renvoie le nombre
uint16_t chordMask (const Chord& c);                      // masque 12 bits relatif a la tonique de la tonalite
int chordBassPc (const Chord& c);
juce::String chordName (const Chord& c, int keyRoot);      // "F#m7/A"
juce::String romanNumeral (const Chord& c, int scaleIdx);  // "ii7", "bVII", "V7/ii"...
int chordFunction (const Chord& c, int scaleIdx);          // 0 tonique, 1 sous-dominante, 2 dominante, 3 couleur/chromatique

// ---------------------------------------------------------------------------
// Analyse : tension (Persichetti) et gravite (Russell)
float intervalTension (const int* midiNotes, int n);       // 0 (consonant) .. 1 (tres dissonant)
float chordTension (const Chord& c);
int lydianTonic (const Chord& c);                          // tonique lydienne (relative) de la gamme parente
int lcRank (int pcFromLydianTonic);                        // 0 (ingoing) .. 11 (outgoing)
float chordGravity (const Chord& c, int keyLydianTonic);   // 0 dedans .. 1 dehors (distance au centre tonal)
int parentScale (const Chord& c, float outgoing);          // index de gamme (catalogue) pour l'arpege / la melodie
int parentScaleRoot (const Chord& c, float outgoing);      // fondamentale relative de cette gamme
juce::String parentScaleName (const Chord& c, float outgoing, int keyRoot);

// identification d'un accord a partir de notes (MIDI, apprentissage, mode Harmonize)
bool identifyChord (uint16_t pcMask, int bassPc, Chord& out);

// accord diatonique sur un degre du mode (empilement de tierces, quartes ou secondes)
enum Stack { StackThirds = 0, StackFourths, StackSeconds };
Chord diatonic (int scaleIdx, int degree, int size /*3..6*/, int stack = StackThirds);

// ---------------------------------------------------------------------------
// Generateurs de progressions
enum Style { SPop = 0, SFunctional, SJazz, SModal, SCinematic, STechno, SGospel, SBlues, SImpression, SAtonal, SLydian, SRandom,
             SNeoSoul, SBossa, kNumStyles };   // nouveaux styles ajoutes a la fin (compatibilite des sessions)
juce::StringArray styleNames();
juce::StringArray cycleNames();          // Quintes / Tierces / Secondes / Mixte (Persichetti)

struct GenOptions
{
    int style = SPop;
    int scale = 0;          // mode courant
    int numChords = 4;      // 1..16
    float complexity = 0.4f;// 0 triades .. 1 9es/11es/13es
    float tension = 0.3f;   // alterations, emprunts, outgoing
    float chroma = 0.2f;    // substitutions chromatiques
    int cycle = 0;          // Persichetti
    int barsPerChord = 1;   // 0 = 2 temps, 1 = 1 mesure, 2 = 2 mesures
    uint32_t seed = 1;
};
std::vector<Chord> generate (const GenOptions& o);

// Harmonisation d'une ligne melodique entiere (Dobbins : notes de l'accord sur les temps forts, extensions
// disponibles, dominantes secondaires ; Persichetti : enchainements par quintes / secondes).
// weights[w][pc] = poids de chaque classe de hauteur (relative a la tonique) dans la fenetre w,
// strong[w][pc] = poids sur les temps forts. Recherche du meilleur chemin (programmation dynamique).
std::vector<Chord> harmonizeLine (const std::vector<std::array<float, 12>>& weights, const std::vector<std::array<float, 12>>& strong,
                                  int scaleIdx, float complexity, float tension, int beatsPerChord);

// Reharmonisation (LaVerne / Dobbins) appliquee a une progression (thread message)
enum Reharm { RTritone = 0, RSecondary, RTwoFive, RApproach, RQuality, RRelative, RSus, RDiminished, RBackdoor, RMediant, RConstant, RPedal, kNumReharm };
juce::StringArray reharmNames();
void reharmonize (std::vector<Chord>& prog, int scaleIdx, int op, int index, uint32_t seed);   // index -1 = au hasard
void reharmonizeAmount (std::vector<Chord>& prog, int scaleIdx, float amount, uint32_t seed);
std::vector<Chord> substitutesFor (const Chord& c, const Chord* next, int scaleIdx);     // propositions (menu)
std::vector<std::pair<Chord, float>> suggestNext (const Chord& c, int scaleIdx, int style);// suite probable (onglet THEORIE)

// Substitution "en direct" (thread audio, deterministe : hash de l'occurrence)
Chord liveSubstitute (const Chord& c, const Chord& next, int scaleIdx, uint32_t hash, float tension);
Chord approachChord (const Chord& target, uint32_t hash);

// ---------------------------------------------------------------------------
// Voicings et conduite des voix
enum Voicing { VClose = 0, VDrop2, VDrop24, VSpread, VShell, VRootlessA, VRootlessB, VQuartal, VUpper, VCluster, VFifths, VAuto, VGuitar, kNumVoicings };
juce::StringArray voicingNames();

struct VoiceOptions
{
    int voicing = VDrop2;
    int voices = 4;         // 3..7 (hors basse)
    float center = 64.0f;   // registre (MIDI)
    float spread = 0.5f;
    float smooth = 0.7f;    // poids de la conduite des voix
    float complexity = 0.4f;
    float tension = 0.3f;
    int bassMode = 1;       // 0 aucune, 1 fondamentale/renversement, 2 pedale (tonique)
    int bassOctave = 2;     // octave MIDI de la basse (C2 = 36)
    int scale = 0;
    int keyRoot = 0;        // 0..11 absolu
    uint32_t hash = 1;
};
// renvoie le nombre de notes (MIDI absolues, triees), basse comprise en notes[0] si bassMode
int voiceChord (const Chord& c, const VoiceOptions& o, const int* prev, int prevN, int* notes, int maxNotes);
// notes "disponibles" pour un arpege/une melodie sur cet accord (gamme parente), en MIDI dans [lo, hi]
int chordScaleNotes (const Chord& c, const VoiceOptions& o, float outgoing, int lo, int hi, int* out, int maxN);

// harmonisation d'une note de melodie (Dobbins : note de l'accord, extension, alteree, etrangere)
Chord harmonizeMelody (int melodyRelPc, const Chord& current, int scaleIdx, uint32_t hash);

// masque (12 bits, relatif a la tonique de la tonalite) de la gamme parente d'un accord (Russell)
uint16_t chordScaleMask (const Chord& c, int scaleIdx, float outgoing);

// Modulation (Persichetti / harmonie classique) : accords qui menent de la tonalite courante vers une tonalite
// situee a 'semis' demi-tons. method : 0 dominante, 1 ii-V, 2 accord pivot + V7, 3 directe (rien)
std::vector<Chord> modulationChords (int scaleIdx, int semis, int method, int beats);

} // namespace color::theory
