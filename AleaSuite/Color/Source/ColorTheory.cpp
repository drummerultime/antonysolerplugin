#include "ColorTheory.h"
#include <algorithm>

namespace color::theory
{
using alea::hash3;
using alea::hUni;

static inline int pcm (int x) noexcept { return ((x % 12) + 12) % 12; }
static inline int popc (uint32_t m) noexcept { int n = 0; while (m) { m &= m - 1; ++n; } return n; }
static inline uint16_t bit (int pc) noexcept { return (uint16_t) (1u << pcm (pc)); }
static uint16_t maskOf (std::initializer_list<int> pcs) { uint16_t m = 0; for (int p : pcs) m |= bit (p); return m; }

// ===========================================================================
// Gammes (catalogue Kadmon / Persichetti / Russell)
static ScaleDef scales[] = {
    { "Ionian (major)",      0, 2, 0 }, { "Dorian",            0, 3, 0 }, { "Phrygian",         0, 3, 0 },
    { "Lydian",              0, 2, 0 }, { "Mixolydian",        0, 2, 0 }, { "Aeolian (minor)",  0, 3, 0 },
    { "Locrian",             0, 3, 0 },
    { "Melodic minor",       0, 3, 1 }, { "Dorian b2",         0, 3, 1 }, { "Lydian augmented", 0, 2, 1 },
    { "Lydian dominant",     0, 2, 1 }, { "Mixolydian b6",     0, 2, 1 }, { "Locrian #2",       0, 3, 1 },
    { "Altered",             0, 3, 1 },
    { "Harmonic minor",      0, 3, 2 }, { "Phrygian dominant", 0, 2, 2 }, { "Harmonic major",   0, 2, 2 },
    { "Double harmonic",     0, 2, 3 }, { "Hungarian minor",   0, 3, 3 }, { "Hungarian major",  0, 2, 3 },
    { "Neapolitan minor",    0, 3, 3 }, { "Neapolitan major",  0, 3, 3 }, { "Persian",          0, 2, 3 },
    { "Enigmatic",           0, 2, 3 },
    { "Whole tone",          0, 6, 4 }, { "Diminished (W-H)",  0, 3, 4 }, { "Diminished (H-W)", 0, 2, 4 },
    { "Augmented (hexatonic)",0, 2, 4 },
    { "Major pentatonic",    0, 4, 5 }, { "Minor pentatonic",  0, 5, 5 }, { "Blues",            0, 5, 5 },
    { "Hirajoshi",           0, 3, 5 }, { "Kumoi",             0, 3, 5 }, { "In-sen",           0, 3, 5 },
    { "Pelog",               0, 3, 5 },
    { "Bebop dominant",      0, 2, 6 }, { "Bebop major",       0, 2, 6 }, { "Chromatic",        0, 1, 4 } };
static constexpr int kNumScales = (int) (sizeof (scales) / sizeof (scales[0]));

static uint16_t scaleMasks[kNumScales];
static struct ScaleInit
{
    ScaleInit()
    {
        const std::initializer_list<int> d[kNumScales] = {
            { 0,2,4,5,7,9,11 }, { 0,2,3,5,7,9,10 }, { 0,1,3,5,7,8,10 }, { 0,2,4,6,7,9,11 }, { 0,2,4,5,7,9,10 }, { 0,2,3,5,7,8,10 },
            { 0,1,3,5,6,8,10 },
            { 0,2,3,5,7,9,11 }, { 0,1,3,5,7,9,10 }, { 0,2,4,6,8,9,11 }, { 0,2,4,6,7,9,10 }, { 0,2,4,5,7,8,10 }, { 0,2,3,5,6,8,10 },
            { 0,1,3,4,6,8,10 },
            { 0,2,3,5,7,8,11 }, { 0,1,4,5,7,8,10 }, { 0,2,4,5,7,8,11 },
            { 0,1,4,5,7,8,11 }, { 0,2,3,6,7,8,11 }, { 0,3,4,6,7,9,10 }, { 0,1,3,5,7,8,11 }, { 0,1,3,5,7,9,11 }, { 0,1,4,5,6,8,11 },
            { 0,1,4,6,8,10,11 },
            { 0,2,4,6,8,10 }, { 0,2,3,5,6,8,9,11 }, { 0,1,3,4,6,7,9,10 }, { 0,3,4,7,8,11 },
            { 0,2,4,7,9 }, { 0,3,5,7,10 }, { 0,3,5,6,7,10 }, { 0,2,3,7,8 }, { 0,2,3,7,9 }, { 0,1,5,7,10 }, { 0,1,3,7,8 },
            { 0,2,4,5,7,9,10,11 }, { 0,2,4,5,7,8,9,11 }, { 0,1,2,3,4,5,6,7,8,9,10,11 } };
        for (int i = 0; i < kNumScales; ++i) { scaleMasks[i] = maskOf (d[i]); scales[i].mask = scaleMasks[i]; }
        // equivalent exact dans la liste partagee (AleaLink) quand il existe : Dorien -> "Dorian", etc.
        for (int i = 0; i < kNumScales; ++i)
            for (int k = 1; k < alea::kNumSharedScales; ++k)
            {
                if (k == 7) continue;   // "Octaves" n'est pas une gamme
                uint16_t m = 0;
                for (int x : alea::scaleDegrees (k)) m |= (uint16_t) (1u << x);
                if (m == scaleMasks[i]) { scales[i].coarse = k; break; }
            }
    }
} scaleInit;

int numScales() { return kNumScales; }
const ScaleDef& scale (int i) { return scales[juce::jlimit (0, kNumScales - 1, i)]; }
static uint16_t smask (int i) { return scaleMasks[juce::jlimit (0, kNumScales - 1, i)]; }

juce::StringArray scaleNameList()
{
    juce::StringArray a;
    for (auto& s : scales) a.add (s.name);
    return a;
}

int scaleSize (int i) { return popc (smask (i)); }

int scaleDegreePc (int scaleIdx, int degree)
{
    int deg[12]; int n = 0;
    const uint16_t m = smask (scaleIdx);
    for (int p = 0; p < 12; ++p) if (m & (1u << p)) deg[n++] = p;
    if (n == 0) return 0;
    const int oct = (int) std::floor ((float) degree / (float) n);
    const int idx = ((degree % n) + n) % n;
    return oct * 12 + deg[idx];
}

bool inScale (int scaleIdx, int relPc) { return (smask (scaleIdx) & bit (relPc)) != 0; }

int nearestInScale (int scaleIdx, int relSemis)
{
    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (scaleIdx, relSemis - d)) return relSemis - d;
        if (inScale (scaleIdx, relSemis + d)) return relSemis + d;
    }
    return relSemis;
}

int modeForCoarse (int coarse, int current)
{
    // garde le mode courant s'il correspond deja, sinon le premier mode du catalogue qui correspond
    if (coarse <= 0 || coarse == 7 || coarse >= alea::kNumSharedScales) return current;
    if (scale (current).coarse == coarse) return current;
    for (int i = 0; i < kNumScales; ++i) if (scales[i].coarse == coarse) return i;
    return current;
}

float scaleBrightness (int scaleIdx)
{
    // somme des degres : locrien (sombre) < phrygien < eolien < dorien < mixolydien < ionien < lydien (clair)
    const uint16_t m = smask (scaleIdx);
    float s = 0; int n = 0;
    for (int p = 1; p < 12; ++p) if (m & (1u << p)) { s += (float) p; ++n; }
    if (n == 0) return 0.5f;
    return juce::jlimit (0.0f, 1.0f, (s / (float) n - 5.0f) / 2.5f + 0.5f);
}

// ===========================================================================
// Qualites
static const QualDef quals[kNumQ] = {
    { "",        { 0, 4, 7 },             3, FMaj, 3 },  { "m",       { 0, 3, 7 },             3, FMin, 3 },
    { "dim",     { 0, 3, 6 },             3, FDim, 3 },  { "aug",     { 0, 4, 8 },             3, FAug, 3 },
    { "sus2",    { 0, 2, 7 },             3, FSus, 3 },  { "sus4",    { 0, 5, 7 },             3, FSus, 3 },
    { "5",       { 0, 7 },                2, FOther, 2 },
    { "6",       { 0, 4, 7, 9 },          4, FMaj, 4 },  { "m6",      { 0, 3, 7, 9 },          4, FMin, 4 },
    { "6/9",     { 0, 4, 7, 9, 14 },      5, FMaj, 4 },
    { "maj7",    { 0, 4, 7, 11 },         4, FMaj, 4 },  { "7",       { 0, 4, 7, 10 },         4, FDom, 4 },
    { "m7",      { 0, 3, 7, 10 },         4, FMin, 4 },  { "m7b5",    { 0, 3, 6, 10 },         4, FHalfDim, 4 },
    { "dim7",    { 0, 3, 6, 9 },          4, FDim, 4 },  { "m(maj7)", { 0, 3, 7, 11 },         4, FMin, 4 },
    { "maj7#5",  { 0, 4, 8, 11 },         4, FAug, 4 },  { "maj7#11", { 0, 4, 7, 11, 18 },     5, FMaj, 4 },
    { "7sus4",   { 0, 5, 7, 10 },         4, FSus, 4 },  { "add9",    { 0, 4, 7, 14 },         4, FMaj, 3 },
    { "madd9",   { 0, 3, 7, 14 },         4, FMin, 3 },  { "maj9",    { 0, 4, 7, 11, 14 },     5, FMaj, 4 },
    { "9",       { 0, 4, 7, 10, 14 },     5, FDom, 4 },  { "m9",      { 0, 3, 7, 10, 14 },     5, FMin, 4 },
    { "m11",     { 0, 3, 7, 10, 14, 17 }, 6, FMin, 4 },  { "13",      { 0, 4, 7, 10, 14, 21 }, 6, FDom, 4 },
    { "7b9",     { 0, 4, 7, 10, 13 },     5, FDom, 4 },  { "7#9",     { 0, 4, 7, 10, 15 },     5, FDom, 4 },
    { "7alt",    { 0, 4, 10, 13, 15, 20 },6, FDom, 4 },  { "7#11",    { 0, 4, 7, 10, 14, 18 }, 6, FDom, 4 },
    { "7b13",    { 0, 4, 7, 10, 20 },     5, FDom, 4 },  { "9sus4",   { 0, 5, 7, 10, 14 },     5, FSus, 4 },
    { "q3",      { 0, 5, 10 },            3, FQuartal, 3 }, { "q4",   { 0, 5, 10, 15 },        4, FQuartal, 4 },
    { "sowhat",  { 0, 5, 10, 15, 19 },   5, FQuartal, 4 },
    { "[0148]",  { 0, 1, 4, 8 },          4, FOther, 4 }, { "[016]",  { 0, 6, 11 },            3, FOther, 3 },
    { "(cl)",    { 0, 2, 4 },             3, FOther, 3 }, { "(myst)", { 0, 6, 10, 16, 21, 26 }, 6, FOther, 6 },
    { "[hex]",   { 0, 3, 4, 7, 8, 11 },   6, FOther, 6 }, { "7b5",    { 0, 4, 6, 10 },         4, FDom, 4 } };

