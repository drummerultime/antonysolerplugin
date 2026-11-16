#include "PhraseDSP.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace pal
{
//==============================================================================
// Passage par zéro le plus proche (coupe sans clic)
int snapToZeroCrossing (const float* x, int n, int pos, int radius)
{
    if (pos <= 0 || pos >= n - 1) return juce::jlimit (0, juce::jmax (0, n - 1), pos);
    for (int d = 0; d <= radius; ++d)
    {
        for (int sgn = -1; sgn <= 1; sgn += 2)
        {
            const int i = pos + sgn * d;
            if (i <= 0 || i >= n) continue;
            if ((x[i - 1] <= 0.0f && x[i] > 0.0f) || (x[i - 1] >= 0.0f && x[i] < 0.0f))
                return i;
            if (d == 0) break;
        }
    }
    return pos;
}

//==============================================================================
// Détection des attaques :
//  1. flux spectral (log-magnitude) + seuil adaptatif, affiné sur l'enveloppe (notes attaquées)
//  2. nouveauté harmonique (chroma) : repère les changements de note sans nouvelle attaque
//     (legato, hammer-on, pull-off, slide, changement d'accord tenu)
std::vector<int> detectOnsets (const float* x, int n, double sr, float sensitivity, float legato, std::vector<char>* legatoFlags)
{
    std::vector<int> onsets { 0 };
    if (legatoFlags != nullptr) legatoFlags->assign (1, 0);
    std::vector<int> legatoPositions;

    const int order = sr > 64000.0 ? 11 : 10;
    const int N = 1 << order;
    const int hop = N / 4;

    if (n < N * 2)
        return onsets;

    juce::dsp::FFT fft (order);
    std::vector<float> window ((size_t) N), buf ((size_t) N * 2), prev ((size_t) N / 2 + 1, 0.0f);

    for (int i = 0; i < N; ++i)
        window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1));

    const int numFrames = 1 + (n - N) / hop;
    std::vector<float> flux ((size_t) numFrames, 0.0f), rms ((size_t) numFrames, 0.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        const float* frame = x + (size_t) f * (size_t) hop;
        std::fill (buf.begin(), buf.end(), 0.0f);
        double e = 0.0;

        for (int i = 0; i < N; ++i)
        {
            buf[(size_t) i] = frame[i] * window[(size_t) i];
            e += (double) frame[i] * frame[i];
        }

        rms[(size_t) f] = (float) std::sqrt (e / N);
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);

        float s = 0.0f;
        for (int k = 0; k <= N / 2; ++k)
        {
            const float m = std::log1p (100.0f * buf[(size_t) k]);
            const float d = m - prev[(size_t) k];
            if (d > 0.0f) s += d;
            prev[(size_t) k] = m;
        }
        flux[(size_t) f] = (f == 0) ? 0.0f : s;
    }

    const float maxFlux = *std::max_element (flux.begin(), flux.end());
    const float maxRms  = *std::max_element (rms.begin(), rms.end());
    if (maxFlux <= 0.0f || maxRms <= 1.0e-6f)
        return onsets;

    for (auto& v : flux) v /= maxFlux;

    const float s = juce::jlimit (0.0f, 1.0f, sensitivity);
    const float delta = juce::jmap (s, 0.30f, 0.02f);
    const int minGapSamples = (int) (0.06 * sr);                 // notes de 60 ms minimum
    const float gate = maxRms * 0.01f;                            // -40 dB sous le max
    const int preRoll = (int) (0.001 * sr);

    // enveloppe ~1 ms pour l'affinage
    const int envHop = juce::jmax (1, (int) (0.001 * sr));
    auto envAt = [&] (int pos)
    {
        float m = 0.0f;
        const int end = juce::jmin (n, pos + envHop);
        for (int i = juce::jmax (0, pos); i < end; ++i) m = juce::jmax (m, std::abs (x[i]));
        return m;
    };

    for (int f = 1; f < numFrames; ++f)
    {
        const int lo = juce::jmax (0, f - 10), hi = juce::jmin (numFrames - 1, f + 10);
        float mean = 0.0f;
        for (int i = lo; i <= hi; ++i) mean += flux[(size_t) i];
        mean /= (float) (hi - lo + 1);

        bool isPeak = true;
        for (int i = juce::jmax (0, f - 3); i <= juce::jmin (numFrames - 1, f + 3); ++i)
            if (flux[(size_t) i] > flux[(size_t) f]) { isPeak = false; break; }

        const bool loudEnough = rms[(size_t) f] > gate || rms[(size_t) juce::jmin (numFrames - 1, f + 2)] > gate;

        if (! (isPeak && loudEnough && flux[(size_t) f] > mean + delta))
            continue;

        // position grossière puis affinage sur l'enveloppe
        int pos = f * hop + N / 2 - hop;
        const int searchStart = juce::jmax (0, pos - hop);
        const int searchEnd   = juce::jmin (n - 1, pos + 2 * hop);

        float peak = 0.0f;
        for (int p = searchStart; p < searchEnd; p += envHop) peak = juce::jmax (peak, envAt (p));

        for (int p = searchStart; p < searchEnd; p += envHop)
            if (envAt (p) >= 0.3f * peak) { pos = p; break; }

        pos = juce::jmax (0, pos - preRoll);

        if (pos - onsets.back() >= minGapSamples && n - pos >= minGapSamples)
            onsets.push_back (pos);
    }

    //== 2. changements harmoniques (legato) =====================================
    const float lg = juce::jlimit (0.0f, 1.0f, legato);
    const int orderC = sr > 64000.0 ? 13 : 12;
    const int Nc = 1 << orderC;
    const int hopC = Nc / 8;
    const int sep = 4;                                   // trames d'écart : fenêtres avant / après sans recouvrement

    if (lg > 0.01f && n >= Nc + hopC * 24)
    {
        juce::dsp::FFT fftC (orderC);
        std::vector<float> winC ((size_t) Nc), bc ((size_t) Nc * 2);
        for (int i = 0; i < Nc; ++i)
            winC[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (Nc - 1));

        // répartition douce de chaque case de fréquence sur les classes de hauteur
        const int binLo = juce::jmax (1, (int) (70.0 * Nc / sr));
        const int binHi = juce::jmin (Nc / 2 - 1, (int) (2200.0 * Nc / sr));
        std::vector<int> pcA ((size_t) Nc / 2, 0);
        std::vector<float> wA ((size_t) Nc / 2, 0.0f);
        for (int b = binLo; b <= binHi; ++b)
        {
            const double midi = 69.0 + 12.0 * std::log2 ((b * sr / Nc) / 440.0);
            const double fl = std::floor (midi);
            pcA[(size_t) b] = (((int) fl % 12) + 12) % 12;
            wA[(size_t) b] = (float) (1.0 - (midi - fl));
        }

        const int F = 1 + (n - Nc) / hopC;
        std::vector<std::array<float, 12>> chroma ((size_t) F);
        std::vector<float> rmsC ((size_t) F, 0.0f);

        for (int f = 0; f < F; ++f)
        {
            const float* frame = x + (size_t) f * (size_t) hopC;
            std::fill (bc.begin(), bc.end(), 0.0f);
            double e = 0.0;
            for (int i = 0; i < Nc; ++i) { bc[(size_t) i] = frame[i] * winC[(size_t) i]; e += (double) frame[i] * frame[i]; }
            rmsC[(size_t) f] = (float) std::sqrt (e / Nc);
            fftC.performFrequencyOnlyForwardTransform (bc.data(), true);

            auto& c = chroma[(size_t) f];
            c.fill (0.0f);
            for (int b = binLo; b <= binHi; ++b)
            {
                const float m = std::sqrt (bc[(size_t) b]);
                const float w = wA[(size_t) b];
                c[(size_t) pcA[(size_t) b]] += m * w;
                c[(size_t) ((pcA[(size_t) b] + 1) % 12)] += m * (1.0f - w);
            }
        }

        const int W = juce::jmax (3, (int) std::round (0.04 * sr / hopC));
        const float gateC = maxRms * 0.04f;
        std::vector<float> nov ((size_t) F, 0.0f);

        // trame f centrée sur le point testé ; A = trames qui finissent avant, B = trames qui commencent après
        for (int f = sep + W; f + sep + W <= F; ++f)
        {
            if (rmsC[(size_t) (f - sep - 1)] < gateC || rmsC[(size_t) (f + sep)] < gateC) continue;
            std::array<float, 12> A {}, B {};
            for (int i = f - sep - W; i < f - sep; ++i)  for (int k = 0; k < 12; ++k) A[(size_t) k] += chroma[(size_t) i][(size_t) k];
            for (int i = f + sep; i < f + sep + W; ++i)  for (int k = 0; k < 12; ++k) B[(size_t) k] += chroma[(size_t) i][(size_t) k];
            double dot = 0.0, na = 0.0, nb = 0.0;
            for (int k = 0; k < 12; ++k) { dot += (double) A[(size_t) k] * B[(size_t) k]; na += (double) A[(size_t) k] * A[(size_t) k]; nb += (double) B[(size_t) k] * B[(size_t) k]; }
            if (na > 0.0 && nb > 0.0) nov[(size_t) f] = (float) (1.0 - dot / std::sqrt (na * nb));
        }

        const float thr = juce::jmap (lg, 0.62f, 0.10f);
       #ifdef PAL_DEBUG_ONSETS
        for (int f = W; f + W <= F; ++f) if (nov[(size_t) f] > 0.05f) printf ("nov %.3f : %.3f\n", (f * hopC + Nc / 2) / sr, nov[(size_t) f]);
       #endif
        const int mergeGap = (int) (0.09 * sr);
        std::vector<int> extra;

        for (int f = W; f + W <= F; ++f)
        {
            if (nov[(size_t) f] < thr) continue;
            bool isPeak = true;
            for (int i = juce::jmax (0, f - sep); i <= juce::jmin (F - 1, f + sep); ++i)
                if (nov[(size_t) i] > nov[(size_t) f] || (juce::exactlyEqual (nov[(size_t) i], nov[(size_t) f]) && i < f)) { isPeak = false; break; }
            if (! isPeak) continue;

            const int pos = f * hopC + Nc / 2;
            if (pos < minGapSamples || n - pos < minGapSamples) continue;

            bool near = false;
            for (int o : onsets) if (std::abs (o - pos) < mergeGap) { near = true; break; }
            if (! near) extra.push_back (pos);
        }

        if (! extra.empty())
        {
            onsets.insert (onsets.end(), extra.begin(), extra.end());
            std::sort (onsets.begin(), onsets.end());
            legatoPositions = extra;
        }
    }

    // coupes calées sur un passage par zéro, écart minimal respecté
    const int zcRadius = juce::jmax (4, (int) (0.002 * sr));
    std::vector<int> out { 0 };
    for (size_t i = 1; i < onsets.size(); ++i)
    {
        const int p = snapToZeroCrossing (x, n, onsets[i], zcRadius);
        if (p - out.back() >= minGapSamples && n - p >= minGapSamples / 2)
        {
            out.push_back (p);
            if (legatoFlags != nullptr)
                legatoFlags->push_back (std::find (legatoPositions.begin(), legatoPositions.end(), onsets[i]) != legatoPositions.end() ? 1 : 0);
        }
    }

    if (out.size() > 256)
    {
        out.resize (256);
        if (legatoFlags != nullptr) legatoFlags->resize (256);
    }

    return out;
}

