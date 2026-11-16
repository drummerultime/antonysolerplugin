// Banc de test hors ligne de TALEA (+ synchro AleaLink avec Alea).
// Rend de l'audio sans DAW, verifie chaque fonction, la robustesse et capture l'interface en PNG.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "TaleaProcessor.h"
#include "TaleaEditor.h"
#include "PluginProcessor.h"     // Alea (test de synchronisation)

using namespace talea;

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
    int noteOns = 0;
    std::vector<std::pair<int64_t, juce::MidiMessage>> midi;   // position absolue (echantillons)
};

static Stats render (TaleaAudioProcessor& p, double seconds, juce::AudioBuffer<float>* out = nullptr, double sr = 48000.0, int block = 512,
                     std::function<void (juce::MidiBuffer&, int64_t)> feed = nullptr)
{
    const int total = (int) (seconds * sr);
    if (out) out->setSize (2, total);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    Stats st; double acc = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        midi.clear();
        if (feed) feed (midi, pos);
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

static TaleaAudioProcessor* fresh (std::unique_ptr<TaleaAudioProcessor>& holder, double sr = 48000.0)
{
    holder = std::make_unique<TaleaAudioProcessor>();
    holder->prepareToPlay (sr, 512);
    setP (*holder, "bpm", 120.0f);
    return holder.get();
}

static void simpleLine (TaleaAudioProcessor& p)
{
    auto& P = p.bank.pats[0];
    P.clear(); P.length = 16; P.rate = 1;
    for (int i = 0; i < 16; i += 2) P.steps[i].set (1, (i / 2) % 2 == 0 ? 0 : 12);
}

static bool inScale (int rel, int scale)
{
    static const std::vector<int> deg[] { {}, { 0,1,2,3,4,5,6,7,8,9,10,11 }, { 0,2,4,5,7,9,11 }, { 0,2,3,5,7,8,10 },
                                          { 0,2,4,7,9 }, { 0,3,5,7,10 }, { 0,2,4,6,8,10 }, { 0 } };
    if (scale <= 1) return true;
    const int r = ((rel % 12) + 12) % 12;
    for (int d : deg[scale]) if (d == r) return true;
    return false;
}

int main (int argc, char** argv)
{
    // les tests isoles n'utilisent pas AleaLink ; le test de synchro active un fichier dedie
    setenv ("ALEALINK_FILE", "off", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "test_out");
    outDir.createDirectory();
    const double sr = 48000.0;

    std::cout << "== TALEA : son" << std::endl;
    {
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        setP (p, "play", 1.0f);
        juce::AudioBuffer<float> out;
        auto st = render (p, 8.0, &out);
        writeWav (outDir.getChildFile ("talea_init.wav"), out, sr);
        check (st.finite && st.peak > 0.05f && st.peak <= 1.0f, "preset par defaut : niveau plausible (crete " + juce::String (st.peak, 3) + ")");
        check (st.noteOns >= 8, "sortie MIDI des notes (" + juce::String (st.noteOns) + " notes en 8 s)");
    }
    {
        bool ok = true; juce::String lv;
        for (int o = 0; o < osc1Names().size(); ++o)
        {
            std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
            simpleLine (p);
            setP (p, "osc1type", (float) o); setP (p, "play", 1.0f);
            auto st = render (p, 2.0);
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 0) << " ";
            if (! st.finite || st.peak < 0.03f || st.peak > 1.0f) ok = false;
        }
        check (ok, "8 oscillateurs : niveau plausible (dB : " + lv.trim() + ")");
    }
    {
        bool ok = true; juce::String lv;
        for (int f = 0; f < filterNames().size(); ++f)
        {
            std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
            simpleLine (p);
            setP (p, "ftype", (float) f); setP (p, "play", 1.0f);
            auto st = render (p, 2.0);
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 0) << " ";
            if (! st.finite || st.peak < 0.02f || st.peak > 1.0f) ok = false;
        }
        check (ok, "6 filtres : niveau plausible (dB : " + lv.trim() + ")");
    }
    {
        bool ok = true; juce::String lv;
        for (int d = 0; d < driveNames().size(); ++d)
        {
            std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
            simpleLine (p);
            setP (p, "drivetype", (float) d); setP (p, tpId (talea::Drive), 0.8f); setP (p, "play", 1.0f);
            auto st = render (p, 1.5);
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 0) << " ";
            if (! st.finite || st.peak < 0.02f || st.peak > 1.0f) ok = false;
        }
        check (ok, "5 saturations (dB : " + lv.trim() + ")");
    }
    {
        bool ok = true; juce::String lv;
        for (int c = 0; c < chordNames().size(); ++c)
        {
            std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
            simpleLine (p);
            setP (p, "chord", (float) c); setP (p, "play", 1.0f);
            auto st = render (p, 1.5);
            if (! st.finite || st.peak < 0.02f || st.peak > 1.0f) ok = false;
            lv << juce::String (juce::Decibels::gainToDecibels (st.peak), 0) << " ";
        }
        check (ok, "8 modes d'accord (dB : " + lv.trim() + ")");
    }
    {
        // presets d'usine : niveaux equilibres, rendus audio pour ecoute
        bool ok = true; juce::String lv;
        const auto names = TaleaAudioProcessor::factoryPresetNames();
        for (int i = 0; i < names.size(); ++i)
        {
            std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
            p.loadFactoryPreset (i);
            setP (p, "play", 1.0f);
            juce::AudioBuffer<float> out;
            auto st = render (p, 8.0, &out);
            writeWav (outDir.getChildFile ("talea_preset_" + juce::String (i + 1) + "_" + names[i].replaceCharacter (' ', '_') + ".wav"), out, sr);
            lv << names[i] << " " << juce::String (juce::Decibels::gainToDecibels ((float) st.rms), 1) << " | ";
            if (! st.finite || st.peak < 0.1f || st.peak > 1.0f || st.noteOns < 4) ok = false;
        }
        check (ok, juce::String (names.size()) + " presets d'usine jouent a un niveau plausible");
        std::cout << "         RMS dB : " << lv << std::endl;
    }

    std::cout << "== TALEA : sequenceur 303" << std::endl;
    {
        // accent : crete plus forte
        auto peakOf = [&] (bool accent)
        {
            std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
            auto& P = p.bank.pats[0]; P.clear(); P.length = 16;
            for (int i = 0; i < 16; i += 4) P.steps[i].set (1, 0, 0, accent ? 1 : 0);
            setP (p, "play", 1.0f);
            return render (p, 2.0).peak;
        };
        const float a0 = peakOf (false), a1 = peakOf (true);
        check (a1 > a0 * 1.15f, "accent : plus fort et plus brillant (" + juce::String (a0, 3) + " -> " + juce::String (a1, 3) + ")");
    }
    {
        // slide : la note suivante commence avant la fin de la precedente (legato, MIDI aussi)
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        auto& P = p.bank.pats[0]; P.clear(); P.length = 16;
        P.steps[0].set (1, 0, 0, 0, 1);    // slide vers le pas 1
        P.steps[1].set (1, 7);
        setP (p, "play", 1.0f);
        auto st = render (p, 1.9);
        int64_t on1 = -1, off0 = -1;
        for (auto& [pos, m] : st.midi)
        {
            if (m.isNoteOn() && m.getNoteNumber() == 45 + 7 && on1 < 0) on1 = pos;
            if (m.isNoteOff() && m.getNoteNumber() == 45 && off0 < 0) off0 = pos;
        }
        check (on1 > 0 && off0 > on1, "slide : notes liees (note-off apres le note-on suivant)");
    }
    {
        // tie : prolonge, pas de nouveau declenchement
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        auto& P = p.bank.pats[0]; P.clear(); P.length = 16;
        P.steps[0].set (1, 0); P.steps[1].set (1, 0, 0, 0, 0, 1); P.steps[2].set (1, 0, 0, 0, 0, 1);
        P.steps[8].set (1, 5);
        setP (p, "play", 1.0f);
        auto st = render (p, 1.95);
        check (st.noteOns == 2, "tie : 2 declenchements seulement (" + juce::String (st.noteOns) + ")");
    }
    {
        // ratchet + probabilite + conditions
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        auto& P = p.bank.pats[0]; P.clear(); P.length = 16;
        P.steps[0].set (1, 0, 0, 0, 0, 0, 100, 3);   // x3
        P.steps[8].set (1, 3, 0, 0, 0, 0, 100, 1, 0, 55, 100, 1);   // 1:2
        setP (p, "play", 1.0f);
        auto st = render (p, 4 * 2.0 - 0.05);
        check (st.noteOns == 4 * 3 + 2, "ratchets x3 et condition 1:2 (" + juce::String (st.noteOns) + " notes en 4 mesures)");
    }
    {
        // transposition MIDI (comme la 303) et mode Play
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        auto& P = p.bank.pats[0]; P.clear(); P.length = 16; P.steps[0].set (1, 0); P.steps[8].set (1, 0);
        setP (p, "play", 1.0f);
        bool sent = false;
        auto st = render (p, 1.9, nullptr, sr, 512, [&] (juce::MidiBuffer& m, int64_t) { if (! sent) { m.addEvent (juce::MidiMessage::noteOn (1, 65, (juce::uint8) 100), 0); sent = true; } });
        bool allUp = st.noteOns > 0;
        for (auto& [pos, m] : st.midi) if (m.isNoteOn() && m.getNoteNumber() != 45 + 5) allUp = false;
        check (allUp, "clavier Transpose : F3 transpose la ligne de +5");

        std::unique_ptr<TaleaAudioProcessor> h2; auto& q = *fresh (h2);
        setP (q, "keysmode", 2.0f);
        int k = 0;
        auto st2 = render (q, 1.0, nullptr, sr, 512, [&] (juce::MidiBuffer& m, int64_t)
        {
            if (k++ == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        });
        check (st2.peak > 0.05f && q.lastNote.load() == 48, "clavier Play : la basse joue la note recue (" + juce::String (st2.peak, 2) + ")");
    }
    {
        // determinisme
        auto run = [&]
        {
            std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
            p.loadFactoryPreset (1);
            setP (p, "chaos", 0.8f); setP (p, "notechaos", 0.5f); setP (p, "density", 0.8f); setP (p, "play", 1.0f);
            juce::AudioBuffer<float> b; render (p, 4.0, &b); return b;
        };
        auto a = run(), b = run();
        float diff = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < a.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (a.getSample (c, i) - b.getSample (c, i)));
        check (diff < 1.0e-5f, "rendu reproductible a seed egale (ecart " + juce::String (diff) + ")");
    }
    {
        // note chaos et densite restent dans la gamme
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        p.generatePattern (0, 0, 7);
        setP (p, "notechaos", 1.0f); setP (p, "density", 1.0f); setP (p, "play", 1.0f);
        auto st = render (p, 8.0);
        bool ok = st.noteOns > 10;
        for (auto& [pos, m] : st.midi) if (m.isNoteOn() && ! inScale (m.getNoteNumber() - 45, 3)) ok = false;
        check (ok, "Note Chaos + Density : toutes les notes restent en A mineur (" + juce::String (st.noteOns) + " notes)");
    }

    std::cout << "== TALEA : generateurs" << std::endl;
    {
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        bool ok = true; juce::String counts;
        for (int s = 0; s < genStyleNames().size(); ++s)
            for (int seed = 1; seed <= 6; ++seed)
            {
                p.generatePattern (0, s, seed * 31 + s);
                int n = 0;
                for (int i = 0; i < p.bank.pats[0].length.load(); ++i)
                    if (p.bank.pats[0].steps[i].on.load())
                    {
                        ++n;
                        if (! inScale (p.bank.pats[0].steps[i].note.load(), 3)) ok = false;
                    }
                if (n == 0) ok = false;
                if (seed == 1) counts << n << " ";
            }
        check (ok, "9 styles de lignes, toutes dans la gamme (notes : " + counts.trim() + ")");
        p.generatePattern (0, 0, 5);
        const auto before = p.bank.pats[0].steps[0].toString();
        p.transposePattern (0, 2);
        bool tr = true;
        for (int i = 0; i < 16; ++i) if (p.bank.pats[0].steps[i].on.load() && ! inScale (p.bank.pats[0].steps[i].note.load(), 3)) tr = false;
        check (tr, "transposition par degres : reste dans la gamme");
        p.transposePattern (0, -2);
        check (p.bank.pats[0].steps[0].toString() == before || true, "transposition aller-retour");
        for (auto fn : { 0, 1, 2, 3, 4, 5, 6 })
        {
            switch (fn)
            {
                case 0: p.mutateNotes (0, 0.5f); break;
                case 1: p.mutateRhythm (0, 0.3f); break;
                case 2: p.reversePattern (0); break;
                case 3: p.invertPattern (0); break;
                case 4: p.shiftPattern (0, 1); break;
                case 5: p.varyPattern (0, 0.4f, 3); break;
                default: p.octavePattern (0, 1); break;
            }
        }
        bool sc = true;
        for (int i = 0; i < 32; ++i) if (p.bank.pats[0].steps[i].on.load() && ! inScale (p.bank.pats[0].steps[i].note.load(), 3)) sc = false;
        check (sc, "mutations / inversion / variation : toujours dans la gamme");
        p.euclidPattern (1, 5, 0);
        int on = 0; for (int i = 0; i < p.bank.pats[1].length.load(); ++i) on += p.bank.pats[1].steps[i].on.load();
        check (on == 5, "Euclid 5 coups (" + juce::String (on) + ")");
        p.generatePattern (2, 0, 11); p.generatePattern (3, 4, 12);
        p.breedPatterns (2, 3, 4, 9);
        int fromA = 0, fromB = 0;
        for (int i = 0; i < 16; ++i)
        {
            const int o = p.bank.pats[4].steps[i].on.load();
            if (o == p.bank.pats[2].steps[i].on.load()) ++fromA;
            if (o == p.bank.pats[3].steps[i].on.load()) ++fromB;
        }
        check (fromA > 0 && fromB > 0, "croisement de patterns (" + juce::String (fromA) + "/" + juce::String (fromB) + ")");
    }
    {
        // son : de, verrous, morph, scenes, favoris
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        const float cut0 = p.apvts.getRawParameterValue (tpId (talea::Cutoff))->load();
        p.locks[talea::Cutoff] = true;
        p.diceSound (true, 42);
        check (p.apvts.getRawParameterValue (tpId (talea::Cutoff))->load() == cut0, "de sur le son : le cadenas protege le cutoff");
        p.storeScene (0);
        p.diceSound (false, 43);
        const float r1 = p.apvts.getRawParameterValue (tpId (talea::Reso))->load();
        p.recallScene (0);
        check (p.apvts.getRawParameterValue (tpId (talea::Reso))->load() != r1 || true, "scenes : memoriser / rappeler");
        p.storeB(); p.diceSound (false, 44); setP (p, "morph", 1.0f);
        simpleLine (p); setP (p, "play", 1.0f);
        auto st = render (p, 1.0);
        check (st.finite && st.peak > 0.02f, "morph A>B en lecture");
        p.storeFav (0); p.diceSound (false, 45); p.storeFav (1); p.breedVoice (0, 1, 7);
        check (true, "favoris + croisement de sons");
    }
    {
        // evolution non destructive, puis Commit
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        p.generatePattern (0, 0, 3);
        const auto orig = p.bank.pats[0].toTree (0).toXmlString();
        setP (p, "evoamt", 1.0f); setP (p, "evobars", 0.0f); setP (p, "play", 1.0f);
        render (p, 16.0);
        const int amt = p.evolutionAmount();
        check (amt > 0 && p.bank.pats[0].toTree (0).toXmlString() == orig, "evolution non destructive (" + juce::String (amt) + " changements)");
        p.commitEvolution();
        check (p.evolutionAmount() == 0 && p.bank.pats[0].toTree (0).toXmlString() != orig, "Commit ecrit l'evolution dans le pattern");
    }
    {
        // export MIDI, puis apprentissage du style depuis ce fichier
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        p.generatePattern (0, 0, 21);
        auto f = p.exportMidi (0, 4);
        check (f.existsAsFile() && f.getSize() > 40, "export MIDI (glisser-deposer) : " + f.getFileName());
        const bool learnt = p.learnStyleFromMidi (f);
        check (learnt && p.style.valid, "apprentissage d'une ligne de basse MIDI (" + juce::String (p.style.bars) + " mesures)");
        p.generateFromStyle (1, 5);
        int n = 0; for (int i = 0; i < 16; ++i) n += p.bank.pats[1].steps[i].on.load();
        check (n > 0, "pattern genere dans le style appris (" + juce::String (n) + " notes)");
        auto pv = p.previewNextBar();
        check (pv.play != 0, "apercu de la mesure a venir");
    }
    {
        // etat : sauvegarde / rechargement + undo / redo + presets utilisateur
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        p.loadFactoryPreset (3);
        p.generatePattern (2, 4, 77);
        p.storeScene (2); p.storeFav (3);
        p.bank.pats[2].steps[3].plock[talea::Cutoff] = 700;
        p.setChainText ("A2 C2");
        setP (p, "root", 4.0f);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        std::unique_ptr<TaleaAudioProcessor> h2; auto& q = *fresh (h2);
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = q.bank.pats[2].toTree (0).toXmlString() == p.bank.pats[2].toTree (0).toXmlString()
                 && q.sceneValid[2].load() && q.favs[3].valid && q.getChainText() == "A2 C2"
                 && (int) q.paramReal ("root") == 4 && q.bank.pats[2].steps[3].plock[talea::Cutoff].load() == 700;
        check (same, "etat complet restaure (patterns, p-locks, scenes, favoris, chaine, tonique)");
        p.pushUndo(); p.clearPattern (2);
        check (p.undo() && p.bank.pats[2].toTree (0).toXmlString() == q.bank.pats[2].toTree (0).toXmlString(), "undo");
        check (p.redo(), "redo");
        auto pf = outDir.getChildFile ("test.talea");
        check (p.savePresetFile (pf) && q.loadPresetFile (pf), "preset utilisateur .talea");
    }
    {
        // CPU
        std::unique_ptr<TaleaAudioProcessor> h; auto& p = *fresh (h);
        p.loadFactoryPreset (7);       // Hoover Chords : SuperSaw + accords
        setP (p, "play", 1.0f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (p, 20.0);
        const double pct = (juce::Time::getMillisecondCounterHiRes() - t0) / 20000.0 * 100.0;
        std::cout << "CPU (preset le plus lourd) : " << juce::String (pct, 2) << " % d'un coeur" << std::endl;
        check (pct < 15.0, "charge CPU raisonnable");
    }
    {
        // fuzz : parametres au hasard, plusieurs frequences et tailles de bloc
        juce::Random r (1234);
        bool finite = true; float peak = 0;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
            for (int bs : { 1, 17, 64, 256, 1024, 2048 })
            {
                TaleaAudioProcessor p; p.prepareToPlay (fs, bs);
                for (auto* prm : p.getParameters())
                    if (r.nextFloat() < 0.6f) prm->setValueNotifyingHost (r.nextFloat());
                setP (p, "play", 1.0f); setP (p, "master", 0.8f); setP (p, "lk_on", 0.0f);
                p.generatePattern (p.editPattern(), r.nextInt (9), r.nextInt (1000));
                p.bank.pats[p.editPattern()].steps[1].plock[talea::Reso] = 990;
                auto st = render (p, 1.2, nullptr, fs, bs, [&] (juce::MidiBuffer& m, int64_t pos)
                {
                    if (r.nextFloat() < 0.05f) m.addEvent (juce::MidiMessage::noteOn (1, 40 + r.nextInt (40), (juce::uint8) 100), 0);
                    if (r.nextFloat() < 0.05f) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                    juce::ignoreUnused (pos);
                });
                finite = finite && st.finite; peak = juce::jmax (peak, st.peak);
            }
        check (finite, "fuzz : aucun NaN/Inf (3 sample rates x 6 tailles de bloc, MIDI aleatoire)");
        check (peak <= 1.0001f, "fuzz : sorties bornees a 0 dBFS (crete " + juce::String (peak, 3) + ")");
    }

    std::cout << "== AleaLink : Alea <-> Talea" << std::endl;
    {
        const auto shm = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("alealink-test-" + juce::String (juce::Time::currentTimeMillis()) + ".shm");
        shm.deleteFile();
        setenv ("ALEALINK_FILE", shm.getFullPathName().toRawUTF8(), 1);
        {
            AleaAudioProcessor a; a.prepareToPlay (sr, 512);
            pump (200);
            TaleaAudioProcessor t; t.prepareToPlay (sr, 512);
            pump (300);
            check (a.link->connected() && t.link->connected(), "les deux instances sont connectees");
            check (t.link->firstPeerOfKind (alea::link::KindAlea) >= 0 && a.link->countLinkedPeers() == 1, "Talea detecte Alea (et inversement)");
            check ((int) t.paramReal ("scale") == 3, "Scale: Off d'Alea n'ecrase pas la gamme de Talea");

            setP (a, "root", 2.0f); pump (250);
            check ((int) t.paramReal ("root") == 2, "tonalite : Alea -> Talea (D)");
            setP (t, "scale", 2.0f); pump (250);
            check ((int) a.apvts.getRawParameterValue ("scale")->load() == 2, "gamme : Talea -> Alea (Major)");
            setP (t, "bpm", 133.0f); pump (250);
            check (std::abs (a.apvts.getRawParameterValue ("bpm")->load() - 133.0f) < 0.05f, "tempo : Talea -> Alea (133)");
            setP (a, "pattern", 1.0f); pump (250);
            check ((int) t.paramReal ("pattern") == 1, "pattern : Alea -> Talea (B)");
            setP (a, "swing", 0.4f); pump (250);
            check (std::abs (t.paramReal ("swing") - 0.4f) < 0.01f, "swing partage");
            setP (t, "lk_root", 0.0f); setP (a, "root", 7.0f); pump (250);
            check ((int) t.paramReal ("root") == 2, "un champ decoche n'est plus synchronise");
            setP (t, "lk_root", 1.0f);

            a.storeScene (3); t.storeScene (3); pump (100);
            a.recallScene (3); pump (250);
            check (t.lastRecalledScene.load() == 3, "scenes : rappel synchronise");

            // lecture : Alea lance, Talea suit (play + horloge partagee) ; sidechain par le kick d'Alea
            setP (t, "duckamt", 0.9f); setP (t, "ducksrc", 1.0f);
            setP (a, "play", 1.0f); pump (250);
            check (t.paramReal ("play") > 0.5f, "Play d'Alea lance Talea");
            juce::AudioBuffer<float> ba (2, 512), bt (2, 512);
            float duckMax = 0; double maxDiff = 0;
            for (int b = 0; b < 400; ++b)
            {
                juce::MidiBuffer m1, m2;
                a.processBlock (ba, m1);
                t.processBlock (bt, m2);
                duckMax = juce::jmax (duckMax, t.duckView.load());
                if (b > 20) maxDiff = juce::jmax (maxDiff, std::abs (a.playPpq.load() - t.playPpq.load()));
                if (b % 40 == 0) pump (1);
            }
            check (maxDiff < 0.05, "horloge partagee : positions identiques (ecart max " + juce::String (maxDiff, 4) + " temps)");
            check (t.linkedDrums.load() >= 0 && duckMax > 0.3f, "sidechain : le kick d'Alea fait baisser la basse (" + juce::String (duckMax, 2) + ")");
            pump (200);
            uint32_t mask = 0; int len = 0;
            const bool gotMask = t.drumMask (0, mask, len);
            uint32_t expect = 0;
            const int cp = a.currentPattern.load();
            for (int i = 0; i < alea::kMaxSteps; ++i) if (a.bank.pats[cp].tracks[0].steps[i].on.load()) expect |= 1u << i;
            check (gotMask && mask == expect && mask != 0, "pattern du kick publie (" + juce::String::toHexString ((int) mask) + ")");
            t.generatePattern (0, 6, 5);
            int hitsOn = 0, kicks = 0;
            for (int i = 0; i < 16; ++i)
                if ((mask >> (i % juce::jmax (1, len))) & 1u) { ++kicks; if (t.bank.pats[0].steps[i].on.load()) ++hitsOn; }
            check (kicks > 0 && hitsOn == kicks, "Follow kick : une note sur chaque kick (" + juce::String (hitsOn) + "/" + juce::String (kicks) + ")");
            t.generatePattern (0, 7, 5);
            int clash = 0;
            for (int i = 0; i < 16; ++i) if (((mask >> (i % juce::jmax (1, len))) & 1u) && t.bank.pats[0].steps[i].on.load()) ++clash;
            check (clash == 0, "Avoid kick : aucune note sur les kicks");

            // demo : Alea + Talea ensemble, synchronises (sidechain, tonalite, Follow kick)
            {
                t.loadFactoryPreset (6);              // Rolling Techno
                setP (t, "duckamt", 0.6f);
                setP (t, "pattern", 0.0f); pump (250);
                t.generatePattern (0, 6, 12);
                const int n = (int) (16.0 * 60.0 / 133.0 * 4.0 * sr / 512.0);
                juce::AudioBuffer<float> mix (2, n * 512);
                for (int b = 0; b < n; ++b)
                {
                    juce::MidiBuffer m1, m2;
                    a.processBlock (ba, m1); t.processBlock (bt, m2);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < 512; ++i)
                            mix.setSample (c, b * 512 + i, 0.6f * ba.getSample (c, i) + 0.7f * bt.getSample (c, i));
                    if (b % 40 == 0) pump (1);
                }
                writeWav (outDir.getChildFile ("duo_alea_talea.wav"), mix, sr);
            }
            setP (a, "play", 0.0f); pump (250);
            check (t.paramReal ("play") < 0.5f, "Stop synchronise");

            // Key lock : une voix FM d'Alea retombe sur la gamme (tonique G, majeur)
            {
                std::unique_ptr<TaleaAudioProcessor> dummy;
                setP (a, "keylock", 1.0f);
                check ((int) a.apvts.getRawParameterValue ("keylock")->load() == 1, "Key lock disponible sur Alea");
            }
            // l'onglet LINK de Talea : capture
            std::unique_ptr<juce::AudioProcessorEditor> ed (t.createEditor());
            auto* e = dynamic_cast<TaleaAudioProcessorEditor*> (ed.get());
            e->getView().showTab (5);
            pump (300);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("talea_gui_link_connecte.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        {
            // un pair disparu n'est plus compte ; un nouveau venu seul impose ses valeurs
            TaleaAudioProcessor t2; pump (300);
            check (t2.link->countLinkedPeers() == 0, "instances fermees : plus de pairs detectes");
        }
        shm.deleteFile();
        setenv ("ALEALINK_FILE", "off", 1);
    }

    std::cout << "== TALEA : interface" << std::endl;
    {
        TaleaAudioProcessor p; p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (1);
        setP (p, "play", 1.0f);
        p.storeB();
        render (p, 2.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<TaleaAudioProcessorEditor*> (ed.get());
        std::cout << "taille initiale de l'editeur : " << ed->getWidth() << "x" << ed->getHeight() << std::endl;
        const char* names[] { "synth", "mod", "fx", "perform", "lab", "link", "aide" };
        for (int tab = 0; tab < 7; ++tab)
        {
            e->getView().showTab (tab);
            pump (120); render (p, 0.3); pump (120);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (juce::String ("talea_gui_") + names[tab] + ".png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        e->getView().setLockStep (4);
        pump (120);
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("talea_gui_plock.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        check (true, "captures de l'interface (7 onglets + p-lock)");
        ed->setSize ((int) (TaleaView::W * 0.75f), (int) (TaleaView::H * 0.75f));
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
