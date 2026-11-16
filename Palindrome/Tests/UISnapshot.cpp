// Photographie de l'interface dans un état vivant (hors DAW)
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

static void setP (PalindromeProcessor& p, const char* id, float v)
{
    auto* rp = p.apvts.getParameter (id);
    rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

static void feed (PalindromeProcessor& p, double seconds, double sr, int block, bool sound, double& t)
{
    juce::AudioBuffer<float> io (2, block); juce::MidiBuffer midi;
    const float notes[] = { 220.0f, 247.0f, 277.0f, 294.0f, 330.0f, 370.0f, 415.0f, 440.0f };
    for (int b = 0; b < (int) (seconds * sr / block); ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const double tt = t + i / sr;
            const int k = (int) (tt / 0.5) % 8;
            const double ph = std::fmod (tt, 0.5);
            const float v = sound ? 0.35f * (float) std::exp (-4.0 * ph) * (float) std::sin (6.2831853 * notes[k] * tt)
                                    + 0.12f * (float) std::sin (6.2831853 * notes[k] * 2.0 * tt) * (float) std::exp (-6.0 * ph) : 0.0f;
            io.setSample (0, i, v); io.setSample (1, i, v);
        }
        p.processBlock (io, midi);
        t += block / sr;
        juce::Thread::sleep (2);
    }
}

static void shoot (PalindromeUI& ui, const juce::String& file)
{
    for (int i = 0; i < 3; ++i) { ui.refreshAll(); juce::Thread::sleep (5); }
    auto img = ui.createComponentSnapshot (ui.getLocalBounds(), true, 1.0f);
    juce::File f (file);
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat().writeImageToStream (img, os);
    printf ("%s\n", file.toRawUTF8());
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0; const int block = 512; double t = 0.0;

    {
        PalindromeProcessor p;
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        setP (p, ParamIDs::captureMode, 3); setP (p, ParamIDs::phraseLen, 4.0f); setP (p, ParamIDs::delayFrom, 1); setP (p, ParamIDs::delay, 1.5f);
        setP (p, ParamIDs::repetitions, 5); setP (p, ParamIDs::order, 1); setP (p, ParamIDs::stereoMode, 3); setP (p, ParamIDs::decay, -4.0f);
        setP (p, ParamIDs::scale, 1); setP (p, ParamIDs::drift, 1); setP (p, ParamIDs::attack, 180.0f); setP (p, ParamIDs::randomness, 60.0f);
        setP (p, ParamIDs::noteReverse, 25.0f); setP (p, ParamIDs::octaveProb, 20.0f); setP (p, ParamIDs::skipProb, 15.0f);
        setP (p, ParamIDs::modDepth, 30.0f); setP (p, ParamIDs::feedback, 30.0f); setP (p, ParamIDs::diffusion, 50.0f);
        setP (p, ParamIDs::duckAmount, -12.0f); setP (p, ParamIDs::textureMix, 60.0f); setP (p, ParamIDs::quantGrid, 2);
        setP (p, ParamIDs::humanize, 20.0f); setP (p, ParamIDs::damping, 45.0f); setP (p, ParamIDs::speedMode, 4);

        feed (p, 4.2, sr, block, true, t);      // capture
        feed (p, 7.0, sr, block, false, t);     // délai + premières répétitions

        setP (p, ParamIDs::order, 7); p.setDirSequenceText ("R - X N R");
        p.setSlotLocked (1, true);
        juce::Thread::sleep (400);   // réanalyse de la dernière capture

        PalindromeUI ui (p);
        ui.setBounds (0, 0, PalindromeUI::baseWidth, PalindromeUI::baseHeight);
        for (int page = 0; page < 8; ++page)
        {
            ui.showPage (page);
            shoot (ui, "/tmp/ui_" + juce::String (page) + ".png");
        }
        ui.showPage (1);
        p.startMidiLearn (ParamIDs::randomness);
        ui.setLearnMode (true);
        shoot (ui, "/tmp/ui_learn.png");
        ui.setLearnMode (false);

        // éditeur complet à petite taille, puis en fenêtre haute
        for (auto [w, h, name] : { std::tuple<int, int, const char*> { 896, 574, "/tmp/ui_small.png" }, { 1100, 900, "/tmp/ui_tall.png" } })
        {
            PalindromeEditor ed (p);
            ed.setSize (w, h);
            juce::Thread::sleep (20);
            auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
            juce::File f (name); f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            printf ("%s\n", name);
        }
    }
    {
        PalindromeProcessor p;
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        setP (p, ParamIDs::captureMode, 4); setP (p, ParamIDs::bankSize, 4); setP (p, ParamIDs::phraseLen, 3.0f);
        setP (p, ParamIDs::delayFrom, 1); setP (p, ParamIDs::delay, 1.0f); setP (p, ParamIDs::repetitions, 2);
        setP (p, ParamIDs::silenceMin, 1.0f); setP (p, ParamIDs::silenceMax, 2.0f); setP (p, ParamIDs::order, 6); setP (p, ParamIDs::stereoMode, 1);
        p.setSequenceText ("3 1 4 2");
        feed (p, 24.0, sr, block, true, t);
        p.setSlotLocked (2, true);
        PalindromeUI ui (p);
        ui.setBounds (0, 0, PalindromeUI::baseWidth, PalindromeUI::baseHeight);
        ui.showPage (8);
        shoot (ui, "/tmp/ui_8.png");
        ui.showPage (0);
        shoot (ui, "/tmp/ui_bank0.png");
    }
    return 0;
}