//==============================================================================
// YIN (de Cheveigné & Kawahara) avec interpolation parabolique.
float detectPitchYin (const float* x, int n, double sr, float& confidence)
{
    confidence = 0.0f;

    const int tauMin = juce::jmax (2, (int) (sr / 1500.0));
    const int W = (int) (sr * 0.025);
    int tauMax = juce::jmin ((int) (sr / 50.0), n - W - 2);

    if (tauMax < tauMin * 2)
        return -1.0f;

    double energy = 0.0;
    for (int j = 0; j < W; ++j) energy += (double) x[j] * x[j];
    if (std::sqrt (energy / W) < 1.0e-4)
        return -1.0f;

    std::vector<float> d ((size_t) tauMax + 1, 0.0f), cmnd ((size_t) tauMax + 1, 1.0f);

    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double sum = 0.0;
        for (int j = 0; j < W; ++j)
        {
            const double diff = (double) x[j] - (double) x[j + tau];
            sum += diff * diff;
        }
        d[(size_t) tau] = (float) sum;
    }

    double running = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += d[(size_t) tau];
        cmnd[(size_t) tau] = running > 0.0 ? (float) (d[(size_t) tau] * tau / running) : 1.0f;
    }

    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
    {
        if (cmnd[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }

    if (best < 0)
    {
        best = tauMin;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cmnd[(size_t) tau] < cmnd[(size_t) best]) best = tau;

        if (cmnd[(size_t) best] > 0.35f)
            return -1.0f; // signal non tonal (percussions, bruit...)
    }

    float refined = (float) best;
    if (best > 1 && best < tauMax)
    {
        const float a = cmnd[(size_t) best - 1], b = cmnd[(size_t) best], c = cmnd[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        if (std::abs (denom) > 1.0e-9f)
            refined += 0.5f * (a - c) / denom;
    }

    confidence = juce::jlimit (0.0f, 1.0f, 1.0f - cmnd[(size_t) best]);
    return (float) (sr / refined);
}

//==============================================================================
juce::StringArray scaleNames()
{
    return { "Off", "Majeur", "Mineur naturel", "Mineur harmonique", juce::String::fromUTF8 ("Mineur mélodique"),
             "Dorien", "Phrygien", "Lydien", "Mixolydien", "Locrien",
             "Pentatonique majeure", "Pentatonique mineure", "Blues", "Par tons", "Chromatique" };
}

bool inScale (int pc, Scale scale)
{
    static const bool table[(int) Scale::NumScales][12] = {
        { 1,1,1,1,1,1,1,1,1,1,1,1 }, // Off
        { 1,0,1,0,1,1,0,1,0,1,0,1 }, // Majeur
        { 1,0,1,1,0,1,0,1,1,0,1,0 }, // Mineur naturel
        { 1,0,1,1,0,1,0,1,1,0,0,1 }, // Mineur harmonique
        { 1,0,1,1,0,1,0,1,0,1,0,1 }, // Mineur mélodique
        { 1,0,1,1,0,1,0,1,0,1,1,0 }, // Dorien
        { 1,1,0,1,0,1,0,1,1,0,1,0 }, // Phrygien
        { 1,0,1,0,1,0,1,1,0,1,0,1 }, // Lydien
        { 1,0,1,0,1,1,0,1,0,1,1,0 }, // Mixolydien
        { 1,1,0,1,0,1,1,0,1,0,1,0 }, // Locrien
        { 1,0,1,0,1,0,0,1,0,1,0,0 }, // Pentatonique majeure
        { 1,0,0,1,0,1,0,1,0,0,1,0 }, // Pentatonique mineure
        { 1,0,0,1,0,1,1,1,0,0,1,0 }, // Blues
        { 1,0,1,0,1,0,1,0,1,0,1,0 }, // Par tons
        { 1,1,1,1,1,1,1,1,1,1,1,1 }  // Chromatique
    };
    const int s = juce::jlimit (0, (int) Scale::NumScales - 1, (int) scale);
    return table[s][((pc % 12) + 12) % 12];
}

float snapToScale (float midi, int root, Scale scale)
{
    if (scale == Scale::Off || midi < 0.0f)
        return midi;

    const int base = (int) std::round (midi);
    float best = midi, bestDist = 1.0e9f;

    for (int c = base - 6; c <= base + 6; ++c)
    {
        if (! inScale (c - root, scale)) continue;
        const float dist = std::abs ((float) c - midi);
        if (dist < bestDist) { bestDist = dist; best = (float) c; }
    }
    return best;
}

int stepInScale (int note, int steps, int root, Scale scale)
{
    if (scale == Scale::Off)
        return note + steps;

    const int dir = steps > 0 ? 1 : -1;
    for (int s = 0; s < std::abs (steps); ++s)
    {
        int guard = 0;
        do { note += dir; } while (! inScale (note - root, scale) && ++guard < 12);
    }
    return note;
}

//==============================================================================
static inline float hermite (const float* s, int len, double pos)
{
    const int i = (int) std::floor (pos);
    const float t = (float) (pos - i);

    auto at = [s, len] (int k) { return s[juce::jlimit (0, len - 1, k)]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);

    const float c0 = x0;
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// Rend une note dans [out, out + L) : lecture (éventuellement inversée) + changement de hauteur.
//  - |shift| <= 1 demi-ton, ou note sans hauteur détectée : rééchantillonnage (façon sampler)
//  - au-delà, note tonale : PSOLA synchrone à la période -> hauteur exacte, durée conservée
// Retourne le nombre d'échantillons de contenu écrits.
// Montée de volume douce (sin²) sur les n premiers échantillons
static void applySwell (float* d, int length, int attack)
{
    const int a = juce::jmin (attack, length);
    for (int j = 0; j < a; ++j)
    {
        const float x = std::sin (juce::MathConstants<float>::halfPi * (float) j / (float) a);
        d[j] *= x * x;
    }
}

//==============================================================================
// Étirement temporel WSOLA (hauteur conservée) : x (m échantillons) -> out (outLen échantillons)
static void wsolaStretch (const float* x, int m, float* out, int outLen, double sr,
                          std::vector<float>& acc, std::vector<float>& wacc)
{
    const int N = juce::jmax (64, ((int) (0.03 * sr)) & ~1);
    const int Hs = N / 2;
    const int tol = juce::jmax (8, (int) (0.008 * sr));
    const double alpha = (double) outLen / (double) juce::jmax (1, m);

    acc.assign ((size_t) (outLen + N), 0.0f);
    wacc.assign ((size_t) (outLen + N), 0.0f);

    int prevPos = 0;
    for (int k = 0; ; ++k)
    {
        const int outPos = k * Hs;
        if (outPos >= outLen) break;

        const int nominal = (int) (outPos / alpha);
        int best = nominal;

        if (k > 0)
        {
            const int natural = prevPos + Hs;   // ce qui suivrait naturellement la trame précédente
            double bestC = -1.0e30;
            for (int d = -tol; d <= tol; d += 2)
            {
                const int cand = nominal + d;
                if (cand < 0 || cand + N > m) continue;
                double c = 0.0;
                for (int j = 0; j < Hs; j += 4)
                {
                    const int a = natural + j;
                    if (a >= m) break;
                    c += (double) x[cand + j] * x[a];
                }
                if (c > bestC) { bestC = c; best = cand; }
            }
        }

        best = juce::jlimit (0, juce::jmax (0, m - 1), best);
        for (int j = 0; j < N; ++j)
        {
            const int si = best + j;
            const float v = si < m ? x[si] : 0.0f;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) j / (float) N);
            acc[(size_t) (outPos + j)] += v * w;
            wacc[(size_t) (outPos + j)] += w;
        }
        prevPos = best;
    }

    for (int n = 0; n < outLen; ++n)
        out[n] = wacc[(size_t) n] > 1.0e-3f ? acc[(size_t) n] / wacc[(size_t) n] : 0.0f;
}

