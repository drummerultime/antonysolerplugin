#pragma once

#include "../Core.h"

namespace alea
{
// 4 sources de modulation aleatoire, cadencees sur le tempo.
//  0 S&H    : nouvelle valeur au hasard a chaque pas (avec une probabilite "Chance")
//  1 Smooth : marche aleatoire lissee (interpolee entre deux tirages), "Jump" = taille des sauts
//  2 Turing : registre a decalage boucle ; "Change" = probabilite qu'un bit mute (0 = boucle figee)
//  3 Chaos  : suite logistique x <- r.x.(1-x), r de 3.5 (quasi periodique) a 4 (chaos)
//  4 LFO    : sinus / triangle / dent de scie / carre / paliers aleatoires selon "Shape"
//  5 Follow : suiveur d'enveloppe d'une piste (valeur fournie par le processeur)
class RandomMods
{
public:
    void reset()
    {
        for (int m = 0; m < kNumMods; ++m) { lastTick[m] = INT64_MIN; value[m] = 0.5f; }
        value[5] = 0.0f;
        smoothFrom = smoothTo = 0.5f;
        turing = 0xA5A5u;
        logistic = 0.37f;
    }

    // clock : position en noires ; rate/p : parametres de chaque module ; turingLen : 2..16
    void advance (double clock, const int* rate, const float* p, int turingLen, uint32_t seed)
    {
        for (int m = 0; m < 4; ++m)
        {
            const double L = modRatePpq (rate[m]);
            const int64_t tick = (int64_t) std::floor (clock / L);
            if (lastTick[m] == INT64_MIN) { lastTick[m] = tick - 1; }
            int64_t steps = tick - lastTick[m];
            if (steps < 0 || steps > 8) steps = 1;          // saut de transport : un seul pas
            for (int64_t i = steps - 1; i >= 0; --i) step (m, tick - i, p[m], turingLen, seed);
            lastTick[m] = tick;

            if (m == 1)   // interpolation lineaire entre deux tirages
            {
                const double frac = clock / L - std::floor (clock / L);
                value[1] = smoothFrom + (smoothTo - smoothFrom) * (float) frac;
            }
        }

        // LFO
        {
            const double L = modRatePpq (rate[4]);
            const double cyc = clock / L;
            const float ph = (float) (cyc - std::floor (cyc));
            const int shape = juce::jlimit (0, 4, (int) (p[4] * 4.999f));
            switch (shape)
            {
                case 0: value[4] = 0.5f + 0.5f * std::sin (juce::MathConstants<float>::twoPi * ph); break;
                case 1: value[4] = 1.0f - 2.0f * std::abs (ph - 0.5f); break;
                case 2: value[4] = ph; break;
                case 3: value[4] = ph < 0.5f ? 1.0f : 0.0f; break;
                default: value[4] = hUni ((uint32_t) (int64_t) std::floor (cyc), 0x55u, seed); break;
            }
        }
    }

    void setFollow (float v) noexcept { value[5] = juce::jlimit (0.0f, 1.0f, v); }

    float value[kNumMods] { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.0f };   // 0..1

private:
    void step (int m, int64_t tick, float p, int turingLen, uint32_t seed)
    {
        const uint32_t tk = (uint32_t) tick;
        switch (m)
        {
            case 0:
                if (hUni (tk, 0x51u, seed) < juce::jmax (0.02f, p)) value[0] = hUni (tk, 0x52u, seed);
                break;
            case 1:
                smoothFrom = smoothTo;
                smoothTo = juce::jlimit (0.0f, 1.0f, smoothTo + hBi (tk, 0x53u, seed) * (0.05f + p * 0.95f));
                break;
            case 2:
            {
                const int len = juce::jlimit (2, 16, turingLen);
                const uint32_t mask = (1u << len) - 1u;
                uint32_t bit = (turing >> (len - 1)) & 1u;
                if (hUni (tk, 0x54u, seed) < p * 0.5f) bit ^= 1u;
                turing = ((turing << 1) | bit) & mask;
                const int bits = juce::jmin (len, 8);
                value[2] = (float) (turing & ((1u << bits) - 1u)) / (float) ((1u << bits) - 1u);
                break;
            }
            case 3:
            {
                const float r = 3.5f + p * 0.5f;
                logistic = r * logistic * (1.0f - logistic);
                if (logistic <= 0.0001f || logistic >= 0.9999f) logistic = 0.37f;
                value[3] = logistic;
                break;
            }
            default: break;
        }
    }

    int64_t lastTick[kNumMods] { INT64_MIN, INT64_MIN, INT64_MIN, INT64_MIN, INT64_MIN, INT64_MIN };
    float smoothFrom = 0.5f, smoothTo = 0.5f;
    uint32_t turing = 0xA5A5u;
    float logistic = 0.37f;
};

} // namespace alea