int numQualities() { return kNumQ; }
const QualDef& quality (int q) { return quals[juce::jlimit (0, kNumQ - 1, q)]; }
juce::StringArray qualityNameList()
{
    juce::StringArray a;
    for (auto& q : quals) a.add (juce::String (q.sym).isEmpty() ? juce::String ("maj") : juce::String (q.sym));
    return a;
}
int findQuality (const char* sym)
{
    for (int i = 0; i < kNumQ; ++i) if (juce::String (quals[i].sym) == sym) return i;
    return 0;
}

int chordTones (const Chord& c, int* out)
{
    const auto& q = quality (c.qual);
    int n = 0; uint16_t seen = 0;
    for (int k = 0; k < q.n; ++k)
    {
        const int p = pcm (c.root + q.iv[k]);
        if (seen & bit (p)) continue;
        seen |= bit (p); out[n++] = p;
    }
    return n;
}

uint16_t chordMask (const Chord& c)
{
    const auto& q = quality (c.qual);
    uint16_t m = 0;
    for (int k = 0; k < q.n; ++k) m |= bit (c.root + q.iv[k]);
    return m;
}

int chordBassPc (const Chord& c)
{
    if (c.bass >= 0) return pcm (c.bass);
    const auto& q = quality (c.qual);
    const int inv = juce::jlimit (0, juce::jmax (0, (int) q.triadSize - 1), (int) c.inversion);
    return pcm (c.root + q.iv[inv]);
}

static const char* noteNames[12] { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

juce::String chordName (const Chord& c, int keyRoot)
{
    const int r = pcm (keyRoot + c.root);
    juce::String s = juce::String (noteNames[r]) + quality (c.qual).sym;
    const int b = chordBassPc (c);
    if (b != pcm (c.root)) s << "/" << noteNames[pcm (keyRoot + b)];
    return s;
}

static const char* romanAbs[12] { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };

static juce::String romanCore (int rootRel, int family)
{
    juce::String r (romanAbs[pcm (rootRel)]);
    const bool lower = family == FMin || family == FHalfDim || family == FDim;
    if (lower) r = r.toLowerCase().replace ("b", "b").replace ("#", "#");
    // toLowerCase ne touche pas aux alterations, mais "bii" garde le b
    return r;
}

juce::String romanNumeral (const Chord& c, int scaleIdx)
{
    const auto& q = quality (c.qual);
    const uint16_t m = chordMask (c);
    const bool diatonic = (m & ~smask (scaleIdx)) == 0;
    juce::String suffix (q.sym);
    if (q.family == FMin) suffix = suffix.fromFirstOccurrenceOf ("m", false, false);
    if (c.qual == QDim) suffix = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
    if (c.qual == QDim7) suffix = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + "7";
    if (c.qual == QHalfDim) suffix = juce::String (juce::CharPointer_UTF8 ("\xc3\xb8")) + "7";
    if (c.qual == QMaj) suffix = {};
    if (c.qual == QAug) suffix = "+";
    // dominante secondaire : V7/x
    if (q.family == FDom && ! diatonic && pcm (c.root) != 0)
    {
        const int target = pcm (c.root + 5);
        if (inScale (scaleIdx, target) && target != 0)
        {
            Chord t; t.root = (int8_t) target;
            const bool minorTarget = inScale (scaleIdx, target + 3) && ! inScale (scaleIdx, target + 4);
            return "V" + suffix + "/" + romanCore (target, minorTarget ? FMin : FMaj);
        }
        if (target == 0 && pcm (c.root) == 1) return "subV" + suffix;     // substitution tritonique de V
    }
    return romanCore (c.root, q.family) + suffix;
}

int chordFunction (const Chord& c, int scaleIdx)
{
    juce::ignoreUnused (scaleIdx);
    const auto& q = quality (c.qual);
    const int r = pcm (c.root);
    if (q.family == FDom || c.qual == QDim7)
    {
        if (r == 0) return 0;              // I7 (blues)
        if (r == 5) return 1;              // IV7 (blues)
        return 2;
    }
    static const int f[12] { 0, 3, 1, 0, 0, 1, 3, 2, 1, 0, 1, 2 };
    return f[r];
}

// ===========================================================================
// Tension (Persichetti) et gravite (Russell)
static float pairTension (int a, int b)
{
    const int d = std::abs (a - b);
    const int ic = d % 12;
    static const float t[12] { 0.0f, 1.0f, 0.45f, 0.1f, 0.1f, 0.15f, 0.7f, 0.05f, 0.1f, 0.1f, 0.45f, 1.0f };
    float v = t[ic];
    if (d > 12 && (ic == 1 || ic == 2 || ic == 10 || ic == 11)) v *= 0.7f;   // ecartees, les dissonances mordent moins
    return v;
}

float intervalTension (const int* notes, int n)
{
    if (n < 2) return 0.0f;
    float s = 0; int pairs = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) { s += pairTension (notes[i], notes[j]); ++pairs; }
    const float mean = s / (float) pairs;
    return juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-mean * 3.2f));
}

float chordTension (const Chord& c)
{
    const auto& q = quality (c.qual);
    int notes[8]; int n = 0;
    for (int k = 0; k < q.n; ++k) notes[n++] = 48 + q.iv[k];
    return intervalTension (notes, n);
}

static const int lcOrder[12] { 0, 7, 2, 9, 4, 11, 6, 8, 3, 10, 5, 1 };

int lcRank (int pc)
{
    pc = pcm (pc);
    for (int i = 0; i < 12; ++i) if (lcOrder[i] == pc) return i;
    return 11;
}

int lydianTonic (const Chord& c)
{
    const auto& q = quality (c.qual);
    switch (q.family)
    {
        case FMaj: case FAug: return pcm (c.root);
        case FMin: return pcm (c.root + 3);
        case FDom: case FSus: case FQuartal: return pcm (c.root + 10);
        case FHalfDim: case FDim: return pcm (c.root + 6);
        default: return pcm (c.root);
    }
}

static int keyLydianTonic (int scaleIdx)
{
    const uint16_t m = smask (scaleIdx);
    int best = 0, bestScore = -100;
    for (int t = 0; t < 12; ++t)
    {
        int score = 0;
        for (int k = 0; k < 7; ++k) if (m & bit (t + lcOrder[k])) score += 3;
        score -= lcRank (t);   // a egalite : la tonique lydienne la plus proche de la tonique du mode
        if (score > bestScore) { bestScore = score; best = t; }
    }
    return best;
}

float chordGravity (const Chord& c, int keyLT)
{
    const float rankLT = (float) lcRank (lydianTonic (c) - keyLT) / 11.0f;
    int tones[12]; const int n = chordTones (c, tones);
    float avg = 0;
    for (int i = 0; i < n; ++i) avg += (float) lcRank (tones[i] - keyLT);
    avg /= (float) juce::jmax (1, n) * 11.0f;
    return juce::jlimit (0.0f, 1.0f, 0.65f * rankLT + 0.35f * avg);
}

static bool chordInScale (const Chord& c, int scaleIdx) { return (chordMask (c) & ~smask (scaleIdx)) == 0; }

int parentScale (const Chord& c, float outgoing)
{
    const auto& q = quality (c.qual);
    const bool out = outgoing > 0.55f, far = outgoing > 0.8f;
    switch (q.family)
    {
        case FMaj:     return out ? (far ? 9 : 3) : 3;           // Lydien (Russell) / Lydien augmente
        case FMin:     return c.qual == QMinMaj7 ? 7 : (out ? (far ? 8 : 7) : 1);   // Dorien / mineur melodique / dorien b2
        case FDom:     return out ? (far ? 13 : 10) : (c.qual == QDom7b9 ? 26 : 4);  // mixolydien / lydien dominant / altere
        case FHalfDim: return out ? 12 : 6;                      // locrien / locrien #2
        case FDim:     return 25;                                // diminuee ton/demi-ton
        case FAug:     return out ? 24 : 9;                      // lydien augmente / par tons
        case FSus:     return out ? 8 : 4;
        case FQuartal: return out ? 29 : 1;
        default:       return (c.qual == Q419 || c.qual == QHexatonic) ? 27 : (c.qual == QMystic ? 10 : 37);
    }
}

int parentScaleRoot (const Chord& c, float outgoing) { juce::ignoreUnused (outgoing); return pcm (c.root); }

juce::String parentScaleName (const Chord& c, float outgoing, int keyRoot)
{
    return juce::String (noteNames[pcm (keyRoot + parentScaleRoot (c, outgoing))]) + " " + scale (parentScale (c, outgoing)).name;
}

// ---------------------------------------------------------------------------
static int bestQualityForRoot (int root, uint16_t mask, int bassPc, float& scoreOut)
{
    int best = 0; float bestS = -1.0e9f;
    for (int q = 0; q < kNumQ; ++q)
    {
        Chord c; c.root = (int8_t) root; c.qual = (int8_t) q;
        const uint16_t cm = chordMask (c);
        const int match = popc (cm & mask), missing = popc (cm & ~mask), extra = popc (mask & ~cm);
        float s = (float) match * 2.0f - (float) missing * 3.0f - (float) extra * 1.6f;
        if (pcm (bassPc) == pcm (root)) s += 1.5f;
        if (quality (q).family == FOther) s -= 1.2f;      // set-classes seulement si rien d'autre ne convient
        s -= 0.05f * (float) quality (q).n;
        if (s > bestS) { bestS = s; best = q; }
    }
    scoreOut = bestS;
    return best;
}

bool identifyChord (uint16_t pcMask, int bassPc, Chord& out)
{
    if (pcMask == 0) return false;
    float best = -1.0e9f;
    for (int r = 0; r < 12; ++r)
    {
        if (! (pcMask & bit (r))) continue;
        float s; const int q = bestQualityForRoot (r, pcMask, bassPc, s);
        if (s > best) { best = s; out.root = (int8_t) r; out.qual = (int8_t) q; }
    }
    out.inversion = 0;
    out.bass = (bassPc >= 0 && pcm (bassPc) != out.root) ? (int8_t) pcm (bassPc) : (int8_t) -1;
    return true;
}

Chord diatonic (int scaleIdx, int degree, int size, int stack)
{
    const int step = stack == StackFourths ? 3 : (stack == StackSeconds ? 1 : 2);
    const int rootPc = scaleDegreePc (scaleIdx, degree);
    uint16_t m = 0;
    for (int k = 0; k < size; ++k) m |= bit (scaleDegreePc (scaleIdx, degree + k * step));
    Chord c; c.root = (int8_t) pcm (rootPc);
    if (stack == StackFourths)
    {
        c.qual = (int8_t) (size >= 5 ? QSoWhat : (size == 4 ? QQuartal4 : QQuartal3));
        return c;
    }
    if (stack == StackSeconds) { c.qual = QCluster; return c; }
    float s; c.qual = (int8_t) bestQualityForRoot (c.root, m, c.root, s);
    return c;
}

// ===========================================================================
// Generateurs
juce::StringArray styleNames()
{
    return { "Pop", "Fonctionnel", "Jazz II-V", "Modal", "Cinematique", "Techno / Deep", "Gospel / Soul", "Blues",
             "Impressionniste", "Atonal (set classes)", "Lydien (Russell)", "Aleatoire", "Neo Soul", "Bossa / Latin jazz" };
}
juce::StringArray cycleNames() { return { "Cycle: Quintes", "Cycle: Tierces", "Cycle: Secondes", "Cycle: Mixte" }; }

