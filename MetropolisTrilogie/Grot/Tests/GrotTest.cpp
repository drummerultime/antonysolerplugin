// Banc de test hors ligne de Grot
#include "GrProcessor.h"
#include "GrEditor.h"
#include "GrPresets.h"
#include <chrono>
#include <set>

using namespace gr;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  ECHEC: " << msg << "\n"; } } while (0)

static constexpr double FS = 48000.0;
static constexpr int BEAT = 24000;   // 120 BPM

// Batterie de synthese : 0 = kit complet, 1 = grosse caisse seule (noires), 2 = charley seul (doubles croches), 3 = conga (croches)
static float drumSample (int64_t t, int kind, alea::FastRng& rng)
{
    const double pi2 = 2.0 * juce::MathConstants<double>::pi;
    float out = 0.0f;
    auto kick = [&] (int64_t tt)
    {
        const double s = (double) tt / FS;
        const double f = 50.0 + 120.0 * std::exp (-s * 30.0);
        const double ph = pi2 * (50.0 * s + 120.0 * (1.0 - std::exp (-s * 30.0)) / 30.0);
        juce::ignoreUnused (f);
        return (float) (0.8 * std::exp (-s * 8.0) * std::sin (ph));
    };
    auto snare = [&] (int64_t tt)
    {
        const double s = (double) tt / FS;
        return (float) (0.5 * std::exp (-s * 20.0) * (0.6 * rng.bi() + 0.5 * std::sin (pi2 * 185.0 * s)));
    };
    auto hat = [&] (int64_t tt)
    {
        const double s = (double) tt / FS;
        return (float) (0.25 * std::exp (-s * 70.0) * rng.bi());
    };
    auto conga = [&] (int64_t tt)
    {
        const double s = (double) tt / FS;
        return (float) (0.6 * std::exp (-s * 14.0) * std::sin (pi2 * 210.0 * s) + 0.15 * std::exp (-s * 120.0) * rng.bi());
    };
    switch (kind)
    {
        case 1: out = kick (t % BEAT); break;
        case 2: out = hat (t % (BEAT / 4)); break;
        case 3: out = conga (t % (BEAT / 2)); break;
        case 4: out = ((t / (BEAT / 2)) % 2 == 0) ? kick (t % (BEAT / 2)) : hat (t % (BEAT / 2)) + kick (t % (BEAT / 2) + BEAT / 2) ; break;
        default:
        {
            const int64_t bar = t % (4 * BEAT);
            out = kick (t % (2 * BEAT));
            if (bar >= BEAT) out += snare ((bar - BEAT) % (2 * BEAT) < BEAT ? (bar - BEAT) % (2 * BEAT) : 999999);
            out += hat (t % (BEAT / 2));
            break;
        }
    }
    return out;
}

static int gKind = 0;
static void makeInput (juce::AudioBuffer<float>& b, int64_t offset)
{
    alea::FastRng rng ((uint32_t) (offset * 2654435761u + 7u));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = drumSample (offset + i, gKind, rng);
        b.setSample (0, i, v);
        b.setSample (1, i, v * 0.95f);
    }
}

struct Stats { float peak = 0, rms = 0; bool finite = true; };

static Stats run (GrotProcessor& p, double seconds, int block = 256, bool silentAfter = false, double silentFrom = 0.0,
                  std::vector<float>* capture = nullptr, double statFrom = 0.0, std::vector<float>* inputCap = nullptr)
{
    juce::AudioBuffer<float> b (2, block);
    juce::MidiBuffer midi;
    const int64_t total = (int64_t) (seconds * FS);
    Stats st; double sum = 0; int64_t cnt = 0;
    for (int64_t pos = 0; pos < total; pos += block)
    {
        makeInput (b, pos);
        if (silentAfter && (double) pos / FS >= silentFrom) b.clear();
        if (inputCap != nullptr) for (int i = 0; i < block; ++i) inputCap->push_back (b.getSample (0, i));
        p.processBlock (b, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = b.getSample (c, i);
                if (! std::isfinite (v)) st.finite = false;
                if ((double) (pos + i) / FS >= statFrom) { st.peak = juce::jmax (st.peak, std::abs (v)); sum += (double) v * v; ++cnt; }
                if (capture != nullptr && c == 0) capture->push_back (v);
            }
    }
    st.rms = (float) std::sqrt (sum / juce::jmax ((int64_t) 1, cnt));
    return st;
}

