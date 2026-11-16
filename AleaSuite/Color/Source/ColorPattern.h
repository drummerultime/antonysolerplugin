#pragma once

#include "ColorCommon.h"

namespace color
{
// Un pas rythmique de COLOR. Chaque coup joue l'accord en vigueur (ou une partie : haut, basse...).
//  len   = duree tenue en pas (1..32), gate = pourcentage de cette duree
//  part  = 0 tout, 1 note du haut (melodie), 2 basse seule, 3 sans basse
//  strum = 0 global, 1 vers le haut, 2 vers le bas
//  oct   = decalage d'octave du voicing (-1..+1)
struct CStep
{
    std::atomic<int8_t> on { 0 }, vel { 100 }, acc { 0 }, len { 1 }, gate { 90 }, prob { 100 }, rat { 1 }, nudge { 0 },
                        cond { 0 }, part { 0 }, strum { 0 }, oct { 0 };
    std::atomic<int16_t> plock[kNumTP];

    CStep() { clearLocks(); }
    void clearLocks() { for (auto& l : plock) l = (int16_t) -1; }
    bool hasLocks() const { for (auto& l : plock) if (l.load() >= 0) return true; return false; }

    void set (int o, int v = 100, int a = 0, int l = 1, int g = 90, int p = 100, int r = 1, int nu = 0, int c = 0, int pa = 0, int sr = 0, int oc = 0)
    {
        on = (int8_t) (o ? 1 : 0); vel = (int8_t) juce::jlimit (1, 127, v); acc = (int8_t) (a ? 1 : 0);
        len = (int8_t) juce::jlimit (1, kSteps, l); gate = (int8_t) juce::jlimit (5, 100, g); prob = (int8_t) juce::jlimit (0, 100, p);
        rat = (int8_t) juce::jlimit (1, 4, r); nudge = (int8_t) juce::jlimit (-50, 50, nu); cond = (int8_t) juce::jlimit (0, kNumConds - 1, c);
        part = (int8_t) juce::jlimit (0, 3, pa); strum = (int8_t) juce::jlimit (0, 2, sr); oct = (int8_t) juce::jlimit (-1, 1, oc);
    }
    void reset() { set (0); clearLocks(); }
    void copyFrom (const CStep& s)
    {
        set (s.on, s.vel, s.acc, s.len, s.gate, s.prob, s.rat, s.nudge, s.cond, s.part, s.strum, s.oct);
        for (int i = 0; i < kNumTP; ++i) plock[i] = s.plock[i].load();
    }

