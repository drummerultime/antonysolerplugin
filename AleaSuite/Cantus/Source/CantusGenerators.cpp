// Generateurs de TALEA : sons (de, mutation, favoris, croisement, scenes, morph, evolution),
// lignes de basse (styles, mutations, transformations, Markov, style MIDI), export, presets.
#include "CantusProcessor.h"

using namespace cantus;

static juce::Random makeRandom (int64_t seed)
{
    if (seed < 0) return juce::Random();
    const uint64_t hi = hash3 ((uint32_t) seed, (uint32_t) (seed >> 32), 0x7A1Eu);
    const uint64_t lo = hash3 ((uint32_t) seed ^ 0x5bd1e995u, 0x7654321u, (uint32_t) (seed >> 32));
    juce::Random r ((juce::int64) ((hi << 32) | lo));
    for (int i = 0; i < 4; ++i) r.nextInt();
    return r;
}

// ---------------------------------------------------------------------------
// Son
void CantusAudioProcessor::diceSound (bool includeTypes, int64_t seed)
{
    auto r = makeRandom (seed);
    if (includeTypes && ! locks[kNumTP])
    {
        setParamReal ("osc1type", (float) r.nextInt (osc1Names().size()));
        setParamReal ("osc2type", (float) (r.nextFloat() < 0.5f ? 0 : r.nextInt (osc2Names().size())));
        setParamReal ("ftype", (float) (r.nextFloat() < 0.7f ? r.nextInt (2) : r.nextInt (5)));
        setParamReal ("drivetype", (float) r.nextInt (driveNames().size()));
        setParamReal ("subtype", (float) r.nextInt (subNames().size()));
        setParamReal ("chord", (float) (r.nextFloat() < 0.85f ? 0 : 1 + r.nextInt (7)));
    }
    for (int i = 0; i < kNumTP; ++i)
    {
        if (locks[i]) continue;
        float lo, hi;
        tpDiceRange (i, lo, hi);
        float v = lo + r.nextFloat() * (hi - lo);
        if ((i == FM || i == NoiseLvl || i == O2Level) && r.nextFloat() < 0.5f) v = 0.0f;
        if (i == O2Semi && r.nextFloat() < 0.6f) v = r.nextBool() ? 0.5f : 0.75f;     // unisson ou octave
        setParamNorm (tpId (i), v);
    }
}

void CantusAudioProcessor::mutateSound (float amount)
{
    juce::Random r;
    for (int i = 0; i < kNumTP; ++i)
    {
        if (locks[i] || i == Level) continue;
        setParamNorm (tpId (i), tpRaw[i]->load() + (r.nextFloat() * 2.0f - 1.0f) * amount);
    }
}

void CantusAudioProcessor::randomizeJitter (int64_t seed)
{
    auto r = makeRandom (seed);
    for (int i = 0; i < kNumTP; ++i)
    {
        if (locks[i]) continue;
        const bool musical = i == Cutoff || i == EnvMod || i == FDecay || i == AccentAmt || i == Reso || i == Drive || i == Shape;
        const float v = (! musical && r.nextFloat() < 0.7f) ? 0.0f : r.nextFloat() * 0.3f;
        setParamNorm (tpJit (i), v);
    }
}

void CantusAudioProcessor::clearJitter()
{
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpJit (i), 0.0f);
}

void CantusAudioProcessor::storeB()
{
    for (int i = 0; i < kNumTP; ++i) snapB[i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) snapBTypes[i] = (int) typeRaw[i]->load();
    hasB = true;
}

void CantusAudioProcessor::recallB()
{
    if (! hasB) return;
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpId (i), snapB[i].load());
    for (int i = 0; i < kNumTypes; ++i) setParamReal (typeParamId (i), (float) snapBTypes[i].load());
}

void CantusAudioProcessor::swapB()
{
    if (! hasB) { storeB(); return; }
    float a[kNumTP]; int at[kNumTypes];
    for (int i = 0; i < kNumTP; ++i) a[i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) at[i] = (int) typeRaw[i]->load();
    recallB();
    for (int i = 0; i < kNumTP; ++i) snapB[i] = a[i];
    for (int i = 0; i < kNumTypes; ++i) snapBTypes[i] = at[i];
}

void CantusAudioProcessor::storeScene (int n)
{
    n = juce::jlimit (0, kScenes - 1, n);
    for (int i = 0; i < kNumTP; ++i) sceneVal[n][i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) sceneTypes[n][i] = (int) typeRaw[i]->load();
    sceneValid[n] = true;
}

void CantusAudioProcessor::recallScene (int n)
{
    n = juce::jlimit (0, kScenes - 1, n);
    if (! sceneValid[n].load()) return;
    for (int i = 0; i < kNumTypes; ++i) setParamReal (typeParamId (i), (float) sceneTypes[n][i].load());
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpId (i), sceneVal[n][i].load());
    lastRecalledScene = n;
}

void CantusAudioProcessor::keepLastNote()
{
    for (int i = 0; i < kNumTP; ++i)
    {
        const float v = lastNorm[i].load();
        if (v >= 0.0f) setParamNorm (tpId (i), v);
        evoOff[i] = 0.0f;
    }
}

void CantusAudioProcessor::storeFav (int slot)
{
    auto& f = favs[juce::jlimit (0, kFavs - 1, slot)];
    for (int i = 0; i < kNumTP; ++i) f.v[i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) f.types[i] = (int) typeRaw[i]->load();
    f.valid = true;
}

void CantusAudioProcessor::recallFav (int slot)
{
    const auto& f = favs[juce::jlimit (0, kFavs - 1, slot)];
    if (! f.valid) return;
    for (int i = 0; i < kNumTypes; ++i) setParamReal (typeParamId (i), (float) f.types[i]);
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpId (i), f.v[i]);
    guiTrig = true;
}

void CantusAudioProcessor::breedVoice (int slotA, int slotB, int64_t seed)
{
    const auto& a = favs[juce::jlimit (0, kFavs - 1, slotA)];
    const auto& b = favs[juce::jlimit (0, kFavs - 1, slotB)];
    if (! a.valid || ! b.valid) return;
    auto r = makeRandom (seed);
    for (int i = 0; i < kNumTypes; ++i) setParamReal (typeParamId (i), (float) (r.nextBool() ? a.types[i] : b.types[i]));
    for (int i = 0; i < kNumTP; ++i)
    {
        if (locks[i]) continue;
        const bool fromA = r.nextBool();
        const float p = fromA ? a.v[i] : b.v[i], q = fromA ? b.v[i] : a.v[i];
        float v = p + (q - p) * r.nextFloat() * 0.35f;
        if (r.nextFloat() < 0.15f) v += (r.nextFloat() * 2.0f - 1.0f) * 0.08f;
        setParamNorm (tpId (i), v);
    }
    guiTrig = true;
}

