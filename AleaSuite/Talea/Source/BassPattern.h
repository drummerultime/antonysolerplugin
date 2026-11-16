#pragma once

#include "TaleaCommon.h"

namespace talea
{
// Un pas de basse. Note = demi-tons au-dessus de la tonique (0..24), oct = decalage d'octave.
// Slide = glisse vers la note suivante (la porte reste ouverte), Tie = prolonge la note precedente.
struct BStep
{
    std::atomic<int8_t> on { 0 }, note { 0 }, oct { 0 }, acc { 0 }, slide { 0 }, tie { 0 },
                        prob { 100 }, rat { 1 }, nudge { 0 }, gate { 55 }, vel { 100 }, cond { 0 };
    std::atomic<int16_t> plock[kNumTP];

    BStep() { clearLocks(); }
    void clearLocks() { for (auto& l : plock) l = (int16_t) -1; }
    bool hasLocks() const { for (auto& l : plock) if (l.load() >= 0) return true; return false; }

    void set (int o, int n, int oc = 0, int a = 0, int sl = 0, int ti = 0, int p = 100, int r = 1, int nu = 0, int g = 55, int v = 100, int c = 0)
    {
        on = (int8_t) (o ? 1 : 0); note = (int8_t) juce::jlimit (0, 24, n); oct = (int8_t) juce::jlimit (-2, 2, oc);
        acc = (int8_t) (a ? 1 : 0); slide = (int8_t) (sl ? 1 : 0); tie = (int8_t) (ti ? 1 : 0);
        prob = (int8_t) juce::jlimit (0, 100, p); rat = (int8_t) juce::jlimit (1, 4, r); nudge = (int8_t) juce::jlimit (-50, 50, nu);
        gate = (int8_t) juce::jlimit (5, 100, g); vel = (int8_t) juce::jlimit (1, 127, v); cond = (int8_t) juce::jlimit (0, kNumConds - 1, c);
    }
    void reset() { set (0, 0); clearLocks(); }
    void copyFrom (const BStep& s)
    {
        set (s.on, s.note, s.oct, s.acc, s.slide, s.tie, s.prob, s.rat, s.nudge, s.gate, s.vel, s.cond);
        for (int i = 0; i < kNumTP; ++i) plock[i] = s.plock[i].load();
    }

    juce::String toString() const
    {
        juce::StringArray f;
        for (int v : { (int) on, (int) note, (int) oct, (int) acc, (int) slide, (int) tie, (int) prob, (int) rat, (int) nudge, (int) gate, (int) vel, (int) cond })
            f.add (juce::String (v));
        juce::String r = f.joinIntoString (",");
        juce::StringArray locks;
        for (int i = 0; i < kNumTP; ++i)
            if (plock[i].load() >= 0) locks.add (juce::String (tpInfo (i).id) + "=" + juce::String ((int) plock[i].load()));
        if (! locks.isEmpty()) r << "|" << locks.joinIntoString ("/");
        return r;
    }
    void fromString (const juce::String& str)
    {
        reset();
        auto f = juce::StringArray::fromTokens (str.upToFirstOccurrenceOf ("|", false, false), ",", "");
        if (f.size() < 12) return;
        set (f[0].getIntValue(), f[1].getIntValue(), f[2].getIntValue(), f[3].getIntValue(), f[4].getIntValue(), f[5].getIntValue(),
             f[6].getIntValue(), f[7].getIntValue(), f[8].getIntValue(), f[9].getIntValue(), f[10].getIntValue(), f[11].getIntValue());
        for (auto& kv : juce::StringArray::fromTokens (str.fromFirstOccurrenceOf ("|", false, false), "/", ""))
        {
            const auto key = kv.upToFirstOccurrenceOf ("=", false, false);
            for (int i = 0; i < kNumTP; ++i)
                if (key == tpInfo (i).id) plock[i] = (int16_t) juce::jlimit (0, 1000, kv.fromFirstOccurrenceOf ("=", false, false).getIntValue());
        }
    }
};

struct BPattern
{
    std::atomic<int8_t> length { 16 }, rate { 1 }, dir { 0 }, groove { 0 };
    BStep steps[kSteps];

