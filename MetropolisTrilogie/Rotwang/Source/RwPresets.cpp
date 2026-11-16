#include "RwPresets.h"
#include "RwProcessor.h"

namespace rw
{
namespace
{
inline float idx (int i, int n) { return ((float) i + 0.5f) / (float) n; }
inline float semi (float s) { return 0.5f + s / 48.0f; }

struct PCtx
{
    RotwangProcessor& p;
    void slot (int s, int type, std::initializer_list<float> v, float mix, std::initializer_list<float> j = {}) const
    {
        p.setParam (sid (s, "type"), (float) type / (float) (kNumTypes - 1));
        p.setParam (sid (s, "on"), 1.0f);
        int k = 0;
        for (float x : v) { if (k < 6) p.setParam (pid (s, k), x); ++k; }
        p.setParam (pid (s, 6), mix);
        k = 0;
        for (float x : j) { if (k < 7) p.setParam (jid (s, k), x); ++k; }
    }
    void row (int r, int src, int dst, float amt) const
    {
        const juce::String n = "row" + juce::String (r + 1);
        p.setParamReal (n + "_src", (float) src);
        p.setParamReal (n + "_dst", (float) dst);
        p.setParamReal (n + "_amt", amt);
    }
    static int dst (int s, int prm) { return 1 + s * kNumSP + prm; }
    void glob (float chaos, int clock, float glide, float evolve = 0.0f, int evobars = 2, float width = 1.0f) const
    {
        p.setParamReal ("chaos", chaos); p.setParamReal ("clock", (float) clock); p.setParamReal ("glide", glide);
        p.setParamReal ("evolve", evolve); p.setParamReal ("evobars", (float) evobars); p.setParamReal ("width", width);
    }
};

// Sources : 1 S&H, 2 Smooth, 3 Turing, 4 Chaos, 5 LFO, 6 Enveloppe, 7 Horloge, 8.. Macros
void reset (RotwangProcessor& p)
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        p.setParam (sid (s, "type"), 0.0f);
        p.setParam (sid (s, "on"), 1.0f);
        for (int k = 0; k < kNumSP; ++k)
        {
            p.setParam (pid (s, k), 0.5f); p.setParam (jid (s, k), 0.0f);
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
    p.setParamReal ("mix", 1.0f); p.setParamReal ("chaos", 0.5f); p.setParamReal ("clock", 3.0f);
    p.setParamReal ("glide", 0.3f); p.setParamReal ("evolve", 0.0f); p.setParamReal ("evobars", 2.0f);
    p.setParamReal ("width", 1.0f); p.setParamReal ("duck", 0.0f); p.setParamReal ("freeze", 0.0f);
    p.setParamReal ("scene", 0.0f);
}

struct Preset { const char* name; void (*fn) (const PCtx&); };

const Preset presets[] = {
    { "Ville haute - Lumiere", [] (const PCtx& c) {
        c.slot (0, TChorus,  { 0.35f, 0.45f, idx (2, 4), 0.4f, 0.1f, 0.8f }, 0.5f, { 0.2f, 0.2f });
        c.slot (1, TDelay,   { idx (6, 12), 0.45f, 0.35f, 0.7f, 0.2f, 1.0f }, 0.45f, { 0.25f, 0.2f, 0.2f, 0.3f, 0.2f });
        c.slot (2, TShimmer, { 0.6f, 0.75f, 0.45f, idx (0, 8), 0.45f, 0.4f }, 0.7f, { 0.0f, 0.0f, 0.3f, 0.2f });
        c.glob (0.45f, 5, 0.6f, 0.25f, 2, 1.3f);
        c.row (0, 2, PCtx::dst (1, 2), 0.25f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (2, 6), 0.3f); } },
    { "Ville basse - Machines", [] (const PCtx& c) {
        c.slot (0, TGate,      { idx (2, 6), 0.65f, 0.25f, 0.8f, 0.15f, 0.4f }, 1.0f, { 0.0f, 0.2f });
        c.slot (1, TDegrade,   { 0.45f, 0.25f, 0.2f, 0.2f, 0.12f, 0.35f }, 0.8f, { 0.2f, 0.3f, 0.3f });
        c.slot (2, TResonator, { idx (2, 12), idx (3, 12), 0.6f, 0.4f, idx (1, 4), 0.4f }, 0.5f, { 0.0f, 0.0f, 0.2f, 0.3f });
        c.slot (3, TPlate,     { 0.35f, 0.4f, 0.6f, 0.05f, 0.2f, 0.5f }, 0.3f);
        c.glob (0.6f, 2, 0.05f, 0.3f, 1, 1.0f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (2, 6), 0.4f); } },
    { "Moloch", [] (const PCtx& c) {
        c.slot (0, TPitch,   { semi (-12), 0.5f, 0.6f, 0.25f, 0.0f, 0.4f }, 0.6f);
        c.slot (1, TDegrade, { 0.7f, 0.3f, 0.35f, 0.4f, 0.2f, 0.5f }, 0.7f, { 0.3f, 0.3f, 0.3f });
        c.slot (2, TRing,    { idx (0, 3), 0.25f, 0.4f, 0.3f, 0.2f, 0.4f }, 0.35f, { 0.0f, 0.2f });
        c.slot (3, TReverse, { idx (3, 6), 0.6f, 0.8f, 0.15f, 0.4f, 0.6f }, 0.6f);
        c.glob (0.5f, 3, 0.4f, 0.4f, 2, 1.2f);
        c.row (0, 6, PCtx::dst (1, 0), 0.4f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (3, 6), 0.4f); } },
    { "Maria - Robot", [] (const PCtx& c) {
        c.slot (0, TRing,     { idx (1, 3), 0.53f, 0.3f, 0.4f, 0.4f, 0.3f }, 0.6f, { 0.0f, 0.15f });
        c.slot (1, TGranular, { 0.55f, 0.55f, 0.35f, semi (0), 0.35f, 0.25f }, 0.6f, { 0.3f, 0.3f, 0.2f });
        c.slot (2, TFlanger,  { 0.35f, 0.7f, 0.8f, 0.4f, 0.5f, 0.4f }, 0.5f);
        c.slot (3, TPlate,    { 0.5f, 0.6f, 0.4f, 0.1f, 0.4f, 0.6f }, 0.4f);
        c.glob (0.55f, 4, 0.5f, 0.3f, 2, 1.4f);
        c.row (0, 2, PCtx::dst (0, 1), 0.15f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (1, 6), 0.4f); } },
    { "Catacombes", [] (const PCtx& c) {
        c.slot (0, TReverse,  { idx (4, 6), 0.7f, 0.85f, 0.1f, 0.5f, 0.6f }, 0.75f);
        c.slot (1, TGranular, { 0.75f, 0.25f, 0.7f, semi (-12), 0.25f, 0.5f }, 0.5f, { 0.2f, 0.3f, 0.3f });
        c.slot (2, TFilter,   { idx (0, 6), 0.45f, 0.35f, 0.6f, 0.4f, 0.15f }, 1.0f, { 0.0f, 0.2f });
        c.glob (0.4f, 6, 0.8f, 0.4f, 3, 1.5f);
        c.row (0, 2, PCtx::dst (2, 1), 0.2f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (1, 6), 0.4f); } },
    { "Tour de Babel", [] (const PCtx& c) {
        c.slot (0, TGranular, { 0.6f, 0.6f, 0.45f, semi (0), 0.55f, 0.3f }, 0.7f, { 0.2f, 0.3f, 0.3f, 0.0f, 0.3f });
        c.slot (1, TScatter,  { 0.6f, 0.45f, 0.35f, 0.6f, 0.9f, idx (6, 7) }, 0.45f);
        c.slot (2, TShimmer,  { 0.65f, 0.8f, 0.5f, idx (6, 8), 0.5f, 0.5f }, 0.6f);
        c.glob (0.6f, 4, 0.3f, 0.35f, 2, 1.5f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (2, 6), 0.4f); } },
    { "Inondation", [] (const PCtx& c) {
        c.slot (0, TGranular, { 0.7f, 0.8f, 0.6f, semi (0), 0.15f, 0.4f }, 0.8f, { 0.2f, 0.2f, 0.3f });
        c.slot (1, TPhaser,   { 0.2f, 0.8f, 0.6f, idx (4, 5), 0.5f, 0.6f }, 0.6f);
        c.slot (2, TPlate,    { 0.85f, 0.9f, 0.5f, 0.1f, 0.6f, 0.7f }, 0.6f);
        c.glob (0.5f, 7, 0.9f, 0.6f, 3, 1.6f);
        c.row (0, 1, PCtx::dst (1, 4), 0.2f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (2, 6), 0.4f); } },
    { "Stade des fils (rythme)", [] (const PCtx& c) {
        c.slot (0, TStutter, { idx (3, 9), 0.35f, idx (1, 5), 0.5f, 0.25f, 1.0f }, 1.0f, { 0.4f, 0.0f, 0.3f, 0.3f });
        c.slot (1, TFilter,  { idx (1, 6), 0.55f, 0.55f, 0.75f, 0.3f, 0.4f }, 0.8f, { 0.0f, 0.3f });
        c.slot (2, TDelay,   { idx (4, 12), 0.4f, 0.55f, 0.9f, 0.1f, 1.0f }, 0.35f, { 0.4f });
        c.slot (3, TPlate,   { 0.3f, 0.4f, 0.5f, 0.0f, 0.2f, 0.5f }, 0.2f);
        c.glob (0.6f, 1, 0.0f, 0.2f, 1, 1.2f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (0, 1), 0.6f); } },
    { "Kalimba cristal", [] (const PCtx& c) {
        c.slot (0, TResonator, { idx (9, 12), idx (7, 12), 0.75f, 0.7f, idx (2, 4), 0.3f }, 0.45f);
        c.slot (1, TChorus,    { 0.3f, 0.5f, idx (3, 4), 0.3f, 0.2f, 0.9f }, 0.4f);
        c.slot (2, TDelay,     { idx (7, 12), 0.5f, 0.4f, 0.8f, 0.15f, 1.0f }, 0.4f, { 0.3f });
        c.slot (3, TShimmer,   { 0.55f, 0.7f, 0.4f, idx (1, 8), 0.4f, 0.4f }, 0.5f);
        c.glob (0.4f, 5, 0.4f, 0.2f, 2, 1.4f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (3, 6), 0.4f); } },
    { "Souffle (vents)", [] (const PCtx& c) {
        c.slot (0, TFilter,   { idx (1, 6), 0.55f, 0.3f, 0.65f, 0.25f, 0.2f }, 0.4f, { 0.0f, 0.2f });
        c.slot (1, TGranular, { 0.65f, 0.45f, 0.3f, semi (12), 0.25f, 0.2f }, 0.45f, { 0.2f, 0.3f });
        c.slot (2, TDegrade,  { 0.15f, 0.1f, 0.05f, 0.35f, 0.08f, 0.25f }, 0.4f);
        c.slot (3, TPlate,    { 0.65f, 0.7f, 0.45f, 0.08f, 0.5f, 0.6f }, 0.55f);
        c.glob (0.4f, 5, 0.7f, 0.3f, 2, 1.3f);
        c.row (0, 6, PCtx::dst (1, 1), 0.3f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (3, 6), 0.4f); } },
    { "Horloge 10h", [] (const PCtx& c) {
        c.slot (0, TGate,    { idx (4, 6), 0.7f, 0.15f, 0.9f, 0.0f, 0.7f }, 1.0f);
        c.slot (1, TScatter, { 0.5f, 0.35f, 0.4f, 1.0f, 0.8f, idx (4, 7) }, 0.5f);
        c.slot (2, TPitch,   { semi (7), 0.5f, 0.5f, 0.15f, 0.35f, 0.3f }, 0.35f);
        c.slot (3, TPlate,   { 0.4f, 0.5f, 0.6f, 0.0f, 0.2f, 0.5f }, 0.25f);
        c.glob (0.5f, 5, 0.0f, 0.2f, 1, 1.2f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (1, 6), 0.4f); } },
    { "Cathedrale", [] (const PCtx& c) {
        c.slot (0, TPlate,   { 0.9f, 1.0f, 0.35f, 0.2f, 0.5f, 0.75f }, 0.8f);
        c.slot (1, TShimmer, { 0.8f, 1.0f, 0.55f, idx (0, 8), 0.4f, 0.6f }, 0.45f);
        c.glob (0.3f, 6, 0.8f, 0.2f, 3, 1.6f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (1, 2), 0.4f); } },
    { "Fausse Maria (chaos)", [] (const PCtx& c) {
        c.slot (0, TStutter,  { idx (4, 9), 0.5f, idx (2, 5), 0.6f, 0.4f, 0.6f }, 1.0f, { 0.5f, 0.3f, 0.4f, 0.5f, 0.3f });
        c.slot (1, TRing,     { idx (2, 3), 0.4f, 0.6f, 0.5f, 0.3f, 0.5f }, 0.4f, { 0.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f });
        c.slot (2, TGranular, { 0.4f, 0.6f, 0.5f, semi (0), 0.7f, 0.5f }, 0.6f, { 0.4f, 0.4f, 0.4f });
        c.slot (3, TDelay,    { idx (5, 12), 0.55f, 0.5f, 0.8f, 0.4f, 1.0f }, 0.4f, { 0.5f, 0.2f, 0.3f });
        c.slot (4, TReverse,  { idx (2, 6), 0.5f, 0.6f, 0.2f, 0.3f, 0.5f }, 0.35f);
        c.glob (0.85f, 1, 0.1f, 0.5f, 0, 1.5f);
        c.row (0, 3, PCtx::dst (1, 1), 0.3f); c.row (1, 4, PCtx::dst (3, 0), 0.2f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (2, 6), 0.4f); } },
    { "Echo bande", [] (const PCtx& c) {
        c.slot (0, TDelay,   { 0.55f, 0.6f, 0.3f, 0.4f, 0.55f, 0.0f }, 0.5f, { 0.15f, 0.1f, 0.2f });
        c.slot (1, TDegrade, { 0.25f, 0.15f, 0.1f, 0.45f, 0.1f, 0.35f }, 0.6f);
        c.slot (2, TPlate,   { 0.45f, 0.5f, 0.5f, 0.05f, 0.3f, 0.5f }, 0.3f);
        c.glob (0.35f, 0, 0.5f, 0.2f, 2, 1.1f);
        c.row (0, 2, PCtx::dst (0, 0), 0.06f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (0, 1), 0.4f); } },
    { "Gel / suspension", [] (const PCtx& c) {
        c.slot (0, TGranular, { 0.85f, 0.7f, 0.9f, semi (0), 0.3f, 0.5f }, 0.9f, { 0.2f, 0.2f, 0.2f });
        c.slot (1, TReverse,  { idx (3, 6), 0.8f, 0.9f, 0.0f, 0.6f, 0.5f }, 0.5f);
        c.slot (2, TPlate,    { 0.95f, 1.0f, 0.4f, 0.1f, 0.7f, 0.8f }, 0.6f);
        c.glob (0.3f, 7, 1.0f, 0.4f, 4, 1.6f);
        c.row (6, 8, kDestChaos, 0.8f); c.row (7, 9, PCtx::dst (2, 6), 0.4f); } },
    { "Init (plate seule)", [] (const PCtx& c) {
        c.slot (0, TPlate, { 0.5f, 0.6f, 0.5f, 0.05f, 0.3f, 0.5f }, 0.5f);
        c.glob (0.5f, 3, 0.3f); } },
};
constexpr int kNumPresets = (int) (sizeof (presets) / sizeof (presets[0]));
} // namespace

int numFactoryPresets() { return kNumPresets; }
juce::String factoryPresetName (int i) { return presets[juce::jlimit (0, kNumPresets - 1, i)].name; }

void applyFactoryPreset (RotwangProcessor& p, int i)
{
    reset (p);
    PCtx c { p };
    presets[juce::jlimit (0, kNumPresets - 1, i)].fn (c);
}
} // namespace rw