static void clearChain (GrotProcessor& p, int source = 4)
{
    applyFactoryPreset (p, numFactoryPresets() - 1);
    p.setParam (sid (0, "type"), 0.0f);
    p.setParamReal ("source", (float) source);
    for (int r = 0; r < kNumRows; ++r) p.setParamReal ("row" + juce::String (r + 1) + "_amt", 0.0f);
}

static void setSlot (GrotProcessor& p, int s, int type, const float* v, float mix, float chance = 1.0f)
{
    p.setParam (sid (s, "type"), (float) type / (float) (kNumTypes - 1));
    p.setParam (sid (s, "on"), 1.0f);
    for (int k = 0; k < 6; ++k) p.setParam (pid (s, k), v[k]);
    p.setParam (pid (s, kMix), mix);
    p.setParam (pid (s, kChance), chance);
}

// ---------------------------------------------------------------------------
static void testEnginesDirect()
{
    std::cout << "[1] Moteurs seuls : 19 types, reglages aleatoires, coups, freeze, bornes\n";
    juce::Random rnd (42);
    alea::FastRng rng (9);
    for (int t = 1; t < kNumTypes; ++t)
    {
        auto e = makeEngine (t);
        e->prepare (FS);
        e->reset();
        float maxAbs = 0; bool fin = true; double energy = 0;
        juce::AudioBuffer<float> in (2, kSub);
        float p[6], hits[kSub];
        for (int k = 0; k < 6; ++k) p[k] = rnd.nextFloat();
        Ctx c; c.fs = FS; c.bpm = 120; c.ppqPerSample = 2.0 / FS; c.seed = 7; c.slot = 0; c.fund = 60.0f; c.body = 120.0f;
        const int64_t total = (int64_t) (FS * 6.0);
        for (int64_t pos = 0; pos < total; pos += kSub)
        {
            if (pos % (int64_t) (FS * 0.5) < kSub) for (int k = 0; k < 6; ++k) p[k] = rnd.nextFloat();
            c.ppq = (double) pos * c.ppqPerSample;
            c.tick = pos / 6000;
            c.freeze = (pos / (int64_t) (FS * 1.5)) == 2;
            for (int i = 0; i < kSub; ++i)
            {
                const float v = drumSample (pos + i, 0, rng);
                in.setSample (0, i, v); in.setSample (1, i, v);
                hits[i] = ((pos + i) % (BEAT / 2) == 0) ? 0.8f : 0.0f;
            }
            c.hit = hits;
            e->process (in.getWritePointer (0), in.getWritePointer (1), kSub, c, p);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kSub; ++i)
                {
                    const float v = in.getSample (ch, i);
                    if (! std::isfinite (v)) fin = false;
                    maxAbs = juce::jmax (maxAbs, std::abs (v));
                    energy += (double) v * v;
                }
        }
        std::cout << "    " << typeNames()[t].paddedRight (' ', 14) << " crete " << juce::String (maxAbs, 3) << "  energie " << juce::String (energy, 1) << "\n";
        CHECK (fin, typeNames()[t] << " : valeurs non finies");
        CHECK (maxAbs < 12.0f, typeNames()[t] << " : crete excessive " << maxAbs);
        CHECK (energy > 1.0, typeNames()[t] << " : sortie quasi muette");
    }
}