// ---------------------------------------------------------------------------
// Evolution
void CantusAudioProcessor::resetEvolution()
{
    for (int p = 0; p < kPatterns; ++p) { evoMask[p] = 0u; for (auto& e : evoNote[p]) e = 0; }
    for (auto& v : evoOff) v = 0.0f;
}

void CantusAudioProcessor::commitEvolution()
{
    const int sc = juce::jmax (1, (int) scaleRaw->load());
    for (int p = 0; p < kPatterns; ++p)
    {
        const uint32_t m = evoMask[p].exchange (0u);
        for (int i = 0; i < kSteps; ++i)
        {
            auto& s = bank.pats[p].steps[i];
            if ((m >> i) & 1u) s.on = (int8_t) (s.on ? 0 : 1);
            const int d = evoNote[p][i].exchange (0);
            if (d != 0)
            {
                const float v = scaleShift ((float) s.note + 12.0f * s.oct, sc, d);
                int oct = (int) std::floor (v / 12.0f);
                int note = (int) v - oct * 12;
                if (oct > 0 && note + 12 <= 24) { note += 12; --oct; }
                s.note = (int8_t) juce::jlimit (0, 24, note);
                s.oct = (int8_t) juce::jlimit (-2, 2, oct);
            }
        }
    }
    for (int i = 0; i < kNumTP; ++i)
    {
        const float o = evoOff[i].exchange (0.0f);
        if (o != 0.0f) setParamNorm (tpId (i), tpRaw[i]->load() + o);
    }
}

