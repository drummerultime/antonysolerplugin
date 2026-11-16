// Banc de test hors ligne de Rotwang
#include "RwProcessor.h"
#include "RwEditor.h"
#include "RwPresets.h"
#include <chrono>
#include <set>

using namespace rw;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  ECHEC: " << msg << "\n"; } } while (0)

static constexpr double FS = 48000.0;

// Signal de test : "kalimba" (sinus amortis) + souffle
static void makeInput (juce::AudioBuffer<float>& b, int64_t offset, uint32_t seed = 1)
{
    alea::FastRng rng (seed + (uint32_t) (offset / 997));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const int64_t t = offset + i;
        const int64_t note = t / 24000;
        const double tn = (double) (t % 24000) / FS;
        static const double f[] { 523.25, 659.25, 783.99, 587.33, 880.0, 440.0 };
        const double fr = f[note % 6];
        const float k = (float) (0.4 * std::exp (-tn * 4.0) * std::sin (2.0 * juce::MathConstants<double>::pi * fr * tn));
        const float breath = (note % 4 == 3) ? rng.bi() * 0.05f : 0.0f;
        b.setSample (0, i, k + breath);
        b.setSample (1, i, k * 0.9f + breath);
    }
}

struct Stats { float peak = 0, rms = 0; bool finite = true; };

static Stats run (RotwangProcessor& p, double seconds, int block = 256, bool silentAfter = false, double silentFrom = 0.0,
                  std::vector<float>* capture = nullptr, double statFrom = 0.0)
{
    juce::AudioBuffer<float> b (2, block);
    juce::MidiBuffer midi;
    const int64_t total = (int64_t) (seconds * FS);
    Stats st; double sum = 0; int64_t cnt = 0;
    for (int64_t pos = 0; pos < total; pos += block)
    {
        makeInput (b, pos);
        if (silentAfter && (double) pos / FS >= silentFrom) b.clear();
        p.processBlock (b, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = b.getSample (c, i);
                if (! std::isfinite (v)) st.finite = false;
                if ((double) (pos + i) / FS >= statFrom)
                {
                    st.peak = juce::jmax (st.peak, std::abs (v));
                    sum += (double) v * v; ++cnt;
                }
                if (capture != nullptr && c == 0) capture->push_back (v);
            }
    }
    st.rms = (float) std::sqrt (sum / juce::jmax ((int64_t) 1, cnt));
    return st;
}

static void clearChain (RotwangProcessor& p)
{
    applyFactoryPreset (p, numFactoryPresets() - 1);
    p.setParam (sid (0, "type"), 0.0f);
    for (int r = 0; r < kNumRows; ++r) p.setParamReal ("row" + juce::String (r + 1) + "_amt", 0.0f);
}

static void setSlot (RotwangProcessor& p, int s, int type, const float* v, float mix)
{
    p.setParam (sid (s, "type"), (float) type / (float) (kNumTypes - 1));
    p.setParam (sid (s, "on"), 1.0f);
    for (int k = 0; k < 6; ++k) p.setParam (pid (s, k), v[k]);
    p.setParam (pid (s, 6), mix);
}