static void testDetection()
{
    std::cout << "[2] Detection des coups selon la source\n";
    struct Case { int kind, source; int expected; const char* name; };
    const Case cases[] { { 1, 0, 8, "grosse caisse en noires" }, { 2, 6, 32, "charley en doubles croches" },
                         { 3, 8, 16, "conga en croches" }, { 4, 4, 16, "grosse caisse + charley alternes" },
                         { 0, 4, 8, "kit complet (au moins les temps)" } };
    for (const auto& cs : cases)
    {
        gKind = cs.kind;
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p, cs.source);
        run (p, 4.0);
        const int h = p.hitCountView.load();
        std::cout << "    " << juce::String (cs.name).paddedRight (' ', 34) << " source " << sourceProfile (cs.source).name << " : " << h << " coups (attendu " << cs.expected << ")\n";
        if (cs.kind == 0) CHECK (h >= cs.expected && h <= 24, cs.name << " : " << h << " coups");
        else CHECK (std::abs (h - cs.expected) <= 1, cs.name << " : " << h << " coups au lieu de " << cs.expected);
    }
    gKind = 0;
}

static void testEnginesInPlugin()
{
    std::cout << "[3] Chaque moteur dans le plugin : fini, limite, audible (5 tirages chacun)\n";
    juce::Random rnd (7);
    for (int t = 1; t < kNumTypes; ++t)
    {
        int audible = 0;
        for (int trial = 0; trial < 5; ++trial)
        {
            GrotProcessor p;
            p.prepareToPlay (FS, 256);
            clearChain (p, rnd.nextInt (kNumSources));
            float v[6];
            for (auto& x : v) x = rnd.nextFloat();
            if (t == TGate) v[1] = 0.4f + 0.6f * v[1];
            if (t == TStutter) v[5] = 0.5f + 0.5f * v[5];
            setSlot (p, 0, t, v, 1.0f);
            const auto st = t == TReverse ? run (p, 9.0, 256, false, 0.0, nullptr, 5.0) : run (p, 3.0, 256, false, 0.0, nullptr, 1.0);
            CHECK (st.finite, typeNames()[t] << " valeurs non finies");
            CHECK (st.peak <= 1.0f, typeNames()[t] << " depasse 0 dBFS : " << st.peak);
            if (st.rms > 1.0e-4f) ++audible;
        }
        CHECK (audible >= 4, typeNames()[t] << " muet trop souvent (" << audible << "/5)");
    }
}

// energie de la difference sortie - entree
static double layerEnergy (GrotProcessor& p, double secs)
{
    std::vector<float> out, in;
    run (p, secs, 256, false, 0.0, &out, 0.0, &in);
    double e = 0;
    for (size_t i = 0; i < out.size(); ++i) { const double d = out[i] - in[i]; e += d * d; }
    return e;
}

static void testChanceAndTriggers()
{
    std::cout << "[4] Sub 808 / couche bruit declenches et Chance par coup\n";
    gKind = 1;
    const float sub[6] { 0.5f, 0.4f, 0.3f, 0.3f, 0.0f, 0.0f };
    double e[3];
    const float ch[3] { 0.0f, 0.5f, 1.0f };
    for (int k = 0; k < 3; ++k)
    {
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p, 0);
        setSlot (p, 0, TSub, sub, 1.0f, ch[k]);
        e[k] = layerEnergy (p, 16.0);
    }
    std::cout << "    energie ajoutee par le Sub : chance 0 % = " << e[0] << " | 50 % = " << e[1] << " | 100 % = " << e[2] << "\n";
    CHECK (e[0] < e[2] * 0.02, "chance 0 % : le sub ne doit pas sonner");
    CHECK (e[2] > 10.0, "chance 100 % : le sub ne sonne pas");
    CHECK (e[1] > e[2] * 0.25 && e[1] < e[2] * 0.75, "chance 50 % : proportion de coups incoherente");

    // accord du sub : frequence proche de la fondamentale de la source (55 Hz)
    {
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p, 0);
        const float s2[6] { 0.5f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f };
        setSlot (p, 0, TSub, s2, 1.0f);
        std::vector<float> out, in;
        run (p, 2.0, 256, false, 0.0, &out, 0.0, &in);
        int zc = 0;
        const size_t a = (size_t) (BEAT * 2 + 4000), b = (size_t) (BEAT * 2 + 16000);
        float prev = 0;
        for (size_t i = a; i < b; ++i) { const float d = out[i] - in[i]; if ((d > 0) != (prev > 0)) ++zc; prev = d; }
        const double f = zc / 2.0 / ((double) (b - a) / FS);
        std::cout << "    frequence du sub (source grosse caisse) : " << f << " Hz\n";
        CHECK (std::abs (f - 55.0) < 5.0, "sub mal accorde : " << f);
    }

    // couche bruit
    {
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p, 1);
        const float nz[6] { 0.1f, 0.6f, 0.3f, 0.5f, 0.0f, 0.0f };
        setSlot (p, 0, TNoise, nz, 1.0f);
        gKind = 0;
        CHECK (layerEnergy (p, 3.0) > 1.0, "la couche de bruit ne sonne pas");
    }
    gKind = 0;
}