struct Scratch
{
    std::vector<float> tmp, wsum, rs, acc, wacc;
    // vocodeur de phase
    std::vector<float> pvIn, pvOut, pvW, pvBuf, pvMag, pvPhA, pvPrevA, pvPhS, pvPrevS, pvWin;
    std::vector<int> pvPeaks, pvOwner;
};

//==============================================================================
// Vocodeur de phase à verrouillage de phase (Laroche & Dolson) :
// changement de hauteur d'un son polyphonique (accords), durée conservée.
// Étirement temporel d'un facteur "ratio", puis rééchantillonnage.
static int pvPitchShift (const float* x, int m, float* out, int outLen, double ratio, double sr, Scratch& sc)
{
    const int order = sr > 64000.0 ? 12 : 11;
    const int N = 1 << order;
    const int Hs = N / 4;
    const int half = N / 2;
    if (m < 16) return 0;

    juce::dsp::FFT fft (order);

    // entrée avec marges (N/2 devant, N derrière)
    sc.pvIn.assign ((size_t) (m + half + N), 0.0f);
    std::copy (x, x + m, sc.pvIn.begin() + half);

    const int stretched = (int) std::ceil (m * ratio) + 4;
    const int Ly = stretched + N + half;
    sc.pvOut.assign ((size_t) Ly, 0.0f);
    sc.pvW.assign ((size_t) Ly, 0.0f);

    if ((int) sc.pvWin.size() != N)
    {
        sc.pvWin.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            sc.pvWin[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    }
    const auto& win = sc.pvWin;

    const int bins = half + 1;
    sc.pvBuf.assign ((size_t) N * 2, 0.0f);
    sc.pvMag.assign ((size_t) bins, 0.0f);       // |X|²
    sc.pvPrevA.assign ((size_t) bins * 2, 0.0f); // X de la trame d'analyse précédente (complexe)
    sc.pvPrevS.assign ((size_t) bins * 2, 0.0f); // Y de la trame de synthèse précédente (complexe)
    sc.pvPhS.assign ((size_t) bins * 2, 0.0f);   // rotation (complexe unitaire) par pic
    sc.pvOwner.assign ((size_t) bins, 0);

    const double twoPi = juce::MathConstants<double>::twoPi;
    auto wrap = [twoPi] (double a) { return a - twoPi * std::round (a / twoPi); };

    int prevA = 0;
    const int inLen = (int) sc.pvIn.size();
    auto& buf = sc.pvBuf;

    for (int k = 0; ; ++k)
    {
        const int sPos = k * Hs;
        const int aPos = (int) std::llround ((double) sPos / ratio);
        if (sPos + N > Ly || aPos + N > inLen) break;
        if (aPos > m + half) break;

        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = sc.pvIn[(size_t) (aPos + i)] * win[(size_t) i];
        fft.performRealOnlyForwardTransform (buf.data(), true);

        float maxMag = 0.0f;
        for (int b = 0; b < bins; ++b)
        {
            const float re = buf[(size_t) (2 * b)], im = buf[(size_t) (2 * b + 1)];
            sc.pvMag[(size_t) b] = re * re + im * im;
            maxMag = juce::jmax (maxMag, sc.pvMag[(size_t) b]);
        }

        // pics spectraux
        sc.pvPeaks.clear();
        if (k > 0 && maxMag > 1.0e-18f)
        {
            const float floor = maxMag * 1.0e-8f;
            for (int b = 2; b < bins - 2; ++b)
            {
                const float v = sc.pvMag[(size_t) b];
                if (v > floor && v > sc.pvMag[(size_t) (b - 1)] && v >= sc.pvMag[(size_t) (b + 1)]
                    && v > sc.pvMag[(size_t) (b - 2)] && v >= sc.pvMag[(size_t) (b + 2)])
                    sc.pvPeaks.push_back (b);
            }
        }

        if (! sc.pvPeaks.empty())
        {
            const double Ha = (double) juce::jmax (1, aPos - prevA);
            const size_t np = sc.pvPeaks.size();

            // zones d'influence : du creux précédent au creux suivant
            int from = 0;
            for (size_t i = 0; i < np; ++i)
            {
                int to = bins;
                if (i + 1 < np)
                {
                    const int a = sc.pvPeaks[i], c = sc.pvPeaks[i + 1];
                    int low = a; float lowV = sc.pvMag[(size_t) a];
                    for (int t = a + 1; t < c; ++t) if (sc.pvMag[(size_t) t] < lowV) { lowV = sc.pvMag[(size_t) t]; low = t; }
                    to = low;
                }
                for (int b = from; b < to; ++b) sc.pvOwner[(size_t) b] = (int) i;
                from = to;
            }

            // nouvelle phase de chaque pic -> rotation à appliquer à toute sa zone
            for (size_t i = 0; i < np; ++i)
            {
                const int p = sc.pvPeaks[i];
                const float xr = buf[(size_t) (2 * p)], xi = buf[(size_t) (2 * p + 1)];
                const float pr = sc.pvPrevA[(size_t) (2 * p)], pim = sc.pvPrevA[(size_t) (2 * p + 1)];
                const double omega = twoPi * p / N;
                const double adv = std::atan2 ((double) xi * pr - (double) xr * pim, (double) xr * pr + (double) xi * pim);
                const double inst = omega + wrap (adv - omega * Ha) / Ha;
                const double prevS = std::atan2 ((double) sc.pvPrevS[(size_t) (2 * p + 1)], (double) sc.pvPrevS[(size_t) (2 * p)]);
                const double rot = prevS + inst * Hs - std::atan2 ((double) xi, (double) xr);
                sc.pvPhS[2 * i]     = (float) std::cos (rot);
                sc.pvPhS[2 * i + 1] = (float) std::sin (rot);
            }

            // mémorise X, puis Y = X · rotation
            for (int b = 0; b < bins; ++b)
            {
                const float xr = buf[(size_t) (2 * b)], xi = buf[(size_t) (2 * b + 1)];
                sc.pvPrevA[(size_t) (2 * b)] = xr; sc.pvPrevA[(size_t) (2 * b + 1)] = xi;
                const size_t o = (size_t) sc.pvOwner[(size_t) b];
                const float cr = sc.pvPhS[2 * o], ci = sc.pvPhS[2 * o + 1];
                const float yr = xr * cr - xi * ci, yi = xr * ci + xi * cr;
                buf[(size_t) (2 * b)] = yr; buf[(size_t) (2 * b + 1)] = yi;
                sc.pvPrevS[(size_t) (2 * b)] = yr; sc.pvPrevS[(size_t) (2 * b + 1)] = yi;
            }
        }
        else
        {
            // première trame (ou silence) : phases d'origine
            for (int b = 0; b < 2 * bins; ++b) { sc.pvPrevA[(size_t) b] = buf[(size_t) b]; sc.pvPrevS[(size_t) b] = buf[(size_t) b]; }
        }

        fft.performRealOnlyInverseTransform (buf.data());

        for (int i = 0; i < N; ++i)
        {
            const float w = win[(size_t) i];
            sc.pvOut[(size_t) (sPos + i)] += buf[(size_t) i] * w;
            sc.pvW[(size_t) (sPos + i)] += w * w;
        }
        prevA = aPos;
    }

    // normalisation, puis y[t] = sortie[t + N/2]
    for (int i = 0; i < Ly; ++i)
        sc.pvOut[(size_t) i] = sc.pvW[(size_t) i] > 1.0e-3f ? sc.pvOut[(size_t) i] / sc.pvW[(size_t) i] : 0.0f;

    const float* y = sc.pvOut.data() + half;
    const int yLen = juce::jmin (stretched, Ly - half);

    int content = 0;
    for (int j = 0; j < outLen; ++j)
    {
        const double sp = j * ratio;
        if (sp > yLen - 2) break;
        out[j] = hermite (y, yLen, sp);
        content = j + 1;
    }
    return content;
}

// Rend une note dans [out, out + L) : lecture (éventuellement inversée) + changement de hauteur.
//  - |shift| <= 1 demi-ton : rééchantillonnage (le plus propre)
//  - accord (note polyphonique) : vocodeur de phase verrouillé
//  - note tonale, |shift| <= 7 : PSOLA synchrone à la période (hauteur exacte, durée conservée)
//  - au-delà ou son non tonal : rééchantillonnage + étirement WSOLA (durée conservée)
// La source peut déborder de "tail" échantillons après la note (lecture normale) ou avant elle
// (lecture inversée) : ce débord sert au fondu enchaîné avec la note suivante.
static int renderNote (const float* chan, int chanLen, int noteStart, int noteLen, int tail,
                       float* out, int L, float shiftSemis, bool reversed, bool poly,
                       int fadeLen, double periodSamples, double sr, Scratch& sc, int attack)
{
    if (noteLen < 4 || L <= 0) return 0;

    const int after  = reversed ? 0 : juce::jmin (tail, chanLen - (noteStart + noteLen));
    const int before = reversed ? juce::jmin (tail, noteStart) : 0;
    const int srcLen = noteLen + after + before;
    const float* src = chan + (noteStart - before);
    const int Lx = L + juce::jmax (0, tail);

    auto& tmp = sc.tmp;
    tmp.resize ((size_t) srcLen);
    for (int i = 0; i < srcLen; ++i)
        tmp[(size_t) i] = reversed ? src[srcLen - 1 - i] : src[i];

    const double ratio = std::pow (2.0, shiftSemis / 12.0);
    int content = 0;

    if (std::abs (shiftSemis) <= 1.01f)
    {
        content = ratio <= 1.0 ? juce::jmin (Lx, (int) ((srcLen - 1) / ratio))
                               : juce::jmin (Lx, (int) ((srcLen - 2) / ratio));
        for (int j = 0; j < content; ++j)
            out[j] = hermite (tmp.data(), srcLen, j * ratio);
    }
    else if (poly)
    {
        content = pvPitchShift (tmp.data(), srcLen, out, juce::jmin (Lx, srcLen), ratio, sr, sc);
    }
    else if (periodSamples >= 8.0 && std::abs (shiftSemis) <= 7.01f && periodSamples * 4.0 <= srcLen)
    {
        content = juce::jmin (Lx, srcLen);
        const double P = periodSamples;
        const double Q = P / ratio;
        const int H = (int) std::ceil (P);
        const bool normalise = Q <= P;

        std::fill (out, out + content, 0.0f);
        sc.wsum.assign ((size_t) content, 0.0f);

        const int lastMark = (int) ((srcLen - 1) / P);
        for (double t = 0.0; t < content; t += Q)
        {
            const int k = juce::jlimit (0, lastMark, (int) std::round (t / P));
            const double ca = k * P;
            const int ct = (int) std::round (t);

            for (int j = -H; j <= H; ++j)
            {
                const int n = ct + j;
                if (n < 0 || n >= content) continue;
                const double sp = ca + j;
                if (sp < 0.0 || sp > srcLen - 1) continue;
                const float w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) j / (float) H);
                out[n] += w * hermite (tmp.data(), srcLen, sp);
                sc.wsum[(size_t) n] += w;
            }
        }

        for (int n = 0; n < content; ++n)
            out[n] = normalise ? (sc.wsum[(size_t) n] > 0.05f ? out[n] / sc.wsum[(size_t) n] : 0.0f)
                               : out[n] * (float) (Q / P);
    }
    else
    {
        // 1. rééchantillonnage (hauteur exacte, durée / ratio)
        const int m = ratio <= 1.0 ? (int) ((srcLen - 1) / ratio) : (int) ((srcLen - 2) / ratio);
        if (m < 8) return 0;
        sc.rs.resize ((size_t) m);
        for (int j = 0; j < m; ++j)
            sc.rs[(size_t) j] = hermite (tmp.data(), srcLen, j * ratio);

        // 2. étirement pour retrouver la durée d'origine
        content = juce::jmin (Lx, srcLen);
        wsolaStretch (sc.rs.data(), m, out, content, sr, sc.acc, sc.wacc);
    }

    // fondus en sin² : fondu entrant au début, fondu sortant centré sur la fin de la place de la note
    // (il chevauche le fondu entrant de la note suivante -> enchaînement à niveau constant)
    const int f = juce::jlimit (1, juce::jmax (1, content / 2), fadeLen);
    const int fadeOutEnd = juce::jmin (content, L + juce::jmax (0, tail));
    const int fadeOutStart = juce::jmax (0, fadeOutEnd - f);
    for (int j = 0; j < content; ++j)
    {
        float g = 1.0f;
        if (j < f)
        {
            const float v = std::sin (juce::MathConstants<float>::halfPi * (float) j / (float) f);
            g = v * v;
        }
        if (j >= fadeOutStart)
        {
            const float v = j >= fadeOutEnd ? 0.0f
                          : std::cos (juce::MathConstants<float>::halfPi * (float) (j - fadeOutStart) / (float) f);
            g = juce::jmin (g, v * v);
        }
        out[j] *= g;
    }
    content = juce::jmin (content, fadeOutEnd);

    if (attack > 0)
        applySwell (out, content, attack);

    return content;
}