int CantusAudioProcessor::evolutionAmount() const
{
    int n = 0;
    for (int p = 0; p < kPatterns; ++p)
    {
        uint32_t v = evoMask[p].load();
        while (v) { n += (int) (v & 1u); v >>= 1; }
        for (auto& e : evoNote[p]) if (e.load() != 0) ++n;
    }
    for (auto& v : evoOff) if (std::abs (v.load()) > 0.01f) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Lignes de basse
namespace
{
struct GenOpts { float dens, range, acc, slide, oct, rootW; int scale; };

int pickNote (juce::Random& r, const GenOpts& o)
{
    if (r.nextFloat() < o.rootW * 0.6f) return r.nextFloat() < 0.75f ? 0 : 12;
    const int degPerOct = (int) alea::scaleDegrees (o.scale).size();
    const int maxDeg = juce::jmax (1, (int) std::round (degPerOct * (0.6f + o.range * 1.4f)));
    const int d = r.nextInt (maxDeg + 1);
    return juce::jlimit (0, 24, (int) scaleShift (0.0f, o.scale, d));
}

uint32_t euclidMask (int k, int n, int rot)
{
    uint32_t m = 0;
    for (int i = 0; i < n; ++i)
    {
        const int j = ((i + rot) % n + n) % n;
        if (k > 0 && (j * k) % n < k) m |= 1u << i;
    }
    return m;
}
} // namespace

// ===========================================================================
// MELODIE (Cantus) : regles tirees des ouvrages lus (Dobbins : notes de l'accord sur les temps forts, notes de
// passage / broderies sur les temps faibles, lignes bebop chromatiques ; Persichetti : contour, motif et
// sequence, saut compense par un mouvement conjoint contraire ; Russell : gamme parente de chaque accord).
namespace
{
struct HChord { int root, bass, mask, scale; double start, end; };

inline int pcOf (int m) { return ((m % 12) + 12) % 12; }
inline bool inM (int m, int mask) { return ((mask >> pcOf (m)) & 1) != 0; }

int stepIn (int p, int dir, int mask)
{
    int q = p;
    for (int k = 0; k < 12; ++k) { q += dir; if (inM (q, mask)) return q; }
    return p + dir;
}

int nearestIn (int p, int mask, int target, float pull)
{
    int best = p; float bs = 1.0e9f;
    for (int d = -9; d <= 9; ++d)
    {
        const int q = p + d;
        if (! inM (q, mask)) continue;
        const float sc = (float) std::abs (d) + pull * (float) std::abs (q - target);
        if (sc < bs) { bs = sc; best = q; }
    }
    return best;
}

// pentatonique de l'accord : notes de la gamme sauf celles a un demi-ton d'une note de l'accord
int pentaOf (const HChord& c)
{
    int m = c.scale;
    for (int i = 0; i < 12; ++i)
        if (((m >> i) & 1) && ! ((c.mask >> i) & 1) && (((c.mask >> ((i + 1) % 12)) & 1) || ((c.mask >> ((i + 11) % 12)) & 1))) m &= ~(1 << i);
    return m | c.mask;
}
} // namespace

void CantusAudioProcessor::readHarmony (std::vector<HarmonySpan>& out, int& totalBeats) const
{
    out.clear();
    alea::link::ProgChord prog[alea::link::kMaxProg];
    double start = 0.0;
    int n = 0;
    if (link != nullptr && chordFollowRaw->load() > 0.5f)
    {
        const int slot = link->firstPeerOfKind (alea::link::KindColor);
        if (slot >= 0) n = link->readProgression (slot, prog, alea::link::kMaxProg, start);
    }
    totalBeats = 0;
    if (n <= 0)
    {
        // sans Color : accord de tonique et gamme de Cantus (4 mesures)
        const int root = (int) rootRaw->load();
        const int sc = juce::jmax (1, (int) scaleRaw->load());
        int scale = 0;
        for (int i = 0; i < 12; ++i) if (std::abs (quantizeSemis ((float) i, sc) - (float) i) < 0.01f) scale |= 1 << ((root + i) % 12);
        const bool minor = (scale >> ((root + 3) % 12)) & 1;
        const int mask = (1 << root) | (1 << ((root + (minor ? 3 : 4)) % 12)) | (1 << ((root + 7) % 12));
        out.push_back ({ root, root, mask, scale, 16 });
        totalBeats = 16;
        return;
    }
    for (int i = 0; i < n; ++i) { out.push_back ({ prog[i].root, prog[i].bass, prog[i].mask, prog[i].scale ? prog[i].scale : prog[i].mask, prog[i].beats }); totalBeats += prog[i].beats; }
}

void CantusAudioProcessor::generateMelodyInto (BPattern& P, int style, int64_t seed, const BPattern* develop)
{
    auto r = makeRandom (seed);
    std::vector<HarmonySpan> hs; int totalBeats = 16;
    readHarmony (hs, totalBeats);
    std::vector<HChord> ch;
    { double acc = 0; for (auto& h : hs) { ch.push_back ({ h.root, h.bass, h.mask, h.scale, acc, acc + h.beats }); acc += h.beats; } }
    auto chordAt = [&ch] (double b) -> const HChord& { for (auto& c : ch) if (b < c.end - 1.0e-9) return c; return ch.back(); };

    const float dens = paramReal ("gendens"), range = paramReal ("genrange"), accP = paramReal ("genacc"), slideP = paramReal ("genslide"),
                leapP = paramReal ("genoct"), dev = paramReal ("improvdev");
    const int base = 24 + 12 * (int) baseOctRaw->load() + (int) rootRaw->load();
    const int lo = base - 3, hi = base + 21;
    const int center = base + 7;

    // developpement d'une phrase existante (improvisation) : meme rythme, hauteurs variees
    if (develop != nullptr && r.nextFloat() > dev)
    {
        P.copyFrom (*develop);
        const int sc = juce::jmax (1, (int) scaleRaw->load());
        const int shift = r.nextFloat() < 0.5f ? 0 : (r.nextBool() ? 1 : -1);    // sequence d'un degre
        for (int i = 0; i < P.length; ++i)
        {
            auto& s = P.steps[i];
            if (! s.on || s.tie) continue;
            int d = shift;
            if (r.nextFloat() < 0.15f + dev * 0.5f) d += r.nextBool() ? 1 : -1;
            if (d != 0)
            {
                const float v = scaleShift ((float) s.note + 12.0f * s.oct, sc, d);
                int oct = (int) std::floor (v / 12.0f), note = (int) v - oct * 12;
                if (oct > 0 && note + 12 <= 24) { note += 12; --oct; }
                s.note = (int8_t) juce::jlimit (0, 24, note); s.oct = (int8_t) juce::jlimit (-2, 2, oct);
            }
        }
        return;
    }

    P.clear();
    P.dir = 0; P.groove = 0;
    const int rate = totalBeats <= 8 ? 1 : 0;
    P.rate = (int8_t) rate;
    const double stepLen = ratePpq (rate);
    const int spb = (int) std::round (1.0 / stepLen);                 // pas par temps
    const int len = juce::jlimit (1, kSteps, (int) std::round (totalBeats / stepLen));
    P.length = (int8_t) len;
    const int bar = 4 * spb;

    // 1) rythme : une cellule par mesure (repetee pour Motif / Riff), durees en pas
    struct Ev { int step, dur; };
    std::vector<Ev> evs;
    auto makeCell = [&] (std::vector<Ev>& cell)
    {
        cell.clear();
        int t = 0;
        auto add = [&] (int d, bool rest) { d = juce::jmax (1, juce::jmin (d, bar - t)); if (! rest) cell.push_back ({ t, d }); t += d; };
        while (t < bar)
        {
            const bool rest = r.nextFloat() > 0.35f + dens * 0.6f;
            switch (style)
            {
                case 0: add (spb * (r.nextFloat() < 0.5f ? 1 : (r.nextFloat() < 0.6f ? 2 : 3)) / (r.nextFloat() < 0.25f ? 2 : 1), rest && t > 0); break;   // lyrique
                case 1: add (t < bar / 2 ? juce::jmax (1, spb / 2) : spb * 2, rest && t >= bar / 2); break;                                         // motif
                case 2: add (juce::jmax (1, spb / 2), false); break;                                                                                     // arpege
                case 3: add (juce::jmax (1, spb / 2) * (1 + r.nextInt (3)), rest); break;                                                               // pentatonique
                case 4: add (juce::jmax (1, spb / 2), rest && r.nextFloat() < 0.3f); break;                                                             // bebop
                case 5: add (juce::jmax (1, spb / 2) * (r.nextFloat() < 0.6f ? 1 : 2), rest); break;                                                    // riff
                case 6: add (spb * (2 + r.nextInt (3)), r.nextFloat() < 0.5f); break;                                                                   // epure
                case 7: add (spb, rest); break;                                                                                                          // grands sauts
                default: add (1 + r.nextInt (spb * 2), rest); break;
            }
        }
        if (cell.empty()) cell.push_back ({ 0, spb });
    };
    std::vector<Ev> cell;
    makeCell (cell);
    for (int b0 = 0; b0 < len; b0 += bar)
    {
        const bool repeatCell = style == 1 || style == 5 || (b0 / bar) % 2 == 0;
        if (! repeatCell && b0 > 0) makeCell (cell);
        for (auto& e : cell) if (b0 + e.step < len) evs.push_back ({ b0 + e.step, juce::jmin (e.dur, len - (b0 + e.step)) });
        if (style != 1 && style != 5 && b0 > 0 && r.nextFloat() < 0.3f) makeCell (cell);
    }

    // 2) hauteurs : contour en arche, temps forts sur l'accord, temps faibles conjoints, sauts compenses
    int p = nearestIn (center, chordAt (0.0).mask, center, 0.3f);
    int lastIv = 0, arpDir = 1;
    std::vector<int> motif;           // intervalles du motif (style Motif)
    for (size_t k = 0; k < evs.size(); ++k)
    {
        const auto& e = evs[k];
        const double b = e.step * stepLen;
        const auto& c = chordAt (b);
        const double phase = b / juce::jmax (1.0, (double) totalBeats);
        const int target = center + (int) std::round ((3.0f + 6.0f * range) * std::sin (juce::MathConstants<double>::pi * phase)) - 2;
        const bool strong = std::fmod (b, 1.0) < 1.0e-6 && (std::fmod (b, 2.0) < 1.0e-6 || e.dur >= spb);
        const bool last = k + 1 == evs.size();
        const int scaleM = style == 3 ? pentaOf ({ c.root, c.bass, c.mask, c.scale, 0, 0 }) : c.scale;
        int q = p;
        if (last)
            q = nearestIn (p, (1 << c.root) | (1 << ((c.root + 7) % 12)), center, 0.2f);                       // cadence : fondamentale / quinte
        else if (style == 1 && (int) motif.size() >= 3 && e.step >= bar)
        {
            // sequence du motif (Persichetti) : memes intervalles, recales sur l'accord
            const int mi = (int) ((k) % motif.size());
            q = p + motif[(size_t) mi];
            if (strong) q = nearestIn (q, c.mask, target, 0.1f); else if (! inM (q, scaleM)) q = stepIn (q, motif[(size_t) mi] >= 0 ? 1 : -1, scaleM);
        }
        else if (style == 2)
        {
            q = stepIn (p, arpDir, c.mask);
            if (q > hi || q > target + 7) { arpDir = -1; q = stepIn (p, -1, c.mask); }
            if (q < lo || q < target - 7) { arpDir = 1; q = stepIn (p, 1, c.mask); }
        }
        else if (std::abs (lastIv) > 4)
            q = stepIn (p, lastIv > 0 ? -1 : 1, scaleM);                                                             // saut compense
        else if ((style == 7 || r.nextFloat() < leapP * 0.35f) && strong)
        {
            const int dir = p < target ? 1 : -1;
            q = nearestIn (p + dir * (7 + r.nextInt (6)), c.mask, target, 0.1f);                                      // grand saut vers une note de l'accord
        }
        else if (strong)
            q = nearestIn (p + (target > p ? 2 : (target < p ? -2 : 0)), c.mask, target, 0.25f);
        else if (style == 4 && r.nextFloat() < 0.45f)
            q = p + (target >= p ? 1 : -1);                                                                          // chromatisme bebop
        else
        {
            const int dir = r.nextFloat() < 0.25f ? (r.nextBool() ? 1 : -1) : (target >= p ? 1 : -1);
            q = stepIn (p, dir, scaleM);
            if (r.nextFloat() < 0.15f) q = stepIn (q, dir, scaleM);                                                  // tierce
        }
        while (q > hi) q -= 12;
        while (q < lo) q += 12;
        if (style == 1 && e.step < bar && k > 0) motif.push_back (q - p);
        lastIv = k > 0 ? q - p : 0;
        p = q;

        // ecriture : note relative a la base, octave, prolongations
        int rel = q - base, oct = 0;
        while (rel > 24) { rel -= 12; ++oct; }
        while (rel < 0) { rel += 12; --oct; }
        const int acc = (strong && r.nextFloat() < accP) ? 1 : 0;
        const bool legato = ! last && k + 1 < evs.size() && evs[k + 1].step == e.step + e.dur && std::abs (lastIv) <= 2 && r.nextFloat() < slideP;
        const int lastTie = e.step + e.dur - 1;
        P.steps[e.step].set (1, rel, oct, acc, (legato && e.dur == 1) ? 1 : 0, 0, 100, 1, 0, e.dur > 1 ? 95 : 70);
        for (int t = e.step + 1; t <= lastTie && t < len; ++t)
            P.steps[t].set (1, rel, oct, 0, (legato && t == lastTie) ? 1 : 0, 1, 100, 1, 0, 95);
    }
}

void CantusAudioProcessor::generatePattern (int pat, int styleIdx, int64_t seed)
{
    generateMelodyInto (bank.pats[pat], styleIdx, seed, nullptr);
}

void CantusAudioProcessor::generatePatternLegacy (int pat, int styleIdx, int64_t seed)
{
    auto r = makeRandom (seed);
    auto& P = bank.pats[pat];
    GenOpts o { paramReal ("gendens"), paramReal ("genrange"), paramReal ("genacc"), paramReal ("genslide"),
                paramReal ("genoct"), paramReal ("genroot"), juce::jmax (1, (int) scaleRaw->load()) };
    const int len = P.length;
    P.clear();
    if (P.rate.load() > 3 && styleIdx != 8) P.rate = 1;

    // rythme
    uint32_t mask = 0;
    uint32_t kick = 0x1111u; int kickLen = 16;
    if (styleIdx == 6 || styleIdx == 7)
    {
        uint32_t m; int l;
        if (drumMask (0, m, l) && m != 0) { kick = 0; for (int i = 0; i < len; ++i) if ((m >> (i % juce::jmax (1, l))) & 1u) kick |= 1u << i; kickLen = l; }
        else { kick = 0; for (int i = 0; i < len; i += 4) kick |= 1u << i; }
    }
    juce::ignoreUnused (kickLen);
    for (int i = 0; i < len; ++i)
    {
        bool on = false;
        switch (styleIdx)
        {
            case 0: on = r.nextFloat() < o.dens * (i % 4 == 0 ? 1.0f : 0.8f); break;
            case 1: on = (i % 4 != 0) && r.nextFloat() < 0.4f + o.dens * 0.6f; break;
            case 2: on = (i % 2 == 0) && r.nextFloat() < 0.5f + o.dens * 0.5f; break;
            case 3: on = (i == 0) || r.nextFloat() < o.dens * 0.25f; break;
            case 4: on = r.nextFloat() < 0.5f + o.dens * 0.5f; break;
            case 5: break;
            case 6: on = ((kick >> i) & 1u) || r.nextFloat() < o.dens * 0.2f; break;
            case 7: on = ! ((kick >> i) & 1u) && r.nextFloat() < 0.3f + o.dens * 0.6f; break;
            default: on = r.nextFloat() < o.dens; break;
        }
        if (on) mask |= 1u << i;
    }
    if (styleIdx == 5) mask = euclidMask (juce::jlimit (1, len, 1 + (int) std::round (o.dens * len * 0.6f)), len, r.nextInt (4));
    if (mask == 0) mask = 1u;

    // notes
    const bool minorish = alea::scaleIsMinor (o.scale);
    const int chordTones[] { 0, minorish ? 3 : 4, 7, 12, 7, minorish ? 3 : 4 };
    int arpPos = 0;
    for (int i = 0; i < len; ++i)
    {
        auto& s = P.steps[i];
        if (! ((mask >> i) & 1u)) continue;
        int note = pickNote (r, o);
        int oct = 0;
        int gate = 50 + r.nextInt (15);
        switch (styleIdx)
        {
            case 1: note = r.nextFloat() < 0.7f + o.rootW * 0.3f ? 0 : (r.nextBool() ? 7 : 12); gate = 35; break;
            case 2: note = 0; if (i % 4 == 2 && r.nextFloat() < 0.6f) note = 12; gate = 70; break;
            case 3: { static const int dub[] { 0, 0, 7, 3, 10, 5 }; note = (int) quantizeSemis ((float) dub[r.nextInt (6)], o.scale); gate = 90; break; }
            case 4: note = (int) quantizeSemis ((float) chordTones[arpPos++ % 6], o.scale); gate = 45; break;
            case 6: if ((kick >> i) & 1u) note = 0; break;
            default: break;
        }
        if (r.nextFloat() < o.oct * 0.5f) oct = r.nextBool() ? 1 : -1;
        if (styleIdx == 2 && note == 12) oct = 0;
        const int acc = r.nextFloat() < o.acc * (styleIdx == 6 && ((kick >> i) & 1u) ? 1.8f : 1.0f) ? 1 : 0;
        const int slide = (styleIdx != 2 && styleIdx != 1 && r.nextFloat() < o.slide) ? 1 : 0;
        s.set (1, note, oct, acc, slide, 0, 100, 1, 0, gate);
        // Dub : notes longues (prolongations)
        if (styleIdx == 3)
        {
            const int ties = r.nextInt (3);
            for (int t = 1; t <= ties && i + t < len && ! ((mask >> (i + t)) & 1u); ++t) P.steps[i + t].set (1, note, oct, 0, 0, 1);
        }
    }
}

// Ligne construite sur la grille de Color (AleaLink) : fondamentale sur chaque changement d'accord, quinte /
// octave / tierce de l'accord, notes d'approche chromatiques ou diatoniques vers la basse de l'accord suivant
// (Dobbins : walking bass). La ligne est ecrite relativement a la basse de chaque accord, et jouee en
// Chord follow > Root : Talea la transpose sur la basse de l'accord en cours.
bool CantusAudioProcessor::generateFromColor (int pat, int64_t seed)
{
    auto r = makeRandom (seed);
    auto& P = bank.pats[pat];
    alea::link::ProgChord prog[alea::link::kMaxProg];
    double start = 0.0;
    int n = 0;
    if (link != nullptr)
    {
        const int slot = link->firstPeerOfKind (alea::link::KindColor);
        if (slot >= 0) n = link->readProgression (slot, prog, alea::link::kMaxProg, start);
    }
    if (n <= 0) { prog[0] = { (int) rootRaw->load(), (int) rootRaw->load(), 0x91, 0xAB5, 4 }; n = 1; }   // repli : accord de tonique
    int totalBeats = 0;
    for (int i = 0; i < n; ++i) totalBeats += prog[i].beats;
    P.clear();
    P.dir = 0; P.groove = 0;
    const int rate = totalBeats <= 8 ? 1 : 0;                       // doubles croches, sinon croches
    P.rate = (int8_t) rate;
    const double stepLen = ratePpq (rate);
    const int len = juce::jlimit (1, kSteps, (int) std::round (totalBeats / stepLen));
    P.length = (int8_t) len;
    const int style = r.nextInt (4);                                // 0 fondamentale-quinte, 1 walking, 2 pulsation, 3 syncopes
    const float dens = paramReal ("gendens");

    auto chordAtBeat = [&] (double b, int& idx, double& cStart, double& cEnd)
    {
        double acc = 0.0;
        for (int i = 0; i < n; ++i)
        {
            if (b < acc + prog[i].beats - 1.0e-9 || i == n - 1) { idx = i; cStart = acc; cEnd = acc + prog[i].beats; return; }
            acc += prog[i].beats;
        }
    };
    auto relTo = [] (int pc, int bass) { return ((pc - bass) % 12 + 12) % 12; };
    for (int i = 0; i < len; ++i)
    {
        const double b = i * stepLen;
        int ci = 0; double cs = 0, ce = 4;
        chordAtBeat (b, ci, cs, ce);
        const auto& c = prog[ci];
        const auto& nx = prog[(ci + 1) % n];
        const int bass = c.bass;
        // intervalles de l'accord vu depuis sa basse
        int third = 4, fifth = 7;
        for (int iv : { 3, 4 }) if ((c.mask >> ((bass + iv) % 12)) & 1) { third = iv; break; }
        for (int iv : { 7, 6, 8 }) if ((c.mask >> ((bass + iv) % 12)) & 1) { fifth = iv; break; }
        const bool change = std::abs (b - cs) < 1.0e-6;
        const bool lastBeforeChange = std::abs ((b + stepLen) - ce) < 1.0e-6 && n > 1;
        const bool onBeat = std::fmod (b, 1.0) < 1.0e-6;
        const int toNext = relTo (nx.bass, bass);
        bool on = false; int note = 0, oct = 0, acc = 0, slide = 0, gate = 60;
        switch (style)
        {
            case 0: on = change || (std::fmod (b - cs, 2.0) < 1.0e-6) || (lastBeforeChange && r.nextFloat() < 0.7f) || r.nextFloat() < dens * 0.15f;
                    note = change ? 0 : (r.nextFloat() < 0.6f ? fifth : 12); break;
            case 1: on = onBeat || (lastBeforeChange && r.nextFloat() < 0.4f);
                    { const int k = (int) std::round (b - cs);
                      static const int pick[] { 0, 1, 2, 1 };
                      const int choice = pick[k % 4];
                      note = change ? 0 : (choice == 1 ? third : (choice == 2 ? fifth : 12)); }
                    gate = 85; break;
            case 2: on = (i % 2 == 0) || r.nextFloat() < dens * 0.3f;
                    note = (i % 4 == 2 && r.nextFloat() < 0.5f) ? 12 : 0; gate = 45; break;
            default: on = change || (std::fmod (b - cs + 0.5, 1.5) < 1.0e-6) || r.nextFloat() < dens * 0.25f;
                    note = r.nextFloat() < 0.7f ? 0 : fifth; break;
        }
        if (lastBeforeChange && on)
        {
            // approche de la basse suivante : demi-ton dessous / dessus, ou degre conjoint
            const int kind = r.nextInt (3);
            note = toNext + (kind == 0 ? -1 : (kind == 1 ? 1 : -2));
            if (note < 0) note += 12;
            acc = 0; slide = style == 1 ? 0 : (r.nextFloat() < 0.4f ? 1 : 0);
        }
        if (change) acc = 1;
        if (! on) continue;
        if (note > 24) note -= 12;
        P.steps[i].set (1, note, oct, acc, slide, 0, 100, 1, 0, gate);
    }
    if ((int) chordFollowRaw->load() == 0) setParamReal ("chordfollow", 1.0f);
    return n > 1 || link != nullptr;
}

void CantusAudioProcessor::generateNotesOnly (int pat, int64_t seed)
{
    auto r = makeRandom (seed);
    GenOpts o { paramReal ("gendens"), paramReal ("genrange"), paramReal ("genacc"), paramReal ("genslide"),
                paramReal ("genoct"), paramReal ("genroot"), juce::jmax (1, (int) scaleRaw->load()) };
    auto& P = bank.pats[pat];
    for (int i = 0; i < P.length; ++i)
    {
        auto& s = P.steps[i];
        if (! s.on || s.tie) continue;
        s.note = (int8_t) pickNote (r, o);
        s.oct = (int8_t) (r.nextFloat() < o.oct * 0.5f ? (r.nextBool() ? 1 : -1) : 0);
    }
}

void CantusAudioProcessor::generateRhythmOnly (int pat, int64_t seed)
{
    auto r = makeRandom (seed);
    auto& P = bank.pats[pat];
    const float dens = paramReal ("gendens");
    int lastNote = 0;
    for (int i = 0; i < P.length; ++i) if (P.steps[i].on) { lastNote = P.steps[i].note; break; }
    for (int i = 0; i < P.length; ++i)
    {
        auto& s = P.steps[i];
        const bool on = r.nextFloat() < dens * (i % 4 == 0 ? 1.0f : 0.8f);
        if (s.on && ! s.tie) lastNote = s.note;
        s.on = (int8_t) (on ? 1 : 0);
        s.tie = 0;
        if (on && s.note == 0 && lastNote != 0 && r.nextFloat() < 0.3f) s.note = (int8_t) lastNote;
        s.acc = (int8_t) (on && r.nextFloat() < paramReal ("genacc") ? 1 : 0);
        s.slide = (int8_t) (on && r.nextFloat() < paramReal ("genslide") ? 1 : 0);
    }
}

void CantusAudioProcessor::mutateNotes (int pat, float amount)
{
    juce::Random r;
    const int sc = juce::jmax (1, (int) scaleRaw->load());
    auto& P = bank.pats[pat];
    for (int i = 0; i < P.length; ++i)
    {
        auto& s = P.steps[i];
        if (! s.on || r.nextFloat() >= amount) continue;
        const float v = scaleShift ((float) s.note, sc, r.nextBool() ? (r.nextBool() ? 1 : 2) : (r.nextBool() ? -1 : -2));
        s.note = (int8_t) juce::jlimit (0, 24, (int) v);
    }
}

void CantusAudioProcessor::mutateRhythm (int pat, float amount)
{
    juce::Random r;
    auto& P = bank.pats[pat];
    for (int i = 0; i < P.length; ++i)
    {
        auto& s = P.steps[i];
        if (r.nextFloat() < amount) { s.on = (int8_t) (s.on ? 0 : 1); s.tie = 0; }
        if (s.on && r.nextFloat() < amount * 0.5f) s.acc = (int8_t) (s.acc ? 0 : 1);
        if (s.on && r.nextFloat() < amount * 0.4f) s.slide = (int8_t) (s.slide ? 0 : 1);
    }
}

void CantusAudioProcessor::transposePattern (int pat, int degrees)
{
    const int sc = juce::jmax (1, (int) scaleRaw->load());
    for (auto& s : bank.pats[pat].steps)
    {
        float v = scaleShift ((float) s.note + 12.0f * s.oct, sc, degrees);
        int oct = (int) std::floor (v / 12.0f);
        int note = (int) v - oct * 12;
        if (oct > 0 && note + 12 <= 24) { note += 12; --oct; }
        s.note = (int8_t) juce::jlimit (0, 24, note);
        s.oct = (int8_t) juce::jlimit (-2, 2, oct);
    }
}

void CantusAudioProcessor::octavePattern (int pat, int dir)
{
    for (auto& s : bank.pats[pat].steps) s.oct = (int8_t) juce::jlimit (-2, 2, s.oct + dir);
}

void CantusAudioProcessor::reversePattern (int pat)
{
    auto& P = bank.pats[pat];
    BPattern tmp; tmp.copyFrom (P);
    const int n = P.length;
    for (int i = 0; i < n; ++i) P.steps[i].copyFrom (tmp.steps[n - 1 - i]);
}

void CantusAudioProcessor::invertPattern (int pat)
{
    const int sc = juce::jmax (1, (int) scaleRaw->load());
    for (auto& s : bank.pats[pat].steps)
    {
        const float v = quantizeSemis (12.0f - (float) s.note, sc);
        s.note = (int8_t) juce::jlimit (0, 24, (int) v);
    }
}

void CantusAudioProcessor::shiftPattern (int pat, int dir)
{
    auto& P = bank.pats[pat];
    BPattern tmp; tmp.copyFrom (P);
    const int n = P.length;
    for (int i = 0; i < n; ++i) P.steps[i].copyFrom (tmp.steps[((i - dir) % n + n) % n]);
}

void CantusAudioProcessor::euclidPattern (int pat, int hits, int rot)
{
    auto& P = bank.pats[pat];
    const int n = P.length;
    const uint32_t m = euclidMask (juce::jlimit (0, n, hits), n, rot);
    int note = 0;
    for (int i = 0; i < n; ++i) if (P.steps[i].on) { note = P.steps[i].note; break; }
    for (int i = 0; i < n; ++i)
    {
        auto& s = P.steps[i];
        const bool on = (m >> i) & 1u;
        if (on && ! s.on) s.note = (int8_t) note;
        s.on = (int8_t) (on ? 1 : 0);
        s.tie = 0;
    }
}

void CantusAudioProcessor::varyPattern (int pat, float amount, int64_t seed)
{
    auto r = makeRandom (seed);
    auto& P = bank.pats[pat];
    const int len = P.length;
    float cnt[4][2][2] {};
    juce::Array<int> notes;
    int on = 0, acc = 0, sl = 0;
    for (int i = 0; i < len; ++i)
    {
        const int prev = P.steps[(i + len - 1) % len].on ? 1 : 0, cur = P.steps[i].on ? 1 : 0;
        cnt[i % 4][prev][cur] += 1.0f;
        if (cur) { ++on; notes.add (P.steps[i].note + 12 * P.steps[i].oct); acc += P.steps[i].acc; sl += P.steps[i].slide; }
    }
    if (on == 0) { generatePattern (pat, 0, seed); return; }
    int prev = P.steps[len - 1].on ? 1 : 0;
    for (int i = 0; i < len; ++i)
    {
        auto& s = P.steps[i];
        if (r.nextFloat() < amount)
        {
            const float a = cnt[i % 4][prev][1] + 0.3f, b = cnt[i % 4][prev][0] + 0.3f;
            const bool nowOn = r.nextFloat() < a / (a + b);
            if (nowOn)
            {
                const int nv = notes[r.nextInt (notes.size())];
                const int oc = (int) std::floor (nv / 12.0f);
                s.set (1, nv - oc * 12, oc, r.nextFloat() < (float) acc / on ? 1 : 0, r.nextFloat() < (float) sl / on ? 1 : 0, 0, 100, 1, 0, s.gate);
            }
            else { s.on = 0; s.tie = 0; }
        }
        prev = s.on ? 1 : 0;
    }
}

void CantusAudioProcessor::breedPatterns (int a, int b, int dst, int64_t seed)
{
    auto r = makeRandom (seed);
    BPattern child;
    const auto& A = bank.pats[a];
    const auto& B = bank.pats[b];
    child.copyFrom (r.nextBool() ? A : B);
    const int len = child.length;
    const int cut = 1 + r.nextInt (juce::jmax (1, len - 1));
    const bool aFirst = r.nextBool();
    for (int i = 0; i < len; ++i)
    {
        // rythme et notes peuvent venir de parents differents
        const auto& rhythmSrc = ((i < cut) == aFirst) ? A : B;
        const auto& noteSrc = r.nextFloat() < 0.5f ? A : B;
        child.steps[i].copyFrom (rhythmSrc.steps[i]);
        if (noteSrc.steps[i].on) { child.steps[i].note = noteSrc.steps[i].note.load(); child.steps[i].oct = noteSrc.steps[i].oct.load(); }
        if (r.nextFloat() < 0.04f) child.steps[i].on = (int8_t) (child.steps[i].on ? 0 : 1);
    }
    bank.pats[dst].copyFrom (child);
}

// ---------------------------------------------------------------------------
// Style appris d'un fichier MIDI de basse
bool CantusAudioProcessor::learnStyleFromMidi (const juce::File& f)
{
    juce::MidiFile mf;
    juce::FileInputStream in (f);
    if (! in.openedOk() || ! mf.readFrom (in)) return false;
    const int tpq = mf.getTimeFormat();
    if (tpq <= 0) return false;
    struct N { int64_t pos; double start, end; int note, vel; };
    std::vector<N> notes;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* ev = seq.getEventPointer (i);
            if (! ev->message.isNoteOn()) continue;
            const double st = ev->message.getTimeStamp();
            const double en = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : st + tpq / 4.0;
            notes.push_back ({ (int64_t) std::llround (st / (tpq / 4.0)), st, en, ev->message.getNoteNumber(), ev->message.getVelocity() });
        }
    }
    if (notes.empty()) return false;
    // une note (la plus grave) par double-croche
    std::sort (notes.begin(), notes.end(), [] (const N& a, const N& b) { return a.pos != b.pos ? a.pos < b.pos : a.note < b.note; });
    std::vector<N> mono;
    for (auto& n : notes) if (mono.empty() || mono.back().pos != n.pos) mono.push_back (n);

    Style s;
    const int64_t maxPos = mono.back().pos;
    const int bars = (int) (maxPos / 16) + 1;
    std::vector<int> grid ((size_t) bars * 16, -1);
    for (size_t i = 0; i < mono.size(); ++i) grid[(size_t) mono[i].pos] = (int) i;
    int pcCount[12] {};
    for (auto& n : mono) pcCount[n.note % 12]++;
    int tonic = 0;
    for (int i = 1; i < 12; ++i) if (pcCount[i] > pcCount[tonic]) tonic = i;
    float onCount[16] {};
    for (int i = 0; i < bars * 16; ++i)
    {
        const int prev = grid[(size_t) ((i + bars * 16 - 1) % (bars * 16))] >= 0 ? 1 : 0;
        const int idx = grid[(size_t) i];
        s.on[i % 16][prev][idx >= 0 ? 1 : 0] += 1.0f;
        if (idx >= 0)
        {
            const auto& n = mono[(size_t) idx];
            onCount[i % 16] += 1.0f;
            if (n.vel >= 110) s.acc[i % 16] += 1.0f;
            if ((size_t) idx + 1 < mono.size() && n.end > mono[(size_t) idx + 1].start + 1.0) s.slide[i % 16] += 1.0f;
            s.degree[((n.note - tonic) % 12 + 12) % 12] += 1.0f;
            if ((size_t) idx + 1 < mono.size())
                s.interval[juce::jlimit (-12, 12, mono[(size_t) idx + 1].note - n.note) + 12] += 1.0f;
        }
    }
    for (int i = 0; i < 16; ++i)
        if (onCount[i] > 0) { s.acc[i] /= onCount[i]; s.slide[i] /= onCount[i]; }
    s.valid = true; s.bars = bars; s.name = f.getFileNameWithoutExtension();
    style = s;
    return true;
}