static void testGatedReverb()
{
    std::cout << "[5] Reverb gated : la queue est coupee apres le temps de porte\n";
    gKind = 1;
    GrotProcessor p;
    p.prepareToPlay (FS, 256);
    clearChain (p, 0);
    const float v[6] { (3 + 0.5f) / 5.0f, 0.8f, 0.8f, 0.3f, 0.0f, 0.2f };   // gated, porte ~100 ms
    setSlot (p, 0, TReverb, v, 1.0f);
    std::vector<float> out;
    run (p, 3.0, 256, false, 0.0, &out);
    double early = 0, late = 0;
    for (int i = 0; i < 2000; ++i) early += std::abs (out[(size_t) (BEAT * 4 + 1000 + i)]);
    for (int i = 0; i < 2000; ++i) late += std::abs (out[(size_t) (BEAT * 4 + 14000 + i)]);
    std::cout << "    energie a +20 ms : " << early << "  a +300 ms : " << late << "\n";
    CHECK (late < early * 0.05, "la porte ne coupe pas la queue");
    gKind = 0;
}

static void testStutterPeriod()
{
    std::cout << "[6] Stutter : periode exacte\n";
    auto e = makeEngine (TStutter);
    e->prepare (FS);
    e->reset();
    const float p[6] { (3 + 0.5f) / 9.0f, 1.0f, (3 + 0.5f) / 5.0f, 0.5f, 0.0f, 1.0f };
    Ctx c; c.fs = FS; c.bpm = 120; c.ppqPerSample = 2.0 / FS; c.seed = 3;
    std::vector<float> out;
    alea::FastRng rng (5);
    juce::AudioBuffer<float> b (2, kSub);
    for (int64_t pos = 0; pos < 96000; pos += kSub)
    {
        for (int i = 0; i < kSub; ++i) { const float x = rng.bi(); b.setSample (0, i, x); b.setSample (1, i, x); }
        c.ppq = (double) pos * c.ppqPerSample;
        e->process (b.getWritePointer (0), b.getWritePointer (1), kSub, c, p);
        for (int i = 0; i < kSub; ++i) out.push_back (b.getSample (0, i));
    }
    const int slice = 6000;
    double num = 0, da = 0, db = 0;
    for (int t = slice + 200; t < 2 * slice - 200; ++t)
    {
        num += (double) out[(size_t) t] * out[(size_t) (t + slice)];
        da += (double) out[(size_t) t] * out[(size_t) t];
        db += (double) out[(size_t) (t + slice)] * out[(size_t) (t + slice)];
    }
    const double corr = num / std::sqrt (da * db + 1e-12);
    std::cout << "    correlation : " << corr << "\n";
    CHECK (corr > 0.98, "repetitions non periodiques");
}