namespace
{
struct Gen
{
    const GenOptions& o;
    alea::FastRng rng;
    int hs;            // gamme d'harmonisation (7 sons)
    bool minorKey;
    std::vector<Chord> out;
    int beats;

    explicit Gen (const GenOptions& opt)
        : o (opt), rng (hash3 (opt.seed * 2654435761u + 17u, (uint32_t) opt.style + 101u, 0xC0102u) | 1u)
    {
        for (int i = 0; i < 3; ++i) rng.next();
        const int sc = juce::jlimit (0, kNumScales - 1, o.scale);
        minorKey = inScale (sc, 3) && ! inScale (sc, 4);
        hs = scaleSize (sc) == 7 ? sc : (minorKey ? 5 : 0);
        beats = o.barsPerChord == 0 ? 2 : (o.barsPerChord == 2 ? 8 : 4);
    }
    float u() { return rng.uni(); }
    int ri (int n) { return n <= 1 ? 0 : (int) (rng.next() % (uint32_t) n); }
    int size() const { return 3 + (o.complexity > 0.3f ? 1 : 0) + (o.complexity > 0.7f ? 1 : 0); }
    Chord deg (int d, int sz = -1) { Chord c = diatonic (hs, d, sz < 0 ? size() : sz); c.beats = (int8_t) beats; return c; }
    Chord ch (int root, int q, int b = -1) { Chord c; c.root = (int8_t) pcm (root); c.qual = (int8_t) q; c.beats = (int8_t) (b < 0 ? beats : b); return c; }
    void add (const Chord& c) { if (out.size() < 16) out.push_back (c); }
    void loop (const std::vector<int>& degs)
    {
        for (int i = 0; (int) out.size() < o.numChords; ++i) add (deg (degs[(size_t) i % degs.size()]));
    }

    // enrichit une qualite simple selon la complexite / tension (et la fonction : V qui resout -> alteree)
    Chord colorize (Chord c, const Chord* next)
    {
        const auto fam = quality (c.qual).family;
        const float cx = o.complexity, tx = o.tension;
        if (c.qual == QMaj)  c.qual = (int8_t) (cx > 0.75f ? (inScale (hs, c.root + 6) ? QMaj7s11 : QMaj9) : cx > 0.45f ? QMaj7 : cx > 0.25f ? QAdd9 : QMaj);
        else if (c.qual == QMin) c.qual = (int8_t) (cx > 0.8f ? QMin11 : cx > 0.55f ? QMin9 : cx > 0.35f ? QMin7 : QMin);
        else if (c.qual == QMaj7 && cx > 0.6f) c.qual = (int8_t) (inScale (hs, c.root + 6) ? QMaj7s11 : QMaj9);
        else if (c.qual == QMin7 && cx > 0.6f) c.qual = (int8_t) (cx > 0.85f ? QMin11 : QMin9);
        else if (fam == FDom)
        {
            const bool resolves = next != nullptr && pcm (next->root - c.root) == 5;
            if (resolves && tx > 0.55f) c.qual = (int8_t) (u() < 0.5f ? QAlt : (u() < 0.5f ? QDom7b9 : QDom7s9));
            else if (cx > 0.7f) c.qual = (int8_t) (u() < 0.5f ? QDom13 : QDom9);
            else if (cx > 0.45f && c.qual == QDom7) c.qual = QDom9;
        }
        return c;
    }
    void colorizeAll()
    {
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = colorize (out[i], &out[(i + 1) % out.size()]);
    }

    // --------------------------------------------------------------- styles
    void pop()
    {
        static const std::vector<std::vector<int>> maj { { 0, 4, 5, 3 }, { 5, 3, 0, 4 }, { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 3, 0, 4, 5 }, { 0, 4, 3, 4 }, { 0, 3, 0, 4 }, { 0, 2, 3, 4 } };
        static const std::vector<std::vector<int>> mnr { { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 0, 5, 6, 0 }, { 0, 2, 6, 3 }, { 0, 3, 6, 2 } };
        const auto& set = minorKey ? mnr : maj;
        loop (set[(size_t) ri ((int) set.size())]);
        if (o.tension > 0.5f && minorKey) for (auto& c : out) if (pcm (c.root) == 7 && u() < 0.8f) c.qual = QDom7;   // V harmonique
        colorizeAll();
    }

    void functional()
    {
        // Persichetti : primaires selon le cycle (quintes I IV V / tierces I III VI / secondes I II VII)
        int cyc = o.cycle == 3 ? ri (3) : o.cycle;
        static const int T[3][3] { { 0, 5, 2 }, { 0, 3, 5 }, { 0, 1, 6 } };
        static const int S[3][2] { { 1, 3 }, { 5, 3 }, { 1, 3 } };
        static const int D[3][2] { { 4, 6 }, { 2, 4 }, { 6, 1 } };
        static const int cad[3][2] { { 4, 0 }, { 2, 0 }, { 1, 0 } };
        int f = 0;   // 0 T, 1 S, 2 D
        const int n = juce::jmax (2, o.numChords);
        for (int i = 0; i < n - 2; ++i)
        {
            int d;
            if (f == 0) d = (i == 0) ? 0 : T[cyc][ri (3)];
            else if (f == 1) d = S[cyc][ri (2)];
            else d = D[cyc][ri (2)];
            add (deg (d));
            const float r = u();
            f = f == 0 ? (r < 0.6f ? 1 : (r < 0.85f ? 2 : 0)) : f == 1 ? (r < 0.7f ? 2 : 1) : (r < 0.8f ? 0 : 1);
            if (o.cycle == 3 && f == 0) cyc = ri (3);
        }
        add (deg (cad[cyc][0])); add (deg (cad[cyc][1]));
        // dominantes secondaires (tension) et emprunts au mode parallele (chroma)
        for (size_t i = 0; i + 1 < out.size(); ++i)
        {
            const int tf = quality (out[i + 1].qual).family;
            if (i > 0 && o.tension > 0.25f && u() < o.tension * 0.5f && pcm (out[i + 1].root) != 0 && (tf == FMaj || tf == FMin))
                out[i] = ch (out[i + 1].root + 7, QDom7, out[i].beats);
            else if (i > 0 && o.chroma > 0.2f && u() < o.chroma * 0.4f && quality (out[i].qual).family == FMaj)
                out[i].qual = (int8_t) (out[i].qual == QMaj7 ? QMin7 : QMin);
        }
        if (minorKey) for (auto& c : out) if (pcm (c.root) == 7) c.qual = QDom7;
        colorizeAll();
    }

    void jazz()
    {
        const int half = juce::jmax (1, beats / 2);
        auto twoFive = [&] (int target, bool minorT)
        {
            add (ch (target + 2, minorT ? QHalfDim : QMin7, half));
            add (ch (target + 7, minorT ? QDom7b9 : QDom7, half));
        };
        if (! minorKey)
        {
            const int tpl = ri (5);
            while ((int) out.size() < o.numChords)
            {
                switch (tpl)
                {
                    case 0: add (ch (0, QMaj7)); add (ch (9, QDom7)); add (ch (2, QMin7)); add (ch (7, QDom7)); break;
                    case 1: add (ch (2, QMin7)); add (ch (7, QDom7)); add (ch (0, QMaj7)); add (ch (0, QMaj7)); break;
                    case 2: add (ch (4, QMin7)); add (ch (9, QDom7)); add (ch (2, QMin7)); add (ch (7, QDom7)); break;
                    case 3: add (ch (0, QMaj7)); add (ch (1, QDim7)); add (ch (2, QMin7)); add (ch (7, QDom7)); break;
                    default: add (ch (0, QMaj7)); twoFive (5, false); add (ch (5, QMaj7)); add (ch (5, QMin7)); add (ch (10, QDom7)); break;
                }
            }
        }
        else
        {
            while ((int) out.size() < o.numChords)
            {
                if (u() < 0.5f) { add (ch (0, QMinMaj7)); add (ch (0, QMin7)); twoFive (0, true); }
                else { add (ch (0, QMin9)); add (ch (5, QMin7)); add (ch (8, QMaj7)); add (ch (7, QDom7b9)); }
            }
        }
        out.resize ((size_t) juce::jmin ((int) out.size(), 16));
        // substitutions tritoniques et dominantes secondaires (chroma)
        for (size_t i = 0; i < out.size(); ++i)
            if (quality (out[i].qual).family == FDom && u() < o.chroma * 0.6f) { out[i].root = (int8_t) pcm (out[i].root + 6); }
        colorizeAll();
    }

    void modal()
    {
        const int sc = o.scale;
        std::vector<Chord> v;
        auto m = [&] (int r, int q) { v.push_back (ch (r, q, beats * 2)); };
        switch (sc)
        {
            case 1:  m (0, QMin7); m (5, QDom7); break;                         // dorien : i7 - IV7 (6te majeure)
            case 2:  m (0, QMin); m (1, QMaj); m (0, QMin); m (10, QMin7); break; // phrygien : i - bII
            case 3:  m (0, QMaj7s11); m (2, QDom9); break;                      // lydien : I - II
            case 4:  m (0, QDom9); m (10, QMaj); m (5, QMaj); m (0, QDom7); break; // mixolydien : I - bVII - IV
            case 5:  m (0, QMin9); m (8, QMaj7); m (10, QAdd9); break;          // eolien : i - bVI - bVII
            case 6:  m (0, QHalfDim); m (1, QMaj7); break;                      // locrien : io - bII
            case 0:  m (0, QMaj9); m (5, QMaj7); break;
            default:
                m (0, diatonic (hs, 0, 4).qual);
                for (int d : { 1, 6, 3 }) if (inScale (o.scale, scaleDegreePc (hs, d))) { m (scaleDegreePc (hs, d), diatonic (hs, d, 4).qual); break; }
                break;
        }
        for (int i = 0; (int) out.size() < o.numChords; ++i) add (v[(size_t) i % v.size()]);
        for (auto& c : out)   // "So What" et sus : couleur modale (Persichetti, Corea/Beirach)
            if (u() < o.tension * 0.5f) c.qual = (int8_t) (quality (c.qual).family == FMin ? QSoWhat : QSus9);
        if (o.numChords <= 2) for (auto& c : out) c.beats = (int8_t) juce::jmin (16, beats * 2);
        else for (auto& c : out) c.beats = (int8_t) beats;
    }

    void cinematic()
    {
        // neo-riemannien : P, L, R, mediantes chromatiques, pole hexatonique
        bool isMinor = minorKey;
        int root = 0;
        add (ch (root, isMinor ? QMin : QMaj));
        while ((int) out.size() < o.numChords)
        {
            const float r = u();
            if (r < 0.2f)       { isMinor = ! isMinor; }                                        // P
            else if (r < 0.4f)  { root += isMinor ? 3 : 9; isMinor = ! isMinor; }               // R
            else if (r < 0.6f)  { root += isMinor ? 8 : 4; isMinor = ! isMinor; }               // L
            else if (r < 0.8f)  { root += (u() < 0.5f ? 4 : 8); }                               // mediante chromatique (meme mode)
            else if (r < 0.9f)  { root += isMinor ? 4 : 8; isMinor = ! isMinor; }               // pole hexatonique
            else                { root += (u() < 0.5f ? 3 : 9); }                               // tierce mineure
            if ((int) out.size() == o.numChords - 1 && o.numChords > 2) { root = 7; isMinor = false; }
            add (ch (root, isMinor ? QMin : QMaj));
        }
        for (auto& c : out)
        {
            if (o.complexity > 0.3f) c.qual = (int8_t) (c.qual == QMin ? (o.complexity > 0.6f ? QMin9 : QMinAdd9) : (o.complexity > 0.6f ? QMaj9 : QAdd9));
            if (u() < o.tension * 0.3f) c.qual = QSus2;
        }
    }

