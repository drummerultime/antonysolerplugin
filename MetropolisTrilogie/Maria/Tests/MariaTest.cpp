// Banc de test hors ligne de Maria
#include "MaProcessor.h"
#include "MaEditor.h"
#include "MaPresets.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <set>
#include <map>
#include <functional>

using namespace ma;

static int failures = 0, checks = 0;
#define CHECK(cond, msg) do { ++checks; if (! (cond)) { ++failures; std::cout << "  ECHEC: " << msg << "\n"; } } while (0)

static constexpr double FS = 48000.0;
static const int kMotif[] { 60, 62, 64, 67, 65, 64, 62, 59 };   // motif de 8 notes
static constexpr int kNoteLen = 14400;                             // 300 ms
static constexpr int kGapLen = 2400;                               // 50 ms

// "Saxophone" de synthese : motif repete, phrases de 4 motifs puis silence
struct Player
{
    int kind = 0;           // 0 melodie, 1 batterie, 2 silence
    bool playing = true;
    int64_t t = 0;
    alea::FastRng rng { 77 };
    int notesPlayed = 0;
    float next()
    {
        ++t;
        if (! playing || kind == 2) return 0.0f;
        if (kind == 3)
        {
            static const int chords[4][3] { { 60, 64, 67 }, { 57, 60, 64 }, { 53, 57, 60 }, { 55, 59, 62 } };   // C, Am, F, G
            const int64_t per = 26400, k = t % per;
            if (k >= 24000) return 0.0f;
            const auto& ch = chords[(t / per) % 4];
            const double s = (double) k / FS;
            const double env = std::exp (-s * 2.5) * std::min (1.0, s / 0.004);
            double y = 0;
            for (int nn : ch) { const double f = 440.0 * std::pow (2.0, (nn - 69) / 12.0); for (int h = 1; h <= 4; ++h) y += std::sin (2 * juce::MathConstants<double>::pi * f * h * s) / (h * h); }
            return (float) (0.18 * env * y);
        }
        if (kind == 1)
        {
            const int64_t k = t % 12000;   // croches a 120 BPM
            const double s = (double) k / FS;
            const int which = (int) ((t / 12000) % 4);
            if (which == 0) return (float) (0.7 * std::exp (-s * 9.0) * std::sin (2 * juce::MathConstants<double>::pi * (55.0 * s + 3.0 * (1.0 - std::exp (-s * 30.0)))));
            if (which == 2) return (float) (0.45 * std::exp (-s * 22.0) * (0.6 * rng.bi() + 0.4 * std::sin (2 * juce::MathConstants<double>::pi * 190.0 * s)));
            return (float) (0.2 * std::exp (-s * 60.0) * rng.bi());
        }
        const int64_t per = kNoteLen + kGapLen;
        const int64_t k = t % per;
        if (k >= kNoteLen) return 0.0f;
        const int note = kMotif[(t / per) % 8];
        const double f = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const double s = (double) k / FS;
        const double env = std::min (1.0, s / 0.01) * std::min (1.0, (double) (kNoteLen - k) / 480.0);
        double y = 0;
        for (int h = 1; h <= 5; ++h) y += std::sin (2 * juce::MathConstants<double>::pi * f * h * s) / (double) h;
        return (float) (0.35 * env * y);
    }
};

struct Stats { float peak = 0, rms = 0; bool finite = true; double energy = 0; };

// joue "seconds" secondes ; analyse synchrone apres chaque bloc
static Stats run (MariaProcessor& p, Player& pl, double seconds, int block = 256, std::vector<float>* capture = nullptr)
{
    juce::AudioBuffer<float> b (2, block);
    juce::MidiBuffer midi;
    const int64_t total = (int64_t) (seconds * FS);
    Stats st; double sum = 0; int64_t cnt = 0;
    for (int64_t pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i) { const float x = pl.next(); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, midi);
        p.analyzeNow();
        p.morphTick();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const float v = b.getSample (c, i);
                if (! std::isfinite (v)) st.finite = false;
                st.peak = juce::jmax (st.peak, std::abs (v));
                sum += (double) v * v; ++cnt;
                if (capture != nullptr && c == 0) capture->push_back (v);
            }
    }
    st.energy = sum;
    st.rms = (float) std::sqrt (sum / juce::jmax ((int64_t) 1, cnt));
    return st;
}

static float idxF (int i, int n) { return ((float) i + 0.5f) / (float) n; }

static void bare (MariaProcessor& p, int source, int mode)
{
    applyFactoryPreset (p, numFactoryPresets() - 1);    // Init : aucun effet
    p.setParamReal ("source", (float) source);
    p.setParamReal ("im_mode", (float) mode);
    p.setParamReal ("clock", 0.0f);
}

// ---------------------------------------------------------------------------
static void testEngines()
{
    std::cout << "[1] Moteurs d'effets : 16 types, reglages aleatoires, bornes\n";
    juce::Random rnd (42);
    Player pl;
    for (int t = 1; t < kNumTypes; ++t)
    {
        auto e = makeEngine (t);
        e->prepare (FS); e->reset();
        float maxAbs = 0; bool fin = true; double energy = 0;
        juce::AudioBuffer<float> in (2, kSub);
        float p[6], hits[kSub];
        for (auto& x : p) x = rnd.nextFloat();
        Ctx c; c.fs = FS; c.bpm = 120; c.ppqPerSample = 2.0 / FS; c.seed = 7; c.fund = 200; c.body = 400;
        for (int64_t pos = 0; pos < (int64_t) (FS * 5); pos += kSub)
        {
            if (pos % 24000 < kSub) for (auto& x : p) x = rnd.nextFloat();
            c.ppq = (double) pos * c.ppqPerSample; c.tick = pos / 6000; c.freeze = (pos / 72000) == 2;
            for (int i = 0; i < kSub; ++i) { const float v = pl.next(); in.setSample (0, i, v); in.setSample (1, i, v); hits[i] = ((pos + i) % 16800 == 0) ? 0.7f : 0.0f; }
            c.hit = hits;
            e->process (in.getWritePointer (0), in.getWritePointer (1), kSub, c, p);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < kSub; ++i)
            {
                const float v = in.getSample (ch, i);
                if (! std::isfinite (v)) fin = false;
                maxAbs = juce::jmax (maxAbs, std::abs (v)); energy += (double) v * v;
            }
        }
        CHECK (fin && maxAbs < 12.0f && energy > 1.0, typeNames()[t] << " : crete " << maxAbs << " energie " << energy);
    }
}

static void testAnalysis()
{
    std::cout << "[2] Analyse : segmentation, hauteurs, Factor Oracle\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDialogue);           // saxophone
    p.setParam (imId (IAct), 0.0f);   // Maria se tait pendant ce test
    Player pl;
    run (p, pl, (kNoteLen + kGapLen) * 24.0 / FS + 0.5, 256);   // 3 fois le motif
    auto& m = p.mem;
    const int n = m.numStates() - 1;
    std::cout << "    " << n << " evenements pour 24 notes jouees\n";
    CHECK (n >= 22 && n <= 26, "segmentation : " << n << " evenements");
    int good = 0;
    for (int s = 1; s <= juce::jmin (n, 24); ++s)
    {
        const auto& g = m.segment (s);
        const int expect = kMotif[(s - 1) % 8];
        if (std::abs (g.pitch - (float) expect) < 0.5f) ++good;
    }
    std::cout << "    hauteurs correctes : " << good << " / " << juce::jmin (n, 24) << "\n";
    CHECK (good >= juce::jmin (n, 24) - 1, "hauteurs mal estimees");
    // la deuxieme occurrence du motif pointe vers la premiere avec un contexte qui grandit
    int maxLrs = 0, linkOk = 0;
    for (int s = 9; s <= juce::jmin (n, 24); ++s)
    {
        maxLrs = juce::jmax (maxLrs, m.lrs (s));
        if (m.sfx (s) == s - 8) ++linkOk;
    }
    std::cout << "    liens de suffixe vers l'occurrence precedente : " << linkOk << " ; contexte max " << maxLrs << "\n";
    CHECK (linkOk >= 12, "liens de suffixe incoherents");
    CHECK (maxLrs >= 10, "contexte (lrs) trop court");
}

static void testDrumAnalysis()
{
    std::cout << "[3] Analyse en mode timbre (batterie)\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 0, MDialogue);
    p.setParam (imId (IAct), 0.0f);
    Player pl; pl.kind = 1;
    run (p, pl, 4.0);
    const int n = p.mem.numStates() - 1;
    std::set<int> syms;
    for (int s = 1; s <= n; ++s) syms.insert (p.mem.segment (s).sym);
    std::cout << "    " << n << " evenements pour 16 frappes, " << syms.size() << " symboles de timbre differents\n";
    CHECK (n >= 14 && n <= 18, "segmentation batterie : " << n);
    CHECK (syms.size() >= 2 && *syms.begin() >= 1000, "symboles de timbre attendus");
}

static double energyBetween (const std::vector<float>& v, double a, double b)
{
    double e = 0;
    for (size_t i = (size_t) (a * FS); i < juce::jmin (v.size(), (size_t) (b * FS)); ++i) e += (double) v[i] * v[i];
    return e;
}

static void testDialogue()
{
    std::cout << "[4] Dialogue : elle repond quand tu te tais, s'arrete quand tu reprends\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDialogue);
    p.setParam (imId (IAct), 1.0f);
    p.setParam (imId (IReact), 0.3f);     // ~0.3 s
    p.setParam (imId (IPolite), 1.0f);
    p.setParam (imId (IPhrase), 0.9f);    // longues phrases
    Player pl;
    std::vector<float> out;
    run (p, pl, 8.0, 256, &out);              // tu joues 8 s
    const double ePlaying = energyBetween (out, 1.0, 8.0);
    pl.playing = false;
    run (p, pl, 3.0, 256, &out);              // silence : Maria repond
    const double eAnswer = energyBetween (out, 9.0, 11.0);
    pl.playing = true;
    run (p, pl, 2.0, 256, &out);              // tu reprends : elle s'arrete
    const double eAfter = energyBetween (out, 11.6, 13.0);
    std::cout << "    energie de Maria : pendant ton jeu " << ePlaying << " | dans ton silence " << eAnswer << " | apres ta reprise " << eAfter << "\n";
    CHECK (ePlaying < 1.0, "Maria joue pendant que tu joues (dialogue)");
    CHECK (eAnswer > 50.0, "Maria ne repond pas");
    CHECK (eAfter < eAnswer * 0.05, "Maria ne s'arrete pas quand tu reprends");
    CHECK (p.impro.phraseCount.load() >= 1, "aucune phrase");
}

static void testDuoAndMaterial()
{
    std::cout << "[5] Duo : elle joue avec toi, avec tes propres notes\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDuo);
    p.setParam (imId (IAct), 1.0f);
    p.setParam (imId (IGap), 0.0f);
    p.setParam (imId (IPolite), 0.0f);
    p.setParam (imId (IFollow), 0.5f);
    Player pl;
    p.setParamReal ("mix", 1.0f);
    std::vector<float> out;
    run (p, pl, 14.0, 256, &out);
    const double e = energyBetween (out, 6.0, 14.0);
    std::cout << "    energie de Maria pendant ton jeu : " << e << "\n";
    CHECK (e > 50.0, "en Duo elle devrait jouer pendant que tu joues");
    // hauteurs jouees : frequence par passage par zero sur des fenetres de 40 ms, comparee aux notes du motif
    int ok = 0, tot = 0;
    for (size_t a = (size_t) (6 * FS); a + 1920 < out.size(); a += 4800)
    {
        double en = 0; int zc = 0;
        for (size_t i = a; i < a + 1920; ++i) { en += (double) out[i] * out[i]; if (i > a && (out[i] >= 0) != (out[i - 1] >= 0)) ++zc; }
        if (en < 1.0) continue;
        ++tot;
        const double f = zc / 2.0 / (1920.0 / FS);
        for (int k : kMotif) { const double fk = 440.0 * std::pow (2.0, (k - 69) / 12.0); if (std::abs (f - fk) / fk < 0.06) { ++ok; break; } }
    }
    std::cout << "    fenetres sur les notes du motif : " << ok << " / " << tot << "\n";
    CHECK (tot > 10 && ok >= tot * 0.6, "Maria ne rejoue pas ton materiau");
}