void CantusAudioProcessor::generateFromStyle (int pat, int64_t seed)
{
    if (! style.valid) return;
    auto r = makeRandom (seed);
    auto& P = bank.pats[pat];
    P.clear(); P.length = 16; P.rate = 1;
    const int sc = juce::jmax (1, (int) scaleRaw->load());
    auto pickWeighted = [&r] (const float* w, int n)
    {
        float tot = 0; for (int i = 0; i < n; ++i) tot += w[i] + 0.01f;
        float x = r.nextFloat() * tot;
        for (int i = 0; i < n; ++i) { x -= w[i] + 0.01f; if (x <= 0) return i; }
        return n - 1;
    };
    int prev = 0, pitch = 0;
    bool first = true;
    for (int i = 0; i < 16; ++i)
    {
        const float a = style.on[i][prev][1] + 0.1f, b = style.on[i][prev][0] + 0.1f;
        const bool on = r.nextFloat() < a / (a + b);
        prev = on ? 1 : 0;
        if (! on) continue;
        if (first) { pitch = pickWeighted (style.degree, 12); first = false; }
        else pitch += pickWeighted (style.interval, 25) - 12;
        while (pitch > 19) pitch -= 12;
        while (pitch < -5) pitch += 12;
        const float q = quantizeSemis ((float) pitch, sc);
        const int oc = (int) std::floor (q / 12.0f);
        P.steps[i].set (1, (int) q - oc * 12, oc, r.nextFloat() < style.acc[i] ? 1 : 0, r.nextFloat() < style.slide[i] ? 1 : 0);
    }
}