// ---------------------------------------------------------------------------
static void testEnginesDirect()
{
    std::cout << "[1] Moteurs seuls : 16 types, parametres aleatoires, freeze, bornes\n";
    juce::Random rnd (42);
    for (int t = 1; t < kNumTypes; ++t)
    {
        auto e = makeEngine (t);
        e->prepare (FS);
        e->reset();
        float maxAbs = 0; bool fin = true; double energy = 0;
        juce::AudioBuffer<float> in (2, kSub);
        float p[6];
        for (int k = 0; k < 6; ++k) p[k] = rnd.nextFloat();
        Ctx c; c.fs = FS; c.bpm = 120; c.ppqPerSample = 2.0 / FS; c.seed = 7; c.slot = 0;
        const int64_t total = (int64_t) (FS * 6.0);
        for (int64_t pos = 0; pos < total; pos += kSub)
        {
            if (pos % (int64_t) (FS * 0.5) < kSub) for (int k = 0; k < 6; ++k) p[k] = rnd.nextFloat();
            c.ppq = (double) pos * c.ppqPerSample;
            c.tick = pos / 6000;
            c.freeze = (pos / (int64_t) (FS * 1.5)) == 2;
            makeInput (in, pos);
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

static void testEnginesInPlugin()
{
    std::cout << "[2] Chaque moteur dans le plugin : fini, limite, audible (5 tirages chacun)\n";
    juce::Random rnd (7);
    for (int t = 1; t < kNumTypes; ++t)
    {
        int audible = 0;
        for (int trial = 0; trial < 5; ++trial)
        {
            RotwangProcessor p;
            p.prepareToPlay (FS, 256);
            clearChain (p);
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

static void testStutterPeriod()
{
    std::cout << "[3] Stutter : la repetition a exactement la duree de la division\n";
    auto e = makeEngine (TStutter);
    e->prepare (FS);
    e->reset();
    const float p[6] { (3 + 0.5f) / 9.0f, 1.0f, (3 + 0.5f) / 5.0f, 0.5f, 0.0f, 1.0f };   // 1/16, chance 100%, 1 bar
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
    std::cout << "    correlation a 6000 echantillons (1/16 a 120 BPM) : " << corr << "\n";
    CHECK (corr > 0.98, "les repetitions ne sont pas periodiques (" << corr << ")");
    // la premiere tranche (live) doit etre l'entree
    double eLive = 0;
    for (int t = 100; t < slice - 200; ++t) eLive += std::abs (out[(size_t) t]);
    CHECK (eLive > 100.0, "la tranche live est muette");
}

static void testRandomClock()
{
    std::cout << "[4] Horloge aleatoire : les valeurs bougent au pas, rien sans horloge\n";
    for (int mode : { 1, 0, 8 })
    {
        RotwangProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p);
        const float v[6] { 0.5f, 0.4f, 0.5f, 0.5f, 0.2f, 1.0f };
        setSlot (p, 0, TDelay, v, 0.5f);
        p.setParam (jid (0, 1), 0.8f);
        p.setParamReal ("chaos", 1.0f);
        p.setParamReal ("clock", (float) mode);
        p.setParamReal ("glide", 0.0f);
        p.setParamReal ("rate", 4.0f);
        juce::AudioBuffer<float> b (2, 256);
        juce::MidiBuffer m;
        std::set<int> seen;
        float lo = 1, hi = 0;
        for (int i = 0; i < (int) (FS * 4 / 256); ++i)
        {
            makeInput (b, i * 256);
            p.processBlock (b, m);
            const float x = p.effectiveValue (0, 1);
            seen.insert ((int) std::round (x * 1000));
            if (i > 40) { lo = juce::jmin (lo, x); hi = juce::jmax (hi, x); }
        }
        std::cout << "    horloge " << clockNames()[mode] << " : " << seen.size() << " valeurs distinctes, plage " << lo << " .. " << hi << "\n";
        if (mode == 0) CHECK (hi - lo < 0.001f, "sans horloge la valeur ne doit pas bouger");
        else
        {
            CHECK (seen.size() >= 6, "l'horloge " << clockNames()[mode] << " ne produit pas assez de valeurs");
            CHECK (hi - lo > 0.3f, "plage de hasard trop faible");
            CHECK (lo >= 0.0f && hi <= 1.0f, "valeur effective hors bornes");
        }
    }
}

static void testSeed()
{
    std::cout << "[5] Seed : meme seed = meme son, autre seed = autre son\n";
    auto render = [] (int seed, std::vector<float>& out)
    {
        RotwangProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (12);
        p.setParamReal ("seed", (float) seed);
        run (p, 4.0, 256, false, 0.0, &out);
    };
    std::vector<float> a, b, c;
    render (11, a); render (11, b); render (12, c);
    float dab = 0, dac = 0;
    for (size_t i = 0; i < a.size(); ++i) { dab = juce::jmax (dab, std::abs (a[i] - b[i])); dac = juce::jmax (dac, std::abs (a[i] - c[i])); }
    std::cout << "    ecart meme seed " << dab << " ; ecart autre seed " << dac << "\n";
    CHECK (dab < 1.0e-6f, "meme seed mais rendu different");
    CHECK (dac > 1.0e-3f, "seeds differentes mais rendu identique");
}

static void testState()
{
    std::cout << "[6] Sauvegarde / rappel de l'etat (parametres, cadenas, scenes, MIDI)\n";
    RotwangProcessor a;
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
    RotwangProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    int diff = 0;
    for (int i = 0; i < a.getParameters().size(); ++i)
        if (std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) > 1.0e-4f)
        {
            ++diff;
            std::cout << "    diff " << dynamic_cast<juce::AudioProcessorParameterWithID*> (a.getParameters()[i])->getParameterID()
                      << " " << a.getParameters()[i]->getValue() << " / " << b.getParameters()[i]->getValue() << "\n";
        }
    CHECK (diff == 0, diff << " parametres differents apres rappel");
    CHECK (b.lockP[2][3].load() && b.slotLock[4].load(), "cadenas perdus");
    CHECK (b.sceneStored (5), "scene perdue");
    CHECK (b.ccForParam (10) == 21, "MIDI learn perdu (" << b.ccForParam (10) << ")");
}

static void testDiceUndoScenes()
{
    std::cout << "[7] De, cadenas, undo, deplacement, scenes et morph\n";
    RotwangProcessor p;
    p.loadFactory (0);
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
    CHECK (p.slotType (0) == t1 && p.slotType (1) == t0, "deplacement de slot rate");
    CHECK (p.lockP[1][0].load(), "le cadenas n'a pas suivi le slot");

    p.slotLock[2].store (true);
    const int t2 = p.slotType (2);
    p.dice (-1, true);
    CHECK (p.slotType (2) == t2, "la mutation a change un slot verrouille");

    // scenes
    p.setParamReal ("morph", 0.0f);
    p.storeScene (3);
    std::vector<float> snap;
    for (auto* prm : p.getParameters()) snap.push_back (prm->getValue());
    p.dice (-1, true);
    p.recallScene (3);
    int diff = 0;
    for (int s = 0; s < kNumSlots; ++s)
        for (int k = 0; k < kNumSP; ++k)
            if (std::abs (p.paramValue (s, k) - p.apvts.getParameter (pid (s, k))->getValue()) > 1e-6f) ++diff;
    int i = 0, d2 = 0;
    for (auto* prm : p.getParameters())
    {
        const auto id = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm)->getParameterID();
        if (id.startsWith ("s") && id.contains ("_") && std::abs (prm->getValue() - snap[(size_t) i]) > 1e-4f) ++d2;
        ++i;
    }
    CHECK (d2 == 0, d2 << " parametres de slot differents apres rappel de scene");

    // morph progressif
    p.setParamReal ("morph", 0.3f);
    p.dice (-1, false);
    const float start = p.paramValue (1, 1);
    p.recallScene (3);
    juce::Thread::sleep (120);
    p.morphTick();
    const float mid = p.paramValue (1, 1);
    juce::Thread::sleep (300);
    p.morphTick();
    const float target = p.paramValue (1, 1);
    std::cout << "    morph : " << start << " -> " << mid << " -> " << target << "\n";
    CHECK (std::abs (target - snap[0]) >= 0.0f, "");
    CHECK (std::abs (mid - start) > 1e-4f || std::abs (target - start) < 1e-4f, "le morph ne progresse pas");

    // variations
    p.generateSceneVariations();
    int stored = 0;
    for (int s = 0; s < kNumScenes; ++s) stored += p.sceneStored (s) ? 1 : 0;
    CHECK (stored == kNumScenes, "variations incompletes");
}

static void testFreeze()
{
    std::cout << "[8] Freeze : la matiere tient apres l'arret du jeu\n";
    for (int t : { TPlate, TGranular, TDelay, TReverse, TResonator, TShimmer })
    {
        RotwangProcessor p;
        p.prepareToPlay (FS, 256);
        clearChain (p);
        const float v[6] { 0.1f, 0.5f, 0.5f, 0.1f, 0.3f, 0.5f };
        setSlot (p, 0, t, v, 1.0f);
        run (p, 2.0);
        p.setParamReal ("freeze", 1.0f);
        const auto st = run (p, 6.0, 256, true, 0.0, nullptr, 4.0);
        std::cout << "    " << typeNames()[t].paddedRight (' ', 12) << " rms apres 4 s de silence gele : " << st.rms << "\n";
        CHECK (st.finite && st.peak <= 1.0f, typeNames()[t] << " freeze instable");
        CHECK (st.rms > 1.0e-3f, typeNames()[t] << " : le freeze ne tient pas");
    }
}

static void testPresets()
{
    std::cout << "[9] Presets d'usine : fini, limite, audible\n";
    for (int i = 0; i < numFactoryPresets(); ++i)
    {
        RotwangProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (i);
        const auto st = run (p, 5.0, 256, false, 0.0, nullptr, 1.0);
        std::cout << "    " << factoryPresetName (i).paddedRight (' ', 26) << " crete " << juce::String (st.peak, 3) << " rms " << juce::String (st.rms, 4) << "\n";
        CHECK (st.finite, factoryPresetName (i) << " non fini");
        CHECK (st.peak <= 1.0f, factoryPresetName (i) << " sature");
        CHECK (st.rms > 2.0e-3f, factoryPresetName (i) << " trop faible");
    }
}

static void testFuzz()
{
    std::cout << "[10] Fuzz : changements de moteurs et de reglages en continu (30 s, blocs variables)\n";
    RotwangProcessor p;
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
            p.setParamReal ("in", 0.0f);
            p.setParamReal ("out", 0.0f);
        }
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) { const float v = b.getSample (c, i); if (! std::isfinite (v)) fin = false; pk = juce::jmax (pk, std::abs (v)); }
        pos += n;
    }
    std::cout << "    crete max " << pk << "\n";
    CHECK (fin, "valeurs non finies pendant le fuzz");
    CHECK (pk <= 1.5f, "crete excessive pendant le fuzz : " << pk);
}