static void testHitClock()
{
    std::cout << "[7] Horloge 'Chaque coup' : un nouveau tirage par frappe\n";
    for (int mode : { kClockHit, kClockHit2, 0 })
    {
        gKind = 1;
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p, 0);
        const float v[6] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
        setSlot (p, 0, TFilter, v, 1.0f);
        p.setParam (jid (0, 1), 0.8f);
        p.setParamReal ("chaos", 1.0f);
        p.setParamReal ("clock", (float) mode);
        p.setParamReal ("glide", 0.0f);
        juce::AudioBuffer<float> b (2, 256);
        juce::MidiBuffer m;
        int changes = 0; float last = -1; bool prevCh = false;
        for (int i = 0; i < (int) (FS * 4 / 256); ++i)
        {
            makeInput (b, (int64_t) i * 256);
            p.processBlock (b, m);
            const float x = p.effectiveValue (0, 1);
            const bool ch = last >= 0 && std::abs (x - last) > 0.01f;
            if (ch && ! prevCh) ++changes;
            prevCh = ch;
            last = x;
        }
        std::cout << "    " << clockNames()[mode].paddedRight (' ', 12) << " : " << changes << " changements pour " << p.hitCountView.load() << " coups\n";
        if (mode == kClockHit) CHECK (changes >= 6 && changes <= 9, "chaque coup : " << changes);
        if (mode == kClockHit2) CHECK (changes >= 3 && changes <= 5, "1 coup / 2 : " << changes);
        if (mode == 0) CHECK (changes == 0, "Off : la valeur bouge");
    }
    gKind = 0;
}

static void testSeed()
{
    std::cout << "[8] Seed : meme seed = meme son, autre seed = autre son\n";
    auto render = [] (int seed, std::vector<float>& out)
    {
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (9);
        p.setParamReal ("seed", (float) seed);
        run (p, 4.0, 256, false, 0.0, &out);
    };
    std::vector<float> a, b, c;
    render (11, a); render (11, b); render (12, c);
    float dab = 0, dac = 0;
    for (size_t i = 0; i < a.size(); ++i) { dab = juce::jmax (dab, std::abs (a[i] - b[i])); dac = juce::jmax (dac, std::abs (a[i] - c[i])); }
    std::cout << "    ecart meme seed " << dab << " ; autre seed " << dac << "\n";
    CHECK (dab < 1.0e-6f, "meme seed mais rendu different");
    CHECK (dac > 1.0e-3f, "seeds differentes mais rendu identique");
}

static void testState()
{
    std::cout << "[9] Sauvegarde / rappel de l'etat\n";
    GrotProcessor a;
    a.loadFactory (3);
    juce::Random rnd (99);
    for (auto* prm : a.getParameters())
        prm->setValueNotifyingHost (dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr ? (rnd.nextBool() ? 1.0f : 0.0f) : rnd.nextFloat());
    a.lockP[2][3].store (true); a.slotLock[4].store (true);
    a.storeScene (5);
    a.startLearn (10);
    {
        juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 21, 100), 0);
        juce::AudioBuffer<float> b (2, 64); b.clear();
        a.prepareToPlay (FS, 64);
        a.processBlock (b, m);
    }
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    GrotProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    int diff = 0;
    for (int i = 0; i < a.getParameters().size(); ++i)
        if (std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) > 1.0e-4f) ++diff;
    CHECK (diff == 0, diff << " parametres differents apres rappel");
    CHECK (b.lockP[2][3].load() && b.slotLock[4].load(), "cadenas perdus");
    CHECK (b.sceneStored (5), "scene perdue");
    CHECK (b.ccForParam (10) == 21, "MIDI learn perdu");
}

