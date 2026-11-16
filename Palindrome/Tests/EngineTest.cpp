// Test hors DAW : mélodie synthétique désaccordée -> vérifie délai, reverse, découpage, correction.
#include "../Source/PhraseEngine.h"
#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <functional>

static float midiToHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

int main()
{
    const double sr = 48000.0; const int block = 512;
    // Do majeur : C4 D4 E4 F4 G4 A4 B4 C5, chaque note désaccordée de +0.4 demi-ton, 0.5 s chacune
    const float melody[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
    const int noteLen = (int) (0.5 * sr);
    const int phraseSamples = 8 * noteLen;
    const int total = (int) (sr * 30.0);

    { // 1. DSP direct
        juce::AudioBuffer<float> src (2, phraseSamples), dst (2, phraseSamples);
        for (int k = 0; k < 8; ++k)
            for (int i = 0; i < noteLen; ++i)
            {
                const float env = std::exp (-3.0f * i / (float) noteLen) * juce::jmin (1.0f, i / 200.0f);
                const float v = 0.5f * env * std::sin (juce::MathConstants<float>::twoPi * midiToHz (melody[k] + 0.4f) * i / (float) sr);
                src.setSample (0, k * noteLen + i, v); src.setSample (1, k * noteLen + i, v);
            }
        auto on = pal::detectOnsets (src.getReadPointer (0), phraseSamples, sr, 0.5f);
        printf ("onsets (%zu):", on.size()); for (auto o : on) printf (" %.3f", o / sr); printf ("\n");

        for (int k = 0; k < 8; ++k) {
            float c; float hz = pal::detectPitchYin (src.getReadPointer (0) + k * noteLen + 1440, 3840, sr, c);
            printf ("note %d : %.1f Hz (attendu %.1f) midi %.2f conf %.2f -> snap Do maj %.0f\n", k, hz, midiToHz (melody[k] + 0.4f),
                    69 + 12 * std::log2 (hz / 440.0f), c, pal::snapToScale (69 + 12 * std::log2 (hz / 440.0f), 0, pal::Scale::Major));
        }
        pal::RenderParams p; p.sampleRate = sr; p.scale = pal::Scale::Major; p.correction = 1.0f; p.reverse = true; p.randomAmount = 0.5f; p.seed = 42;
        pal::RenderInfo info; pal::renderPhrase (src, phraseSamples, p, dst, &info);
        printf ("rendu reverse+50%% alea : ordre"); for (auto i : info.sourceIndex) printf (" %d", i);
        printf ("\n  corrections:"); for (auto s : info.shifts) printf (" %.2f", s); printf ("\n");
        // vérifie la hauteur après correction de la note 0 (sortie) en relisant forward d'une note non inversée
        p.reverse = false; p.randomAmount = 0; pal::renderPhrase (src, phraseSamples, p, dst, &info);
        for (int k = 0; k < 8; ++k) { float c; float hz = pal::detectPitchYin (dst.getReadPointer (0) + k * noteLen + 1440, 3840, sr, c);
            printf ("  corrigé note %d : midi %.2f\n", k, 69 + 12 * std::log2 (hz / 440.0f)); }
    }

    { // 2. moteur : capture au seuil, phrase 4 s, délai 15 s, alterné
        PhraseEngine e; e.prepare (sr, block);
        PhraseEngine::Settings s; s.captureMode = PhraseEngine::Threshold; s.thresholdGain = 0.05f;
        s.phraseSeconds = 4.0; s.delaySeconds = 15.0; s.order = PhraseEngine::AlternateStartReverse; s.repetitions = 2; s.randomAmount = 0.3f;
        juce::AudioBuffer<float> in (2, block), wet (2, block);
        double firstWet = -1, lastWet = -1;
        for (int pos = 0; pos < total; pos += block)
        {
            in.clear();
            for (int i = 0; i < block; ++i) {
                const int t = pos + i - (int) (sr * 1.0); // la musique commence à 1 s et dure 4 s
                if (t >= 0 && t < phraseSamples) { int k = t / noteLen, j = t % noteLen;
                    float v = 0.5f * std::exp (-3.0f * j / (float) noteLen) * std::sin (juce::MathConstants<float>::twoPi * midiToHz (melody[k]) * j / (float) sr);
                    in.setSample (0, i, v); in.setSample (1, i, v); }
            }
            { juce::AudioBuffer<float> snd (2, block); e.process (in, 0, block, wet, snd, s); }
            { // ~10x le temps réel : laisse le worker travailler
                juce::Thread::sleep (1);
            }
            if (wet.getMagnitude (0, 0, block) > 0.01f) { if (firstWet < 0) firstWet = pos / sr; lastWet = pos / sr; }
            if (pos == (int) (sr * 6.0) / block * block) printf ("t=6s : relectures programmées = %d\n", e.pendingCount.load());
        }
        printf ("relecture audible de %.2f s à %.2f s (attendu ~16 s -> ~24 s : reverse 4 s puis normal 4 s)\n", firstWet, lastWet);
        e.release();
    }
    auto runFlow = [&] (PhraseEngine::Settings s, double seconds, const char* name, const char* seqText)
    {
        PhraseEngine e; e.setSequence (seqText); e.prepare (sr, block);
        juce::AudioBuffer<float> in (2, block), wet (2, block);
        bool was = false; double onAt = 0; int lastCap = -1; bool wasRec = false;
        for (int pos = 0; pos < (int) (sr * seconds); pos += block)
        {
            // chaque capture a un timbre différent pour les reconnaître : fréquence selon le temps
            const float f = 0.02f + 0.01f * (float) ((pos / (int) sr) % 7);
            for (int i = 0; i < block; ++i) { float v = 0.3f * std::sin (f * (pos + i)); in.setSample (0, i, v); in.setSample (1, i, v); }
            { juce::AudioBuffer<float> snd (2, block); e.process (in, 0, block, wet, snd, s); }
            juce::Thread::sleep (1);
            const bool rec = e.captureState.load() == PhraseEngine::CapRecording;
            if (rec && ! wasRec) printf ("%s : capture (emplacement %d) à %.2f s\n", name, e.captureTarget.load() + 1, pos / sr);
            wasRec = rec;
            const bool on = wet.getMagnitude (0, 0, block) > 0.01f;
            if (on && ! was) onAt = pos / sr;
            if (! on && was) printf ("%s :   relecture %.2f -> %.2f s  (banque C%d)\n", name, onAt, pos / sr, e.bankPlayingSlot.load() + 1);
            was = on;
            (void) lastCap;
        }
        e.release();
    };

    { // 3. Cycle : phrase 2 s, délai 15 s, 2 répétitions, silence 3 s
        PhraseEngine::Settings s; s.captureMode = PhraseEngine::Cycle; s.phraseSeconds = 2.0; s.delaySeconds = 15.0;
        s.repetitions = 2; s.repeatGap = 0.0; s.silenceMin = 3.0; s.silenceMax = 3.0;
        runFlow (s, 45, "cycle", "");
    }
    { // 4. Banque de 3, séquence 3 1 2, phrase 2 s, délai 15 s, 1 répétition, silence 2 s
        PhraseEngine::Settings s; s.captureMode = PhraseEngine::Bank; s.bankSize = 3; s.phraseSeconds = 2.0; s.delaySeconds = 15.0;
        s.repetitions = 1; s.silenceMin = 2.0; s.silenceMax = 2.0;
        runFlow (s, 90, "banque", "3 1 2");
    }

    { // 5. nouvelles fonctions du rendu
        juce::AudioBuffer<float> src (2, phraseSamples), dst (2, phraseSamples);
        for (int k = 0; k < 8; ++k)
            for (int i = 0; i < noteLen; ++i)
            {
                const float env = std::exp (-2.0f * i / (float) noteLen) * juce::jmin (1.0f, i / 200.0f);
                const float v = 0.5f * env * std::sin (juce::MathConstants<float>::twoPi * midiToHz (melody[k]) * i / (float) sr);
                src.setSample (0, k * noteLen + i, v); src.setSample (1, k * noteLen + i, v);
            }
        auto pitches = [&] (const char* name)
        {
            printf ("%s :", name);
            for (int k = 0; k < 8; ++k) { float c; float hz = pal::detectPitchYin (dst.getReadPointer (0) + k * noteLen + 1440, 3840, sr, c);
                printf (" %.1f", hz > 0 ? 69 + 12 * std::log2 (hz / 440.0f) : -1.0f); }
            printf ("\n");
        };
        pal::RenderParams p; p.sampleRate = sr; p.scale = pal::Scale::Major; p.correction = 1.0f;
        p.transpose = 2; pal::renderPhrase (src, phraseSamples, p, dst, nullptr);
        pitches ("Do majeur +2 degrés (attendu 64 65 67 69 71 72 74 76)");
        p.transpose = 0; p.scale = pal::Scale::PentaMinor; p.keyRoot = 9; pal::renderPhrase (src, phraseSamples, p, dst, nullptr);
        pitches ("La penta mineure, recalage (attendu 60 62 64 64 67 69 72 72)");
        p.scale = pal::Scale::Off; p.octaveProb = 1.0f; p.seed = 7; pal::renderPhrase (src, phraseSamples, p, dst, nullptr);
        pitches ("Sauts d'octave 100% (chaque note ±12)");
        p.octaveProb = 0.0f; p.gain = juce::Decibels::decibelsToGain (-6.0f); pal::renderPhrase (src, phraseSamples, p, dst, nullptr);
        printf ("gain -6 dB : crête %.3f (source %.3f)\n", dst.getMagnitude (0, 0, phraseSamples), src.getMagnitude (0, 0, phraseSamples));
        p.gain = 1.0f; p.skipProb = 0.5f; p.seed = 3; pal::RenderInfo info; pal::renderPhrase (src, phraseSamples, p, dst, &info);
        int muted = 0; for (auto i : info.sourceIndex) muted += (i < 0); printf ("notes muettes 50%% : %d / 8\n", muted);
    }
    { // 6. superposé + reverse à gauche / normal à droite
        PhraseEngine::Settings s; s.captureMode = PhraseEngine::Cycle; s.phraseSeconds = 2.0; s.delaySeconds = 15.0;
        s.repetitions = 2; s.order = PhraseEngine::Superimposed; s.stereoMode = PhraseEngine::ReverseLeft; s.width = 1.0f;
        s.decayDb = -6.0f; s.silenceMin = s.silenceMax = 30.0;
        PhraseEngine e; e.prepare (sr, block);
        juce::AudioBuffer<float> in (2, block), wet (2, block);
        double eL[2] = {0,0}, eR[2] = {0,0};
        for (int pos = 0; pos < (int) (sr * 20); pos += block)
        {
            for (int i = 0; i < block; ++i) { float t = (float) (pos + i) / (float) sr; float v = t < 2.0f ? 0.3f * std::sin (0.03f * (pos + i)) * (t) : 0.0f; in.setSample (0, i, v); in.setSample (1, i, v); }
            { juce::AudioBuffer<float> snd (2, block); e.process (in, 0, block, wet, snd, s); }
            juce::Thread::sleep (1);
            const double t = pos / sr;
            const int rep = (t >= 15.0 && t < 17.0) ? 0 : (t >= 17.0 && t < 19.0) ? 1 : -1;
            if (rep >= 0) { eL[rep] += wet.getRMSLevel (0, 0, block); eR[rep] += wet.getRMSLevel (1, 0, block); }
            if (pos == (int) (sr * 3) / block * block) printf ("superposé : %d relectures programmées (attendu 4)\n", e.pendingCount.load());
        }
        printf ("rép.1 énergie G %.2f / D %.2f ; rép.2 G %.2f / D %.2f (rép.2 ≈ moitié : -6 dB)\n", eL[0], eR[0], eL[1], eR[1]);
        e.release();

        // reverse seul + "reverse à gauche" : la droite doit être vide
        s.order = PhraseEngine::ReverseOnly; s.repetitions = 1;
        PhraseEngine e2; e2.prepare (sr, block);
        double l = 0, r = 0;
        for (int pos = 0; pos < (int) (sr * 18); pos += block)
        {
            for (int i = 0; i < block; ++i) { float t = (float) (pos + i) / (float) sr; float v = t < 2.0f ? 0.3f * std::sin (0.03f * (pos + i)) : 0.0f; in.setSample (0, i, v); in.setSample (1, i, v); }
            { juce::AudioBuffer<float> snd (2, block); e2.process (in, 0, block, wet, snd, s); }
            juce::Thread::sleep (1);
            l += wet.getRMSLevel (0, 0, block); r += wet.getRMSLevel (1, 0, block);
        }
        printf ("reverse seul, reverse à gauche : G %.2f / D %.2f (D attendu ~0)\n", l, r);
        e2.release();
    }

    { // 7. précision du changement de hauteur (sinus / son riche en harmoniques)
        const int nl = 24000, nn = nl * 4; const float mel2[4] = { 52, 57, 62, 67 };
        for (int wave = 0; wave < 2; ++wave)
        {
            juce::AudioBuffer<float> src (2, nn), dst (2, nn);
            for (int k = 0; k < 4; ++k) for (int i = 0; i < nl; ++i)
            {
                const float f = midiToHz (mel2[k]); float v = 0;
                if (wave == 0) v = std::sin (6.2831853f * f * i / (float) sr);
                else for (int h = 1; h <= 8; ++h) v += std::sin (6.2831853f * f * h * i / (float) sr + h * 0.7f) / (h * 1.3f);
                v *= 0.3f * std::exp (-2.f * i / nl) * std::min (1.f, i / 200.f);
                src.setSample (0, k * nl + i, v); src.setSample (1, k * nl + i, v);
            }
            printf ("%s\n", wave ? "harmoniques" : "sinus");
            for (int sh : { -12, -7, -5, -3, 3, 5, 7, 12 })
            {
                pal::RenderParams p; p.sampleRate = sr; p.transpose = sh; pal::renderPhrase (src, nn, p, dst, nullptr);
                printf ("  %+3d :", sh);
                for (int k = 0; k < 4; ++k) { float c; float hz = pal::detectPitchYin (dst.getReadPointer (0) + k * nl + 2000, 6000, sr, c);
                    printf (" %+.2f", hz > 0 ? 69 + 12 * std::log2 (hz / 440) - mel2[k] - sh : 99.f); }
                printf ("\n");
            }
        }
    }

    { // 8. attaque (violoning)
        juce::AudioBuffer<float> src (2, phraseSamples), dst (2, phraseSamples);
        for (int k = 0; k < 8; ++k) for (int i = 0; i < noteLen; ++i)
        { const float v = 0.5f * std::sin (juce::MathConstants<float>::twoPi * midiToHz (melody[k]) * i / (float) sr);
          src.setSample (0, k * noteLen + i, v); src.setSample (1, k * noteLen + i, v); }
        auto env = [&] (const char* name)
        {
            printf ("%s : niveau note 3 à 10/100/200/300/450 ms :", name);
            for (int ms : { 10, 100, 200, 300, 450 })
                printf (" %.2f", dst.getMagnitude (0, 2 * noteLen + (int) (ms * 0.001 * sr), 480));
            printf ("\n");
        };
        pal::RenderParams p; p.sampleRate = sr; p.attackMs = 300.0f;
        pal::renderPhrase (src, phraseSamples, p, dst, nullptr); env ("par note, 300 ms");
        p.attackPerNote = false; pal::renderPhrase (src, phraseSamples, p, dst, nullptr); env ("par répétition, 300 ms");
        pal::RenderInfo inf;
        auto starts = [&] (const char* name)
        {
            printf ("%s : niveau 20 ms après chaque début de note (%d notes) :", name, (int) inf.noteStarts.size());
            for (auto st : inf.noteStarts) printf (" %.2f", dst.getMagnitude (0, (int) (st * phraseSamples) + (int) (0.02 * sr), 240));
            printf ("\n");
        };
        p.attackPerNote = true; p.attackPattern = 1; pal::renderPhrase (src, phraseSamples, p, dst, &inf);
        starts ("une note sur deux (attendu bas/haut alternés)");
        p.attackPattern = 4; p.attackProb = 0.5f; p.seed = 11; pal::renderPhrase (src, phraseSamples, p, dst, &inf);
        starts ("au hasard 50 %");
        p.attackPattern = 0; p.attackDirection = 1; p.reverse = true; pal::renderPhrase (src, phraseSamples, p, dst, &inf);
        starts ("normales seulement, lecture reverse (attendu aucune montée)");
        p.attackDirection = 2; pal::renderPhrase (src, phraseSamples, p, dst, &inf);
        starts ("reverse seulement, lecture reverse (attendu montée partout)");
    }

    { // 9. délais courts + temps de calcul
        for (int fromEnd = 0; fromEnd < 2; ++fromEnd)
        {
            PhraseEngine::Settings s; s.captureMode = PhraseEngine::Cycle; s.phraseSeconds = 6.0; s.delaySeconds = 5.0;
            s.delayFromEnd = fromEnd == 1; s.repetitions = 1; s.silenceMin = s.silenceMax = 60.0;
            PhraseEngine e; e.prepare (sr, block);
            juce::AudioBuffer<float> in (2, block), wet (2, block);
            double first = -1;
            for (int pos = 0; pos < (int) (sr * 14) && first < 0; pos += block)
            {
                for (int i = 0; i < block; ++i) { float v = 0.3f * std::sin (0.03f * (pos + i)); in.setSample (0, i, v); in.setSample (1, i, v); }
                { juce::AudioBuffer<float> snd (2, block); e.process (in, 0, block, wet, snd, s); }
                juce::Thread::sleep (10); // ~temps réel
                if (wet.getMagnitude (0, 0, block) > 0.01f) first = pos / sr;
            }
            printf ("phrase 6 s, délai 5 s depuis %s : 1re répétition à %.2f s (attendu %.1f s)\n",
                    fromEnd ? "la fin" : "le début", first, fromEnd ? 11.0 : 6.5);
            e.release();
        }
        // pire cas : phrase de 20 s, gamme + transposition + octaves (PSOLA)
        const int n20 = (int) (sr * 20);
        juce::AudioBuffer<float> src (2, n20), dst (2, n20);
        for (int i = 0; i < n20; ++i) { const int k = (i / 12000) % 8; float v = 0;
            for (int h = 1; h <= 6; ++h) v += std::sin (6.2831853f * midiToHz (melody[k]) * h * (i % 12000) / (float) sr) / h;
            v *= 0.2f * std::exp (-3.f * (i % 12000) / 12000.f); src.setSample (0, i, v); src.setSample (1, i, v); }
        pal::RenderParams p; p.sampleRate = sr; p.scale = pal::Scale::Major; p.transpose = 3; p.octaveProb = 0.5f;
        p.randomAmount = 1.0f; p.attackMs = 200; p.lowpassHz = 5000;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        pal::renderPhrase (src, n20, p, dst, nullptr);
        printf ("rendu d'une phrase de 20 s (pire cas, %d notes) : %.0f ms\n", n20 / 12000, juce::Time::getMillisecondCounterHiRes() - t0);
    }

    { // 10. v2 : rendu (quantification, vitesse de bande, texture)
        juce::AudioBuffer<float> src (2, phraseSamples), dst (2, phraseSamples);
        for (int k = 0; k < 8; ++k)
            for (int i = 0; i < noteLen; ++i)
            {
                const float env = std::exp (-3.0f * i / (float) noteLen) * juce::jmin (1.0f, i / 200.0f);
                const float v = 0.5f * env * std::sin (juce::MathConstants<float>::twoPi * midiToHz (melody[k]) * i / (float) sr);
                src.setSample (0, k * noteLen + i, v); src.setSample (1, k * noteLen + i, v);
            }
        pal::RenderParams p; p.sampleRate = sr; p.beatSamples = sr * 60.0 / 100.0; // 100 BPM -> 0,6 s par temps
        p.quantGridBeats = 1.0f; p.quantAmount = 1.0f; pal::RenderInfo inf;
        pal::renderPhrase (src, phraseSamples, p, dst, &inf);
        printf ("quantif. 100%% sur la noire (0,6 s) : débuts");
        for (auto st : inf.noteStarts) printf (" %.2f", st * phraseSamples / sr);
        printf (" s\n");

        p.quantGridBeats = 0.0f; p.speed = 0.5f;
        const int outLen = pal::renderPhrase (src, phraseSamples, p, dst, nullptr);
        float c; const float hz = pal::detectPitchYin (dst.getReadPointer (0) + 2880, 3840, sr, c);
        printf ("demi-vitesse : durée %.2f s (attendu 8.00), 1re note midi %.1f (attendu 48)\n", outLen / sr, 69 + 12 * std::log2 (hz / 440.0f));

        p.speed = 1.0f; p.textureMix = 1.0f; p.grainMs = 120; p.grainDensity = 25; p.grainSpray = 0.4f;
        pal::renderPhrase (src, phraseSamples, p, dst, nullptr);
        printf ("texture 100%% : niveau RMS %.3f (source %.3f)\n", dst.getRMSLevel (0, 0, phraseSamples), src.getRMSLevel (0, 0, phraseSamples));
    }
    { // 11. v2 : Hold, pas suivant, réglages par emplacement, import
        auto run = [&] (PhraseEngine& e, PhraseEngine::Settings& s, double fromSec, double toSec, std::function<void(double)> each)
        {
            juce::AudioBuffer<float> in (2, block), wet (2, block), snd (2, block);
            for (int pos = (int) (fromSec * sr); pos < (int) (toSec * sr); pos += block)
            {
                for (int i = 0; i < block; ++i) { float v = 0.3f * std::sin (0.03f * (pos + i)); in.setSample (0, i, v); in.setSample (1, i, v); }
                e.process (in, 0, block, wet, snd, s);
                juce::Thread::sleep (1);
                if (each) each (wet.getMagnitude (0, 0, block) > 0.01f ? pos / sr : -1.0);
            }
        };

        { // Hold : phrase de 2 s, 1 répétition, délai 2 s depuis la fin ; Hold activé -> doit tourner en boucle
            PhraseEngine e; e.prepare (sr, block);
            PhraseEngine::Settings s; s.captureMode = PhraseEngine::Cycle; s.phraseSeconds = 2.0; s.delaySeconds = 2.0;
            s.delayFromEnd = true; s.repetitions = 1; s.silenceMin = s.silenceMax = 3.0;
            double lastOn = -1; int captures = 0; bool wasRec = false;
            run (e, s, 0, 3.0, nullptr);
            s.hold = true;
            run (e, s, 3.0, 20.0, [&] (double t) {
                if (t >= 0) lastOn = t;
                const bool rec = e.captureState.load() == PhraseEngine::CapRecording;
                if (rec && ! wasRec) ++captures; wasRec = rec; });
            printf ("Hold : son jusqu'à %.1f s sur 20 s, nouvelles captures pendant Hold : %d (attendu ~20 s et 0)\n", lastOn, captures);
            e.release();
        }
        { // réglages par emplacement + import : banque de 2 remplie par import, emplacement 2 en 3 répétitions
            PhraseEngine e; e.prepare (sr, block);
            PhraseEngine::Settings s; s.captureMode = PhraseEngine::Bank; s.bankSize = 2; s.repetitions = 1;
            s.silenceMin = s.silenceMax = 1.0; s.slotReps[1] = 3; s.slotTranspose[1] = 5; s.hold = false;
            juce::AudioBuffer<float> clip (2, (int) sr);
            for (int i = 0; i < (int) sr; ++i) { const float v = 0.4f * std::sin (6.2831853f * 220.0f * i / (float) sr); clip.setSample (0, i, v); clip.setSample (1, i, v); }
            e.setSequence ("2 1");
            e.queueImport (0, clip, sr);
            e.queueImport (1, clip, sr * 2.0); // fréquence d'échantillonnage différente : doit être convertie
            int reps2 = 0, reps1 = 0; bool was = false;
            run (e, s, 0, 14.0, [&] (double t) {
                const bool on = t >= 0;
                if (on && ! was) { if (e.bankPlayingSlot.load() == 1) ++reps2; else ++reps1; }
                was = on; });
            printf ("import + réglages par emplacement : longueurs %.2f s / %.2f s (attendu 1.00 / 0.50), relectures C2=%d C1=%d\n",
                    e.bankLengthView[0].load(), e.bankLengthView[1].load(), reps2, reps1);
            juce::AudioBuffer<float> back;
            printf ("export emplacement 1 : %s, %d échantillons\n", e.copyBankAudio (0, back) ? "ok" : "ÉCHEC", back.getNumSamples());
            e.release();
        }
    }
    { // 12. v2 : sauvegarde de la banque dans l'état du plugin (FLAC) et rechargement
        juce::ScopedJuceInitialiser_GUI gui;
        PalindromeProcessor a;
        a.setPlayConfigDetails (2, 2, sr, block);
        a.prepareToPlay (sr, block);
        juce::AudioBuffer<float> clip (2, (int) (sr * 1.5));
        for (int i = 0; i < clip.getNumSamples(); ++i) { const float v = 0.4f * std::sin (6.2831853f * 330.0f * i / (float) sr); clip.setSample (0, i, v); clip.setSample (1, i, -v); }
        a.engine.queueImport (2, clip, sr);
        juce::AudioBuffer<float> io (2, block); juce::MidiBuffer midi;
        for (int b = 0; b < 4; ++b) { io.clear(); a.processBlock (io, midi); }
        juce::MemoryBlock state; a.getStateInformation (state);

        PalindromeProcessor b2;
        b2.setPlayConfigDetails (2, 2, sr, block);
        b2.setStateInformation (state.getData(), (int) state.getSize());
        b2.prepareToPlay (sr, block);
        for (int b = 0; b < 4; ++b) { io.clear(); b2.processBlock (io, midi); }
        juce::AudioBuffer<float> back;
        const bool ok = b2.engine.copyBankAudio (2, back);
        float err = 0; if (ok) for (int i = 0; i < back.getNumSamples(); ++i) err = juce::jmax (err, std::abs (back.getSample (1, i) - clip.getSample (1, i)));
        printf ("état sauvegardé : %.0f Ko ; banque rechargée : %s, %.2f s, erreur max %.5f\n",
                state.getSize() / 1024.0, ok ? "ok" : "ÉCHEC", back.getNumSamples() / sr, err);

        // presets d'usine
        b2.setCurrentProgram (3);
        printf ("preset « %s » : répétitions %.0f, saturation %.0f %%\n", b2.getProgramName (3).toRawUTF8(),
                b2.param (ParamIDs::repetitions), b2.param (ParamIDs::drive));
    }
    { // 13. v2.2 : Hold pendant un silence, séquence de sens, verrous
        auto run = [&] (PhraseEngine& e, PhraseEngine::Settings& s, double fromSec, double toSec, std::function<void(double)> each)
        {
            juce::AudioBuffer<float> in (2, block), wet (2, block), snd (2, block);
            for (int pos = (int) (fromSec * sr); pos < (int) (toSec * sr); pos += block)
            {
                for (int i = 0; i < block; ++i) { float v = 0.3f * std::sin (0.03f * (pos + i)); in.setSample (0, i, v); in.setSample (1, i, v); }
                e.process (in, 0, block, wet, snd, s);
                juce::Thread::sleep (1);
                if (each) each (wet.getMagnitude (0, 0, block) > 0.01f ? pos / sr : -1.0);
            }
        };
        {
            PhraseEngine e; e.prepare (sr, block);
            PhraseEngine::Settings s; s.captureMode = PhraseEngine::Cycle; s.phraseSeconds = 2.0; s.delaySeconds = 2.0;
            s.delayFromEnd = true; s.repetitions = 1; s.silenceMin = s.silenceMax = 10.0;
            double firstAfter = -1, lastOn = -1; int captures = 0; bool wasRec = false;
            run (e, s, 0, 8.0, nullptr);       // capture 0-2, répétition 4-6, silence ensuite
            s.hold = true;
            run (e, s, 8.0, 20.0, [&] (double t) {
                if (t >= 0) { if (firstAfter < 0) firstAfter = t; lastOn = t; }
                const bool rec = e.captureState.load() == PhraseEngine::CapRecording;
                if (rec && ! wasRec) ++captures; wasRec = rec; });
            printf ("Hold pendant le silence (à 8 s) : reprise à %.2f s, son jusqu'à %.1f s, captures %d (attendu ~8.5, ~20, 0)\n", firstAfter, lastOn, captures);
            e.release();
        }
        {
            PhraseEngine e; e.prepare (sr, block);
            PhraseEngine::Settings s; s.captureMode = PhraseEngine::Cycle; s.phraseSeconds = 1.0; s.delaySeconds = 1.0;
            s.delayFromEnd = true; s.repetitions = 4; s.order = PhraseEngine::TypedSequence; s.silenceMin = s.silenceMax = 30.0;
            e.setDirectionSequence ("R - x N");
            run (e, s, 0, 1.2, nullptr);
            juce::String seen;
            for (auto& v : e.passViews) if (v.active.load()) seen << (v.reverse.load() ? "R" : "N") << "@" << juce::String (v.startIn.load() + 1.2f, 1) << " ";
            printf ("séquence « R - x N » : %s(attendu R@2.0 R@4.0 N@4.0 N@5.0)\n", seen.toRawUTF8());
            e.release();
        }
        {
            PhraseEngine e; e.prepare (sr, block);
            PhraseEngine::Settings s; s.captureMode = PhraseEngine::Bank; s.bankSize = 2; s.repetitions = 1; s.targetSlot = 0;
            s.silenceMin = s.silenceMax = 1.0;
            juce::AudioBuffer<float> clip (2, (int) sr);
            for (int i = 0; i < (int) sr; ++i) { const float v = 0.4f * std::sin (0.05f * i); clip.setSample (0, i, v); clip.setSample (1, i, v); }
            e.queueImport (0, clip, sr); e.queueImport (1, clip, sr);
            run (e, s, 0, 0.1, nullptr);
            e.setSlotLocked (0, true);
            e.requestCapture();
            run (e, s, 0.1, 0.2, nullptr);
            const int target = e.captureTarget.load();
            e.clearAll();
            run (e, s, 0.2, 0.3, nullptr);
            printf ("verrou emplacement 1 : recapture redirigée vers %d (attendu 2) ; après Vider : 1 = %.1f s, 2 = %.1f s (attendu 1.0 / 0.0)\n",
                    target + 1, e.bankLengthView[0].load(), e.bankLengthView[1].load());
            e.release();
        }
    }
    { // 14. v2.2 : MIDI learn, Vider en fondu, tap tempo, annuler / A-B
        juce::ScopedJuceInitialiser_GUI gui;
        PalindromeProcessor a;
        a.forgetAllMidiMappings();
        a.setPlayConfigDetails (2, 2, sr, block);
        a.prepareToPlay (sr, block);
        auto setP = [&] (const char* id, float v) { auto* p = a.apvts.getParameter (id); p->setValueNotifyingHost (p->convertTo0to1 (v)); };
        auto pump = [&] (int ms) { for (int k = 0; k < ms / 33; ++k) a.serviceMessageThread(); };
        juce::AudioBuffer<float> io (2, block);
        setP (ParamIDs::captureMode, 2.0f);     // manuelle
        setP (ParamIDs::phraseLen, 1.0f); setP (ParamIDs::delay, 1.0f); setP (ParamIDs::mix, 100.0f);
        setP (ParamIDs::randomness, 0.0f);

        a.startMidiLearn (ParamIDs::hold);
        { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0); io.clear(); a.processBlock (io, m); }
        { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0); io.clear(); a.processBlock (io, m); }
        printf ("MIDI learn Hold : « %s » (attendu CC 64)", a.getMidiMappingText (ParamIDs::hold).toRawUTF8());
        { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0); m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 10); io.clear(); a.processBlock (io, m); }
        pump (100);
        printf (" ; pédale -> Hold %s", a.param (ParamIDs::hold) > 0.5f ? "activé" : "ÉCHEC");
        { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0); io.clear(); a.processBlock (io, m); }
        pump (100);
        printf (", 2e appui -> %s\n", a.param (ParamIDs::hold) < 0.5f ? "désactivé" : "ÉCHEC");

        a.startMidiLearn (MidiActions::capture);
        { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); io.clear(); a.processBlock (io, m); }
        for (int b = 0; b < 4; ++b) { io.clear(); juce::MidiBuffer m; a.processBlock (io, m); }
        { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); io.clear(); a.processBlock (io, m); }
        printf ("note C3 apprise -> capture : %s\n", a.engine.captureState.load() == PhraseEngine::CapRecording ? "ok" : "ÉCHEC");

        // la capture enregistre 1 s de sinus, puis on attend la relecture et on vide en pleine lecture
        int t = 0;
        auto blockAt = [&] (bool input)
        {
            io.clear();
            if (input) for (int i = 0; i < block; ++i) { const float v = 0.5f * std::sin (6.2831853f * 220.0f * (t + i) / (float) sr); io.setSample (0, i, v); io.setSample (1, i, v); }
            juce::MidiBuffer m; a.processBlock (io, m); t += block; juce::Thread::sleep (1);
        };
        while (t < (int) (1.0 * sr)) blockAt (true);
        while (t < (int) (2.0 * sr)) blockAt (false);
        const float before = io.getMagnitude (0, 0, block);
        a.requestClear();
        std::vector<float> outEnv;
        float maxStep = 0.0f, prev = io.getSample (0, block - 1);
        for (int b = 0; b < 8; ++b)
        {
            blockAt (false);
            for (int i = 0; i < block; ++i) { maxStep = std::max (maxStep, std::abs (io.getSample (0, i) - prev)); prev = io.getSample (0, i); }
            outEnv.push_back (io.getMagnitude (0, 0, block));
        }
        printf ("Vider en fondu : niveau avant %.3f, puis par bloc :", before);
        for (auto v : outEnv) printf (" %.3f", v);
        printf (" ; plus grand saut %.3f (sinus 220 Hz : %.3f)\n", maxStep, 0.5f * 6.2831853f * 220.0f / (float) sr);

        // tap tempo à 100 BPM
        for (int k = 0; k < 4; ++k) { a.tapTempo(); juce::Thread::sleep (600); }
        pump (150);
        printf ("tap tempo (600 ms) : %.1f BPM, source %s\n", a.param (ParamIDs::internalBpm), a.param (ParamIDs::tempoSource) > 0.5f ? "interne" : "ÉCHEC");

        // annuler / rétablir
        setP (ParamIDs::randomness, 30.0f); pump (800);
        setP (ParamIDs::randomness, 80.0f); pump (800);
        a.undo();
        const float u = a.param (ParamIDs::randomness);
        a.redo();
        printf ("annuler : %.0f (attendu 30), rétablir : %.0f (attendu 80)\n", u, a.param (ParamIDs::randomness));
        a.selectAB (1); setP (ParamIDs::randomness, 10.0f);
        a.selectAB (0); const float ra = a.param (ParamIDs::randomness);
        a.selectAB (1); const float rb = a.param (ParamIDs::randomness);
        printf ("A/B : A = %.0f (attendu 80), B = %.0f (attendu 10)\n", ra, rb);
        a.forgetAllMidiMappings();
    }
    return 0;
}
