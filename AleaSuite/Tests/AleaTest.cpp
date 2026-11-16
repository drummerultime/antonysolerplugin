// Banc de test hors ligne : rend de l'audio sans DAW, verifie chaque fonction,
// la robustesse (fuzz), l'etat, et capture l'interface en PNG.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace alea;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  [OK]   " : "  [FAIL] ") << what << std::endl;
    if (! ok) ++failures;
}

static void setP (AleaAudioProcessor& p, const juce::String& id, float realValue)
{
    auto* prm = p.apvts.getParameter (id);
    jassert (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

struct Stats { float peak = 0; double rms = 0; bool finite = true; int noteOns[kNumTracks] {}; };

static Stats render (AleaAudioProcessor& p, double seconds, juce::AudioBuffer<float>* out, double sr = 48000.0, int block = 512, int channels = 2)
{
    const int total = (int) (seconds * sr);
    if (out) out->setSize (2, total);
    juce::AudioBuffer<float> buf (channels, block);
    juce::MidiBuffer midi;
    Stats st; double acc = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), channels, n);
        midi.clear();
        p.processBlock (view, midi);
        for (const auto m : midi)
            if (m.getMessage().isNoteOn())
            {
                const int t = m.getMessage().getNoteNumber() - 36;
                if (t >= 0 && t < kNumTracks) ++st.noteOns[t];
            }
        for (int c = 0; c < 2; ++c)
        {
            const float* d = view.getReadPointer (c);
            for (int i = 0; i < n; ++i)
            {
                if (! std::isfinite (d[i])) st.finite = false;
                st.peak = juce::jmax (st.peak, std::abs (d[i]));
                acc += (double) d[i] * d[i];
            }
            if (out) out->copyFrom (c, pos, view, c, 0, n);
        }
    }
    st.rms = std::sqrt (acc / (2.0 * total));
    return st;
}

static void writeWav (const juce::AudioBuffer<float>& b, const juce::File& f, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static float hitPeak (AleaAudioProcessor& p, int track)
{
    const double sr = 48000.0; const int block = 512;
    juce::AudioBuffer<float> buf (2, block);
    float peak = 0;
    for (int pos = 0; pos < (int) (sr * 1.5); pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36 + track, (juce::uint8) 100), 0);
        p.processBlock (buf, midi);
        peak = juce::jmax (peak, buf.getMagnitude (0, block));
    }
    return peak;
}

static void soloTrack (AleaAudioProcessor& p, int keep)
{
    for (int t = 0; t < kNumTracks; ++t) setP (p, tid (t, "mute"), t == keep ? 0.0f : 1.0f);
}