// ---------------------------------------------------------------------------
CantusAudioProcessor::Preview CantusAudioProcessor::previewNextBar() const
{
    Preview pv;
    const bool run = isRunning.load();
    const double from = run ? playPpq.load() : 0.0;
    const int pat = currentPattern.load();
    auto ctx = makeContext (pat, run ? patStartShared.load() : 0.0, bpmRaw->load());
    BassCondState cs;
    runBassSequencer (bank, ctx, &cs, from, from + 4.0, [&] (double, BassEvent e)
    {
        if (e.step >= 0) (e.ghost ? pv.ghost : pv.play) |= 1u << e.step;
    });
    const auto& P = bank.pats[pat];
    const uint32_t evo = evoMask[pat].load();
    for (int i = 0; i < P.length; ++i)
    {
        const bool on = (P.steps[i].on != 0) != (((evo >> i) & 1u) != 0);
        if (on && ! P.steps[i].tie && ! ((pv.play >> i) & 1u)) pv.skip |= 1u << i;
    }
    return pv;
}

juce::File CantusAudioProcessor::exportMidi (int pat, int bars)
{
    constexpr int ppqTicks = 960;
    const double bpm = bpmRaw->load();
    auto ctx = makeContext (pat, 0.0, bpm);
    BassCondState cs;
    const int base = 24 + 12 * (int) baseOctRaw->load() + (int) rootRaw->load();
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Cantus"), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0.0);
    runBassSequencer (bank, ctx, &cs, 0.0, bars * 4.0, [&] (double te, BassEvent e)
    {
        const double tick = juce::jmax (0.0, std::round (te * ppqTicks));
        const int note = juce::jlimit (0, 127, base + (int) e.semis);
        seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (e.accent ? 127 : juce::jlimit (1, 127, (int) (e.vel * 110.0f)))), tick);
        seq.addEvent (juce::MidiMessage::noteOff (1, note), tick + juce::jmax (10.0, e.gatePpq * ppqTicks));
    });
    seq.sort();
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppqTicks);
    mf.addTrack (seq);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Cantus").getChildFile ("MIDI");
    dir.createDirectory();
    auto f = dir.getChildFile ("Cantus Pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + pat)) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) { mf.writeTo (os); os.flush(); }
    return f;
}

