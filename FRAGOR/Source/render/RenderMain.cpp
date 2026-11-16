// Outil de test : rend chaque preset d'usine (et des patchs générés) en WAV,
// vérifie NaN / crêtes / continu et mesure le coût CPU.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"

namespace
{
    struct Note { double startBeat, lenBeats; int note; float vel; };

    std::vector<Note> bassPattern()
    {
        // 4 mesures à 140 BPM, en Fa mineur
        return { { 0.0, 1.5, 41, 0.9f }, { 1.5, 0.5, 41, 0.8f }, { 2.0, 1.0, 44, 0.9f }, { 3.0, 1.0, 39, 0.9f },
                 { 4.0, 3.0, 41, 1.0f }, { 7.0, 1.0, 48, 0.9f },
                 { 8.0, 0.75, 41, 0.9f }, { 8.75, 0.75, 41, 0.9f }, { 9.5, 0.5, 46, 0.9f }, { 10.0, 2.0, 44, 0.9f },
                 { 12.0, 4.0, 36, 1.0f } };
    }

    std::vector<Note> chordPattern()
    {
        std::vector<Note> n;
        const int chords[4][3] = { { 53, 56, 60 }, { 49, 53, 56 }, { 51, 55, 58 }, { 48, 51, 55 } };
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 3; ++k)
                n.push_back ({ c * 4.0, c == 3 ? 3.5 : 1.5, chords[c][k], 0.85f }),
                n.push_back ({ c * 4.0 + 2.5, 1.0, chords[c][k], 0.7f });
        return n;
    }

    struct Stats { float peak = 0, rms = 0, dc = 0; int nans = 0; double cpu = 0; };

    Stats render (FragorProcessor& p, const std::vector<Note>& notes, double bpm, double sr, double seconds, const juce::File& out)
    {
        const int block = 512;
        const int total = (int) (seconds * sr);
        juce::AudioBuffer<float> all (2, total);
        juce::AudioBuffer<float> buf (2, block);
        const double spb = 60.0 / bpm * sr;

        p.prepareToPlay (sr, block);
        Stats st;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            buf.setSize (2, n, false, false, true);
            juce::MidiBuffer midi;
            for (auto& nt : notes)
            {
                const int on = (int) (nt.startBeat * spb), off = (int) ((nt.startBeat + nt.lenBeats) * spb) - 1;
                if (on >= pos && on < pos + n)   midi.addEvent (juce::MidiMessage::noteOn (1, nt.note, nt.vel), on - pos);
                if (off >= pos && off < pos + n) midi.addEvent (juce::MidiMessage::noteOff (1, nt.note), off - pos);
            }
            p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, buf, c, 0, n);
        }
        st.cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0 / seconds;

        double sum = 0, dc = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
            {
                const float v = all.getSample (c, i);
                if (! std::isfinite (v)) { ++st.nans; all.setSample (c, i, 0.0f); continue; }
                st.peak = std::max (st.peak, std::abs (v));
                sum += (double) v * v;
                dc += v;
            }
        st.rms = (float) std::sqrt (sum / (2.0 * total));
        st.dc = (float) (dc / (2.0 * total));

        out.deleteFile();
        juce::WavAudioFormat wav;
        if (auto os = out.createOutputStream())
            if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0))
            {
                os.release();
                std::unique_ptr<juce::AudioFormatWriter> writer (w);
                writer->writeFromAudioSampleBuffer (all, 0, total);
            }
        return st;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("renders").getFullPathName());
    outDir.createDirectory();
    const double sr = 48000.0, bpm = 140.0;
    const double seconds = 16.0 * 60.0 / bpm + 1.5;

    FragorProcessor proc;
    proc.fallbackBpm = bpm;

    if (argc > 2 && juce::String (argv[2]) == "--stress")
    {
        juce::Random r (42);
        int failures = 0;
        // 1) aller-retour de l'état
        proc.loadFactoryPreset (8);
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        FragorProcessor other;
        other.setStateInformation (mb.getData(), (int) mb.getSize());
        int diffs = 0;
        auto& pa = proc.getParameters();
        auto& pb = other.getParameters();
        for (int i = 0; i < pa.size(); ++i)
            if (std::abs (pa[i]->getValue() - pb[i]->getValue()) > 1.0e-5f) ++diffs;
        std::printf ("state roundtrip: %d diffs, preset '%s', macros '%s'\n", diffs, other.presetName.toRawUTF8(),
                     other.macroNames.joinIntoString (",").toRawUTF8());
        if (diffs) ++failures;

        // 2) fuzz : fréquences, tailles de blocs, paramètres aléatoires, notes aléatoires
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            proc.prepareToPlay (fs, 2048);
            juce::AudioBuffer<float> buf (2, 2048);
            int bad = 0; float peak = 0;
            for (int it = 0; it < 1500; ++it)
            {
                const int n = 1 + r.nextInt (2048);
                buf.setSize (2, n, false, false, true);
                juce::MidiBuffer midi;
                if (r.nextFloat() < 0.3f) midi.addEvent (juce::MidiMessage::noteOn (1, 12 + r.nextInt (100), r.nextFloat()), r.nextInt (n));
                if (r.nextFloat() < 0.25f) midi.addEvent (juce::MidiMessage::noteOff (1, 12 + r.nextInt (100)), r.nextInt (n));
                if (r.nextFloat() < 0.05f) midi.addEvent (juce::MidiMessage::pitchWheel (1, r.nextInt (16384)), 0);
                if (r.nextFloat() < 0.05f) midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, r.nextBool() ? 127 : 0), 0);
                if (r.nextFloat() < 0.2f)
                    for (int k = 0; k < 6; ++k)
                    {
                        auto* prm = pa[r.nextInt (pa.size())];
                        prm->setValueNotifyingHost (r.nextFloat());
                    }
                if (r.nextFloat() < 0.01f) proc.loadFactoryPreset (r.nextInt (proc.getNumPrograms()));
                proc.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const float v = buf.getSample (c, i);
                        if (! std::isfinite (v) || std::abs (v) > 4.01f) ++bad;
                        else peak = std::max (peak, std::abs (v));
                    }
            }
            std::printf ("fuzz @ %6.0f Hz : %d bad samples, peak %.3f\n", fs, bad, peak);
            if (bad) ++failures;
        }
        std::printf (failures ? "STRESS FAILED\n" : "STRESS OK\n");
        return failures;
    }

    if (argc > 3 && juce::String (argv[2]) == "--import")
    {
        proc.loadFactoryPreset (0);
        juce::String err;
        const bool ok = proc.importWavetable (0, juce::File (juce::String (argv[3])), err);
        std::printf ("import %s : %s  table=%s\n", argv[3], ok ? "OK" : err.toRawUTF8(), proc.userTableName[0].toRawUTF8());
        auto* p = proc.apvts.getParameter ("aPos");
        std::vector<Note> notes { { 0.0, 3.0, 45, 1.0f } };
        // balayage de la position via la matrice
        proc.addModulation (fragor::srcEnv2, fragor::dstAPos, 1.0f);
        proc.apvts.getParameter ("e2A")->setValueNotifyingHost (proc.apvts.getParameter ("e2A")->convertTo0to1 (1.2f));
        proc.apvts.getParameter ("e2S")->setValueNotifyingHost (1.0f);
        juce::ignoreUnused (p);
        const auto st = render (proc, notes, bpm, sr, 2.0, outDir.getChildFile ("import.wav"));
        std::printf ("rms %.3f peak %.3f nan %d\n", st.rms, st.peak, st.nans);
        // aller-retour d'état avec la table utilisateur
        juce::MemoryBlock mb; proc.getStateInformation (mb);
        FragorProcessor other; other.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("state: user table restored = %s (%s), state size %d KB\n", other.getUserTable (0) ? "yes" : "NO",
                     other.userTableName[0].toRawUTF8(), (int) mb.getSize() / 1024);
        return ok ? 0 : 1;
    }

    if (argc > 2 && juce::String (argv[2]) == "--alias")
    {
        // screech synchronisé joué très haut : 1x vs 4x
        for (int q : { 0, 1, 2 })
        {
            proc.loadFactoryPreset (0);
            auto set = [&] (const char* id, float v) { auto* p = proc.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
            set ("quality", (float) q); set ("distOn", 0); set ("ottOn", 0); set ("subOn", 0); set ("fltOn", 0); set ("clip", 0);
            set ("aPos", 0.5f); set ("aWarpMode", 3); set ("aWarp", 0.6f); set ("aUni", 1); set ("aRandPhase", 0);
            std::vector<Note> notes { { 0.0, 6.0, 91, 1.0f } };
            const auto st = render (proc, notes, bpm, sr, 3.0, outDir.getChildFile ("alias_q" + juce::String (q) + ".wav"));
            std::printf ("quality %d : rms %.3f cpu %.1f%%\n", q, st.rms, st.cpu * 100.0);
        }
        return 0;
    }

    if (argc > 2 && juce::String (argv[2]) == "--snapshot")
    {
        proc.loadFactoryPreset (36);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto save = [&] (const juce::String& name)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            auto f = outDir.getChildFile (name);
            f.deleteFile();
            if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        };
        save ("ui_mod.png");
        auto* fe = dynamic_cast<FragorEditor*> (ed.get());
        fe->showPage (1);
        save ("ui_fx.png");
        fe->showPage (0);
        fe->openCurveEditor (0);
        save ("ui_curve.png");
        fe->openBrowser();
        save ("ui_browser.png");
        return 0;
    }
    proc.setPlayConfigDetails (0, 2, sr, 512);

    const auto& list = fragor::factoryPresets();
    bool ok = true;
    auto report = [&ok] (const juce::String& name, const Stats& s)
    {
        const bool bad = s.nans > 0 || s.peak > 1.5f || s.rms < 0.003f || std::abs (s.dc) > 0.02f;
        if (bad) ok = false;
        std::printf ("%-32s peak %6.3f  rms %6.3f  dc %+7.4f  nan %d  cpu %5.1f%%  %s\n", name.toRawUTF8(), s.peak, s.rms, s.dc, s.nans,
                     s.cpu * 100.0, bad ? "<-- CHECK" : "");
    };

    for (size_t i = 0; i < list.size(); ++i)
    {
        proc.loadFactoryPreset ((int) i);
        const bool poly = (int) proc.apvts.getRawParameterValue ("voiceMode")->load() == 0;
        const auto name = juce::String (i).paddedLeft ('0', 2) + "_" + list[i].category + "_" + list[i].name.replaceCharacter (' ', '_');
        const auto st = render (proc, poly ? chordPattern() : bassPattern(), bpm, sr, seconds, outDir.getChildFile (name + ".wav"));
        report (list[i].category + " / " + list[i].name, st);
    }

    for (int s = 0; s < 8; ++s)
        for (int k = 0; k < 2; ++k)
        {
            proc.generate (s);
            const auto st = render (proc, bassPattern(), bpm, sr, seconds,
                                    outDir.getChildFile ("gen_" + fragor::generatorStyles()[s] + "_" + juce::String (k) + ".wav"));
            report (proc.presetName, st);
            proc.mutate (0.8f);
            const auto st2 = render (proc, bassPattern(), bpm, sr, seconds, outDir.getChildFile ("mut_" + juce::String (s) + "_" + juce::String (k) + ".wav"));
            report (proc.presetName, st2);
        }

    std::printf (ok ? "\nALL OK\n" : "\nSOME PRESETS NEED ATTENTION\n");
    return 0;
}