    void techno()
    {
        const int v = ri (5);
        std::vector<Chord> p;
        switch (v)
        {
            case 0: p = { ch (0, QMin9) }; break;
            case 1: p = { ch (0, QMin9), ch (10, QSus9) }; break;
            case 2: p = { ch (0, QMin7), ch (5, QMin7) }; break;
            case 3: p = { ch (0, QMin9), ch (3, QMin9), ch (5, QMin9), ch (10, QMin9) }; break;   // structure constante
            default: p = { ch (0, QMin9), ch (8, QMaj7), ch (10, QSus9) }; break;
        }
        if (o.tension > 0.6f) for (auto& c : p) c.qual = (int8_t) (u() < 0.5f ? QQuartal4 : QSoWhat);
        for (int i = 0; (int) out.size() < o.numChords; ++i) add (p[(size_t) i % p.size()]);
    }

    void gospel()
    {
        const int v = ri (4);
        const int half = juce::jmax (1, beats / 2);
        auto add2 = [&] (Chord c, int b) { c.beats = (int8_t) b; add (c); };
        while ((int) out.size() < o.numChords)
        {
            switch (v)
            {
                case 0: { Chord iv = ch (5, QMaj); iv.bass = 0; add2 (ch (0, QMaj9), beats); add2 (iv, beats); break; }      // I - IV/I
                case 1: add2 (ch (0, QMaj7), half); add2 (ch (4, QMin7), half); add2 (ch (5, QMaj7), half); add2 (ch (6, QDim7), half);
                        { Chord i64 = ch (0, QMaj); i64.inversion = 2; add2 (i64, half); } add2 (ch (9, QMin7), half);
                        add2 (ch (2, QMin9), half); { Chord iv5 = ch (5, QMaj); iv5.bass = 7; add2 (iv5, half); } break;     // IV/V = V9sus
                case 2: add2 (ch (5, QMaj7), beats); add2 (ch (5, QMin6), beats); add2 (ch (0, QAdd9), beats * 2); break;    // plagale mineure
                default: add2 (ch (0, QMaj), half); add2 (ch (0, QDom7), half); add2 (ch (5, QMaj), half); add2 (ch (5, QMin6), half); break;
            }
        }
        out.resize ((size_t) juce::jmin ((int) out.size(), 16));
    }

    void blues()
    {
        const bool jazzy = o.complexity > 0.5f;
        const int b = 4;
        if (minorKey)
        {
            const int rr[12] { 0, 5, 0, 0, 5, 5, 0, 0, 8, 7, 0, 7 };
            const int qq[12] { QMin7, QMin7, QMin7, QMin7, QMin7, QMin7, QMin7, QMin7, QDom7, QDom7b9, QMin7, QDom7b9 };
            for (int i = 0; i < 12; ++i) add (ch (rr[i], qq[i], b));
            return;
        }
        if (! jazzy)
        {
            const int rr[12] { 0, 5, 0, 0, 5, 5, 0, 0, 7, 5, 0, 7 };
            for (int i = 0; i < 12; ++i) add (ch (rr[i], QDom7, b));
            return;
        }
        // blues jazz (bebop) : condense en 16 accords max
        add (ch (0, QDom7, 4)); add (ch (5, QDom7, 4)); add (ch (0, QDom7, 4)); add (ch (7, QMin7, 2)); add (ch (0, QDom7, 2));
        add (ch (5, QDom7, 4)); add (ch (6, QDim7, 4)); add (ch (0, QDom7, 4)); add (ch (9, QDom7, 4));
        add (ch (2, QMin7, 4)); add (ch (7, QDom7, 4)); add (ch (0, QDom7, 2)); add (ch (9, QDom7, 2)); add (ch (2, QMin7, 2)); add (ch (7, QDom7, 2));
    }

    void impressionist()
    {
        const int mode = ri (3);    // 0 planing (structure constante), 1 par tons, 2 cycle de secondes/tierces
        const int q = o.complexity > 0.5f ? (u() < 0.5f ? QMaj9 : QMin9) : (u() < 0.5f ? QMaj7 : QQuartal4);
        int d = 0;
        bool pedal = u() < 0.4f + o.tension * 0.3f;
        for (int i = 0; i < o.numChords; ++i)
        {
            Chord c;
            if (mode == 0)
            {
                const int pc = scaleDegreePc (hs, d);
                c = (u() < o.chroma) ? ch (pc, q) : deg (d);    // parallelisme reel (chromatique) ou tonal (diatonique)
                if (u() < 0.6f) c.qual = (int8_t) q;
                d += (u() < 0.5f ? 1 : -1);
            }
            else if (mode == 1) { c = ch (i * 2, u() < 0.5f ? QAug : QMaj7s5); }
            else { c = deg (d); d += (u() < 0.5f ? 1 : 2) * (u() < 0.5f ? 1 : -1); }
            if (pedal) c.bass = 0;
            add (c);
        }
    }

    void atonal()
    {
        // Boss : familles 4-19, 3-5 (viennois), hexatonique ; enchainements par invariance n-1 et circulation de l'agregat
        static const int fam[] { Q419, QViennese, QMinMaj7, QMaj7s5, QAug, QDom7b5, QQuartal3, QHexatonic };
        uint16_t recent = 0;
        Chord cur = ch (0, fam[ri (6)]);
        add (cur); recent |= chordMask (cur);
        while ((int) out.size() < o.numChords)
        {
            if (u() < o.chroma * 0.25f && (int) out.size() + 1 < o.numChords)
            {   // "spectre de tonalite" : une cadence V-i, brouillee juste apres
                add (ch (7, QDom7)); cur = ch (0, QMin); add (cur); recent = chordMask (cur); continue;
            }
            Chord best = cur; float bestS = -1.0e9f;
            const bool aggregate = u() < 0.5f;
            for (int r = 0; r < 12; ++r)
                for (int qq : fam)
                {
                    Chord c = ch (r, qq);
                    if (c.root == cur.root && c.qual == cur.qual) continue;
                    const uint16_t m = chordMask (c), pm = chordMask (cur);
                    const int common = popc (m & pm), sz = popc (m);
                    float s = aggregate ? (float) popc (m & ~recent) * 1.5f : (common == sz - 1 ? 4.0f : -(float) std::abs (common - (sz - 1)));
                    s += u() * 1.5f;
                    if (s > bestS) { bestS = s; best = c; }
                }
            cur = best; add (cur);
            recent |= chordMask (cur);
            if (popc (recent) >= 12) recent = chordMask (cur);
        }
    }

    void lydian()
    {
        // Russell : accords dont la tonique lydienne est proche (ingoing) ; la tension ouvre vers l'outgoing
        const int keyLT = keyLydianTonic (o.scale);
        const int limit = 1 + (int) std::round (o.tension * 9.0f);
        static const int pool[] { QMaj7s11, QMaj7, QMin9, QDom9, QHalfDim, QMaj7s5, QSus9, QMin7, QDom7s11 };
        std::vector<Chord> cands; std::vector<float> w;
        for (int r = 0; r < 12; ++r)
            for (int q : pool)
            {
                Chord c = ch (r, q);
                const int rank = lcRank (lydianTonic (c) - keyLT);
                if (rank > limit) continue;
                cands.push_back (c);
                w.push_back (1.0f / (1.0f + (float) rank) + (inScale (o.scale, r) ? 0.4f : 0.0f));
            }
        if (cands.empty()) { add (ch (0, QMaj7s11)); return; }
        float tot = 0; for (float x : w) tot += x;
        while ((int) out.size() < o.numChords)
        {
            float r = u() * tot; size_t k = 0;
            while (k + 1 < cands.size() && r > w[k]) { r -= w[k]; ++k; }
            add (cands[k]);
        }
        // stations toniques (I majeur / VI mineur de la gamme lydienne) en fin de phrase
        Chord st = (u() < 0.5f) ? ch (keyLT, QMaj7s11) : ch (keyLT + 9, QMin9);
        st.beats = (int8_t) beats;
        out.back() = st;
    }

    // Neo soul (Dobbins : accords de 9e / 11e, iv mineur emprunte, V7#9, plagal "gospel" ; mouvements par secondes)
    void neoSoul()
    {
        struct P { int r; int q; };
        static const std::vector<std::vector<P>> maj {
            { { 0, QMaj9 }, { 4, QMin7 }, { 9, QMin9 }, { 2, QMin9 } },
            { { 5, QMaj9 }, { 5, QMin9 }, { 4, QMin7 }, { 9, QDom7s9 } },
            { { 2, QMin9 }, { 7, QSus9 }, { 0, QMaj9 }, { 9, QMin11 } },
            { { 0, QMaj7s11 }, { 10, QDom9 }, { 5, QMaj9 }, { 5, QMin9 } },
            { { 5, QMaj9 }, { 4, QMin9 }, { 3, QMaj9 }, { 2, QMin11 } } };
        static const std::vector<std::vector<P>> mnr {
            { { 0, QMin9 }, { 5, QMin9 }, { 10, QSus9 }, { 3, QMaj7 } },
            { { 0, QMin11 }, { 8, QMaj7 }, { 2, QHalfDim }, { 7, QDom7s9 } },
            { { 5, QMin9 }, { 7, QDom7s9 }, { 0, QMin9 }, { 8, QMaj9 } },
            { { 0, QMin9 }, { 3, QMaj7 }, { 8, QMaj7s11 }, { 7, QSus9 } } };
        const auto& set = minorKey ? mnr : maj;
        const auto& pat = set[(size_t) ri ((int) set.size())];
        for (int i = 0; (int) out.size() < o.numChords; ++i) add (ch (pat[(size_t) i % pat.size()].r, pat[(size_t) i % pat.size()].q));
        // chroma : dominante remplacee par sa substitution tritonique, accords de passage par demi-ton
        for (size_t i = 0; i < out.size(); ++i)
            if (quality (out[i].qual).family == FDom && u() < o.chroma * 0.6f) out[i].root = (int8_t) pcm (out[i].root + 6);
        if (o.complexity < 0.3f)
            for (auto& c : out) { const int f = quality (c.qual).family; c.qual = (int8_t) (f == FMaj ? QMaj7 : f == FMin ? QMin7 : f == FDom ? QDom7 : c.qual); }
    }