//==============================================================================
// Analyse spectrale fine : note la plus grave + présence de notes non harmoniques (accord)
static float spectralBass (const float* x, int len, double sr, bool& inharmonic, int& numPeaks)
{
    inharmonic = false;
    numPeaks = 0;
    const int order = sr > 64000.0 ? 14 : 13;
    const int N = 1 << order;
    const int use = juce::jmin (len, N);
    if (use < 256) return -1.0f;

    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < use; ++i)
        buf[(size_t) i] = x[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (use - 1)));
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);

    const int lo = juce::jmax (2, (int) (55.0 * N / sr)), hi = juce::jmin (N / 2 - 2, (int) (2500.0 * N / sr));
    float maxMag = 0.0f;
    for (int b = lo; b <= hi; ++b) maxMag = juce::jmax (maxMag, buf[(size_t) b]);
    if (maxMag <= 1.0e-9f) return -1.0f;

    struct Peak { double f; float m; };
    std::vector<Peak> peaks;
    for (int b = lo; b <= hi; ++b)
    {
        const float v = buf[(size_t) b];
        if (v < maxMag * 0.08f) continue;
        if (! (v > buf[(size_t) (b - 1)] && v >= buf[(size_t) (b + 1)] && v > buf[(size_t) (b - 2)] && v >= buf[(size_t) (b + 2)])) continue;
        const double a = std::log (buf[(size_t) (b - 1)] + 1.0e-12), c = std::log (buf[(size_t) (b + 1)] + 1.0e-12), m = std::log (v + 1.0e-12);
        const double den = a - 2.0 * m + c;
        const double off = std::abs (den) > 1.0e-12 ? 0.5 * (a - c) / den : 0.0;
        peaks.push_back ({ (b + juce::jlimit (-0.5, 0.5, off)) * sr / N, v });
    }
    if (peaks.empty()) return -1.0f;

    if (peaks.size() > 16)
    {
        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.m > b.m; });
        peaks.resize (16);
    }
    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.f < b.f; });
    numPeaks = (int) peaks.size();

    const double L = peaks.front().f;
    auto nonHarmonic = [&] (double base)
    {
        int count = 0;
        for (auto& pk : peaks)
        {
            if (pk.m < maxMag * 0.25f) continue;
            const double h = pk.f / base;
            const double hr = juce::jmax (1.0, std::round (h));
            const double cents = 1200.0 * std::log2 (pk.f / (hr * base));
            if (std::abs (cents) > 35.0) ++count;
        }
        return count;
    };
    inharmonic = juce::jmin (nonHarmonic (L), nonHarmonic (L * 0.5)) >= 1;
    return (float) (69.0 + 12.0 * std::log2 (L / 440.0));
}

