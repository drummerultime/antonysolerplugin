// Banc de test hors ligne de CANTUS (melodie) et du quatuor Alea + Talea + Color + Cantus (AleaLink v3).
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "CantusProcessor.h"
#include "CantusEditor.h"
#include "ColorProcessor.h"
#include "TaleaProcessor.h"
#include "PluginProcessor.h"     // Alea

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
    float peak = 0; bool finite = true; int noteOns = 0;
    std::vector<std::pair<int64_t, juce::MidiMessage>> midi;
};

template <typename Proc>
static Stats render (Proc& p, double seconds, juce::AudioBuffer<float>* out = nullptr, double sr = 48000.0, int block = 512)
{
    const int total = (int) (seconds * sr);
    if (out) out->setSize (2, total);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Stats st;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        midi.clear();
        p.processBlock (view, midi);
        for (const auto m : midi)
        {
            if (m.getMessage().isNoteOn()) ++st.noteOns;
            st.midi.push_back ({ (int64_t) pos + m.samplePosition, m.getMessage() });
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = view.getSample (c, i);
                if (! std::isfinite (v)) st.finite = false;
                st.peak = juce::jmax (st.peak, std::abs (v));
                if (out) out->setSample (c, pos + i, v);
            }
        if ((pos / block) % 50 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
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

static bool inMinor (int pcRel) { for (int d : { 0, 2, 3, 5, 7, 8, 10 }) if (pcRel == d) return true; return false; }

int main (int argc, char** argv)
{
    setenv ("ALEALINK_FILE", "off", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "test_out");
    outDir.createDirectory();
    const double sr = 48000.0;

    std::cout << "== CANTUS : melodie seule" << std::endl;
    {
        // 9 styles : melodies dans la gamme (la mineur), contour borne, fin sur la tonique ou la quinte
        bool inKey = true, ends = true, range = true; juce::String counts;
        for (int st = 0; st < cantus::genStyleNames().size(); ++st)
        {
            CantusAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, "bpm", 120.0f); setP (p, "root", 9.0f); setP (p, "scale", 3.0f);
            p.generatePattern (0, st, 100 + st);
            const auto& P = p.bank.pats[0];
            int lastNote = -100, lo = 1000, hi = -1000, notes = 0;
            for (int i = 0; i < P.length; ++i)
            {
                const auto& s = P.steps[i];
                if (! s.on || s.tie) continue;
                const int v = s.note + 12 * s.oct;
                ++notes; lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); lastNote = v;
                if (st != 4 && ! inMinor (((v % 12) + 12) % 12)) inKey = false;   // le bebop garde ses chromatismes de passage
            }
            const int lastPc = ((lastNote % 12) + 12) % 12;
            if (! (lastPc == 0 || lastPc == 7)) ends = false;
            if (hi - lo > 26) range = false;
            counts << notes << " ";
        }
        check (inKey, "9 styles de melodie : toutes les notes dans la gamme (notes : " + counts.trim() + ")");
        check (ends, "chaque phrase se termine sur la tonique ou la quinte (cadence)");
        check (range, "ambitus raisonnable (moins de 2 octaves)");
    }
    {
        CantusAudioProcessor p; p.prepareToPlay (sr, 512);
        setP (p, "play", 1.0f); setP (p, "bpm", 120.0f);
        juce::AudioBuffer<float> out;
        auto st = render (p, 8.0, &out);
        check (st.finite && st.peak > 0.05f && st.peak <= 1.0f && st.noteOns >= 6, "melodie par defaut : niveau " + juce::String (st.peak, 2) + ", " + juce::String (st.noteOns) + " notes");
        // vibrato : change le son, reste fini
        CantusAudioProcessor q; q.prepareToPlay (sr, 512);
        setP (q, "play", 1.0f); setP (q, "bpm", 120.0f); setP (q, "vibdepth", 1.0f); setP (q, "vibdelay", 0.0f);
        juce::AudioBuffer<float> o2;
        auto s2 = render (q, 8.0, &o2);
        float diff = 0; for (int i = 0; i < out.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (out.getSample (0, i) - o2.getSample (0, i)));
        check (s2.finite && diff > 0.01f, "vibrato actif (ecart " + juce::String (diff, 3) + ")");
    }
    {
        juce::String lv; bool ok = true;
        const auto names = CantusAudioProcessor::factoryPresetNames();
        for (int i = 0; i < names.size(); ++i)
        {
            CantusAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, "bpm", 120.0f);
            p.loadFactoryPreset (i);
            setP (p, "play", 1.0f);
            juce::AudioBuffer<float> out;
            auto st = render (p, 8.0, &out);
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 1) << " ";
            ok = ok && st.finite && st.peak > 0.03f && st.peak <= 1.0f && st.noteOns > 0;
            writeWav (outDir.getChildFile ("cantus_preset_" + juce::String (i + 1) + "_" + names[i].replace (" ", "_") + ".wav"), out, sr);
        }
        check (ok, juce::String (names.size()) + " presets d'usine (dB : " + lv.trim() + ")");
    }
    {
        // improvisation : une nouvelle phrase chaque mesure
        CantusAudioProcessor p; p.prepareToPlay (sr, 512);
        setP (p, "bpm", 120.0f); setP (p, "improv", 1.0f); setP (p, "improvdev", 0.5f); setP (p, "play", 1.0f);
        const auto before = p.bank.pats[0].toTree (0).toXmlString();
        render (p, 16.0);
        check (p.improvCount.load() >= 5 && p.bank.pats[0].toTree (0).toXmlString() != before, "Improvise : " + juce::String (p.improvCount.load()) + " phrases en 8 mesures");
    }
    {
        // question / reponse : silence une mesure sur deux
        CantusAudioProcessor p; p.prepareToPlay (sr, 512);
        setP (p, "bpm", 120.0f); setP (p, "callresp", 1.0f); setP (p, "play", 1.0f);
        auto st = render (p, 8.0);
        int odd = 0, even = 0;
        for (auto& [pos, m] : st.midi) if (m.isNoteOn()) (((int) ((double) pos / sr / 2.0 + 0.002) & 1) ? odd : even)++;
        check (odd == 0 && even > 0, "Call & Response : notes en mesures paires seulement (" + juce::String (even) + " / " + juce::String (odd) + ")");
    }
    {
        // etat
        CantusAudioProcessor p; p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (3);
        setP (p, "improv", 2.0f); setP (p, "dialogue", 0.8f);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        CantusAudioProcessor q; q.prepareToPlay (sr, 512);
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.bank.pats[0].toTree (0).toXmlString() == p.bank.pats[0].toTree (0).toXmlString() && (int) q.paramReal ("improv") == 2,
               "etat restaure (melodie, improvisation, dialogue)");
    }
    {
        juce::Random r (77);
        bool finite = true; float peak = 0;
        for (double fs : { 44100.0, 96000.0 })
            for (int bs : { 1, 64, 1024 })
            {
                CantusAudioProcessor p; p.prepareToPlay (fs, bs);
                for (auto* prm : p.getParameters()) if (r.nextFloat() < 0.6f) prm->setValueNotifyingHost (r.nextFloat());
                setP (p, "play", 1.0f); setP (p, "master", 0.8f); setP (p, "lk_on", 0.0f);
                p.generatePattern (0, r.nextInt (9), r.nextInt (1000));
                auto st = render (p, 1.5, nullptr, fs, bs);
                finite = finite && st.finite; peak = juce::jmax (peak, st.peak);
            }
        check (finite && peak <= 1.0001f, "fuzz : aucun NaN, crete " + juce::String (peak, 3));
    }

    // =====================================================================
    {
        // une note qui chevauche la suivante (swing, humanize, gate long) doit etre redeclenchee : seul un slide lie les notes
        CantusAudioProcessor p; p.prepareToPlay (sr, 512);
        setP (p, "bpm", 120.0f);
        auto& P = p.bank.pats[0];
        P.clear(); P.length = 16; P.rate = 1;
        for (int i = 0; i < 16; ++i) P.steps[i].set (1, (i * 3) % 12, 0, 0, 0, 0, 100, 1, 0, 100);
        setP (p, "swing", 0.7f); setP (p, "humanize", 1.0f); setP (p, "play", 1.0f);
        p.legatoNotes = 0;
        render (p, 4.0);
        const int lg = p.legatoNotes.load();
        P.steps[3].slide = 1; P.steps[9].slide = 1;
        p.legatoNotes = 0;
        render (p, 4.0);
        check (lg == 0 && p.legatoNotes.load() > 0, "chevauchements redeclenches (0 note avalee) ; seuls les slides lient les notes (" + juce::String (p.legatoNotes.load()) + ")");
    }

    {
        // diagnostic : chaque style genere, combien de notes auraient ete avalees par l'ancienne version
        juce::String rep;
        for (int st = 0; st < cantus::genStyleNames().size(); ++st)
        {
            CantusAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, "bpm", 120.0f); setP (p, "lk_on", 0.0f);
            setP (p, "genstyle", (float) st);
            p.generatePattern (0, st, 77 + st);
            setP (p, "play", 1.0f);
            p.notesPlayed = 0; p.overlapNotes = 0; p.legatoNotes = 0;
            render (p, 8.0);
            rep << cantus::genStyleNames()[st] << " " << p.notesPlayed.load() << "/" << p.overlapNotes.load() << "/" << p.legatoNotes.load() << "  ";
        }
        std::cout << "     notes jouees / chevauchements rejoues / slides : " << rep << std::endl;
    }

    {
        // Density basse : les notes sur les temps restent ; diagnostic des notes retirees ; bouton "jouer toutes les notes"
        CantusAudioProcessor p; p.prepareToPlay (sr, 512);
        setP (p, "bpm", 120.0f); setP (p, "lk_on", 0.0f);
        auto& P = p.bank.pats[0];
        P.clear(); P.length = 16; P.rate = 1;
        for (int i = 0; i < 16; ++i) P.steps[i].set (1, (i * 2) % 12, 0, 0, 0, 0, i % 3 == 0 ? 50 : 100, 1, 0, 60);
        setP (p, "density", 0.0f); setP (p, "seqchaos", 0.6f); setP (p, "callresp", 1.0f); setP (p, "play", 1.0f);
        p.notesPlayed = 0; for (auto& d : p.seqDrops) d = 0; p.callRespDrops = 0;
        auto st = render (p, 8.0);
        int onBeatNotes = 0;
        for (auto& [pos, m] : st.midi) if (m.isNoteOn()) { const double q = (double) pos / sr * 2.0; if (std::abs (q - std::round (q)) < 0.02) ++onBeatNotes; }
        check (p.seqDrops[0].load() > 0 && p.seqDrops[1].load() > 0 && p.seqDrops[2].load() > 0 && p.callRespDrops.load() > 0,
               "diagnostic : notes retirees comptees (proba " + juce::String (p.seqDrops[0].load()) + ", density " + juce::String (p.seqDrops[1].load())
               + ", seq chaos " + juce::String (p.seqDrops[2].load()) + ", call/resp " + juce::String (p.callRespDrops.load()) + ")");
        check (onBeatNotes > 0, "Density a 0 : les notes sur les temps restent (" + juce::String (onBeatNotes) + ")");
        p.playAllNotes();
        p.notesPlayed = 0; for (auto& d : p.seqDrops) d = 0; p.callRespDrops = 0;
        render (p, 8.0);
        check (p.notesPlayed.load() >= 60 && p.seqDrops[0].load() + p.seqDrops[1].load() + p.seqDrops[2].load() + p.callRespDrops.load() == 0,
               "Jouer toutes les notes : " + juce::String (p.notesPlayed.load()) + " notes, aucune retiree");
    }

    std::cout << "== Quatuor : Alea + Talea + Color + Cantus" << std::endl;
    {
        const auto shm = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("alealink-q-" + juce::String (juce::Time::currentTimeMillis()) + ".shm");
        shm.deleteFile();
        setenv ("ALEALINK_FILE", shm.getFullPathName().toRawUTF8(), 1);
        {
            AleaAudioProcessor a; a.prepareToPlay (sr, 512); pump (150);
            TaleaAudioProcessor t; t.prepareToPlay (sr, 512); pump (150);
            ColorAudioProcessor c; c.prepareToPlay (sr, 512); pump (150);
            CantusAudioProcessor m; m.prepareToPlay (sr, 512); pump (300);
            check (m.link->firstPeerOfKind (alea::link::KindColor) >= 0 && m.link->firstPeerOfKind (alea::link::KindTalea) >= 0
                   && c.link->firstPeerOfKind (alea::link::KindCantus) >= 0, "les 4 instruments se voient");
            setP (c, "root", 9.0f); setP (c, "mode", 5.0f); pump (250);
            check (m.paramReal ("root") > 8.5f && t.paramReal ("root") > 8.5f, "tonalite partagee (la)");
            for (int k : { 2, 7, 0 })
            {
                setP (c, "root", (float) k); pump (300);
                check ((int) m.paramReal ("root") == k && (int) t.paramReal ("root") == k && (int) a.apvts.getRawParameterValue ("root")->load() == k,
                       "Color change la tonique -> " + juce::String (k) + " : Talea " + juce::String ((int) t.paramReal ("root")) + ", Cantus "
                       + juce::String ((int) m.paramReal ("root")) + ", Alea " + juce::String ((int) a.apvts.getRawParameterValue ("root")->load()));
            }
            setP (c, "root", 9.0f); pump (300);
            // modulation du pattern en cours (Key +5) : les autres suivent la tonalite qui sonne
            c.bank.pats[c.currentPattern.load()].keyShift = 5; pump (300);
            check ((int) t.paramReal ("root") == 2 && (int) m.paramReal ("root") == 2 && (int) c.paramReal ("root") == 9,
                   "Key +5 dans Color (la -> re) : Talea " + juce::String ((int) t.paramReal ("root")) + ", Cantus " + juce::String ((int) m.paramReal ("root")));
            setP (t, "root", 4.0f); pump (300);
            check (c.sharedKey() == 4 && (int) c.paramReal ("root") == 11 && (int) m.paramReal ("root") == 4,
                   "tonique changee dans Talea (mi) : Color sonne en mi en gardant sa modulation (tonique " + juce::String ((int) c.paramReal ("root")) + " + 5)");
            c.bank.pats[c.currentPattern.load()].keyShift = 0; pump (300);
            setP (c, "root", 9.0f); pump (300);
            // mode exact : Color en dorien / lydien / ionien -> meme gamme chez les autres, et dans l'autre sens
            {
                struct M { int mode; int shared; const char* name; };
                const M ms[] { { 1, 8, "Dorian" }, { 3, 10, "Lydian" }, { 0, 2, "Ionian -> Major (Ionian)" }, { 14, 13, "Harmonic minor" }, { 5, 3, "Aeolian -> Minor" } };
                for (auto& x : ms)
                {
                    setP (c, "mode", (float) x.mode); pump (350);
                    check ((int) t.paramReal ("scale") == x.shared && (int) m.paramReal ("scale") == x.shared && (int) a.apvts.getRawParameterValue ("scale")->load() == x.shared,
                           juce::String ("mode ") + x.name + " de Color -> Talea / Cantus / Alea : " + alea::scaleNames()[(int) t.paramReal ("scale")] + " / "
                           + alea::scaleNames()[(int) m.paramReal ("scale")] + " / " + alea::scaleNames()[(int) a.apvts.getRawParameterValue ("scale")->load()]);
                }
                // les 38 modes de Color ont un equivalent exact chez les autres
                int exact = 0; juce::String miss;
                for (int md = 0; md < color::theory::numScales(); ++md)
                {
                    setP (c, "mode", (float) md); pump (250);
                    const int sh = (int) m.paramReal ("scale");
                    uint16_t want = color::theory::scale (md).mask, got = 0;
                    for (int x : alea::scaleDegrees (sh)) got |= (uint16_t) (1u << x);
                    if (want == got && (int) t.paramReal ("scale") == sh && c.mode() == md) ++exact; else miss << color::theory::scale (md).name << " ";
                }
                check (exact == color::theory::numScales(), "les " + juce::String (color::theory::numScales()) + " modes de Color arrivent exacts dans Cantus et Talea (" + juce::String (exact) + ") " + miss);
                setP (m, "scale", 9.0f); pump (350);
                check (c.mode() == 2, juce::String ("gamme Phrygian choisie dans Cantus -> Color passe en ") + color::theory::scale (c.mode()).name);
                setP (c, "mode", 5.0f); pump (350);
            }
            c.setProgressionText (0, "Am Fmaj7 C G");
            c.setProgressionText (1, "Dm7 E7 Am Am");
            setP (c, "playmode", 0.0f); setP (c, "complex", 0.2f);
            juce::AudioBuffer<float> bc (2, 512), bt (2, 512), ba (2, 512), bm (2, 512);
            auto runBlocks = [&] (int nBlocks, std::function<void (int)> perBlock)
            {
                for (int b = 0; b < nBlocks; ++b)
                {
                    juce::MidiBuffer m1, m2, m3, m4;
                    c.processBlock (bc, m3); a.processBlock (ba, m1); t.processBlock (bt, m2); m.processBlock (bm, m4);
                    if (perBlock) perBlock (b);
                    if (b % 40 == 0) pump (1);
                }
            };
            runBlocks (10, nullptr);          // Color publie sa grille
            // Talea : ligne sur la grille de Color
            const bool tg = t.generateFromColor (0, 3);
            check (tg && (int) t.paramReal ("chordfollow") >= 1, "Talea : ligne generee sur la grille de Color (Chord follow active)");
            // Cantus : melodie sur la grille
            setP (m, "chordfollow", 1.0f);
            m.generatePattern (0, 0, 9);
            {
                std::vector<CantusAudioProcessor::HarmonySpan> hs; int tb = 0;
                m.readHarmony (hs, tb);
                check (hs.size() == 4 && tb == 16, "Cantus lit la grille de Color (" + juce::String ((int) hs.size()) + " accords, " + juce::String (tb) + " temps)");
            }

            setP (c, "play", 1.0f); pump (250);
            int strongIn = 0, strongN = 0, allIn = 0, allN = 0, bassOnChange = 0, bassChanges = 0;
            const int nb = (int) (8.0 * 2.0 * sr / 512.0);       // 8 mesures a 120
            juce::AudioBuffer<float> mix (2, nb * 512);
            for (int b = 0; b < nb; ++b)
            {
                juce::MidiBuffer m1, m2, m3, m4;
                c.processBlock (bc, m3); a.processBlock (ba, m1); t.processBlock (bt, m2); m.processBlock (bm, m4);
                auto chordOf = [&] (double ppq) { return c.effectiveChord (c.currentPattern.load(), c.patStartShared.load(), ppq + 1.0e-4); };
                const int key = c.keyRootFor (c.currentPattern.load());
                for (const auto mm : m4)
                    if (mm.getMessage().isNoteOn())
                    {
                        const double ppq = m.playPpq.load() - (512 - mm.samplePosition) / sr * 2.0;
                        const auto ch = chordOf (ppq);
                        const int pc = mm.getMessage().getNoteNumber() % 12;
                        const uint16_t rel = (uint16_t) (1u << ((pc - key + 12) % 12));
                        const bool inChord = (color::theory::chordMask (ch) & rel) != 0;
                        const bool inScale = (color::theory::chordScaleMask (ch, c.mode(), c.paramReal ("outgoing")) & rel) != 0;
                        const double frac = ppq - std::floor (ppq);
                        if (frac < 0.08 || frac > 0.92) { ++strongN; if (inChord) ++strongIn; }
                        ++allN; if (inScale) ++allIn;
                    }
                for (const auto mm : m2)
                    if (mm.getMessage().isNoteOn())
                    {
                        const double ppq = t.playPpq.load() - (512 - mm.samplePosition) / sr * 2.0;
                        const auto pos = c.chordPosAt (c.currentPattern.load(), c.patStartShared.load(), ppq + 1.0e-4);
                        if (std::abs (ppq - pos.start) < 0.05)
                        {
                            ++bassChanges;
                            const auto ch = chordOf (ppq);
                            if (mm.getMessage().getNoteNumber() % 12 == (key + color::theory::chordBassPc (ch)) % 12) ++bassOnChange;
                        }
                    }
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        mix.setSample (ch, b * 512 + i, 0.45f * ba.getSample (ch, i) + 0.55f * bt.getSample (ch, i) + 0.5f * bc.getSample (ch, i) + 0.5f * bm.getSample (ch, i));
                if (b % 40 == 0) pump (1);
            }
            writeWav (outDir.getChildFile ("quatuor_alea_talea_color_cantus.wav"), mix, sr);
            check (strongN > 4 && strongIn * 100 >= strongN * 95, "Cantus : notes des temps forts dans l'accord (" + juce::String (strongIn) + "/" + juce::String (strongN) + ")");
            check (allN > 8 && allIn * 100 >= allN * 95, "Cantus : toutes les notes dans la gamme de l'accord (" + juce::String (allIn) + "/" + juce::String (allN) + ")");
            check (bassChanges > 2 && bassOnChange * 100 >= bassChanges * 90, "Talea : basse de l'accord sur chaque changement (" + juce::String (bassOnChange) + "/" + juce::String (bassChanges) + ")");

            // dialogue : avec dialogue = 1, la melodie cede des notes aux attaques de la basse
            // (lignes en doubles croches des deux cotes pour provoquer des collisions)
            {
                auto& M = m.bank.pats[m.currentPattern.load()];
                for (int i = 0; i < 16; ++i) M.steps[i].set (1, (i * 5) % 12, 0, 0, 0, 0, 100, 1, 0, 60);
                M.length = 16; M.rate = 1;
                auto& T = t.bank.pats[t.currentPattern.load()];
                for (int i = 0; i < 16; ++i) T.steps[i].set (1, 0, 0, 0, 0, 0, 100, 1, 0, 50);
                T.length = 16; T.rate = 1;
                setP (m, "density", 0.5f); setP (t, "density", 0.5f);
            }
            m.dialogueDrops = 0;
            setP (m, "dialogue", 1.0f); pump (50);
            runBlocks (400, nullptr);
            const int drops = m.dialogueDrops.load();
            m.dialogueDrops = 0;
            setP (m, "dialogue", 0.0f); pump (50);
            runBlocks (400, nullptr);
            check (drops > 0 && m.dialogueDrops.load() == 0, "Dialogue : " + juce::String (drops) + " notes cedees a la basse (0 sans dialogue)");
            for (float d : { 0.25f, 0.5f })
            {
                m.dialogueDrops = 0; m.notesPlayed = 0;
                setP (m, "dialogue", d); pump (50);
                runBlocks (800, nullptr);
                const int dr = m.dialogueDrops.load(), pl = m.notesPlayed.load();
                check (pl > 20 && dr * 100 <= (dr + pl) * 10, "Dialogue a " + juce::String ((int) (d * 100)) + " % : " + juce::String (dr) + " notes cedees sur "
                       + juce::String (dr + pl) + " (10 % max, jamais sur un temps)");
            }

            // forme partagee : Color en song mode (A2 B2), Talea / Cantus / Alea suivent
            setP (c, "play", 0.0f); pump (300);
            c.setChainText ("A2 B2");
            setP (c, "songmode", 1.0f);
            setP (c, "play", 1.0f); pump (250);
            int tB = 0, mB = 0, aB = 0, cB = 0, samples = 0;
            runBlocks ((int) (8.0 * 2.0 * sr / 512.0), [&] (int)
            {
                const double bar = c.playPpq.load() / 4.0;
                const double inBar = bar - std::floor (bar);
                if (inBar < 0.2 || inBar > 0.8) return;      // loin des changements
                const bool expectB = ((int) std::floor (bar) % 4) >= 2;
                ++samples;
                if ((c.currentPattern.load() == 1) == expectB) ++cB;
                if ((t.currentPattern.load() == 1) == expectB) ++tB;
                if ((m.currentPattern.load() == 1) == expectB) ++mB;
                if ((a.currentPattern.load() == 1) == expectB) ++aB;
            });
            check (samples > 20 && cB == samples && tB == samples && mB == samples && aB == samples,
                   "forme partagee : Talea, Cantus et Alea suivent la chaine A2 B2 de Color (" + juce::String (tB) + "/" + juce::String (mB) + "/"
                   + juce::String (aB) + " sur " + juce::String (samples) + ")");
            setP (c, "play", 0.0f); pump (250);

            std::unique_ptr<juce::AudioProcessorEditor> ed (m.createEditor());
            auto* e = dynamic_cast<CantusAudioProcessorEditor*> (ed.get());
            const char* names[] { "synth", "mod", "fx", "perform", "lab", "link", "aide" };
            for (int tab = 0; tab < 7; ++tab)
            {
                e->getView().showTab (tab);
                pump (150);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                auto f = outDir.getChildFile (juce::String ("cantus_gui_") + names[tab] + ".png"); f.deleteFile();
                juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
            }
            check (true, "captures de l'interface de Cantus (7 onglets)");
        }
        shm.deleteFile();
        setenv ("ALEALINK_FILE", "off", 1);
    }

    std::cout << (failures == 0 ? "TOUS LES TESTS PASSENT" : "ECHECS : " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
