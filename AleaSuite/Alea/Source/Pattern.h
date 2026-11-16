#pragma once

#include "Common.h"

namespace alea
{
// Un pas du sequenceur. Champs atomiques : edites par l'interface, lus par le thread audio.
struct Step
{
    std::atomic<int8_t> on { 0 }, acc { 0 }, prob { 100 }, rat { 1 }, nudge { 0 }, tune { 0 }, cond { 0 }, slide { 0 };
    // parameter locks : -1 = pas de verrou, sinon 0..1000 (valeur normalisee x1000)
    std::atomic<int16_t> plock[kNumVP];

    Step() { clearLocks(); }

    void set (int o, int a, int p, int r, int n, int t, int c = 0, int sl = 0)
    {
        slide = (int8_t) (sl ? 1 : 0);
        on = (int8_t) o; acc = (int8_t) a; prob = (int8_t) juce::jlimit (0, 100, p);
        rat = (int8_t) juce::jlimit (1, 4, r); nudge = (int8_t) juce::jlimit (-50, 50, n);
        tune = (int8_t) juce::jlimit (-24, 24, t); cond = (int8_t) juce::jlimit (0, kNumConds - 1, c);
    }
    void clearLocks() { for (auto& l : plock) l = (int16_t) -1; }
    bool hasLocks() const { for (auto& l : plock) if (l.load() >= 0) return true; return false; }
    void reset() { set (0, 0, 100, 1, 0, 0, 0, 0); clearLocks(); }
    void copyFrom (const Step& s)
    {
        set (s.on, s.acc, s.prob, s.rat, s.nudge, s.tune, s.cond, s.slide);
        for (int i = 0; i < kNumVP; ++i) plock[i] = s.plock[i].load();
    }

    juce::String toString() const
    {
        juce::String r = juce::String ((int) on) + "," + juce::String ((int) acc) + "," + juce::String ((int) prob) + ","
                       + juce::String ((int) rat) + "," + juce::String ((int) nudge) + "," + juce::String ((int) tune) + ","
                       + juce::String ((int) cond) + "," + juce::String ((int) slide);
        juce::StringArray locks;
        for (int i = 0; i < kNumVP; ++i)
            if (plock[i].load() >= 0) locks.add (juce::String (vpInfo (i).id) + "=" + juce::String ((int) plock[i].load()));
        if (! locks.isEmpty()) r << "|" << locks.joinIntoString ("/");
        return r;
    }
    void fromString (const juce::String& str)
    {
        reset();
        const auto main = str.upToFirstOccurrenceOf ("|", false, false);
        auto f = juce::StringArray::fromTokens (main, ",", "");
        if (f.size() < 6) return;
        set (f[0].getIntValue(), f[1].getIntValue(), f[2].getIntValue(), f[3].getIntValue(),
             f[4].getIntValue(), f[5].getIntValue(), f.size() > 6 ? f[6].getIntValue() : 0, f.size() > 7 ? f[7].getIntValue() : 0);
        const auto lk = str.fromFirstOccurrenceOf ("|", false, false);
        for (auto& kv : juce::StringArray::fromTokens (lk, "/", ""))
        {
            const auto key = kv.upToFirstOccurrenceOf ("=", false, false);
            for (int i = 0; i < kNumVP; ++i)
                if (key == vpInfo (i).id)
                    plock[i] = (int16_t) juce::jlimit (0, 1000, kv.fromFirstOccurrenceOf ("=", false, false).getIntValue());
        }
    }
};

struct TrackPat
{
    std::atomic<int8_t> length { 16 }, rate { 1 }, dir { 0 }, groove { 0 };
    Step steps[kMaxSteps];

    void copyFrom (const TrackPat& o)
    {
        length = o.length.load(); rate = o.rate.load(); dir = o.dir.load(); groove = o.groove.load();
        for (int i = 0; i < kMaxSteps; ++i) steps[i].copyFrom (o.steps[i]);
    }
    void clear() { for (auto& s : steps) s.reset(); }

    // index de pas joue pour le compteur absolu k, selon le sens de lecture
    int stepIndex (int64_t k, uint32_t salt) const noexcept
    {
        const int len = juce::jlimit (1, kMaxSteps, (int) length.load());
        const int m = (int) (k % len);
        switch (dir.load())
        {
            case 1: return len - 1 - m;
            case 2:
            {
                if (len == 1) return 0;
                const int period = 2 * len - 2;
                const int p = (int) (k % period);
                return p < len ? p : period - p;
            }
            case 3: return (int) (hash3 ((uint32_t) k, 0x5EEDu, salt) % (uint32_t) len);
            default: return m;
        }
    }
};

struct Pattern
{
    TrackPat tracks[kNumTracks];
    void copyFrom (const Pattern& o) { for (int t = 0; t < kNumTracks; ++t) tracks[t].copyFrom (o.tracks[t]); }
};

struct PatternBank
{
    Pattern pats[kNumPatterns];

