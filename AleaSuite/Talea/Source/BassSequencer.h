#pragma once

#include "BassPattern.h"

namespace talea
{
struct BassEvent
{
    int offset = 0;
    float semis = 0.0f;        // demi-tons au-dessus de la tonique (deja dans la gamme)
    float absMidi = -1.0f;     // note absolue (mode clavier Play), sinon -1
    float vel = 0.8f;
    bool accent = false;
    bool ghost = false;
    bool fromMidi = false;
    double gatePpq = 0.25;     // duree de la porte (en noires)
    int8_t pat = -1, step = -1;
    uint32_t hitKey = 0, barKey = 0;
};

struct BassContext
{
    int pattern = 0;
    double patStart = 0.0;
    uint32_t seed = 1;
    float swing = 0, seqChaos = 0, ratChaos = 0, human = 0, density = 0.5f, noteChaos = 0;
    bool fill = false;
    double bpm = 120.0;
    int scale = 3;
    const std::atomic<uint32_t>* evoMask = nullptr;   // pas inverses par l'evolution
    const std::atomic<int8_t>* evoNote = nullptr;     // [kSteps] decalage en degres de gamme
};

struct BassCondState
{
    struct Entry { int64_t k = -1; bool res = false; };
    Entry cache[8];
    int64_t lastK = -1;
    bool lastRes = false;
    void reset() { lastK = -1; lastRes = false; for (auto& e : cache) e = {}; }
};

template <typename Emit>
void runBassSequencer (const BBank& bank, const BassContext& c, BassCondState* cs, double ppqStart, double ppqEnd, Emit&& emit)
{
    const auto& pat = bank.pats[juce::jlimit (0, kPatterns - 1, c.pattern)];
    const int len = juce::jlimit (1, kSteps, (int) pat.length.load());
    const double stepLen = ratePpq (pat.rate.load());
    const int groove = pat.groove.load();
    const double humanPpq = 0.015 * (c.bpm / 60.0) * c.human;
    const float density = juce::jlimit (0.0f, 1.0f, c.density + (c.fill ? 0.12f : 0.0f));
    const float ratChaos = juce::jmin (1.0f, c.ratChaos + (c.fill ? 0.3f : 0.0f));
    const uint32_t salt = c.seed * 16u + 11u;
    const uint32_t evo = c.evoMask != nullptr ? c.evoMask->load (std::memory_order_relaxed) : 0u;
    const int64_t kFirst = (int64_t) std::floor (ppqStart / stepLen) - 2;
    const int64_t kLast  = (int64_t) std::floor (ppqEnd / stepLen) + 2;
    const int64_t kStart = (int64_t) std::ceil (c.patStart / stepLen - 1.0e-9);
    const int sc = c.scale <= 0 ? 1 : c.scale;

    auto stepOn = [&] (int idx) { return (pat.steps[idx].on != 0) != (((evo >> idx) & 1u) != 0); };

    for (int64_t k = juce::jmax ((int64_t) 0, kFirst); k <= kLast; ++k)
    {
        const int idx = pat.stepIndex (k, salt);
        const BStep& st = pat.steps[idx];
        const uint32_t kk = (uint32_t) k;
        const int64_t loop = k >= kStart ? (k - kStart) / len : -1;

        bool on = stepOn (idx);
        if (on && st.tie) continue;                  // prolongation : pas de nouvelle note
        bool acc = st.acc != 0, ghost = false;
        float vel = (float) st.vel / 127.0f;
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

        float semis = (float) st.note + 12.0f * (float) st.oct;
        if (density < 0.5f)
        {
            if (on && ! acc && hUni (kk, salt, 20) < (0.5f - density) * 2.0f) on = false;
        }
        else if (density > 0.5f && ! on && st.cond == 0 && hUni (kk, salt, 21) < (density - 0.5f) * 0.6f)
        {
            on = true; ghost = true; acc = false; vel = 0.45f;
            static const float ghostNotes[] { 0, 0, 7, 12, 3, 5, 10, -5 };
            semis = ghostNotes[hash3 (kk, salt, 22) % 8u];
        }
        if (c.seqChaos > 0.0f)
        {
            const float h = hUni (kk, salt, 1);
            if (on && h < c.seqChaos * 0.25f) on = false;
            else if (! on && h > 1.0f - c.seqChaos * 0.15f)
            {
                on = true; ghost = true; acc = false; vel = 0.45f;
                static const float ghostNotes[] { 0, 12, 7, 0, 5, 3, 10, 0 };
                semis = ghostNotes[hash3 (kk, salt, 23) % 8u];
            }
        }
        if (! on) continue;

        // derives de hauteur : evolution (non destructive) et chaos de notes
        int shiftDeg = c.evoNote != nullptr ? (int) c.evoNote[idx].load (std::memory_order_relaxed) : 0;
        if (c.noteChaos > 0.0f && hUni (kk, salt, 30) < c.noteChaos * 0.5f)
            shiftDeg += (int) std::round (hBi (kk, salt, 31) * 2.0f);
        semis = shiftDeg != 0 ? scaleShift (semis, sc, shiftDeg) : quantizeSemis (semis, sc);

        // prolongations (tie) et slide de sortie
        int ties = 0;
        while (ties < len - 1)
        {
            const int nidx = pat.stepIndex (k + ties + 1, salt);
            if (! (stepOn (nidx) && pat.steps[nidx].tie)) break;
            ++ties;
        }
        const int lastIdx = ties > 0 ? pat.stepIndex (k + ties, salt) : idx;
        const bool slideOut = pat.steps[lastIdx].slide != 0;

        int rat = juce::jlimit (1, 4, (int) st.rat);
        if (ratChaos > 0.0f && hUni (kk, salt, 3) < ratChaos * 0.3f) rat = 2 + (int) (hUni (kk, salt, 4) * 2.99f);

        const double base = (double) k * stepLen + grooveOffset (groove, k, c.swing) * stepLen + st.nudge / 100.0 * stepLen;
        for (int r = 0; r < rat; ++r)
        {
            const double te = base + r * stepLen / rat + humanPpq * hBi (kk, salt, 10u + (uint32_t) r);
            if (te < ppqStart || te >= ppqEnd) continue;
            BassEvent e;
            e.semis = semis;
            e.vel = vel * std::pow (0.85f, (float) r);
            e.accent = acc && r == 0;
            e.ghost = ghost;
            e.pat = (int8_t) c.pattern;
            e.step = (int8_t) idx;
            e.hitKey = hash3 (kk, (uint32_t) r, salt);
            e.barKey = hash3 ((uint32_t) (k / len), 0xBA5u, salt);
            const bool last = r == rat - 1;
            if (rat > 1 && ! last) e.gatePpq = stepLen / rat * 0.5;
            else if (slideOut && ! ghost) e.gatePpq = (ties + 1) * stepLen - r * stepLen / rat + stepLen * 0.15;   // la porte chevauche la note suivante
            else e.gatePpq = ties * stepLen + (stepLen / rat) * (double) st.gate / 100.0;
            emit (te, e);
        }
    }
}

} // namespace talea
