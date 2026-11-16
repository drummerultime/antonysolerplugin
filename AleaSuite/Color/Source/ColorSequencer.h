#pragma once

#include "ColorPattern.h"

namespace color
{
// Coup rythmique produit par le sequenceur (l'accord est choisi au moment de jouer)
struct ColorHit
{
    int offset = 0;
    double ppq = 0.0;
    float vel = 0.8f;
    bool accent = false, ghost = false;
    double gatePpq = 0.25;
    int8_t pat = -1, step = -1, part = 0, strum = 0, oct = 0;
    int64_t k = 0;               // numero de pas absolu (arpege, alternance du strum)
    uint32_t hitKey = 0, barKey = 0;
};

struct ColorContext
{
    int pattern = 0;
    double patStart = 0.0;
    uint32_t seed = 1;
    float swing = 0, seqChaos = 0, ratChaos = 0, human = 0, density = 0.5f;
    bool fill = false;
    double bpm = 120.0;
    const std::atomic<uint32_t>* evoMask = nullptr;
};

struct ColorCondState
{
    struct Entry { int64_t k = -1; bool res = false; };
    Entry cache[8];
    int64_t lastK = -1;
    bool lastRes = false;
    void reset() { lastK = -1; lastRes = false; for (auto& e : cache) e = {}; }
};

template <typename Emit>
void runColorRhythm (const CBank& bank, const ColorContext& c, ColorCondState* cs, double ppqStart, double ppqEnd, Emit&& emit)
{
    const auto& pat = bank.pats[juce::jlimit (0, kPatterns - 1, c.pattern)];
    const int len = juce::jlimit (1, kSteps, (int) pat.length.load());
    const double stepLen = ratePpq (pat.rate.load());
    const int groove = pat.groove.load();
    const double humanPpq = 0.02 * (c.bpm / 60.0) * c.human;
    const float density = juce::jlimit (0.0f, 1.0f, c.density + (c.fill ? 0.15f : 0.0f));
    const float ratChaos = juce::jmin (1.0f, c.ratChaos + (c.fill ? 0.3f : 0.0f));
    const uint32_t salt = c.seed * 16u + 13u;
    const uint32_t evo = c.evoMask != nullptr ? c.evoMask->load (std::memory_order_relaxed) : 0u;
    const int64_t kFirst = (int64_t) std::floor (ppqStart / stepLen) - 2;
    const int64_t kLast  = (int64_t) std::floor (ppqEnd / stepLen) + 2;
    const int64_t kStart = (int64_t) std::ceil (c.patStart / stepLen - 1.0e-9);

    for (int64_t k = juce::jmax ((int64_t) 0, kFirst); k <= kLast; ++k)
    {
        const int idx = pat.stepIndex (k, salt);
        const CStep& st = pat.steps[idx];
        const uint32_t kk = (uint32_t) k;
        const int64_t loop = k >= kStart ? (k - kStart) / len : -1;

        bool on = (st.on != 0) != (((evo >> idx) & 1u) != 0);
        bool acc = st.acc != 0, ghost = false;
        float vel = (float) st.vel / 127.0f;
        int holdSteps = juce::jlimit (1, kSteps, (int) st.len);
        const bool probPass = hUni (kk, salt, 2) * 100.0f < (float) st.prob;

        if (on && st.cond != 0)
        {
            bool res = false, cached = false;
            if (cs != nullptr) for (auto& e : cs->cache) if (e.k == k) { res = e.res; cached = true; break; }
            if (! cached)
            {
                const bool pre = cs != nullptr ? cs->lastRes : true;
                res = conditionPasses (st.cond, loop, c.fill, pre, pre) && probPass;
                if (cs != nullptr)
                {
                    cs->cache[k & 7] = { k, res };
                    if (k > cs->lastK) { cs->lastK = k; cs->lastRes = res; }
                }
            }
            if (! res) on = false;
        }
        else if (on && ! probPass) on = false;

        if (density < 0.5f)
        {
            if (on && ! acc && hUni (kk, salt, 20) < (0.5f - density) * 2.0f) on = false;
        }
        else if (density > 0.5f && ! on && st.cond == 0 && hUni (kk, salt, 21) < (density - 0.5f) * 0.5f)
        {
            on = true; ghost = true; acc = false; vel = 0.42f; holdSteps = 1;
        }
        if (c.seqChaos > 0.0f)
        {
            const float h = hUni (kk, salt, 1);
            if (on && h < c.seqChaos * 0.25f) on = false;
            else if (! on && h > 1.0f - c.seqChaos * 0.15f) { on = true; ghost = true; acc = false; vel = 0.42f; holdSteps = 1; }
        }
        if (! on) continue;

        int rat = juce::jlimit (1, 4, (int) st.rat);
        if (ratChaos > 0.0f && hUni (kk, salt, 3) < ratChaos * 0.3f) rat = 2 + (int) (hUni (kk, salt, 4) * 2.99f);

        const double base = (double) k * stepLen + grooveOffset (groove, k, c.swing) * stepLen + st.nudge / 100.0 * stepLen;
        for (int r = 0; r < rat; ++r)
        {
            const double te = base + r * stepLen / rat + humanPpq * hBi (kk, salt, 10u + (uint32_t) r);
            if (te < ppqStart || te >= ppqEnd) continue;
            ColorHit e;
            e.ppq = te;
            e.vel = vel * std::pow (0.85f, (float) r);
            e.accent = acc && r == 0;
            e.ghost = ghost;
            e.pat = (int8_t) c.pattern;
            e.step = (int8_t) idx;
            e.part = ghost ? (int8_t) 0 : st.part.load();
            e.strum = st.strum.load();
            e.oct = st.oct.load();
            e.k = k * 4 + r;
            e.hitKey = hash3 (kk, (uint32_t) r, salt);
            e.barKey = hash3 ((uint32_t) (k / len), 0xBA5u, salt);
            if (rat > 1 && r < rat - 1) e.gatePpq = stepLen / rat * 0.5;
            else if (rat > 1) e.gatePpq = (stepLen / rat) * (double) st.gate / 100.0;
            else e.gatePpq = holdSteps * stepLen * (double) st.gate / 100.0;
            emit (te, e);
        }
    }
}

} // namespace color