    juce::ValueTree toTree() const
    {
        juce::ValueTree root ("PATTERNS");
        for (int p = 0; p < kNumPatterns; ++p)
        {
            juce::ValueTree pt ("P");
            pt.setProperty ("i", p, nullptr);
            for (int t = 0; t < kNumTracks; ++t)
            {
                const auto& tp = pats[p].tracks[t];
                juce::ValueTree tt ("T");
                tt.setProperty ("i", t, nullptr);
                tt.setProperty ("len", (int) tp.length.load(), nullptr);
                tt.setProperty ("rate", (int) tp.rate.load(), nullptr);
                tt.setProperty ("dir", (int) tp.dir.load(), nullptr);
                tt.setProperty ("groove", (int) tp.groove.load(), nullptr);
                juce::StringArray steps;
                for (const auto& s : tp.steps) steps.add (s.toString());
                tt.setProperty ("steps", steps.joinIntoString (";"), nullptr);
                pt.appendChild (tt, nullptr);
            }
            root.appendChild (pt, nullptr);
        }
        return root;
    }

    void fromTree (const juce::ValueTree& root)
    {
        if (! root.isValid()) return;
        for (auto pt : root)
        {
            const int p = pt.getProperty ("i", -1);
            if (p < 0 || p >= kNumPatterns) continue;
            for (auto tt : pt)
            {
                const int t = tt.getProperty ("i", -1);
                if (t < 0 || t >= kNumTracks) continue;
                auto& tp = pats[p].tracks[t];
                tp.length = (int8_t) juce::jlimit (1, kMaxSteps, (int) tt.getProperty ("len", 16));
                tp.rate   = (int8_t) juce::jlimit (0, kNumRates - 1, (int) tt.getProperty ("rate", 1));
                tp.dir    = (int8_t) juce::jlimit (0, 3, (int) tt.getProperty ("dir", 0));
                tp.groove = (int8_t) juce::jlimit (0, kNumGrooves - 1, (int) tt.getProperty ("groove", 0));
                auto steps = juce::StringArray::fromTokens (tt.getProperty ("steps").toString(), ";", "");
                for (int i = 0; i < kMaxSteps; ++i)
                {
                    if (i < steps.size()) tp.steps[i].fromString (steps[i]);
                    else tp.steps[i].reset();
                }
            }
        }
    }

    void initDefault()
    {
        auto& a = pats[0];
        for (auto& tp : a.tracks) { tp.clear(); tp.length = 16; tp.rate = 1; tp.dir = 0; tp.groove = 0; }

        auto on = [&] (int t, int i, int acc = 0, int prob = 100, int rat = 1, int tune = 0, int cond = 0, int slide = 0)
        { a.tracks[t].steps[i].set (1, acc, prob, rat, 0, tune, cond, slide); };

        for (int i : { 0, 4, 8, 12 }) on (0, i);                          // Kick
        on (0, 14, 0, 100, 1, 0, 10);                                      //   + coup sur Fill
        on (1, 4); on (1, 12); on (1, 14, 0, 35);                          // Snare
        on (1, 15, 0, 100, 2, 0, 2);                                       //   + roulement 1 fois sur 2 (2:2)
        on (2, 12, 1);                                                      // Clap
        for (int i : { 0, 4, 8, 12 }) on (3, i);                           // C.Hat
        for (int i : { 1, 3, 5, 7, 9, 11, 13, 15 }) on (3, i, 0, 70);
        for (int i : { 2, 6, 10, 14 }) on (4, i, i == 14 ? 1 : 0);          // O.Hat
        on (5, 7); on (5, 13, 0, 100, 1, -5); on (5, 15, 0, 60, 1, 3);      // Tom
        a.tracks[6].length = 12;                                            // Perc : polymetrie 12 pas
        on (6, 3); on (6, 6, 0, 100, 1, 7, 0, 1); on (6, 10, 1, 100, 2, 12);
        a.tracks[6].steps[6].plock[Decay] = 650;                            //   exemple de p-lock
        a.tracks[7].length = 20;                                            // Bell : 20 pas
        on (7, 10, 0, 50); on (7, 14, 0, 30, 1, -5); on (7, 18, 0, 60, 1, 7);

        for (int p = 1; p < kNumPatterns; ++p) pats[p].copyFrom (a);
    }
};

// Copie d'une piste (presse-papier de l'interface)
struct TrackClip
{
    bool valid = false;
    int len = 16, rate = 1, dir = 0, groove = 0;
    juce::String steps[kMaxSteps];

    void grab (const TrackPat& tp)
    {
        valid = true; len = tp.length; rate = tp.rate; dir = tp.dir; groove = tp.groove;
        for (int i = 0; i < kMaxSteps; ++i) steps[i] = tp.steps[i].toString();
    }
    void put (TrackPat& tp) const
    {
        if (! valid) return;
        tp.length = (int8_t) len; tp.rate = (int8_t) rate; tp.dir = (int8_t) dir; tp.groove = (int8_t) groove;
        for (int i = 0; i < kMaxSteps; ++i) tp.steps[i].fromString (steps[i]);
    }
};

} // namespace alea