static void testContinuity()
{
    std::cout << "[6] Continuite : citations longues contre recombinaison\n";
    int jumps[2];
    for (int k = 0; k < 2; ++k)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        bare (p, 3, MDuo);
        p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IPhrase), 1.0f); p.setParam (imId (IPolite), 0.0f);
        p.setParam (imId (ICont), k == 0 ? 1.0f : 0.0f);
        p.setParam (imId (ICtx), 0.0f);
        Player pl;
        run (p, pl, 6.0);
        const int s0 = p.impro.segCount.load();
        int j = 0, last = p.impro.jumpTo.load();
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
        for (int i = 0; i < (int) (FS * 10 / 256); ++i)
        {
            for (int s = 0; s < 256; ++s) { const float x = pl.next(); b.setSample (0, s, x); b.setSample (1, s, x); }
            p.processBlock (b, m); p.analyzeNow();
            const int jt = p.impro.jumpTo.load();
            if (jt != last) { ++j; last = jt; }
        }
        jumps[k] = j;
        std::cout << "    continuite " << (k == 0 ? "100 %" : "0 %  ") << " : " << j << " sauts pour " << (p.impro.segCount.load() - s0) << " evenements joues\n";
    }
    CHECK (jumps[1] > jumps[0] + 5, "la continuite ne change pas le comportement");
}

static void testClearLoadSave (const juce::File& dir)
{
    std::cout << "[7] Effacer, sauver la memoire, charger un fichier\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDialogue);
    p.setParam (imId (IAct), 0.0f);
    Player pl;
    run (p, pl, 5.0);
    CHECK (p.mem.numStates() > 10, "rien appris");
    auto wav = dir.getChildFile ("maria_memoire_test.wav");
    CHECK (p.saveMemory (wav) && wav.getSize() > 100000, "sauvegarde de la memoire");
    p.clearMemory();
    p.analyzeNow();
    CHECK (p.mem.numStates() == 1, "la memoire n'est pas effacee");

    MariaProcessor q;
    q.prepareToPlay (FS, 256);
    bare (q, 3, MDialogue);
    CHECK (q.loadFileNow (wav), "chargement du fichier");
    std::cout << "    memoire rechargee : " << (q.mem.numStates() - 1) << " evenements (avant effacement : environ " << (int) (5.0 * FS / (kNoteLen + kGapLen)) << ")\n";
    CHECK (q.mem.numStates() - 1 >= 12, "le fichier charge n'a pas ete analyse");
    // Maria peut jouer avec le fichier charge
    q.setParam (imId (IAct), 1.0f);
    q.impro.forcePhrase();
    Player silent; silent.kind = 2;
    std::vector<float> out;
    run (q, silent, 3.0, 256, &out);
    CHECK (energyBetween (out, 0.0, 3.0) > 10.0, "pas de phrase avec la memoire chargee");
}

static void testSeed()
{
    std::cout << "[8] Seed : meme seed = meme improvisation\n";
    auto render = [] (int seed, std::vector<float>& out)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (2);
        p.setParamReal ("seed", (float) seed);
        Player pl;
        run (p, pl, 10.0, 256, &out);
    };
    std::vector<float> a, b, c;
    render (5, a); render (5, b); render (6, c);
    float dab = 0, dac = 0;
    for (size_t i = 0; i < a.size(); ++i) { dab = juce::jmax (dab, std::abs (a[i] - b[i])); dac = juce::jmax (dac, std::abs (a[i] - c[i])); }
    std::cout << "    ecart meme seed " << dab << " ; autre seed " << dac << "\n";
    CHECK (dab < 1.0e-6f, "meme seed mais improvisation differente");
    CHECK (dac > 1.0e-3f, "seeds differentes mais rendu identique");
}

static void testStateScenesDice()
{
    std::cout << "[9] Etat, de, scenes, cadenas\n";
    MariaProcessor a;
    a.loadFactory (3);
    juce::Random rnd (99);
    for (auto* prm : a.getParameters())
        prm->setValueNotifyingHost (dynamic_cast<juce::AudioParameterBool*> (prm) != nullptr ? (rnd.nextBool() ? 1.0f : 0.0f) : rnd.nextFloat());
    a.lockIm[IMem].store (true); a.lockP[1][2].store (true);
    a.storeScene (4);
    juce::MemoryBlock mb;
    a.getStateInformation (mb);
    MariaProcessor b;
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    int diff = 0;
    for (int i = 0; i < a.getParameters().size(); ++i)
        if (std::abs (a.getParameters()[i]->getValue() - b.getParameters()[i]->getValue()) > 1.0e-4f) ++diff;
    CHECK (diff == 0, diff << " parametres differents apres rappel");
    CHECK (b.lockIm[IMem].load() && b.lockP[1][2].load() && b.sceneStored (4), "cadenas ou scenes perdus");

    MariaProcessor p;
    p.loadFactory (0);
    p.lockIm[ICont].store (true);
    const float c0 = p.imParam (ICont), a0 = p.imParam (IAct);
    p.dice (-1, false);
    CHECK (std::abs (p.imParam (ICont) - c0) < 1e-6f, "le de a change un reglage verrouille de Maria");
    CHECK (std::abs (p.imParam (IAct) - a0) > 1e-4f, "le de ne touche pas l'improvisatrice");
    p.undo();
    CHECK (std::abs (p.imParam (IAct) - a0) < 1e-4f, "undo");
    p.setParamReal ("morph", 0.0f);
    p.storeScene (2);
    const float act = p.imParam (IAct);
    p.dice (-1, true);
    p.recallScene (2);
    CHECK (std::abs (p.imParam (IAct) - act) < 1e-4f, "la scene ne rappelle pas l'improvisatrice");
}

static void testPresets()
{
    std::cout << "[10] Presets d'usine : 10 s de jeu puis 6 s de silence\n";
    for (int i = 0; i < numFactoryPresets(); ++i)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (i);
        Player pl;
        pl.kind = (sourceProfile (p.sourceIndex()).symMode == 0) ? 1 : 0;
        std::vector<float> out;
        const auto s1 = run (p, pl, 10.0, 256, &out);
        pl.playing = false;
        const auto s2 = run (p, pl, 6.0, 256, &out);
        const double e = energyBetween (out, 2.0, 16.0);
        std::cout << "    " << factoryPresetName (i).paddedRight (' ', 32) << " crete " << juce::String (juce::jmax (s1.peak, s2.peak), 3)
                  << "  phrases " << p.impro.phraseCount.load() << "\n";
        CHECK (s1.finite && s2.finite, factoryPresetName (i) << " non fini");
        CHECK (juce::jmax (s1.peak, s2.peak) <= 1.0f, factoryPresetName (i) << " sature");
        CHECK (e > 5.0 && p.impro.phraseCount.load() >= 1, factoryPresetName (i) << " : Maria ne joue pas");
    }
}

static void testAdapt()
{
    std::cout << "[11] Adapter a chaque source\n";
    for (int s = 0; s < kNumSources; ++s)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        p.setParamReal ("source", (float) s);
        p.adaptToSource();
        Player pl; pl.kind = sourceProfile (s).symMode == 0 ? 1 : 0;
        std::vector<float> out;
        const auto st = run (p, pl, 6.0, 256, &out);
        pl.playing = false;
        const auto st2 = run (p, pl, 4.0, 256, &out);
        CHECK (p.sourceIndex() == s && st.finite && st2.finite && juce::jmax (st.peak, st2.peak) <= 1.0f && p.mem.numStates() > 5,
               "adaptation " << sourceProfile (s).name);
    }
}

static void testFuzzCpu()
{
    std::cout << "[12] Fuzz (40 s) et charge CPU\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 512);
    p.loadFactory (16);
    juce::Random rnd (1234);
    juce::AudioBuffer<float> b (2, 512);
    juce::MidiBuffer m;
    Player pl;
    bool fin = true; float pk = 0;
    const auto t0 = std::chrono::high_resolution_clock::now();
    double analysisTime = 0;
    for (int64_t pos = 0; pos < (int64_t) (FS * 40); )
    {
        const int n = 1 + rnd.nextInt (512);
        b.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i) { const float x = pl.next(); b.setSample (0, i, x); b.setSample (1, i, x); }
        if (rnd.nextFloat() < 0.03f)
        {
            const auto& ps = p.getParameters();
            for (int k = 0; k < 4; ++k) ps[rnd.nextInt (ps.size())]->setValueNotifyingHost (rnd.nextFloat());
            p.setParamReal ("in", 0.0f); p.setParamReal ("out", 0.0f); p.setParamReal ("learn", 1.0f);
            if (rnd.nextFloat() < 0.1f) pl.playing = ! pl.playing;
            if (rnd.nextFloat() < 0.05f) p.clearMemory();
        }
        p.processBlock (b, m);
        const auto a0 = std::chrono::high_resolution_clock::now();
        p.analyzeNow();
        analysisTime += std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - a0).count();
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) { const float v = b.getSample (c, i); if (! std::isfinite (v)) fin = false; pk = juce::jmax (pk, std::abs (v)); }
        pos += n;
    }
    const double el = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - t0).count();
    std::cout << "    crete max " << pk << " ; 40 s traitees en " << el << " s (" << juce::String (100.0 * el / 40.0, 1)
              << " % d'un coeur, dont analyse " << juce::String (100.0 * analysisTime / 40.0, 1) << " %)\n";
    CHECK (fin, "valeurs non finies");
    CHECK (pk <= 1.5f, "crete excessive");
    CHECK (el < 40.0 * 0.5, "trop lourd");
}

static void testMidi()
{
    std::cout << "[13] MIDI : phrase, silence, ecoute\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDialogue);
    p.setParam (imId (IAct), 0.0f);
    Player pl;
    run (p, pl, 5.0);
    juce::AudioBuffer<float> b (2, 256); b.clear();
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
    p.processBlock (b, m);
    m.clear();
    pl.playing = false;
    std::vector<float> out;
    run (p, pl, 1.0, 256, &out);
    CHECK (p.impro.phraseCount.load() >= 1 && energyBetween (out, 0.0, 1.0) > 1.0, "la note 48 ne lance pas de phrase");
    m.addEvent (juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100), 0);
    p.processBlock (b, m);
    p.morphTick();
    CHECK (p.apvts.getRawParameterValue ("learn")->load() < 0.5f, "la note 50 ne coupe pas l'ecoute");
}

static void testTempo()
{
    std::cout << "[15] Tempo manuel : evenements cales sur la grille, phrases sur le temps\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 32);
    bare (p, 3, MDuo);
    p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IPolite), 0.0f); p.setParam (imId (IPhrase), 0.6f);
    p.setParamReal ("im_tempo", 2.0f);
    p.setParamReal ("bpm", 120.0f);
    p.setParamReal ("im_grid", 4.0f);    // 1/8 = 12000 echantillons
    p.setParamReal ("im_align", 0.0f);   // phrases sur le temps = 24000 echantillons
    Player pl;
    juce::AudioBuffer<float> b (2, 32);
    juce::MidiBuffer m;
    int lastSeg = p.impro.segCount.load(), lastPh = p.impro.phraseCount.load();
    int onGrid = 0, total = 0, phOnBeat = 0, phTotal = 0;
    for (int64_t pos = 0; pos < (int64_t) (FS * 20); pos += 32)
    {
        for (int i = 0; i < 32; ++i) { const float x = pl.next(); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
        if ((pos & 255) == 0) p.analyzeNow();
        const int sc = p.impro.segCount.load(), pc = p.impro.phraseCount.load();
        if (sc != lastSeg)
        {
            const int64_t r = pos % 12000;
            ++total; if (r < 64 || r > 12000 - 64) ++onGrid;
            lastSeg = sc;
        }
        if (pc != lastPh)
        {
            const int64_t r = pos % 24000;
            ++phTotal; if (r < 64 || r > 24000 - 64) ++phOnBeat;
            lastPh = pc;
        }
    }
    std::cout << "    evenements sur la grille de croches : " << onGrid << " / " << total << " ; phrases sur le temps : " << phOnBeat << " / " << phTotal << "\n";
    CHECK (total > 10 && onGrid == total, "evenements hors grille");
    CHECK (phTotal >= 1 && phOnBeat == phTotal, "phrases hors temps");
}

static void testResize()
{
    std::cout << "[16] Fenetre redimensionnable\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    const int w0 = ed->getWidth(), h0 = ed->getHeight();
    ed->setSize (700, (int) std::round (700.0 * h0 / w0));
    CHECK (ed->isResizable(), "fenetre non redimensionnable");
    CHECK (std::abs (p.uiScale.load() - 700.0f / 1294.0f) < 0.01f, "l'echelle ne suit pas la fenetre : " << p.uiScale.load());
    juce::MemoryBlock mb; p.getStateInformation (mb);
    MariaProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
    std::unique_ptr<juce::AudioProcessorEditor> ed2 (q.createEditor());
    CHECK (std::abs (ed2->getWidth() - 700) <= 2, "la taille n'est pas memorisee (" << ed2->getWidth() << ")");
}