// Hauteur et nature (note seule / accord) d'une note.
// playMode : 0 = auto, 1 = notes (mono), 2 = accords (poly)
static void analyseNote (const std::vector<float>& mono, Note& note, double sr, int playMode)
{
    note.analysed = true;
    note.midi = -1.0f;
    note.poly = false;

    const int offset = juce::jmin ((int) (0.03 * sr), note.length / 4);
    const int avail = juce::jmin (note.length - offset, (int) (0.08 * sr));
    float conf = 0.0f;
    const float hz = avail > 0 ? detectPitchYin (mono.data() + note.start + offset, avail, sr, conf) : -1.0f;
    const float yinMidi = (hz > 0.0f) ? 69.0f + 12.0f * std::log2 (hz / 440.0f) : -1.0f;
    note.confidence = conf;

    if (playMode == 1)
    {
        note.midi = conf > 0.6f ? yinMidi : -1.0f;
        return;
    }

    bool inharmonic = false;
    int numPeaks = 0;
    const int specLen = juce::jmin (note.length - offset, (int) (0.17 * sr));
    const float bass = specLen > 0 ? spectralBass (mono.data() + note.start + offset, specLen, sr, inharmonic, numPeaks) : -1.0f;

    if (playMode == 2)
    {
        note.poly = true;
        note.midi = bass;
        return;
    }

    if (yinMidi >= 0.0f && conf > 0.8f && ! inharmonic)
    {
        note.midi = yinMidi;
    }
    else if (bass >= 0.0f && (inharmonic || conf <= 0.6f) && numPeaks >= 2)
    {
        note.poly = true;
        note.midi = bass;
    }
    else
    {
        note.midi = conf > 0.6f ? yinMidi : -1.0f;
    }
}