static void testDiceUndoScenes()
{
    std::cout << "[10] De, cadenas, undo, deplacement, scenes, adaptation a la source\n";
    GrotProcessor p;
    p.loadFactory (1);
    p.lockP[0][0].store (true);
    const float locked = p.paramValue (0, 0);
    const float before = p.paramValue (1, 1);
    p.dice (-1, false);
    CHECK (std::abs (p.paramValue (0, 0) - locked) < 1e-6f, "le de a modifie un parametre verrouille");
    CHECK (std::abs (p.paramValue (1, 1) - before) > 1e-4f, "le de n'a rien change");
    p.undo();
    CHECK (std::abs (p.paramValue (1, 1) - before) < 1e-4f, "undo ne restaure pas");
    const int t0 = p.slotType (0), t1 = p.slotType (1);
    p.moveSlot (0, 1);
    CHECK (p.slotType (0) == t1 && p.slotType (1) == t0, "deplacement rate");
    p.slotLock[2].store (true);
    const int t2 = p.slotType (2);
    p.dice (-1, true);
    CHECK (p.slotType (2) == t2, "la mutation a change un slot verrouille");
    p.setParamReal ("morph", 0.0f);
    p.storeScene (3);
    std::vector<float> snap;
    for (int s = 0; s < kNumSlots; ++s) for (int k = 0; k < kNumSP; ++k) snap.push_back (p.paramValue (s, k));
    p.dice (-1, true);
    p.recallScene (3);
    int d2 = 0, i = 0;
    for (int s = 0; s < kNumSlots; ++s) for (int k = 0; k < kNumSP; ++k) if (std::abs (p.paramValue (s, k) - snap[(size_t) i++]) > 1e-4f) ++d2;
    CHECK (d2 == 0, d2 << " parametres differents apres rappel de scene");

    for (int src = 0; src < kNumSources; ++src)
    {
        GrotProcessor q;
        q.prepareToPlay (FS, 256);
        q.setParamReal ("source", (float) src);
        q.adaptToSource();
        CHECK (q.sourceIndex() == src, "l'adaptation a change la source");
        const auto st = run (q, 2.0, 256, false, 0.0, nullptr, 0.5);
        CHECK (st.finite && st.peak <= 1.0f && st.rms > 1.0e-3f, "adaptation " << sourceProfile (src).name << " : rms " << st.rms << " crete " << st.peak);
    }
}

static void testFreeze()
{
    std::cout << "[11] Freeze : la matiere tient apres l'arret du jeu\n";
    for (int t : { TReverb, TGranular, TDelay, TReverse, TResonator })
    {
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p);
        const float v[6] { 0.3f, 0.5f, 0.5f, 0.1f, 0.3f, 0.5f };
        setSlot (p, 0, t, v, 1.0f);
        run (p, 2.0);
        p.setParamReal ("freeze", 1.0f);
        const auto st = run (p, 6.0, 256, true, 0.0, nullptr, 4.0);
        std::cout << "    " << typeNames()[t].paddedRight (' ', 12) << " rms gele : " << st.rms << "\n";
        CHECK (st.finite && st.peak <= 1.0f, typeNames()[t] << " freeze instable");
        CHECK (st.rms > 1.0e-3f, typeNames()[t] << " : le freeze ne tient pas");
    }
}

static void testPresets()
{
    std::cout << "[12] Presets d'usine\n";
    for (int i = 0; i < numFactoryPresets(); ++i)
    {
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (i);
        const auto st = run (p, 5.0, 256, false, 0.0, nullptr, 1.0);
        std::cout << "    " << factoryPresetName (i).paddedRight (' ', 30) << " crete " << juce::String (st.peak, 3) << " rms " << juce::String (st.rms, 4) << "\n";
        CHECK (st.finite, factoryPresetName (i) << " non fini");
        CHECK (st.peak <= 1.0f, factoryPresetName (i) << " sature");
        CHECK (st.rms > 5.0e-3f, factoryPresetName (i) << " trop faible");
    }
}