static void testSelfListening()
{
    std::cout << "[17] Anti-repiquage : Maria entendue par le micro (sono)\n";
    int learned[2]; bool stopped[2];
    for (int k = 0; k < 2; ++k)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        bare (p, 3, MDialogue);
        p.setParam (imId (IAct), 1.0f); p.setParam (imId (IPolite), 1.0f); p.setParam (imId (IPhrase), 1.0f); p.setParam (imId (IReact), 0.2f);
        p.setParamReal ("im_self", k == 0 ? 0.0f : 1.0f);
        Player pl;
        run (p, pl, 6.0);                       // tu joues : elle apprend
        pl.playing = false;
        const int before = p.mem.numStates();
        // silence du musicien, mais le micro capte sa voix a -10 dB (retour de sono)
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
        std::vector<float> last (256, 0.0f);
        bool everPlayed = false; int stopsAfterPlay = 0;
        for (int i = 0; i < (int) (FS * 6 / 256); ++i)
        {
            for (int s = 0; s < 256; ++s) { b.setSample (0, s, last[(size_t) s] * 0.3f); b.setSample (1, s, last[(size_t) s] * 0.3f); }
            p.processBlock (b, m); p.analyzeNow();
            for (int s = 0; s < 256; ++s) last[(size_t) s] = b.getSample (0, s);
            const bool pl2 = p.impro.isPlaying();
            if (pl2) everPlayed = true; else if (everPlayed) ++stopsAfterPlay;
        }
        learned[k] = p.mem.numStates() - before;
        stopped[k] = stopsAfterPlay > 0;
        std::cout << "    " << (k == 0 ? "Apprend tout     " : "Ignore sa voix   ") << " : " << learned[k] << " evenements appris de sa propre voix, "
                  << (stopped[k] ? "elle s'est interrompue" : "elle a continue") << "\n";
    }
    CHECK (learned[1] <= 1, "elle apprend encore sa propre voix (" << learned[1] << ")");
    CHECK (learned[0] > learned[1], "le mode 'Apprend tout' devrait apprendre sa voix");
}

static void testSessionMemory (const juce::File&)
{
    std::cout << "[18] Memoire liee au set (sauvee / rechargee avec l'etat)\n";
    juce::MemoryBlock mb;
    juce::File f;
    int n0 = 0;
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        bare (p, 3, MDialogue);
        p.setParam (imId (IAct), 0.0f);
        Player pl;
        run (p, pl, 6.0);
        n0 = p.mem.numStates();
        p.getStateInformation (mb);
        p.serviceBackground();
        f = p.sessionMemoryFile();
        CHECK (f.existsAsFile() && f.getSize() > 100000, "memoire de session non ecrite");
    }
    MariaProcessor q;
    q.prepareToPlay (FS, 256);
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    q.serviceBackground();
    std::cout << "    " << (n0 - 1) << " evenements avant, " << (q.mem.numStates() - 1) << " apres rechargement\n";
    CHECK (q.mem.numStates() >= n0 - 3, "memoire non rechargee");
    // une 2e instance avec le meme etat (piste dupliquee) ne reprend pas le meme identifiant
    MariaProcessor r;
    r.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (r.memoryId() != q.memoryId(), "deux instances partagent le meme fichier de memoire");
    f.deleteFile();
}

static double zcFreq (const std::vector<float>& v, size_t a, size_t len)
{
    int zc = 0; double en = 0;
    for (size_t i = a + 1; i < a + len && i < v.size(); ++i) { en += (double) v[i] * v[i]; if ((v[i] >= 0) != (v[i - 1] >= 0)) ++zc; }
    return en < 0.5 ? -1.0 : zc / 2.0 / ((double) len / FS);
}

static void testPitchOnly()
{
    std::cout << "[19] Transposition 'hauteur seule' : +12 sans changer la duree\n";
    double durRatio[2] {}; int okOct[2] {};
    for (int k = 0; k < 2; ++k)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        bare (p, 3, MDuo);
        p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IPolite), 0.0f); p.setParam (imId (ICont), 1.0f);
        p.setParam (imId (ITransp), 0.5f + 12.0f / 48.0f);
        p.setParamReal ("im_tmode", (float) k);
        Player pl;
        run (p, pl, 6.0);
        const int s0 = p.impro.segCount.load();
        std::vector<float> out;
        run (p, pl, 8.0, 256, &out);
        const int segs = p.impro.segCount.load() - s0;
        durRatio[k] = 8.0 / juce::jmax (1, segs) / ((kNoteLen + kGapLen) / FS);
        int tot = 0;
        for (size_t a = 0; a + 1920 < out.size(); a += 4800)
        {
            const double f = zcFreq (out, a, 1920);
            if (f < 0) continue;
            ++tot;
            for (int n : kMotif) { const double fk = 2.0 * 440.0 * std::pow (2.0, (n - 69) / 12.0); if (std::abs (f - fk) / fk < 0.07) { ++okOct[k]; break; } }
        }
        std::cout << "    " << (k == 0 ? "Bande       " : "Hauteur seule") << " : duree d'un evenement x" << juce::String (durRatio[k], 2)
                  << " ; fenetres a l'octave : " << okOct[k] << " / " << tot << "\n";
    }
    CHECK (durRatio[0] < 0.7, "en mode bande, +12 devrait raccourcir les evenements");
    CHECK (durRatio[1] > 0.8 && durRatio[1] < 1.25, "en hauteur seule, la duree doit etre gardee");
    CHECK (okOct[1] >= 10, "en hauteur seule, la transposition d'octave n'est pas entendue");
}

static void testRandomTMode()
{
    std::cout << "[19b] Transposition 'aleatoire' : tirage bande / hauteur seule par segment\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDuo);
    p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IPolite), 0.0f); p.setParam (imId (ICont), 1.0f);
    p.setParam (imId (ITransp), 0.5f + 12.0f / 48.0f);
    p.setParamReal ("im_tmode", 2.0f);
    CHECK (p.transpMode() == 2, "le parametre im_tmode n'accepte pas la valeur 'aleatoire'");
    Player pl;
    run (p, pl, 14.0);
    const int a = p.impro.tmodeCount[0].load(), b = p.impro.tmodeCount[1].load();
    std::cout << "    segments en bande : " << a << " ; en hauteur seule : " << b << "\n";
    CHECK (a >= 3 && b >= 3, "en mode aleatoire, les deux modes devraient etre tires");
    // le mode fait partie des scenes
    p.setParamReal ("morph", 0.0f); p.setParamReal ("im_tmode", 0.0f); p.storeScene (0);
    p.setParamReal ("im_tmode", 1.0f); p.recallScene (0); p.morphTick();
    CHECK (p.transpMode() == 0, "le mode de transposition n'est pas rappele par les scenes");
}

static void testChords()
{
    std::cout << "[20] Piano : reconnaissance d'accords\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 6, MDialogue);       // piano (hybride)
    p.setParam (imId (IAct), 0.0f);
    Player pl; pl.kind = 3;
    run (p, pl, 26400.0 * 8 / FS + 0.3);
    static const int expect[4] { 0, 12 + 9, 5, 7 };   // C, Am, F, G
    int ok = 0, n = p.mem.numStates() - 1;
    for (int s = 1; s <= n; ++s)
    {
        const auto& g = p.mem.segment (s);
        const int e = expect[(int) (g.start / 26400) % 4];
        if (g.chord == e) ++ok;
    }
    std::cout << "    " << ok << " / " << n << " evenements reconnus comme le bon accord (C, Am, F, G)\n";
    CHECK (n >= 6 && ok >= n - 1, "accords mal reconnus");
}

static void testVoice2AndMidi()
{
    std::cout << "[21] 2e voix et sortie MIDI\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDuo);
    p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IPolite), 0.0f);
    p.setParamReal ("v2_on", 1.0f); p.setParamReal ("v2_mode", (float) MDuo); p.setParamReal ("v2_pan", 1.0f);
    p.setParamReal ("midi_out", 1.0f);
    Player pl;
    run (p, pl, 6.0);
    juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
    double eL = 0, eR = 0, diff = 0;
    std::set<int> notes; int ons = 0, offs = 0; std::set<int> chans;
    for (int i = 0; i < (int) (FS * 8 / 256); ++i)
    {
        for (int s = 0; s < 256; ++s) { const float x = pl.next(); b.setSample (0, s, x); b.setSample (1, s, x); }
        m.clear();
        p.processBlock (b, m); p.analyzeNow();
        for (int s = 0; s < 256; ++s) { const float a = b.getSample (0, s), c = b.getSample (1, s); eL += a * a; eR += c * c; diff += (a - c) * (a - c); }
        for (const auto meta : m)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) { ++ons; notes.insert (msg.getNoteNumber()); chans.insert (msg.getChannel()); }
            else if (msg.isNoteOff()) ++offs;
        }
    }
    int inMotif = 0;
    for (int nn : notes) for (int k : kMotif) if (nn == k) { ++inMotif; break; }
    std::cout << "    energie G " << eL << " / D " << eR << " ; difference G-D " << diff << " ; " << ons << " notes MIDI (" << offs << " note-off), "
              << notes.size() << " hauteurs dont " << inMotif << " du motif, canaux " << chans.size() << "\n";
    CHECK (p.impro2.phraseCount.load() >= 1, "la 2e voix ne joue pas");
    CHECK (diff > 0.1 * (eL + eR) * 0.5, "les deux voix ne sont pas separees dans la stereo");
    CHECK (ons > 10 && std::abs (ons - offs) <= 4, "sortie MIDI incomplete");
    CHECK (inMotif == (int) notes.size(), "notes MIDI hors de ton jeu");
    CHECK (chans.size() == 2, "la 2e voix devrait sortir sur le canal suivant");
}

static void testPanicAndEngines()
{
    std::cout << "[22] Panique et moteurs a la demande\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDuo);
    const float v[6] { idxF (2, 5), 0.9f, 0.9f, 0.3f, 0.0f, 0.3f };
    p.setParam (sid (0, "type"), (float) TReverb / (float) (kNumTypes - 1));
    for (int k = 0; k < 6; ++k) p.setParam (pid (0, k), v[k]);
    p.setParam (pid (0, kMix), 1.0f);
    p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f);
    Player pl;
    run (p, pl, 6.0);
    p.setParamReal ("freeze", 1.0f);
    run (p, pl, 1.0);
    juce::AudioBuffer<float> b (2, 256); b.clear(); juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 47, (juce::uint8) 100), 0);
    p.processBlock (b, m);
    pl.playing = false;
    std::vector<float> out;
    run (p, pl, 0.1, 256, &out);
    double e = 0; for (size_t i = 2400; i < out.size(); ++i) e += out[i] * out[i];
    CHECK (e < 1.0e-6, "la panique ne coupe pas le son (" << e << ")");
    CHECK (p.apvts.getRawParameterValue ("freeze")->load() < 0.5f, "la panique ne coupe pas le freeze");
    // moteurs prepares a la demande puis liberes
    juce::Random rnd (3);
    for (int i = 0; i < 40; ++i) { p.setParam (sid (rnd.nextInt (kNumSlots), "type"), rnd.nextFloat()); run (p, pl, 0.05); }
    const int before = p.preparedEngines();
    p.serviceEngines (0.0);
    const int after = p.preparedEngines();
    int active = 0; for (int s = 0; s < kNumSlots; ++s) if (p.slotType (s) != 0) ++active;
    std::cout << "    moteurs prepares : " << before << " apres 40 changements, " << after << " apres liberation (" << active << " en service)\n";
    CHECK (after <= active && before > after, "la liberation des moteurs ne fonctionne pas");
    run (p, pl, 0.5);
    for (int s = 0; s < kNumSlots; ++s) if (p.slotType (s) != 0) CHECK (p.preparedEngines() >= 1, "moteur en service perdu");
}


