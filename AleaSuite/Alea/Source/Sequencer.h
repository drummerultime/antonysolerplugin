#pragma once

#include "Pattern.h"

namespace alea
{
struct SeqEvent
{
    int offset = 0;
    int track = 0;
    float vel = 1.0f;
    bool accent = false;
    bool slide = false;
    float tune = 0.0f;
    uint32_t hitKey = 0, barKey = 0;
    int8_t pat = -1, step = -1;     // pour les p-locks (-1 = declenchement MIDI / interface)
    bool fromMidiIn = false;
    bool ghost = false;             // ajoute par la densite / le chaos
};

struct SeqContext
{
    int pattern = 0;
    double patStart = 0.0;          // position (ppq) du debut du pattern : reference des conditions
    uint32_t seed = 1;
    float swing = 0, seqChaos = 0, ratChaos = 0, human = 0, density = 0.5f;
    bool fill = false;
    double bpm = 120.0;
    const std::atomic<uint32_t>* evoMask = nullptr;   // [kNumTracks] : pas inverses par l'evolution
};

// Memoire des conditions PRE / NEI (resultat du dernier pas conditionnel de chaque piste)
struct CondState
{
    struct Entry { int64_t k = -1; bool res = false; };
    Entry cache[kNumTracks][8];
    int64_t lastK[kNumTracks];
    bool lastRes[kNumTracks];
    CondState() { reset(); }
    void reset()
    {
        for (int t = 0; t < kNumTracks; ++t)
        {
            lastK[t] = -1; lastRes[t] = false;
            for (auto& e : cache[t]) e = {};
        }
    }
};

inline uint32_t trackSalt (uint32_t seed, int t) noexcept { return seed * 16u + (uint32_t) t; }

// Parcourt les pas qui tombent dans [ppqStart, ppqEnd) et appelle emit (ppqTime, SeqEvent).
// Les decisions aleatoires sont des hachages deterministes de (pas, piste, seed) :
// le resultat ne depend pas du decoupage en blocs, et un bounce reproduit le jeu.
template <typename Emit>
void runSequencer (const PatternBank& bank, const SeqContext& c, CondState* cs, double ppqStart, double ppqEnd, Emit&& emit)
{
    const auto& pat = bank.pats[juce::jlimit (0, kNumPatterns - 1, c.pattern)];
    const double humanPpq = 0.020 * (c.bpm / 60.0) * c.human;
    const float density = juce::jlimit (0.0f, 1.0f, c.density + (c.fill ? 0.12f : 0.0f));
    const float ratChaos = juce::jmin (1.0f, c.ratChaos + (c.fill ? 0.3f : 0.0f));

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = pat.tracks[t];
        const int len = juce::jlimit (1, kMaxSteps, (int) tp.length.load());
        const double stepLen = ratePpq (tp.rate.load());
        const int groove = tp.groove.load();
        const int64_t kFirst = (int64_t) std::floor (ppqStart / stepLen) - 2;
        const int64_t kLast  = (int64_t) std::floor (ppqEnd / stepLen) + 2;
        const int64_t kStart = (int64_t) std::ceil (c.patStart / stepLen - 1.0e-9);
        const uint32_t salt = trackSalt (c.seed, t);
        const uint32_t evo = c.evoMask != nullptr ? c.evoMask[t].load (std::memory_order_relaxed) : 0u;

        for (int64_t k = juce::jmax ((int64_t) 0, kFirst); k <= kLast; ++k)
        {
            const int idx = tp.stepIndex (k, salt);
            const Step& st = tp.steps[idx];
            const uint32_t kk = (uint32_t) k;
            const int64_t loop = k >= kStart ? (k - kStart) / len : -1;

            bool on = (st.on != 0) != (((evo >> idx) & 1u) != 0);
            bool acc = st.acc != 0;
            bool ghost = false;
            float vel = acc ? 1.0f : 0.75f;
            const bool probPass = hUni (kk, salt, 2) * 100.0f < (float) st.prob;

            if (on && st.cond != 0)
            {
                bool res = false, cached = false;
                if (cs != nullptr)
                    for (auto& e : cs->cache[t]) if (e.k == k) { res = e.res; cached = true; break; }
                if (! cached)
                {
                    const bool pre = cs != nullptr ? cs->lastRes[t] : true;
                    const bool nei = cs != nullptr ? cs->lastRes[(t + kNumTracks - 1) % kNumTracks] : true;
                    res = conditionPasses (st.cond, loop, c.fill, pre, nei) && probPass;
                    if (cs != nullptr)
                    {
                        cs->cache[t][k & 7] = { k, res };
                        if (k > cs->lastK[t]) { cs->lastK[t] = k; cs->lastRes[t] = res; }
                    }
                }
                if (! res) on = false;
            }
            else if (on && ! probPass) on = false;

            // densite (macro) : retire des coups non accentues / ajoute des coups fantomes
            if (density < 0.5f)
            {
                if (on && ! acc && hUni (kk, salt, 20) < (0.5f - density) * 2.0f) on = false;
            }
            else if (density > 0.5f && ! on && st.cond == 0 && hUni (kk, salt, 21) < (density - 0.5f) * 0.7f)
            {
                on = true; vel = 0.45f; acc = false; ghost = true;
            }

            // chaos de sequence : notes fantomes / omissions non destructives
            if (c.seqChaos > 0.0f)
            {
                const float h = hUni (kk, salt, 1);
                if (on && h < c.seqChaos * 0.25f) on = false;
                else if (! on && h > 1.0f - c.seqChaos * 0.15f) { on = true; vel = 0.4f; acc = false; ghost = true; }
            }
            if (! on) continue;

            int rat = juce::jlimit (1, 4, (int) st.rat);
            if (ratChaos > 0.0f && hUni (kk, salt, 3) < ratChaos * 0.3f)
                rat = 2 + (int) (hUni (kk, salt, 4) * 2.99f);

            const double base = (double) k * stepLen
                              + grooveOffset (groove, k, c.swing) * stepLen
                              + st.nudge / 100.0 * stepLen;

            for (int r = 0; r < rat; ++r)
            {
                const double te = base + r * stepLen / rat + humanPpq * hBi (kk, salt, 10u + (uint32_t) r);
                if (te >= ppqStart && te < ppqEnd)
                {
                    SeqEvent e;
                    e.track = t;
                    e.vel = vel * std::pow (0.85f, (float) r);
                    e.accent = acc && r == 0;
                    e.slide = st.slide != 0 && r == 0 && ! ghost;
                    e.tune = (float) st.tune;
                    e.hitKey = hash3 (kk, (uint32_t) r, salt);
                    e.barKey = hash3 ((uint32_t) (k / len), 0xBA5u, salt);
                    e.pat = (int8_t) c.pattern;
                    e.step = (int8_t) idx;
                    e.ghost = ghost;
                    emit (te, e);
                }
            }
        }
    }
}

} // namespace alea