    // Bossa / latin jazz : changements tous les 2 temps, ii-V avec demi-diminues, II7 (V/V), subV
    void bossa()
    {
        const int h = juce::jmax (2, beats / 2);
        struct P { int r; int q; int b; };
        static const std::vector<std::vector<P>> maj {
            { { 0, QMaj7, 2 }, { 2, QDom9, 2 }, { 2, QMin7, 1 }, { 1, QDom7s11, 1 } },                                    // I - II7 - ii - bII7
            { { 0, QMaj7, 1 }, { 9, QMin7, 1 }, { 2, QMin7, 1 }, { 7, QDom7, 1 } },                                       // I vi ii V
            { { 0, QMaj7, 1 }, { 6, QHalfDim, 1 }, { 11, QDom7b9, 1 }, { 4, QMin7, 1 }, { 9, QDom7, 1 }, { 2, QMin7, 1 }, { 7, QDom7, 1 }, { 0, QSixNine, 1 } },
            { { 0, QSixNine, 2 }, { 0, QDim7, 1 }, { 2, QMin7, 1 }, { 7, QDom13, 2 } } };
        static const std::vector<std::vector<P>> mnr {
            { { 0, QMin6, 2 }, { 2, QHalfDim, 1 }, { 7, QDom7b9, 1 } },
            { { 0, QMin7, 1 }, { 5, QMin7, 1 }, { 10, QDom7, 1 }, { 3, QMaj7, 1 }, { 8, QMaj7, 1 }, { 2, QHalfDim, 1 }, { 7, QDom7b9, 1 }, { 0, QMin6, 1 } },
            { { 0, QMinMaj7, 1 }, { 0, QMin7, 1 }, { 0, QMin6, 1 }, { 8, QDom7, 1 } } };
        const auto& set = minorKey ? mnr : maj;
        const auto& pat = set[(size_t) ri ((int) set.size())];
        for (int i = 0; (int) out.size() < o.numChords; ++i)
        {
            const auto& p = pat[(size_t) i % pat.size()];
            add (ch (p.r, p.q, p.b * h));
        }
        for (size_t i = 0; i + 1 < out.size(); ++i)
            if (out[i].qual == QDom7 && pcm (out[i + 1].root - out[i].root) == 5 && u() < o.chroma * 0.7f) out[i].root = (int8_t) pcm (out[i].root + 6);
        colorizeAll();
    }

    void random()
    {
        for (int i = 0; i < o.numChords; ++i)
        {
            if (u() < o.chroma) add (ch (ri (12), ri (2) == 0 ? QMaj7 : (ri (2) ? QMin7 : QDom7)));
            else add (deg (ri (7)));
        }
        colorizeAll();
    }
};
} // namespace

std::vector<Chord> generate (const GenOptions& opt)
{
    GenOptions o = opt;
    o.numChords = juce::jlimit (1, 16, o.numChords);
    Gen g (o);
    switch (o.style)
    {
        case SPop: g.pop(); break;
        case SFunctional: g.functional(); break;
        case SJazz: g.jazz(); break;
        case SModal: g.modal(); break;
        case SCinematic: g.cinematic(); break;
        case STechno: g.techno(); break;
        case SGospel: g.gospel(); break;
        case SBlues: g.blues(); break;
        case SImpression: g.impressionist(); break;
        case SAtonal: g.atonal(); break;
        case SLydian: g.lydian(); break;
        case SNeoSoul: g.neoSoul(); break;
        case SBossa: g.bossa(); break;
        default: g.random(); break;
    }
    if (g.out.empty()) g.add (g.ch (0, QMaj));
    for (auto& c : g.out) { c.root = (int8_t) pcm (c.root); c.beats = (int8_t) juce::jlimit (1, 16, (int) c.beats); }
    return g.out;
}


// ===========================================================================
// Harmonisation d'une ligne melodique
std::vector<Chord> harmonizeLine (const std::vector<std::array<float, 12>>& weights, const std::vector<std::array<float, 12>>& strong,
                                  int sc, float complexity, float tension, int beatsPerChord)
{
    const int nW = (int) weights.size();
    if (nW == 0) return {};
    const int hsc = scaleSize (sc) == 7 ? sc : (inScale (sc, 3) && ! inScale (sc, 4) ? 5 : 0);
    const int size = 3 + (complexity > 0.3f ? 1 : 0);
    // candidats : accords diatoniques (triades ou 7e) + dominantes secondaires (V7/x)
    std::vector<Chord> cand;
    for (int d = 0; d < 7; ++d) cand.push_back (diatonic (hsc, d, size));
    for (int d = 1; d < 7; ++d)
    {
        Chord t = diatonic (hsc, d, 3);
        if (quality (t.qual).family == FDim) continue;
        Chord v; v.root = (int8_t) pcm (t.root + 7); v.qual = QDom7;
        cand.push_back (v);
    }
    const int nC = (int) cand.size();
    const int firstSec = 7;

    auto local = [&] (int w, int ci)
    {
        const Chord& c = cand[(size_t) ci];
        const uint16_t m = chordMask (c);
        float s = 0.0f;
        for (int pc = 0; pc < 12; ++pc)
        {
            const float wt = weights[(size_t) w][(size_t) pc], st = strong[(size_t) w][(size_t) pc];
            if (wt <= 0.0f) continue;
            const int rel = pcm (pc - c.root);
            if (m & bit (pc)) s += wt + 1.5f * st;                                         // note de l'accord
            else if (rel == 2 || rel == 9 || (rel == 5 && quality (c.qual).family == FMin)) s += 0.45f * wt;   // extension disponible (9, 13, 11 sur mineur)
            else if ((m & bit (pc - 1)) && ! (m & bit (pc + 1)) && rel != 2) s -= 1.2f * st + 0.4f * wt; // 9e mineure sur une note de l'accord (note a eviter)
            else s -= 0.6f * st;                                                           // note etrangere sur un temps fort
        }
        if (ci >= firstSec) s -= 0.35f * (1.0f - tension);                                 // dominantes secondaires : plus rares sans tension
        return s;
    };
    auto trans = [&] (int a, int b)
    {
        const Chord& x = cand[(size_t) a]; const Chord& y = cand[(size_t) b];
        const int iv = pcm (y.root - x.root);
        float s = 0.0f;
        if (iv == 5) s += 0.5f;                     // quinte descendante (Persichetti : mouvement le plus fort)
        else if (iv == 2 || iv == 10) s += 0.15f;
        else if (iv == 3 || iv == 4 || iv == 8 || iv == 9) s += 0.1f;
        if (a == b) s -= 0.35f;
        if (a >= firstSec) s += iv == 5 ? 0.8f : -1.5f; // une dominante secondaire doit resoudre
        const int fx = chordFunction (x, hsc), fy = chordFunction (y, hsc);
        if ((fx == 1 && fy == 2) || (fx == 2 && fy == 0)) s += 0.3f;
        return s;
    };

    std::vector<std::vector<float>> score ((size_t) nW, std::vector<float> ((size_t) nC, -1.0e9f));
    std::vector<std::vector<int>> from ((size_t) nW, std::vector<int> ((size_t) nC, -1));
    for (int c = 0; c < nC; ++c) score[0][(size_t) c] = local (0, c) + (c == 0 ? 0.6f : 0.0f);
    for (int w = 1; w < nW; ++w)
        for (int c = 0; c < nC; ++c)
        {
            const float l = local (w, c);
            for (int p = 0; p < nC; ++p)
            {
                const float v = score[(size_t) w - 1][(size_t) p] + trans (p, c) + l;
                if (v > score[(size_t) w][(size_t) c]) { score[(size_t) w][(size_t) c] = v; from[(size_t) w][(size_t) c] = p; }
            }
        }
    int best = 0;
    for (int c = 0; c < nC; ++c)
    {
        const float fin = score[(size_t) nW - 1][(size_t) c] + (c == 0 ? 0.5f : 0.0f);   // finir sur la tonique si possible
        if (fin > score[(size_t) nW - 1][(size_t) best] + (best == 0 ? 0.5f : 0.0f)) best = c;
    }
    std::vector<Chord> out ((size_t) nW);
    for (int w = nW - 1; w >= 0; --w)
    {
        Chord c = cand[(size_t) best];
        c.beats = (int8_t) juce::jlimit (1, 32, beatsPerChord);
        if (quality (c.qual).family == FDom && tension > 0.6f) c.qual = QDom7b9;
        out[(size_t) w] = c;
        best = w > 0 ? from[(size_t) w][(size_t) best] : best;
    }
    // accords identiques consecutifs fusionnes
    std::vector<Chord> merged;
    for (auto& c : out)
    {
        if (! merged.empty() && merged.back().root == c.root && merged.back().qual == c.qual && merged.back().beats + c.beats <= 32)
            merged.back().beats = (int8_t) (merged.back().beats + c.beats);
        else merged.push_back (c);
    }
    return merged;
}

// ===========================================================================
// Reharmonisation
juce::StringArray reharmNames()
{
    return { "Tritone sub", "Secondary dominant", "Insert ii-V", "Approach chord", "Change quality", "Relative sub",
             "Sus sub", "Diminished passing", "Backdoor", "Chromatic mediant", "Constant structure", "Pedal point" };
}

static bool isDom (const Chord& c) { return quality (c.qual).family == FDom; }

static void insertBefore (std::vector<Chord>& p, int i, Chord c)
{
    // prend la moitie de la duree de l'accord precedent (ou de l'accord cible s'il n'y a pas de place)
    const int prev = (i - 1 + (int) p.size()) % (int) p.size();
    Chord& donor = p[(size_t) (p[(size_t) prev].beats >= 2 ? prev : i)];
    if (p.size() >= 16 || donor.beats < 2) { p[(size_t) prev] = c; p[(size_t) prev].beats = donor.beats; return; }
    const int half = donor.beats / 2;
    donor.beats = (int8_t) (donor.beats - half);
    c.beats = (int8_t) half;
    const int at = (&donor == &p[(size_t) prev]) ? prev + 1 : i;
    p.insert (p.begin() + at, c);
}