//==============================================================================
// Texture granulaire : nuage de grains tirés de la phrase source
static void renderTexture (const juce::AudioBuffer<float>& source, int n, const RenderParams& p,
                           juce::AudioBuffer<float>& cloud)
{
    const int numCh = juce::jmin (2, source.getNumChannels());
    cloud.setSize (2, n, false, false, true);
    cloud.clear();

    const double sr = p.sampleRate;
    const int G = juce::jlimit (32, juce::jmax (32, n / 2), (int) (p.grainMs * 0.001 * sr));
    const int count = juce::jmax (1, (int) (p.grainDensity * (double) n / sr));
    const double overlap = (double) count * G / (double) n;
    const float norm = 1.0f / (float) std::sqrt (juce::jmax (1.0, overlap * 0.5));

    juce::Random rng ((juce::int64) (p.seed ^ 0x1234567u));

    for (int g = 0; g < count; ++g)
    {
        const double t = rng.nextDouble() * (n - G);                          // position du grain en sortie
        double readPos = t + (rng.nextDouble() - 0.5) * 2.0 * p.grainSpray * n; // tête de lecture dispersée
        readPos = juce::jlimit (0.0, (double) juce::jmax (0, n - G - 2), readPos);

        float semis = (p.scale == Scale::Off) ? (float) p.transpose : 0.0f;
        if (rng.nextFloat() < p.octaveProb) semis += rng.nextBool() ? 12.0f : -12.0f;
        const double ratio = std::pow (2.0, semis / 12.0);
        const bool rev = p.reverse != (rng.nextFloat() < p.noteReverseProb);
        const float pan = (rng.nextFloat() * 2.0f - 1.0f) * 0.7f;
        const float gains[2] = { std::sqrt (0.5f * (1.0f - pan)) * 1.414f, std::sqrt (0.5f * (1.0f + pan)) * 1.414f };

        const int t0 = (int) t;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* s = source.getReadPointer (juce::jmin (ch, numCh - 1));
            float* d = cloud.getWritePointer (ch);
            for (int j = 0; j < G; ++j)
            {
                const int o = t0 + j;
                if (o >= n) break;
                double sp = rev ? readPos + (G - 1 - j) * ratio : readPos + j * ratio;
                if (sp < 0.0 || sp > n - 2) continue;
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) j / (float) G);
                d[o] += hermite (s, n, sp) * w * gains[ch] * norm;
            }
        }
    }
}

//==============================================================================
void analysePhrase (const juce::AudioBuffer<float>& source, int n, double sr, float sensitivity, float legato, int playMode,
                    PhraseAnalysis& an, int generation, bool withPitch)
{
    const int numCh = juce::jmin (source.getNumChannels(), 2);
    if (n <= 0 || numCh <= 0) return;

    if (! an.matches (generation, n, sr, sensitivity, legato, playMode))
    {
        an.mono.assign ((size_t) n, 0.0f);
        for (int ch = 0; ch < numCh; ++ch)
        {
            const float* s = source.getReadPointer (ch);
            for (int i = 0; i < n; ++i) an.mono[(size_t) i] += s[i] / (float) numCh;
        }

        std::vector<char> legatoFlags;
        const auto onsets = detectOnsets (an.mono.data(), n, sr, sensitivity, legato, &legatoFlags);
        an.notes.clear();
        an.notes.reserve (onsets.size());
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            Note note;
            note.start = onsets[i];
            note.legatoCut = i < legatoFlags.size() && legatoFlags[i] != 0;
            note.length = (i + 1 < onsets.size() ? onsets[i + 1] : n) - note.start;
            if (note.length > 0) an.notes.push_back (note);
        }
        an.setKey (generation, n, sr, sensitivity, legato, playMode);
    }

    if (withPitch)
        for (auto& note : an.notes)
            if (! note.analysed) analyseNote (an.mono, note, sr, playMode);
}

