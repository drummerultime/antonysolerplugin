#include "ColorTheory.h"
#include <iostream>
using namespace color::theory;
int main()
{
    juce::ScopedJuceInitialiser_GUI g;
    for (int st = 0; st < kNumStyles; ++st)
        for (int sc : { 0, 5, 1 })
        {
            GenOptions o; o.style = st; o.scale = sc; o.numChords = 8; o.complexity = 0.5f; o.tension = 0.5f; o.chroma = 0.3f; o.seed = 7;
            auto p = generate (o);
            std::cout << styleNames()[st] << " / " << scale (sc).name << " : ";
            for (auto& c : p) std::cout << chordName (c, 0) << "(" << romanNumeral (c, sc) << "," << (int) c.beats << ") ";
            std::cout << std::endl;
        }
    // voicings
    GenOptions o; o.style = SJazz; o.numChords = 4; o.complexity = 0.6f; o.tension = 0.5f; auto p = generate (o);
    for (int v = 0; v < kNumVoicings; ++v)
    {
        VoiceOptions vo; vo.voicing = v; vo.voices = 4; vo.complexity = 0.6f; vo.tension = 0.5f; vo.keyRoot = 0;
        int prev[8]; int pn = 0;
        std::cout << voicingNames()[v] << ": ";
        for (auto& c : p)
        {
            int n[10]; const int k = voiceChord (c, vo, prev, pn, n, 10);
            std::cout << chordName (c, 0) << "[";
            for (int i = 0; i < k; ++i) std::cout << juce::MidiMessage::getMidiNoteName (n[i], true, true, 3) << (i + 1 < k ? " " : "");
            std::cout << "] t=" << juce::String (intervalTension (n + 1, k - 1), 2) << "  ";
            pn = k - 1; for (int i = 0; i < pn; ++i) prev[i] = n[i + 1];
        }
        std::cout << std::endl;
    }
    for (auto& c : p) std::cout << chordName (c, 0) << " parent " << parentScaleName (c, 0.2f, 0) << " / out " << parentScaleName (c, 0.9f, 0) << std::endl;
    Chord id; identifyChord ((1<<2)|(1<<5)|(1<<9)|(1<<0), 2, id); std::cout << "identify D F A C -> " << chordName (id, 0) << std::endl;
    return 0;
}