// ---------------------------------------------------------------------------
// Outils pour les tests du jeu a plusieurs
struct Tones
{
    std::vector<int> notes; std::vector<float> amps;
    int len = 14400, gap = 2400; int64_t t = 0; bool playing = true;
    float next()
    {
        ++t;
        if (! playing || notes.empty()) return 0.0f;
        const int64_t per = len + gap, k = t % per;
        if (k >= len) return 0.0f;
        const size_t idx = (size_t) ((t / per) % (int64_t) notes.size());
        const double f = 440.0 * std::pow (2.0, (notes[idx] - 69) / 12.0);
        const double s = (double) k / FS;
        const double env = std::min (1.0, s / 0.01) * std::min (1.0, (double) (len - k) / 480.0);
        double y = 0;
        for (int h = 1; h <= 5; ++h) y += std::sin (2 * juce::MathConstants<double>::pi * f * h * s) / (double) h;
        const float a = amps.empty() ? 0.35f : amps[idx % amps.size()];
        return (float) (a * env * y);
    }
};

struct Chord   // accord tenu (orgue)
{
    std::vector<int> notes; double t = 0; float amp = 0.12f; bool playing = true;
    float next()
    {
        t += 1.0 / FS;
        if (! playing) return 0.0f;
        double y = 0;
        for (int nn : notes) { const double f = 440.0 * std::pow (2.0, (nn - 69) / 12.0); for (int h = 1; h <= 3; ++h) y += std::sin (2 * juce::MathConstants<double>::pi * f * h * t) / (h * h); }
        return (float) (amp * y);
    }
};

struct MidiLog { std::vector<int> notes; std::vector<int> vels; };

static void runG (MariaProcessor& p, std::function<float()> main, double seconds, MidiLog* log = nullptr,
                  std::function<float()> side = nullptr, double* eL = nullptr, double* eR = nullptr)
{
    const int block = 256;
    const int nch = side ? 4 : 2;
    juce::AudioBuffer<float> b (nch, block);
    juce::MidiBuffer midi;
    const int64_t total = (int64_t) (seconds * FS);
    for (int64_t pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i)
        {
            const float x = main ? main() : 0.0f;
            b.setSample (0, i, x); b.setSample (1, i, x);
            if (side) { const float y = side(); b.setSample (2, i, y); b.setSample (3, i, y); }
        }
        midi.clear();
        p.processBlock (b, midi);
        p.analyzeNow();
        p.morphTick();
        if (log != nullptr)
            for (const auto meta : midi)
            {
                const auto m = meta.getMessage();
                if (m.isNoteOn()) { log->notes.push_back (m.getNoteNumber()); log->vels.push_back (m.getVelocity()); }
            }
        for (int i = 0; i < block; ++i)
        {
            if (eL != nullptr) *eL += (double) b.getSample (0, i) * b.getSample (0, i);
            if (eR != nullptr) *eR += (double) b.getSample (1, i) * b.getSample (1, i);
        }
    }
}

static void duoSetup (MariaProcessor& p, int mode = MDuo)
{
    bare (p, 3, mode);
    p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IPolite), 0.0f);
    p.setParam (imId (IFollow), 0.5f); p.setParam (imId (IMem), 1.0f);
    p.setParamReal ("midi_out", 1.0f);
}

static void testHarmony()
{
    std::cout << "[23] Reactivite harmonique : Maria joue dans l'accord entendu (do majeur)\n";
    double ratio[3] {};
    int shifts = 0;
    for (int k = 0; k < 3; ++k)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p);
        p.setParam (imId (ICont), 0.0f); p.setParam (imId (ICtx), 0.0f);
        Tones chrom; chrom.notes = { 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71 };
        p.setParam (imId (IAct), 0.0f);
        runG (p, [&] { return chrom.next(); }, 12.0);          // apprend 12 hauteurs
        p.setParamReal ("learn", 0.0f);
        p.setParam (imId (IAct), 1.0f);
        p.setParamReal ("im_harm", k == 0 ? 0.0f : 1.0f);
        p.setParamReal ("im_harmmode", k == 2 ? 1.0f : 0.0f);
        Chord c; c.notes = { 48, 52, 55, 60, 64, 67 };
        MidiLog log;
        runG (p, [&] { return c.next(); }, 14.0, &log);
        int in = 0;
        for (int nn : log.notes) { const int pc = nn % 12; if (pc == 0 || pc == 4 || pc == 7) ++in; }
        ratio[k] = log.notes.empty() ? 0.0 : (double) in / (double) log.notes.size();
        if (k == 2) shifts = p.impro.harmShifts.load();
        std::cout << "    " << (k == 0 ? "sans harmonie        " : (k == 1 ? "harmonie : choisir   " : "choisir + transposer ")) << ": "
                  << (int) std::round (ratio[k] * 100) << "% de notes dans l'accord (" << log.notes.size() << " notes)"
                  << (k == 0 ? juce::String (" ; accord entendu : ") + chordName (p.listener.state.chord.load()) : juce::String()) << "\n";
        if (k == 0) CHECK (p.listener.state.chord.load() == 0, "l'ecoute en direct ne reconnait pas l'accord de do");
    }
    CHECK (ratio[1] > ratio[0] + 0.25, "le choix harmonique ne favorise pas les notes de l'accord");
    CHECK (ratio[2] > 0.8, "la transposition harmonique ne ramene pas les notes dans l'accord");
    CHECK (shifts > 3, "aucune transposition harmonique");
}

static void testMotifs()
{
    std::cout << "[24] Leitmotivs : marquer un passage, le rappeler, le garder avec le set\n";
    juce::MemoryBlock mb;
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p, MDialogue);
        p.setParam (imId (IAct), 0.0f); p.setParam (imId (IGap), 1.0f);
        p.setParam (imId (IMem), 1.0f); p.setParam (imId (ICtx), 0.0f); p.setParam (imId (ICont), 0.3f);
        Tones a; a.notes = { 60, 62, 64, 65, 67 };
        Tones b; b.notes = { 64, 65, 71, 72, 74, 67, 69 };   // partage des notes avec le theme
        runG (p, [&] { return a.next(); }, 5.0);
        runG (p, nullptr, 2.0);                   // la phrase se ferme
        juce::MidiBuffer mk; mk.addEvent (juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), 0);   // marquer A (note 52)
        juce::AudioBuffer<float> z (2, 256); z.clear();
        p.processBlock (z, mk);
        runG (p, [&] { return b.next(); }, 6.0);
        runG (p, nullptr, 1.0);
        CHECK (p.motifSeconds (0) > 3.5f && p.motifSeconds (0) < 6.0f, "leitmotiv A mal marque (" << p.motifSeconds (0) << " s)");
        int lo = 0, hi = 0;
        const int ns = p.motifStates (0, lo, hi);
        for (int var = 0; var < 2; ++var)
        {
            p.setParamReal ("im_motvar", var == 0 ? 0.0f : 1.0f);
            const int m0 = p.impro.motifSegs.load(), i0 = p.impro.motifSegsInside.load();
            MidiLog log;
            p.impro.forceSilence(); runG (p, nullptr, 0.3);
            log.notes.clear();
            p.triggerMotif (0);
            runG (p, nullptr, 7.0, &log);
            int inA = 0;
            for (int nn : log.notes) for (int q : a.notes) if (nn == q) { ++inA; break; }
            const int segs = p.impro.motifSegs.load() - m0, inside = p.impro.motifSegsInside.load() - i0;
            std::cout << "    variation " << (var == 0 ? "0  " : "100") << "% : " << segs << " evenements joues, " << inside << " dans le theme ; "
                      << inA << " / " << log.notes.size() << " notes MIDI du theme (theme : " << ns << " evenements)\n";
            if (var == 0)
            {
                CHECK (segs >= ns - 2 && inside == segs, "sans variation, le leitmotiv n'est pas rejoue tel quel");
                CHECK (! log.notes.empty() && inA == (int) log.notes.size(), "le leitmotiv joue d'autres notes que le theme");
            }
            else CHECK (inside < segs, "avec variation, Maria ne s'ecarte jamais du theme");
        }
        CHECK (p.impro.motifPhrases.load() == 2, "le leitmotiv n'a pas ete lance");
        p.getStateInformation (mb);
        p.serviceBackground();
    }
    MariaProcessor q;
    q.prepareToPlay (FS, 256);
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    q.serviceBackground();
    int lo = 0, hi = 0;
    const int ns = q.motifStates (0, lo, hi);
    std::cout << "    apres reouverture du set : leitmotiv A = " << juce::String (q.motifSeconds (0), 2) << " s, " << ns << " evenements\n";
    CHECK (ns >= 3 && q.motifSeconds (0) > 3.5f, "leitmotiv perdu a la reouverture du set");
    if (ns > 0)
    {
        bool allA = true;
        for (int s2 = lo; s2 <= hi; ++s2) { const int pc = (int) std::round (q.mem.segment (s2).pitch); if (pc != 60 && pc != 62 && pc != 64 && pc != 65 && pc != 67) allA = false; }
        CHECK (allA, "le leitmotiv recharge ne pointe plus sur le bon passage");
    }
}

static bool enableSidechain (MariaProcessor& p)
{
    auto layout = p.getBusesLayout();
    if (layout.inputBuses.size() < 2) return false;
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    const bool ok = p.setBusesLayout (layout);
    p.prepareToPlay (FS, 256);
    return ok;
}

static void testSidechain()
{
    std::cout << "[25] Ecoute croisee : apprendre un musicien, reagir a un autre (sidechain)\n";
    {
        MariaProcessor p;
        CHECK (enableSidechain (p), "le bus sidechain n'est pas accepte");
        duoSetup (p, MDialogue);
        p.setParam (imId (IAct), 0.0f);
        p.setParamReal ("sc_learn", 1.0f);           // apprend la sidechain
        Tones t; t.notes = { 72, 74, 76 };
        runG (p, nullptr, 6.0, nullptr, [&] { return t.next(); });
        const int n1 = p.mem.numStates();
        CHECK (p.sidechainConnected(), "sidechain non detectee");
        CHECK (n1 > 10, "Maria n'apprend pas la sidechain (" << n1 << ")");
        bool ok = true;
        for (int s2 = p.mem.firstValidState(); s2 < n1; ++s2) { const float pt = p.mem.segment (s2).pitch; if (pt > 0 && (pt < 71 || pt > 77)) ok = false; }
        CHECK (ok, "la memoire contient autre chose que la sidechain");
        p.setParamReal ("sc_learn", 0.0f);
        Chord c; c.notes = { 48, 52, 55 };
        runG (p, nullptr, 3.0, nullptr, [&] { return c.next(); });
        CHECK (std::abs (p.mem.numStates() - n1) <= 1, "en 'Piste', Maria apprend encore la sidechain");
    }
    {
        // apprend la guitare (piste), se tait tant que l'autre musicien (sidechain) joue, repond quand il s'arrete
        MariaProcessor p;
        enableSidechain (p);
        duoSetup (p, MDialogue);
        p.setParam (imId (IAct), 1.0f); p.setParam (imId (IReact), 0.1f);
        Tones g; g.notes = { 60, 64, 67, 65 };
        p.setParamReal ("sc_react", 1.0f);
        Tones other; other.notes = { 50, 53 }; other.gap = 600;
        int ph0 = 0, ph1 = 0, ph2 = 0;
        runG (p, [&] { return g.next(); }, 6.0, nullptr, [&] { return other.next(); });   // guitare + autre musicien
        ph0 = p.impro.phraseCount.load();
        runG (p, nullptr, 4.0, nullptr, [&] { return other.next(); });                       // seul l'autre joue
        ph1 = p.impro.phraseCount.load();
        runG (p, nullptr, 4.0, nullptr, nullptr);                                              // silence de tous
        ph2 = p.impro.phraseCount.load();
        std::cout << "    phrases : " << ph0 << " pendant le jeu, +" << (ph1 - ph0) << " quand seul l'autre joue, +" << (ph2 - ph1) << " dans le silence\n";
        CHECK (ph1 - ph0 == 0, "en Dialogue, Maria parle par-dessus le musicien de la sidechain");
        CHECK (ph2 - ph1 >= 1, "Maria ne repond pas quand le musicien de la sidechain se tait");
    }
}

static juce::File writeTonesWav (const juce::File& f, std::vector<int> notes, double seconds)
{
    f.deleteFile();
    Tones t; t.notes = std::move (notes);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), FS, 1, 24, {}, 0));
    os.release();
    juce::AudioBuffer<float> b (1, (int) (seconds * FS));
    for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, t.next());
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

