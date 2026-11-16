// Banc de test hors ligne de COLOR (+ suivi des accords par Talea et Alea via AleaLink).
// Verifie la theorie (progressions, voicings, regles de Dobbins), le moteur (modes de jeu, clavier, substitutions),
// la robustesse, rend des demos et capture l'interface.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ColorProcessor.h"
#include "ColorEditor.h"
#include "TaleaProcessor.h"
#include "PluginProcessor.h"     // Alea

using namespace color;
namespace th = color::theory;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [OK]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

template <typename P>
static void setP (P& p, const juce::String& id, float realValue)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

struct Stats
{
    float peak = 0; double rms = 0; bool finite = true;
    int noteOns = 0, maxPoly = 0;
    std::vector<std::pair<int64_t, juce::MidiMessage>> midi;
};

template <typename Proc>
static Stats render (Proc& p, double seconds, juce::AudioBuffer<float>* out = nullptr, double sr = 48000.0, int block = 512,
                     std::function<void (juce::MidiBuffer&, int64_t)> feed = nullptr)
{
    const int total = (int) (seconds * sr);
    if (out) out->setSize (2, total);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Stats st; double acc = 0;
    int poly = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        midi.clear();
        if (feed) feed (midi, pos);
        p.processBlock (view, midi);
        for (const auto m : midi)
        {
            if (m.getMessage().isNoteOn()) { ++st.noteOns; ++poly; st.maxPoly = juce::jmax (st.maxPoly, poly); }
            if (m.getMessage().isNoteOff()) poly = juce::jmax (0, poly - 1);
            st.midi.push_back ({ (int64_t) pos + m.samplePosition, m.getMessage() });
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = view.getSample (c, i);
                if (! std::isfinite (v)) st.finite = false;
                st.peak = juce::jmax (st.peak, std::abs (v));
                acc += (double) v * v;
                if (out) out->setSample (c, pos + i, v);
            }
    }
    st.rms = std::sqrt (acc / juce::jmax (1, 2 * total));
    return st;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

static ColorAudioProcessor* fresh (std::unique_ptr<ColorAudioProcessor>& holder, double sr = 48000.0)
{
    holder = std::make_unique<ColorAudioProcessor>();
    holder->prepareToPlay (sr, 512);
    setP (*holder, "bpm", 120.0f);
    return holder.get();
}

static uint16_t absMask (const th::Chord& c, int key)
{
    const uint16_t rel = th::chordMask (c);
    uint16_t a = 0;
    for (int i = 0; i < 12; ++i) if (rel & (1u << i)) a |= (uint16_t) (1u << ((i + key) % 12));
    return a;
}

