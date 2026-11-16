#include "GrPresets.h"
#include "GrProcessor.h"

namespace gr
{
namespace
{
inline float idx (int i, int n) { return ((float) i + 0.5f) / (float) n; }
inline float semi (float s) { return 0.5f + s / 48.0f; }

// Sources : 1 S&H, 2 Smooth, 3 Turing, 4 Chaos, 5 LFO, 6 Enveloppe, 7 Horloge, 8 Coup, 9 Velocite, 10.. Macros
struct PCtx
{
    GrotProcessor& p;
    void slot (int s, int type, std::initializer_list<float> v, float mix, float chance = 1.0f, std::initializer_list<float> j = {}) const
    {
        p.setParam (sid (s, "type"), (float) type / (float) (kNumTypes - 1));
        p.setParam (sid (s, "on"), 1.0f);
        int k = 0;
        for (float x : v) { if (k < 6) p.setParam (pid (s, k), x); ++k; }
        p.setParam (pid (s, kMix), mix);
        p.setParam (pid (s, kChance), chance);
        k = 0;
        for (float x : j) { if (k < kNumSP) p.setParam (jid (s, k), x); ++k; }
    }
    void row (int r, int src, int dst, float amt) const
    {
        const juce::String n = "row" + juce::String (r + 1);
        p.setParamReal (n + "_src", (float) src);
        p.setParamReal (n + "_dst", (float) dst);
        p.setParamReal (n + "_amt", amt);
    }
    static int dst (int s, int prm) { return 1 + s * kNumSP + prm; }
    void glob (int source, float chaos, int clock, float glide = 0.0f, float evolve = 0.0f, float width = 1.0f) const
    {
        p.setParamReal ("source", (float) source);
        p.setParamReal ("chaos", chaos); p.setParamReal ("clock", (float) clock); p.setParamReal ("glide", glide);
        p.setParamReal ("evolve", evolve); p.setParamReal ("width", width);
    }
    void macros (int s1, int p1, int s2, int p2) const
    {
        row (6, 10, kDestChaos, 0.8f);
        row (7, 11, dst (s1, p1), 0.5f);
        if (s2 >= 0) row (5, 12, dst (s2, p2), 0.5f);
    }
};

void reset (GrotProcessor& p, bool keepSource)
{
    const float src = p.apvts.getParameter ("source")->getValue();
    for (int s = 0; s < kNumSlots; ++s)
    {
        p.setParam (sid (s, "type"), 0.0f);
        p.setParam (sid (s, "on"), 1.0f);
        for (int k = 0; k < kNumSP; ++k)
        {
            p.setParam (pid (s, k), k == kChance ? 1.0f : 0.5f); p.setParam (jid (s, k), 0.0f);
            p.lockP[s][k].store (false);
        }
        p.slotLock[s].store (false);
    }
    for (int r = 0; r < kNumRows; ++r)
    {
        const juce::String n = "row" + juce::String (r + 1);
        p.setParam (n + "_src", 0.0f); p.setParam (n + "_dst", 0.0f); p.setParamReal (n + "_amt", 0.0f);
    }
    for (int m = 0; m < kNumMacros; ++m) p.setParam ("macro" + juce::String (m + 1), 0.0f);
    p.setParamReal ("mix", 1.0f); p.setParamReal ("chaos", 0.4f); p.setParamReal ("clock", (float) kClockHit);
    p.setParamReal ("glide", 0.0f); p.setParamReal ("evolve", 0.0f); p.setParamReal ("evobars", 2.0f);
    p.setParamReal ("width", 1.0f); p.setParamReal ("duck", 0.0f); p.setParamReal ("freeze", 0.0f);
    p.setParamReal ("scene", 0.0f);
    if (keepSource) p.setParam ("source", src);
}

struct Preset { const char* name; void (*fn) (const PCtx&); };

const Preset presets[] = {
    { "Fin - Polish kit", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.62f, 0.45f, 0.5f, idx (0, 3), 0.0f, 0.08f }, 1.0f);
        c.slot (1, TSmash,     { 0.35f, 0.45f, 0.4f, 0.3f, 0.1f, 0.4f }, 0.35f);
        c.slot (2, TReverb,    { idx (0, 5), 0.3f, 0.5f, 0.5f, 0.05f, 0.3f }, 0.18f);
        c.glob (4, 0.2f, 0); c.macros (1, kMix, 2, kMix); } },
    { "Kick - Poids lourd", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.72f, 0.4f, 0.6f, idx (1, 3), 0.0f, 0.15f }, 1.0f);
        c.slot (1, TSub,       { 0.5f, 0.45f, 0.35f, 0.35f, 0.2f, 0.0f }, 0.6f);
        c.slot (2, TSmash,     { 0.4f, 0.5f, 0.35f, 0.45f, 0.3f, 0.6f }, 0.5f);
        c.slot (3, TDist,      { idx (0, 5), 0.25f, 0.45f, 0.1f, 0.3f, 0.0f }, 0.4f);
        c.glob (0, 0.2f, 0); c.macros (1, kMix, 3, 1); } },
    { "Kick - Moloch techno", [] (const PCtx& c) {
        c.slot (0, TSub,    { 0.5f, 0.65f, 0.5f, 0.3f, 0.5f, 0.15f }, 0.7f);
        c.slot (1, TDist,   { idx (2, 5), 0.55f, 0.4f, 0.3f, 0.5f, 0.25f }, 0.6f, 1.0f, { 0.0f, 0.2f });
        c.slot (2, TFilter, { idx (0, 6), 0.45f, 0.45f, 0.75f, 0.2f, 0.45f }, 0.7f);
        c.slot (3, TReverb, { idx (3, 5), 0.5f, 0.6f, 0.4f, 0.05f, 0.35f }, 0.35f, 0.5f);
        c.glob (0, 0.5f, kClockHit); c.macros (1, 1, 3, kChance); } },
    { "Caisse claire - Gated 80s", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.75f, 0.5f, 0.6f, idx (0, 3), 0.1f, 0.1f }, 1.0f);
        c.slot (1, TNoise,     { idx (0, 4), 0.55f, 0.35f, 0.4f, 0.7f, 0.2f }, 0.5f);
        c.slot (2, TSmash,     { 0.45f, 0.4f, 0.3f, 0.5f, 0.2f, 0.5f }, 0.4f);
        c.slot (3, TReverb,    { idx (3, 5), 0.6f, 0.8f, 0.4f, 0.05f, 0.45f }, 0.55f);
        c.glob (1, 0.3f, kClockHit); c.macros (3, kMix, 1, kMix); } },
    { "Caisse claire - Dub throws", [] (const PCtx& c) {
        c.slot (0, TFilter, { idx (1, 6), 0.55f, 0.45f, 0.6f, 0.5f, 0.35f }, 0.5f, 0.5f);
        c.slot (1, TDelay,  { idx (6, 12), 0.55f, 0.35f, 0.8f, 0.2f, 1.0f }, 0.6f, 0.3f, { 0.0f, 0.15f });
        c.slot (2, TReverb, { idx (1, 5), 0.55f, 0.6f, 0.5f, 0.1f, 0.4f }, 0.4f, 0.4f);
        c.glob (1, 0.5f, kClockHit); c.macros (1, kChance, 2, kChance); } },
    { "Toms - Corps de metal", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.65f, 0.5f, 0.5f, idx (0, 3), 0.1f, 0.0f }, 1.0f);
        c.slot (1, TBody,      { idx (2, 5), 0.5f, 0.55f, 0.6f, 0.3f, 0.3f }, 0.4f);
        c.slot (2, TRing,      { idx (1, 3), 0.53f, 0.3f, 0.4f, 0.2f, 0.2f }, 0.3f, 0.6f);
        c.slot (3, TReverb,    { idx (2, 5), 0.45f, 0.6f, 0.5f, 0.05f, 0.3f }, 0.35f);
        c.glob (2, 0.4f, kClockHit); c.macros (1, kMix, 2, kMix); } },
    { "Overheads - Cathedrale", [] (const PCtx& c) {
        c.slot (0, TSmash,   { 0.5f, 0.3f, 0.6f, 0.6f, 0.2f, 0.5f }, 0.4f);
        c.slot (1, TPitch,   { semi (12), 0.5f, 0.5f, 0.2f, 0.0f, 0.3f }, 0.3f, 0.3f);
        c.slot (2, TReverb,  { idx (2, 5), 0.8f, 0.9f, 0.4f, 0.1f, 0.5f }, 0.5f);
        c.slot (3, TReverse, { idx (3, 6), 0.6f, 0.7f, 0.1f, 0.4f, 0.5f }, 0.3f, 0.5f);
        c.glob (3, 0.35f, kClockHit, 0.2f, 0.2f, 1.4f); c.macros (2, kMix, 3, kMix); } },
    { "Drumkit - Ecrase (NY)", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.65f, 0.45f, 0.6f, idx (0, 3), 0.0f, 0.1f }, 1.0f);
        c.slot (1, TSmash,     { 0.75f, 0.2f, 0.3f, 0.85f, 0.5f, 0.5f }, 0.5f);
        c.slot (2, TDist,      { idx (0, 5), 0.35f, 0.5f, 0.1f, 0.5f, 0.1f }, 0.35f);
        c.glob (4, 0.3f, 0); c.macros (1, kMix, 2, 1); } },
    { "Drumkit - Cassette lo-fi", [] (const PCtx& c) {
        c.slot (0, TDegrade, { 0.3f, 0.3f, 0.25f, 0.45f, 0.25f, 0.35f }, 0.8f);
        c.slot (1, TFilter,  { idx (0, 6), 0.6f, 0.3f, 0.6f, 0.1f, 0.4f }, 0.7f);
        c.slot (2, TMod,     { idx (0, 3), 0.3f, 0.4f, 0.1f, 0.4f, 0.8f }, 0.3f);
        c.slot (3, TSmash,   { 0.5f, 0.4f, 0.4f, 0.5f, 0.2f, 0.4f }, 0.4f);
        c.glob (4, 0.3f, 3, 0.6f, 0.2f); c.macros (0, 3, 1, 1); } },
    { "Drumkit - Glitch machine", [] (const PCtx& c) {
        c.slot (0, TStutter, { idx (3, 9), 0.35f, idx (1, 5), 0.5f, 0.3f, 1.0f }, 1.0f, 1.0f, { 0.4f, 0.0f, 0.3f, 0.3f });
        c.slot (1, TPitch,   { 0.5f, 0.5f, 0.4f, 0.1f, 0.5f, 0.2f }, 0.5f, 0.4f);
        c.slot (2, TGate,    { idx (2, 6), 0.7f, 0.3f, 0.8f, 0.1f, 0.5f }, 0.6f);
        c.slot (3, TScatter, { 0.5f, 0.4f, 0.3f, 0.9f, 0.8f, idx (2, 7) }, 0.4f);
        c.glob (4, 0.6f, kClockHit); c.macros (0, 1, 1, kChance); } },
    { "Charley - Granules", [] (const PCtx& c) {
        c.slot (0, TGranular, { 0.35f, 0.6f, 0.3f, semi (12), 0.4f, 0.6f }, 0.6f, 1.0f, { 0.2f, 0.2f, 0.2f });
        c.slot (1, TRing,     { idx (0, 3), 0.7f, 0.3f, 0.4f, 0.1f, 0.2f }, 0.3f, 0.5f);
        c.slot (2, TDelay,    { idx (2, 12), 0.35f, 0.6f, 0.9f, 0.1f, 1.0f }, 0.3f, 0.4f);
        c.glob (6, 0.45f, kClockHit); c.macros (0, kMix, 1, kChance); } },
    { "Cymbales - Infini", [] (const PCtx& c) {
        c.slot (0, TReverse,  { idx (3, 6), 0.7f, 0.8f, 0.1f, 0.4f, 0.4f }, 0.6f);
        c.slot (1, TGranular, { 0.7f, 0.4f, 0.8f, semi (0), 0.3f, 0.0f }, 0.4f);
        c.slot (2, TReverb,   { idx (2, 5), 0.9f, 1.0f, 0.3f, 0.1f, 0.6f }, 0.55f);
        c.glob (5, 0.35f, 6, 0.8f, 0.3f, 1.5f); c.macros (0, kMix, 2, kMix); } },
    { "Djembe - Terre", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.65f, 0.55f, 0.5f, idx (0, 3), 0.15f, 0.0f }, 1.0f);
        c.slot (1, TSub,       { 0.5f, 0.3f, 0.3f, 0.3f, 0.0f, 0.3f }, 0.3f, 0.6f);
        c.slot (2, TBody,      { idx (0, 5), 0.5f, 0.4f, 0.4f, 0.2f, 0.2f }, 0.3f);
        c.slot (3, TReverb,    { idx (0, 5), 0.35f, 0.6f, 0.5f, 0.02f, 0.3f }, 0.25f);
        c.glob (10, 0.4f, kClockHit); c.macros (2, kMix, 3, kMix); } },
    { "Conga / Bongo - Machines", [] (const PCtx& c) {
        c.slot (0, TFilter,    { idx (1, 6), 0.55f, 0.5f, 0.6f, 0.6f, 0.3f }, 0.5f);
        c.slot (1, TResonator, { idx (12, 13), idx (7, 12), 0.55f, 0.5f, idx (1, 4), 0.4f }, 0.35f);
        c.slot (2, TDelay,     { idx (2, 12), 0.35f, 0.5f, 0.9f, 0.1f, 1.0f }, 0.35f, 0.4f);
        c.glob (8, 0.5f, kClockHit); c.macros (1, kMix, 2, kChance); } },
    { "Tablas - Resonances", [] (const PCtx& c) {
        c.slot (0, TBody,      { idx (3, 5), 0.5f, 0.65f, 0.6f, 0.3f, 0.3f }, 0.35f);
        c.slot (1, TResonator, { idx (12, 13), idx (1, 12), 0.6f, 0.5f, idx (1, 4), 0.3f }, 0.3f);
        c.slot (2, TReverse,   { idx (2, 6), 0.5f, 0.6f, 0.1f, 0.3f, 0.5f }, 0.3f, 0.3f);
        c.glob (12, 0.4f, kClockHit); c.macros (0, kMix, 1, kMix); } },
    { "Derbouka - Desert electrique", [] (const PCtx& c) {
        c.slot (0, TDist,   { idx (3, 5), 0.4f, 0.55f, 0.2f, 0.7f, 0.3f }, 0.5f);
        c.slot (1, TDelay,  { idx (5, 12), 0.45f, 0.45f, 0.9f, 0.25f, 1.0f }, 0.4f, 0.5f);
        c.slot (2, TReverb, { idx (1, 5), 0.5f, 0.6f, 0.5f, 0.05f, 0.4f }, 0.3f);
        c.glob (11, 0.45f, kClockHit); c.macros (0, 1, 1, kChance); } },
    { "Triangle - Cristal", [] (const PCtx& c) {
        c.slot (0, TPitch,    { semi (-12), 0.5f, 0.6f, 0.3f, 0.3f, 0.3f }, 0.4f);
        c.slot (1, TGranular, { 0.6f, 0.4f, 0.5f, semi (0), 0.4f, 0.3f }, 0.4f);
        c.slot (2, TReverb,   { idx (2, 5), 0.75f, 0.85f, 0.3f, 0.05f, 0.5f }, 0.5f);
        c.glob (14, 0.4f, kClockHit, 0.3f); c.macros (0, kMix, 1, kMix); } },
    { "Tambourin - Poussiere", [] (const PCtx& c) {
        c.slot (0, TDegrade, { 0.2f, 0.25f, 0.2f, 0.2f, 0.3f, 0.3f }, 0.6f);
        c.slot (1, TScatter, { 0.4f, 0.5f, 0.3f, 0.8f, 0.9f, idx (1, 7) }, 0.4f);
        c.slot (2, TGate,    { idx (2, 6), 0.6f, 0.4f, 0.7f, 0.2f, 0.6f }, 0.5f);
        c.glob (13, 0.5f, kClockHit); c.macros (1, kMix, 2, kMix); } },
    { "Cajon - Coeur-machine", [] (const PCtx& c) {
        c.slot (0, TSub,  { 0.5f, 0.35f, 0.3f, 0.3f, 0.2f, 0.2f }, 0.5f, 0.7f);
        c.slot (1, TDist, { idx (1, 5), 0.35f, 0.5f, 0.3f, 0.4f, 0.2f }, 0.4f);
        c.slot (2, TGate, { idx (2, 6), 0.75f, 0.2f, 0.9f, 0.15f, 0.3f }, 0.5f);
        c.slot (3, TReverb, { idx (0, 5), 0.35f, 0.5f, 0.5f, 0.02f, 0.3f }, 0.2f);
        c.glob (15, 0.4f, kClockHit); c.macros (0, kMix, 2, kMix); } },
    { "Gong / bol - Abime", [] (const PCtx& c) {
        c.slot (0, TPitch,   { semi (-12), 0.5f, 0.8f, 0.3f, 0.0f, 0.4f }, 0.4f);
        c.slot (1, TBody,    { idx (3, 5), 0.5f, 0.85f, 0.5f, 0.4f, 0.2f }, 0.3f);
        c.slot (2, TReverse, { idx (4, 6), 0.8f, 0.9f, 0.1f, 0.5f, 0.6f }, 0.5f);
        c.slot (3, TReverb,  { idx (2, 5), 0.95f, 1.0f, 0.4f, 0.1f, 0.6f }, 0.6f);
        c.glob (17, 0.3f, 7, 0.9f, 0.4f, 1.6f); c.macros (0, kMix, 2, kMix); } },
    { "Shaker - Pluie", [] (const PCtx& c) {
        c.slot (0, TGranular, { 0.25f, 0.8f, 0.5f, semi (0), 0.5f, 0.8f }, 0.6f);
        c.slot (1, TMod,      { idx (2, 3), 0.3f, 0.6f, 0.4f, 0.6f, 0.7f }, 0.4f);
        c.slot (2, TReverb,   { idx (1, 5), 0.5f, 0.6f, 0.4f, 0.05f, 0.4f }, 0.3f);
        c.glob (16, 0.5f, kClockHit); c.macros (0, 1, 1, kMix); } },
    { "Room - Ecrasement", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.35f, 0.75f, 0.4f, idx (0, 3), 0.0f, 0.0f }, 1.0f);
        c.slot (1, TSmash,     { 0.85f, 0.15f, 0.5f, 1.0f, 0.6f, 0.3f }, 0.7f);
        c.slot (2, TDist,      { idx (2, 5), 0.3f, 0.45f, 0.2f, 0.5f, 0.0f }, 0.3f);
        c.glob (18, 0.3f, 0); c.macros (1, kMix, 2, kMix); } },
    { "EXTREME - Destruction", [] (const PCtx& c) {
        c.slot (0, TDist,    { idx (2, 5), 0.85f, 0.5f, 0.5f, 0.5f, 0.5f }, 0.9f);
        c.slot (1, TDegrade, { 0.6f, 0.6f, 0.5f, 0.4f, 0.3f, 0.4f }, 0.7f, 1.0f, { 0.0f, 0.4f, 0.4f });
        c.slot (2, TRing,    { idx (0, 3), 0.4f, 0.7f, 0.4f, 0.3f, 0.4f }, 0.5f, 0.5f);
        c.slot (3, TStutter, { idx (4, 9), 0.4f, idx (1, 5), 0.6f, 0.4f, 1.0f }, 1.0f);
        c.glob (4, 0.7f, kClockHit); c.macros (0, 1, 2, kChance); } },
    { "EXTREME - Metal hurlant", [] (const PCtx& c) {
        c.slot (0, TBody,   { idx (2, 5), 0.6f, 0.8f, 0.8f, 0.6f, 0.6f }, 0.6f);
        c.slot (1, TRing,   { idx (1, 3), 0.6f, 0.5f, 0.6f, 0.6f, 0.3f }, 0.5f);
        c.slot (2, TDist,   { idx (3, 5), 0.6f, 0.6f, 0.4f, 0.6f, 0.4f }, 0.6f);
        c.slot (3, TReverb, { idx (4, 5), 0.6f, 0.7f, 0.3f, 0.02f, 0.5f }, 0.5f);
        c.glob (2, 0.6f, kClockHit); c.macros (0, kMix, 1, kMix); } },
    { "Init (vide)", [] (const PCtx& c) {
        c.slot (0, TTransient, { 0.5f, 0.5f, 0.5f, idx (0, 3), 0.0f, 0.0f }, 1.0f);
        c.glob (4, 0.4f, kClockHit); } },
};
constexpr int kNumPresets = (int) (sizeof (presets) / sizeof (presets[0]));
} // namespace

