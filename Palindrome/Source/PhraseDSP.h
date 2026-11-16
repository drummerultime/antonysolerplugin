#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

// Traitement "hors temps réel" d'une phrase capturée :
// découpage en notes (onsets), détection de hauteur (YIN), correction / transposition dans une gamme,
// hasard dosé (ordre, notes inversées, muettes, répétées, octaves, mélodie), rendu normal ou reverse,
// filtres et gain de la répétition.
namespace pal
{
enum class Scale
{
    Off = 0, Major, NaturalMinor, HarmonicMinor, MelodicMinor,
    Dorian, Phrygian, Lydian, Mixolydian, Locrian,
    PentaMajor, PentaMinor, Blues, WholeTone, Chromatic,
    NumScales
};

juce::StringArray scaleNames();

struct Note
{
    int start = 0;            // position dans la phrase source (échantillons)
    int length = 0;           // durée (échantillons)
    float midi = -1.0f;       // hauteur détectée (-1 = non détectée / non tonale) ; accord : note la plus grave
    float confidence = 0.0f;
    bool poly = false;        // accord / son polyphonique
    bool legatoCut = false;   // coupe trouvée par changement harmonique (sans attaque)
    bool analysed = false;
};

enum PlayMode { PlayAuto = 0, PlayMono, PlayPoly };

// Analyse d'une phrase (mono + découpage + hauteurs), réutilisée d'une répétition à l'autre
struct PhraseAnalysis
{
    std::vector<float> mono;
    std::vector<Note> notes;

    bool matches (int gen, int len, double rate, float sens, float leg, int mode) const
    {
        return valid && gen == generation && len == length && std::abs (rate - sampleRate) < 0.5
            && std::abs (sens - sensitivity) < 1.0e-5f && std::abs (leg - legato) < 1.0e-5f && mode == playMode;
    }
    void setKey (int gen, int len, double rate, float sens, float leg, int mode)
    {
        valid = true; generation = gen; length = len; sampleRate = rate; sensitivity = sens; legato = leg; playMode = mode;
    }
    void invalidate() { valid = false; }

private:
    bool valid = false;
    int generation = 0, length = 0, playMode = 0;
    double sampleRate = 0.0;
    float sensitivity = 0.0f, legato = 0.0f;
};

struct RenderParams
{
    bool reverse = false;
    double sampleRate = 48000.0;
    juce::uint32 seed = 1;

    // découpage
    float sensitivity = 0.5f;
    float legato = 0.5f;           // détection des changements de note sans attaque (0 = off)
    int playMode = PlayAuto;       // notes seules / accords / auto
    float fadeMs = 6.0f;

    // hasard (0..1)
    float randomAmount = 0.0f;     // désordre des notes
    float noteReverseProb = 0.0f;  // notes inversées individuellement
    float skipProb = 0.0f;         // notes muettes
    float repeatProb = 0.0f;       // notes répétées (bégaiement)
    float octaveProb = 0.0f;       // sauts d'octave
    float melodicJitter = 0.0f;    // déplacement de ±1/±2 degrés dans la gamme

    // tonalité
    int keyRoot = 0;               // 0 = Do ... 11 = Si
    Scale scale = Scale::Off;
    float correction = 1.0f;       // 0..1 : recalage sur la gamme
    int transpose = 0;             // en degrés de la gamme (ou demi-tons si gamme Off)

    // couleur de la répétition
    float gain = 1.0f;
    float attackMs = 0.0f;         // montée de volume (violoning)
    bool attackPerNote = true;     // true : chaque note, false : toute la répétition
    int attackDirection = 0;       // 0 : normal et reverse, 1 : lectures normales, 2 : lectures reverse
    int attackPattern = 0;         // 0 : toutes, 1 : une sur deux, 2 : une sur trois, 3 : une sur quatre, 4 : au hasard
    float attackProb = 0.5f;       // probabilité si "au hasard"
    int repIndex = 0;              // numéro de la répétition (0 = première)

    // rythme
    double beatSamples = 0.0;      // durée d'un temps (0 = inconnue)
    float quantGridBeats = 0.0f;   // grille de recalage en temps (0 = off)
    float quantAmount = 0.0f;      // 0..1
    float humanizeMs = 0.0f;       // flou rythmique

    // vitesse de bande (hauteur + durée)
    float speed = 1.0f;

    // texture granulaire
    float textureMix = 0.0f;       // 0..1
    float grainMs = 100.0f;
    float grainDensity = 20.0f;    // grains par seconde
    float grainSpray = 0.3f;       // dispersion de la tête de lecture (0..1)
    float lowpassHz = 20000.0f;
    float highpassHz = 20.0f;
};

// Infos pour l'affichage de la phrase rejouée
struct RenderInfo
{
    std::vector<float> peaks;      // vignette de forme d'onde (0..1)
    std::vector<float> noteStarts; // début de chaque note dans la sortie (0..1)
    std::vector<float> shifts;     // décalage de hauteur appliqué (demi-tons)
    std::vector<int> sourceIndex;  // index d'origine de chaque note (-1 = muette)
    std::vector<char> noteReversed;
    std::vector<char> notePoly;    // 1 = accord
    bool reverse = false;
    int numNotes = 0;
};

std::vector<int> detectOnsets (const float* mono, int numSamples, double sampleRate, float sensitivity, float legato = 0.0f,
                               std::vector<char>* legatoFlags = nullptr);
int snapToZeroCrossing (const float* x, int numSamples, int pos, int radius);
float detectPitchYin (const float* x, int numSamples, double sampleRate, float& confidence);
bool inScale (int pitchClass, Scale scale);
float snapToScale (float midi, int root, Scale scale);
int stepInScale (int note, int steps, int root, Scale scale);

// Analyse (découpage + hauteurs si withPitch) gardée dans "cache" ; "generation" identifie le contenu de la phrase.
void analysePhrase (const juce::AudioBuffer<float>& source, int numSamples, double sampleRate, float sensitivity, float legato,
                    int playMode, PhraseAnalysis& cache, int generation, bool withPitch);

// source : phrase brute (numSamples échantillons, 1 ou 2 canaux)
// dest   : reçoit le rendu (agrandi si nécessaire). Retourne la longueur rendue
//          (différente de numSamples si la vitesse de bande n'est pas 1).
int renderPhrase (const juce::AudioBuffer<float>& source, int numSamples,
                   const RenderParams& params,
                   juce::AudioBuffer<float>& dest,
                   RenderInfo* info,
                   PhraseAnalysis* cache = nullptr, int generation = 0);
} // namespace pal