//==============================================================================
int renderPhrase (const juce::AudioBuffer<float>& source, int n,
                  const RenderParams& p,
                  juce::AudioBuffer<float>& dest,
                  RenderInfo* info,
                  PhraseAnalysis* cache, int generation)
{
    const int numCh = juce::jmin (source.getNumChannels(), 2);
    jassert (source.getNumSamples() >= n);

    if (dest.getNumChannels() < 2 || dest.getNumSamples() < n)
        dest.setSize (2, juce::jmax (n, dest.getNumSamples()), false, false, true);

    for (int ch = 0; ch < dest.getNumChannels(); ++ch)
        dest.clear (ch, 0, n);

    if (n <= 0 || numCh <= 0)
        return 0;

    const double sr = p.sampleRate;

    // 1-2. analyse (mono + découpage), mise en cache tant que la phrase et les réglages ne changent pas
    PhraseAnalysis local;
    PhraseAnalysis& an = cache != nullptr ? *cache : local;
    analysePhrase (source, n, sr, p.sensitivity, p.legato, p.playMode, an, generation, false);

    const std::vector<float>& mono = an.mono;
    std::vector<Note>& notes = an.notes;

    // entrée mono sur piste stéréo : un seul canal à calculer
    bool sameChannels = numCh == 2;
    if (sameChannels)
    {
        const float* l = source.getReadPointer (0);
        const float* r = source.getReadPointer (1);
        for (int i = 0; i < n; i += 7)
            if (std::abs (l[i] - r[i]) > 1.0e-6f) { sameChannels = false; break; }
    }
    const int renderCh = sameChannels ? 1 : numCh;

    const int numNotes = (int) notes.size();
    juce::Random rng ((juce::int64) p.seed);

    // 3. ordre : reverse = ordre inversé, puis désordre dosé
    std::vector<int> order ((size_t) numNotes);
    std::iota (order.begin(), order.end(), 0);
    if (p.reverse) std::reverse (order.begin(), order.end());

    const float a = juce::jlimit (0.0f, 1.0f, p.randomAmount);
    {
        std::vector<float> keys ((size_t) numNotes);
        for (int k = 0; k < numNotes; ++k)
            keys[(size_t) k] = (1.0f - a) * (float) k + a * (float) numNotes * rng.nextFloat();

        if (a > 0.0f && numNotes > 1)
        {
            std::vector<int> idx ((size_t) numNotes);
            std::iota (idx.begin(), idx.end(), 0);
            std::stable_sort (idx.begin(), idx.end(), [&] (int l, int r) { return keys[(size_t) l] < keys[(size_t) r]; });

            std::vector<int> shuffled ((size_t) numNotes);
            for (int k = 0; k < numNotes; ++k) shuffled[(size_t) k] = order[(size_t) idx[(size_t) k]];
            order.swap (shuffled);
        }
    }

    const bool scaleOn = p.scale != Scale::Off;
    const bool needPitch = scaleOn && (p.correction > 0.0f || p.transpose != 0 || p.melodicJitter > 0.0f);

    juce::Random atkRng ((juce::int64) (p.seed ^ 0x5bd1e995u));   // tirages séparés : ne change pas les autres hasards
    juce::Random timeRng ((juce::int64) (p.seed ^ 0x0777u));

    auto directionOk = [&p] (bool reversed)
    {
        return p.attackDirection == 0 || (p.attackDirection == 1 && ! reversed) || (p.attackDirection == 2 && reversed);
    };
    auto patternHit = [&p] (int index, float r)
    {
        switch (p.attackPattern)
        {
            case 1:  return index % 2 == 0;
            case 2:  return index % 3 == 0;
            case 3:  return index % 4 == 0;
            case 4:  return r < p.attackProb;
            default: return true;
        }
    };

    // 4. planification de chaque note de sortie
    struct Plan { int src; bool mute; bool reversed; float shift; double period; int start; int origLen; bool swell; };
    std::vector<Plan> plan;
    plan.reserve ((size_t) numNotes);

    int pos = 0, prevSource = -1;
    for (int k = 0; k < numNotes && pos < n; ++k)
    {
        // tirages toujours dans le même ordre : résultat stable pour une graine donnée
        const float rSkip = rng.nextFloat(), rRep = rng.nextFloat(), rRev = rng.nextFloat();
        const float rOct = rng.nextFloat(), rOctUp = rng.nextFloat();
        const float rJit = rng.nextFloat(), rJitUp = rng.nextFloat(), rJitBig = rng.nextFloat();
        const float rAtk = atkRng.nextFloat();

        Plan pl;
        pl.origLen = juce::jmin (notes[(size_t) order[(size_t) k]].length, n - pos);
        pl.start = pos;
        pl.src = order[(size_t) k];
        if (prevSource >= 0 && rRep < p.repeatProb) pl.src = prevSource;

        pl.mute = rSkip < p.skipProb;
        pl.reversed = p.reverse != (rRev < p.noteReverseProb);
        Note& note = notes[(size_t) pl.src];

        float shift = 0.0f;
        if (! scaleOn)
        {
            shift = (float) p.transpose;
        }
        else
        {
            if (needPitch && ! note.analysed) analyseNote (mono, note, sr, p.playMode);

            int steps = p.transpose;
            if (rJit < p.melodicJitter)
                steps += (rJitUp < 0.5f ? -1 : 1) * (rJitBig < 0.3f ? 2 : 1);

            if (note.midi >= 0.0f)
            {
                const float snapped = snapToScale (note.midi, p.keyRoot, p.scale);
                const float corrected = note.midi + p.correction * (snapped - note.midi);
                const int target = stepInScale ((int) std::round (snapped), steps, p.keyRoot, p.scale);
                shift = (corrected - note.midi) + ((float) target - snapped);
            }
            else
            {
                shift = std::round ((float) steps * 12.0f / 7.0f); // son non tonal : approximation
            }
        }
        if (rOct < p.octaveProb) shift += rOctUp < 0.5f ? -12.0f : 12.0f;
        pl.shift = juce::jlimit (-36.0f, 36.0f, shift);

        pl.period = 0.0;
        if (std::abs (pl.shift) > 1.01f)
        {
            if (! note.analysed) analyseNote (mono, note, sr, p.playMode);
            if (note.midi >= 0.0f && ! note.poly)
                pl.period = sr / (440.0 * std::pow (2.0, (note.midi - 69.0) / 12.0));
        }

        pl.swell = p.attackPerNote && directionOk (pl.reversed) && patternHit (k, rAtk);

        plan.push_back (pl);
        prevSource = pl.src;
        pos += pl.origLen;
    }

    // 5. recalage rythmique (grille) + flou
    const int numPlan = (int) plan.size();
    std::vector<int> starts ((size_t) numPlan);
    {
        const double grid = p.quantGridBeats > 0.0f && p.beatSamples > 0.0 ? p.quantGridBeats * p.beatSamples : 0.0;
        const double humanize = p.humanizeMs * 0.001 * sr;
        const int minLen = (int) (0.01 * sr);

        for (int k = 0; k < numPlan; ++k)
        {
            double st = plan[(size_t) k].start;
            if (grid > 0.0 && k > 0)
                st += p.quantAmount * (std::round (st / grid) * grid - st);
            const double r = timeRng.nextDouble();
            if (humanize > 0.0 && k > 0)
                st += (r * 2.0 - 1.0) * humanize;

            int s = (int) std::round (st);
            if (k > 0 && s < starts[(size_t) k - 1] + minLen)
            {
                // deux notes sur la même ligne : la seconde passe à la ligne suivante
                const double push = grid > 0.0 ? juce::jmax ((double) minLen, grid * p.quantAmount) : (double) minLen;
                s = starts[(size_t) k - 1] + (int) std::round (push);
            }
            starts[(size_t) k] = juce::jlimit (0, juce::jmax (0, n - 1), s);
        }
    }

    // 6. rendu
    const int fadeLen = juce::jmax (1, (int) (p.fadeMs * 0.001 * sr));
    const int attack = (int) (juce::jmax (0.0f, p.attackMs) * 0.001 * sr);
    Scratch sc;
    std::vector<float> noteOut;

    if (info != nullptr)
    {
        info->noteStarts.clear(); info->shifts.clear(); info->sourceIndex.clear(); info->noteReversed.clear(); info->notePoly.clear();
        info->reverse = p.reverse;
        info->numNotes = numNotes;
    }

    for (int k = 0; k < numPlan; ++k)
    {
        const auto& pl = plan[(size_t) k];
        const int st = starts[(size_t) k];
        const int end = k + 1 < numPlan ? starts[(size_t) k + 1] : n;
        const int L = juce::jmax (0, end - st);
        const Note& note = notes[(size_t) pl.src];

        if (info != nullptr)
        {
            info->noteStarts.push_back ((float) st / (float) n);
            info->shifts.push_back (pl.mute ? 0.0f : pl.shift);
            info->sourceIndex.push_back (pl.mute ? -1 : pl.src);
            info->noteReversed.push_back (pl.reversed ? 1 : 0);
            info->notePoly.push_back (note.analysed && note.poly ? 1 : 0);
        }

        if (pl.mute || L <= 0) continue;

        const int noteAttack = (attack > 0 && pl.swell) ? attack : 0;
        const int tail = k + 1 < numPlan ? fadeLen : 0;   // la dernière note s'arrête à la fin de la phrase
        noteOut.assign ((size_t) (L + tail), 0.0f);
        for (int ch = 0; ch < renderCh; ++ch)
        {
            std::fill (noteOut.begin(), noteOut.end(), 0.0f);
            const int written = renderNote (source.getReadPointer (ch), n, note.start, note.length, tail,
                                            noteOut.data(), L, pl.shift, pl.reversed, note.analysed && note.poly,
                                            fadeLen, pl.period, sr, sc, noteAttack);
            dest.addFrom (ch, st, noteOut.data(), juce::jmin (written, n - st));
        }
    }

    if (renderCh == 1)
        dest.copyFrom (1, 0, dest, 0, 0, n);
    const int outCh = 2;

    // 7. texture granulaire
    if (p.textureMix > 0.0f)
    {
        juce::AudioBuffer<float> cloud;
        renderTexture (source, n, p, cloud);
        for (int ch = 0; ch < outCh; ++ch)
        {
            dest.applyGain (ch, 0, n, 1.0f - p.textureMix);
            dest.addFrom (ch, 0, cloud, ch, 0, n, p.textureMix);
        }
    }

    // attaque sur toute la répétition
    if (attack > 0 && ! p.attackPerNote && directionOk (p.reverse)
        && patternHit (p.repIndex, juce::Random ((juce::int64) (p.seed ^ 0x2545f491u)).nextFloat()))
        for (int ch = 0; ch < outCh; ++ch)
            applySwell (dest.getWritePointer (ch), n, attack);

    // 8. couleur de la répétition : filtres + gain
    if (p.highpassHz > 25.0f || p.lowpassHz < 19000.0f)
    {
        const float nyq = (float) (sr * 0.45);
        auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, juce::jlimit (20.0f, nyq, p.highpassHz));
        auto lp = juce::dsp::IIR::Coefficients<float>::makeLowPass  (sr, juce::jlimit (100.0f, nyq, p.lowpassHz));

        for (int ch = 0; ch < outCh; ++ch)
        {
            juce::dsp::IIR::Filter<float> fHp (hp), fLp (lp);
            float* d = dest.getWritePointer (ch);
            const bool useHp = p.highpassHz > 25.0f, useLp = p.lowpassHz < 19000.0f;
            for (int i = 0; i < n; ++i)
            {
                float v = d[i];
                if (useHp) v = fHp.processSample (v);
                if (useLp) v = fLp.processSample (v);
                d[i] = v;
            }
        }
    }

    if (std::abs (p.gain - 1.0f) > 1.0e-4f)
        for (int ch = 0; ch < outCh; ++ch)
            dest.applyGain (ch, 0, n, p.gain);

    // 9. vitesse de bande : hauteur et durée liées
    int outLen = n;
    if (std::abs (p.speed - 1.0f) > 1.0e-3f && p.speed > 0.05f)
    {
        outLen = juce::jmax (1, (int) std::round (n / p.speed));
        juce::AudioBuffer<float> tmp (outCh, n);
        for (int ch = 0; ch < outCh; ++ch) tmp.copyFrom (ch, 0, dest, ch, 0, n);
        if (dest.getNumSamples() < outLen)
            dest.setSize (2, outLen, false, false, true);
        for (int ch = 0; ch < outCh; ++ch)
        {
            const float* s = tmp.getReadPointer (ch);
            float* d = dest.getWritePointer (ch);
            for (int j = 0; j < outLen; ++j)
            {
                const double sp = j * (double) p.speed;
                d[j] = sp <= n - 1 ? hermite (s, n, sp) : 0.0f;
            }
        }
    }

    // 10. vignette
    if (info != nullptr)
    {
        constexpr int cols = 600;
        info->peaks.assign (cols, 0.0f);
        for (int c = 0; c < cols; ++c)
        {
            const int s0 = (int) ((juce::int64) c * outLen / cols);
            const int s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (c + 1) * outLen / cols));
            float m = 0.0f;
            for (int ch = 0; ch < outCh; ++ch)
                m = juce::jmax (m, dest.getMagnitude (ch, s0, juce::jmin (s1, outLen) - s0));
            info->peaks[(size_t) c] = juce::jmin (1.0f, m);
        }
        if (outLen != n)
            for (auto& st : info->noteStarts) st = juce::jlimit (0.0f, 1.0f, st);
    }

    return outLen;
}
} // namespace pal
