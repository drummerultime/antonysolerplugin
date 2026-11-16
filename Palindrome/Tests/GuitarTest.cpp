// Test du découpage legato et du mode accords sur une guitare synthétique (Karplus-Strong).
#include "../Source/PhraseDSP.h"
#include <cstdio>
#include <chrono>

static double midiHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }

// Corde pincée : ligne à retard fractionnaire + filtre moyenneur ; la hauteur peut changer sans nouvelle attaque.
struct KSString
{
    std::vector<float> line;
    int w = 0;
    double sr;
    float last = 0.0f;
    juce::Random rng { 7 };
    explicit KSString (double rate) : line (8192, 0.0f), sr (rate) {}

    void pluck (double hz, float amp)
    {
        const int P = (int) (sr / hz);
        float lp = 0.0f;
        for (int i = 0; i < P; ++i)
        {
            lp = 0.5f * lp + 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
            line[(size_t) ((w - P + i + 8192) % 8192)] += amp * lp;
        }
    }
    float tick (double hz)
    {
        const double P = sr / hz;
        const double rp = w - P;
        const int i0 = (int) std::floor (rp);
        const float fr = (float) (rp - i0);
        const float a = line[(size_t) ((i0 + 8192) % 8192)], b = line[(size_t) ((i0 + 1 + 8192) % 8192)];
        const float d = a + fr * (b - a);
        const float y = 0.996f * 0.5f * (d + last);
        last = d;
        line[(size_t) w] = y;
        w = (w + 1) % 8192;
        return y;
    }
};

static std::vector<double> peaksOf (const float* x, int len, double sr)
{
    const int N = 16384;
    juce::dsp::FFT fft (14);
    std::vector<float> b ((size_t) N * 2, 0.0f);
    const int use = std::min (len, N);
    for (int i = 0; i < use; ++i) b[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (6.2831853f * i / (use - 1)));
    fft.performFrequencyOnlyForwardTransform (b.data(), true);
    float mx = 0; for (int k = 10; k < N / 4; ++k) mx = std::max (mx, b[(size_t) k]);
    std::vector<std::pair<float, double>> pk;
    for (int k = (int) (70.0 * N / sr); k < (int) (1000.0 * N / sr); ++k)
        if (b[(size_t) k] > 0.2f * mx && b[(size_t) k] > b[(size_t) k - 1] && b[(size_t) k] >= b[(size_t) k + 1] && b[(size_t) k] > b[(size_t) k - 2] && b[(size_t) k] >= b[(size_t) k + 2])
        {
            const double a = std::log (b[(size_t) k - 1]), c = std::log (b[(size_t) k + 1]), m = std::log (b[(size_t) k]);
            pk.push_back ({ b[(size_t) k], (k + 0.5 * (a - c) / (a - 2 * m + c)) * sr / N });
        }
    std::sort (pk.begin(), pk.end(), [] (auto& l, auto& r) { return l.first > r.first; });
    std::vector<double> f; for (size_t i = 0; i < std::min<size_t> (5, pk.size()); ++i) f.push_back (pk[i].second);
    std::sort (f.begin(), f.end());
    return f;
}

static float maxJump (const juce::AudioBuffer<float>& b, int n)
{
    // plus grand saut d'un échantillon à l'autre au-delà de ce que prédit la pente (détecteur de clics)
    const float* x = b.getReadPointer (0);
    float m = 0.0f;
    for (int i = 2; i < n; ++i) m = std::max (m, std::abs (x[i] - 2 * x[i - 1] + x[i - 2]));
    return m;
}