void reharmonize (std::vector<Chord>& p, int sc, int op, int index, uint32_t seed)
{
    if (p.empty()) return;
    alea::FastRng rng (hash3 (seed, (uint32_t) op, 0xBEA7u) | 1u);
    rng.next();
    const int n = (int) p.size();
    auto pick = [&] (auto pred) -> int
    {
        if (index >= 0 && index < n) return pred (p[(size_t) index]) ? index : -1;
        int c[16]; int k = 0;
        for (int i = 0; i < n; ++i) if (pred (p[(size_t) i])) c[k++] = i;
        return k == 0 ? -1 : c[rng.next() % (uint32_t) k];
    };
    auto any = [] (const Chord&) { return true; };
    switch (op)
    {
        case RTritone: { const int i = pick (isDom); if (i >= 0) p[(size_t) i].root = (int8_t) pcm (p[(size_t) i].root + 6); break; }
        case RSecondary:
        {
            const int i = pick ([] (const Chord& c) { return pcm (c.root) != 0 && ! isDom (c); });
            if (i >= 0) { Chord d; d.root = (int8_t) pcm (p[(size_t) i].root + 7); d.qual = QDom7; insertBefore (p, i, d); }
            break;
        }
        case RTwoFive:
        {
            const int i = pick (isDom);
            if (i >= 0)
            {
                Chord& v = p[(size_t) i];
                Chord ii; ii.root = (int8_t) pcm (v.root + 7); ii.qual = QMin7;
                if (v.beats >= 2 && p.size() < 16) { const int h = v.beats / 2; v.beats = (int8_t) (v.beats - h); ii.beats = (int8_t) h; p.insert (p.begin() + i, ii); }
                else insertBefore (p, i, ii);
            }
            break;
        }
        case RApproach:
        {
            const int i = pick (any);
            if (i >= 0) { Chord a; a.root = (int8_t) pcm (p[(size_t) i].root + 1); a.qual = QDom7; insertBefore (p, i, a); }
            break;
        }
        case RQuality:
        {
            const int i = pick (any);
            if (i < 0) break;
            Chord& c = p[(size_t) i];
            const int f = quality (c.qual).family;
            const uint32_t r = rng.next() % 3u;
            if (f == FMaj) c.qual = (int8_t) (r == 0 ? QMin7 : (r == 1 ? QMaj7s11 : QSixNine));
            else if (f == FMin) c.qual = (int8_t) (r == 0 ? QDom7 : (r == 1 ? QMinMaj7 : QMin11));
            else if (f == FDom) c.qual = (int8_t) (r == 0 ? QSus9 : (r == 1 ? QAlt : QDom13));
            else c.qual = (int8_t) (r == 0 ? QMaj7 : QMin7);
            break;
        }
        case RRelative:
        {
            const int i = pick ([] (const Chord& c) { const int f = quality (c.qual).family; return f == FMaj || f == FMin; });
            if (i < 0) break;
            Chord& c = p[(size_t) i];
            if (quality (c.qual).family == FMaj) { c.root = (int8_t) pcm (c.root + (rng.next() & 1u ? 9 : 4)); c.qual = QMin7; }
            else { c.root = (int8_t) pcm (c.root + 3); c.qual = QMaj7; }
            break;
        }
        case RSus: { const int i = pick (isDom); if (i >= 0) p[(size_t) i].qual = QSus9; break; }
        case RDiminished:
        {
            const int i = pick (any);
            if (i >= 0) { Chord d; d.root = (int8_t) pcm (p[(size_t) i].root - 1); d.qual = QDim7; insertBefore (p, i, d); }
            break;
        }
        case RBackdoor:
        {
            const int i = pick ([] (const Chord& c) { return isDom (c) && pcm (c.root) == 7; });
            if (i >= 0) { p[(size_t) i].root = 10; p[(size_t) i].qual = QDom9; Chord iv; iv.root = 5; iv.qual = QMin7; insertBefore (p, i, iv); }
            break;
        }
        case RMediant:
        {
            const int i = pick ([] (const Chord& c) { return pcm (c.root) != 0; });
            if (i >= 0) p[(size_t) i].root = (int8_t) pcm (p[(size_t) i].root + (rng.next() & 1u ? 4 : 8));
            break;
        }
        case RConstant:
        {
            const int i = pick (any);
            if (i >= 0) { const int q = p[(size_t) i].qual; for (auto& c : p) c.qual = (int8_t) q; }
            break;
        }
        case RPedal: for (auto& c : p) c.bass = 0; break;
        default: break;
    }
    juce::ignoreUnused (sc);
}

void reharmonizeAmount (std::vector<Chord>& p, int sc, float amount, uint32_t seed)
{
    static const int ops[] { RTritone, RSecondary, RTwoFive, RApproach, RQuality, RRelative, RSus, RDiminished, RBackdoor, RMediant };
    const int count = juce::jlimit (0, 12, (int) std::round (amount * (float) p.size()));
    for (int k = 0; k < count; ++k)
        reharmonize (p, sc, ops[hash3 (seed, (uint32_t) k, 0x5EEDu) % 10u], -1, seed + (uint32_t) k * 31u);
}

std::vector<Chord> substitutesFor (const Chord& c, const Chord* next, int sc)
{
    std::vector<Chord> v;
    auto add = [&] (int root, int q) { Chord x = c; x.root = (int8_t) pcm (root); x.qual = (int8_t) q; x.bass = -1; x.inversion = 0;
                                       for (auto& y : v) if (y.root == x.root && y.qual == x.qual) return; if (! (x.root == c.root && x.qual == c.qual)) v.push_back (x); };
    const int f = quality (c.qual).family;
    if (f == FDom) { add (c.root + 6, c.qual); add (c.root, QSus9); add (c.root, QAlt); add (c.root, QDom13); add (c.root, QDom7s11); add (c.root + 4, QDim7); add (c.root + 7, QMin7); }
    if (f == FMaj) { add (c.root, QMaj7); add (c.root, QSixNine); add (c.root, QMaj7s11); add (c.root + 9, QMin7); add (c.root + 4, QMin7); add (c.root, QMin7); add (c.root + 4, QMaj); add (c.root + 8, QMaj); }
    if (f == FMin) { add (c.root, QMin9); add (c.root, QMin6); add (c.root, QMinMaj7); add (c.root, QMin11); add (c.root + 3, QMaj7); add (c.root, QDom7); add (c.root, QSoWhat); }
    if (f == FHalfDim) { add (c.root + 3, QMin6); add (c.root + 8, QDom9); add (c.root, QMin7); }
    if (f == FSus || f == FQuartal) { add (c.root, QDom7); add (c.root + 7, QMin7); add (c.root, QQuartal4); }
    if (next != nullptr)
    {
        add (next->root + 7, QDom7);       // V7 de l'accord suivant
        add (next->root + 1, QDom7);       // approche par le demi-ton superieur (subV)
        add (next->root - 1, QDim7);       // diminue de passage
    }
    add (c.root, QQuartal4);
    juce::ignoreUnused (sc);
    return v;
}

std::vector<std::pair<Chord, float>> suggestNext (const Chord& c, int sc, int style)
{
    std::vector<std::pair<Chord, float>> v;
    const int hsc = scaleSize (sc) == 7 ? sc : 0;
    auto score = [&] (const Chord& x)
    {
        const int iv = pcm (x.root - c.root);
        static const float rootW[12] { 0.1f, 0.35f, 0.5f, 0.55f, 0.55f, 1.0f, 0.3f, 0.6f, 0.55f, 0.55f, 0.5f, 0.35f };   // quinte descendante = +5
        float s = rootW[iv];
        const int f0 = chordFunction (c, sc), f1 = chordFunction (x, sc);
        if ((f0 == 1 && f1 == 2) || (f0 == 2 && f1 == 0) || (f0 == 0 && f1 == 1)) s += 0.5f;
        if (style == SCinematic && (iv == 4 || iv == 8 || iv == 3 || iv == 9)) s += 0.4f;
        if (style == SImpression && (iv == 2 || iv == 10)) s += 0.3f;
        if (style == SNeoSoul && (iv == 1 || iv == 2 || iv == 10 || iv == 11)) s += 0.25f;
        if (style == SBossa && (iv == 5 || iv == 6)) s += 0.25f;
        if (isDom (c) && iv == 5) s += 0.6f;
        if (chordInScale (x, sc)) s += 0.3f;
        return s;
    };
    for (int d = 0; d < 7; ++d) { Chord x = diatonic (hsc, d, 4); v.push_back ({ x, score (x) }); }
    for (int r : { 1, 6, 10, 8, 3 }) { Chord x; x.root = (int8_t) r; x.qual = r == 1 || r == 6 ? QDom7 : QMaj7; v.push_back ({ x, score (x) }); }
    { Chord x; x.root = (int8_t) pcm (c.root + 2); x.qual = QDom7; v.push_back ({ x, score (x) }); }   // V7 du degre suivant (II7 : V/V)
    std::sort (v.begin(), v.end(), [] (auto& a, auto& b) { return a.second > b.second; });
    if (v.size() > 8) v.resize (8);
    return v;
}

Chord liveSubstitute (const Chord& c, const Chord& next, int sc, uint32_t h, float tension)
{
    juce::ignoreUnused (sc);
    Chord x = c;
    const float u = hUni (h, 1u, 0x51u), w = hUni (h, 2u, 0x52u);
    const int f = quality (c.qual).family;
    if (f == FDom)
    {
        const bool resolves = pcm (next.root - c.root) == 5;
        if (resolves && u < 0.4f) x.root = (int8_t) pcm (c.root + 6);                       // triton
        else x.qual = (int8_t) (w < tension ? (w < tension * 0.5f ? QAlt : QDom7b9) : (u < 0.7f ? QDom13 : QSus9));
    }
    else if (f == FMaj) x.qual = (int8_t) (u < 0.35f ? QMaj7s11 : (u < 0.65f ? QSixNine : (w < tension ? QMaj7s5 : QMaj9)));
    else if (f == FMin) x.qual = (int8_t) (u < 0.4f ? QMin11 : (u < 0.7f ? QMin6 : (w < tension ? QSoWhat : QMin9)));
    else if (f == FHalfDim) { if (u < 0.5f) x.qual = QMin7; }
    else if (f == FSus) x.qual = (int8_t) (u < 0.5f ? QQuartal4 : QSus9);
    return x;
}

Chord approachChord (const Chord& target, uint32_t h)
{
    Chord a;
    const float u = hUni (h, 3u, 0xA9u);
    if (u < 0.45f)      { a.root = (int8_t) pcm (target.root + 1); a.qual = QDom7; }   // demi-ton au-dessus (subV)
    else if (u < 0.8f)  { a.root = (int8_t) pcm (target.root + 7); a.qual = QDom7b9; } // dominante
    else                { a.root = (int8_t) pcm (target.root - 1); a.qual = QDim7; }   // diminue au demi-ton inferieur
    a.beats = 1;
    return a;
}

// ===========================================================================
// Voicings
juce::StringArray voicingNames()
{
    return { "Close", "Drop 2", "Drop 2+4", "Spread", "Shell", "Rootless A", "Rootless B", "Quartal", "Upper struct.", "Cluster", "Fifths", "Auto", "Guitar" };
}

