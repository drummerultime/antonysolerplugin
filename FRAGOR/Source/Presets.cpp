#include "Presets.h"

namespace fragor
{
// ---------------------------------------------------------------- index lisibles
namespace T  { enum { Basic, Growl, Yoi, Metal, Screech, Fold, Hollow, Digital, Pulse, Reese, Talk, Chainsaw }; }
namespace W  { enum { Off, BendP, BendM, Sync, Pwm, Mirror, Quant, FmOsc, FmSub, Ring }; }
namespace F  { enum { LP24, LP12, BP, HP, Notch, Vowel, CombP, CombM }; }
namespace D  { enum { Tube, Hard, Foldback, SineFold, Rectify, Diode, Overdrive }; }
namespace S  { enum { Sine, Tri, SawD, SawU, Square, Wub, Decay, SH, Drift, Custom }; }
// divisions LFO / delay
namespace V  { enum { Bars4, Bars2, d1, d2, d2T, d4D, d4, d4T, d8D, d8, d8T, d16D, d16, d16T, d32, d32T, d64 }; }
// pas du séquenceur
namespace Q  { enum { Off, Open, s1, s2, s2T, s4, s4T, s8, s8T, s16, s16T, s32, s32T, s64 }; }
namespace QL { enum { Half, Quarter, Eighth, Sixteenth }; }

// ---------------------------------------------------------------- courbes
LfoCurve defaultCurve() { return { { 0.0f, 0.0f, -0.4f }, { 0.5f, 1.0f, 0.4f }, { 1.0f, 0.0f, 0.0f } }; }

void renderCurve (const LfoCurve& cIn, float* table, int size)
{
    LfoCurve c = cIn;
    if (c.size() < 2) c = defaultCurve();
    std::sort (c.begin(), c.end(), [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
    c.front().x = 0.0f;
    c.back().x = 1.0f;
    size_t seg = 0;
    for (int i = 0; i < size; ++i)
    {
        const float x = (float) i / (float) (size - 1);
        while (seg + 2 < c.size() && x > c[seg + 1].x) ++seg;
        const auto& a = c[seg];
        const auto& b = c[seg + 1];
        const float w = b.x - a.x;
        float t = w > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (x - a.x) / w) : 1.0f;
        const float k = a.c;
        if (k > 0.001f)       t = std::pow (t, 1.0f + k * 6.0f);
        else if (k < -0.001f) t = 1.0f - std::pow (1.0f - t, 1.0f - k * 6.0f);
        table[i] = juce::jlimit (0.0f, 1.0f, a.y + (b.y - a.y) * t);
    }
}

juce::String curveToString (const LfoCurve& c)
{
    juce::StringArray s;
    for (auto& p : c) s.add (juce::String (p.x, 4) + ":" + juce::String (p.y, 4) + ":" + juce::String (p.c, 3));
    return s.joinIntoString (";");
}

LfoCurve curveFromString (const juce::String& str)
{
    LfoCurve c;
    for (auto& t : juce::StringArray::fromTokens (str, ";", ""))
    {
        auto v = juce::StringArray::fromTokens (t, ":", "");
        if (v.size() == 3) c.push_back ({ juce::jlimit (0.0f, 1.0f, v[0].getFloatValue()), juce::jlimit (0.0f, 1.0f, v[1].getFloatValue()),
                                          juce::jlimit (-1.0f, 1.0f, v[2].getFloatValue()) });
    }
    if (c.size() < 2) c = defaultCurve();
    return c;
}

juce::StringArray curvePresetNames()
{
    return { "WUB", "SAW STAB", "DOUBLE BUMP", "TRIPLET BUMPS", "STAIRS", "TALK", "GATE 3:1", "RISE", "STUTTER", "RAMP SNAP" };
}

LfoCurve curvePreset (int i)
{
    switch (i)
    {
        case 1:  return { { 0, 1, 0.6f }, { 1, 0, 0 } };
        case 2:  return { { 0, 0, -0.3f }, { 0.25f, 1, 0.3f }, { 0.5f, 0.05f, -0.3f }, { 0.75f, 1, 0.3f }, { 1, 0, 0 } };
        case 3:  return { { 0, 0, -0.3f }, { 0.1667f, 1, 0.3f }, { 0.3333f, 0, -0.3f }, { 0.5f, 1, 0.3f }, { 0.6667f, 0, -0.3f }, { 0.8333f, 1, 0.3f }, { 1, 0, 0 } };
        case 4:  return { { 0, 0, 0 }, { 0.249f, 0, 0 }, { 0.25f, 0.33f, 0 }, { 0.499f, 0.33f, 0 }, { 0.5f, 0.66f, 0 }, { 0.749f, 0.66f, 0 }, { 0.75f, 1, 0 }, { 1, 1, 0 } };
        case 5:  return { { 0, 0.2f, -0.3f }, { 0.15f, 1, 0.4f }, { 0.4f, 0.35f, -0.2f }, { 0.55f, 0.85f, 0.3f }, { 0.8f, 0.1f, 0 }, { 1, 0.2f, 0 } };
        case 6:  return { { 0, 1, 0 }, { 0.74f, 1, 0 }, { 0.75f, 0, 0 }, { 1, 0, 0 } };
        case 7:  return { { 0, 0, 0.6f }, { 1, 1, 0 } };
        case 8:  return { { 0, 1, 0.5f }, { 0.12f, 0.1f, 0 }, { 0.125f, 1, 0.5f }, { 0.245f, 0.1f, 0 }, { 0.25f, 0.9f, 0.4f }, { 0.5f, 0.0f, 0 },
                          { 0.5f, 0, -0.5f }, { 0.75f, 1, 0 }, { 0.875f, 1, 0.5f }, { 1, 0, 0 } };
        case 9:  return { { 0, 0, 0.4f }, { 0.85f, 1, 0 }, { 0.86f, 0, 0 }, { 1, 0, 0 } };
        default: return defaultCurve();
    }
}

std::array<int, numFxModules> defaultFxOrder() { return { fxDist1, fxCrush, fxDist2, fxOtt, fxChorus, fxDelay, fxReverb }; }

void PatchExtras::reset()
{
    for (auto& c : curves) c = defaultCurve();
    order = defaultFxOrder();
}

// ---------------------------------------------------------------- PatchBuilder
PatchBuilder& PatchBuilder::curve (int lfo, const LfoCurve& c)
{
    extras.curves[(size_t) juce::jlimit (0, 2, lfo - 1)] = c;
    return set ("l" + juce::String (lfo) + "Shape", (float) LfoShape::Custom);
}

PatchBuilder& PatchBuilder::fxOrder (std::initializer_list<int> order)
{
    std::array<int, numFxModules> o {};
    std::array<bool, numFxModules> used {};
    size_t i = 0;
    for (int v : order) if (i < o.size() && v >= 0 && v < numFxModules && ! used[(size_t) v]) { o[i++] = v; used[(size_t) v] = true; }
    for (int v : defaultFxOrder()) if (! used[(size_t) v]) o[i++] = v;
    extras.order = o;
    return *this;
}

PatchBuilder& PatchBuilder::set (const juce::String& id, float value)
{
    if (auto* p = state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        jassertfalse;
    return *this;
}

PatchBuilder& PatchBuilder::mod (int slot, int src, int dst, float amt)
{
    const juce::String p = "m" + juce::String (slot);
    set (p + "Src", (float) src);
    set (p + "Dst", (float) dst);
    set (p + "Amt", amt);
    modSlot = std::max (modSlot, slot + 1);
    return *this;
}

PatchBuilder& PatchBuilder::nextMod (int src, int dst, float amt)
{
    if (modSlot <= kNumMatrixSlots) mod (modSlot, src, dst, amt);
    return *this;
}

PatchBuilder& PatchBuilder::seq (std::initializer_list<int> steps, int stepLen)
{
    on ("seqOn");
    set ("seqSteps", (float) steps.size());
    set ("seqLen", (float) stepLen);
    int i = 1;
    for (int s : steps) set ("seq" + juce::String (i++), (float) s);
    return *this;
}

PatchBuilder& PatchBuilder::macroNames (const juce::String& a, const juce::String& b, const juce::String& c, const juce::String& d)
{
    macros = { a, b, c, d };
    return *this;
}

void PatchBuilder::defaults()
{
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    extras.reset();
    modSlot = 1;
    macros = { "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4" };
}

// ---------------------------------------------------------------- bases communes
namespace
{
    void bassBase (PatchBuilder& b)
    {
        b.defaults();
        b.set ("voiceMode", 1).set ("glide", 0.0f).set ("bendRange", 12);
        b.on ("subOn").set ("subShape", 0).set ("subOct", 1).set ("subLevel", 0.55f).on ("subDirect");
        b.set ("ampA", 0.002f).set ("ampD", 0.5f).set ("ampS", 1.0f).set ("ampR", 0.05f).set ("ampVel", 0.1f);
        b.on ("ottOn").set ("ottDepth", 0.45f).set ("ottTime", 0.25f);
        b.on ("distOn").set ("distMode", D::Tube).set ("distDrive", 0.3f).set ("distTone", 14000.0f);
        b.on ("revOn").set ("revMix", 0.0f).set ("revSize", 0.55f);
        b.set ("clip", 0.4f).set ("masterGain", -6.0f);
        b.on ("subMono").set ("subMonoFreq", 140.0f);
    }

    // Macros standard : 1 = vitesse/mouvement, 2 = grain, 3 = ouverture, 4 = espace
    void stdMacros (PatchBuilder& b, int m3Dest = dstCutoff, float m3Amt = 0.3f)
    {
        b.nextMod (srcMacro1, dstLfo1Rate, 0.25f);
        b.nextMod (srcMacro2, dstDistDrive, 0.45f);
        b.nextMod (srcMacro3, m3Dest, m3Amt);
        b.nextMod (srcMacro4, dstReverbMix, 0.45f);
        b.macroNames ("SPEED", "GRIT", "OPEN", "SPACE");
    }

    void oscA (PatchBuilder& b, int table, float pos, int warpMode = W::Off, float warp = 0.0f, int uni = 1, float det = 0.15f, float level = 0.8f)
    {
        b.on ("aOn").set ("aTable", (float) table).set ("aPos", pos).set ("aWarpMode", (float) warpMode).set ("aWarp", warp)
         .set ("aUni", (float) uni).set ("aDetune", det).set ("aLevel", level);
    }
    void oscB (PatchBuilder& b, int table, float pos, int warpMode = W::Off, float warp = 0.0f, int uni = 1, float det = 0.15f, float level = 0.5f)
    {
        b.on ("bOn").set ("bTable", (float) table).set ("bPos", pos).set ("bWarpMode", (float) warpMode).set ("bWarp", warp)
         .set ("bUni", (float) uni).set ("bDetune", det).set ("bLevel", level);
    }
    void filter (PatchBuilder& b, int type, float cutoff, float reso, float drive = 0.0f, float morph = 0.0f, float key = 0.0f)
    {
        b.on ("fltOn").set ("fltType", (float) type).set ("fltCutoff", cutoff).set ("fltReso", reso)
         .set ("fltDrive", drive).set ("fltMorph", morph).set ("fltKey", key);
    }
    void lfo (PatchBuilder& b, int n, int shape, int div, bool retrig = true, float curve = 0.0f, float smooth = 0.05f)
    {
        const juce::String p = "l" + juce::String (n);
        b.set (p + "Shape", (float) shape).on (p + "Sync").set (p + "Div", (float) div).on (p + "Retrig", retrig)
         .set (p + "Curve", curve).set (p + "Smooth", smooth);
    }
    void lfoFree (PatchBuilder& b, int n, int shape, float hz, bool retrig = false)
    {
        const juce::String p = "l" + juce::String (n);
        b.set (p + "Shape", (float) shape).on (p + "Sync", false).set (p + "Rate", hz).on (p + "Retrig", retrig);
    }
    void dist (PatchBuilder& b, int mode, float drive, float tone = 14000.0f, float mix = 1.0f, float bias = 0.0f)
    {
        b.on ("distOn").set ("distMode", (float) mode).set ("distDrive", drive).set ("distTone", tone).set ("distMix", mix).set ("distBias", bias);
    }
}

// ---------------------------------------------------------------- Presets d'usine
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets
    {
        { "INIT", "Init", [] (PatchBuilder& b) { b.defaults(); } },

        // ======================================================= WOBBLE
        { "WOBBLE", "Wub Classic", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.55f, W::Off, 0.0f, 3, 0.12f, 0.85f);
            oscB (b, T::Basic, 0.75f, W::Off, 0.0f, 1, 0.0f, 0.35f);
            b.set ("bOct", -1);
            filter (b, F::LP24, 70.0f, 0.5f, 0.35f);
            lfo (b, 1, S::Wub, V::d8);
            b.mod (1, srcLfo1, dstCutoff, 0.62f).mod (2, srcLfo1, dstAPos, 0.15f);
            dist (b, D::Tube, 0.35f);
            stdMacros (b);
        } },

        { "WOBBLE", "Wobble Seq 140", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.6f, W::BendP, 0.2f, 4, 0.14f, 0.85f);
            oscB (b, T::Pulse, 0.2f, W::Off, 0.0f, 1, 0.0f, 0.3f);
            filter (b, F::LP24, 80.0f, 0.55f, 0.4f);
            lfo (b, 1, S::Wub, V::d8, true, 0.2f);
            b.seq ({ Q::s8, Q::s8, Q::s16, Q::s16, Q::s8T, Q::s4, Q::s16T, Q::s32 }, QL::Quarter);
            b.mod (1, srcLfo1, dstCutoff, 0.6f).mod (2, srcLfo1, dstAWarp, 0.4f).mod (3, srcLfo1, dstDistDrive, 0.15f);
            dist (b, D::Tube, 0.4f);
            stdMacros (b);
        } },

        { "WOBBLE", "Wobble Triplet Madness", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Hollow, 0.35f, W::Mirror, 0.3f, 3, 0.1f, 0.85f);
            oscB (b, T::Basic, 0.5f, W::Sync, 0.2f, 2, 0.1f, 0.4f);
            filter (b, F::LP24, 90.0f, 0.6f, 0.3f);
            lfo (b, 1, S::Wub, V::d8T, true, 0.3f);
            b.seq ({ Q::s8T, Q::s8T, Q::s16T, Q::s8T, Q::s32T, Q::s16T, Q::s4T, Q::Open }, QL::Quarter);
            b.mod (1, srcLfo1, dstCutoff, 0.6f).mod (2, srcLfo1, dstBWarp, 0.6f).mod (3, srcLfo1, dstAPos, 0.3f);
            dist (b, D::Foldback, 0.3f, 10000.0f, 0.7f);
            stdMacros (b);
        } },

        { "WOBBLE", "Vowel Wobble", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.5f, W::Off, 0.0f, 3, 0.1f, 0.85f);
            oscB (b, T::Chainsaw, 0.2f, W::Off, 0.0f, 1, 0.0f, 0.35f);
            filter (b, F::Vowel, 1800.0f, 0.55f, 0.35f, 0.0f);
            lfo (b, 1, S::Wub, V::d8);
            b.seq ({ Q::s8, Q::s16, Q::s8, Q::s16T, Q::s8, Q::s4, Q::s32, Q::s16 }, QL::Quarter);
            b.mod (1, srcLfo1, dstMorph, 1.0f).mod (2, srcLfo1, dstCutoff, 0.12f);
            dist (b, D::Tube, 0.45f);
            stdMacros (b, dstMorph, 0.5f);
        } },

        { "WOBBLE", "Wub Brass", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.62f, W::Off, 0.0f, 5, 0.18f, 0.8f);
            oscB (b, T::Metal, 0.25f, W::Off, 0.0f, 1, 0.0f, 0.45f);
            filter (b, F::LP24, 120.0f, 0.4f, 0.5f);
            lfo (b, 1, S::Wub, V::d4, true, -0.3f);
            b.seq ({ Q::s4, Q::s8, Q::s8, Q::s16, Q::s4, Q::s4T, Q::s8T, Q::s16T }, QL::Half);
            b.mod (1, srcLfo1, dstCutoff, 0.6f).mod (2, srcLfo1, dstBPos, 0.6f);
            dist (b, D::Overdrive, 0.4f);
            b.set ("ottDepth", 0.6f);
            stdMacros (b);
        } },

        { "WOBBLE", "Drunk Wob", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Reese, 0.3f, W::Off, 0.0f, 5, 0.2f, 0.85f);
            filter (b, F::LP24, 100.0f, 0.5f, 0.3f);
            lfoFree (b, 1, S::Wub, 3.0f, true);
            lfo (b, 2, S::SH, V::d8, false);
            b.mod (1, srcLfo1, dstCutoff, 0.6f).mod (2, srcLfo2, dstLfo1Rate, 0.6f).mod (3, srcLfo1, dstAPos, 0.4f);
            dist (b, D::Tube, 0.4f);
            stdMacros (b);
        } },

        // ======================================================= GROWL
        { "GROWL", "Growl Machine", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Growl, 0.2f, W::FmOsc, 0.25f, 2, 0.08f, 0.85f);
            oscB (b, T::Basic, 0.0f, W::Off, 0.0f, 1, 0.0f, 0.0f);
            filter (b, F::LP24, 2600.0f, 0.3f, 0.2f);
            lfo (b, 1, S::Wub, V::d8, true, 0.0f);
            b.seq ({ Q::s8, Q::s8, Q::s16, Q::s8, Q::s16T, Q::s16T, Q::s8, Q::s32 }, QL::Quarter);
            b.mod (1, srcLfo1, dstAPos, 0.65f).mod (2, srcLfo1, dstAWarp, 0.35f).mod (3, srcLfo1, dstCutoff, 0.15f);
            dist (b, D::Foldback, 0.32f, 12000.0f, 0.8f);
            b.set ("ottDepth", 0.65f);
            b.mod (4, srcMacro1, dstLfo1Rate, 0.25f).mod (5, srcMacro2, dstDistDrive, 0.4f)
             .mod (6, srcMacro3, dstAWarp, 0.5f).mod (7, srcMacro4, dstReverbMix, 0.4f);
            b.macroNames ("SPEED", "GRIT", "FM", "SPACE");
        } },

        { "GROWL", "Talking Growl", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Talk, 0.0f, W::BendP, 0.25f, 2, 0.06f, 0.85f);
            filter (b, F::Vowel, 1500.0f, 0.45f, 0.3f);
            lfo (b, 1, S::Tri, V::d4, true);
            lfo (b, 2, S::SawU, V::d8, true);
            b.mod (1, srcLfo1, dstAPos, 1.0f).mod (2, srcLfo2, dstMorph, 0.9f);
            dist (b, D::Tube, 0.5f);
            b.set ("ottDepth", 0.6f);
            b.mod (3, srcMacro1, dstLfo1Rate, 0.25f).mod (4, srcMacro2, dstDistDrive, 0.4f)
             .mod (5, srcMacro3, dstMorph, 0.5f).mod (6, srcMacro4, dstReverbMix, 0.4f);
            b.macroNames ("SPEED", "GRIT", "MOUTH", "SPACE");
        } },

        { "GROWL", "Metal Grind", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Metal, 0.35f, W::Off, 0.0f, 2, 0.06f, 0.8f);
            oscB (b, T::Fold, 0.4f, W::Ring, 0.6f, 1, 0.0f, 0.45f);
            filter (b, F::BP, 1100.0f, 0.35f, 0.4f);
            b.set ("fltMix", 0.75f);
            lfo (b, 1, S::Wub, V::d16);
            b.seq ({ Q::s16, Q::s16, Q::s8, Q::s32, Q::s16, Q::s16T, Q::s8, Q::Off }, QL::Eighth);
            b.mod (1, srcLfo1, dstAPos, 0.5f).mod (2, srcLfo1, dstCutoff, 0.25f).mod (3, srcLfo1, dstBWarp, 0.4f);
            dist (b, D::Hard, 0.4f, 9000.0f);
            b.set ("ottDepth", 0.7f);
            stdMacros (b);
        } },

        { "GROWL", "FM Horn", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.0f, W::FmOsc, 0.45f, 1, 0.0f, 0.9f);
            oscB (b, T::Basic, 0.0f, W::Off, 0.0f, 1, 0.0f, 0.0f);
            b.set ("bSemi", 7);
            filter (b, F::LP24, 3000.0f, 0.25f, 0.4f);
            lfo (b, 1, S::Wub, V::d8, true, 0.3f);
            b.seq ({ Q::s8, Q::s8, Q::s8, Q::s16, Q::s4, Q::s8T, Q::s16, Q::s16 }, QL::Quarter);
            b.mod (1, srcLfo1, dstAWarp, 0.5f).mod (2, srcLfo1, dstCutoff, 0.2f);
            dist (b, D::Tube, 0.55f);
            b.set ("ottDepth", 0.55f);
            b.mod (3, srcMacro1, dstLfo1Rate, 0.25f).mod (4, srcMacro2, dstDistDrive, 0.4f)
             .mod (5, srcMacro3, dstAWarp, 0.4f).mod (6, srcMacro4, dstReverbMix, 0.4f);
            b.macroNames ("SPEED", "GRIT", "FM", "SPACE");
        } },

        { "GROWL", "Folded Fury", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Fold, 0.25f, W::BendM, 0.2f, 3, 0.1f, 0.85f);
            filter (b, F::LP24, 1800.0f, 0.3f);
            lfo (b, 1, S::Wub, V::d8, true, 0.4f);
            b.seq ({ Q::s8, Q::s16, Q::s8, Q::s16, Q::s8T, Q::s8T, Q::s32, Q::s16 }, QL::Quarter);
            b.mod (1, srcLfo1, dstAPos, 0.7f).mod (2, srcLfo1, dstCutoff, 0.3f);
            dist (b, D::SineFold, 0.45f, 11000.0f, 0.8f);
            b.set ("ottDepth", 0.7f);
            stdMacros (b);
        } },

        { "GROWL", "Vocal Gibberish", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Talk, 0.4f, W::Off, 0.0f, 2, 0.08f, 0.85f);
            oscB (b, T::Growl, 0.5f, W::Off, 0.0f, 1, 0.0f, 0.35f);
            filter (b, F::Vowel, 1600.0f, 0.5f, 0.3f);
            lfo (b, 1, S::SH, V::d16, true, 0.0f, 0.25f);
            lfo (b, 2, S::Drift, V::d8, true);
            b.mod (1, srcLfo1, dstMorph, 1.0f).mod (2, srcLfo2, dstAPos, 0.6f).mod (3, srcLfo1, dstBPos, 0.5f);
            dist (b, D::Tube, 0.45f);
            b.set ("ottDepth", 0.6f);
            stdMacros (b, dstMorph, 0.4f);
        } },

        { "GROWL", "Mid Bass Stack", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Growl, 0.3f, W::FmSub, 0.2f, 3, 0.1f, 0.8f);
            oscB (b, T::Chainsaw, 0.2f, W::Sync, 0.15f, 2, 0.12f, 0.45f);
            filter (b, F::LP24, 500.0f, 0.45f, 0.4f);
            lfo (b, 1, S::Wub, V::d8, true, 0.15f);
            b.seq ({ Q::s8, Q::s8, Q::s16, Q::s16, Q::s8, Q::s32, Q::s16T, Q::s16T, Q::s8, Q::s8, Q::s16, Q::Open, Q::s4, Q::s4, Q::s32T, Q::s16 }, QL::Quarter);
            b.mod (1, srcLfo1, dstCutoff, 0.5f).mod (2, srcLfo1, dstAPos, 0.5f).mod (3, srcLfo1, dstBWarp, 0.5f)
             .mod (4, srcLfo1, dstAWarp, 0.3f);
            dist (b, D::Foldback, 0.35f, 12000.0f, 0.7f);
            b.set ("ottDepth", 0.7f);
            stdMacros (b);
        } },

        // ======================================================= YOI
        { "YOI", "Yoi Yoi", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Yoi, 0.0f, W::Off, 0.0f, 2, 0.07f, 0.85f);
            oscB (b, T::Basic, 0.5f, W::Off, 0.0f, 1, 0.0f, 0.2f);
            filter (b, F::LP24, 6000.0f, 0.2f, 0.2f);
            b.set ("e3A", 0.12f).set ("e3D", 0.3f).set ("e3S", 1.0f).set ("e3R", 0.1f);
            b.set ("penvAmt", 2.0f).set ("penvDecay", 0.08f);
            b.mod (1, srcEnv3, dstAPos, 1.0f);
            dist (b, D::Tube, 0.5f);
            b.set ("ottDepth", 0.6f);
            b.mod (2, srcMacro1, dstAPos, 0.3f).mod (3, srcMacro2, dstDistDrive, 0.4f)
             .mod (4, srcMacro3, dstCutoff, 0.2f).mod (5, srcMacro4, dstReverbMix, 0.4f);
            b.macroNames ("VOWEL", "GRIT", "OPEN", "SPACE");
        } },

        { "YOI", "Yoi Wobble Rhythm", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Yoi, 0.0f, W::BendP, 0.15f, 3, 0.08f, 0.85f);
            filter (b, F::LP24, 4000.0f, 0.3f, 0.25f);
            lfo (b, 1, S::SawU, V::d8, true, -0.4f);
            b.seq ({ Q::s8, Q::s8, Q::s4, Q::s8, Q::s16, Q::s16, Q::s4, Q::Open }, QL::Quarter);
            b.mod (1, srcLfo1, dstAPos, 1.0f).mod (2, srcLfo1, dstCutoff, 0.12f);
            dist (b, D::Overdrive, 0.45f);
            b.set ("ottDepth", 0.6f);
            stdMacros (b);
        } },

        // ======================================================= RIDDIM
        { "RIDDIM", "Riddim Square", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Pulse, 0.05f, W::Pwm, 0.0f, 1, 0.0f, 0.85f);
            filter (b, F::LP12, 300.0f, 0.35f, 0.3f);
            lfo (b, 1, S::Tri, V::d8T, true, 0.3f);
            b.seq ({ Q::s8T, Q::s8T, Q::s16T, Q::s8T }, QL::Quarter);
            b.mod (1, srcLfo1, dstCutoff, 0.5f).mod (2, srcLfo1, dstAWarp, 0.5f);
            dist (b, D::Hard, 0.5f, 9000.0f);
            b.on ("crushOn").set ("crushBits", 9.0f).set ("crushRate", 0.1f).set ("crushMix", 0.5f);
            b.set ("ottDepth", 0.75f);
            stdMacros (b);
        } },

        { "RIDDIM", "Riddim Hollow Chop", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Hollow, 0.4f, W::Off, 0.0f, 1, 0.0f, 0.9f);
            filter (b, F::CombM, 261.6f, 0.65f, 0.2f, 0.0f, 1.0f);
            b.set ("fltMix", 0.7f);
            lfo (b, 1, S::Square, V::d8T, true, 0.0f, 0.15f);
            b.seq ({ Q::s8T, Q::Off, Q::s16T, Q::s8T, Q::Open, Q::s16T }, QL::Eighth);
            b.mod (1, srcLfo1, dstAPos, 0.5f).mod (2, srcLfo1, dstReso, 0.25f).mod (3, srcLfo1, dstAmp, -0.3f);
            dist (b, D::Hard, 0.45f, 10000.0f);
            b.set ("ottDepth", 0.8f);
            stdMacros (b, dstReso, 0.3f);
        } },

        { "RIDDIM", "Riddim Digital Bite", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Digital, 0.35f, W::Quant, 0.3f, 1, 0.0f, 0.85f);
            filter (b, F::LP12, 400.0f, 0.4f);
            lfo (b, 1, S::Wub, V::d16T, true, 0.3f);
            b.seq ({ Q::s8T, Q::s16T, Q::s16T, Q::s8T, Q::s32T, Q::s16T }, QL::Eighth);
            b.mod (1, srcLfo1, dstCutoff, 0.5f).mod (2, srcLfo1, dstAPos, 0.4f).mod (3, srcLfo1, dstCrush, 0.2f);
            dist (b, D::Hard, 0.4f);
            b.on ("crushOn").set ("crushBits", 7.0f).set ("crushRate", 0.15f).set ("crushMix", 0.6f);
            b.set ("ottDepth", 0.75f);
            stdMacros (b);
        } },

        // ======================================================= TEAROUT
        { "TEAROUT", "Tearout Chainsaw", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Chainsaw, 0.3f, W::Off, 0.0f, 4, 0.12f, 0.8f);
            oscB (b, T::Screech, 0.5f, W::Sync, 0.4f, 2, 0.1f, 0.4f);
            b.set ("bOct", 1);
            filter (b, F::LP24, 550.0f, 0.4f, 0.5f);
            lfo (b, 1, S::Wub, V::d16, true, 0.2f);
            b.seq ({ Q::s16, Q::s16, Q::s32, Q::s16, Q::s8, Q::s32, Q::s16T, Q::s16T }, QL::Eighth);
            b.mod (1, srcLfo1, dstCutoff, 0.55f).mod (2, srcLfo1, dstAPos, 0.5f).mod (3, srcLfo1, dstBWarp, 0.5f);
            dist (b, D::Foldback, 0.55f, 11000.0f, 0.75f);
            b.on ("crushOn").set ("crushBits", 11.0f).set ("crushRate", 0.0f).set ("crushMix", 0.35f);
            b.set ("ottDepth", 0.8f);
            stdMacros (b);
        } },

        { "TEAROUT", "Tear The Roof", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Metal, 0.3f, W::FmSub, 0.22f, 3, 0.1f, 0.85f);
            oscB (b, T::Chainsaw, 0.4f, W::Off, 0.0f, 2, 0.08f, 0.35f);
            filter (b, F::CombP, 261.6f, 0.62f, 0.3f, 0.0f, 1.0f);
            b.set ("fltMix", 0.55f);
            lfo (b, 1, S::SH, V::d16, true, 0.0f, 0.12f);
            lfo (b, 2, S::Wub, V::d8, true);
            b.mod (1, srcLfo1, dstAPos, 0.5f).mod (2, srcLfo2, dstAWarp, 0.25f).mod (3, srcLfo1, dstReso, 0.2f)
             .mod (4, srcLfo2, dstBPos, 0.5f);
            dist (b, D::Foldback, 0.4f, 10000.0f, 0.65f);
            b.set ("ottDepth", 0.75f);
            stdMacros (b, dstAWarp, 0.4f);
        } },

        { "TEAROUT", "Neuro Rip", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Reese, 0.4f, W::BendP, 0.3f, 5, 0.22f, 0.8f);
            oscB (b, T::Fold, 0.5f, W::FmOsc, 0.3f, 1, 0.0f, 0.45f);
            filter (b, F::CombM, 180.0f, 0.6f, 0.4f);
            b.set ("fltMix", 0.65f);
            lfo (b, 1, S::Wub, V::d16, true, 0.0f);
            b.seq ({ Q::s16, Q::s32, Q::s16, Q::s16T, Q::s8, Q::s32T, Q::s16, Q::s64 }, QL::Eighth);
            b.mod (1, srcLfo1, dstCutoff, 0.35f).mod (2, srcLfo1, dstBPos, 0.6f).mod (3, srcLfo1, dstAWarp, 0.4f);
            dist (b, D::Diode, 0.55f, 12000.0f);
            b.set ("ottDepth", 0.8f);
            stdMacros (b);
        } },

        // ======================================================= SCREECH
        { "SCREECH", "Screech Lead", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Screech, 0.4f, W::Off, 0.0f, 5, 0.22f, 0.8f);
            b.set ("aOct", 1).set ("glide", 0.06f).set ("subLevel", 0.25f);
            filter (b, F::LP24, 9000.0f, 0.25f);
            b.set ("e2A", 0.001f).set ("e2D", 0.25f).set ("e2S", 0.3f);
            lfo (b, 1, S::Sine, V::d16, true);
            b.mod (1, srcEnv2, dstAPos, 0.4f).mod (2, srcLfo1, dstAPitch, 0.006f).mod (3, srcModWheel, dstAWarp, 0.5f);
            b.set ("aWarpMode", (float) W::Sync);
            dist (b, D::Tube, 0.3f);
            b.on ("dlyOn").set ("dlyDiv", (float) V::d8D).set ("dlyMix", 0.18f);
            b.set ("revMix", 0.15f);
            stdMacros (b);
        } },

        { "SCREECH", "Sync Screamer", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.5f, W::Sync, 0.3f, 4, 0.15f, 0.85f);
            filter (b, F::HP, 120.0f, 0.3f);
            lfo (b, 1, S::SawU, V::d4, true, 0.3f);
            b.seq ({ Q::s4, Q::s8, Q::s8, Q::s16 }, QL::Quarter);
            b.mod (1, srcLfo1, dstAWarp, 0.7f).mod (2, srcLfo1, dstAPos, 0.3f);
            dist (b, D::Hard, 0.4f, 12000.0f);
            b.set ("ottDepth", 0.7f);
            stdMacros (b, dstAWarp, 0.3f);
        } },

        { "SCREECH", "Skree Bend", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Screech, 0.6f, W::Mirror, 0.4f, 3, 0.15f, 0.85f);
            b.set ("voiceMode", 2).set ("glide", 0.15f).set ("aOct", 1);
            filter (b, F::BP, 1800.0f, 0.4f, 0.4f);
            lfo (b, 1, S::Wub, V::d16T, true);
            b.mod (1, srcLfo1, dstCutoff, 0.3f).mod (2, srcLfo1, dstAPos, 0.3f);
            dist (b, D::Foldback, 0.4f, 12000.0f, 0.6f);
            b.set ("ottDepth", 0.7f);
            stdMacros (b);
        } },

        // ======================================================= REESE
        { "REESE", "Reese Deep", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Reese, 0.2f, W::Off, 0.0f, 6, 0.18f, 0.8f);
            oscB (b, T::Basic, 0.5f, W::Off, 0.0f, 4, 0.3f, 0.45f);
            filter (b, F::LP24, 900.0f, 0.2f, 0.2f);
            lfoFree (b, 2, S::Drift, 0.25f);
            b.mod (1, srcLfo2, dstAPos, 0.6f).mod (2, srcLfo2, dstCutoff, 0.08f);
            dist (b, D::Overdrive, 0.25f);
            b.set ("ottDepth", 0.5f);
            b.mod (3, srcMacro1, dstDetune, 0.4f).mod (4, srcMacro2, dstDistDrive, 0.45f)
             .mod (5, srcMacro3, dstCutoff, 0.3f).mod (6, srcMacro4, dstReverbMix, 0.4f);
            b.macroNames ("DETUNE", "GRIT", "OPEN", "SPACE");
        } },

        { "REESE", "Comb Swarm", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.5f, W::Off, 0.0f, 7, 0.35f, 0.85f);
            filter (b, F::CombP, 261.6f, 0.6f, 0.2f, 0.0f, 1.0f);
            lfo (b, 1, S::Sine, V::d2, false);
            b.mod (1, srcLfo1, dstCutoff, 0.1f).mod (2, srcLfo1, dstReso, 0.3f);
            dist (b, D::Tube, 0.3f);
            b.set ("ottDepth", 0.5f);
            stdMacros (b);
        } },

        { "REESE", "Hollow Wub", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Hollow, 0.2f, W::BendM, 0.3f, 4, 0.16f, 0.85f);
            filter (b, F::Notch, 400.0f, 0.6f, 0.2f);
            lfo (b, 1, S::Tri, V::d4, true);
            b.seq ({ Q::s4, Q::s8, Q::s4, Q::s16, Q::s4, Q::s8T, Q::s2, Q::Open }, QL::Half);
            b.mod (1, srcLfo1, dstCutoff, 0.5f).mod (2, srcLfo1, dstAPos, 0.6f);
            dist (b, D::Tube, 0.35f);
            stdMacros (b);
        } },

        // ======================================================= SUB
        { "SUB", "Sub Pressure", [] (PatchBuilder& b)
        {
            bassBase (b);
            b.on ("aOn", false).on ("bOn", false);
            b.set ("subOct", 2).set ("subLevel", 0.95f);
            b.set ("penvAmt", 12.0f).set ("penvDecay", 0.02f);
            dist (b, D::Tube, 0.15f, 1200.0f, 0.5f);
            b.on ("ottOn", false).set ("clip", 0.2f);
            b.mod (1, srcMacro2, dstDistDrive, 0.4f).mod (2, srcMacro2, dstDistMix, 0.4f);
            b.macroNames ("--", "WARMTH", "--", "--");
        } },

        { "SUB", "808 Glide Sub", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.0f, W::Off, 0.0f, 1, 0.0f, 0.7f);
            b.set ("voiceMode", 2).set ("glide", 0.12f);
            b.set ("subLevel", 0.0f);
            b.set ("penvAmt", 7.0f).set ("penvDecay", 0.04f);
            b.set ("ampD", 1.6f).set ("ampS", 0.0f).set ("ampR", 0.3f);
            dist (b, D::Overdrive, 0.35f, 3000.0f);
            b.on ("ottOn", false);
            b.mod (1, srcMacro2, dstDistDrive, 0.5f);
            b.macroNames ("--", "DIRT", "--", "--");
        } },

        // ======================================================= FX
        { "FX", "Laser Zap", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.0f, W::Off, 0.0f, 3, 0.1f, 0.85f);
            b.set ("subLevel", 0.0f).set ("aOct", 1);
            b.set ("penvAmt", 48.0f).set ("penvDecay", 0.18f);
            b.set ("ampD", 0.35f).set ("ampS", 0.0f).set ("ampR", 0.2f);
            dist (b, D::Foldback, 0.4f);
            b.on ("dlyOn").set ("dlyDiv", (float) V::d16).set ("dlyFb", 0.5f).set ("dlyMix", 0.35f);
            b.set ("voiceMode", 0);
            stdMacros (b);
        } },

        { "FX", "Impact Downlifter", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Growl, 0.6f, W::Off, 0.0f, 6, 0.4f, 0.7f);
            b.set ("noiseLevel", 0.5f).set ("noiseColor", 0.6f);
            b.set ("penvAmt", 36.0f).set ("penvDecay", 2.0f);
            b.set ("ampD", 3.0f).set ("ampS", 0.0f).set ("ampR", 2.0f);
            filter (b, F::LP24, 200.0f, 0.4f);
            b.set ("fltEnvAmt", 0.7f).set ("e2D", 2.5f).set ("e2S", 0.0f);
            dist (b, D::Tube, 0.5f);
            b.set ("revMix", 0.45f).set ("revSize", 0.9f);
            stdMacros (b);
        } },

        { "FX", "Noise Riser", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Screech, 0.0f, W::Sync, 0.0f, 6, 0.4f, 0.4f);
            b.set ("subLevel", 0.0f).set ("aOct", 1).set ("voiceMode", 0);
            b.set ("noiseLevel", 0.8f).set ("noiseColor", 0.7f);
            b.set ("ampA", 4.0f).set ("ampR", 1.0f);
            filter (b, F::BP, 300.0f, 0.5f);
            b.set ("e2A", 4.0f).set ("e2S", 1.0f);
            b.set ("fltEnvAmt", 0.65f);
            b.mod (1, srcEnv2, dstAWarp, 0.9f).mod (2, srcEnv2, dstAPos, 1.0f);
            b.on ("dlyOn").set ("dlyMix", 0.3f).set ("revMix", 0.35f);
            stdMacros (b);
        } },

        // ======================================================= LEAD / PAD / STAB
        { "STAB", "Pluck Stab", [] (PatchBuilder& b)
        {
            bassBase (b);
            b.set ("voiceMode", 0).set ("subLevel", 0.0f);
            oscA (b, T::Basic, 0.5f, W::Off, 0.0f, 5, 0.2f, 0.8f);
            oscB (b, T::Hollow, 0.3f, W::Off, 0.0f, 3, 0.1f, 0.4f);
            filter (b, F::LP24, 250.0f, 0.35f);
            b.set ("fltEnvAmt", 0.55f).set ("e2D", 0.25f).set ("e2S", 0.0f);
            b.set ("ampD", 0.45f).set ("ampS", 0.15f).set ("ampR", 0.3f).set ("ampVel", 0.5f);
            dist (b, D::Tube, 0.2f);
            b.on ("dlyOn").set ("dlyDiv", (float) V::d8D).set ("dlyMix", 0.25f);
            b.set ("revMix", 0.25f).set ("ottDepth", 0.35f);
            stdMacros (b);
        } },

        { "PAD", "Dark Pad", [] (PatchBuilder& b)
        {
            bassBase (b);
            b.set ("voiceMode", 0).set ("subLevel", 0.0f);
            oscA (b, T::Reese, 0.3f, W::Off, 0.0f, 6, 0.25f, 0.7f);
            oscB (b, T::Hollow, 0.5f, W::Off, 0.0f, 4, 0.2f, 0.4f);
            filter (b, F::LP24, 1400.0f, 0.2f);
            b.set ("ampA", 0.8f).set ("ampR", 2.5f);
            lfoFree (b, 2, S::Drift, 0.15f);
            b.mod (1, srcLfo2, dstAPos, 0.5f).mod (2, srcLfo2, dstBPos, 0.4f);
            b.set ("distDrive", 0.1f);
            b.on ("chorusOn").set ("chorusMix", 0.4f);
            b.set ("revMix", 0.4f).set ("revSize", 0.85f).set ("ottDepth", 0.25f);
            stdMacros (b);
        } },

        { "LEAD", "Talkbox Lead", [] (PatchBuilder& b)
        {
            bassBase (b);
            b.set ("voiceMode", 2).set ("glide", 0.08f).set ("subLevel", 0.0f);
            oscA (b, T::Talk, 0.3f, W::Off, 0.0f, 4, 0.12f, 0.8f);
            b.set ("aOct", 1);
            filter (b, F::Vowel, 2200.0f, 0.45f);
            lfo (b, 2, S::Sine, V::d16, true);
            b.mod (1, srcModWheel, dstMorph, 1.0f).mod (2, srcLfo2, dstAPitch, 0.005f).mod (3, srcVelocity, dstAPos, 0.5f);
            dist (b, D::Tube, 0.3f);
            b.on ("dlyOn").set ("dlyMix", 0.2f).set ("revMix", 0.2f);
            stdMacros (b, dstMorph, 0.6f);
        } },

        // ======================================================= v1.1 : courbes, double filtre/disto, HQ, MPE
        { "WOBBLE", "Custom Wub Stutter", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Basic, 0.6f, W::BendP, 0.25f, 4, 0.14f, 0.85f);
            oscB (b, T::Hollow, 0.4f, W::Off, 0.0f, 2, 0.1f, 0.35f);
            filter (b, F::LP24, 80.0f, 0.55f, 0.35f);
            lfo (b, 1, S::Wub, V::d2, true);
            b.curve (1, curvePreset (8));
            b.mod (1, srcLfo1, dstCutoff, 0.62f).mod (2, srcLfo1, dstAPos, 0.25f).mod (3, srcLfo1, dstAWarp, 0.3f);
            dist (b, D::Tube, 0.4f);
            stdMacros (b);
        } },

        { "RIDDIM", "Riddim Triplet Gate", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Pulse, 0.08f, W::Pwm, 0.1f, 1, 0.0f, 0.9f);
            filter (b, F::LP12, 350.0f, 0.4f, 0.3f);
            lfo (b, 1, S::Wub, V::d2, true);
            b.curve (1, curvePreset (3));
            b.mod (1, srcLfo1, dstCutoff, 0.5f).mod (2, srcLfo1, dstAPos, 0.3f).mod (3, srcLfo1, dstAWarp, 0.45f);
            dist (b, D::Hard, 0.5f, 9000.0f);
            b.on ("dist2On").set ("dist2Mode", (float) D::Tube).set ("dist2Drive", 0.25f).set ("dist2Mix", 0.6f);
            b.set ("ottDepth", 0.75f);
            stdMacros (b);
        } },

        { "TEAROUT", "Double Dist Grinder", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Chainsaw, 0.35f, W::Sync, 0.2f, 3, 0.12f, 0.85f);
            oscB (b, T::Metal, 0.3f, W::FmOsc, 0.2f, 1, 0.0f, 0.3f);
            filter (b, F::LP24, 700.0f, 0.4f, 0.4f);
            b.on ("f2On").set ("f2Type", (float) F::CombP).set ("f2Cutoff", 261.6f).set ("f2Key", 1.0f).set ("f2Reso", 0.6f).set ("f2Mix", 0.8f);
            b.set ("fltRouting", 1);
            lfo (b, 1, S::Wub, V::d16, true, 0.2f);
            b.seq ({ Q::s16, Q::s16, Q::s32, Q::s16, Q::s8, Q::s16T, Q::s16T, Q::s32 }, QL::Eighth);
            b.mod (1, srcLfo1, dstCutoff, 0.45f).mod (2, srcLfo1, dstReso2, 0.25f).mod (3, srcLfo1, dstAPos, 0.45f)
             .mod (4, srcLfo1, dstDist2Drive, 0.2f);
            dist (b, D::Tube, 0.4f, 14000.0f, 1.0f);
            b.on ("dist2On").set ("dist2Mode", (float) D::Foldback).set ("dist2Drive", 0.35f).set ("dist2Mix", 0.6f);
            b.fxOrder ({ fxDist1, fxOtt, fxDist2, fxCrush });
            b.set ("ottDepth", 0.7f);
            stdMacros (b);
        } },

        { "GROWL", "Formant Split", [] (PatchBuilder& b)
        {
            bassBase (b);
            oscA (b, T::Growl, 0.3f, W::FmSub, 0.15f, 3, 0.1f, 0.85f);
            filter (b, F::LP24, 300.0f, 0.4f, 0.3f);
            b.on ("f2On").set ("f2Type", (float) F::Vowel).set ("f2Cutoff", 1700.0f).set ("f2Reso", 0.5f);
            b.set ("fltRouting", 1);
            lfo (b, 1, S::Wub, V::d8, true);
            lfo (b, 2, S::Tri, V::d4, true);
            b.seq ({ Q::s8, Q::s8, Q::s16, Q::s8T, Q::s8, Q::s16, Q::s4, Q::s32 }, QL::Quarter);
            b.mod (1, srcLfo1, dstCutoff, 0.55f).mod (2, srcLfo2, dstMorph2, 1.0f).mod (3, srcLfo1, dstAPos, 0.45f);
            dist (b, D::Tube, 0.45f);
            b.set ("ottDepth", 0.6f);
            stdMacros (b, dstMorph2, 0.5f);
        } },

        { "SCREECH", "Hi-Def Screech", [] (PatchBuilder& b)
        {
            bassBase (b);
            b.set ("quality", 2);
            oscA (b, T::Screech, 0.7f, W::Sync, 0.55f, 5, 0.2f, 0.85f);
            b.set ("aOct", 1).set ("glide", 0.05f).set ("subLevel", 0.2f);
            filter (b, F::BP, 2200.0f, 0.35f, 0.3f);
            lfo (b, 1, S::Custom, V::d4, true);
            b.curve (1, curvePreset (5));
            b.mod (1, srcLfo1, dstAWarp, 0.4f).mod (2, srcLfo1, dstCutoff, 0.25f).mod (3, srcLfo1, dstAPos, 0.3f);
            dist (b, D::Hard, 0.35f, 15000.0f);
            b.set ("ottDepth", 0.65f);
            stdMacros (b);
        } },

        { "LEAD", "MPE Growl Lead", [] (PatchBuilder& b)
        {
            bassBase (b);
            b.set ("voiceMode", 0).set ("mpeOn", 1.0f).set ("subLevel", 0.3f);
            oscA (b, T::Growl, 0.2f, W::BendP, 0.2f, 3, 0.1f, 0.85f);
            filter (b, F::Vowel, 1500.0f, 0.45f, 0.3f);
            b.mod (1, srcAftertouch, dstAPos, 0.8f).mod (2, srcSlide, dstMorph, 1.0f).mod (3, srcAftertouch, dstDistDrive, 0.3f)
             .mod (4, srcVelocity, dstCutoff, 0.15f);
            dist (b, D::Tube, 0.35f);
            b.set ("ottDepth", 0.5f).set ("revMix", 0.15f);
            b.macroNames ("--", "--", "--", "--");
        } },
    };
    return presets;
}

