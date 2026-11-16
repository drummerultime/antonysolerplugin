// Generateurs de COLOR : sons (de, mutation, favoris, croisement, scenes, morph, evolution),
// harmonie (progressions par style, reharmonisation, cadences, transformations, saisie texte, analyse MIDI),
// rythmes (presets, generation, mutation, euclide), export MIDI, presets d'usine.
#include "ColorProcessor.h"

using namespace color;
namespace th = color::theory;

static juce::Random makeRandom (int64_t seed)
{
    if (seed < 0) return juce::Random();
    const uint64_t hi = hash3 ((uint32_t) seed, (uint32_t) (seed >> 32), 0xC010u);
    const uint64_t lo = hash3 ((uint32_t) seed ^ 0x5bd1e995u, 0x7654321u, (uint32_t) (seed >> 32));
    juce::Random r ((juce::int64) ((hi << 32) | lo));
    for (int i = 0; i < 4; ++i) r.nextInt();
    return r;
}

// ---------------------------------------------------------------------------
// Son
void ColorAudioProcessor::diceSound (bool includeTypes, int64_t seed)
{
    auto r = makeRandom (seed);
    if (includeTypes && ! locks[kNumTP])
    {
        static const int osc1W[] { 0, 0, 1, 2, 3, 4, 4, 5, 5, 5, 6, 7, 7 };
        setParamReal ("osc1type", (float) osc1W[r.nextInt (13)]);
        setParamReal ("osc2type", (float) (r.nextFloat() < 0.45f ? 0 : 1 + r.nextInt (4)));
        setParamReal ("ftype", (float) (r.nextFloat() < 0.7f ? 2 : r.nextInt (4)));
        setParamReal ("drivetype", (float) r.nextInt (driveNames().size()));
        setParamReal ("subtype", (float) r.nextInt (subNames().size()));
    }
    for (int i = 0; i < kNumTP; ++i)
    {
        if (locks[i]) continue;
        float lo, hi;
        tpDiceRange (i, lo, hi);
        float v = lo + r.nextFloat() * (hi - lo);
        if ((i == FM || i == NoiseLvl || i == O2Level) && r.nextFloat() < 0.5f) v = 0.0f;
        if (i == O2Semi && r.nextFloat() < 0.7f) { static const float iv[] { 0.5f, 0.75f, 0.646f, 0.604f }; v = iv[r.nextInt (4)]; }   // unisson, octave, quinte, tierce
        setParamNorm (tpId (i), v);
    }
}

void ColorAudioProcessor::mutateSound (float amount)
{
    juce::Random r;
    for (int i = 0; i < kNumTP; ++i)
    {
        if (locks[i] || i == Level) continue;
        setParamNorm (tpId (i), tpRaw[i]->load() + (r.nextFloat() * 2.0f - 1.0f) * amount);
    }
}

void ColorAudioProcessor::randomizeJitter (int64_t seed)
{
    auto r = makeRandom (seed);
    for (int i = 0; i < kNumTP; ++i)
    {
        if (locks[i]) continue;
        const bool musical = i == Cutoff || i == EnvMod || i == Shape || i == Attack || i == O2Fine || i == Width;
        const float v = (! musical && r.nextFloat() < 0.7f) ? 0.0f : r.nextFloat() * 0.25f;
        setParamNorm (tpJit (i), v);
    }
}

void ColorAudioProcessor::clearJitter()
{
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpJit (i), 0.0f);
}

void ColorAudioProcessor::storeB()
{
    for (int i = 0; i < kNumTP; ++i) snapB[i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) snapBTypes[i] = (int) typeRaw[i]->load();
    hasB = true;
}

void ColorAudioProcessor::recallB()
{
    if (! hasB) return;
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpId (i), snapB[i].load());
    for (int i = 0; i < kNumTypes; ++i) setParamReal (typeParamId (i), (float) snapBTypes[i].load());
}

void ColorAudioProcessor::swapB()
{
    if (! hasB) { storeB(); return; }
    float a[kNumTP]; int at[kNumTypes];
    for (int i = 0; i < kNumTP; ++i) a[i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) at[i] = (int) typeRaw[i]->load();
    recallB();
    for (int i = 0; i < kNumTP; ++i) snapB[i] = a[i];
    for (int i = 0; i < kNumTypes; ++i) snapBTypes[i] = at[i];
}

void ColorAudioProcessor::storeScene (int n)
{
    n = juce::jlimit (0, kScenes - 1, n);
    for (int i = 0; i < kNumTP; ++i) sceneVal[n][i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) sceneTypes[n][i] = (int) typeRaw[i]->load();
    sceneValid[n] = true;
}

void ColorAudioProcessor::recallScene (int n)
{
    n = juce::jlimit (0, kScenes - 1, n);
    if (! sceneValid[n].load()) return;
    for (int i = 0; i < kNumTypes; ++i) setParamReal (typeParamId (i), (float) sceneTypes[n][i].load());
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpId (i), sceneVal[n][i].load());
    lastRecalledScene = n;
}

void ColorAudioProcessor::keepLastNote()
{
    for (int i = 0; i < kNumTP; ++i)
    {
        const float v = lastNorm[i].load();
        if (v >= 0.0f) setParamNorm (tpId (i), v);
        evoOff[i] = 0.0f;
    }
}

void ColorAudioProcessor::storeFav (int slot)
{
    auto& f = favs[juce::jlimit (0, kFavs - 1, slot)];
    for (int i = 0; i < kNumTP; ++i) f.v[i] = tpRaw[i]->load();
    for (int i = 0; i < kNumTypes; ++i) f.types[i] = (int) typeRaw[i]->load();
    f.valid = true;
}

void ColorAudioProcessor::recallFav (int slot)
{
    const auto& f = favs[juce::jlimit (0, kFavs - 1, slot)];
    if (! f.valid) return;
    for (int i = 0; i < kNumTypes; ++i) setParamReal (typeParamId (i), (float) f.types[i]);
    for (int i = 0; i < kNumTP; ++i) setParamNorm (tpId (i), f.v[i]);
    guiTrig = true;
}

void ColorAudioProcessor::breedVoice (int slotA, int slotB, int64_t seed)
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
void ColorAudioProcessor::resetEvolution()
{
    for (int p = 0; p < kPatterns; ++p) { evoMask[p] = 0u; evoSub[p] = 0u; }
    for (auto& v : evoOff) v = 0.0f;
}

void ColorAudioProcessor::commitEvolution()
{
    const int sc = mode();
    const uint32_t seed = (uint32_t) seedRaw->load();
    for (int p = 0; p < kPatterns; ++p)
    {
        const uint32_t m = evoMask[p].exchange (0u);
        for (int i = 0; i < kSteps; ++i)
            if ((m >> i) & 1u) { auto& s = bank.pats[p].steps[i]; s.on = (int8_t) (s.on ? 0 : 1); }
        const uint32_t su = evoSub[p].exchange (0u);
        auto& P = bank.pats[p];
        const int n = P.chordCount();
        for (int i = 0; i < n; ++i)
            if ((su >> i) & 1u)
            {
                auto c = th::liveSubstitute (P.chord (i), P.chord ((i + 1) % n), sc, hash3 ((uint32_t) i, seed ^ 0xE5u, (uint32_t) p),
                                             juce::jmax (0.5f, tensionRaw->load()));
                c.beats = P.chord (i).beats;
                P.setChord (i, c);
            }
    }
    for (int i = 0; i < kNumTP; ++i)
    {
        const float o = evoOff[i].exchange (0.0f);
        if (o != 0.0f) setParamNorm (tpId (i), tpRaw[i]->load() + o);
    }
}