namespace
{
struct Tones { int iv[12]; int n = 0; void add (int x) { for (int i = 0; i < n; ++i) if (pcm (iv[i]) == pcm (x)) return; if (n < 12) iv[n++] = x; } bool has (int x) const { for (int i = 0; i < n; ++i) if (pcm (iv[i]) == pcm (x)) return true; return false; }
               bool hasRaw (int x) const { for (int i = 0; i < n; ++i) if (iv[i] == x) return true; return false; }
               void remove (int x) { int k = 0; for (int i = 0; i < n; ++i) if (pcm (iv[i]) != pcm (x)) iv[k++] = iv[i]; n = k; } };

int third (const Chord& c)   // 3ce (ou 4te pour sus / 2de pour sus2)
{
    const auto& q = quality (c.qual);
    for (int k = 0; k < q.n; ++k) { const int x = pcm (q.iv[k]); if (x == 3 || x == 4) return x; }
    for (int k = 0; k < q.n; ++k) { const int x = pcm (q.iv[k]); if (x == 5 || x == 2) return x; }
    return 4;
}
int seventh (const Chord& c)  // 7e (ou 6te)
{
    const auto& q = quality (c.qual);
    for (int k = 0; k < q.n; ++k) { const int x = pcm (q.iv[k]); if (x == 10 || x == 11) return x; }
    for (int k = 0; k < q.n; ++k) { const int x = pcm (q.iv[k]); if (x == 9) return x; }
    return -1;
}

// tons de l'accord + extensions selon complexite / tension (Dobbins : notes disponibles par famille)
void colorTones (const Chord& c, const VoiceOptions& o, Tones& t)
{
    const auto& q = quality (c.qual);
    for (int k = 0; k < q.n; ++k) t.add (q.iv[k]);
    const float cx = o.complexity, tx = o.tension;
    const float r = hUni (o.hash, 9u, 0xE7u);
    switch (q.family)
    {
        case FMaj:
            if (cx > 0.25f) t.add (14);
            if (cx > 0.45f && seventh (c) < 0) t.add (cx > 0.7f ? 9 : 11);
            if (tx > 0.6f || (cx > 0.8f && inScale (o.scale, c.root + 6))) t.add (18);
            break;
        case FMin:
            if (cx > 0.25f) t.add (14);
            if (cx > 0.45f && seventh (c) < 0) t.add (10);
            if (cx > 0.7f) t.add (17);
            break;
        case FDom:
            if (c.qual == QAlt) break;
            if (tx > 0.45f)
            {   // tensions alterees : elles remplacent les tensions naturelles (pas de 9 et b9 ensemble)
                t.remove (14); t.add (r < 0.5f ? 13 : 15);
                if (tx > 0.65f) { t.remove (21); t.add (20); }
                if (tx > 0.8f) t.add (18);
            }
            else { if (cx > 0.3f) t.add (14); if (cx > 0.6f) t.add (21); }
            break;
        case FHalfDim: if (cx > 0.4f) t.add (17); if (cx > 0.75f) t.add (14); break;
        case FDim:     if (cx > 0.5f) t.add (14); break;
        case FSus:     if (cx > 0.3f) t.add (14); if (cx > 0.6f) t.add (21); break;
        default: break;
    }
}

// ordre de priorite des notes (Dobbins : 3ce et 7e d'abord, puis couleurs, puis 5te, puis fondamentale)
int priorityList (const Chord& c, const Tones& t, int* out, bool includeRoot)
{
    int n = 0;
    auto push = [&] (int iv) { for (int i = 0; i < n; ++i) if (pcm (out[i]) == pcm (iv)) return; out[n++] = iv; };
    const int th = third (c), sv = seventh (c);
    if (t.has (th)) push (th);
    if (sv >= 0 && t.has (sv)) push (sv);
    for (int i = 0; i < t.n; ++i) { const int x = pcm (t.iv[i]); if (x != 0 && x != 7 && x != th && x != sv) push (t.iv[i]); }
    for (int i = 0; i < t.n; ++i) if (pcm (t.iv[i]) == 7 || pcm (t.iv[i]) == 6 || pcm (t.iv[i]) == 8) push (t.iv[i]);
    if (includeRoot) push (0);
    for (int i = 0; i < t.n; ++i) push (t.iv[i]);
    return n;
}

float lowIntervalPenalty (const int* v, int n)
{
    // limites d'intervalle grave : sous ~E3 les intervalles serres deviennent boueux (Persichetti / Dobbins)
    float p = 0;
    for (int i = 0; i + 1 < n; ++i)
    {
        const int lo = v[i], d = v[i + 1] - v[i];
        if (lo < 48 && d < 7) p += (float) (7 - d) * 0.6f;
        else if (lo < 55 && d < 3) p += (float) (3 - d) * 0.6f;
    }
    return p;
}

float minorNinthPenalty (const int* v, int n, int rootAbs, bool dom)
{
    float p = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (v[j] - v[i] == 13)
            {
                if (dom && pcm (v[i] - rootAbs) == 0) continue;   // fondamentale / b9 d'une 7b9 : accepte
                p += 2.5f;
            }
    return p;
}

float movement (const int* a, int na, const int* b, int nb)
{
    if (nb == 0) return 0.0f;
    float s = 0;
    for (int i = 0; i < na; ++i)
    {
        int best = 1000;
        for (int j = 0; j < nb; ++j) best = juce::jmin (best, std::abs (a[i] - b[j]));
        s += (float) best;
    }
    // notes communes tenues = bonus
    return s / (float) na;
}
} // namespace

// Voicing de guitare (accordage standard) : une position de 4 cases, cordes graves etouffables, cordes a vide permises.
// Score : basse correcte, 3ce et 7e presentes, nombre de cordes, resonance des cordes a vide, conduite des voix.
static int guitarVoicing (const Chord& c, const VoiceOptions& o, const Tones& t, int rootAbs, const int* prev, int prevN, int* notes, int maxNotes)
{
    static const int strings[6] { 40, 45, 50, 55, 59, 64 };
    uint16_t ct = 0;
    for (int i = 0; i < t.n; ++i) ct |= bit (pcm (rootAbs + t.iv[i]));
    const int bassPc = o.bassMode > 0 ? pcm (o.keyRoot + chordBassPc (c)) : rootAbs;
    const int th3 = pcm (rootAbs + third (c));
    const int s7 = seventh (c);
    const int th7 = s7 >= 0 ? pcm (rootAbs + s7) : -1;
    float prevC = 0.0f; for (int i = 0; i < prevN; ++i) prevC += (float) prev[i]; if (prevN > 0) prevC /= (float) prevN;

    float bestScore = -1.0e9f; int best[6]; int bestN = 0;
    for (int base = 0; base <= 9; ++base)
    {
        int opts[6][6]; int no[6];
        for (int s = 0; s < 6; ++s)
        {
            no[s] = 0;
            if (s < 2) opts[s][no[s]++] = -1;                                   // etouffee
            if (ct & bit (strings[s])) opts[s][no[s]++] = 0;                   // a vide
            for (int f = juce::jmax (1, base); f <= base + 3; ++f)
                if (ct & bit (strings[s] + f)) opts[s][no[s]++] = f;
            if (no[s] == 0) { no[s] = -1; break; }
        }
        bool ok = true; for (int s = 0; s < 6; ++s) if (no[s] < 0) ok = false;
        if (! ok) continue;
        int idx[6] {};
        while (true)
        {
            int fr[6]; for (int s = 0; s < 6; ++s) fr[s] = opts[s][idx[s]];
            if (! (fr[0] >= 0 && fr[1] < 0))                                    // pas de corde grave isolee sous une etouffee
            {
                int v[6]; int n = 0; uint16_t cov = 0; int opens = 0, fmin = 99, fmax = -1;
                for (int s = 0; s < 6; ++s)
                    if (fr[s] >= 0) { v[n++] = strings[s] + fr[s]; cov |= bit (strings[s] + fr[s]); if (fr[s] == 0) ++opens; else { fmin = juce::jmin (fmin, fr[s]); fmax = juce::jmax (fmax, fr[s]); } }
                if (n >= 4)
                {
                    float sc = 0.0f;
                    int low = v[0]; for (int i = 1; i < n; ++i) low = juce::jmin (low, v[i]);
                    sc += pcm (low) == bassPc ? 30.0f : -25.0f;          // la note la plus grave (une corde a vide peut sonner sous une corde frettee)
                    if (! (cov & bit (th3))) sc -= 18.0f;
                    if (th7 >= 0 && ! (cov & bit (th7))) sc -= 14.0f;
                    if (! (cov & bit (rootAbs))) sc -= 6.0f;
                    for (int k = 0; k < 12; ++k) if (cov & bit (k)) sc += 2.0f;
                    sc += (float) n + 0.6f * (float) opens;
                    if (fmax >= 0 && fmax - fmin > 3) sc -= 30.0f;
                    float cen = 0.0f; for (int i = 0; i < n; ++i) cen += (float) v[i]; cen /= (float) n;
                    sc -= 0.15f * std::abs (cen - (o.center - 4.0f));
                    if (prevN > 0) sc -= o.smooth * 0.5f * std::abs (cen - prevC);
                    sc += hUni (o.hash, (uint32_t) (base * 7 + n), 0x6A1u) * 0.5f;
                    if (sc > bestScore) { bestScore = sc; bestN = n; for (int i = 0; i < n; ++i) best[i] = v[i]; }
                }
            }
            int s = 5;
            while (s >= 0 && ++idx[s] >= no[s]) { idx[s] = 0; --s; }
            if (s < 0) break;
        }
    }
    int count = 0;
    for (int i = 0; i < bestN && count < maxNotes; ++i) notes[count++] = best[i];
    std::sort (notes, notes + count);
    return count;
}