// ---------------------------------------------------------------- Générateur de patchs
juce::StringArray generatorStyles() { return { "WOBBLE", "GROWL", "RIDDIM", "TEAROUT", "SCREECH", "REESE", "YOI", "CHAOS" }; }

namespace
{
    template <typename C> int pick (juce::Random& r, const C& c) { return c[(size_t) r.nextInt ((int) c.size())]; }
    float rr (juce::Random& r, float lo, float hi) { return lo + r.nextFloat() * (hi - lo); }
}

void generatePatch (PatchBuilder& b, int style, juce::Random& rng)
{
    if (style == 7) style = rng.nextInt (7);
    bassBase (b);

    std::vector<int> tablesA, tablesB, filters, dists, seqPool;
    int seqLen = QL::Quarter;
    switch (style)
    {
        case 0: // WOBBLE
            tablesA = { T::Basic, T::Hollow, T::Pulse, T::Reese };  tablesB = { T::Basic, T::Metal, T::Pulse };
            filters = { F::LP24, F::LP24, F::LP24, F::Vowel };       dists = { D::Tube, D::Overdrive, D::Foldback };
            seqPool = { Q::s8, Q::s8, Q::s16, Q::s16, Q::s8T, Q::s4, Q::s16T, Q::s32, Q::Open };
            break;
        case 1: // GROWL
            tablesA = { T::Growl, T::Talk, T::Metal, T::Fold };      tablesB = { T::Basic, T::Fold, T::Growl };
            filters = { F::LP24, F::Vowel, F::BP, F::CombP };        dists = { D::Foldback, D::Tube, D::SineFold, D::Hard };
            seqPool = { Q::s8, Q::s8, Q::s16, Q::s16T, Q::s8T, Q::s32 };
            break;
        case 2: // RIDDIM
            tablesA = { T::Pulse, T::Hollow, T::Digital, T::Basic }; tablesB = { T::Pulse, T::Digital };
            filters = { F::LP12, F::LP24, F::CombM };                dists = { D::Hard, D::Hard, D::Tube };
            seqPool = { Q::s8T, Q::s8T, Q::s16T, Q::s16T, Q::s4T, Q::Off, Q::s32T };
            seqLen = QL::Eighth;
            break;
        case 3: // TEAROUT
            tablesA = { T::Chainsaw, T::Metal, T::Screech, T::Fold }; tablesB = { T::Screech, T::Fold, T::Chainsaw };
            filters = { F::LP24, F::CombP, F::CombM, F::BP };        dists = { D::Foldback, D::Rectify, D::Diode, D::Hard };
            seqPool = { Q::s16, Q::s16, Q::s32, Q::s16T, Q::s8, Q::s32T, Q::s64 };
            seqLen = QL::Eighth;
            break;
        case 4: // SCREECH
            tablesA = { T::Screech, T::Basic, T::Chainsaw };         tablesB = { T::Screech, T::Metal };
            filters = { F::HP, F::BP, F::LP24 };                     dists = { D::Hard, D::Tube, D::Foldback };
            seqPool = { Q::s4, Q::s8, Q::s16, Q::s8T };
            break;
        case 5: // REESE
            tablesA = { T::Reese, T::Basic, T::Hollow };             tablesB = { T::Basic, T::Reese };
            filters = { F::LP24, F::CombP, F::Notch };               dists = { D::Overdrive, D::Tube };
            seqPool = { Q::s2, Q::s4, Q::s4, Q::s8 };
            seqLen = QL::Half;
            break;
        default: // YOI
            tablesA = { T::Yoi, T::Talk, T::Growl };                 tablesB = { T::Basic, T::Yoi };
            filters = { F::LP24, F::Vowel };                         dists = { D::Tube, D::Overdrive };
            seqPool = { Q::s8, Q::s4, Q::s8, Q::s16, Q::Open };
            break;
    }

    const int wA = rng.nextFloat() < 0.5f ? W::Off : pick (rng, std::vector<int> { W::BendP, W::BendM, W::Sync, W::Mirror, W::FmOsc, W::FmSub, W::Pwm });
    oscA (b, pick (rng, tablesA), rr (rng, 0.0f, 0.7f), wA, rr (rng, 0.1f, 0.45f), 1 + rng.nextInt (5), rr (rng, 0.05f, 0.25f), 0.85f);
    const bool useB = rng.nextFloat() < 0.7f || wA == W::FmOsc;
    if (useB)
    {
        const int wB = rng.nextFloat() < 0.6f ? W::Off : pick (rng, std::vector<int> { W::Sync, W::Ring, W::BendP, W::Mirror });
        oscB (b, pick (rng, tablesB), rr (rng, 0.0f, 0.8f), wB, rr (rng, 0.1f, 0.5f), 1 + rng.nextInt (3), rr (rng, 0.05f, 0.25f),
              wA == W::FmOsc ? rr (rng, 0.0f, 0.3f) : rr (rng, 0.2f, 0.5f));
        b.set ("bOct", (float) pick (rng, std::vector<int> { -1, 0, 0, 1 }));
        if (wA == W::FmOsc) b.set ("bSemi", (float) pick (rng, std::vector<int> { 0, 0, 7, 12, 5 }));
    }

    const int ft = pick (rng, filters);
    float cutoff = 100.0f, key = 0.0f;
    switch (ft)
    {
        case F::LP24: case F::LP12: cutoff = rr (rng, 60.0f, 500.0f); break;
        case F::Vowel:              cutoff = rr (rng, 1200.0f, 2500.0f); break;
        case F::CombP: case F::CombM: cutoff = 261.6f; key = 1.0f; break;
        case F::BP:                 cutoff = rr (rng, 500.0f, 2000.0f); break;
        case F::HP:                 cutoff = rr (rng, 60.0f, 250.0f); break;
        default:                    cutoff = rr (rng, 200.0f, 800.0f); break;
    }
    filter (b, ft, cutoff, rr (rng, 0.25f, 0.65f), rr (rng, 0.1f, 0.5f), 0.0f, key);
    if (ft == F::CombP || ft == F::CombM) b.set ("fltMix", rr (rng, 0.5f, 0.8f));

    // LFO 1 + séquence rythmique
    const int shape = pick (rng, std::vector<int> { S::Wub, S::Wub, S::Wub, S::Tri, S::SawU, S::SawD, S::Sine, S::Square });
    lfo (b, 1, shape, style == 2 ? V::d8T : V::d8, true, rr (rng, -0.3f, 0.5f), 0.05f);
    if (style != 6 || rng.nextBool())
    {
        const int steps = pick (rng, std::vector<int> { 4, 6, 8, 8, 8, 16 });
        b.on ("seqOn").set ("seqSteps", (float) steps).set ("seqLen", (float) seqLen);
        for (int i = 1; i <= kNumSeqSteps; ++i) b.set ("seq" + juce::String (i), (float) pick (rng, seqPool));
    }

    // destinations du LFO 1 selon le filtre
    if (ft == F::Vowel)                          b.nextMod (srcLfo1, dstMorph, rr (rng, 0.6f, 1.0f));
    else if (ft == F::CombP || ft == F::CombM)   b.nextMod (srcLfo1, dstReso, rr (rng, 0.15f, 0.3f));
    else if (ft == F::LP24 || ft == F::LP12)     b.nextMod (srcLfo1, dstCutoff, rr (rng, 0.45f, 0.65f));
    else                                         b.nextMod (srcLfo1, dstCutoff, rr (rng, 0.15f, 0.35f));
    b.nextMod (srcLfo1, dstAPos, rr (rng, 0.2f, 0.7f));
    if (wA != W::Off) b.nextMod (srcLfo1, dstAWarp, rr (rng, 0.2f, 0.5f));
    if (useB && rng.nextBool()) b.nextMod (srcLfo1, dstBPos, rr (rng, 0.2f, 0.6f));
    if (style == 6) { b.set ("e3A", rr (rng, 0.06f, 0.2f)).set ("e3S", 1.0f); b.nextMod (srcEnv3, dstAPos, 1.0f); }

    dist (b, pick (rng, dists), rr (rng, 0.25f, 0.55f), rr (rng, 8000.0f, 16000.0f), rr (rng, 0.6f, 1.0f));
    if (style == 2 || style == 3 || rng.nextFloat() < 0.15f)
        b.on ("crushOn").set ("crushBits", rr (rng, 7.0f, 12.0f)).set ("crushRate", rr (rng, 0.0f, 0.15f)).set ("crushMix", rr (rng, 0.3f, 0.6f));
    b.set ("ottDepth", rr (rng, 0.45f, 0.85f));
    if (rng.nextFloat() < 0.3f) b.fxOrder ({ fxOtt, fxDist1, fxCrush, fxDist2 });

    // ingrédients v1.1 : 2e disto, 2e filtre, courbe dessinée
    if (rng.nextFloat() < 0.4f)
        b.on ("dist2On").set ("dist2Mode", (float) pick (rng, dists)).set ("dist2Drive", rr (rng, 0.15f, 0.4f)).set ("dist2Mix", rr (rng, 0.3f, 0.7f));
    if (rng.nextFloat() < 0.35f)
    {
        const int t2 = pick (rng, std::vector<int> { F::Vowel, F::CombP, F::BP, F::Notch });
        b.on ("f2On").set ("f2Type", (float) t2).set ("f2Reso", rr (rng, 0.3f, 0.6f)).set ("fltRouting", (float) rng.nextInt (2));
        if (t2 == F::CombP) b.set ("f2Cutoff", 261.6f).set ("f2Key", 1.0f).set ("f2Mix", 0.6f);
        else b.set ("f2Cutoff", rr (rng, 700.0f, 2500.0f));
        b.nextMod (srcLfo2, t2 == F::Vowel ? dstMorph2 : dstCutoff2, rr (rng, 0.3f, 0.9f));
        lfo (b, 2, pick (rng, std::vector<int> { S::Tri, S::SawU, S::Wub, S::SH }), pick (rng, std::vector<int> { V::d4, V::d8, V::d16 }), true);
    }
    if (rng.nextFloat() < 0.3f && ! (rng.nextFloat() < 0.5f && style == 6))
    {
        b.curve (1, curvePreset (rng.nextInt (curvePresetNames().size())));
        b.set ("l1Div", (float) pick (rng, std::vector<int> { V::d2, V::d4, V::d1 }));
        b.on ("seqOn", false);
    }
    stdMacros (b);
}