int ColorAudioProcessor::evolutionAmount() const
{
    int n = 0;
    for (int p = 0; p < kPatterns; ++p)
        for (uint32_t v : { evoMask[p].load(), evoSub[p].load() })
            while (v) { n += (int) (v & 1u); v >>= 1; }
    for (auto& v : evoOff) if (std::abs (v.load()) > 0.01f) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Harmonie
void ColorAudioProcessor::generateProgression (int pat, int64_t seed)
{
    th::GenOptions o;
    o.style = (int) styleRaw->load();
    o.scale = mode();
    o.numChords = (int) nchordsRaw->load();
    o.complexity = complexRaw->load();
    o.tension = tensionRaw->load();
    o.chroma = chromaRaw->load();
    o.cycle = (int) cycleRaw->load();
    o.barsPerChord = (int) chordLenRaw->load();
    o.seed = seed < 0 ? (uint32_t) juce::Random::getSystemRandom().nextInt() : (uint32_t) seed;
    auto prog = th::generate (o);
    if (prog.empty()) return;
    if ((int) prog.size() > kMaxChords) prog.resize ((size_t) kMaxChords);
    bank.pats[pat].setProgression (prog);
    evoSub[pat] = 0u;
}

void ColorAudioProcessor::reharmonize (int pat, int op, int index, int64_t seed)
{
    auto prog = bank.pats[pat].progression();
    th::reharmonize (prog, mode(), op, index, seed < 0 ? (uint32_t) juce::Random::getSystemRandom().nextInt() : (uint32_t) seed);
    if ((int) prog.size() > kMaxChords) prog.resize ((size_t) kMaxChords);
    if (! prog.empty()) bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::reharmonizeAmount (int pat, float amount, int64_t seed)
{
    auto prog = bank.pats[pat].progression();
    th::reharmonizeAmount (prog, mode(), amount, seed < 0 ? (uint32_t) juce::Random::getSystemRandom().nextInt() : (uint32_t) seed);
    if ((int) prog.size() > kMaxChords) prog.resize ((size_t) kMaxChords);
    if (! prog.empty()) bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::varyProgression (int pat, float amount, int64_t seed)
{
    // variation douce : couleurs (qualites) et renversements, sans toucher aux fondamentales
    auto r = makeRandom (seed);
    auto prog = bank.pats[pat].progression();
    for (auto& c : prog)
    {
        if (r.nextFloat() < amount * 0.5f)
        {
            auto subs = th::substitutesFor (c, nullptr, mode());
            if (! subs.empty())
            {
                auto s = subs[(size_t) r.nextInt ((int) juce::jmin<size_t> (subs.size(), 4))];
                if (s.root == c.root) { s.beats = c.beats; c = s; }
            }
        }
        if (r.nextFloat() < amount * 0.3f) c.inversion = (int8_t) r.nextInt (3);
    }
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::simplifyProgression (int pat)
{
    auto prog = bank.pats[pat].progression();
    for (auto& c : prog)
    {
        switch (th::quality (c.qual).family)
        {
            case th::FMaj:     c.qual = th::QMaj; break;
            case th::FMin:     c.qual = th::QMin; break;
            case th::FDom:     c.qual = th::QDom7; break;
            case th::FHalfDim: c.qual = th::QHalfDim; break;
            case th::FDim:     c.qual = th::QDim; break;
            case th::FAug:     c.qual = th::QAug; break;
            case th::FSus:     c.qual = th::QSus4; break;
            default:
            {
                th::Chord id;
                if (th::identifyChord ((uint16_t) (th::chordMask (c) & 0xfff), -1, id)) { c.root = id.root; c.qual = id.qual; }
                break;
            }
        }
        c.bass = -1;
    }
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::enrichProgression (int pat)
{
    auto prog = bank.pats[pat].progression();
    juce::Random r;
    for (auto& c : prog)
    {
        switch (th::quality (c.qual).family)
        {
            case th::FMaj: c.qual = (int8_t) (c.qual == th::QMaj ? th::QMaj7 : (r.nextBool() ? th::QMaj9 : th::QSixNine)); break;
            case th::FMin: c.qual = (int8_t) (c.qual == th::QMin ? th::QMin7 : (r.nextBool() ? th::QMin9 : th::QMin11)); break;
            case th::FDom: c.qual = (int8_t) (c.qual == th::QDom7 ? th::QDom9 : th::QDom13); break;
            case th::FSus: c.qual = th::QSus9; break;
            case th::FDim: c.qual = th::QDim7; break;
            default: break;
        }
    }
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::transposeProgression (int pat, int degrees)
{
    // transposition diatonique : un accord diatonique reste diatonique (sa qualite suit le mode)
    const int sc = mode();
    const int n = th::scaleSize (sc);
    auto prog = bank.pats[pat].progression();
    for (auto& c : prog)
    {
        int deg = -1;
        for (int d = 0; d < n; ++d) if ((th::scaleDegreePc (sc, d) % 12) == c.root) { deg = d; break; }
        const bool diat = (th::chordMask (c) & ~[&] { uint16_t m = 0; for (int i = 0; i < 12; ++i) if (th::inScale (sc, i)) m |= (uint16_t) (1u << i); return m; }()) == 0;
        if (deg >= 0 && diat && n == 7)
        {
            const int size = th::quality (c.qual).n >= 4 ? 4 : 3;
            auto x = th::diatonic (sc, deg + degrees + 7 * 4, size);
            x.beats = c.beats; x.inversion = c.inversion;
            c = x;
        }
        else if (deg >= 0 && n == 7)
        {
            // accord non diatonique sur un degre : la fondamentale suit la gamme, la qualite est conservee
            const int newRoot = ((th::scaleDegreePc (sc, deg + degrees + 7 * 4) % 12) + 12) % 12;
            const int semis = newRoot - c.root;
            c.root = (int8_t) newRoot;
            if (c.bass >= 0) c.bass = (int8_t) ((c.bass + semis + 120) % 12);
        }
        else
        {
            const int semis = degrees * 12 / juce::jmax (1, n);
            c.root = (int8_t) ((c.root + semis + 120) % 12);
            if (c.bass >= 0) c.bass = (int8_t) ((c.bass + semis + 120) % 12);
        }
    }
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::rotateProgression (int pat, int dir)
{
    auto prog = bank.pats[pat].progression();
    if (prog.size() < 2) return;
    if (dir > 0) std::rotate (prog.begin(), prog.begin() + 1, prog.end());
    else std::rotate (prog.begin(), prog.end() - 1, prog.end());
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::reverseProgression (int pat)
{
    auto prog = bank.pats[pat].progression();
    std::reverse (prog.begin(), prog.end());
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::addCadence (int pat, int type)
{
    // cadences (Persichetti / harmonie classique) : parfaite, plagale, rompue, demi-cadence, backdoor, modale
    const int sc = mode();
    auto prog = bank.pats[pat].progression();
    const int beats = prog.empty() ? 4 : prog.back().beats;
    const bool cx = complexRaw->load() > 0.4f;
    auto tonic = th::diatonic (th::scaleSize (sc) == 7 ? sc : 5, 0, cx ? 4 : 3);
    auto mk = [beats] (int root, int q) { th::Chord c; c.root = (int8_t) ((root + 120) % 12); c.qual = (int8_t) q; c.beats = (int8_t) beats; return c; };
    std::vector<th::Chord> cad;
    tonic.beats = (int8_t) beats;
    const bool minor = th::quality (tonic.qual).family == th::FMin;
    switch (type)
    {
        case 0: cad = { mk (7, cx ? th::QDom9 : th::QDom7), tonic }; break;                                         // V7 - I
        case 1: cad = { mk (5, minor ? (cx ? th::QMin7 : th::QMin) : (cx ? th::QMaj7 : th::QMaj)), tonic }; break;  // IV - I
        case 2: cad = { mk (7, th::QDom7), mk (minor ? 8 : 9, minor ? th::QMaj7 : th::QMin7) }; break;             // V - vi (rompue)
        case 3: cad = { mk (minor ? 5 : 2, minor ? th::QMin7 : th::QMin7), mk (7, th::QSus7) }; break;          // ii - V (demi-cadence)
        case 4: cad = { mk (5, th::QMin7), mk (10, th::QDom7), tonic }; break;                                     // iv - bVII7 - I (backdoor)
        default: cad = { mk (10, cx ? th::QMaj9 : th::QMaj), tonic }; break;                                       // bVII - i (modale, eolien/mixolydien)
    }
    const int keep = juce::jmax (0, juce::jmin ((int) prog.size(), kMaxChords) - (int) cad.size());
    prog.resize ((size_t) keep);
    for (auto& c : cad) if ((int) prog.size() < kMaxChords) prog.push_back (c);
    bank.pats[pat].setProgression (prog);
}

// Modulation : le pattern suivant recoit la meme grille dans la nouvelle tonalite, et la fin de ce pattern
// prepare l'arrivee (dominante, ii-V, accord pivot ou modulation directe).
void ColorAudioProcessor::modulateToNext (int pat, int semis, int method)
{
    const int dst = (pat + 1) % kPatterns;
    auto& P = bank.pats[pat];
    auto& D = bank.pats[dst];
    D.copyFrom (P);
    int ns = P.keyShift.load() + semis;
    ns = ((ns % 12) + 12) % 12; if (ns > 6) ns -= 12;
    D.keyShift = (int8_t) ns;
    auto prog = P.progression();
    const int beats = prog.empty() ? 4 : prog.back().beats;
    auto add = th::modulationChords (mode(), semis, method, method == 1 && beats >= 4 ? beats / 2 : beats);
    if (add.empty()) return;
    int keep = (int) prog.size();
    int need = 0; for (auto& c : add) need += c.beats;
    // on remplace la fin de la grille (memes durees totales)
    int removed = 0;
    while (keep > 1 && removed < need) { removed += prog[(size_t) keep - 1].beats; --keep; }
    prog.resize ((size_t) keep);
    for (auto& c : add) if ((int) prog.size() < kMaxChords) prog.push_back (c);
    P.setProgression (prog);
}

void ColorAudioProcessor::setChordAt (int pat, int index, const th::Chord& c)
{
    auto& P = bank.pats[pat];
    if (index >= 0 && index < P.chordCount()) P.setChord (index, c);
}

void ColorAudioProcessor::insertChord (int pat, int index, const th::Chord& c)
{
    auto prog = bank.pats[pat].progression();
    if ((int) prog.size() >= kMaxChords) return;
    index = juce::jlimit (0, (int) prog.size(), index);
    prog.insert (prog.begin() + index, c);
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::removeChord (int pat, int index)
{
    auto prog = bank.pats[pat].progression();
    if (prog.size() <= 1 || index < 0 || index >= (int) prog.size()) return;
    prog.erase (prog.begin() + index);
    bank.pats[pat].setProgression (prog);
}

void ColorAudioProcessor::setChordLengths (int pat, int beats)
{
    auto prog = bank.pats[pat].progression();
    for (auto& c : prog) c.beats = (int8_t) juce::jlimit (1, 32, beats);
    bank.pats[pat].setProgression (prog);
}

juce::String ColorAudioProcessor::progressionText (int pat) const
{
    juce::StringArray a;
    const auto& P = bank.pats[pat];
    const int defBeats = chordLenRaw->load() < 0.5f ? 2 : (chordLenRaw->load() > 1.5f ? 8 : 4);
    for (int i = 0; i < P.chordCount(); ++i)
    {
        const auto c = P.chord (i);
        auto s = th::chordName (c, keyRootFor (pat));
        if (c.beats != defBeats) s << ":" << (int) c.beats;
        a.add (s);
    }
    return a.joinIntoString (" ");
}

namespace
{
    int parseNote (const juce::String& s, int& used)
    {
        static const int base[7] { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        used = 0;
        if (s.isEmpty()) return -1;
        const auto c = juce::CharacterFunctions::toUpperCase (s[0]);
        if (c < 'A' || c > 'G') return -1;
        int pc = base[c - 'A'];
        used = 1;
        while (used < s.length() && (s[used] == '#' || s[used] == 'b'))
        {
            // juste apres la lettre, b / # sont des alterations (aucune qualite ne commence par b)
            pc += s[used] == '#' ? 1 : -1;
            ++used;
        }
        return (pc + 12) % 12;
    }

    int parseQuality (juce::String q)
    {
        q = q.trim();
        static const char* alias[][2] {
            { "maj", "" }, { "M", "" }, { "min", "m" }, { "-", "m" }, { "M7", "maj7" }, { "Maj7", "maj7" }, { "^7", "maj7" }, { "Δ", "maj7" },
            { "-7", "m7" }, { "min7", "m7" }, { "ø", "m7b5" }, { "o", "dim" }, { "°", "dim" }, { "o7", "dim7" }, { "°7", "dim7" },
            { "+", "aug" }, { "sus", "sus4" }, { "7sus", "7sus4" }, { "M9", "maj9" }, { "-9", "m9" }, { "69", "6/9" }, { "mM7", "m(maj7)" },
            { "m(M7)", "m(maj7)" }, { "11", "m11" }, { "alt", "7alt" }, { "7#5", "aug" } };
        for (auto& a : alias) if (q == juce::String::fromUTF8 (a[0])) { q = a[1]; break; }
        for (int i = 0; i < th::numQualities(); ++i) if (q == th::quality (i).sym) return i;
        return -1;
    }
}

bool ColorAudioProcessor::setProgressionText (int pat, const juce::String& text)
{
    auto tokens = juce::StringArray::fromTokens (text.replace (",", " ").replace ("|", " ").replace ("-", " -").replace (" -7", " m7"), " \t\n", "");
    tokens.removeEmptyStrings();
    std::vector<th::Chord> prog;
    const int key = keyRootFor (pat);
    const int defBeats = chordLenRaw->load() < 0.5f ? 2 : (chordLenRaw->load() > 1.5f ? 8 : 4);
    for (auto tok : tokens)
    {
        if (tok == "-") continue;
        int beats = defBeats;
        if (tok.containsChar (':')) { beats = juce::jlimit (1, 32, tok.fromLastOccurrenceOf (":", false, false).getIntValue()); tok = tok.upToLastOccurrenceOf (":", false, false); }
        int used = 0;
        const int root = parseNote (tok, used);
        if (root < 0) return false;
        juce::String rest = tok.substring (used);
        int bassPc = -1;
        const int slash = rest.lastIndexOfChar ('/');
        if (slash >= 0)
        {
            int u2 = 0;
            const int b = parseNote (rest.substring (slash + 1), u2);
            if (b >= 0 && u2 == rest.length() - slash - 1) { bassPc = b; rest = rest.substring (0, slash); }
        }
        const int q = parseQuality (rest);
        if (q < 0) return false;
        th::Chord c;
        c.root = (int8_t) ((root - key + 12) % 12);
        c.qual = (int8_t) q;
        c.beats = (int8_t) beats;
        if (bassPc >= 0)
        {
            const int rel = (bassPc - key + 12) % 12;
            c.bass = rel == c.root ? (int8_t) -1 : (int8_t) rel;
        }
        prog.push_back (c);
        if ((int) prog.size() >= kMaxChords) break;
    }
    if (prog.empty()) return false;
    bank.pats[pat].setProgression (prog);
    evoSub[pat] = 0u;
    return true;
}

// ---------------------------------------------------------------------------
// Rythmes
void color::applyRhythmPreset (CPattern& p, int preset, uint32_t seed)
{
    p.clearRhythm();
    p.rate = 1; p.dir = 0; p.groove = 0; p.length = 16;
    auto S = [&p] (int i, int len, int gate = 90, int vel = 100, int acc = 0, int part = 0, int strum = 0)
    {
        p.steps[i].set (1, vel, acc, len, gate, 100, 1, 0, 0, part, strum, 0);
    };
    juce::Random r ((juce::int64) seed * 7919 + preset);
    switch (preset)
    {
        case 0: S (0, 16, 100, 100); break;                                                         // ronde
        case 1: for (int i = 0; i < 16; i += 4) S (i, 3, 95, i == 0 ? 110 : 92, i == 0); break;      // noires
        case 2: for (int i = 2; i < 16; i += 4) S (i, 1, 60, i == 6 || i == 14 ? 112 : 96, i == 6 || i == 14); break;  // contretemps
        case 3: S (0, 5, 85, 110, 1); S (6, 2, 70, 100); S (8, 5, 85, 104); S (14, 2, 70, 96); break; // charleston
        case 4: p.length = 32; for (int i : { 0, 6, 12, 20, 24 }) S (i, 2, 70, i == 0 ? 112 : 98, i == 0 || i == 20); break; // clave 3-2
        case 5: S (0, 6, 90, 108, 1); S (6, 4, 85, 100); S (10, 4, 80, 98); S (14, 2, 90, 106, 1); break; // anticipations
        case 6: for (int i = 0; i < 16; ++i) S (i, 1, 70, (i % 4) == 0 ? 112 : 84, (i % 4) == 0); break; // arpege en doubles
        case 7: for (int i : { 2, 5, 8, 10, 13 }) S (i, 1, 40, i == 2 || i == 10 ? 110 : 92, i == 10); break;  // house chops
        case 8:                                                                                     // euclide
        {
            const int k = 3 + r.nextInt (6), rot = r.nextInt (16);
            for (int i = 0; i < 16; ++i)
                if (((i * k) % 16) < k) S ((i + rot) % 16, 1 + r.nextInt (3), 70, 96 + r.nextInt (20));
            break;
        }
        default:                                                                                    // aleatoire
        {
            int i = 0;
            while (i < 16)
            {
                const int len = 1 + r.nextInt (r.nextFloat() < 0.5f ? 2 : 6);
                if (r.nextFloat() < 0.6f || i == 0) S (i, len, 60 + r.nextInt (40), 80 + r.nextInt (40), r.nextFloat() < 0.2f);
                i += len;
            }
            break;
        }
    }
}

void ColorAudioProcessor::applyRhythm (int pat, int preset, int64_t seed)
{
    applyRhythmPreset (bank.pats[pat], preset, seed < 0 ? (uint32_t) juce::Random::getSystemRandom().nextInt() : (uint32_t) seed);
    evoMask[pat] = 0u;
}

void ColorAudioProcessor::generateRhythm (int pat, int64_t seed)
{
    auto r = makeRandom (seed);
    // rythmes idiomatiques par style (Dobbins : comping jazz, latin ; house / techno ; gospel ; cinematique)
    static const int byStyle[th::kNumStyles][4] {
        { 1, 5, 2, 5 },   // Pop
        { 1, 0, 3, 1 },   // Fonctionnel
        { 3, 5, 3, 4 },   // Jazz
        { 0, 5, 7, 2 },   // Modal
        { 0, 0, 1, 6 },   // Cinematique
        { 7, 2, 7, 8 },   // Techno / Deep
        { 1, 5, 3, 6 },   // Gospel
        { 3, 1, 5, 2 },   // Blues
        { 0, 6, 1, 0 },   // Impressionniste
        { 8, 9, 0, 8 },   // Atonal
        { 0, 6, 5, 1 },   // Lydien
        { 9, 8, 4, 7 },   // Aleatoire
        { 3, 5, 1, 7 },   // Neo Soul
        { 4, 3, 5, 4 } }; // Bossa
    const int st = juce::jlimit (0, th::kNumStyles - 1, (int) styleRaw->load());
    applyRhythmPreset (bank.pats[pat], byStyle[st][r.nextInt (4)], (uint32_t) r.nextInt());
    mutateRhythm (pat, 0.08f);
    evoMask[pat] = 0u;
}

void ColorAudioProcessor::mutateRhythm (int pat, float amount)
{
    juce::Random r;
    auto& P = bank.pats[pat];
    const int len = P.length.load();
    for (int i = 0; i < len; ++i)
    {
        auto& s = P.steps[i];
        if (r.nextFloat() < amount * 0.4f) s.on = (int8_t) (s.on ? 0 : 1);
        if (s.on && r.nextFloat() < amount * 0.5f) s.vel = (int8_t) juce::jlimit (30, 127, (int) s.vel + r.nextInt (41) - 20);
        if (s.on && r.nextFloat() < amount * 0.3f) s.len = (int8_t) juce::jlimit (1, 16, (int) s.len + (r.nextBool() ? 1 : -1));
        if (s.on && r.nextFloat() < amount * 0.15f) s.part = (int8_t) r.nextInt (4);
    }
}

void ColorAudioProcessor::shiftPattern (int pat, int dir)
{
    auto& P = bank.pats[pat];
    const int len = P.length.load();
    CPattern tmp; tmp.copyFrom (P);
    for (int i = 0; i < len; ++i) P.steps[(i + dir + len) % len].copyFrom (tmp.steps[i]);
}

void ColorAudioProcessor::euclidPattern (int pat, int hitsN, int rot)
{
    auto& P = bank.pats[pat];
    const int len = juce::jmax (1, (int) P.length.load());
    hitsN = juce::jlimit (0, len, hitsN);
    for (int i = 0; i < len; ++i)
    {
        const bool on = hitsN > 0 && ((i * hitsN) % len) < hitsN;
        auto& s = P.steps[(i + rot + len * 4) % len];
        if (on) s.set (1, s.vel > 0 ? s.vel.load() : 100, 0, juce::jmax (1, len / juce::jmax (1, hitsN) - 1), 85);
        else s.on = 0;
    }
}

void ColorAudioProcessor::breedPatterns (int a, int b, int dst, int64_t seed)
{
    auto r = makeRandom (seed);
    CPattern A, B; A.copyFrom (bank.pats[a]); B.copyFrom (bank.pats[b]);
    auto& D = bank.pats[dst];
    D.copyRhythmFrom (r.nextBool() ? A : B);
    for (int i = 0; i < kSteps; ++i) if (r.nextBool()) D.steps[i].copyFrom ((r.nextBool() ? A : B).steps[i]);
    // harmonie : moitie de chaque progression
    auto pa = A.progression(), pb = B.progression();
    std::vector<th::Chord> out;
    const int n = juce::jmax ((int) pa.size(), (int) pb.size());
    for (int i = 0; i < n && (int) out.size() < kMaxChords; ++i)
    {
        const bool useA = i < n / 2 ? true : false;
        const auto& src = useA ? pa : pb;
        if (i < (int) src.size()) out.push_back (src[(size_t) i]);
        else if (i < (int) (useA ? pb : pa).size()) out.push_back ((useA ? pb : pa)[(size_t) i]);
    }
    if (! out.empty()) D.setProgression (out);
}

ColorAudioProcessor::Preview ColorAudioProcessor::previewNextBar() const
{
    Preview pv;
    const bool run = isRunning.load();
    const double from = run ? playPpq.load() : 0.0;
    const int pat = currentPattern.load();
    auto ctx = makeContext (pat, run ? patStartShared.load() : 0.0, bpmRaw->load());
    ColorCondState cs;
    runColorRhythm (bank, ctx, &cs, from, from + 4.0, [&] (double, ColorHit e)
    {
        if (e.step >= 0) (e.ghost ? pv.ghost : pv.play) |= 1u << e.step;
    });
    const auto& P = bank.pats[pat];
    const uint32_t evo = evoMask[pat].load();
    for (int i = 0; i < P.length; ++i)
    {
        const bool on = (P.steps[i].on != 0) != (((evo >> i) & 1u) != 0);
        if (on && ! ((pv.play >> i) & 1u)) pv.skip |= 1u << i;
    }
    return pv;
}

// ---------------------------------------------------------------------------
// Export MIDI (rendu hors ligne de ce que joue COLOR : accords, rythme, arpege)
juce::File ColorAudioProcessor::exportMidi (int pat, int bars)
{
    constexpr int ppqTicks = 960;
    const double bpm = bpmRaw->load();
    const int pm = (int) playModeRaw->load();
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Color"), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0.0);
    const double total = bars * 4.0;
    int prev[8]; int prevNn = 0;
    auto voiceAt = [&] (double t, int* notes)
    {
        const auto c = effectiveChord (pat, 0.0, t + 1.0e-6);
        auto o = voiceOptions (hash3 ((uint32_t) chordPosAt (pat, 0.0, t + 1.0e-6).occurrence, (uint32_t) seedRaw->load(), 0x701Cu));
        o.keyRoot = keyRootFor (pat);
        const int n = th::voiceChord (c, o, prevNn > 0 ? prev : nullptr, prevNn, notes, 8);
        prevNn = 0;
        for (int i = o.bassMode > 0 ? 1 : 0; i < n; ++i) prev[prevNn++] = notes[i];
        return n;
    };
    auto add = [&] (int note, double t0, double t1, int vel)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, note), (juce::uint8) juce::jlimit (1, 127, vel)), std::round (t0 * ppqTicks));
        seq.addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, note)), std::round (juce::jmax (t0 + 0.01, t1) * ppqTicks) - 1);
    };
    // accords successifs (frontieres)
    std::vector<std::pair<double, double>> spans;
    for (double t = 0.0; t < total - 1.0e-6;)
    {
        const auto p = chordPosAt (pat, 0.0, t + 1.0e-6);
        double end = juce::jmin (p.end, total);
        bool appr = false;
        effectiveChord (pat, 0.0, p.end - 0.5, nullptr, &appr);
        if (appr && p.end - 1.0 > t + 1.0e-6 && p.end - 1.0 < end) end = p.end - 1.0;
        spans.push_back ({ t, end });
        t = end;
    }
    if (pm == 0)
    {
        for (auto& sp : spans)
        {
            int notes[8]; const int n = voiceAt (sp.first, notes);
            for (int i = 0; i < n; ++i) add (notes[i], sp.first, sp.second, 92);
        }
    }
    else
    {
        auto ctx = makeContext (pat, 0.0, bpm);
        ColorCondState cs;
        std::vector<ColorHit> hv;
        runColorRhythm (bank, ctx, &cs, 0.0, total, [&] (double te, ColorHit e) { e.ppq = te; hv.push_back (e); });
        std::sort (hv.begin(), hv.end(), [] (const ColorHit& a, const ColorHit& b) { return a.ppq < b.ppq; });
        size_t si = 0;
        int notes[8]; int n = 0; int arpC = 0;
        const bool hasBass = (int) bassModeRaw->load() > 0;
        for (auto& h : hv)
        {
            bool changed = false;
            while (si < spans.size() && spans[si].first <= h.ppq + 1.0e-9) { n = voiceAt (spans[si].first, notes); ++si; changed = true; arpC = 0; if (pm == 3 && hasBass) add (notes[0], spans[si - 1].first, spans[si - 1].second, 90); }
            juce::ignoreUnused (changed);
            const int vel = h.accent ? 120 : (int) (h.vel * 110.0f);
            if (pm == 3)
            {
                const int lo = hasBass ? 1 : 0;
                const int cnt = n - lo;
                if (cnt <= 0) continue;
                const int k = arpC++;
                add (notes[lo + k % cnt] + 12 * ((k / cnt) % juce::jmax (1, (int) arpOctRaw->load())), h.ppq, h.ppq + h.gatePpq, vel);
            }
            else
                for (int i = 0; i < n; ++i)
                {
                    if (h.part == 1 && i != n - 1) continue;
                    if (h.part == 2 && i != 0) continue;
                    if (h.part == 3 && hasBass && i == 0) continue;
                    const double st = h.ppq + (pm == 2 ? i * 0.02 : 0.0);
                    add (notes[i], st, h.ppq + h.gatePpq, vel);
                }
        }
    }
    seq.sort();
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppqTicks);
    mf.addTrack (seq);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Color").getChildFile ("MIDI");
    dir.createDirectory();
    auto f = dir.getChildFile ("Color Pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + pat)) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) { mf.writeTo (os); os.flush(); }
    return f;
}

// Lecture des notes d'un fichier MIDI (temps en noires), batterie (canal 10) exclue
namespace
{
    struct MNote { double a, b; int note; };
    bool readMidiNotes (const juce::File& file, std::vector<MNote>& notes)
    {
        juce::FileInputStream in (file);
        if (! in.openedOk()) return false;
        juce::MidiFile mf;
        if (! mf.readFrom (in)) return false;
        const double tpq = mf.getTimeFormat() > 0 ? (double) mf.getTimeFormat() : 960.0;
        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence s (*mf.getTrack (t));
            s.updateMatchedPairs();
            for (int i = 0; i < s.getNumEvents(); ++i)
            {
                auto* e = s.getEventPointer (i);
                if (! e->message.isNoteOn() || e->message.getChannel() == 10) continue;
                const double a = e->message.getTimeStamp() / tpq;
                const double b = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : a + 0.5;
                notes.push_back ({ a, juce::jmax (a + 0.05, b), e->message.getNoteNumber() });
            }
        }
        return ! notes.empty();
    }

    // tonalite par correlation avec les profils de Krumhansl-Kessler
    void detectKey (const std::vector<MNote>& notes, int& key, bool& minor)
    {
        float hist[12] {};
        for (auto& n : notes) hist[n.note % 12] += (float) (n.b - n.a);
        static const float majP[12] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
        static const float minP[12] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
        float best = -1.0e9f; key = 0; minor = false;
        for (int k = 0; k < 12; ++k)
            for (int m = 0; m < 2; ++m)
            {
                float s = 0.0f;
                for (int i = 0; i < 12; ++i) s += hist[(i + k) % 12] * (m ? minP[i] : majP[i]);
                if (s > best) { best = s; key = k; minor = m == 1; }
            }
    }

    // proportion du temps ou une seule note sonne : > 0.8 = melodie
    float monophony (const std::vector<MNote>& notes)
    {
        double one = 0, more = 0;
        for (auto& n : notes)
        {
            int over = 0;
            for (auto& m : notes) if (&m != &n && m.a < n.b - 0.05 && m.b > n.a + 0.05) ++over;
            (over == 0 ? one : more) += n.b - n.a;
        }
        return (float) (one / juce::jmax (1.0e-6, one + more));
    }
}

bool ColorAudioProcessor::isMelodyFile (const juce::File& f)
{
    std::vector<MNote> notes;
    return readMidiNotes (f, notes) && monophony (notes) > 0.8f;
}

// Harmonisation d'une melodie MIDI : tonalite estimee, une fenetre par accord (duree reglee dans HARMONIE,
// allongee si la melodie depasse 16 accords), meilleur chemin d'accords (theory::harmonizeLine).
bool ColorAudioProcessor::harmonizeMelodyFile (const juce::File& file, int pat)
{
    std::vector<MNote> notes;
    if (! readMidiNotes (file, notes)) return false;
    int key = 0; bool minor = false;
    detectKey (notes, key, minor);
    double endT = 0.0; for (auto& n : notes) endT = juce::jmax (endT, n.b);
    const int cl = (int) chordLenRaw->load();
    int win = cl == 0 ? 2 : (cl == 2 ? 8 : 4);
    while (std::ceil (endT / win - 1.0e-6) > kMaxChords && win < 32) win *= 2;
    const int nW = juce::jlimit (1, kMaxChords, (int) std::ceil (endT / win - 1.0e-6));
    const int sc = minor ? (inScaleMode (mode(), true) ? mode() : 5) : (inScaleMode (mode(), false) ? mode() : 0);
    std::vector<std::array<float, 12>> w ((size_t) nW), st ((size_t) nW);
    for (auto& x : w) x.fill (0.0f);
    for (auto& x : st) x.fill (0.0f);
    for (auto& n : notes)
    {
        const int pc = ((n.note - key) % 12 + 12) % 12;
        for (int i = 0; i < nW; ++i)
        {
            const double a = i * (double) win, b = a + win;
            const double ov = juce::jmin (n.b, b) - juce::jmax (n.a, a);
            if (ov <= 0.0) continue;
            w[(size_t) i][(size_t) pc] += (float) ov;
            // temps forts : debut de fenetre et temps 1 / 3 de chaque mesure
            const double pos = n.a - a;
            if (n.a >= a - 1.0e-6 && n.a < b && (std::abs (pos) < 0.01 || std::abs (std::fmod (n.a, 2.0)) < 0.01))
                st[(size_t) i][(size_t) pc] += (float) juce::jmin (2.0, n.b - n.a) * (std::abs (pos) < 0.01 ? 1.5f : 1.0f);
        }
    }
    auto prog = th::harmonizeLine (w, st, sc, complexRaw->load(), tensionRaw->load(), win);
    if (prog.empty()) return false;
    if ((int) prog.size() > kMaxChords) prog.resize ((size_t) kMaxChords);
    pushUndo();
    setParamReal ("root", (float) key);
    setParamReal ("mode", (float) sc);
    bank.pats[pat].setProgression (prog);
    evoSub[pat] = 0u;
    // la melodie est rangee dans le pattern suivant comme reference ? non : on garde le rythme courant
    return true;
}

bool ColorAudioProcessor::inScaleMode (int m, bool minor)
{
    const bool isMinor = th::inScale (m, 3) && ! th::inScale (m, 4);
    return th::scaleSize (m) == 7 && isMinor == minor;
}

// Rythme d'apres la batterie d'Alea (AleaLink) : 0 = entre les kicks, 1 = avec la caisse claire, 2 = contretemps du charleston
bool ColorAudioProcessor::rhythmFromDrums (int pat, int mode2)
{
    if (link == nullptr) return false;
    const int slot = link->firstPeerOfKind (alea::link::KindAlea);
    if (slot < 0) return false;
    const int track = mode2 == 0 ? 0 : (mode2 == 1 ? 1 : 2);
    uint32_t mask = 0; int len = 16, rate = 1;
    if (! link->readTrack (slot, track, mask, len, rate)) return false;
    len = juce::jlimit (1, kSteps, len);
    auto& P = bank.pats[pat];
    P.clearRhythm();
    P.length = (int8_t) len; P.rate = (int8_t) juce::jlimit (0, kNumRates - 1, rate);
    for (int i = 0; i < len; ++i)
    {
        const bool hit = ((mask >> i) & 1u) != 0;
        if (mode2 == 0)
        {
            // entre les kicks : le pas juste apres un kick est laisse libre, le coup suivant tient jusqu'au kick
            if (hit) continue;
            const bool prevKick = ((mask >> ((i + len - 1) % len)) & 1u) != 0;
            if (prevKick && i % 2 == 1) continue;
            if (i % 2 == 0 || ((mask >> ((i + 1) % len)) & 1u) != 0)
            {
                int hold = 1;
                while (hold < 4 && ! ((mask >> ((i + hold) % len)) & 1u)) ++hold;
                P.steps[i].set (1, i % 4 == 2 ? 110 : 92, i % 4 == 2, hold, 80);
            }
        }
        else if (hit) P.steps[i].set (1, 104, mode2 == 1, mode2 == 1 ? 2 : 1, mode2 == 1 ? 70 : 45);
    }
    evoMask[pat] = 0u;
    return true;
}

// Analyse harmonique d'un fichier MIDI : tonalite (profils de Krumhansl), puis accords par temps,
// fusion des accords identiques consecutifs (au plus 16 accords).
bool ColorAudioProcessor::learnChordsFromMidi (const juce::File& file, int pat)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) return false;
    juce::MidiFile mf;
    if (! mf.readFrom (in)) return false;
    const double tpq = mf.getTimeFormat() > 0 ? (double) mf.getTimeFormat() : 960.0;
    struct N { double a, b; int note; };
    std::vector<N> notes;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence s (*mf.getTrack (t));
        s.updateMatchedPairs();
        for (int i = 0; i < s.getNumEvents(); ++i)
        {
            auto* e = s.getEventPointer (i);
            if (! e->message.isNoteOn() || e->message.getChannel() == 10) continue;
            const double a = e->message.getTimeStamp() / tpq;
            const double b = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : a + 0.5;
            notes.push_back ({ a, juce::jmax (a + 0.05, b), e->message.getNoteNumber() });
        }
    }
    if (notes.empty()) return false;
    double endT = 0.0; for (auto& n : notes) endT = juce::jmax (endT, n.b);
    const int totalBeats = juce::jlimit (1, 512, (int) std::ceil (endT - 1.0e-3));

    // tonalite
    float hist[12] {};
    for (auto& n : notes) hist[n.note % 12] += (float) (n.b - n.a);
    static const float majP[12] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float minP[12] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    float best = -1.0e9f; int key = 0; bool minor = false;
    for (int k = 0; k < 12; ++k)
        for (int m = 0; m < 2; ++m)
        {
            float s = 0.0f;
            for (int i = 0; i < 12; ++i) s += hist[(i + k) % 12] * (m ? minP[i] : majP[i]);
            if (s > best) { best = s; key = k; minor = m == 1; }
        }

    // accords par fenetre (1 temps, puis 2 ou 4 si trop d'accords)
    for (int win : { 1, 2, 4, 8 })
    {
        std::vector<th::Chord> prog;
        for (int b = 0; b < totalBeats; b += win)
        {
            float w[12] {}; int low = 128;
            for (auto& n : notes)
            {
                const double ov = juce::jmin (n.b, (double) (b + win)) - juce::jmax (n.a, (double) b);
                if (ov <= 0.0) continue;
                w[n.note % 12] += (float) ov;
                if (n.a <= b + 0.25 * win) low = juce::jmin (low, n.note);
            }
            float mx = 0.0f; for (float v : w) mx = juce::jmax (mx, v);
            th::Chord c;
            if (mx <= 0.0f) { if (! prog.empty()) { prog.back().beats = (int8_t) juce::jmin (32, prog.back().beats + win); } continue; }
            uint16_t mask = 0;
            for (int i = 0; i < 12; ++i) if (w[i] >= mx * 0.3f) mask |= (uint16_t) (1u << ((i - key + 12) % 12));
            if (! th::identifyChord (mask, low < 128 ? (low - key + 120) % 12 : -1, c)) continue;
            c.beats = (int8_t) win;
            if (! prog.empty() && prog.back().root == c.root && prog.back().qual == c.qual && prog.back().bass == c.bass && prog.back().beats + win <= 32)
                prog.back().beats = (int8_t) (prog.back().beats + win);
            else prog.push_back (c);
        }
        if (prog.empty()) continue;
        if ((int) prog.size() <= kMaxChords || win == 8)
        {
            if ((int) prog.size() > kMaxChords) prog.resize ((size_t) kMaxChords);
            pushUndo();
            setParamReal ("root", (float) key);
            setParamReal ("mode", (float) (minor ? 5 : 0));
            bank.pats[pat].keyShift = 0;
            bank.pats[pat].setProgression (prog);
            evoSub[pat] = 0u;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Banque par defaut et presets d'usine
void ColorAudioProcessor::initDefaultBank()
{
    // Am9 - Fmaj7 - Cmaj7/E - G6 (relatif a la tonique : la, mode eolien)
    std::vector<th::Chord> prog;
    auto mk = [] (int root, int q, int bass = -1) { th::Chord c; c.root = (int8_t) root; c.qual = (int8_t) q; c.bass = (int8_t) bass; c.beats = 4; return c; };
    prog = { mk (0, th::QMin9), mk (8, th::QMaj7), mk (3, th::QMaj7, 7), mk (10, th::QSix) };
    for (int p = 0; p < kPatterns; ++p)
    {
        bank.pats[p].setProgression (prog);
        applyRhythmPreset (bank.pats[p], 2, 1u);
    }
}

void ColorAudioProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        {
            const auto& id = wp->paramID;
            if (id == "play" || id == "pattern" || id.startsWith ("lk_") || id == "root" || id == "scale" || id == "mode" || id == "bpm") continue;
            p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture();
        }
    initDefaultBank();
    for (auto& l : locks) l = false;
    hasB = false;
    for (auto& v : sceneValid) v = false;
    for (auto& f : favs) f.valid = false;
    resetEvolution();
    setChainText ("A4 B4");
}

juce::StringArray ColorAudioProcessor::factoryPresetNames()
{
    return { "Init Pads", "Neo Soul Keys", "Deep House Stabs", "Cinematic Strings", "Gospel Organ", "Lydian Glass",
             "Impressionist Planing", "Blues Comp", "Dub Techno Chord", "Atonal Clusters", "Arp Dreams", "Strum Guitar",
             "Neo Soul Rhodes", "Bossa Nylon" };
}

void ColorAudioProcessor::loadFactoryPreset (int index)
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
    int rhythm = 2;
    bool gen = true;
    switch (index)
    {
        case 1:   // Neo Soul Keys : FM type Rhodes, rootless, jazz II-V, comp syncope
            P ("osc1type", 4); P ("osc2type", 3); P ("ftype", 2); P ("style", th::SJazz); P ("mode", 1); P ("voicing", th::VRootlessA);
            P ("voices", 4); P ("playmode", 1); P ("complex", 0.7f); P ("tension", 0.35f); P ("chordlen", 1); P ("nchords", 4);
            V (FM, 0.32f); V (O2Semi, 0.75f); V (O2Level, 0.15f); V (Cutoff, 0.62f); V (EnvMod, 0.25f); V (Attack, 0.02f); V (Decay, 0.62f);
            V (Sustain, 0.35f); V (Release, 0.45f); V (VelFilt, 0.6f); V (SendRev, 0.25f); P ("chmix", 0.45f); P ("humanize", 0.35f);
            J (FM, 0.1f);
            rhythm = 3;
            break;
        case 2:   // Deep House Stabs
            P ("osc1type", 0); P ("osc2type", 1); P ("ftype", 1); P ("style", th::STechno); P ("mode", 1); P ("voicing", th::VDrop2);
            P ("playmode", 1); P ("complex", 0.6f); P ("nchords", 2); P ("chordlen", 1); P ("bassmode", 0);
            V (O2Fine, 0.6f); V (O2Level, 0.4f); V (Cutoff, 0.42f); V (Reso, 0.35f); V (EnvMod, 0.45f); V (FDecay, 0.35f); V (Attack, 0.0f);
            V (Decay, 0.45f); V (Sustain, 0.15f); V (Release, 0.3f); V (SendDly, 0.35f); V (SendRev, 0.3f);
            P ("dlyfb", 0.45f); P ("dlydiv", 7); P ("duckamt", 0.4f);
            J (Cutoff, 0.08f);
            MX (0, 2, Cutoff, 0.3f);
            rhythm = 7;
            break;
        case 3:   // Cinematic Strings
            P ("osc1type", 5); P ("ftype", 2); P ("style", th::SCinematic); P ("mode", 5); P ("voicing", th::VSpread); P ("voices", 5);
            P ("playmode", 0); P ("chordlen", 2); P ("nchords", 4); P ("spread", 0.8f); P ("glidemode", 1);
            V (Shape, 0.75f); V (Cutoff, 0.5f); V (Reso, 0.1f); V (EnvMod, 0.1f); V (Attack, 0.62f); V (Decay, 0.9f); V (Sustain, 0.9f);
            V (Release, 0.7f); V (Glide, 0.6f); V (SendRev, 0.55f); V (Width, 0.95f);
            P ("chmix", 0.5f); P ("revsize", 0.85f);
            MX (0, 5, Cutoff, 0.15f); P ("m5rate", 3);
            break;
        case 4:   // Gospel Organ
            P ("osc1type", 4); P ("osc2type", 3); P ("subtype", 0); P ("ftype", 5); P ("style", th::SGospel); P ("mode", 0); P ("voicing", th::VDrop2);
            P ("voices", 5); P ("playmode", 1); P ("complex", 0.6f); P ("chordlen", 0); P ("nchords", 8);
            V (O2Semi, 0.75f); V (O2Level, 0.45f); V (SubLevel, 0.3f); V (Attack, 0.01f); V (Decay, 0.9f); V (Sustain, 0.95f); V (Release, 0.15f);
            V (Drive, 0.2f); P ("drivetype", 3); P ("chmix", 0.6f); P ("chrate", 0.6f);
            rhythm = 5;
            break;
        case 5:   // Lydian Glass : arpege dans la gamme parente (Russell)
            P ("osc1type", 7); P ("ftype", 2); P ("style", th::SLydian); P ("mode", 3); P ("voicing", th::VQuartal); P ("playmode", 3);
            P ("arpmode", 4); P ("arpoct", 2); P ("outgoing", 0.35f); P ("chordlen", 2);
            V (Shape, 0.4f); V (Cutoff, 0.7f); V (Decay, 0.55f); V (Sustain, 0.0f); V (Release, 0.5f); V (SendDly, 0.4f); V (SendRev, 0.45f);
            P ("dlyfb", 0.5f); P ("dlydiv", 7);
            rhythm = 6;
            break;
        case 6:   // Impressionist Planing (Persichetti / Debussy)
            P ("osc1type", 3); P ("osc2type", 4); P ("ftype", 2); P ("style", th::SImpression); P ("mode", 3); P ("voicing", th::VClose);
            P ("playmode", 0); P ("chordlen", 1); P ("nchords", 6); P ("complex", 0.55f); P ("glidemode", 1);
            V (O2Level, 0.3f); V (O2Semi, 0.75f); V (Cutoff, 0.58f); V (Attack, 0.45f); V (Release, 0.6f); V (Glide, 0.45f); V (SendRev, 0.5f);
            P ("chmix", 0.4f);
            break;
        case 7:   // Blues Comp (shells, charleston)
            P ("osc1type", 2); P ("ftype", 1); P ("style", th::SBlues); P ("mode", 4); P ("voicing", th::VShell); P ("playmode", 1);
            P ("nchords", 12); P ("chordlen", 1); P ("swing", 0.55f);
            V (Shape, 0.4f); V (Cutoff, 0.5f); V (EnvMod, 0.3f); V (Decay, 0.5f); V (Sustain, 0.3f); V (Release, 0.3f); V (Drive, 0.2f);
            rhythm = 3;
            break;
        case 8:   // Dub Techno Chord : un accord mineur, delai en echo
            P ("osc1type", 0); P ("osc2type", 1); P ("ftype", 1); P ("style", th::STechno); P ("mode", 5); P ("voicing", th::VClose);
            P ("playmode", 1); P ("nchords", 1); P ("complex", 0.5f); P ("bassmode", 0);
            V (O2Fine, 0.62f); V (O2Level, 0.5f); V (Cutoff, 0.35f); V (Reso, 0.3f); V (EnvMod, 0.35f); V (FDecay, 0.4f); V (Decay, 0.5f);
            V (Sustain, 0.2f); V (SendDly, 0.6f); V (SendRev, 0.45f);
            P ("dlyfb", 0.72f); P ("dlydiv", 7); P ("dlytone", 0.3f); P ("revsize", 0.85f);
            MX (0, 3, Cutoff, 0.35f); MX (1, 5, SendDly, 0.2f);
            rhythm = 2;
            break;
        case 9:   // Atonal Clusters (Boss : set-classes, circulation de l'agregat)
            P ("osc1type", 6); P ("osc2type", 3); P ("ftype", 3); P ("style", th::SAtonal); P ("mode", 37); P ("voicing", th::VCluster);
            P ("playmode", 1); P ("nchords", 6); P ("notechaos", 0.3f);
            V (Shape, 0.5f); V (FM, 0.2f); V (Cutoff, 0.55f); V (Reso, 0.4f); V (Attack, 0.15f); V (Release, 0.6f); V (SendRev, 0.5f);
            J (Shape, 0.2f); J (Cutoff, 0.15f);
            rhythm = 9;
            break;
        case 10:  // Arp Dreams
            P ("osc1type", 5); P ("ftype", 2); P ("style", th::SPop); P ("mode", 0); P ("playmode", 3); P ("arpmode", 2); P ("arpoct", 2);
            P ("voicing", th::VSpread);
            V (Shape, 0.4f); V (Cutoff, 0.55f); V (EnvMod, 0.35f); V (Decay, 0.4f); V (Sustain, 0.2f); V (Release, 0.35f); V (SendDly, 0.35f); V (SendRev, 0.3f);
            P ("dlydiv", 7); MX (0, 5, Cutoff, 0.3f);
            rhythm = 6;
            break;
        case 11:  // Strum Guitar
            P ("osc1type", 7); P ("ftype", 2); P ("style", th::SPop); P ("mode", 0); P ("playmode", 2); P ("voicing", th::VSpread); P ("voices", 5);
            P ("strum", 0.55f); P ("humanize", 0.3f);
            V (Shape, 0.55f); V (Cutoff, 0.68f); V (Decay, 0.6f); V (Sustain, 0.0f); V (Release, 0.45f); V (SendRev, 0.25f);
            rhythm = 1;
            break;
        case 12:  // Neo Soul Rhodes : FM douce, rootless, vibrato de voix, comp en anticipations
            P ("osc1type", 4); P ("osc2type", 3); P ("ftype", 2); P ("style", th::SNeoSoul); P ("mode", 1); P ("voicing", th::VRootlessB);
            P ("voices", 5); P ("playmode", 1); P ("complex", 0.75f); P ("chroma", 0.4f); P ("chordlen", 1); P ("nchords", 4);
            P ("swing", 0.3f); P ("humanize", 0.45f); P ("vspread", 0.45f); P ("livesub", 0.25f);
            V (FM, 0.26f); V (O2Semi, 0.75f); V (O2Level, 0.12f); V (Cutoff, 0.6f); V (EnvMod, 0.3f); V (Attack, 0.02f); V (Decay, 0.6f);
            V (Sustain, 0.4f); V (Release, 0.5f); V (VelFilt, 0.7f); V (SendRev, 0.3f); P ("chmix", 0.55f); P ("chrate", 0.25f);
            MX (0, 5, FM, 0.15f); P ("m5rate", 2);
            rhythm = 5;
            break;
        case 13:  // Bossa Nylon : corde pincee, basse alternee, changements tous les 2 temps
            P ("osc1type", 7); P ("ftype", 2); P ("style", th::SBossa); P ("mode", 0); P ("voicing", th::VDrop2); P ("voices", 4);
            P ("playmode", 1); P ("complex", 0.6f); P ("chordlen", 1); P ("nchords", 8); P ("humanize", 0.3f); P ("vspread", 0.3f);
            V (Shape, 0.5f); V (Cutoff, 0.64f); V (Decay, 0.55f); V (Sustain, 0.0f); V (Release, 0.4f); V (SendRev, 0.22f); V (Width, 0.8f);
            rhythm = 4;
            break;
        default:  // Init Pads
            gen = false;
            break;
    }
    if (gen)
    {
        generateProgression (0, 9000 + index);
        applyRhythm (0, rhythm, 77 + index);
        for (int p = 1; p < kPatterns; ++p)
        {
            bank.pats[p].copyFrom (bank.pats[0]);
            if (p >= 4) generateProgression (p, 9100 + index * 10 + p);
            else reharmonizeAmount (p, 0.15f * (float) p, 500 * p + index);
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