static void testCorpus (const juce::File& dir)
{
    std::cout << "[26] Corpus externe : improviser avec un enregistrement prepare, melange ou non au direct\n";
    const auto wav = writeTonesWav (dir.getChildFile ("maria_corpus_test.wav"), { 79, 81, 83, 84 }, 10.0);
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    duoSetup (p);
    CHECK (p.loadCorpusNow (wav), "corpus non charge");
    std::cout << "    corpus '" << p.corpusName() << "' : " << juce::String (p.corpusSeconds(), 1) << " s, " << (p.corpus.numStates() - 1) << " evenements\n";
    CHECK (p.corpus.numStates() > 20, "corpus mal analyse");
    // memoire du direct vide, corpus seul
    p.setParamReal ("corpus_mix", 1.0f);
    MidiLog log;
    runG (p, nullptr, 5.0, &log);
    int fromC = 0; juce::String other; for (int nn : log.notes) { if (nn >= 79) ++fromC; else other << nn << " "; }
    std::cout << "    sans jouer, corpus seul : " << log.notes.size() << " notes, " << fromC << " du corpus " << other << "\n";
    CHECK (log.notes.size() > 6 && fromC == (int) log.notes.size(), "Maria ne joue pas le corpus");
    // melange : direct + corpus
    Tones g; g.notes = { 60, 62, 64 };
    p.setParam (imId (IAct), 0.0f);
    runG (p, [&] { return g.next(); }, 6.0);
    p.setParam (imId (IAct), 1.0f); p.setParam (imId (IPhrase), 0.15f);
    for (float mix : { 0.0f, 0.5f, 1.0f })
    {
        p.setParamReal ("corpus_mix", mix);
        p.impro.forceSilence(); runG (p, nullptr, 0.3);
        const int c0 = p.impro.corpusSegs.load(), l0 = p.impro.liveSegs.load();
        runG (p, nullptr, 16.0);
        const int dc = p.impro.corpusSegs.load() - c0, dl = p.impro.liveSegs.load() - l0;
        std::cout << "    melange " << (int) (mix * 100) << "% : " << dl << " evenements du direct, " << dc << " du corpus\n";
        if (mix == 0.0f) CHECK (dc == 0 && dl > 5, "a 0 %, Maria devrait jouer seulement le direct");
        if (mix == 0.5f) CHECK (dc > 2 && dl > 2, "a 50 %, Maria devrait melanger direct et corpus");
        if (mix == 1.0f) CHECK (dl <= 2 && dc > 5, "a 100 %, Maria devrait jouer seulement le corpus");
    }
    // le corpus suit le set
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    MariaProcessor q;
    q.prepareToPlay (FS, 256);
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    q.serviceBackground();
    CHECK (q.corpusReady.load() && q.corpusName() == "maria_corpus_test", "le corpus n'est pas recharge avec le set");
    p.clearCorpus();
    p.serviceBackground();
    CHECK (! p.corpusReady.load() && p.corpusName().isEmpty(), "le corpus n'est pas retire");
    runG (p, nullptr, 1.0);
    wav.deleteFile();
}

static void testContrast()
{
    std::cout << "[27] Mode Contraste : doux et rare quand les musiciens jouent fort, l'inverse dans le calme\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    duoSetup (p, MContrast);
    p.setParam (imId (ICont), 0.0f); p.setParam (imId (ICtx), 0.0f);
    Tones t; t.notes = { 60, 62, 64, 65 }; t.amps = { 0.6f, 0.03f, 0.6f, 0.03f };   // forts et doux en alternance
    p.setParam (imId (IAct), 0.0f);
    runG (p, [&] { return t.next(); }, 10.0);
    p.setParamReal ("learn", 0.0f);
    p.setParam (imId (IAct), 1.0f);
    Tones loud; loud.notes = { 48, 50 }; loud.amps = { 0.8f }; loud.len = 4000; loud.gap = 2000;
    MidiLog a, b;
    double eA = 0, eB = 0;
    runG (p, [&] { return loud.next(); }, 10.0, &a, nullptr, &eA);
    runG (p, nullptr, 1.0);
    runG (p, nullptr, 10.0, &b, nullptr, &eB);
    auto mean = [] (const std::vector<int>& v) { double s2 = 0; for (int x : v) s2 += x; return v.empty() ? 0.0 : s2 / (double) v.size(); };
    std::cout << "    musiciens forts : velocite moyenne " << (int) mean (a.vels) << " (" << a.vels.size() << " notes) ; silence : " << (int) mean (b.vels)
              << " (" << b.vels.size() << " notes)\n";
    CHECK (mean (b.vels) > mean (a.vels) + 15.0, "le contraste n'inverse pas l'energie de Maria");
    CHECK (b.vels.size() > a.vels.size(), "le contraste ne rend pas Maria plus rare quand les musiciens sont actifs");
}

static void testSplitOutputs()
{
    std::cout << "[28] Sorties separees : voix 1 a gauche, voix 2 a droite\n";
    double res[2][2] {};
    for (int k = 0; k < 2; ++k)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p);
        p.setParamReal ("v2_on", 1.0f); p.setParamReal ("v2_mode", (float) MDuo);
        p.setParamReal ("out_route", 1.0f);
        p.setParamReal ("mix", 1.0f);
        if (k == 0) { p.setParam (imId (ILevel), 0.0f); p.setParamReal ("v2_level", 0.8f); }     // voix 1 muette
        else        { p.setParam (imId (ILevel), 0.7f); p.setParamReal ("v2_level", 0.0f); }      // voix 2 muette
        Tones g; g.notes = { 60, 64, 67 };
        runG (p, [&] { return g.next(); }, 5.0);
        runG (p, nullptr, 6.0, nullptr, nullptr, &res[k][0], &res[k][1]);
        std::cout << "    " << (k == 0 ? "voix 2 seule" : "voix 1 seule") << " : energie G " << juce::String (res[k][0], 3) << " / D " << juce::String (res[k][1], 3) << "\n";
    }
    CHECK (res[0][1] > 1.0 && res[0][0] < res[0][1] * 0.01, "la voix 2 ne sort pas seule a droite");
    CHECK (res[1][0] > 1.0 && res[1][1] < res[1][0] * 0.01, "la voix 1 ne sort pas seule a gauche");
}

// ---------------------------------------------------------------------------
static void testPulse()
{
    std::cout << "[29] Pulsation des musiciens : tempo et phase sans clic\n";
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p);
        p.setParamReal ("im_tempo", 3.0f);
        Player pl; pl.kind = 1;                       // batterie a 120 BPM (croches)
        runG (p, [&] { return pl.next(); }, 10.0);
        const float bpm = p.pulse.bpmView.load();
        const double S = (double) p.pulse.sampleView.load();
        double truePh = std::fmod (S / 24000.0, 1.0);
        double err = std::abs ((double) p.pulse.phaseView.load() - truePh); if (err > 0.5) err = 1.0 - err;
        std::cout << "    batterie 120 BPM : " << juce::String (bpm, 1) << " BPM, confiance " << juce::String (p.pulse.confView.load(), 2)
                  << ", erreur de phase " << juce::String (err * 100.0, 1) << " % du temps\n";
        CHECK (p.pulse.validView.load() && std::abs (bpm - 120.0f) < 3.0f, "tempo de la batterie mal detecte");
        CHECK (err < 0.1, "phase de la pulsation mal calee");
        CHECK (std::abs (p.hostBpm() - 120.0) < 4.0, "Maria ne prend pas le tempo detecte");
    }
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p);
        p.setParamReal ("im_tempo", 3.0f);
        Tones t; t.notes = { 60, 67, 64, 67 }; t.len = 3000; t.gap = 30000 - 3000;   // 96 BPM
        runG (p, [&] { return t.next(); }, 12.0);
        const float bpm = p.pulse.bpmView.load();
        std::cout << "    notes a 96 BPM : " << juce::String (bpm, 1) << " BPM\n";
        CHECK (p.pulse.validView.load() && std::abs (bpm - 96.0f) < 3.0f, "tempo a 96 mal detecte");
    }
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p);
        p.setParamReal ("im_tempo", 3.0f);
        Chord c; c.notes = { 48, 55, 64 };
        runG (p, [&] { return c.next(); }, 8.0);
        std::cout << "    accord tenu : pulsation " << (p.pulse.validView.load() ? "trouvee (!)" : "absente -> rythme libre") << "\n";
        CHECK (! p.pulse.validView.load(), "une pulsation est inventee sur un son tenu");
    }
}

static void testChapters()
{
    std::cout << "[30] Chapitres : puiser dans une scene precise de la memoire\n";
    const std::vector<int> A { 60, 62, 64, 65, 67 }, B { 72, 74, 76, 77, 79 };
    auto from = [] (const MidiLog& log, const std::vector<int>& set)
    {
        int k = 0; for (int nn : log.notes) for (int q : set) if (nn == q) { ++k; break; }
        return k;
    };
    juce::MemoryBlock mb;
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p);
        p.setParam (imId (IAct), 0.0f); p.setParam (imId (IGap), 1.0f);
        Tones a; a.notes = A; Tones b; b.notes = B;
        runG (p, [&] { return a.next(); }, 6.0);
        p.setParamReal ("chap_cur", 1.0f);           // chapitre 2
        runG (p, nullptr, 0.1);
        runG (p, [&] { return b.next(); }, 6.0);
        p.setParamReal ("learn", 0.0f);
        p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f);
        std::cout << "    chapitre 1 : " << juce::String (p.chapterSeconds (1), 1) << " s, chapitre 2 : " << juce::String (p.chapterSeconds (2), 1) << " s\n";
        CHECK (p.chapterSeconds (1) > 4.0 && p.chapterSeconds (2) > 4.0, "chapitres mal enregistres");
        struct Case { float play, pick; const std::vector<int>* want; const char* name; };
        const Case cases[] { { 2, 0, &A, "chapitre choisi 1   " }, { 2, 1, &B, "chapitre choisi 2   " }, { 1, 0, &B, "chapitre en cours (2)" }, { 3, 0, &A, "les autres chapitres" } };
        for (const auto& c : cases)
        {
            p.setParamReal ("chap_play", c.play); p.setParamReal ("chap_pick", c.pick);
            p.impro.forceSilence(); runG (p, nullptr, 0.3);
            MidiLog log;
            runG (p, nullptr, 6.0, &log);
            const int k = from (log, *c.want);
            std::cout << "    " << c.name << " : " << k << " / " << log.notes.size() << " notes du bon chapitre\n";
            CHECK (log.notes.size() > 5 && k == (int) log.notes.size(), "Maria puise hors du chapitre (" << c.name << ")");
        }
        p.setParamReal ("chap_play", 2.0f); p.setParamReal ("chap_pick", 0.0f);
        p.getStateInformation (mb);
        p.serviceBackground();
    }
    MariaProcessor q;
    q.prepareToPlay (FS, 256);
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    q.serviceBackground();
    duoSetup (q);
    q.setParamReal ("learn", 0.0f);
    q.setParamReal ("chap_play", 2.0f); q.setParamReal ("chap_pick", 0.0f);
    MidiLog log;
    runG (q, nullptr, 6.0, &log);
    const int k = from (log, A);
    std::cout << "    apres reouverture du set : chapitre 1 = " << juce::String (q.chapterSeconds (1), 1) << " s ; " << k << " / " << log.notes.size() << " notes du chapitre 1\n";
    CHECK (q.chapterSeconds (1) > 4.0 && log.notes.size() > 5 && k == (int) log.notes.size(), "chapitres perdus a la reouverture du set");
}