void mutatePatch (juce::AudioProcessorValueTreeState& s, PatchExtras& ex, juce::Random& rng, float amount)
{
    static const juce::StringArray contIds { "aPos", "aWarp", "aDetune", "bPos", "bWarp", "bDetune", "bLevel",
                                             "fltCutoff", "fltReso", "fltDrive", "fltMorph",
                                             "l1Curve", "l2Curve", "distDrive", "distBias", "distTone", "ottDepth",
                                             "m1Amt", "m2Amt", "m3Amt", "m4Amt",
                                             "f2Cutoff", "f2Reso", "f2Morph", "dist2Drive", "dist2Bias" };
    static const juce::StringArray choiceIds { "aWarpMode", "bWarpMode", "distMode", "dist2Mode", "l1Shape", "aTable", "bTable" };

    auto nudge = [&] (const juce::String& id, float range)
    {
        if (auto* p = s.getParameter (id))
        {
            const float g = (float) (rng.nextDouble() * 2.0 - 1.0) * range;
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + g));
        }
    };

    for (auto& id : contIds) nudge (id, 0.25f * amount);

    for (auto& id : choiceIds)
        if (rng.nextFloat() < 0.25f * amount)
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) rng.nextInt (id.endsWith ("Table") ? 12 : p->choices.size())));

    // courbes dessinées : on bouge un peu les points intérieurs
    for (auto& c : ex.curves)
        for (size_t k = 1; k + 1 < c.size(); ++k)
        {
            c[k].y = juce::jlimit (0.0f, 1.0f, c[k].y + (float) (rng.nextDouble() * 2.0 - 1.0) * 0.2f * amount);
            c[k].c = juce::jlimit (-1.0f, 1.0f, c[k].c + (float) (rng.nextDouble() * 2.0 - 1.0) * 0.3f * amount);
        }

    // séquence : on change quelques pas
    if (auto* on = s.getRawParameterValue ("seqOn"); on != nullptr && on->load() > 0.5f)
        for (int i = 1; i <= kNumSeqSteps; ++i)
            if (rng.nextFloat() < 0.35f * amount)
                if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter ("seq" + juce::String (i))))
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) (2 + rng.nextInt (p->choices.size() - 2))));
}
} // namespace fragor