    juce::String toString() const
    {
        juce::StringArray f;
        for (int v : { (int) on, (int) vel, (int) acc, (int) len, (int) gate, (int) prob, (int) rat, (int) nudge, (int) cond, (int) part, (int) strum, (int) oct })
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

// accord compacte dans un entier (lecture sans verrou depuis le thread audio)
inline int32_t packChord (const theory::Chord& c)
{
    return (int32_t) ((c.root & 15) | ((c.qual & 63) << 4) | ((c.inversion & 7) << 10) | (((c.bass + 1) & 31) << 13) | ((c.beats & 63) << 18) | (1 << 30));
}
inline theory::Chord unpackChord (int32_t v)
{
    theory::Chord c;
    c.root = (int8_t) (v & 15);
    c.qual = (int8_t) juce::jlimit (0, theory::numQualities() - 1, (v >> 4) & 63);
    c.inversion = (int8_t) ((v >> 10) & 7);
    c.bass = (int8_t) (((v >> 13) & 31) - 1);
    c.beats = (int8_t) juce::jlimit (1, 32, (v >> 18) & 63);
    if (c.root > 11) c.root = 0;
    if (c.bass > 11) c.bass = -1;
    return c;
}

struct CPattern
{
    std::atomic<int8_t> length { 16 }, rate { 1 }, dir { 0 }, groove { 0 };
    CStep steps[kSteps];
    std::atomic<int8_t> numChords { 4 };
    std::atomic<int8_t> keyShift { 0 };          // tonalite propre au pattern (modulation), en demi-tons
    std::atomic<int32_t> chords[kMaxChords];

    CPattern() { for (auto& c : chords) c = packChord ({}); }

    theory::Chord chord (int i) const { return unpackChord (chords[juce::jlimit (0, kMaxChords - 1, i)].load (std::memory_order_relaxed)); }
    void setChord (int i, const theory::Chord& c) { if (i >= 0 && i < kMaxChords) chords[i] = packChord (c); }
    int chordCount() const { return juce::jlimit (1, kMaxChords, (int) numChords.load()); }
    void setProgression (const std::vector<theory::Chord>& p)
    {
        const int n = juce::jlimit (1, kMaxChords, (int) p.size());
        for (int i = 0; i < n; ++i) setChord (i, p[(size_t) i]);
        numChords = (int8_t) n;
    }
    std::vector<theory::Chord> progression() const
    {
        std::vector<theory::Chord> v;
        for (int i = 0; i < chordCount(); ++i) v.push_back (chord (i));
        return v;
    }
    int totalBeats() const
    {
        int t = 0;
        for (int i = 0; i < chordCount(); ++i) t += chord (i).beats;
        return juce::jmax (1, t);
    }

    void copyFrom (const CPattern& o)
    {
        length = o.length.load(); rate = o.rate.load(); dir = o.dir.load(); groove = o.groove.load();
        for (int i = 0; i < kSteps; ++i) steps[i].copyFrom (o.steps[i]);
        copyHarmonyFrom (o);
    }
    void copyHarmonyFrom (const CPattern& o)
    {
        numChords = o.numChords.load();
        keyShift = o.keyShift.load();
        for (int i = 0; i < kMaxChords; ++i) chords[i] = o.chords[i].load();
    }
    void copyRhythmFrom (const CPattern& o)
    {
        length = o.length.load(); rate = o.rate.load(); dir = o.dir.load(); groove = o.groove.load();
        for (int i = 0; i < kSteps; ++i) steps[i].copyFrom (o.steps[i]);
    }
    void clearRhythm() { for (auto& s : steps) s.reset(); }

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
        juce::StringArray ch;
        for (int i = 0; i < chordCount(); ++i)
        {
            const auto c = chord (i);
            ch.add (juce::String (c.root) + ":" + theory::quality (c.qual).sym + ":" + juce::String (c.inversion) + ":" + juce::String (c.bass) + ":" + juce::String (c.beats));
        }
        t.setProperty ("chords", ch.joinIntoString (";"), nullptr);
        t.setProperty ("key", (int) keyShift.load(), nullptr);
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
        std::vector<theory::Chord> prog;
        for (auto& tok : juce::StringArray::fromTokens (t.getProperty ("chords").toString(), ";", ""))
        {
            auto f = juce::StringArray::fromTokens (tok, ":", "");
            if (f.size() < 5) continue;
            theory::Chord c;
            c.root = (int8_t) juce::jlimit (0, 11, f[0].getIntValue());
            const int q = theory::findQuality (f[1].toRawUTF8());
            c.qual = (int8_t) (q >= 0 ? q : theory::QMaj);
            c.inversion = (int8_t) juce::jlimit (0, 6, f[2].getIntValue());
            c.bass = (int8_t) juce::jlimit (-1, 11, f[3].getIntValue());
            c.beats = (int8_t) juce::jlimit (1, 32, f[4].getIntValue());
            prog.push_back (c);
        }
        if (! prog.empty()) setProgression (prog);
        keyShift = (int8_t) juce::jlimit (-11, 11, (int) t.getProperty ("key", 0));
    }
};

struct CBank
{
    CPattern pats[kPatterns];

    juce::ValueTree toTree() const
    {
        juce::ValueTree root ("COLORPATTERNS");
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
};

// ---------------------------------------------------------------------------
// Rythmes predefinis (onglet PERFORM / generateur) : remplit les pas d'un pattern
void applyRhythmPreset (CPattern& p, int preset, uint32_t seed);

} // namespace color