static void testCross()
{
    std::cout << "[31] Cross-synthese : la phrase de Maria avec le timbre d'un autre musicien\n";
    auto e = makeEngine (TCross);
    e->prepare (FS); e->reset();
    float p[6] { 0.1f, 0.2f, 0.3f, 0.5f, 0.6f, 0.6f };   // timbre de la sidechain, forme de Maria
    juce::AudioBuffer<float> buf (2, kSub);
    std::vector<float> side ((size_t) kSub);
    Ctx c; c.fs = FS; c.bpm = 120; c.ppqPerSample = 2.0 / FS;
    double eOn = 0, eOff = 0; int zc = 0, nOn = 0; float prev = 0; bool fin = true;
    double ph = 0, ph2 = 0;
    for (int64_t pos = 0; pos < (int64_t) (FS * 4); pos += kSub)
    {
        for (int i = 0; i < kSub; ++i)
        {
            const int64_t t = pos + i;
            const bool on = (t / 9600) % 2 == 0;                     // Maria : rafales de 200 ms
            ph += 220.0 / FS; ph -= std::floor (ph);
            const float saw = (float) (2.0 * ph - 1.0) * 0.3f;
            buf.setSample (0, i, on ? saw : 0.0f); buf.setSample (1, i, on ? saw : 0.0f);
            ph2 += 1000.0 / FS; ph2 -= std::floor (ph2);
            side[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * ph2);   // l'autre : 1 kHz tenu
        }
        c.side = side.data(); c.track = nullptr;
        e->process (buf.getWritePointer (0), buf.getWritePointer (1), kSub, c, p);
        for (int i = 0; i < kSub; ++i)
        {
            const int64_t t = pos + i;
            const float y = 0.5f * (buf.getSample (0, i) + buf.getSample (1, i));
            if (! std::isfinite (y)) fin = false;
            if (t < FS) continue;                                       // laisse le gain s'installer
            const int64_t k = t % 19200;
            if (k > 1500 && k < 8000) { eOn += y * y; ++nOn; if ((y >= 0) != (prev >= 0)) ++zc; }
            else if (k > 11000 && k < 18000) eOff += y * y;
            prev = y;
        }
    }
    const double freq = zc / 2.0 / ((double) nOn / FS);
    std::cout << "    energie pendant les rafales " << juce::String (eOn, 2) << " / entre " << juce::String (eOff, 4)
              << " ; frequence entendue " << (int) freq << " Hz (timbre de l'autre : 1000 Hz)\n";
    CHECK (fin, "sortie non finie");
    CHECK (eOn > 1.0 && eOff < eOn * 0.02, "la forme de Maria n'est pas transmise");
    CHECK (std::abs (freq - 1000.0) < 150.0, "le timbre n'est pas celui de l'autre musicien");
}

static void testArc()
{
    std::cout << "[32] Arc d'intensite : montee et descente sur la duree choisie\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDialogue);
    p.setParam (imId (IAct), 0.3f);
    p.setParamReal ("arc_time", 2.0f); p.setParamReal ("arc_depth", 1.0f);
    runG (p, nullptr, 0.5);
    const float a0 = p.effectiveIm (IAct), g0 = p.effectiveIm (IGap);
    p.setParamReal ("arc_goal", 1.0f);
    runG (p, nullptr, 1.0);
    const float mid = p.arcView.load();
    runG (p, nullptr, 1.5);
    const float a1 = p.effectiveIm (IAct), g1 = p.effectiveIm (IGap);
    juce::AudioBuffer<float> z (2, 256); z.clear(); juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);        // DESCENDRE
    p.processBlock (z, m);
    runG (p, nullptr, 2.5);
    const float a2 = p.effectiveIm (IAct);
    std::cout << "    activite " << juce::String (a0, 2) << " -> " << juce::String (a1, 2) << " (arc a mi-chemin : " << juce::String (mid, 2)
              << ") -> " << juce::String (a2, 2) << " ; silences " << juce::String (g0, 2) << " -> " << juce::String (g1, 2) << "\n";
    CHECK (a1 > a0 + 0.35f && g1 < g0 - 0.3f, "l'arc ne fait pas monter l'intensite");
    CHECK (mid > 0.65f && mid < 0.85f, "l'arc ne suit pas la duree choisie");
    CHECK (a2 < 0.05f && p.arcView.load() < 0.01f, "la note 62 ne fait pas redescendre l'arc");
}

static void testMidiLearning()
{
    std::cout << "[33] Apprentissage MIDI : Maria apprend le clavier et rejoue en MIDI\n";
    juce::MemoryBlock mb;
    std::set<int> taught;
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        bare (p, 3, MDialogue);
        p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IReact), 0.1f);
        p.setParamReal ("midi_learn", 2.0f);          // canal 2
        p.setParamReal ("mv_on", 1.0f); p.setParamReal ("mv_mode", (float) MDialogue); p.setParamReal ("mv_ch", 3.0f);   // sort sur le canal 4
        // le clavier : notes seules et accords, une toutes les 300 ms, avec la note 48 (qui est aussi une commande)
        const std::vector<std::vector<int>> seq { { 48 }, { 52 }, { 55 }, { 60, 64, 67 }, { 57 }, { 59 }, { 53, 57, 60 }, { 62 } };
        for (auto& v : seq) for (int nn : v) taught.insert (nn);
        juce::AudioBuffer<float> b (2, 256); b.clear();
        const int64_t per = 14400;
        int notesSent = 0, ph0 = p.impro.phraseCount.load();
        for (int64_t pos = 0; pos < (int64_t) (FS * 9); pos += 256)
        {
            juce::MidiBuffer m;
            for (int i = 0; i < 256; ++i)
            {
                const int64_t t = pos + i;
                if (t % per == 0 && t < (int64_t) (FS * 8.5)) { for (int nn : seq[(size_t) ((t / per) % 8)]) { m.addEvent (juce::MidiMessage::noteOn (2, nn, (juce::uint8) 90), i); ++notesSent; } }
                if (t % per == 9600) for (int nn : seq[(size_t) ((t / per) % 8)]) m.addEvent (juce::MidiMessage::noteOff (2, nn), i);
            }
            b.clear();
            p.processBlock (b, m);
            p.analyzeNow();
        }
        const int learned = p.midiLearned.load();
        CHECK (p.impro.phraseCount.load() == ph0, "une note du canal d'apprentissage a ete prise pour une commande");
        // silence du clavier : la voix MIDI repond
        std::map<int, std::vector<int>> onsByTime; int ons = 0, bad = 0;
        juce::AudioBuffer<float> z (2, 256);
        for (int64_t pos = 0; pos < (int64_t) (FS * 8); pos += 256)
        {
            juce::MidiBuffer m; z.clear();
            p.processBlock (z, m); p.analyzeNow();
            for (const auto meta : m)
            {
                const auto msg = meta.getMessage();
                if (! msg.isNoteOn() || msg.getChannel() != 4) continue;
                ++ons; if (taught.count (msg.getNoteNumber()) == 0) ++bad;
                onsByTime[(int) (pos + meta.samplePosition)].push_back (msg.getNoteNumber());
            }
        }
        int chords = 0; for (auto& [t, v] : onsByTime) if (v.size() == 3) ++chords;
        std::cout << "    " << notesSent << " notes envoyees, " << learned << " apprises (" << (p.midiMem.numStates() - 1) << " evenements) ; voix MIDI : "
                  << ons << " notes sur le canal 4, " << bad << " hors du jeu du clavier, " << chords << " accords\n";
        CHECK (p.midiMem.numStates() - 1 >= 20 && learned >= notesSent - 6, "le clavier n'est pas appris");
        CHECK (ons > 8 && bad == 0, "la voix MIDI ne rejoue pas le clavier");
        CHECK (chords >= 1, "les accords du clavier ne sont pas rejoues");
        p.getStateInformation (mb);
    }
    MariaProcessor q;
    q.prepareToPlay (FS, 256);
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    q.analyzeNow();
    std::cout << "    apres reouverture du set : " << (q.midiMem.numStates() - 1) << " evenements MIDI\n";
    CHECK (q.midiMem.numStates() - 1 >= 20, "la memoire MIDI n'est pas gardee avec le set");
}

// ---------------------------------------------------------------------------
static void testMachine()
{
    std::cout << "[34] Mode Machine : un ostinato cale sur la mesure, qui mute lentement\n";
    for (int k = 0; k < 2; ++k)
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p, MMachine);
        p.setParam (imId (IPhrase), 1.0f);
        p.setParamReal ("im_tempo", 2.0f); p.setParamReal ("bpm", 120.0f);
        p.setParamReal ("mach_len", 2.0f);                  // 1 mesure
        p.setParamReal ("mach_mut", k == 0 ? 0.0f : 1.0f);
        Tones t; t.notes = { 60, 62, 64, 65, 67, 69, 71, 72 };
        p.setParam (imId (IAct), 0.0f);
        runG (p, [&] { return t.next(); }, 8.0);
        p.setParamReal ("learn", 0.0f);
        p.setParam (imId (IAct), 1.0f);
        p.impro.forceSilence(); runG (p, nullptr, 0.3);
        // capture des notes et de leurs instants
        std::vector<std::pair<int64_t, int>> ev;
        juce::AudioBuffer<float> z (2, 256);
        for (int64_t pos = 0; pos < (int64_t) (FS * 12); pos += 256)
        {
            juce::MidiBuffer m; z.clear();
            p.processBlock (z, m); p.analyzeNow();
            for (const auto meta : m) if (meta.getMessage().isNoteOn() && meta.getMessage().getChannel() == 1) ev.push_back ({ pos + meta.samplePosition, meta.getMessage().getNoteNumber() });
        }
        const int L = p.impro.machineLength.load();
        int same = 0, cmp = 0, timeOk = 0;
        for (size_t i = (size_t) L; i + (size_t) L < ev.size(); ++i)
        {
            ++cmp;
            if (ev[i].second == ev[i + (size_t) L].second) ++same;
            if (std::abs ((ev[i + (size_t) L].first - ev[i].first) - 96000) <= 300) ++timeOk;    // une mesure a 120 BPM
        }
        std::cout << "    mutation " << (k == 0 ? "0  " : "100") << "% : boucle de " << L << " evenements, " << p.impro.machineCycles.load() << " tours, "
                  << same << " / " << cmp << " notes identiques au tour suivant, " << timeOk << " / " << cmp << " a une mesure pile, "
                  << p.impro.machineMutations.load() << " mutations\n";
        CHECK (L >= 2 && cmp >= L * 3, "la machine ne tourne pas");
        CHECK (timeOk >= cmp * 0.9, "la boucle n'est pas calee sur la mesure");
        if (k == 0) CHECK (same == cmp, "sans mutation, la boucle devrait se repeter a l'identique");
        else CHECK (p.impro.machineMutations.load() >= 2 && same < cmp, "la boucle ne mute pas");
    }
}

static void testDrone()
{
    std::cout << "[35] Nappes : sons tenus tires de la memoire, dans l'harmonie entendue\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    duoSetup (p, MDialogue);
    p.setParam (imId (IAct), 0.0f); p.setParam (imId (ILevel), 0.0f);      // l'improvisatrice se tait
    Tones chrom; chrom.notes = { 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71 };
    runG (p, [&] { return chrom.next(); }, 12.0);
    p.setParamReal ("learn", 0.0f);
    p.setParamReal ("drone_on", 1.0f); p.setParamReal ("drone_time", 3.0f); p.setParamReal ("mix", 1.0f);
    Chord c; c.notes = { 48, 52, 55 }; c.amp = 0.05f;
    int inChord = 0, picks = 0, last = -1;
    std::vector<float> out;
    for (int blk = 0; blk < (int) (FS * 16 / 256); ++blk)
    {
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m;
        for (int i = 0; i < 256; ++i) { const float x = c.next(); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m); p.analyzeNow();
        for (int i = 0; i < 256; ++i) out.push_back (0.5f * (b.getSample (0, i) + b.getSample (1, i)));
        const int st = p.droneStateView.load();
        if (st != last && st > 0)
        {
            last = st;
            const int pc = ((int) std::round (p.mem.segment (st).pitch)) % 12;
            if (blk * 256 > (int) FS) { ++picks; if (pc == 0 || pc == 4 || pc == 7) ++inChord;}
        }
    }
    // continuite (au-dela de l'accord d'entree, retire par Mix 100 %)
    int win = 0, loud = 0;
    for (size_t a = (size_t) (FS * 3); a + 960 < out.size(); a += 960)
    {
        double e = 0; for (size_t i = a; i < a + 960; ++i) e += out[i] * out[i];
        ++win; if (10.0 * std::log10 (e / 960.0 + 1e-12) > -45.0) ++loud;
    }
    std::cout << "    " << p.drone.changes.load() << " fragments, " << inChord << " / " << picks << " dans l'accord de do ; son continu sur "
              << loud << " / " << win << " fenetres de 20 ms\n";
    CHECK (p.drone.changes.load() >= 4, "les nappes ne se renouvellent pas");
    CHECK (loud >= win * 0.97, "la nappe n'est pas continue");
    CHECK (inChord >= picks - 1 && picks >= 3, "les nappes ne suivent pas l'harmonie");
    p.setParamReal ("drone_on", 0.0f);
    runG (p, nullptr, 2.5);
    CHECK (! p.drone.active(), "la nappe ne s'eteint pas");
}