int main()
{
    const double sr = 48000.0;
    int failures = 0;
    auto check = [&] (bool ok, const char* what) { printf ("  [%s] %s\n", ok ? " OK " : "FAIL", what); if (! ok) ++failures; };

    //== 1. phrase legato ======================================================
    {
        printf ("1. Ligne legato (2 attaques, 3 changements sans attaque)\n");
        struct Ev { double t; double midi; bool pick; };
        const Ev evs[] = { { 0.00, 57, true }, { 0.60, 59, false }, { 1.00, 60, false }, { 1.40, 64, true }, { 2.00, 62, false }, { 2.60, 0, false } };
        const int n = (int) (2.6 * sr);
        juce::AudioBuffer<float> src (2, n);
        KSString str (sr);
        int e = 0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            if (e < 5 && t >= evs[e].t) { if (evs[e].pick) str.pluck (midiHz (evs[e].midi), 0.8f); ++e; }
            const float v = str.tick (midiHz (evs[e - 1].midi));
            src.setSample (0, i, v); src.setSample (1, i, v);
        }

        for (float lg : { 0.0f, 0.5f, 0.8f })
        {
            auto on = pal::detectOnsets (src.getReadPointer (0), n, sr, 0.5f, lg);
            printf ("   legato %.0f %% -> %zu notes :", lg * 100, on.size());
            for (auto o : on) printf (" %.3f", o / sr);
            printf ("\n");
            if (lg >= 0.5f)
            {
                bool all = on.size() == 5;
                for (int k = 0; k < 5 && all; ++k) all = std::abs (on[(size_t) k] / sr - evs[k].t) < 0.045;
                check (all, "5 notes trouvées à moins de 45 ms");
            }
            else check (on.size() <= 3, "sans legato : surtout les attaques");
        }

        // coupe au passage par zéro
        auto on = pal::detectOnsets (src.getReadPointer (0), n, sr, 0.5f, 0.5f);
        bool zc = true;
        for (size_t k = 1; k < on.size(); ++k)
        {
            const float a = src.getSample (0, on[k] - 1), b = src.getSample (0, on[k]);
            zc = zc && ((a <= 0 && b > 0) || (a >= 0 && b < 0));
        }
        check (zc, "toutes les coupes tombent sur un passage par zéro");

        pal::RenderParams p; p.sampleRate = sr; p.transpose = 3; p.legato = 0.5f;
        juce::AudioBuffer<float> dst;
        pal::RenderInfo info;
        pal::renderPhrase (src, n, p, dst, &info);
        int polys = 0; for (auto v : info.notePoly) polys += v;
        check (polys == 0, "notes seules reconnues comme notes (pas accords)");

        // clics : désordre total, comparé au signal d'origine
        p.transpose = 0; p.randomAmount = 1.0f; p.seed = 5;
        pal::renderPhrase (src, n, p, dst, &info);
        const float jIn = maxJump (src, n), jOut = maxJump (dst, n);
        printf ("   saut max (dérivée 2nde) : source %.4f, désordre total %.4f\n", jIn, jOut);
        check (jOut < jIn * 2.5f, "pas de clic aux coupes avec le désordre");
    }

    //== 2. accords ================================================================
    {
        printf ("2. Accords grattés : Do, Sol, La mineur\n");
        const double chords[3][6] = { { 48, 52, 55, 60, 64, 0 }, { 43, 47, 50, 55, 59, 67 }, { 45, 52, 57, 60, 64, 0 } };
        const double len = 1.2;
        const int n = (int) (3 * len * sr);
        juce::AudioBuffer<float> src (2, n);
        std::vector<KSString> strings; for (int s = 0; s < 6; ++s) strings.emplace_back (sr);
        double cur[6] = { 0 };
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const int c = std::min (2, (int) (t / len));
            const double tc = t - c * len;
            float v = 0.0f;
            for (int s = 0; s < 6; ++s)
            {
                const double m = chords[c][s];
                if (m > 0 && std::abs (tc - s * 0.012) < 0.5 / sr) { cur[s] = m; strings[(size_t) s].pluck (midiHz (m), 0.35f); }
                if (m <= 0 && tc < 1.0 / sr) cur[s] = 0;
                if (cur[s] > 0) v += strings[(size_t) s].tick (midiHz (cur[s]));
            }
            src.setSample (0, i, v); src.setSample (1, i, v);
        }

        auto on = pal::detectOnsets (src.getReadPointer (0), n, sr, 0.5f, 0.5f);
        printf ("   %zu notes :", on.size()); for (auto o : on) printf (" %.3f", o / sr); printf ("\n");
        check (on.size() == 3, "3 accords trouvés (le grattage ne crée pas de fausses notes)");

        pal::RenderParams p; p.sampleRate = sr; p.transpose = 3; p.legato = 0.5f;
        juce::AudioBuffer<float> dst;
        pal::RenderInfo info;
        const auto t0 = std::chrono::steady_clock::now();
        pal::renderPhrase (src, n, p, dst, &info);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        int polys = 0; for (auto v : info.notePoly) polys += v;
        printf ("   reconnus comme accords : %d / %zu (rendu %.0f ms)\n", polys, info.notePoly.size(), ms);
        check (polys == (int) info.notePoly.size(), "mode auto : accords reconnus");

        for (int c = 0; c < 3; ++c)
        {
            const int at = (int) ((c * len + 0.25) * sr);
            auto pin = peaksOf (src.getReadPointer (0) + at, (int) (0.3 * sr), sr);
            auto pout = peaksOf (dst.getReadPointer (0) + at, (int) (0.3 * sr), sr);
            double worst = 0; int matched = 0;
            for (auto fo : pout)
            {
                double best = 1e9;
                for (auto fi : pin) best = std::min (best, std::abs (1200.0 * std::log2 (fo / (fi * std::pow (2.0, 3 / 12.0)))));
                if (best < 30) ++matched;
                worst = std::max (worst, best);
            }
            printf ("   accord %d : %d/%zu partiels à +3 demi-tons (écart max %.0f cents)\n", c, matched, pout.size(), worst);
            check (matched >= (int) pout.size() - 1 && ! pout.empty(), "accord transposé juste");
        }

        // niveau conservé
        const float rin = src.getRMSLevel (0, 0, n), rout = dst.getRMSLevel (0, 0, n);
        printf ("   niveau : source %.3f, transposé %.3f\n", rin, rout);
        check (rout > rin * 0.6f && rout < rin * 1.5f, "niveau conservé");

        // mode forcé "notes" : l'accord passe par le PSOLA (référence) -> doit rester utilisable
        p.playMode = pal::PlayMono;
        pal::renderPhrase (src, n, p, dst, &info);
        polys = 0; for (auto v : info.notePoly) polys += v;
        check (polys == 0, "mode notes forcé : aucun accord");

        // 20 s d'accords
        juce::AudioBuffer<float> big (2, (int) (20 * sr));
        for (int i = 0; i < big.getNumSamples(); ++i) { const float v = src.getSample (0, i % n); big.setSample (0, i, v); big.setSample (1, i, v); }
        p.playMode = pal::PlayPoly; p.transpose = 5;
        const auto t1 = std::chrono::steady_clock::now();
        pal::renderPhrase (big, big.getNumSamples(), p, dst, nullptr);
        const double ms2 = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t1).count();
        printf ("   20 s d'accords transposés : %.0f ms\n", ms2);
        check (ms2 < 900.0, "calcul assez rapide (< 0,9 s pour 20 s, FFT sans vDSP)");

        // cache d'analyse
        pal::PhraseAnalysis cache;
        const auto t2 = std::chrono::steady_clock::now();
        pal::renderPhrase (big, big.getNumSamples(), p, dst, nullptr, &cache, 1);
        const auto t3 = std::chrono::steady_clock::now();
        pal::renderPhrase (big, big.getNumSamples(), p, dst, nullptr, &cache, 1);
        const auto t4 = std::chrono::steady_clock::now();
        pal::analysePhrase (big, big.getNumSamples(), sr, p.sensitivity, p.legato, p.playMode, cache, 2, true);
        const auto t5 = std::chrono::steady_clock::now();
        pal::renderPhrase (big, big.getNumSamples(), p, dst, nullptr, &cache, 2);
        printf ("   pré-analysée : rendu %.0f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t5).count());
        printf ("   avec cache : 1er %.0f ms, suivant %.0f ms\n",
                std::chrono::duration<double, std::milli> (t3 - t2).count(), std::chrono::duration<double, std::milli> (t4 - t3).count());
    }

    //== 3. vocodeur sur un sinus : hauteur et niveau =============================
    {
        printf ("3. Vocodeur de phase sur un sinus 220 Hz, +7 demi-tons\n");
        const int n = (int) (1.0 * sr);
        juce::AudioBuffer<float> src (2, n), dst;
        for (int i = 0; i < n; ++i) { const float v = 0.5f * (float) std::sin (2 * M_PI * 220.0 * i / sr); src.setSample (0, i, v); src.setSample (1, i, v); }
        pal::RenderParams p; p.sampleRate = sr; p.transpose = 7; p.playMode = pal::PlayPoly; p.legato = 0.0f;
        pal::renderPhrase (src, n, p, dst, nullptr);
        float conf; const float hz = pal::detectPitchYin (dst.getReadPointer (0) + n / 2, 4000, sr, conf);
        printf ("   %.1f Hz (attendu %.1f), niveau %.3f (source %.3f)\n", hz, 220.0 * std::pow (2.0, 7 / 12.0),
                dst.getRMSLevel (0, n / 4, n / 2), src.getRMSLevel (0, n / 4, n / 2));
        check (std::abs (hz - 329.6f) < 2.0f, "hauteur exacte");
        check (std::abs (dst.getRMSLevel (0, n / 4, n / 2) / src.getRMSLevel (0, n / 4, n / 2) - 1.0f) < 0.15f, "niveau exact");
    }

    printf (failures == 0 ? "\nTOUT EST OK\n" : "\n%d ÉCHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
