#pragma once

// Briques communes a Alea et Talea : hasard deterministe, gammes, subdivisions,
// grooves, conditions, modulateurs. (espace de noms alea)
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace alea
{
constexpr int kNumMods = 6;

// Subdivisions du sequenceur (par piste)
// (ordre conserve pour la compatibilite : les nouvelles valeurs sont ajoutees a la fin)
inline const juce::StringArray& rateNames()
{
    static const juce::StringArray s { "1/8", "1/16", "1/16T", "1/32", "1/4", "1/8T", "1/16 x5", "1/16 x7", "1/32T" };
    return s;
}
constexpr int kNumRates = 9;
inline double ratePpq (int r)
{
    static const double v[] { 0.5, 0.25, 1.0 / 6.0, 0.125, 1.0, 1.0 / 3.0, 0.2, 1.0 / 7.0, 1.0 / 12.0 };
    return v[juce::jlimit (0, kNumRates - 1, r)];
}

// Grooves par piste (decalage en fraction de pas)
inline const juce::StringArray& grooveNames()
{
    static const juce::StringArray s { "Groove: Global", "Straight", "MPC 54%", "MPC 58%", "MPC 62%", "MPC 66%", "Shuffle", "Laid back", "Push" };
    return s;
}
constexpr int kNumGrooves = 9;
inline double grooveOffset (int g, int64_t k, float globalSwing) noexcept
{
    const bool odd = (k & 1) != 0;
    switch (g)
    {
        case 0: return odd ? globalSwing * 0.5 : 0.0;
        case 2: return odd ? 0.08 : 0.0;
        case 3: return odd ? 0.16 : 0.0;
        case 4: return odd ? 0.24 : 0.0;
        case 5: return odd ? 0.32 : 0.0;
        case 6: return odd ? 1.0 / 3.0 : 0.0;
        case 7: return (k % 4 == 0) ? 0.0 : (odd ? 0.12 : 0.06);
        case 8: return odd ? -0.06 : ((k % 4 == 2) ? -0.04 : 0.0);
        default: return 0.0;
    }
}

inline const juce::StringArray& dirNames() { static const juce::StringArray s { "Forward", "Reverse", "Ping-pong", "Random" }; return s; }

// Conditions de declenchement (facon Elektron)
constexpr int kNumConds = 18;
inline const juce::StringArray& condNames()
{
    static const juce::StringArray s { "Always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                       "Fill", "!Fill", "First", "!First", "PRE", "!PRE", "NEI", "!NEI" };
    return s;
}
inline const juce::StringArray& condShort()
{
    static const juce::StringArray s { "", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "F", "!F", "1st", "!1st",
                                       "PRE", "!PR", "NEI", "!NE" };
    return s;
}

// Divisions du delay
inline const juce::StringArray& delayDivNames()
{
    static const juce::StringArray s { "1/32", "1/16", "1/16T", "1/16.", "1/8", "1/8T", "1/8.", "1/4", "1/4.", "1/2" };
    return s;
}
inline double delayDivPpq (int i)
{
    static const double v[] { 0.125, 0.25, 1.0 / 6.0, 0.375, 0.5, 1.0 / 3.0, 0.75, 1.0, 1.5, 2.0 };
    return v[juce::jlimit (0, 9, i)];
}

// Modulateurs aleatoires
inline const juce::StringArray& modNames() { static const juce::StringArray s { "S&H", "Smooth", "Turing", "Chaos", "LFO", "Follow" }; return s; }
inline const juce::StringArray& modParamNames() { static const juce::StringArray s { "Chance", "Jump", "Change", "R", "Shape", "Release" }; return s; }


inline const juce::StringArray& modRateNames() { static const juce::StringArray s { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars" }; return s; }
inline double modRatePpq (int i) { static const double v[] { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }; return v[juce::jlimit (0, 6, i)]; }

// Gammes partagees par toute la suite (AleaLink). Les index 0..7 sont historiques (sessions existantes) ;
// les modes et gammes ajoutes en v5.0.4 suivent, pour que Color transmette son mode exact.
inline const juce::StringArray& scaleNames()
{
    static const juce::StringArray s { "Scale: Off", "Chromatic", "Major (Ionian)", "Minor (Aeolian)", "Penta Maj", "Penta Min", "Whole tone", "Octaves",
                                       "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "Harmonic minor", "Melodic minor", "Blues",
                                       "Phrygian dominant", "Lydian dominant", "Diminished (W-H)", "Harmonic major", "Hirajoshi",
                                       "Dorian b2", "Lydian augmented", "Mixolydian b6", "Locrian #2", "Altered", "Double harmonic",
                                       "Hungarian minor", "Hungarian major", "Neapolitan minor", "Neapolitan major", "Persian", "Enigmatic",
                                       "Diminished (H-W)", "Augmented", "Kumoi", "In-sen", "Pelog", "Bebop dominant", "Bebop major" };
    return s;
}
constexpr int kNumSharedScales = 40;   // toutes les gammes de Color ont un equivalent exact
inline const std::vector<int>& scaleDegrees (int scale)
{
    static const std::vector<int> d[kNumSharedScales] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 },
        { 0, 2, 4, 7, 9 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 6, 8, 10 }, { 0 },
        { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 1, 3, 5, 6, 8, 10 },
        { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 11 }, { 0, 3, 5, 6, 7, 10 }, { 0, 1, 4, 5, 7, 8, 10 }, { 0, 2, 4, 6, 7, 9, 10 },
        { 0, 2, 3, 5, 6, 8, 9, 11 }, { 0, 2, 4, 5, 7, 8, 11 }, { 0, 2, 3, 7, 8 },
        { 0, 1, 3, 5, 7, 9, 10 }, { 0, 2, 4, 6, 8, 9, 11 }, { 0, 2, 4, 5, 7, 8, 10 }, { 0, 2, 3, 5, 6, 8, 10 }, { 0, 1, 3, 4, 6, 8, 10 },
        { 0, 1, 4, 5, 7, 8, 11 }, { 0, 2, 3, 6, 7, 8, 11 }, { 0, 3, 4, 6, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 11 }, { 0, 1, 3, 5, 7, 9, 11 },
        { 0, 1, 4, 5, 6, 8, 11 }, { 0, 1, 4, 6, 8, 10, 11 }, { 0, 1, 3, 4, 6, 7, 9, 10 }, { 0, 3, 4, 7, 8, 11 }, { 0, 2, 3, 7, 9 },
        { 0, 1, 5, 7, 10 }, { 0, 1, 3, 7, 8 }, { 0, 2, 4, 5, 7, 9, 10, 11 }, { 0, 2, 4, 5, 7, 8, 9, 11 } };
    return d[juce::jlimit (0, kNumSharedScales - 1, scale)];
}
inline bool scaleIsMinor (int scale)
{
    if (scale <= 1 || scale == 6 || scale == 7) return false;
    const auto& d = scaleDegrees (scale);
    bool m3 = false, M3 = false;
    for (int x : d) { if (x == 3) m3 = true; if (x == 4) M3 = true; }
    return m3 && ! M3;
}
inline float quantizeSemis (float semis, int scale)
{
    if (scale <= 0 || scale >= kNumSharedScales) return semis;
    const auto& degrees = scaleDegrees (scale);
    const float oct = std::floor (semis / 12.0f);
    const float rem = semis - oct * 12.0f;
    float best = 0.0f, bestDist = 1.0e9f;
    for (int d : degrees)
        for (int o : { -12, 0, 12 })
        {
            const float cand = (float) (d + o);
            const float dist = std::abs (cand - rem);
            if (dist < bestDist) { bestDist = dist; best = cand; }
        }
    return oct * 12.0f + best;
}

// ---------------------------------------------------------------------------
// Hasard : hash deterministe (reproductible via la "seed") + xorshift rapide
inline uint32_t hash3 (uint32_t a, uint32_t b, uint32_t c) noexcept
{
    uint32_t h = a * 0x9E3779B1u;
    h ^= (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
inline float hUni (uint32_t a, uint32_t b, uint32_t c) noexcept { return (float) (hash3 (a, b, c) >> 8) * (1.0f / 16777216.0f); }
inline float hBi  (uint32_t a, uint32_t b, uint32_t c) noexcept { return hUni (a, b, c) * 2.0f - 1.0f; }

// Forme de distribution : 0 uniforme, 1 gaussienne (approx.), 2 "rare" (petits ecarts, rares grands sauts)
inline float shapedBi (int dist, uint32_t a, uint32_t b, uint32_t c) noexcept
{
    const float u = hBi (a, b, c);
    if (dist == 1)
    {
        const float g = (u + hBi (a, b + 7919u, c) + hBi (a, b + 15887u, c)) * (1.0f / 3.0f);
        return juce::jlimit (-1.0f, 1.0f, g * 1.7f);
    }
    if (dist == 2) return u * u * u;
    return u;
}

struct FastRng
{
    uint32_t s = 0x12345678u;
    explicit FastRng (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi()  noexcept { return uni() * 2.0f - 1.0f; }
};

inline bool conditionPasses (int cond, int64_t loop, bool fill, bool pre, bool nei) noexcept
{
    static constexpr int a[] { 0, 1, 2, 1, 2, 3, 1, 2, 3, 4 };
    static constexpr int b[] { 1, 2, 2, 3, 3, 3, 4, 4, 4, 4 };
    if (loop < 0) loop = 0;
    if (cond >= 1 && cond <= 9) return (int) (loop % b[cond]) == a[cond] - 1;
    switch (cond)
    {
        case 10: return fill;
        case 11: return ! fill;
        case 12: return loop == 0;
        case 13: return loop != 0;
        case 14: return pre;
        case 15: return ! pre;
        case 16: return nei;
        case 17: return ! nei;
        default: return true;
    }
}

} // namespace alea