static void testRecovery (const juce::File& dir)
{
    std::cout << "[36] Sauvegarde de secours (plantage), fichiers FLAC, nettoyage\n";
    juce::MemoryBlock mb;
    int nAll = 0;
    juce::String id;
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        duoSetup (p, MDialogue);
        p.setParam (imId (IAct), 0.0f);
        Tones a; a.notes = { 60, 62, 64 };
        runG (p, [&] { return a.next(); }, 5.0);
        p.getStateInformation (mb);          // le set est sauve ici...
        p.serviceBackground();
        id = p.memoryId();
        CHECK (p.sessionMemoryFile().hasFileExtension ("flac") && p.sessionMemoryFile().existsAsFile(), "memoire de session non ecrite en FLAC");
        const double ratio = (double) p.sessionMemoryFile().getSize() / ((double) (p.mem.writePos() - p.mem.oldestValidPos()) * 3.0);
        std::cout << "    FLAC : " << (int) (ratio * 100) << " % de la taille d'un WAV 24 bits\n";
        Tones b; b.notes = { 72, 74, 76 };
        runG (p, [&] { return b.next(); }, 5.0);      // ... puis on continue de jouer
        runG (p, nullptr, 2.0);
        p.markMotif (1); runG (p, nullptr, 0.05);
        nAll = p.mem.numStates();
        p.autosaveNow();
        CHECK (p.recoveryAudioFile().existsAsFile() && p.recoveryStateFile().existsAsFile(), "secours non ecrit");
    }   // ... et Live plante
    MariaProcessor q;
    q.prepareToPlay (FS, 256);
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    q.serviceBackground();
    const int nSet = q.mem.numStates();
    CHECK (q.recoveryAvailable.load(), "le secours n'est pas propose a la reouverture");
    q.acceptRecovery();
    q.serviceBackground();
    std::cout << "    " << (nSet - 1) << " evenements avec le set seul, " << (q.mem.numStates() - 1) << " avec le secours (avant plantage : " << (nAll - 1) << ")"
              << " ; leitmotiv B : " << juce::String (q.motifSeconds (1), 1) << " s\n";
    CHECK (q.mem.numStates() >= nAll - 3 && q.mem.numStates() > nSet + 5, "le secours ne restaure pas la memoire");
    CHECK (q.motifSeconds (1) > 1.0f, "le secours ne restaure pas les leitmotivs");
    // nettoyage : sur un dossier de test seulement
    auto tmp = dir.getChildFile ("maria_nettoyage_test");
    tmp.deleteRecursively(); tmp.createDirectory();
    auto oldF = tmp.getChildFile ("ancien.flac"), newF = tmp.getChildFile ("recent.flac"), keepF = tmp.getChildFile ("garde.flac");
    for (auto* f : { &oldF, &newF, &keepF }) f->replaceWithText ("x");
    oldF.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::days (40));
    keepF.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::days (40));
    const int removed = MariaProcessor::cleanupFolder (tmp, 30, "garde");
    CHECK (removed == 1 && ! oldF.existsAsFile() && newF.existsAsFile() && keepF.existsAsFile(), "nettoyage incorrect");
    tmp.deleteRecursively();
}

static void testFeedback()
{
    std::cout << "[37] Retour d'etat MIDI (Wing, controleur)\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    duoSetup (p, MDuo);
    p.setParamReal ("fb_ch", 5.0f);
    std::map<int, int> lastNote, lastCC; int msgs = 0;
    bool sawOn = false, sawOff = false;
    auto blockRun = [&] (std::function<float()> gen, double sec)
    {
        juce::AudioBuffer<float> b (2, 256);
        for (int64_t pos = 0; pos < (int64_t) (sec * FS); pos += 256)
        {
            juce::MidiBuffer m;
            for (int i = 0; i < 256; ++i) { const float x = gen ? gen() : 0.0f; b.setSample (0, i, x); b.setSample (1, i, x); }
            p.processBlock (b, m); p.analyzeNow(); p.morphTick();
            for (const auto meta : m)
            {
                const auto x = meta.getMessage();
                if (x.getChannel() != 5) continue;
                ++msgs;
                if (x.isNoteOnOrOff()) { lastNote[x.getNoteNumber()] = x.isNoteOn() ? x.getVelocity() : 0; if (x.getNoteNumber() == 48) (x.isNoteOn() ? sawOn : sawOff) = true; }
                if (x.isController()) lastCC[x.getControllerNumber()] = x.getControllerValue();
            }
        }
    };
    Tones t; t.notes = { 60, 64, 67 };
    blockRun ([&] { return t.next(); }, 6.0);
    p.impro.forceSilence();
    blockRun (nullptr, 1.0);
    p.setParam ("morph", 0.0f);
    p.storeScene (2); p.recallScene (2);
    p.nextChapter();
    p.markMotif (0);
    blockRun (nullptr, 0.2);
    std::cout << "    " << msgs << " messages ; PHRASE allume puis eteint : " << (sawOn && sawOff ? "oui" : "non") << " ; scene 3 : " << lastNote[38]
              << " ; chapitre (CC20) : " << lastCC[20] << " ; leitmotiv A : " << lastNote[52] << "\n";
    CHECK (sawOn && sawOff, "le retour PHRASE ne suit pas Maria");
    CHECK (lastNote[38] == 127 && lastNote[36] == 0, "le retour de scene est faux");
    CHECK (lastCC[20] == 2, "le retour de chapitre est faux");
    CHECK (lastNote[52] == 127, "le retour de leitmotiv est faux");
    CHECK (msgs < 200, "le retour d'etat envoie trop de messages (" << msgs << ")");
}