int numFactoryPresets() { return kNumPresets; }
juce::String factoryPresetName (int i) { return presets[juce::jlimit (0, kNumPresets - 1, i)].name; }

void applyFactoryPreset (GrotProcessor& p, int i)
{
    reset (p, false);
    PCtx c { p };
    presets[juce::jlimit (0, kNumPresets - 1, i)].fn (c);
}

void applySourceStarter (GrotProcessor& p, int source)
{
    reset (p, true);
    PCtx c { p };
    p.setParamReal ("source", (float) source);
    switch (sourceProfile (source).family)
    {
        case 0:   // grosse caisse, cajon
            c.slot (0, TTransient, { 0.66f, 0.45f, 0.55f, idx (1, 3), 0.1f, 0.1f }, 1.0f);
            c.slot (1, TSub,       { 0.5f, 0.4f, 0.3f, 0.35f, 0.15f, 0.0f }, 0.4f);
            c.slot (2, TSmash,     { 0.4f, 0.5f, 0.35f, 0.4f, 0.2f, 0.5f }, 0.35f);
            c.slot (3, TReverb,    { idx (0, 5), 0.3f, 0.45f, 0.5f, 0.02f, 0.3f }, 0.15f, 0.6f);
            c.macros (1, kMix, 2, kMix); break;
        case 1:   // caisse claire
            c.slot (0, TTransient, { 0.7f, 0.5f, 0.55f, idx (0, 3), 0.1f, 0.05f }, 1.0f);
            c.slot (1, TNoise,     { idx (0, 4), 0.55f, 0.35f, 0.4f, 0.6f, 0.15f }, 0.3f);
            c.slot (2, TSmash,     { 0.4f, 0.45f, 0.3f, 0.45f, 0.2f, 0.5f }, 0.3f);
            c.slot (3, TReverb,    { idx (1, 5), 0.45f, 0.55f, 0.5f, 0.05f, 0.4f }, 0.25f, 0.7f);
            c.slot (4, TDelay,     { idx (6, 12), 0.45f, 0.4f, 0.8f, 0.2f, 1.0f }, 0.3f, 0.2f);
            c.macros (1, kMix, 4, kChance); break;
        case 2:   // toms
            c.slot (0, TTransient, { 0.65f, 0.5f, 0.5f, idx (0, 3), 0.1f, 0.0f }, 1.0f);
            c.slot (1, TBody,      { idx (0, 5), 0.5f, 0.45f, 0.45f, 0.2f, 0.15f }, 0.25f);
            c.slot (2, TSmash,     { 0.4f, 0.45f, 0.4f, 0.4f, 0.15f, 0.5f }, 0.3f);
            c.slot (3, TReverb,    { idx (0, 5), 0.4f, 0.55f, 0.5f, 0.03f, 0.3f }, 0.25f);
            c.macros (1, kMix, 3, kMix); break;
        case 3:   // metal : cymbales, charley, tambourin, triangle, shaker
            c.slot (0, TTransient, { 0.6f, 0.42f, 0.7f, idx (2, 3), 0.1f, 0.0f }, 1.0f);
            c.slot (1, TGranular,  { 0.35f, 0.5f, 0.3f, semi (0), 0.35f, 0.6f }, 0.25f, 0.6f);
            c.slot (2, TDelay,     { idx (2, 12), 0.35f, 0.6f, 0.9f, 0.1f, 1.0f }, 0.25f, 0.3f);
            c.slot (3, TReverb,    { idx (1, 5), 0.5f, 0.6f, 0.4f, 0.05f, 0.4f }, 0.25f);
            c.macros (1, kMix, 2, kChance); break;
        case 4:   // peaux jouees a la main
            c.slot (0, TTransient, { 0.62f, 0.55f, 0.55f, idx (0, 3), 0.15f, 0.0f }, 1.0f);
            c.slot (1, TBody,      { idx (0, 5), 0.5f, 0.4f, 0.45f, 0.2f, 0.2f }, 0.25f);
            c.slot (2, TResonator, { idx (12, 13), idx (7, 12), 0.5f, 0.5f, idx (1, 4), 0.3f }, 0.2f, 0.5f);
            c.slot (3, TReverb,    { idx (0, 5), 0.35f, 0.6f, 0.5f, 0.02f, 0.3f }, 0.25f);
            c.macros (1, kMix, 2, kMix); break;
        case 6:   // gong, bol
            c.slot (0, TBody,    { idx (3, 5), 0.5f, 0.75f, 0.5f, 0.3f, 0.2f }, 0.3f);
            c.slot (1, TReverse, { idx (3, 6), 0.7f, 0.8f, 0.1f, 0.4f, 0.5f }, 0.3f);
            c.slot (2, TReverb,  { idx (2, 5), 0.8f, 0.9f, 0.4f, 0.08f, 0.5f }, 0.5f);
            c.macros (0, kMix, 2, kMix); break;
        default:  // bus : overhead, drumkit, room
            c.slot (0, TTransient, { 0.62f, 0.45f, 0.55f, idx (0, 3), 0.05f, 0.05f }, 1.0f);
            c.slot (1, TSmash,     { 0.65f, 0.25f, 0.35f, 0.75f, 0.4f, 0.5f }, 0.35f);
            c.slot (2, TDist,      { idx (0, 5), 0.3f, 0.5f, 0.1f, 0.5f, 0.0f }, 0.2f);
            c.slot (3, TReverb,    { idx (0, 5), 0.35f, 0.5f, 0.5f, 0.03f, 0.3f }, 0.2f);
            c.slot (4, TStutter,   { idx (3, 9), 0.15f, idx (1, 5), 0.5f, 0.2f, 1.0f }, 1.0f);
            c.macros (1, kMix, 4, 1); break;
    }
    p.setParamReal ("chaos", 0.35f);
    p.setParamReal ("clock", (float) kClockHit);
    // un peu d'alea par coup sur les reglages expressifs
    for (int s = 0; s < kNumSlots; ++s)
        if (p.slotType (s) != TOff) { p.setParam (jid (s, 1), 0.15f); p.setParam (jid (s, 2), 0.1f); }
}
} // namespace gr