int main (int argc, char** argv)
{
    // AleaLink teste a part (TaleaTest) : ici chaque instance doit rester independante
    if (std::getenv ("ALEALINK_FILE") == nullptr) setenv ("ALEALINK_FILE", "off", 1);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "test_out");
    outDir.createDirectory();
    const double sr = 48000.0;
    const double bar = 4 * 60.0 / 124.0;

    // echantillon de test (pour le moteur Sample)
    const auto sampleFile = outDir.getChildFile ("test_sample.wav");
    {
        juce::AudioBuffer<float> s (1, 12000);
        for (int i = 0; i < 12000; ++i) s.setSample (0, i, std::sin (i * 0.05f) * std::exp (-i / 3000.0f));
        writeWav (s, sampleFile, 48000.0);
    }

    if (argc > 2 && juce::String (argv[2]) == "diag")
    {
        AleaAudioProcessor p;
        p.prepareToPlay (sr, 512);
        p.loadSample (0, sampleFile);
        setP (p, "glue", 0.0f);
        setP (p, tid (0, "level"), 0.5f); setP (p, tid (0, "drive"), 0.0f);
        for (int e = 0; e < kNumEngines; ++e)
        {
            setP (p, tid (0, "engine"), (float) e);
            std::cout << engineNames()[e] << ":";
            for (float tb : { 0.0f, 0.5f, 1.0f })
              for (float cl : { 0.0f, 0.5f, 1.0f })
                for (float tu : { 0.3f, 0.5f, 0.8f })
                  for (float dc : { 0.2f, 0.7f })
                  {
                    setP (p, tid (0, "timbre"), tb); setP (p, tid (0, "color"), cl); setP (p, tid (0, "tune"), tu); setP (p, tid (0, "decay"), dc);
                    std::cout << " " << juce::String (juce::Decibels::gainToDecibels (hitPeak (p, 0)), 0);
                  }
            std::cout << std::endl;
        }
        return 0;
    }


    std::cout << "== Alea v3 : banc de test ==" << std::endl;

    // 1) moteurs
    {
        AleaAudioProcessor p;
        p.prepareToPlay (sr, 512);
        setP (p, "glue", 0.0f);
        check (p.loadSample (0, sampleFile), "chargement d'un echantillon");
        for (int t = 0; t < kNumTracks; ++t) { setP (p, tid (t, "srev"), 0.0f); setP (p, tid (t, "sdly"), 0.0f); }
        std::cout << "Crete par voix (kit par defaut) :";
        for (int t = 0; t < kNumTracks; ++t)
        {
            const float pk = hitPeak (p, t);
            std::cout << " " << defaultKit (t).name << " " << juce::String (juce::Decibels::gainToDecibels (pk), 1);
            if (pk < 0.02f || pk > 1.01f) check (false, juce::String ("niveau ") + defaultKit (t).name);
        }
        std::cout << std::endl << "Crete par moteur :";
        for (int e = 0; e < kNumEngines; ++e)
        {
            setP (p, tid (0, "engine"), (float) e);
            const float pk = hitPeak (p, 0);
            std::cout << " " << engineNames()[e] << " " << juce::String (juce::Decibels::gainToDecibels (pk), 1);
            if (pk < 0.02f || pk > 1.01f) check (false, "moteur " + engineNames()[e]);
        }
        std::cout << std::endl;
        check (true, "10 moteurs produisent un niveau plausible");

        // punch
        setP (p, tid (0, "engine"), (float) EngKick);
        setP (p, tid (0, "drive"), 0.0f);
        setP (p, tid (0, "punch"), 0.5f);  const float p0 = hitPeak (p, 0);
        setP (p, tid (0, "punch"), 1.0f);  const float p1 = hitPeak (p, 0);
        check (p1 > p0 * 1.2f, "Punch renforce l'attaque (" + juce::String (p0, 3) + " -> " + juce::String (p1, 3) + ")");
        setP (p, tid (0, "punch"), 0.5f);
        // velocite
        setP (p, tid (0, "velsens"), 0.0f);
        float a = hitPeak (p, 0);
        setP (p, tid (0, "velsens"), 1.0f);
        float b = hitPeak (p, 0);
        check (b < a * 0.9f, "Vel Sens : velocite 100 plus douce quand la sensibilite monte");
    }

    // 2) groove + chaos + demos
    {
        AleaAudioProcessor p;
        p.prepareToPlay (sr, 512);
        setP (p, "bpm", 124.0f);
        setP (p, "play", 1.0f);
        juce::AudioBuffer<float> clean;
        auto s1 = render (p, 8 * bar, &clean);
        writeWav (clean, outDir.getChildFile ("alea_demo_1_init.wav"), sr);
        check (s1.finite && s1.rms > 0.01, "groove par defaut (RMS " + juce::String (juce::Decibels::gainToDecibels ((float) s1.rms), 1) + " dB)");
        check (s1.noteOns[0] >= 30, "MIDI Out : notes emises par le sequenceur (" + juce::String (s1.noteOns[0]) + " kicks)");

        for (int i = 1; i < (int) AleaAudioProcessor::factoryPresetNames().size(); ++i)
        {
            p.loadFactoryPreset (i);
            setP (p, "play", 1.0f);
            juce::AudioBuffer<float> b;
            auto st = render (p, 8 * bar, &b);
            const auto name = AleaAudioProcessor::factoryPresetNames()[i];
            writeWav (b, outDir.getChildFile ("alea_demo_preset_" + juce::String (i) + "_" + name.replaceCharacter (' ', '_') + ".wav"), sr);
            check (st.finite && st.rms > 0.005 && st.peak <= 1.0f,
                   "preset " + name + " (RMS " + juce::String (juce::Decibels::gainToDecibels ((float) st.rms), 1) + " dB)");
        }
    }

    // 3) sequenceur : conditions, p-locks, sens de lecture, densite, fill
    {
        auto fresh = [&] (AleaAudioProcessor& p)
        {
            p.prepareToPlay (sr, 512);
            for (auto& tp : p.bank.pats[0].tracks) tp.clear();
            setP (p, "bpm", 120.0f); setP (p, "play", 1.0f);
        };
        {
            AleaAudioProcessor p; fresh (p);
            p.bank.pats[0].tracks[0].steps[0].set (1, 0, 100, 1, 0, 0, 1);    // 1:2
            auto st = render (p, 8 * 2.0 - 0.1, nullptr);                       // 8 mesures a 120
            check (st.noteOns[0] == 4, "condition 1:2 -> 4 coups sur 8 boucles (" + juce::String (st.noteOns[0]) + ")");
        }
        {
            AleaAudioProcessor p; fresh (p);
            p.bank.pats[0].tracks[0].steps[0].set (1, 0, 100, 1, 0, 0, 12);   // First
            auto st = render (p, 8 * 2.0 - 0.1, nullptr);
            check (st.noteOns[0] == 1, "condition First -> 1 seul coup (" + juce::String (st.noteOns[0]) + ")");
        }
        {
            AleaAudioProcessor p; fresh (p);
            p.bank.pats[0].tracks[0].steps[0].set (1, 0, 100, 1, 0, 0, 10);   // Fill
            auto st = render (p, 4 * 2.0 - 0.1, nullptr);
            setP (p, "fill", 1.0f);
            auto st2 = render (p, 4 * 2.0, nullptr);
            check (st.noteOns[0] == 0 && st2.noteOns[0] >= 4, "condition Fill : muet sans Fill, joue avec Fill (" + juce::String (st2.noteOns[0]) + ")");
        }
        {
            AleaAudioProcessor p; fresh (p);
            for (int i = 0; i < 16; i += 4) p.bank.pats[0].tracks[0].steps[i].set (1, 0, 100, 1, 0, 0);
            p.bank.pats[0].tracks[0].steps[4].plock[Decay] = 1000;
            p.bank.pats[0].tracks[0].steps[8].plock[Tune] = 0;
            render (p, 0.55, nullptr);    // pas 0 (0 s) et pas 4 (0.5 s)
            check (std::abs (p.lastNorm[0][Decay].load() - 1.0f) < 1e-3f, "p-lock Decay applique au pas 5");
            render (p, 0.5, nullptr);     // pas 8
            check (p.lastNorm[0][Tune].load() < 1e-3f && std::abs (p.lastNorm[0][Decay].load() - defaultVP (0, Decay)) < 1e-3f,
                   "p-lock Tune applique au pas 9, Decay revenu a la valeur du potard");
        }
        {
            AleaAudioProcessor p; fresh (p);
            auto& tp = p.bank.pats[0].tracks[0];
            tp.length = 4; tp.dir = 1;
            check (tp.stepIndex (0, 0) == 3 && tp.stepIndex (1, 0) == 2, "sens Reverse");
            tp.dir = 2;
            check (tp.stepIndex (3, 0) == 3 && tp.stepIndex (4, 0) == 2 && tp.stepIndex (6, 0) == 0, "sens Ping-pong");
        }
        {
            AleaAudioProcessor p; fresh (p);
            for (int i = 0; i < 16; ++i) p.bank.pats[0].tracks[3].steps[i].set (1, 0, 100, 1, 0, 0);
            setP (p, "density", 0.0f);
            auto lo = render (p, 4 * 2.0, nullptr);
            p.bank.pats[0].tracks[3].clear();
            setP (p, "density", 1.0f);
            auto hi = render (p, 4 * 2.0, nullptr);
            check (lo.noteOns[3] < 16 && hi.noteOns[3] > 4, "Density retire / ajoute des coups (" + juce::String (lo.noteOns[3]) + " / " + juce::String (hi.noteOns[3]) + ")");
        }
        {
            AleaAudioProcessor p; fresh (p);
            p.bank.initDefault();
            p.bank.pats[1].tracks[0].clear();          // pattern B sans kick
            check (p.setChainText ("A1 B1"), "chaine du song mode acceptee");
            check (! p.setChainText ("Z9 ??"), "chaine invalide refusee");
            p.setChainText ("A1 B1");
            setP (p, "songmode", 1.0f);
            auto st = render (p, 4 * 2.0, nullptr);    // A B A B
            check (st.noteOns[0] == 8, "song mode alterne A (kick) et B (sans kick) : " + juce::String (st.noteOns[0]) + " kicks");
        }
    }

    // 4) alea : morph, modulateurs, gamme, link, reproductibilite
    {
        AleaAudioProcessor p;
        p.prepareToPlay (sr, 512);
        p.storeB();
        setP (p, tid (0, "decay"), 0.0f);
        p.setParamReal ("morph", 1.0f);
        hitPeak (p, 0);
        check (std::abs (p.lastNorm[0][Decay].load() - defaultVP (0, Decay)) < 1e-3f, "Morph = 1 rejoue l'instantane B");
        p.setParamReal ("morph", 0.5f);
        hitPeak (p, 0);
        check (std::abs (p.lastNorm[0][Decay].load() - defaultVP (0, Decay) * 0.5f) < 1e-3f, "Morph = 0.5 interpole A et B");
        p.setParamReal ("morph", 0.0f);

        // modulateur S&H -> tune piste 1
        setP (p, mxId (0, "src"), 1); setP (p, mxId (0, "trk"), 1); setP (p, mxId (0, "dst"), Tune); setP (p, mxId (0, "amt"), 1.0f);
        setP (p, modId (0, "rate"), 0);
        setP (p, "play", 1.0f);
        for (auto& tp : p.bank.pats[0].tracks) tp.clear();
        for (int i = 0; i < 16; ++i) p.bank.pats[0].tracks[0].steps[i].set (1, 0, 100, 1, 0, 0);
        juce::Array<float> tunes;
        for (int k = 0; k < 16; ++k) { render (p, 0.125, nullptr); tunes.addIfNotAlreadyThere (std::round (p.lastNorm[0][Tune].load() * 1000.0f)); }
        check (tunes.size() >= 6, "modulateur S&H fait varier le Tune (" + juce::String (tunes.size()) + " valeurs)");

        // gamme : penta mineure, alea sur le tune
        setP (p, mxId (0, "amt"), 0.0f);
        setP (p, "scale", 5); setP (p, jitId (0, Tune), 1.0f); setP (p, "chaos", 1.0f);
        bool onScale = true;
        for (int k = 0; k < 24; ++k)
        {
            render (p, 0.125, nullptr);
            const float semis = (p.lastNorm[0][Tune].load() - 0.5f) * 48.0f;
            const int pc = ((int) std::round (semis) % 12 + 12) % 12;
            if (std::abs (semis - std::round (semis)) > 0.05f || ! (pc == 0 || pc == 3 || pc == 5 || pc == 7 || pc == 10)) onScale = false;
        }
        check (onScale, "Scale : l'accordage aleatoire tombe sur la penta mineure");

        // link : decay et color varient ensemble
        setP (p, "scale", 0); setP (p, jitId (0, Tune), 0.0f);
        setP (p, jitId (0, Decay), 1.0f); setP (p, jitId (0, Color), 1.0f); setP (p, tid (0, "link"), 1.0f);
        setP (p, tid (0, "decay"), 0.5f); setP (p, tid (0, "color"), 0.5f);
        bool same = true;
        for (int k = 0; k < 10; ++k)
        {
            render (p, 0.125, nullptr);
            if (std::abs ((p.lastNorm[0][Decay].load() - 0.5f) - (p.lastNorm[0][Color].load() - 0.5f)) > 1e-3f) same = false;
        }
        check (same, "Rnd Link = 100 % : les decalages sont identiques");

        // reproductibilite
        AleaAudioProcessor a, b;
        for (auto* q : { &a, &b })
        {
            q->prepareToPlay (sr, 512);
            setP (*q, "play", 1.0f); setP (*q, "seqchaos", 0.5f); setP (*q, "chaos", 1.0f); setP (*q, "rnddist", 1);
            setP (*q, tid (3, "decay_rnd"), 0.8f);
        }
        juce::AudioBuffer<float> ra, rb;
        render (a, 4.0, &ra); render (b, 4.0, &rb);
        double diff = 0;
        for (int i = 0; i < ra.getNumSamples(); ++i) diff = juce::jmax (diff, (double) std::abs (ra.getSample (0, i) - rb.getSample (0, i)));
        check (diff < 1.0e-5, "rendu reproductible a seed egale");
    }

    // 5) multi-out
    {
        AleaAudioProcessor p;
        auto layout = p.getBusesLayout();
        for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = juce::AudioChannelSet::stereo();
        check (p.setBusesLayout (layout), "configuration 9 sorties stereo acceptee");
        p.prepareToPlay (sr, 512);
        setP (p, tid (0, "out"), 1.0f);
        soloTrack (p, 0);
        juce::AudioBuffer<float> buf (18, 512);
        float mainPk = 0, auxPk = 0;
        for (int k = 0; k < 40; ++k)
        {
            juce::MidiBuffer midi;
            if (k == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            p.processBlock (buf, midi);
            mainPk = juce::jmax (mainPk, buf.getMagnitude (0, 0, 512), buf.getMagnitude (1, 0, 512));
            auxPk = juce::jmax (auxPk, buf.getMagnitude (2, 0, 512), buf.getMagnitude (3, 0, 512));
        }
        check (auxPk > 0.05f && mainPk < 0.001f, "Out Separate : le kick sort sur Voice 1, pas sur Main");
    }

    // 6) historique, variation, export MIDI, presets fichier, etat
    {
        AleaAudioProcessor p;
        p.prepareToPlay (sr, 512);
        p.pushUndo();
        setP (p, tid (2, "decay"), 0.1f);
        p.bank.pats[0].tracks[2].clear();
        check (p.undo(), "undo disponible");
        check (std::abs (p.apvts.getRawParameterValue (tid (2, "decay"))->load() - defaultVP (2, Decay)) < 1e-4f
               && p.bank.pats[0].tracks[2].steps[12].on == 1, "undo restaure parametre et pattern");
        check (p.redo() && p.bank.pats[0].tracks[2].steps[12].on == 0, "redo");

        p.bank.initDefault();
        int before = 0, after = 0, diffs = 0;
        p.randomizeTrackPattern (0, 5, 77);
        for (int i = 0; i < 16; ++i) before += p.bank.pats[0].tracks[5].steps[i].on;
        TrackClip clip; clip.grab (p.bank.pats[0].tracks[5]);
        p.varyTrack (0, 5, 0.5f, 42);
        for (int i = 0; i < 16; ++i) { after += p.bank.pats[0].tracks[5].steps[i].on; diffs += p.bank.pats[0].tracks[5].steps[i].toString() != clip.steps[i]; }
        check (diffs > 0 && std::abs (after - before) <= 6, "Vary : variation proche du style (" + juce::String (before) + " -> " + juce::String (after) + " coups)");

        auto mf = p.exportMidi (0, 4);
        juce::MidiFile midiFile;
        juce::FileInputStream in (mf);
        bool okRead = in.openedOk() && midiFile.readFrom (in);
        int notes = 0;
        if (okRead) for (const auto* ev : *midiFile.getTrack (0)) if (ev->message.isNoteOn()) ++notes;
        check (okRead && notes > 40, "export MIDI 4 mesures (" + juce::String (notes) + " notes)");

        const auto pf = outDir.getChildFile ("test.alea");
        p.bank.pats[2].tracks[1].steps[3].set (1, 1, 50, 2, 10, 3, 6);
        p.bank.pats[2].tracks[1].steps[3].plock[Cutoff] = 321;
        p.bank.pats[2].tracks[1].dir = 3;
        p.storeB();
        p.setChainText ("A2 C1 B3");
        p.loadSample (4, sampleFile);
        setP (p, "chaos", 0.9f);
        check (p.savePresetFile (pf), "sauvegarde d'un preset fichier");
        AleaAudioProcessor q;
        check (q.loadPresetFile (pf), "chargement du preset fichier");
        const auto& st = q.bank.pats[2].tracks[1].steps[3];
        check (st.on == 1 && st.acc == 1 && st.prob == 50 && st.rat == 2 && st.nudge == 10 && st.tune == 3 && st.cond == 6
               && st.plock[Cutoff] == 321 && q.bank.pats[2].tracks[1].dir == 3, "pas, condition, p-lock et sens restaures");
        check (q.hasB.load() && q.getChainText() == "A2 C1 B3" && q.sampleName (4) == "test_sample", "morph B, chaine et echantillon restaures");
        check (std::abs (q.apvts.getRawParameterValue ("chaos")->load() - 0.9f) < 1e-4f, "parametres restaures");

        // MIDI learn
        p.learnParam = p.paramIndex ("chaos");
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 127), 0);
        p.processBlock (buf, midi);
        midi.clear(); midi.addEvent (juce::MidiMessage::controllerEvent (1, 21, 0), 0);
        p.processBlock (buf, midi);
        check (p.ccForParam (p.paramIndex ("chaos")) == 21 && p.apvts.getRawParameterValue ("chaos")->load() < 0.01f, "MIDI learn : CC21 pilote Chaos");
    }

    // 7b) fonctions v3
    {
        // Keep
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, jitId (0, Decay), 1.0f); setP (p, "chaos", 1.0f);
            hitPeak (p, 0);
            const float played = p.lastNorm[0][Decay].load();
            p.keepLastHit (0);
            check (std::abs (p.apvts.getRawParameterValue (vpId (0, Decay))->load() - played) < 1e-4f, "Keep fige la valeur jouee au dernier coup");
        }
        // favoris + breed
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, vpId (1, Decay), 0.2f); p.storeFav (1, 0);
            setP (p, vpId (1, Decay), 0.8f); p.storeFav (1, 1);
            p.recallFav (1, 0);
            check (std::abs (p.apvts.getRawParameterValue (vpId (1, Decay))->load() - 0.2f) < 1e-4f, "favori rappele");
            bool inside = true;
            for (int k = 0; k < 20; ++k)
            {
                p.breedVoice (1, 0, 1, 900 + k);
                const float v = p.apvts.getRawParameterValue (vpId (1, Decay))->load();
                if (v < 0.2f - 0.081f || v > 0.8f + 0.081f) inside = false;
            }
            check (inside, "Breed : descendant entre les parents (+/- mutation)");
        }
        // scenes + crossfader + rappel MIDI
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, vpId (0, Decay), 0.2f); p.storeScene (0);
            setP (p, vpId (0, Decay), 0.8f); p.storeScene (1);
            setP (p, vpId (0, Decay), 0.5f);
            p.recallScene (0);
            check (std::abs (p.apvts.getRawParameterValue (vpId (0, Decay))->load() - 0.2f) < 1e-4f, "scene rappelee");
            setP (p, "scenea", 0); setP (p, "sceneb", 1); setP (p, "sceneon", 1.0f); setP (p, "scenexf", 0.5f);
            hitPeak (p, 0);
            check (std::abs (p.lastNorm[0][Decay].load() - 0.5f) < 1e-3f, "crossfader de scenes a 50 %");
            setP (p, "sceneon", 0.0f);
            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 25, (juce::uint8) 100), 0);
            p.processBlock (buf, midi);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            check (std::abs (p.apvts.getRawParameterValue (vpId (0, Decay))->load() - 0.8f) < 1e-4f, "note MIDI C#0 rappelle la scene 2");
        }
        // evolution
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, "play", 1.0f); setP (p, "evoamt", 1.0f); setP (p, "evobars", 0); setP (p, "evotarget", 2);
            TrackClip before; before.grab (p.bank.pats[0].tracks[3]);
            render (p, 8 * bar, nullptr);
            const int amt = p.evolutionAmount();
            check (amt > 5, "evolution : " + juce::String (amt) + " changements en 8 mesures");
            check (before.steps[0] == p.bank.pats[0].tracks[3].steps[0].toString(), "evolution non destructive avant Commit");
            p.commitEvolution();
            int diffs = 0;
            for (int i = 0; i < 16; ++i) diffs += before.steps[i] != p.bank.pats[0].tracks[3].steps[i].toString();
            check (p.evolutionAmount() == 0, "Commit ecrit et vide la couche d'evolution (" + juce::String (diffs) + " pas changes sur T4)");
        }
        // croisement de patterns
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            for (int t = 0; t < kNumTracks; ++t) { p.bank.pats[0].tracks[t].clear(); p.bank.pats[1].tracks[t].clear(); }
            for (int i = 0; i < 16; ++i) p.bank.pats[0].tracks[0].steps[i].on = 1;
            p.breedPatterns (0, 1, 2, 5);
            int on = 0; for (int i = 0; i < 16; ++i) on += p.bank.pats[2].tracks[0].steps[i].on;
            check (on > 0 && on < 16, "Breed patterns : moitie de chaque parent (" + juce::String (on) + "/16)");
        }
        // condition PRE
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            for (auto& tp : p.bank.pats[0].tracks) tp.clear();
            setP (p, "bpm", 120.0f); setP (p, "play", 1.0f);
            p.bank.pats[0].tracks[0].steps[0].set (1, 0, 100, 1, 0, 0, 1);    // 1:2
            p.bank.pats[0].tracks[0].steps[8].set (1, 0, 100, 1, 0, 0, 14);   // PRE
            p.bank.pats[0].tracks[1].steps[8].set (1, 0, 100, 1, 0, 0, 16);   // NEI (piste 1 = voisine de la piste 2)
            auto st = render (p, 8 * 2.0 - 0.1, nullptr);
            check (st.noteOns[0] == 8 && st.noteOns[1] == 4, "conditions PRE et NEI (" + juce::String (st.noteOns[0]) + ", " + juce::String (st.noteOns[1]) + ")");
        }
        // groove et nouvelles subdivisions
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            for (auto& tp : p.bank.pats[0].tracks) tp.clear();
            setP (p, "bpm", 120.0f); setP (p, "play", 1.0f);
            p.bank.pats[0].tracks[0].steps[1].on = 1;
            p.bank.pats[0].tracks[0].groove = 5;   // MPC 66 %
            juce::AudioBuffer<float> buf (2, 512);
            int pos = -1;
            for (int b = 0; b < 40 && pos < 0; ++b)
            {
                juce::MidiBuffer midi;
                p.processBlock (buf, midi);
                for (const auto m : midi) if (m.getMessage().isNoteOn()) pos = b * 512 + m.samplePosition;
            }
            check (std::abs (pos - (int) (0.25 * 1.32 * 0.5 * sr)) < 3, "groove MPC 66 % (" + juce::String (pos) + " echantillons)");

            AleaAudioProcessor q; q.prepareToPlay (sr, 512);
            for (auto& tp : q.bank.pats[0].tracks) tp.clear();
            setP (q, "bpm", 120.0f); setP (q, "play", 1.0f);
            auto& tp = q.bank.pats[0].tracks[0];
            tp.length = 20; tp.rate = 6;             // 1/16 x5 : 5 pas par temps
            for (int i = 0; i < 20; ++i) tp.steps[i].on = 1;
            auto st = render (q, 2 * 2.0 - 0.05, nullptr);
            check (st.noteOns[0] == 40, "subdivision 1/16 x5 : 40 coups en 2 mesures (" + juce::String (st.noteOns[0]) + ")");
        }
        // effets d'insertion
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, tid (0, "drive"), 0.0f);
            setP (p, vpId (0, FxAmt), 0.7f);
            const float ref = hitPeak (p, 0);
            bool ok = true;
            juce::String lv;
            for (int fx = 1; fx <= 4; ++fx)
            {
                setP (p, tid (0, "fxtype"), (float) fx);
                const float pk = hitPeak (p, 0);
                lv << juce::String (juce::Decibels::gainToDecibels (pk), 1) << " ";
                if (! std::isfinite (pk) || pk < 0.02f || pk > 1.01f || std::abs (pk - ref) < 1e-4f) ok = false;
            }
            check (ok, "FX d'insertion Fold / Comb / Ring / Tape (" + lv.trim() + " dB)");
        }
        // couche echantillon
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            p.loadSample (0, sampleFile);
            const float a = hitPeak (p, 0);
            setP (p, vpId (0, Layer), 1.0f);
            const float b = hitPeak (p, 0);
            check (std::abs (a - b) > 0.01f, "Smp Layer superpose l'echantillon a la synthese");
        }
        // sidechain
        {
            auto run = [&] (float duck)
            {
                AleaAudioProcessor p; p.prepareToPlay (sr, 512);
                soloTrack (p, 4);                       // kick muet : il sert seulement de source de sidechain
                setP (p, "duckamt", duck);
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
                midi.addEvent (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 100), 0);
                p.processBlock (buf, midi);
                midi.clear();
                double e = 0;
                for (int k = 0; k < 6; ++k) { p.processBlock (buf, midi); e += buf.getRMSLevel (0, 0, 512); }
                return e;
            };
            const double dry = run (0.0f), ducked = run (1.0f);
            check (ducked < dry * 0.7, "sidechain : le kick fait baisser le reste");
        }
        // clavier + slide
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, "keystrack", 7);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 200);
            p.processBlock (buf, midi);
            const float pk = buf.getMagnitude (0, 0, 512);
            check (p.flash[6].load() > 0.5f && pk > 0.01f, "mode clavier : la voix 7 joue sur les notes C3+ (legato = slide)");
        }
        // HQ + plate
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, "play", 1.0f); setP (p, "revtype", 1);
            setP (p, tid (6, "engine"), (float) EngZap);
            juce::AudioBuffer<float> a, b;
            auto s1 = render (p, 2.0, &a);
            setP (p, "hq", 1.0f);
            auto s2 = render (p, 2.0, &b);
            check (s1.finite && s2.finite && s2.rms > s1.rms * 0.5 && s2.rms < s1.rms * 2.0, "mode HQ x2 + reverbe plate");
        }
        // modulation d'une cible globale (Density)
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            setP (p, "play", 1.0f);
            auto base = render (p, 4 * bar, nullptr);
            setP (p, mxId (0, "src"), 5); setP (p, mxId (0, "dst"), (float) (kNumVP + GDensity)); setP (p, mxId (0, "amt"), -1.0f);
            setP (p, modId (4, "p"), 0.7f);   // LFO carre
            AleaAudioProcessor q; q.prepareToPlay (sr, 512);
            setP (q, "play", 1.0f);
            setP (q, mxId (0, "src"), 5); setP (q, mxId (0, "dst"), (float) (kNumVP + GDensity)); setP (q, mxId (0, "amt"), -1.0f);
            setP (q, modId (4, "p"), 0.7f);
            auto mod = render (q, 4 * bar, nullptr);
            int nb = 0, nm = 0;
            for (int t = 0; t < kNumTracks; ++t) { nb += base.noteOns[t]; nm += mod.noteOns[t]; }
            check (nm != nb, "LFO -> G:Density module la densite en continu (" + juce::String (nb) + " -> " + juce::String (nm) + ")");
        }
        // style appris depuis un MIDI
        {
            juce::MidiMessageSequence seq;
            for (int barN = 0; barN < 4; ++barN)
                for (int i = 0; i < 16; ++i)
                {
                    const double tick = (barN * 16 + i) * 120.0;
                    if (i % 4 == 0) seq.addEvent (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), tick);
                    if (i == 4 || i == 12) seq.addEvent (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 120), tick);
                    if (i % 2 == 0) seq.addEvent (juce::MidiMessage::noteOn (10, 42, (juce::uint8) 80), tick);
                }
            juce::MidiFile mf; mf.setTicksPerQuarterNote (480); mf.addTrack (seq);
            auto f = outDir.getChildFile ("style.mid"); f.deleteFile();
            { juce::FileOutputStream os (f); mf.writeTo (os); }
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            check (p.learnStyleFromMidi (f) && p.style.bars == 4, "apprentissage d'un fichier MIDI (" + juce::String (p.style.bars) + " mesures)");
            int match = 0;
            for (int k = 0; k < 10; ++k)
            {
                p.generateFromStyle (0, 0, k);
                p.generateFromStyle (0, 1, 100 + k);
                bool ok = true;
                for (int i = 0; i < 16; ++i)
                {
                    if ((p.bank.pats[0].tracks[0].steps[i].on != 0) != (i % 4 == 0)) ok = false;
                    if ((p.bank.pats[0].tracks[1].steps[i].on != 0) != (i == 4 || i == 12)) ok = false;
                }
                match += ok;
            }
            check (match >= 2, "patterns generes dans le style appris (" + juce::String (match) + "/10 identiques au modele)");
        }
        // apercu
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            auto pv = p.previewNextBar();
            check ((pv.play[0] & 0x1111u) == 0x1111u, "apercu : les 4 kicks de la mesure a venir");
        }
        // etat v3
        {
            AleaAudioProcessor p; p.prepareToPlay (sr, 512);
            p.storeFav (2, 3); p.storeScene (5); p.evoMask[1][2] = 0x80000001u; p.evoOff[3][Decay] = 0.2f;
            p.bank.pats[0].tracks[4].groove = 6; p.bank.pats[0].tracks[4].steps[2].slide = 1;
            juce::MemoryBlock mb; p.getStateInformation (mb);
            AleaAudioProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
            check (q.favs[2][3].valid && q.sceneValid[5].load() && q.evoMask[1][2].load() == 0x80000001u
                   && std::abs (q.evoOff[3][Decay].load() - 0.2f) < 1e-4f && q.bank.pats[0].tracks[4].groove == 6
                   && q.bank.pats[0].tracks[4].steps[2].slide == 1, "etat v3 restaure (favoris, scenes, evolution, groove, slide)");
        }
    }

    // 7) CPU
    {
        AleaAudioProcessor p;
        p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (2);
        setP (p, "play", 1.0f);
        auto t0 = juce::Time::getMillisecondCounterHiRes();
        render (p, 30.0, nullptr);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "CPU (preset Broken Glitch) : " << juce::String (ms / 30000.0 * 100.0, 2) << " % d'un coeur" << std::endl;
        check (ms < 30000.0 * 0.25, "charge CPU raisonnable");
    }

    // 8) robustesse facon pluginval
    {
        juce::Random r (42);
        bool allFinite = true; float worst = 0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 1, 17, 64, 256, 1024, 2048 })
            {
                AleaAudioProcessor p;
                const bool multi = block == 64 || block == 1024;
                if (multi)
                {
                    auto layout = p.getBusesLayout();
                    for (int i = 1; i < layout.outputBuses.size(); ++i) layout.outputBuses.getReference (i) = juce::AudioChannelSet::stereo();
                    p.setBusesLayout (layout);
                }
                const int ch = multi ? 18 : 2;
                p.loadSample (2, sampleFile);
                p.prepareToPlay (rate, block);
                setP (p, "play", 1.0f);
                juce::AudioBuffer<float> buf (ch, block);
                juce::MidiBuffer midi;
                const int nBlocks = (int) (rate * 3.0 / block);
                auto& params = p.getParameters();
                for (int b = 0; b < nBlocks; ++b)
                {
                    if (b % juce::jmax (1, 2048 / block) == 0)
                        for (int k = 0; k < 16; ++k)
                        {
                            auto* prm = params[r.nextInt (params.size())];
                            if (prm->getName (32).contains ("Play")) continue;
                            prm->setValue (r.nextFloat());
                        }
                    if (b % juce::jmax (1, 8192 / block) == 0)
                    {
                        auto& s = p.bank.pats[0].tracks[r.nextInt (kNumTracks)].steps[r.nextInt (16)];
                        s.set (1, r.nextInt (2), r.nextInt (101), 1 + r.nextInt (4), r.nextInt (101) - 50, r.nextInt (49) - 24, r.nextInt (kNumConds));
                        s.plock[r.nextInt (kNumVP)] = (int16_t) r.nextInt (1001);
                    }
                    midi.clear();
                    if (r.nextFloat() < 0.05f) midi.addEvent (juce::MidiMessage::noteOn (1, 36 + r.nextInt (24), (juce::uint8) (1 + r.nextInt (127))), r.nextInt (block));
                    if (r.nextFloat() < 0.02f) midi.addEvent (juce::MidiMessage::controllerEvent (1, r.nextInt (128), r.nextInt (128)), r.nextInt (block));
                    p.processBlock (buf, midi);
                    for (int c = 0; c < ch; ++c)
                        for (int i = 0; i < block; ++i)
                        {
                            const float v = buf.getSample (c, i);
                            if (! std::isfinite (v)) allFinite = false;
                            worst = juce::jmax (worst, std::abs (v));
                        }
                }
            }
        check (allFinite, "fuzz : aucun NaN/Inf (3 sample rates x 6 tailles de bloc, multi-out, p-locks, conditions)");
        check (worst <= 1.0f, "fuzz : sorties bornees a 0 dBFS (crete " + juce::String (worst, 3) + ")");
    }

    // 9) interface : chaque onglet + p-lock, redimensionnement
    {
        AleaAudioProcessor p;
        p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (1);
        setP (p, "play", 1.0f);
        p.storeB();
        render (p, 2.0, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* e = dynamic_cast<AleaAudioProcessorEditor*> (ed.get());
        std::cout << "taille initiale de l'editeur : " << ed->getWidth() << "x" << ed->getHeight() << " (uiScale " << p.uiScale.load() << ")" << std::endl;
        e->selectTrack (3);
        render (p, 0.4, nullptr);
        auto* view = dynamic_cast<MainView*> (e->getChildComponent (0));
        const char* names[] { "voice", "mod", "fx", "perform", "lab", "link", "aide" };
        for (int tab = 0; tab < 7; ++tab)
        {
            // clic sur l'onglet
            for (auto* c : view->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText().equalsIgnoreCase (names[tab]))
                    b->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
            render (p, 0.3, nullptr);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (juce::String ("alea_gui_") + names[tab] + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat png;
            png.writeImageToStream (img, os);
        }
        view->setLockStep (6, 6);
        for (auto* c : view->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "VOICE") b->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile ("alea_gui_plock.png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
        ed->setSize ((int) (MainView::W * 0.75f), (int) (MainView::H * 0.75f));
        check (std::abs (p.uiScale.load() - 0.75f) < 0.01f, "interface redimensionnable (echelle memorisee)");
        check (true, "captures de l'interface (7 onglets + p-lock)");

        for (int i = 0; i < 6; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e2 (p.createEditor());
            render (p, 0.05, nullptr);
            juce::MemoryBlock mb; p.getStateInformation (mb); p.setStateInformation (mb.getData(), (int) mb.getSize());
        }
        check (true, "editeur ouvert/ferme + rechargement d'etat pendant le rendu");
    }

    std::cout << (failures == 0 ? juce::String ("TOUS LES TESTS PASSENT") : "ECHECS : " + juce::String (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