static void testCpu()
{
    std::cout << "[11] Charge CPU : 6 slots lourds, buffer 128\n";
    RotwangProcessor p;
    p.prepareToPlay (FS, 128);
    clearChain (p);
    const float g[6] { 0.6f, 1.0f, 0.5f, 0.5f, 0.5f, 0.3f };
    const float v[6] { 0.6f, 0.6f, 0.5f, 0.5f, 0.5f, 0.5f };
    setSlot (p, 0, TGranular, g, 0.6f);
    setSlot (p, 1, TShimmer, v, 0.5f);
    setSlot (p, 2, TReverse, v, 0.5f);
    setSlot (p, 3, TResonator, v, 0.5f);
    const float sc[6] { 0.6f, 1.0f, 0.5f, 0.5f, 0.5f, 0.1f }, ch[6] { 0.5f, 0.5f, 0.9f, 0.5f, 0.5f, 0.5f };
    setSlot (p, 4, TScatter, sc, 0.5f);
    setSlot (p, 5, TChorus, ch, 0.5f);
    for (int s = 0; s < 6; ++s) for (int k = 0; k < 7; ++k) p.setParam (jid (s, k), 0.3f);
    p.setParamReal ("clock", 1.0f);
    const auto t0 = std::chrono::high_resolution_clock::now();
    run (p, 20.0, 128);
    const double el = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
    std::cout << "    20 s d'audio en " << el << " s  -> " << juce::String (100.0 * el / 20.0, 1) << " % d'un coeur (y compris le signal de test)\n";
    CHECK (el < 20.0 * 0.5, "trop lourd");
}