static void testFuzz()
{
    std::cout << "[13] Fuzz : 30 s de changements aleatoires\n";
    GrotProcessor p;
    p.prepareToPlay (FS, 1024);
    juce::Random rnd (1234);
    juce::AudioBuffer<float> b (2, 1024);
    juce::MidiBuffer m;
    bool fin = true; float pk = 0;
    int64_t pos = 0;
    while (pos < (int64_t) (FS * 30))
    {
        const int n = 1 + rnd.nextInt (1024);
        b.setSize (2, n, false, false, true);
        makeInput (b, pos);
        if (rnd.nextFloat() < 0.08f)
        {
            const auto& ps = p.getParameters();
            for (int k = 0; k < 6; ++k) ps[rnd.nextInt (ps.size())]->setValueNotifyingHost (rnd.nextFloat());
            p.setParam (sid (rnd.nextInt (kNumSlots), "type"), rnd.nextFloat());
            p.setParamReal ("in", 0.0f); p.setParamReal ("out", 0.0f);
        }
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) { const float v = b.getSample (c, i); if (! std::isfinite (v)) fin = false; pk = juce::jmax (pk, std::abs (v)); }
        pos += n;
    }
    std::cout << "    crete max " << pk << "\n";
    CHECK (fin, "valeurs non finies");
    CHECK (pk <= 1.5f, "crete excessive : " << pk);
}

static void testCpu()
{
    std::cout << "[14] Charge CPU : 6 slots lourds, buffer 128\n";
    GrotProcessor p;
    p.prepareToPlay (FS, 128);
    clearChain (p);
    const float v[6] { 0.6f, 0.6f, 0.5f, 0.5f, 0.5f, 0.5f };
    const float g[6] { 0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.6f };
    setSlot (p, 0, TDist, v, 0.6f);
    setSlot (p, 1, TBody, v, 0.5f);
    setSlot (p, 2, TGranular, g, 0.6f);
    setSlot (p, 3, TReverb, v, 0.5f);
    setSlot (p, 4, TResonator, v, 0.5f);
    setSlot (p, 5, TReverse, v, 0.5f);
    for (int s = 0; s < 6; ++s) for (int k = 0; k < kNumSP; ++k) p.setParam (jid (s, k), 0.3f);
    const auto t0 = std::chrono::high_resolution_clock::now();
    run (p, 20.0, 128);
    const double el = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
    std::cout << "    20 s d'audio en " << el << " s -> " << juce::String (100.0 * el / 20.0, 1) << " % d'un coeur\n";
    CHECK (el < 20.0 * 0.5, "trop lourd");
}

static void testMidi()
{
    std::cout << "[15] MIDI : learn CC, scenes, freeze\n";
    GrotProcessor p;
    p.prepareToPlay (FS, 64);
    p.loadFactory (0);
    const int idx = dynamic_cast<juce::AudioProcessorParameterWithID*> (p.apvts.getParameter ("macro1"))->getParameterIndex();
    p.startLearn (idx);
    juce::AudioBuffer<float> b (2, 64); b.clear();
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::controllerEvent (1, 20, 10), 0);
    p.processBlock (b, m);
    m.clear(); m.addEvent (juce::MidiMessage::controllerEvent (1, 20, 127), 0);
    p.processBlock (b, m);
    CHECK (p.ccForParam (idx) == 20, "CC non appris");
    CHECK (p.apvts.getParameter ("macro1")->getValue() > 0.99f, "le CC ne pilote pas la macro");
    m.clear(); m.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), 0);
    m.addEvent (juce::MidiMessage::noteOn (1, 46, (juce::uint8) 100), 1);
    p.processBlock (b, m);
    p.morphTick();
    CHECK (p.activeScene() == 2, "note 38 -> scene 3");
    CHECK (p.apvts.getRawParameterValue ("freeze")->load() > 0.5f, "note 46 -> freeze");
}

static void testMono()
{
    std::cout << "[16] Entree mono\n";
    GrotProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.setBusesLayout (l), "mono refuse");
    p.prepareToPlay (FS, 256);
    p.loadFactory (0);
    const auto st = run (p, 2.0);
    CHECK (st.finite && st.rms > 1e-3f, "mono muet ou instable");
}

