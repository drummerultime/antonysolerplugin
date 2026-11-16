#pragma once

// GROT - moteurs d'effets. Chaque moteur traite un bloc stereo en place et rend le signal "wet".
#include "MaParams.h"
#include "Core.h"
#include <memory>
#include <vector>

namespace ma
{
// Contexte d'un sous-bloc (au plus kSub echantillons)
struct Ctx
{
    double fs = 48000.0;
    double bpm = 120.0;
    double ppq = 0.0;            // position (en noires) du premier echantillon
    double ppqPerSample = 0.0;
    bool freeze = false;
    uint32_t seed = 1;
    int64_t tick = 0;            // index du pas de l'horloge aleatoire
    int slot = 0;
    float env = 0.0f;            // enveloppe d'entree 0..1
    const float* hit = nullptr;  // velocite des coups detectes (0 = pas de coup), un par echantillon
    float fund = 100.0f, body = 200.0f, lo = 40.0f, hi = 12000.0f, decayMs = 300.0f;   // profil de la source
    const float* side = nullptr;   // sidechain mono (autre musicien), nullptr si absente
    const float* track = nullptr;  // entree de la piste (mono)
    inline float hitAt (int i) const noexcept { return hit != nullptr ? hit[i] : 0.0f; }
    double samplesPerBeat() const noexcept { return 60.0 / juce::jmax (20.0, bpm) * fs; }
};

class Engine
{
public:
    virtual ~Engine() = default;
    virtual void prepare (double fs) = 0;
    virtual void reset() = 0;
    virtual void process (float* L, float* R, int n, const Ctx& c, const float* p) = 0;
};

std::unique_ptr<Engine> makeEngine (int type);

// ---------------------------------------------------------------------------
// Briques DSP (exposees pour les tests)
struct Line
{
    std::vector<float> b;
    int mask = 0, w = 0;
    void prepare (int minLen) { const int n = juce::nextPowerOfTwo (juce::jmax (8, minLen + 8)); b.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill (b.begin(), b.end(), 0.0f); w = 0; }
    int size() const noexcept { return mask + 1; }
    inline void push (float x) noexcept { b[(size_t) w] = x; w = (w + 1) & mask; }
    inline float tap (int d) const noexcept { return b[(size_t) ((w - d) & mask)]; }            // d >= 1
    inline float read (float d) const noexcept                                                 // lineaire, d >= 1
    {
        const int i = (int) d; const float f = d - (float) i;
        const float a = b[(size_t) ((w - i) & mask)], c = b[(size_t) ((w - i - 1) & mask)];
        return a + f * (c - a);
    }
    inline float readH (float d) const noexcept                                                // Hermite, d >= 2
    {
        const int i = (int) d; const float f = d - (float) i;
        const float xm1 = b[(size_t) ((w - i + 1) & mask)], x0 = b[(size_t) ((w - i) & mask)];
        const float x1 = b[(size_t) ((w - i - 1) & mask)], x2 = b[(size_t) ((w - i - 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

// Transposition par deux tetes de lecture croisees
struct Shifter
{
    Line l;
    float ph = 0.0f;
    void prepare (double fs) { l.prepare ((int) (fs * 0.3)); ph = 0.0f; }
    void clear() { l.clear(); ph = 0.0f; }
    inline float process (float x, float ratio, float W) noexcept
    {
        l.push (x);
        ph += (1.0f - ratio) / W;
        ph -= std::floor (ph);
        float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        const float g1 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * ph);
        const float g2 = 1.0f - g1;
        return l.readH (2.0f + ph * W) * g1 + l.readH (2.0f + p2 * W) * g2;
    }
};

// Transformee de Hilbert IIR (O. Niemitalo) : deux sorties en quadrature
struct Hilbert
{
    float xa[4][2] {}, ya[4][2] {}, xb[4][2] {}, yb[4][2] {}, delayA = 0.0f;
    void clear() { *this = Hilbert(); }
    inline void process (float x, float& re, float& im) noexcept
    {
        static constexpr float A[4] { 0.6923878f * 0.6923878f, 0.9360654322959f * 0.9360654322959f, 0.9882295226860f * 0.9882295226860f, 0.9987488452737f * 0.9987488452737f };
        static constexpr float B[4] { 0.4021921162426f * 0.4021921162426f, 0.8561710882420f * 0.8561710882420f, 0.9722909545651f * 0.9722909545651f, 0.9952884791278f * 0.9952884791278f };
        float a = x, b = x;
        for (int k = 0; k < 4; ++k)
        {
            const float ya0 = A[k] * (a + ya[k][1]) - xa[k][1];
            xa[k][1] = xa[k][0]; xa[k][0] = a; ya[k][1] = ya[k][0]; ya[k][0] = ya0; a = ya0;
            const float yb0 = B[k] * (b + yb[k][1]) - xb[k][1];
            xb[k][1] = xb[k][0]; xb[k][0] = b; yb[k][1] = yb[k][0]; yb[k][0] = yb0; b = yb0;
        }
        re = delayA; delayA = a;
        im = b;
    }
};

// Filtre a variable d'etat (Cytomic / TPT)
struct Svf
{
    float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void clear() { ic1 = ic2 = 0; }
    void set (float fc, float fs, float kk) noexcept
    {
        const float g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (10.0f, fs * 0.45f, fc) / fs);
        k = kk; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void process (float x, float& low, float& band, float& high) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        low = v2; band = v1; high = x - k * v1 - v2;
    }
};

// Reverbe FDN 8 lignes (Hadamard), entree stereo, decay et taille independants, gel
class Verb
{
public:
    void prepare (double sampleRate);
    void clear();
    void setParams (float size, float t60, float damp, float predelaySec, float mod, float width);
    void process (float inL, float inR, float& outL, float& outR, bool freeze) noexcept;
private:
    float fs = 48000.0f;
    std::vector<float> lines[8];
    std::vector<float> pre[2], ap[2][4];
    int lineSize = 0, w = 0, preSize = 0, preW = 0, preD = 1;
    int apLen[2][4] {}, apW[2][4] {};
    float len[8] {}, g[8] {}, lp[8] {};
    float dampC = 0.5f, width = 1.0f, modDepth = 0.0f, ph[4] {};
};

inline float softClip (float x) noexcept
{
    const float a = std::abs (x);
    if (a < 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}
inline float onePoleCoef (float hz, double fs) noexcept { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) fs); }

// Intervalles "musicaux" tires au hasard (du plus consonant au plus dissonant)
float randomInterval (float amount, uint32_t a, uint32_t b, uint32_t c) noexcept;
} // namespace ma