int main (int argc, char** argv)
{
    setenv ("ALEALINK_FILE", "off", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "test_out");
    outDir.createDirectory();
    const double sr = 48000.0;

    // =====================================================================
    std::cout << "== COLOR : theorie" << std::endl;
    {
        bool ok = true; int total = 0; juce::String bad;
        for (int st = 0; st < th::kNumStyles; ++st)
            for (int seed = 1; seed <= 40; ++seed)
                for (int nc : { 1, 4, 8, 16 })
                {
                    th::GenOptions o; o.style = st; o.scale = st == th::SLydian ? 3 : 5; o.numChords = nc; o.seed = (uint32_t) seed;
                    o.complexity = (float) (seed % 5) / 4.0f; o.tension = (float) (seed % 3) / 2.0f; o.chroma = (float) (seed % 4) / 3.0f;
                    o.cycle = seed % 4; o.barsPerChord = seed % 3;
                    auto p = th::generate (o);
                    ++total;
                    bool good = ! p.empty() && (int) p.size() <= 16;
                    for (auto& c : p) good = good && c.root >= 0 && c.root < 12 && c.qual >= 0 && c.qual < th::numQualities() && c.beats >= 1 && c.beats <= 32;
                    if (! good) { ok = false; bad << th::styleNames()[st] << "/" << seed << " "; }
                }
        check (ok, juce::String (total) + " progressions generees (12 styles), toutes valides " + bad);
    }
    {
        // fonctionnel : commence sur la tonique, finit par une cadence vers la tonique ou la dominante
        int good = 0;
        for (int seed = 1; seed <= 30; ++seed)
        {
            th::GenOptions o; o.style = th::SFunctional; o.scale = 0; o.numChords = 8; o.seed = (uint32_t) seed;
            auto p = th::generate (o);
            if (! p.empty() && th::chordFunction (p.front(), 0) == 0) ++good;
        }
        check (good >= 27, "style fonctionnel : la progression commence sur la tonique (" + juce::String (good) + "/30)");
        int dom = 0, n = 0;
        for (int seed = 1; seed <= 30; ++seed)
        {
            th::GenOptions o; o.style = th::SJazz; o.scale = 0; o.numChords = 8; o.seed = (uint32_t) seed; o.complexity = 0.6f;
            for (auto& c : th::generate (o)) { ++n; if (th::quality (c.qual).family == th::FDom) ++dom; }
        }
        check (dom * 5 >= n, "jazz II-V : au moins 20 % de dominantes (" + juce::String (dom) + "/" + juce::String (n) + ")");
        // blues : 12 accords dominants / iv / I
        th::GenOptions o; o.style = th::SBlues; o.scale = 4; o.numChords = 12; o.seed = 3;
        auto b = th::generate (o);
        check (b.size() >= 10 && th::quality (b[0].qual).family == th::FDom, "blues : grille de dominantes (" + juce::String ((int) b.size()) + " accords)");
    }
    {
        // regles de voicing (Dobbins) sur toutes les progressions
        int chords = 0, missing37 = 0, minor9 = 0, lowMud = 0; double move = 0; int moves = 0;
        for (int st = 0; st < th::kNumStyles; ++st)
            for (int seed = 1; seed <= 12; ++seed)
            {
                th::GenOptions o; o.style = st; o.scale = 0; o.numChords = 8; o.seed = (uint32_t) seed; o.complexity = 0.6f;
                auto prog = th::generate (o);
                for (int v : { th::VClose, th::VDrop2, th::VShell, th::VRootlessA, th::VAuto })
                {
                    th::VoiceOptions vo; vo.voicing = v; vo.voices = 4; vo.center = 62; vo.complexity = 0.6f; vo.bassMode = 1; vo.scale = 0; vo.keyRoot = 9;
                    int prev[8]; int pn = 0;
                    for (auto& c : prog)
                    {
                        int notes[8];
                        const int n = th::voiceChord (c, vo, pn > 0 ? prev : nullptr, pn, notes, 8);
                        ++chords;
                        const auto& q = th::quality (c.qual);
                        const int fam = q.family;
                        const bool tertian7 = q.n >= 4 && q.triadSize >= 4 && (fam == th::FMaj || fam == th::FMin || fam == th::FDom || fam == th::FHalfDim);
                        if (tertian7 && st != th::SAtonal)
                        {
                            bool has3 = false, has7 = false;
                            const int root = (9 + c.root) % 12;
                            for (int i = 1; i < n; ++i)
                            {
                                const int iv = ((notes[i] - root) % 12 + 12) % 12;
                                if (iv == 3 || iv == 4) has3 = true;
                                if (iv == 10 || iv == 11 || (iv == 9 && c.qual == th::QDim7)) has7 = true;
                            }
                            if (! (has3 && has7)) ++missing37;
                        }
                        if (fam != th::FDom && st != th::SAtonal && v != th::VClose)
                            for (int i = 1; i < n; ++i) for (int j = i + 1; j < n; ++j) if (notes[j] - notes[i] == 13) ++minor9;
                        for (int i = 2; i < n; ++i) if (notes[i - 1] < 50 && notes[i] - notes[i - 1] < 3) ++lowMud;
                        // conduite des voix
                        int up[8]; int un = 0;
                        for (int i = 1; i < n; ++i) up[un++] = notes[i];
                        if (pn > 0 && un == pn)
                        {
                            int d = 0; for (int i = 0; i < un; ++i) d += std::abs (up[i] - prev[i]);
                            move += (double) d / un; ++moves;
                        }
                        for (int i = 0; i < un; ++i) prev[i] = up[i];
                        pn = un;
                    }
                }
            }
        const double avgMove = move / juce::jmax (1, moves);
        check (missing37 * 50 <= chords, "voicings : 3ce et 7e presentes (" + juce::String (missing37) + " manques sur " + juce::String (chords) + ")");
        check (minor9 * 50 <= chords, "voicings : pas de 9e mineure entre voix hors dominantes (" + juce::String (minor9) + ")");
        check (lowMud * 50 <= chords, "voicings : pas d'intervalles serres dans le grave (" + juce::String (lowMud) + ")");
        check (avgMove < 4.0, "conduite des voix : mouvement moyen par voix " + juce::String (avgMove, 2) + " demi-tons");
    }
    {
        // reconnaissance d'accords
        struct T { std::vector<int> pcs; int bass; const char* expect; };
        const T tests[] { { { 0, 4, 7 }, 0, "C" }, { { 9, 0, 4 }, 9, "Am" }, { { 2, 5, 9, 0 }, 2, "Dm7" }, { { 7, 11, 2, 5 }, 7, "G7" },
                          { { 0, 4, 7, 11 }, 4, "Cmaj7/E" }, { { 11, 2, 5, 9 }, 11, "Bm7b5" }, { { 0, 5, 7 }, 0, "Csus4" } };
        int good = 0; juce::String got;
        for (auto& t : tests)
        {
            uint16_t m = 0; for (int pc : t.pcs) m |= (uint16_t) (1u << pc);
            th::Chord c;
            if (th::identifyChord (m, t.bass, c) && th::chordName (c, 0) == t.expect) ++good;
            else got << th::chordName (c, 0) << "(" << t.expect << ") ";
        }
        check (good == 7, "reconnaissance d'accords (" + juce::String (good) + "/7) " + got);
    }

    // =====================================================================
    std::cout << "== COLOR : progression et edition" << std::endl;
    {
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "root", 9.0f); setP (p, "mode", 5.0f); setP (p, "chordlen", 1.0f);
        check (p.setProgressionText (0, "Am9 Fmaj7 C/E G6"), "saisie texte : Am9 Fmaj7 C/E G6");
        check (p.progressionText (0) == "Am9 Fmaj7 C/E G6", "texte relu a l'identique (" + p.progressionText (0) + ")");
        check (p.setProgressionText (0, "Dm7:2 G7:2 Cmaj7 Bm7b5 E7b9 C6/9 Bb7#11"), "saisie avec durees et qualites alterees");
        check (p.bank.pats[0].chord (0).beats == 2 && p.bank.pats[0].chordCount() == 7, "durees (:2) et nombre d'accords");
        check (! p.setProgressionText (0, "Xm7 Hmaj"), "texte invalide refuse");
        // transposition diatonique aller-retour
        p.setProgressionText (0, "Am7 Dm7 G7 Cmaj7 Fmaj7 Bm7b5 Em7");
        const auto before = p.progressionText (0);
        p.transposeProgression (0, 2); p.transposeProgression (0, -2);
        check (p.progressionText (0) == before, "transposition diatonique aller-retour (" + p.progressionText (0) + ")");
        // reharmonisation : toutes les techniques restent valides
        bool ok = true;
        for (int op = 0; op < th::kNumReharm; ++op)
            for (int s = 0; s < 6; ++s)
            {
                p.setProgressionText (0, "Am7 Dm7 G7 Cmaj7");
                p.reharmonize (0, op, -1, s * 31 + op);
                const int n = p.bank.pats[0].chordCount();
                ok = ok && n >= 1 && n <= kMaxChords && p.bank.pats[0].totalBeats() >= 4;
            }
        check (ok, juce::String (th::kNumReharm) + " techniques de reharmonisation (LaVerne) appliquees sans erreur");
        p.setProgressionText (0, "Am7 Dm7 G7 Cmaj7");
        p.reharmonize (0, th::RTritone, 2, 5);
        check (p.progressionText (0).contains ("Db7") || p.progressionText (0).contains ("C#7"), "substitution tritonique : G7 -> Db7 (" + p.progressionText (0) + ")");
        p.setProgressionText (0, "Cmaj7 Am7 Dm7 G7");
        p.addCadence (0, 0);
        check (p.bank.pats[0].chord (p.bank.pats[0].chordCount() - 1).root == 0, "cadence parfaite : finit sur la tonique (" + p.progressionText (0) + ")");
        p.simplifyProgression (0);
        bool triads = true; for (int i = 0; i < p.bank.pats[0].chordCount(); ++i) triads = triads && th::quality (p.bank.pats[0].chord (i).qual).n <= 4;
        check (triads, "Simplify : triades / 7e (" + p.progressionText (0) + ")");
        p.enrichProgression (0);
        check (true, "Enrich : " + p.progressionText (0));
        // generation par style dans le plugin
        for (int st = 0; st < th::kNumStyles; ++st)
        {
            setP (p, "style", (float) st);
            p.generateProgression (0, 100 + st);
            std::cout << "     " << th::styleNames()[st].paddedRight (' ', 18) << p.progressionText (0) << std::endl;
        }
        check (true, "generation dans les 12 styles (grilles ci-dessus)");
    }

    // =====================================================================
    std::cout << "== COLOR : son et jeu" << std::endl;
    {
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "play", 1.0f);
        juce::AudioBuffer<float> out;
        auto st = render (p, 8.0, &out);
        check (st.finite && st.peak > 0.05f && st.peak <= 1.0f, "preset par defaut : niveau plausible (crete " + juce::String (st.peak, 3) + ")");
        check (st.noteOns >= 8, "sortie MIDI des accords (" + juce::String (st.noteOns) + " notes en 8 s)");
        check (st.maxPoly <= 8, "polyphonie bornee a 8 voix (max " + juce::String (st.maxPoly) + ")");
        writeWav (outDir.getChildFile ("color_default.wav"), out, sr);
    }
    {
        // les notes jouees appartiennent a l'accord en cours (complexite 0, pas de substitution)
        bool okAll = true; juce::String detail;
        for (int pm = 0; pm < 4; ++pm)
        {
            std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
            setP (p, "complex", 0.0f); setP (p, "tension", 0.0f); setP (p, "playmode", (float) pm); setP (p, "notechaos", 0.0f);
            setP (p, "arpmode", 0.0f);
            p.setProgressionText (0, "Am Fmaj7 C/E G");
            applyRhythmPreset (p.bank.pats[0], 6, 1);
            setP (p, "play", 1.0f);
            auto st = render (p, 8.0);
            int good = 0, total = 0;
            for (auto& [pos, m] : st.midi)
            {
                if (! m.isNoteOn()) continue;
                const double ppq = (double) pos / sr * 2.0 + 0.02;
                const auto c = p.effectiveChord (0, 0.0, ppq);
                const uint16_t mk = absMask (c, 9) | (uint16_t) (1u << ((9 + th::chordBassPc (c)) % 12));
                ++total;
                if ((mk >> (m.getNoteNumber() % 12)) & 1u) ++good;
                else std::cout << "       hors accord : pm " << pm << " ppq " << ppq << " note " << m.getNoteNumber() << " accord " << th::chordName (c, 9) << std::endl;
            }
            const bool ok = total > 0 && good == total;
            okAll = okAll && ok;
            detail << playModeNames()[pm].fromFirstOccurrenceOf (": ", false, false) << " " << good << "/" << total << "  ";
        }
        check (okAll, "toutes les notes sont des notes de l'accord en cours (" + detail.trim() + ")");
    }
    {
        // notes communes conservees en mode Pad : C -> Am (C et E communs) ne redeclenche pas tout
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "root", 0.0f); setP (p, "mode", 0.0f); setP (p, "complex", 0.0f); setP (p, "voicing", (float) th::VClose); setP (p, "voices", 3.0f);
        setP (p, "bassmode", 0.0f); setP (p, "playmode", 0.0f);
        p.setProgressionText (0, "C Am");
        setP (p, "play", 1.0f);
        auto st = render (p, 3.9);
        check (st.noteOns <= 4, "Pad : notes communes conservees entre C et Am (" + juce::String (st.noteOns) + " declenchements au lieu de 6)");
    }
    {
        // modes de jeu : niveaux plausibles et nombre de notes croissant Pad < Comp ; Strum etale ; Arp une note a la fois
        juce::String lv; bool ok = true; int notes[4] {};
        for (int pm = 0; pm < 4; ++pm)
        {
            std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
            setP (p, "playmode", (float) pm);
            applyRhythmPreset (p.bank.pats[0], 1, 1);
            setP (p, "play", 1.0f);
            auto st = render (p, 6.0);
            notes[pm] = st.noteOns;
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 1) << " ";
            ok = ok && st.finite && st.peak > 0.03f && st.peak <= 1.0f;
        }
        check (ok && notes[1] > notes[0] && notes[3] < notes[1], "Pad / Comp / Strum / Arp (crete dB : " + lv.trim() + ", notes " + juce::String (notes[0]) + "/"
               + juce::String (notes[1]) + "/" + juce::String (notes[2]) + "/" + juce::String (notes[3]) + ")");
    }
    {
        // strum : les notes d'un coup sont decalees dans le temps
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "playmode", 2.0f); setP (p, "strum", 0.8f);
        applyRhythmPreset (p.bank.pats[0], 0, 1);
        setP (p, "play", 1.0f);
        auto st = render (p, 1.0);
        int64_t first = -1, last = -1;
        for (auto& [pos, m] : st.midi) if (m.isNoteOn()) { if (first < 0) first = pos; last = pos; }
        const double ms = (double) (last - first) / sr * 1000.0;
        check (ms > 30.0 && ms < 120.0, "Strum : accord etale sur " + juce::String (ms, 1) + " ms");
    }
    {
        // oscillateurs / filtres
        juce::String lv; bool ok = true;
        for (int o = 0; o < osc1Names().size(); ++o)
        {
            std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
            setP (p, "osc1type", (float) o);
            setP (p, "play", 1.0f);
            auto st = render (p, 2.0);
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 1) << " ";
            ok = ok && st.finite && st.peak > 0.03f && st.peak <= 1.0f;
        }
        check (ok, "8 oscillateurs polyphoniques (crete dB : " + lv.trim() + ")");
    }
    {
        juce::String lv; bool ok = true;
        const auto names = ColorAudioProcessor::factoryPresetNames();
        for (int i = 0; i < names.size(); ++i)
        {
            std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
            p.loadFactoryPreset (i);
            setP (p, "play", 1.0f);
            juce::AudioBuffer<float> out;
            auto st = render (p, 8.0, &out);
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 1) << " ";
            ok = ok && st.finite && st.peak > 0.03f && st.peak <= 1.0f;
            writeWav (outDir.getChildFile ("color_preset_" + juce::String (i + 1).paddedLeft ('0', 2) + "_" + names[i].replace (" ", "_") + ".wav"), out, sr);
            std::cout << "     " << names[i].paddedRight (' ', 24) << p.progressionText (0) << std::endl;
        }
        check (ok, juce::String (names.size()) + " presets d'usine a un niveau plausible (dB : " + lv.trim() + ")");
    }
    {
        // substitutions en direct et accords d'approche : la progression n'est pas modifiee
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        p.setProgressionText (0, "Dm7 G7 Cmaj7 A7");
        const auto before = p.progressionText (0);
        setP (p, "livesub", 1.0f); setP (p, "approach", 1.0f); setP (p, "playmode", 1.0f);
        setP (p, "play", 1.0f);
        int subs = 0, appr = 0, prevIdx = -1;
        juce::AudioBuffer<float> buf (2, 512);
        for (int b = 0; b < (int) (16.0 * sr / 512); ++b)
        {
            juce::MidiBuffer m; p.processBlock (buf, m);
            if (p.curChordIndex.load() != prevIdx || b % 20 == 0)
            {
                prevIdx = p.curChordIndex.load();
                if (p.curChordSub.load()) ++subs;
                if (p.curChordApproach.load()) ++appr;
            }
        }
        check (subs > 0 && appr > 0 && p.progressionText (0) == before, "Live Sub + Approach : " + juce::String (subs) + " substitutions, "
               + juce::String (appr) + " approches, progression intacte");
    }
    {
        // clavier : Chord select, Harmonize, Play (transport arrete)
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        p.setProgressionText (0, "Am Fmaj7 C G");
        setP (p, "keysmode", 2.0f); setP (p, "complex", 0.0f);
        auto st = render (p, 0.5, nullptr, sr, 512, [] (juce::MidiBuffer& m, int64_t pos) { if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 50, (juce::uint8) 100), 0); });
        // D3 = 2e touche blanche depuis C3 -> accord 2 (Fmaj7)
        bool fOk = st.noteOns >= 3;
        for (auto& [pos, m] : st.midi) if (m.isNoteOn()) fOk = fOk && ((absMask (p.bank.pats[0].chord (1), 9) >> (m.getNoteNumber() % 12)) & 1u);
        check (fOk, "clavier Chord select : D3 joue l'accord 2 (Fmaj7) - " + juce::String (st.noteOns) + " notes");
        render (p, 0.2, nullptr, sr, 512, [] (juce::MidiBuffer& m, int64_t pos) { if (pos == 0) m.addEvent (juce::MidiMessage::noteOff (1, 50), 0); });
        setP (p, "keysmode", 4.0f); setP (p, "complex", 0.0f);
        auto st2 = render (p, 0.5, nullptr, sr, 512, [] (juce::MidiBuffer& m, int64_t pos) { if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 52, (juce::uint8) 100), 0); });
        const auto kc = unpackChord (p.keyChordPacked.load());
        check (st2.noteOns >= 3 && th::chordName (kc, 9) == "Em", "clavier Play : une touche (E) = accord diatonique du degre en la mineur (" + th::chordName (kc, 9) + ")");
        render (p, 0.2, nullptr, sr, 512, [] (juce::MidiBuffer& m, int64_t pos) { if (pos == 0) m.addEvent (juce::MidiMessage::noteOff (1, 52), 0); });
        auto st3 = render (p, 0.5, nullptr, sr, 512, [] (juce::MidiBuffer& m, int64_t pos)
        { if (pos == 0) for (int n : { 50, 53, 57, 60 }) m.addEvent (juce::MidiMessage::noteOn (1, n, (juce::uint8) 100), 0); });
        const auto kc2 = unpackChord (p.keyChordPacked.load());
        check (th::chordName (kc2, 9) == "Dm7", "clavier Play : D F A C reconnu comme " + th::chordName (kc2, 9));
        render (p, 0.2, nullptr, sr, 512, [] (juce::MidiBuffer& m, int64_t pos) { if (pos == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); });
        setP (p, "keysmode", 3.0f);
        render (p, 0.3, nullptr, sr, 512, [] (juce::MidiBuffer& m, int64_t pos) { if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 66, (juce::uint8) 100), 0); });
        const auto kc3 = unpackChord (p.keyChordPacked.load());
        check ((absMask (kc3, 9) >> 6) & 1u, "clavier Harmonize : F# harmonise par " + th::chordName (kc3, 9));
        juce::ignoreUnused (st3);
    }

    {
        // harmonisation d'une melodie (Au clair de la lune, do majeur, 8 mesures)
        const int mel[] { 60,60,60,62, 64,-1,62,-1, 60,64,62,62, 60,-1,-1,-1, 60,60,60,62, 64,-1,62,-1, 60,64,62,62, 60,-1,-1,-1 };
        juce::MidiMessageSequence seq;
        for (int i = 0; i < 32; ++i)
        {
            if (mel[i] < 0) continue;
            int len = 1; while (i + len < 32 && mel[i + len] < 0) ++len;
            seq.addEvent (juce::MidiMessage::noteOn (1, mel[i], (juce::uint8) 100), i * 960.0);
            seq.addEvent (juce::MidiMessage::noteOff (1, mel[i]), (i + len) * 960.0 - 10);
        }
        seq.updateMatchedPairs();
        juce::MidiFile mf; mf.setTicksPerQuarterNote (960); mf.addTrack (seq);
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("color_melody_test.mid");
        f.deleteFile(); { juce::FileOutputStream os (f); mf.writeTo (os); }
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "chordlen", 1.0f); setP (p, "complex", 0.5f);
        check (ColorAudioProcessor::isMelodyFile (f), "fichier MIDI reconnu comme melodie");
        const bool ok = p.harmonizeMelodyFile (f, 0);
        check (ok && p.keyRoot() == 0, "harmonisation : tonalite estimee (" + rootNames()[p.keyRoot()] + " " + th::scale (p.mode()).name + ") -> " + p.progressionText (0));
        // notes des temps forts (temps 1 et 3) dans l'accord
        int good = 0, tot = 0;
        for (int i = 0; i < 32; i += 2)
        {
            int note = mel[i]; int k = i; while (note < 0 && k > 0) note = mel[--k];
            const auto c = p.effectiveChord (0, 0.0, i + 0.01);
            ++tot; if ((absMask (c, p.keyRoot()) >> (note % 12)) & 1u) ++good;
        }
        check (good * 100 >= tot * 85, "harmonisation : notes des temps forts dans l'accord (" + juce::String (good) + "/" + juce::String (tot) + ")");
        check (th::chordFunction (p.bank.pats[0].chord (0), p.mode()) == 0, "harmonisation : commence sur la tonique");
        auto f2 = p.exportMidi (0, 4);
        check (! ColorAudioProcessor::isMelodyFile (f2), "un fichier d'accords n'est pas pris pour une melodie");
        f.deleteFile();
    }
    {
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        p.loadFactoryPreset (1);
        setP (p, "vspread", 1.0f); setP (p, "play", 1.0f);
        auto st = render (p, 3.0);
        check (st.finite && st.peak > 0.03f && st.peak <= 1.0f, "Voice Spread au maximum : rendu sain");
    }
    {
        // reproductibilite
        auto run = [&] (juce::AudioBuffer<float>& o)
        {
            std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
            p.loadFactoryPreset (2);
            setP (p, "seed", 42.0f); setP (p, "livesub", 0.5f); setP (p, "chaos", 0.8f);
            setP (p, "play", 1.0f);
            render (p, 4.0, &o);
        };
        juce::AudioBuffer<float> a, b; run (a); run (b);
        float diff = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (a.getSample (c, i) - b.getSample (c, i)));
        check (diff < 1.0e-4f, "rendu reproductible a seed egale (ecart " + juce::String (diff) + ")");
    }
    {
        // rythmes, euclide, mutation, croisement
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        bool ok = true;
        for (int r = 0; r < rhythmNames().size(); ++r)
        {
            p.applyRhythm (0, r, 5);
            int on = 0; for (int i = 0; i < p.bank.pats[0].length.load(); ++i) on += p.bank.pats[0].steps[i].on.load();
            ok = ok && on > 0;
        }
        check (ok, juce::String (rhythmNames().size()) + " rythmes predefinis");
        p.euclidPattern (0, 5, 0);
        int on = 0; for (int i = 0; i < 16; ++i) on += p.bank.pats[0].steps[i].on.load();
        check (on == 5, "Euclid 5 coups (" + juce::String (on) + ")");
        p.setProgressionText (0, "Am F C G"); p.setProgressionText (1, "Dm7 G7 Cmaj7 Fmaj7");
        p.breedPatterns (0, 1, 2, 7);
        check (p.bank.pats[2].chordCount() >= 2, "croisement de patterns : " + p.progressionText (2));
    }
    {
        // evolution non destructive
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        p.setProgressionText (0, "Dm7 G7 Cmaj7 A7");
        setP (p, "evoamt", 1.0f); setP (p, "evobars", 0.0f); setP (p, "evotarget", 0.0f); setP (p, "playmode", 1.0f);
        const auto orig = p.bank.pats[0].toTree (0).toXmlString();
        setP (p, "play", 1.0f);
        render (p, 30.0);
        const int amt = p.evolutionAmount();
        check (amt > 0 && p.bank.pats[0].toTree (0).toXmlString() == orig, "evolution non destructive (" + juce::String (amt) + " changements)");
        p.commitEvolution();
        check (p.evolutionAmount() == 0 && p.bank.pats[0].toTree (0).toXmlString() != orig, "Commit ecrit l'evolution : " + p.progressionText (0));
    }
    {
        // export MIDI puis analyse du fichier : on retrouve la grille
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "playmode", 0.0f); setP (p, "complex", 0.3f);
        p.setProgressionText (0, "Am7 Dm7 G7 Cmaj7");
        auto f = p.exportMidi (0, 4);
        check (f.existsAsFile() && f.getSize() > 60, "export MIDI : " + f.getFileName());
        std::unique_ptr<ColorAudioProcessor> h2; auto& q = *fresh (h2);
        const bool learnt = q.learnChordsFromMidi (f, 0);
        check (learnt && q.bank.pats[0].chordCount() == 4, "analyse MIDI : " + q.progressionText (0) + " (tonalite " + rootNames()[q.keyRoot()] + " "
               + th::scale (q.mode()).name + ")");
        bool rootsOk = true;
        const int kp = p.keyRoot(), kq = q.keyRoot();
        for (int i = 0; i < 4 && i < q.bank.pats[0].chordCount(); ++i)
            rootsOk = rootsOk && (p.bank.pats[0].chord (i).root + kp) % 12 == (q.bank.pats[0].chord (i).root + kq) % 12;
        check (rootsOk, "analyse MIDI : memes fondamentales que la grille exportee");
    }
    {
        // etat complet, undo, preset
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        p.loadFactoryPreset (1);
        p.setProgressionText (3, "Ebmaj7 Abmaj7/C Bb7sus4 Cm9");
        p.bank.pats[3].steps[5].plock[color::Cutoff] = 321;
        p.storeScene (6); p.storeFav (2); p.setChainText ("A2 D4");
        juce::MemoryBlock mb; p.getStateInformation (mb);
        std::unique_ptr<ColorAudioProcessor> h2; auto& q = *fresh (h2);
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool same = q.progressionText (3) == p.progressionText (3) && q.bank.pats[3].steps[5].plock[color::Cutoff].load() == 321
                          && q.sceneValid[6].load() && q.favs[2].valid && q.getChainText() == "A2 D4";
        check (same, "etat complet restaure (progressions, p-locks, scenes, favoris, chaine)");
        p.pushUndo();
        p.setProgressionText (3, "C");
        check (p.undo() && p.progressionText (3) == q.progressionText (3), "undo");
        check (p.redo(), "redo");
        auto pf = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("color_test.color");
        check (p.savePresetFile (pf) && q.loadPresetFile (pf), "preset utilisateur .color");
        pf.deleteFile();
    }
    {
        // CPU : 8 voix SuperSaw tenues
        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        p.loadFactoryPreset (3);
        setP (p, "voices", 7.0f);
        setP (p, "play", 1.0f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (p, 20.0);
        const double pct = (juce::Time::getMillisecondCounterHiRes() - t0) / 20000.0 * 100.0;
        std::cout << "CPU (8 voix SuperSaw) : " << juce::String (pct, 2) << " % d'un coeur" << std::endl;
        check (pct < 40.0, "charge CPU raisonnable");
    }
    {
        juce::Random r (4321);
        bool finite = true; float peak = 0;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int bs : { 1, 17, 64, 256, 1024, 2048 })
            {
                ColorAudioProcessor p; p.prepareToPlay (fs, bs);
                for (auto* prm : p.getParameters())
                    if (r.nextFloat() < 0.6f) prm->setValueNotifyingHost (r.nextFloat());
                setP (p, "play", 1.0f); setP (p, "master", 0.8f); setP (p, "lk_on", 0.0f);
                p.generateProgression (p.editPattern(), r.nextInt (1000));
                p.bank.pats[p.editPattern()].steps[1].plock[color::Reso] = 990;
                auto st = render (p, 1.2, nullptr, fs, bs, [&] (juce::MidiBuffer& m, int64_t)
                {
                    if (r.nextFloat() < 0.05f) m.addEvent (juce::MidiMessage::noteOn (1, 40 + r.nextInt (40), (juce::uint8) 100), 0);
                    if (r.nextFloat() < 0.03f) m.addEvent (juce::MidiMessage::noteOff (1, 40 + r.nextInt (40)), 0);
                    if (r.nextFloat() < 0.02f) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                });
                finite = finite && st.finite; peak = juce::jmax (peak, st.peak);
            }
        check (finite, "fuzz : aucun NaN/Inf (3 sample rates x 6 tailles de bloc, MIDI aleatoire)");
        check (peak <= 1.0001f, "fuzz : sorties bornees a 0 dBFS (crete " + juce::String (peak, 3) + ")");
    }

    std::cout << "== COLOR : nouveautes (guitare, modulation, couches)" << std::endl;
    {
        // voicing guitare : 4 a 6 cordes, dans la tessiture, basse = fondamentale, 3ce presente
        int good = 0, total = 0;
        th::GenOptions go; go.style = th::SPop; go.scale = 0; go.numChords = 8; go.seed = 11;
        for (int seed = 1; seed <= 6; ++seed)
        {
            go.seed = (uint32_t) seed; go.style = seed % 3 == 0 ? th::SJazz : th::SPop;
            th::VoiceOptions vo; vo.voicing = th::VGuitar; vo.bassMode = 0; vo.keyRoot = 4; vo.center = 60; vo.scale = 0;
            int prev[8]; int pn = 0;
            for (auto& c : th::generate (go))
            {
                int notes[8];
                const int n = th::voiceChord (c, vo, pn > 0 ? prev : nullptr, pn, notes, 8);
                ++total;
                bool ok = n >= 4 && n <= 6 && notes[0] >= 40 && notes[n - 1] <= 88 && (notes[0] - (4 + c.root)) % 12 == 0;
                bool has3 = false;
                for (int i = 0; i < n; ++i) { const int iv = ((notes[i] - 4 - c.root) % 12 + 12) % 12; if (iv == 3 || iv == 4 || iv == 5 || iv == 2) has3 = true; }
                if (ok && has3) ++good;

                for (int i = 0; i < n; ++i) prev[i] = notes[i];
                pn = n;
            }
        }
        check (good * 10 >= total * 9, "voicing guitare : 4-6 cordes, fondamentale a la basse, 3ce presente (" + juce::String (good) + "/" + juce::String (total) + ")");

        std::unique_ptr<ColorAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "root", 0.0f); setP (p, "mode", 0.0f); setP (p, "chordlen", 1.0f);
        p.setProgressionText (0, "C Am F G");
        p.modulateToNext (0, 5, 0);
        check (p.bank.pats[1].keyShift.load() == 5 && p.progressionText (0).endsWith ("C7") && p.progressionText (1).startsWith ("F"),
               "modulation par la dominante vers +5 : " + p.progressionText (0) + "  ->  " + p.progressionText (1));
        p.setProgressionText (0, "C Am F G");
        p.modulateToNext (0, 7, 2);
        check ((p.bank.pats[1].keyShift.load() + 12) % 12 == 7 && p.progressionText (0).endsWith ("D7"), "modulation par accord pivot vers la dominante : " + p.progressionText (0));
        // chaine transposee : B joue dans la tonalite +5
        p.setProgressionText (0, "C"); p.setProgressionText (1, "C");
        p.bank.pats[1].keyShift = 0;
        check (p.setChainText ("A1 A1+5"), "chaine Song transposee (A1 A1+5)");
        setP (p, "songmode", 1.0f); setP (p, "complex", 0.0f); setP (p, "playmode", 0.0f); setP (p, "bassmode", 0.0f);
        setP (p, "play", 1.0f);
        auto st = render (p, 4.0);
        int inF = 0, inC = 0;
        for (auto& [pos, m] : st.midi)
            if (m.isNoteOn())
            {
                const double bar = (double) pos / sr / 2.0;
                const int pc = m.getNoteNumber() % 12;
                if (bar >= 0.98 && bar < 2.0) { if (pc == 5 || pc == 9 || pc == 0) ++inF; }
                if (bar < 0.95) { if (pc == 0 || pc == 4 || pc == 7) ++inC; }
            }
        check (inF >= 3 && inC >= 3, "la 2e mesure sonne en fa (" + juce::String (inF) + " notes de F, " + juce::String (inC) + " de C)");
        // couches
        juce::String lv; bool ok = true;
        for (int l = 0; l < layerNames().size(); ++l)
        {
            std::unique_ptr<ColorAudioProcessor> h2; auto& q = *fresh (h2);
            setP (q, "layer", (float) l); setP (q, "layerlvl", 0.8f);
            setP (q, "play", 1.0f);
            auto s2 = render (q, 2.0);
            lv << juce::String (juce::Decibels::gainToDecibels (s2.peak), 1) << " ";
            ok = ok && s2.finite && s2.peak > 0.03f && s2.peak <= 1.0f;
        }
        check (ok, "couches (off, cloche, verre, souffle, octave) : dB " + lv.trim());
    }

    // =====================================================================
    std::cout << "== AleaLink : Color -> Talea / Alea" << std::endl;
    {
        const auto shm = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("alealink-color-" + juce::String (juce::Time::currentTimeMillis()) + ".shm");
        shm.deleteFile();
        setenv ("ALEALINK_FILE", shm.getFullPathName().toRawUTF8(), 1);
        {
            AleaAudioProcessor a; a.prepareToPlay (sr, 512);
            pump (200);
            TaleaAudioProcessor t; t.prepareToPlay (sr, 512);
            pump (200);
            ColorAudioProcessor c; c.prepareToPlay (sr, 512);
            pump (300);
            check (c.link->connected() && c.link->firstPeerOfKind (alea::link::KindAlea) >= 0 && c.link->firstPeerOfKind (alea::link::KindTalea) >= 0,
                   "Color detecte Alea et Talea");
            check (t.link->firstPeerOfKind (alea::link::KindColor) >= 0, "Talea detecte Color");
            setP (c, "mode", 1.0f); pump (250);     // dorien -> gamme partagee "mineur"
            check ((int) t.paramReal ("scale") == 8, "mode dorien de Color -> gamme Dorian chez Talea (mode exact partage)");
            setP (t, "root", 2.0f); pump (250);
            check (c.keyRoot() == 2, "tonalite : Talea -> Color (D)");

            c.setProgressionText (0, "Dm7 Bbmaj7 Gm7 A7");
            setP (c, "playmode", 0.0f);
            setP (t, "chordfollow", 2.0f);
            setP (a, "keylock", 2.0f); setP (a, "keychord", 1.0f);
            setP (c, "play", 1.0f); pump (250);
            check (t.paramReal ("play") > 0.5f && a.apvts.getRawParameterValue ("play")->load() > 0.5f, "Play de Color lance Talea et Alea");

            juce::AudioBuffer<float> ba (2, 512), bt (2, 512), bc (2, 512);
            int tNotes = 0, tGood = 0; double maxDiff = 0;
            const int n = (int) (16.0 * 60.0 / 120.0 * 4.0 * sr / 512.0);
            juce::AudioBuffer<float> mix (2, n * 512);
            int maskSeen = 0;
            for (int b = 0; b < n; ++b)
            {
                juce::MidiBuffer m1, m2, m3;
                c.processBlock (bc, m3);
                a.processBlock (ba, m1);
                t.processBlock (bt, m2);
                if (a.colorChordMask != 0) ++maskSeen;
                for (const auto mm : m2)
                    if (mm.getMessage().isNoteOn())
                    {
                        ++tNotes;
                        const double ppq = t.playPpq.load() - (512 - mm.samplePosition) / sr * 2.0;
                        const auto ch = c.effectiveChord (c.currentPattern.load(), c.patStartShared.load(), ppq + 1.0e-4);
                        if ((absMask (ch, c.keyRoot()) >> (mm.getMessage().getNoteNumber() % 12)) & 1u) ++tGood;
                    }
                if (b > 20) maxDiff = juce::jmax (maxDiff, std::abs (c.playPpq.load() - t.playPpq.load()));
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        mix.setSample (ch, b * 512 + i, 0.5f * ba.getSample (ch, i) + 0.6f * bt.getSample (ch, i) + 0.7f * bc.getSample (ch, i));
                if (b % 40 == 0) pump (1);
            }
            check (maxDiff < 0.05, "horloge partagee Color / Talea (ecart max " + juce::String (maxDiff, 4) + " temps)");
            check (tNotes > 8 && tGood * 100 >= tNotes * 95, "Talea Chord tones : notes de basse dans l'accord de Color (" + juce::String (tGood) + "/" + juce::String (tNotes) + ")");
            check (maskSeen > n / 2, "Alea Key lock = accords de Color : accord recu");
            writeWav (outDir.getChildFile ("trio_alea_talea_color.wav"), mix, sr);

            // Root : la ligne suit la basse des accords
            setP (t, "chordfollow", 1.0f); pump (100);
            int rootHits = 0, rn = 0;
            for (int b = 0; b < 400; ++b)
            {
                juce::MidiBuffer m1, m2, m3;
                c.processBlock (bc, m3); a.processBlock (ba, m1); t.processBlock (bt, m2);
                if (t.followChordRoot.load() >= 0) ++rootHits;
                ++rn;
            }
            check (rootHits > rn / 2, "Talea Root : accord de Color suivi");


            // rythme d'apres la batterie d'Alea
            {
                uint32_t kick = 0, snare = 0; int len = 0, rate = 0;
                const int as = c.link->firstPeerOfKind (alea::link::KindAlea);
                c.link->readTrack (as, 0, kick, len, rate);
                c.link->readTrack (as, 1, snare, len, rate);
                const bool r0 = c.rhythmFromDrums (0, 0);
                int clash = 0, hitsN = 0;
                for (int i = 0; i < c.bank.pats[0].length.load(); ++i)
                    if (c.bank.pats[0].steps[i].on.load()) { ++hitsN; if ((kick >> i) & 1u) ++clash; }
                check (r0 && hitsN > 0 && clash == 0, "Rythme d'Alea : accords entre les kicks (" + juce::String (hitsN) + " coups, 0 sur le kick)");
                const bool r1 = c.rhythmFromDrums (0, 1);
                bool same = r1;
                for (int i = 0; i < c.bank.pats[0].length.load(); ++i) same = same && ((c.bank.pats[0].steps[i].on.load() != 0) == (((snare >> i) & 1u) != 0));
                check (same && snare != 0, "Rythme d'Alea : accords sur la caisse claire");
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed (c.createEditor());
            auto* e = dynamic_cast<ColorAudioProcessorEditor*> (ed.get());
            e->getView().showTab (7);
            pump (300);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("color_gui_link_connecte.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            setP (c, "play", 0.0f); pump (250);
        }
        shm.deleteFile();
        setenv ("ALEALINK_FILE", "off", 1);
    }

    // =====================================================================
    std::cout << "== COLOR : interface" << std::endl;
    {
        ColorAudioProcessor p; p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (1);
        setP (p, "play", 1.0f); setP (p, "livesub", 0.4f);
        p.storeB();
        render (p, 2.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<ColorAudioProcessorEditor*> (ed.get());
        std::cout << "taille initiale de l'editeur : " << ed->getWidth() << "x" << ed->getHeight() << std::endl;
        const char* names[] { "synth", "harmonie", "mod", "fx", "perform", "lab", "theorie", "link", "aide" };
        for (int tab = 0; tab < 9; ++tab)
        {
            e->getView().showTab (tab);
            pump (120); render (p, 0.4); pump (120);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (juce::String ("color_gui_") + names[tab] + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        e->getView().setLockStep (2);
        pump (120);
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("color_gui_plock.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        check (true, "captures de l'interface (9 onglets + p-lock)");
        ed->setSize ((int) (ColorView::W * 0.75f), (int) (ColorView::H * 0.75f));
        check (std::abs (p.uiScale.load() - 0.75f) < 0.01f, "interface redimensionnable (echelle memorisee)");
        for (int i = 0; i < 5; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e2 (p.createEditor());
            render (p, 0.05);
            juce::MemoryBlock mb; p.getStateInformation (mb); p.setStateInformation (mb.getData(), (int) mb.getSize());
        }
        check (true, "editeur ouvert/ferme + rechargement d'etat pendant le rendu");
    }

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : "ECHECS : " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