static void testDistLatency()
{
    std::cout << "[18] Distorsion sans latence\n";
    auto e = makeEngine (TDist);
    e->prepare (FS); e->reset();
    const float p[6] { (0 + 0.5f) / 5.0f, 0.0f, 0.5f, 0.0f, 0.5f, 0.0f };
    Ctx c; c.fs = FS; c.bpm = 120; c.ppqPerSample = 2.0 / FS; c.body = 200;
    std::vector<float> in, out;
    juce::AudioBuffer<float> b (2, kSub);
    alea::FastRng rng (11);
    for (int k = 0; k < 200; ++k)
    {
        for (int i = 0; i < kSub; ++i) { const float x = rng.bi() * 0.1f; b.setSample (0, i, x); b.setSample (1, i, x); in.push_back (x); }
        e->process (b.getWritePointer (0), b.getWritePointer (1), kSub, c, p);
        for (int i = 0; i < kSub; ++i) out.push_back (b.getSample (0, i));
    }
    int best = 0; double bv = -1;
    for (int lag = 0; lag < 40; ++lag)
    {
        double s = 0;
        for (size_t i = 100; i + 40 < in.size(); ++i) s += (double) in[i] * out[i + (size_t) lag];
        if (s > bv) { bv = s; best = lag; }
    }
    std::cout << "    retard mesure : " << best << " echantillon(s)\n";
    CHECK (best <= 1, "la distorsion ajoute de la latence (" << best << ")");
}

static void testPanicEngines()
{
    std::cout << "[19] Panique et moteurs a la demande\n";
    GrotProcessor p;
    p.prepareToPlay (FS, 256);
    clearChain (p);
    const float v[6] { (2 + 0.5f) / 5.0f, 0.9f, 0.9f, 0.3f, 0.0f, 0.3f };
    setSlot (p, 0, TReverb, v, 1.0f);
    run (p, 2.0);
    p.setParamReal ("freeze", 1.0f);
    run (p, 0.5);
    juce::AudioBuffer<float> b (2, 256); b.clear(); juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 47, (juce::uint8) 100), 0);
    p.processBlock (b, m);
    p.morphTick();
    std::vector<float> out;
    run (p, 0.1, 256, true, 0.0, &out);
    p.morphTick();
    double e = 0; for (size_t i = 2400; i < out.size(); ++i) e += out[i] * out[i];
    CHECK (e < 1.0e-6, "la panique ne coupe pas le son (" << e << ")");
    CHECK (p.apvts.getRawParameterValue ("freeze")->load() < 0.5f, "la panique ne coupe pas le freeze");
    juce::Random rnd (3);
    for (int i = 0; i < 40; ++i) { p.setParam (sid (rnd.nextInt (kNumSlots), "type"), rnd.nextFloat()); run (p, 0.05); }
    const int before = p.preparedEngines();
    p.serviceEngines (0.0);
    int active = 0; for (int s = 0; s < kNumSlots; ++s) if (p.slotType (s) != 0) ++active;
    std::cout << "    moteurs prepares : " << before << " -> " << p.preparedEngines() << " (" << active << " en service)\n";
    CHECK (p.preparedEngines() <= active && before > p.preparedEngines(), "liberation des moteurs");
    const auto st = run (p, 1.0);
    CHECK (st.finite, "apres liberation");
}

static void testGui (const juce::File& outDir)
{
    std::cout << "[17] Interface : captures\n";
    for (int pr : { 1, 9, 22 })
    {
        GrotProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (pr);
        run (p, 2.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        CHECK (img.getWidth() == ed->getWidth(), "capture invalide");
        auto f = outDir.getChildFile ("grot_gui_" + juce::String (pr) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "    " << f.getFullPathName() << "\n";
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    testEnginesDirect();
    testDetection();
    testEnginesInPlugin();
    testChanceAndTriggers();
    testGatedReverb();
    testStutterPeriod();
    testHitClock();
    testSeed();
    testState();
    testDiceUndoScenes();
    testFreeze();
    testPresets();
    testFuzz();
    testCpu();
    testMidi();
    testMono();
    testDistLatency();
    testPanicEngines();
    testGui (outDir);
    std::cout << "\n" << (checks - failures) << "/" << checks << " verifications OK\n";
    return failures == 0 ? 0 : 1;
}