int voiceChord (const Chord& c, const VoiceOptions& o, const int* prev, int prevN, int* notes, int maxNotes)
{
    Tones t; colorTones (c, o, t);
    const int rootAbs = pcm (o.keyRoot + c.root);
    const bool dom = quality (c.qual).family == FDom;
    const int nv = juce::jlimit (2, 7, o.voices);
    const int fam = quality (c.qual).family;
    auto isAvoid = [&] (int rel)   // notes a eviter (Dobbins) : 11te juste sur majeur/dominante, b9 hors dominante
    {
        if (t.has (rel)) return false;
        if ((fam == FMaj || fam == FDom) && rel == 5) return true;
        if (fam != FDom && fam != FDim && rel == 1) return true;
        return false;
    };

    int voicing = o.voicing;
    if (voicing == VGuitar) return guitarVoicing (c, o, t, rootAbs, prev, prevN, notes, maxNotes);
    if (voicing == VAuto)
    {
        const int f = quality (c.qual).family;
        voicing = f == FQuartal ? VQuartal : (f == FDom && o.tension > 0.45f) ? VUpper : (t.n <= 3 ? VSpread : VDrop2);
        if (o.complexity > 0.75f && f == FMin) voicing = VQuartal;
    }

    // ensemble de classes de hauteur a placer (relatives a la fondamentale)
    int pri[16]; const bool needRoot = o.bassMode == 0 || voicing == VSpread || voicing == VFifths || voicing == VClose || t.n <= 3;
    int np = priorityList (c, t, pri, needRoot);
    int set[8]; int ns = 0;
    switch (voicing)
    {
        case VShell:
        {
            set[ns++] = third (c);
            const int sv = seventh (c); if (sv >= 0) set[ns++] = sv; else set[ns++] = 7;
            if (nv >= 3) for (int i = 0; i < np && ns < 3; ++i) if (pcm (pri[i]) != pcm (set[0]) && pcm (pri[i]) != pcm (set[1])) set[ns++] = pri[i];
            break;
        }
        case VRootlessA: case VRootlessB:
        {
            const int sv = seventh (c) >= 0 ? seventh (c) : 9;
            const bool dm = quality (c.qual).family == FDom;
            const int ninth = dm && t.hasRaw (13) ? 13 : (dm && t.hasRaw (15) ? 15 : 14);
            const int fifthish = t.hasRaw (21) ? 21 : (t.hasRaw (20) ? 20 : (t.hasRaw (6) ? 6 : (t.hasRaw (8) ? 8 : 7)));
            if (voicing == VRootlessA) { set[ns++] = third (c); set[ns++] = fifthish; set[ns++] = sv; set[ns++] = ninth; }
            else { set[ns++] = sv; set[ns++] = ninth; set[ns++] = third (c) + 12; set[ns++] = fifthish + 12; }
            break;
        }
        case VFifths: set[ns++] = 0; set[ns++] = 7; set[ns++] = 14; if (nv >= 4) set[ns++] = 21; if (nv >= 5) set[ns++] = 12 + third (c); break;
        default:
            for (int i = 0; i < np && ns < nv; ++i) set[ns++] = pri[i];
            if (ns < nv && ns > 0 && voicing != VCluster) { set[ns] = set[0] + 12; ++ns; }   // doublure seulement si necessaire
            break;
    }

    if (voicing == VClose || voicing == VDrop2 || voicing == VDrop24)
    {   // position serree : notes classees dans l'octave, les renversements font le reste
        for (int i = 0; i < ns; ++i) set[i] = pcm (set[i]);
        std::sort (set, set + ns);
        ns = (int) (std::unique (set, set + ns) - set);
    }
    const bool ordered = voicing == VShell || voicing == VRootlessA || voicing == VRootlessB || voicing == VFifths;
    // candidats : renversements x octaves (et variantes propres a chaque type)
    int best[8]; int bestN = 0; float bestCost = 1.0e9f;
    const float center = o.center;
    const int spreadExtra = (int) std::round (o.spread * 7.0f);
    auto consider = [&] (int* v, int n)
    {
        std::sort (v, v + n);
        for (int i = 0; i < n; ++i) if (v[i] < 28 || v[i] > 100) return;
        float centroid = 0; for (int i = 0; i < n; ++i) centroid += (float) v[i]; centroid /= (float) n;
        const float span = (float) (v[n - 1] - v[0]);
        float cost = (0.25f + (1.0f - o.smooth) * 0.5f) * std::abs (centroid - center) + o.smooth * movement (v, n, prev, prevN) * 1.2f;
        cost += std::abs (centroid - center) > 7.0f ? (std::abs (centroid - center) - 7.0f) : 0.0f;
        for (int i = 0; i < n; ++i) if (isAvoid (pcm (v[i] - rootAbs))) cost += 3.0f;
        cost += lowIntervalPenalty (v, n) + minorNinthPenalty (v, n, rootAbs, dom);
        cost += std::abs (span - (10.0f + o.spread * 14.0f)) * 0.08f;
        cost += hUni (o.hash, (uint32_t) (v[0] * 7 + n), 0x77u) * 0.15f;   // brise les egalites (reproductible)
        if (cost < bestCost) { bestCost = cost; bestN = n; for (int i = 0; i < n; ++i) best[i] = v[i]; }
    };

    const int baseC = (int) std::round (center) - 12;
    for (int oct = -2; oct <= 2; ++oct)
    {
        const int anchor = 12 * ((baseC + oct * 12) / 12) + rootAbs;
        if (voicing == VQuartal)
        {
            // pile de quartes depuis chaque note de l'accord (quartes diatoniques : juste ou augmentee selon la gamme)
            for (int s = 0; s < t.n; ++s)
            {
                int v[8]; int p = anchor + t.iv[s] % 12;
                const int count = juce::jlimit (3, 6, nv);
                for (int k = 0; k < count; ++k)
                {
                    v[k] = p;
                    const int nxt = p + 5;
                    p = (inScale (o.scale, pcm (nxt - o.keyRoot)) || ! inScale (o.scale, pcm (nxt + 1 - o.keyRoot))) ? nxt : nxt + 1;
                }
                if (c.qual == QSoWhat || nv >= 5) v[count - 1] = v[count - 2] + 4;   // "So What" : quartes + tierce majeure en haut
                consider (v, count);
            }
            continue;
        }
        if (voicing == VUpper)
        {
            // shell (3ce + 7e) dans le medium + triade de tensions au-dessus (upper structure)
            const int sv = seventh (c) >= 0 ? seventh (c) : 9;
            static const int triadsDom[3][3] { { 14, 18, 21 }, { 20, 24, 27 }, { 15, 20, 24 } };   // II, bVI, bIII (9 #11 13 / b13 1 b9 / #9 b13 1)
            static const int triadsMaj[2][3] { { 14, 18, 21 }, { 7, 11, 14 } };
            static const int triadsMin[2][3] { { 10, 14, 17 }, { 3, 7, 10 } };
            const int f = quality (c.qual).family;
            const int (*tri)[3] = f == FDom ? triadsDom : (f == FMin ? triadsMin : triadsMaj);
            const int nt = f == FDom ? (o.tension > 0.6f ? 3 : 1) : 2;
            for (int k = 0; k < nt; ++k)
            {
                int v[8]; int n = 0;
                v[n++] = anchor + third (c); v[n++] = anchor + sv;
                if (v[1] < v[0]) v[1] += 12;
                for (int j = 0; j < 3; ++j) v[n++] = anchor + tri[k][j] + 12;
                consider (v, n);
            }
            continue;
        }
        if (voicing == VSpread)
        {
            // espacement facon serie harmonique : grands intervalles en bas, serres en haut
            int v[8]; int n = 0; int last = anchor - 12;
            for (int i = 0; i < ns; ++i)
            {
                const int minGap = juce::jmax (2, 7 - i * 2) + (i == 0 ? 0 : spreadExtra / 3);
                int p = 12 * ((last - pcm (rootAbs + set[i])) / 12) + pcm (rootAbs + set[i]);
                while (p < last + (n == 0 ? 0 : minGap)) p += 12;
                v[n++] = p; last = p;
            }
            consider (v, n);
            continue;
        }
        if (voicing == VCluster)
        {
            // clusters : notes de l'accord + notes de gamme voisines, serrees en secondes
            for (int s0 = 0; s0 < t.n; ++s0)
            {
                int v[8]; int n = 0;
                int p = anchor + pcm (t.iv[s0]);
                for (int k = 0; n < juce::jmin (nv, 6) && k < 12; ++k, ++p)
                    if ((inScale (o.scale, pcm (p - o.keyRoot)) && ! isAvoid (pcm (p - rootAbs))) || t.has (pcm (p - rootAbs))) v[n++] = p;
                if (n >= 2) consider (v, n);
            }
            continue;
        }
        // position serree / drop 2 / drop 2+4 / shell / rootless / quintes : chaque renversement
        for (int rot = 0; rot < (ordered ? 1 : ns); ++rot)
        {
            int v[8];
            int last = -1000;
            for (int i = 0; i < ns; ++i)
            {
                const int iv = set[(rot + i) % ns];
                int p = anchor + pcm (iv);
                if (voicing == VRootlessB || voicing == VFifths || voicing == VShell) p = anchor + iv;   // l'ordre des notes est voulu
                while (p <= last) p += 12;
                v[i] = p; last = p;
            }
            int n = ns;
            if (voicing == VDrop2 && n >= 4) { std::sort (v, v + n); v[n - 2] -= 12; }
            if (voicing == VDrop24 && n >= 4) { std::sort (v, v + n); v[n - 2] -= 12; v[n - 4] -= 12; }
            if (o.spread > 0.65f && n >= 3) { std::sort (v, v + n); v[0] -= 12; }
            consider (v, n);
        }
    }

    int count = 0;
    // basse
    if (o.bassMode > 0)
    {
        const int bpc = o.bassMode == 2 ? pcm (o.keyRoot) : pcm (o.keyRoot + chordBassPc (c));
        int b = 12 * (o.bassOctave + 1) + bpc;
        const int lowest = bestN > 0 ? best[0] : 60;
        while (b > lowest - 5) b -= 12;
        if (b < 24) b += 12;
        if (count < maxNotes) notes[count++] = b;
    }
    for (int i = 0; i < bestN && count < maxNotes; ++i) notes[count++] = best[i];
    std::sort (notes + (o.bassMode > 0 ? 1 : 0), notes + count);
    return count;
}

int chordScaleNotes (const Chord& c, const VoiceOptions& o, float outgoing, int lo, int hi, int* out, int maxN)
{
    int sc; int root;
    if (outgoing < 0.35f && chordInScale (c, o.scale)) { sc = o.scale; root = 0; }
    else { sc = parentScale (c, outgoing); root = parentScaleRoot (c, outgoing); }
    const uint16_t m = smask (sc);
    int n = 0;
    for (int p = lo; p <= hi && n < maxN; ++p)
        if (m & bit (p - o.keyRoot - root)) out[n++] = p;
    return n;
}

Chord harmonizeMelody (int mel, const Chord& cur, int sc, uint32_t h)
{
    mel = pcm (mel);
    // 1) note de l'accord ou extension disponible : on garde l'accord
    Tones t; VoiceOptions vo; vo.complexity = 0.8f; vo.tension = 0.2f; vo.scale = sc; colorTones (cur, vo, t);
    for (int i = 0; i < t.n; ++i) if (pcm (cur.root + t.iv[i]) == mel) return cur;
    // 2) accord diatonique de 7e qui contient la note, au plus pres (en quintes) de l'accord courant
    const int hsc = scaleSize (sc) == 7 ? sc : 0;
    Chord best = cur; int bestD = 100;
    for (int d = 0; d < 7; ++d)
    {
        Chord x = diatonic (hsc, d, 4);
        if (! (chordMask (x) & bit (mel))) continue;
        const int dist = lcRank (x.root - cur.root) + (int) (hUni (h, (uint32_t) d, 0x4Du) * 2.0f);
        if (dist < bestD) { bestD = dist; best = x; }
    }
    if (bestD < 100) { best.beats = cur.beats; return best; }
    // 3) note alteree : dominante dont la note est la b9 / #11 (LaVerne)
    Chord d; d.root = (int8_t) pcm (mel - 1); d.qual = QDom7b9; d.beats = cur.beats;
    return d;
}

uint16_t chordScaleMask (const Chord& c, int scaleIdx, float outgoing)
{
    int sc, root;
    if (outgoing < 0.35f && chordInScale (c, scaleIdx)) { sc = scaleIdx; root = 0; }
    else { sc = parentScale (c, outgoing); root = parentScaleRoot (c, outgoing); }
    uint16_t m = 0;
    for (int d = 0; d < scaleSize (sc); ++d) m |= bit (root + scaleDegreePc (sc, d));
    return (uint16_t) (m | chordMask (c));
}

std::vector<Chord> modulationChords (int scaleIdx, int semis, int method, int beats)
{
    std::vector<Chord> out;
    const int t = pcm (semis);                                   // nouvelle tonique (relative a l'ancienne)
    auto mk = [beats] (int root, int q) { Chord c; c.root = (int8_t) pcm (root); c.qual = (int8_t) q; c.beats = (int8_t) beats; return c; };
    switch (method)
    {
        case 0: out.push_back (mk (t + 7, QDom7)); break;                                         // V7 de la nouvelle tonalite
        case 1: out.push_back (mk (t + 2, QMin7)); out.push_back (mk (t + 7, QDom7)); break;      // ii - V
        case 2:
        {
            // accord pivot : accord diatonique commun aux deux tonalites (le plus proche de la sous-dominante de la nouvelle)
            const int hs = scaleSize (scaleIdx) == 7 ? scaleIdx : 0;
            Chord pivot = mk (t + 5, QMaj); bool found = false;
            for (int pref : { 5, 2, 9, 4, 0 })
                for (int d = 0; d < 7 && ! found; ++d)
                {
                    Chord x = diatonic (hs, d, 3);
                    if (pcm (x.root - t) != pref) continue;
                    // la meme triade existe-t-elle dans la nouvelle tonalite (majeure) ?
                    const uint16_t m = chordMask (x);
                    bool inNew = true;
                    for (int k = 0; k < 12; ++k) if ((m & bit (k)) && ! inScale (0, pcm (k - t))) inNew = false;
                    if (inNew) { pivot = x; pivot.beats = (int8_t) beats; found = true; }
                }
            out.push_back (pivot);
            out.push_back (mk (t + 7, QDom7));
            break;
        }
        default: break;                                                                           // directe
    }
    return out;
}

} // namespace color::theory