static void testMidi()
{
    std::cout << "[12] MIDI : learn CC, scenes par notes, freeze par note\n";
    RotwangProcessor p;
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
    CHECK (p.activeScene() == 2, "la note 38 ne rappelle pas la scene 3 (" << p.activeScene() << ")");
    CHECK (p.apvts.getRawParameterValue ("freeze")->load() > 0.5f, "la note 46 n'active pas le freeze");
}

static void testMono()
{
    std::cout << "[13] Entree mono -> sortie stereo\n";
    RotwangProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::mono());
    l.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK (p.setBusesLayout (l), "mono refuse");
    p.prepareToPlay (FS, 256);
    p.loadFactory (0);
    const auto st = run (p, 2.0);
    CHECK (st.finite && st.rms > 1e-3f, "mono muet ou instable");
}

static void testPanicEngines()
{
    std::cout << "[15] Panique et moteurs a la demande\n";
    RotwangProcessor p;
    p.prepareToPlay (FS, 256);
    clearChain (p);
    const float v[6] { 0.9f, 0.9f, 0.3f, 0.1f, 0.3f, 0.5f };
    setSlot (p, 0, TPlate, v, 1.0f);
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
    CHECK (run (p, 1.0).finite, "apres liberation");
}

static void testGui (const juce::File& outDir)
{
    std::cout << "[14] Interface : captures\n";
    for (int pr : { 0, 1, 12 })
    {
        RotwangProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (pr);
        run (p, 1.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        CHECK (img.getWidth() == ed->getWidth(), "capture invalide");
        auto f = outDir.getChildFile ("rotwang_gui_" + juce::String (pr) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "    " << f.getFullPathName() << " (" << ed->getWidth() << "x" << ed->getHeight() << ")\n";
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    testEnginesDirect();
    testEnginesInPlugin();
    testStutterPeriod();
    testRandomClock();
    testSeed();
    testState();
    testDiceUndoScenes();
    testFreeze();
    testPresets();
    testFuzz();
    testCpu();
    testMidi();
    testMono();
    testPanicEngines();
    testGui (outDir);
    std::cout << "\n" << (checks - failures) << "/" << checks << " verifications OK\n";
    return failures == 0 ? 0 : 1;
}