// ---------------------------------------------------------------------------
// Presets d'usine
void CantusAudioProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        {
            const auto& id = wp->paramID;
            if (id == "play" || id == "pattern" || id.startsWith ("lk_") || id == "root" || id == "scale" || id == "bpm") continue;
            p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture();
        }
    bank.initDefault();
    for (auto& l : locks) l = false;
    hasB = false;
    for (auto& v : sceneValid) v = false;
    for (auto& f : favs) f.valid = false;
    resetEvolution();
    setChainText ("A4 B4");
}

juce::StringArray CantusAudioProcessor::factoryPresetNames()
{
    return { "Init Lead", "Singing Saw", "Soft Flute", "Square Chip", "FM Bell Lead", "Pluck Melody", "Anthem Supersaw", "Bebop Synth", "Fold Whistle" };
}

void CantusAudioProcessor::loadFactoryPreset (int index)
{
    pushUndo();
    resetToDefaults();
    auto P = [this] (const char* id, float real) { setParamReal (id, real); };
    auto V = [this] (int tp, float n) { setParamNorm (tpId (tp), n); };
    auto J = [this] (int tp, float n) { setParamNorm (tpJit (tp), n); };
    auto MX = [this] (int s, int src, int dst, float amt)
    {
        const juce::String pre = "mx" + juce::String (s + 1);
        setParamReal (pre + "src", (float) src); setParamReal (pre + "dst", (float) dst); setParamReal (pre + "amt", amt);
    };
    int genStyle = 0;
    switch (index)
    {
        case 1:   // Singing Saw : saw + osc2 desaccorde, vibrato marque, legato
            P ("osc1type", 0); P ("osc2type", 1); P ("ftype", 1); P ("glidemode", 0);
            V (O2Level, 0.45f); V (O2Fine, 0.56f); V (Cutoff, 0.6f); V (Reso, 0.2f); V (EnvMod, 0.25f); V (Attack, 0.15f);
            V (Sustain, 0.8f); V (Release, 0.4f); V (Glide, 0.4f); P ("vibdepth", 0.4f); P ("vibdelay", 0.4f); V (SendDly, 0.35f);
            genStyle = 0;
            break;
        case 2:   // Soft Flute : sinus + souffle, attaque douce
            P ("osc1type", 4); P ("osc2type", 5); P ("ftype", 2);
            V (O2Level, 0.12f); V (NoiseLvl, 0.08f); V (Cutoff, 0.55f); V (Reso, 0.05f); V (Attack, 0.3f); V (Sustain, 0.85f);
            V (Release, 0.4f); P ("vibdepth", 0.3f); P ("vibrate", 0.5f); V (SendRev, 0.45f);
            genStyle = 0;
            break;
        case 3:   // Square Chip : carre, arpeges rapides
            P ("osc1type", 1); P ("ftype", 5); P ("glidemode", 2);
            V (Attack, 0.0f); V (Decay, 0.4f); V (Sustain, 0.5f); V (Release, 0.15f); P ("vibdepth", 0.0f); V (SendDly, 0.3f);
            genStyle = 2;
            break;
        case 4:   // FM Bell Lead
            P ("osc1type", 4); P ("osc2type", 3); P ("ftype", 2);
            V (FM, 0.35f); V (O2Semi, 0.75f); V (Cutoff, 0.7f); V (Attack, 0.0f); V (Decay, 0.55f); V (Sustain, 0.25f); V (Release, 0.5f);
            V (SendRev, 0.4f); V (SendDly, 0.3f); P ("vibdepth", 0.15f);
            genStyle = 1;
            break;
        case 5:   // Pluck Melody
            P ("osc1type", 7); P ("ftype", 2); P ("glidemode", 2);
            V (Shape, 0.4f); V (Cutoff, 0.65f); V (Decay, 0.5f); V (Sustain, 0.0f); V (Release, 0.35f); V (SendDly, 0.4f); P ("vibdepth", 0.0f);
            genStyle = 3;
            break;
        case 6:   // Anthem Supersaw
            P ("osc1type", 5); P ("ftype", 1); P ("chord", 1);
            V (Shape, 0.7f); V (Cutoff, 0.62f); V (Reso, 0.2f); V (EnvMod, 0.3f); V (Attack, 0.05f); V (Sustain, 0.8f); V (Release, 0.45f);
            V (SendRev, 0.45f); P ("chmix", 0.4f); P ("vibdepth", 0.2f);
            genStyle = 1;
            break;
        case 7:   // Bebop Synth : legato rapide, lignes chromatiques
            P ("osc1type", 2); P ("ftype", 1); P ("glidemode", 0);
            V (Shape, 0.35f); V (Cutoff, 0.5f); V (Reso, 0.35f); V (EnvMod, 0.4f); V (FDecay, 0.35f); V (Glide, 0.2f); V (Sustain, 0.6f);
            P ("swing", 0.5f); P ("vibdepth", 0.1f);
            genStyle = 4;
            break;
        case 8:   // Fold Whistle
            P ("osc1type", 6); P ("ftype", 3);
            V (Shape, 0.3f); V (Cutoff, 0.68f); V (Reso, 0.4f); V (Attack, 0.2f); V (Sustain, 0.8f); P ("vibdepth", 0.35f); V (SendRev, 0.35f);
            J (Shape, 0.15f);
            MX (0, 2, Shape, 0.3f);
            genStyle = 7;
            break;
        default: break;   // Init Lead
    }
    setParamReal ("genstyle", (float) genStyle);
    if (genStyle >= 0)
    {
        generatePattern (0, genStyle, 4242 + index);
        for (int p = 1; p < kPatterns; ++p)
        {
            bank.pats[p].copyFrom (bank.pats[0]);
            varyPattern (p, 0.08f * (float) p, 1000 * p + index);
        }
    }
    storeScene (0);
    for (int n = 1; n < 4; ++n)
    {
        auto r = makeRandom (700 * index + n);
        for (int i = 0; i < kNumTP; ++i)
            sceneVal[n][i] = juce::jlimit (0.0f, 1.0f, tpRaw[i]->load() + (i == Level ? 0.0f : (r.nextFloat() * 2.0f - 1.0f) * 0.1f * (float) n));
        for (int i = 0; i < kNumTypes; ++i) sceneTypes[n][i] = (int) typeRaw[i]->load();
        sceneValid[n] = true;
    }
    currentPresetName = factoryPresetNames()[juce::jlimit (0, factoryPresetNames().size() - 1, index)];
}