    void copyFrom (const BPattern& o)
    {
        length = o.length.load(); rate = o.rate.load(); dir = o.dir.load(); groove = o.groove.load();
        for (int i = 0; i < kSteps; ++i) steps[i].copyFrom (o.steps[i]);
    }
    void clear() { for (auto& s : steps) s.reset(); }

    int stepIndex (int64_t k, uint32_t salt) const noexcept
    {
        const int len = juce::jlimit (1, kSteps, (int) length.load());
        const int m = (int) (k % len);
        switch (dir.load())
        {
            case 1: return len - 1 - m;
            case 2: { if (len == 1) return 0; const int period = 2 * len - 2; const int p = (int) (k % period); return p < len ? p : period - p; }
            case 3: return (int) (hash3 ((uint32_t) k, 0x5EEDu, salt) % (uint32_t) len);
            default: return m;
        }
    }

    juce::ValueTree toTree (int index) const
    {
        juce::ValueTree t ("P");
        t.setProperty ("i", index, nullptr);
        t.setProperty ("len", (int) length.load(), nullptr);
        t.setProperty ("rate", (int) rate.load(), nullptr);
        t.setProperty ("dir", (int) dir.load(), nullptr);
        t.setProperty ("groove", (int) groove.load(), nullptr);
        juce::StringArray st;
        for (const auto& s : steps) st.add (s.toString());
        t.setProperty ("steps", st.joinIntoString (";"), nullptr);
        return t;
    }
    void fromTree (const juce::ValueTree& t)
    {
        length = (int8_t) juce::jlimit (1, kSteps, (int) t.getProperty ("len", 16));
        rate   = (int8_t) juce::jlimit (0, kNumRates - 1, (int) t.getProperty ("rate", 1));
        dir    = (int8_t) juce::jlimit (0, 3, (int) t.getProperty ("dir", 0));
        groove = (int8_t) juce::jlimit (0, kNumGrooves - 1, (int) t.getProperty ("groove", 0));
        auto st = juce::StringArray::fromTokens (t.getProperty ("steps").toString(), ";", "");
        for (int i = 0; i < kSteps; ++i) { if (i < st.size()) steps[i].fromString (st[i]); else steps[i].reset(); }
    }
};

struct BBank
{
    BPattern pats[kPatterns];

    juce::ValueTree toTree() const
    {
        juce::ValueTree root ("BASSPATTERNS");
        for (int p = 0; p < kPatterns; ++p) root.appendChild (pats[p].toTree (p), nullptr);
        return root;
    }
    void fromTree (const juce::ValueTree& root)
    {
        if (! root.isValid()) return;
        for (auto t : root)
        {
            const int p = t.getProperty ("i", -1);
            if (p >= 0 && p < kPatterns) pats[p].fromTree (t);
        }
    }

    // ligne acide par defaut (tonique = 0)
    void initDefault()
    {
        auto& a = pats[0];
        a.clear(); a.length = 16; a.rate = 1; a.dir = 0; a.groove = 0;
        //           on note oct acc slide tie
        a.steps[0].set  (1, 0,  0, 1, 0, 0);
        a.steps[1].set  (1, 0,  0, 0, 0, 0);
        a.steps[2].set  (1, 12, 0, 0, 1, 0);
        a.steps[3].set  (1, 10, 0, 0, 0, 0);
        a.steps[4].set  (0, 0);
        a.steps[5].set  (1, 7,  0, 1, 0, 0);
        a.steps[6].set  (1, 0,  0, 0, 0, 0, 70);
        a.steps[7].set  (1, 3,  0, 0, 1, 0);
        a.steps[8].set  (1, 5,  0, 1, 0, 0);
        a.steps[9].set  (1, 0,  0, 0, 0, 0);
        a.steps[10].set (1, 12, 0, 0, 0, 0, 100, 2);
        a.steps[11].set (1, 0,  1, 1, 1, 0);
        a.steps[12].set (1, 0,  0, 0, 0, 0);
        a.steps[13].set (0, 0);
        a.steps[14].set (1, 7,  0, 0, 1, 0);
        a.steps[15].set (1, 10, 0, 1, 0, 0, 100, 1, 0, 55, 100, 2);
        for (int p = 1; p < kPatterns; ++p) pats[p].copyFrom (a);
    }
};

} // namespace talea