static void testGui (const juce::File& outDir)
{
    std::cout << "[14] Interface : captures\n";
    for (int pr : { 1, 5 })
    {
        MariaProcessor p;
        p.prepareToPlay (FS, 256);
        p.loadFactory (pr);
        Player pl; pl.kind = pr == 5 ? 1 : 0;
        run (p, pl, 12.0);
        pl.playing = false;
        run (p, pl, 1.5);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        CHECK (img.getWidth() == ed->getWidth(), "capture invalide");
        auto f = outDir.getChildFile ("maria_gui_" + juce::String (pr) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::cout << "    " << f.getFullPathName() << "\n";
        if (pr == 1)
        {
            p.markMotif (0); p.markMotif (2);
            run (p, pl, 0.05);
            if (auto* tc = ed->findChildWithID ("dummy")) juce::ignoreUnused (tc);
            std::function<juce::TabbedComponent* (juce::Component*)> findTabs = [&] (juce::Component* c) -> juce::TabbedComponent*
            {
                if (auto* t = dynamic_cast<juce::TabbedComponent*> (c)) return t;
                for (auto* ch : c->getChildren()) if (auto* t = findTabs (ch)) return t;
                return nullptr;
            };
            if (auto* tabs = findTabs (ed.get()))
            {
                for (int i = 0; i < tabs->getNumTabs(); ++i) if (tabs->getTabNames()[i] == "JEU A PLUSIEURS") tabs->setCurrentTabIndex (i);
                ed->repaint();
                auto img2 = ed->createComponentSnapshot (ed->getLocalBounds());
                auto f2 = outDir.getChildFile ("maria_gui_ensemble.png");
                f2.deleteFile();
                juce::FileOutputStream os2 (f2);
                juce::PNGImageFormat().writeImageToStream (img2, os2);
                CHECK (tabs->getCurrentTabName() == "JEU A PLUSIEURS", "onglet JEU A PLUSIEURS absent");
                for (int i = 0; i < tabs->getNumTabs(); ++i) if (tabs->getTabNames()[i] == "FORME / MIDI") tabs->setCurrentTabIndex (i);
                p.nextChapter(); run (p, pl, 0.05);
                auto img3 = ed->createComponentSnapshot (ed->getLocalBounds());
                auto f3 = outDir.getChildFile ("maria_gui_forme.png");
                f3.deleteFile();
                juce::FileOutputStream os3 (f3);
                juce::PNGImageFormat().writeImageToStream (img3, os3);
                CHECK (tabs->getCurrentTabName() == "FORME / MIDI", "onglet FORME / MIDI absent");
                for (int i = 0; i < tabs->getNumTabs(); ++i) if (tabs->getTabNames()[i] == "MACHINE / NAPPES") tabs->setCurrentTabIndex (i);
                p.setParamReal ("drone_on", 1.0f); run (p, pl, 0.5);
                auto img4 = ed->createComponentSnapshot (ed->getLocalBounds());
                auto f4 = outDir.getChildFile ("maria_gui_machine.png");
                f4.deleteFile();
                juce::FileOutputStream os4 (f4);
                juce::PNGImageFormat().writeImageToStream (img4, os4);
                CHECK (tabs->getCurrentTabName() == "MACHINE / NAPPES", "onglet MACHINE / NAPPES absent");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Endurance : un concert entier de batterie dense (1h30), en accelere
struct DenseDrums
{
    int64_t t = 0;
    alea::FastRng rng { 991 };
    bool playing = true;
    float next()
    {
        ++t;
        if (! playing) return 0.0f;
        const int64_t per = 1800;                          // une frappe toutes les 37,5 ms
        const int64_t k = t % per;
        const double s = (double) k / FS;
        const int which = (int) ((t / per) % 5);
        if (which == 0) return (float) (0.7 * std::exp (-s * 30.0) * std::sin (2 * juce::MathConstants<double>::pi * 60.0 * s));
        if (which == 2) return (float) (0.5 * std::exp (-s * 45.0) * (0.6 * rng.bi() + 0.4 * std::sin (2 * juce::MathConstants<double>::pi * 210.0 * s)));
        return (float) (0.25 * std::exp (-s * 90.0) * rng.bi());
    }
};

static Stats runDense (MariaProcessor& p, DenseDrums& d, double seconds, int block = 1024)
{
    juce::AudioBuffer<float> b (2, block);
    juce::MidiBuffer midi;
    const int64_t total = (int64_t) (seconds * FS);
    Stats st; double sum = 0; int64_t cnt = 0;
    for (int64_t pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i) { const float x = d.next(); b.setSample (0, i, x); b.setSample (1, i, x); }
        midi.clear();
        p.processBlock (b, midi);
        p.analyzeNow();
        p.morphTick();
        for (int i = 0; i < block; ++i)
        {
            const float v = b.getSample (0, i);
            if (! std::isfinite (v)) st.finite = false;
            st.peak = juce::jmax (st.peak, std::abs (v));
            sum += (double) v * v; ++cnt;
        }
    }
    st.energy = sum;
    st.rms = (float) std::sqrt (sum / juce::jmax ((int64_t) 1, cnt));
    return st;
}

static void testEndurance()
{
    std::cout << "[38] Endurance : 1h30 de batterie dense (en accelere), Maria apprend et repond jusqu'au bout\n";
    const auto t0 = std::chrono::steady_clock::now();
    MariaProcessor p;
    p.prepareToPlay (FS, 1024);
    bare (p, 0, MDialogue);                                  // source Batterie, mode Dialogue
    p.setParam (imId (IAct), 1.0f); p.setParam (imId (IGap), 0.0f); p.setParam (imId (IPolite), 0.0f);
    p.setParam (imId (IMem), 1.0f);
    DenseDrums d;
    int statesAt[10] {};
    float answerRms[10] {};
    bool finite = true;
    for (int m = 0; m < 90; ++m)
    {
        p.setParamReal ("chap_cur", (float) juce::jmin (15, m / 10));     // un chapitre toutes les 10 minutes
        d.playing = true;  finite &= runDense (p, d, 50.0).finite;
        d.playing = false; const auto st = runDense (p, d, 10.0);         // silence : Maria repond
        finite &= st.finite;
        if (m % 10 == 9) { statesAt[m / 10] = p.mem.numStates(); answerRms[m / 10] = st.rms; }
    }
    for (int k = 0; k < 9; ++k)
        std::cout << "    " << (k + 1) * 10 << " min : " << statesAt[k] << " evenements appris, reponse " << juce::String (juce::Decibels::gainToDecibels (answerRms[k]), 1) << " dB\n";
    const int nEnd = p.mem.numStates();
    std::cout << "    fin : " << nEnd << " evenements, " << (nEnd - p.mem.firstValidState()) << " encore en memoire ("
              << juce::String ((double) (p.mem.writePos() - p.mem.oldestValidPos()) / FS / 60.0, 1) << " min d'audio)\n";
    CHECK (finite, "valeurs non finies pendant l'endurance");
    CHECK (statesAt[8] > statesAt[7] + 1000, "Maria n'apprend plus en fin de concert (" << statesAt[7] << " -> " << statesAt[8] << ")");
    CHECK (nEnd - p.mem.firstValidState() > 1000, "plus aucun evenement exploitable en fin de concert");
    CHECK (answerRms[8] > 0.003f, "Maria ne repond plus en fin de concert");
    // un chapitre vieux de 25 minutes (chapitre 7 = minutes 60 a 70) est encore jouable
    p.setParamReal ("chap_play", 2.0f); p.setParamReal ("chap_pick", 6.0f);
    p.setParamReal ("learn", 0.0f);
    p.impro.forceSilence();
    d.playing = false; runDense (p, d, 0.5);
    const auto old = runDense (p, d, 15.0);
    std::cout << "    chapitre 7 (il y a 25 min) : " << juce::String (p.chapterSeconds (7), 1) << " s en memoire, reponse "
              << juce::String (juce::Decibels::gainToDecibels (old.rms), 1) << " dB\n";
    CHECK (p.chapterSeconds (7) > 60.0 && old.rms > 0.003f, "un chapitre de 25 minutes n'est plus jouable");
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::cout << "    duree du test : " << juce::String (secs, 0) << " s (" << juce::String (5400.0 / secs, 0) << " x temps reel)\n";
}

// ---------------------------------------------------------------------------
static void testScenes48()
{
    std::cout << "[39] 48 scenes : une par sequence, banques de 8, parametre Scene automatisable\n";
    MariaProcessor p;
    p.prepareToPlay (FS, 256);
    bare (p, 3, MDuo);
    Player pl;
    // scene 41 (indice 40) : reglage distinct, rappel par le parametre "scene"
    p.setParam (imId (IAct), 0.2f); p.storeScene (3);
    p.setParam (imId (IAct), 0.9f); p.storeScene (40);
    p.setParamReal ("morph", 0.0f);
    p.recallScene (3); run (p, pl, 0.1);
    p.setParamReal ("scene", 41.0f); run (p, pl, 0.2);
    std::cout << "    scene active : " << (p.activeScene() + 1) << ", banque " << (p.sceneBank.load() + 1) << ", activite " << juce::String (p.imParam (IAct), 2) << "\n";
    CHECK (p.activeScene() == 40 && p.sceneBank.load() == 5 && std::abs (p.imParam (IAct) - 0.9f) < 0.02f, "scene 41 mal rappelee par le parametre");
    // note MIDI 36 = 1re scene de la banque affichee
    p.sceneBank.store (0);
    juce::AudioBuffer<float> b (2, 256); b.clear();
    juce::MidiBuffer midi; midi.addEvent (juce::MidiMessage::noteOn (1, 39, (juce::uint8) 100), 0);
    p.processBlock (b, midi); p.morphTick();
    CHECK (p.activeScene() == 3, "note 39 n'a pas rappele la scene 4 de la banque 1");
    // etat : les 48 scenes survivent a une sauvegarde
    juce::MemoryBlock mb; p.getStateInformation (mb);
    MariaProcessor q; q.prepareToPlay (FS, 256); q.setStateInformation (mb.getData(), (int) mb.getSize());
    CHECK (q.sceneStored (40) && q.sceneStored (3) && ! q.sceneStored (20), "scenes mal sauvegardees");
}

static void testPulseBus()
{
    std::cout << "[41] Pulsation partagee : un suiveur se cale sur le tempo et la phase publies\n";
    auto& bus = metro::PulseBus::get();
    CHECK (bus.ok(), "fichier de pulsation indisponible");
    const int s = bus.claimSlot();
    const double fs = 48000.0, beat0 = 1000.0;           // un temps a t = 1 s, 100 BPM
    metro::PulseFollower fo; double ppq0 = 0.37, pps = 0, bpm = 0, nowMs = 2000.0, start = 0;
    bool ok = true;
    for (int i = 0; i < 2000; ++i)                       // ~ 10,7 s de blocs de 256
    {
        bus.publish (s, 100.0, beat0, 0.9, nowMs);
        ok = ok && fo.step (256, fs, nowMs, ppq0, start, pps, bpm);
        nowMs += 256.0 / fs * 1000.0;
    }
    double want = std::fmod ((nowMs - beat0) / 600.0, 1.0), got = fo.ppq - std::floor (fo.ppq);
    double err = std::abs (want - got); err = std::min (err, 1.0 - err);
    std::cout << "    tempo " << bpm << ", ecart de phase " << juce::String (err, 4) << " temps\n";
    CHECK (ok && std::abs (bpm - 100.0) < 1e-6 && err < 0.02, "suiveur pas cale");
    bus.retract (s);
    double b2, t2; CHECK (! fo.step (256, fs, nowMs + 5000.0, 0, start, pps, bpm) || bus.read (nowMs, b2, t2), "pulsation perimee encore suivie");
}

static void testSoft()
{
    std::cout << "[40] Nappe douce : modulations lentes, peu de reinjection, meme niveau\n";
    float pv[6] { 0.0f, 0.95f, 0.9f, 0.98f, 0.5f, 0.5f };   // modulation : mode chorus, vitesse et profondeur fortes
    float a[6]; std::copy (pv, pv + 6, a);
    softenParams (TMod, a, 1.0f);
    std::cout << "    chorus : vitesse " << juce::String (pv[1], 2) << " -> " << juce::String (a[1], 2) << ", reinjection " << juce::String (pv[3], 2) << " -> " << juce::String (a[3], 2) << "\n";
    CHECK (a[1] <= pv[1] * 0.26f && a[3] <= pv[3] * 0.21f && a[2] < pv[2], "chorus pas adouci");
    float f[6] { 0.4f, 0.95f, 0.9f, 0.98f, 0.5f, 0.5f };     // flanger : reinjection bipolaire -> vers le centre
    softenParams (TMod, f, 1.0f);
    CHECK (std::abs (f[3] - 0.5f) < 0.08f, "flanger : reinjection pas recentree");
    float r[6] { 0.0f, 0.5f, 0.9f, 0.95f, 0.9f, 0.5f };      // filtre : resonance, sauts
    softenParams (TFilter, r, 1.0f);
    CHECK (r[2] < 0.3f && std::abs (r[3] - 0.5f) < 0.15f && r[4] < 0.2f, "filtre pas adouci");
    float z[6] { 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f }, z0[6]; std::copy (z, z + 6, z0);
    softenParams (TMod, z, 0.0f);
    CHECK (std::equal (z, z + 6, z0), "nappe douce a 0 modifie le son");
    // dans le processeur : flanger extreme, nappe douce 0 puis 1 -> niveau comparable, valeurs finies
    float lv[2];
    for (int k = 0; k < 2; ++k)
    {
        MariaProcessor p; p.prepareToPlay (FS, 256);
        bare (p, 3, MDuo);
        p.setParamReal (sid (0, "type"), (float) TMod); p.setParamReal (sid (0, "on"), 1.0f);
        const float flg[6] { 0.4f, 0.95f, 0.9f, 0.98f, 0.5f, 0.5f };      // flanger rapide, reinjection maximale
        for (int q = 0; q < 6; ++q) p.setParam (pid (0, q), flg[q]);
        p.setParam (pid (0, kMix), 1.0f);
        p.setParamReal ("soft", (float) k);
        Player pl; const auto st = run (p, pl, 3.0);
        lv[k] = juce::Decibels::gainToDecibels (st.rms + 1.0e-9f);
        CHECK (st.finite, "valeurs non finies (nappe douce " << k << ")");
    }
    std::cout << "    flanger extreme : " << juce::String (lv[0], 1) << " dB, avec nappe douce " << juce::String (lv[1], 1) << " dB\n";
    CHECK (std::abs (lv[0] - lv[1]) < 6.0f, "la nappe douce change trop le niveau");
}

// ---------------------------------------------------------------------------
// Outil : construit les scenes d'un etat Maria a partir d'un plan (une scene par sequence)
//   MariaTest <dossier> scenebuild etat.xml plan.json sortie.xml
// plan.json : { "scenes": [ { "i": k, "set": { "<identifiant AU>": valeur normalisee, ... } } ],
//               "real": { "<id JUCE>": valeur reelle, ... } }   (reglages fixes, appliques avant les scenes)
// Ecrit aussi sortie.xml.ids : les identifiants AU couverts par les scenes (une ligne chacun).
static int sceneBuild (const juce::File& stateFile, const juce::File& planFile, const juce::File& outFile)
{
    MariaProcessor p;
    p.prepareToPlay (FS, 512);
    auto xml = juce::XmlDocument::parse (stateFile);
    if (xml == nullptr) { std::cout << "etat illisible\n"; return 2; }
    p.restoreState (juce::ValueTree::fromXml (*xml));
    const auto plan = juce::JSON::parse (planFile);
    std::map<int, juce::RangedAudioParameter*> byAu;
    for (auto* prm : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            byAu[(int) ((juce::uint32) r->getParameterID().hashCode() & 0x7fffffffu)] = r;
    if (auto* real = plan.getProperty ("real", {}).getDynamicObject())
        for (const auto& kv : real->getProperties()) p.setParamReal (kv.name.toString(), (float) kv.value);
    const auto sceneIds = p.sceneParamIds();
    std::set<int> covered;
    int nScenes = 0, nSet = 0, nMissing = 0;
    if (auto* arr = plan.getProperty ("scenes", {}).getArray())
        for (const auto& sc : *arr)
        {
            const int i = (int) sc.getProperty ("i", -1);
            if (i < 0 || i >= kNumScenes) continue;
            if (auto* setv = sc.getProperty ("set", {}).getDynamicObject())
                for (const auto& kv : setv->getProperties())
                {
                    const int au = kv.name.toString().getIntValue();
                    auto it = byAu.find (au);
                    if (it == byAu.end()) { ++nMissing; continue; }
                    if (! sceneIds.contains (it->second->getParameterID())) continue;   // hors scene : reste en automation
                    it->second->setValueNotifyingHost ((float) kv.value);
                    covered.insert (au); ++nSet;
                }
            p.storeScene (i);
            ++nScenes;
        }
    p.recallScene (0);
    p.setParamReal ("scene", 0.0f);
    juce::MemoryBlock mb; p.getStateInformation (mb);
    if (auto out = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize())) out->writeTo (outFile);
    juce::String ids; for (int a : covered) ids << a << "\n";
    outFile.withFileExtension ("ids").replaceWithText (ids);
    std::cout << nScenes << " scenes, " << nSet << " valeurs, " << covered.size() << " reglages couverts, " << nMissing << " inconnus\n";
    return nScenes > 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    if (argc > 5 && juce::String (argv[2]) == "scenebuild")
    {
        juce::ScopedJuceInitialiser_GUI init0;
        return sceneBuild (juce::File (argv[3]), juce::File (argv[4]), juce::File (argv[5]));
    }
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    // les tests ecrivent des memoires de session : elles sont retirees a la fin
    juce::StringArray before;
    for (const auto& f : MariaProcessor::sessionsFolder().findChildFiles (juce::File::findFiles, false, "*")) before.add (f.getFileName());
    struct Cleanup { juce::StringArray& b; ~Cleanup() { for (const auto& f : MariaProcessor::sessionsFolder().findChildFiles (juce::File::findFiles, false, "*")) if (! b.contains (f.getFileName())) f.deleteFile(); } } cleanup { before };
    if (argc > 2 && juce::String (argv[2]) == "endurance")
    {
        testEndurance();
        std::cout << "\n" << (checks - failures) << "/" << checks << " verifications OK\n";
        return failures == 0 ? 0 : 1;
    }
    if (argc > 2 && juce::String (argv[2]) == "new")
    {
        testMachine(); testDrone(); testRecovery (outDir); testFeedback();
        std::cout << "\n" << (checks - failures) << "/" << checks << " verifications OK\n";
        return failures == 0 ? 0 : 1;
    }
    testEngines();
    testAnalysis();
    testDrumAnalysis();
    testDialogue();
    testDuoAndMaterial();
    testContinuity();
    testClearLoadSave (outDir);
    testSeed();
    testStateScenesDice();
    testPresets();
    testAdapt();
    testFuzzCpu();
    testMidi();
    testTempo();
    testResize();
    testSelfListening();
    testSessionMemory (outDir);
    testPitchOnly();
    testRandomTMode();
    testChords();
    testVoice2AndMidi();
    testPanicAndEngines();
    testHarmony();
    testMotifs();
    testSidechain();
    testCorpus (outDir);
    testContrast();
    testSplitOutputs();
    testPulse();
    testChapters();
    testCross();
    testArc();
    testMidiLearning();
    testMachine();
    testDrone();
    testRecovery (outDir);
    testFeedback();
    testScenes48();
    testSoft(); testPulseBus();
    testGui (outDir);
    std::cout << "\n" << (checks - failures) << "/" << checks << " verifications OK\n";
    return failures == 0 ? 0 : 1;
}
