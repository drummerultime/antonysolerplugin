#include "MaPresets.h"
#include "MaProcessor.h"

namespace ma
{
namespace
{
inline float idx (int i, int n) { return ((float) i + 0.5f) / (float) n; }
inline float semi (float s) { return 0.5f + s / 48.0f; }

// Sources : 1 S&H, 2 Smooth, 3 Turing, 4 Chaos, 5 LFO, 6 Enveloppe, 7 Horloge, 8 Segment, 9 Joueur, 10.. Macros
struct PCtx
{
    MariaProcessor& p;
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
    // Activite, Reactivite, Phrase, Silences, Continuite, Contexte, Memoire, Suivi, Transpo, Reverse, Politesse, Niveau
    void im (int mode, std::initializer_list<float> v, std::initializer_list<float> j = {}) const
    {
        p.setParamReal ("im_mode", (float) mode);
        int k = 0;
        for (float x : v) { if (k < kNumIm) p.setParam (imId (k), x); ++k; }
        k = 0;
        for (float x : j) { if (k < kNumIm) p.setParam (imJid (k), x); ++k; }
    }
    void row (int r, int src, int dst, float amt) const
    {
        const juce::String n = "row" + juce::String (r + 1);
        p.setParamReal (n + "_src", (float) src);
        p.setParamReal (n + "_dst", (float) dst);
        p.setParamReal (n + "_amt", amt);
    }
    static int dst (int s, int prm) { return 1 + s * kNumSP + prm; }
    void glob (int source, float chaos, int clock, float glide = 0.0f, float evolve = 0.0f, float width = 1.2f) const
    {
        p.setParamReal ("source", (float) source);
        p.setParamReal ("chaos", chaos); p.setParamReal ("clock", (float) clock); p.setParamReal ("glide", glide);
        p.setParamReal ("evolve", evolve); p.setParamReal ("width", width);
    }
    void macros (int s1, int p1) const
    {
        row (6, 10, kDestIm + IAct, 0.6f);          // Macro 1 : activite de Maria
        row (7, 11, kDestIm + ICont, -0.6f);        // Macro 2 : plus de recombinaison
        if (s1 >= 0) row (5, 12, dst (s1, p1), 0.5f);   // Macro 3 : effet principal
        row (4, 13, kDestChaos, 0.8f);              // Macro 4 : chaos
    }
};

void reset (MariaProcessor& p, bool keepSource)
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
    for (int i = 0; i < kNumIm; ++i) { p.setParam (imId (i), imDefault (i)); p.setParam (imJid (i), 0.0f); p.lockIm[i].store (i == ILevel); }
    for (int r = 0; r < kNumRows; ++r)
    {
        const juce::String n = "row" + juce::String (r + 1);
        p.setParam (n + "_src", 0.0f); p.setParam (n + "_dst", 0.0f); p.setParamReal (n + "_amt", 0.0f);
    }
    for (int m = 0; m < kNumMacros; ++m) p.setParam ("macro" + juce::String (m + 1), 0.0f);
    p.setParamReal ("mix", 1.0f); p.setParamReal ("chaos", 0.4f); p.setParamReal ("clock", (float) kClockPhrase);
    p.setParamReal ("glide", 0.0f); p.setParamReal ("evolve", 0.0f); p.setParamReal ("evobars", 2.0f);
    p.setParamReal ("width", 1.2f); p.setParamReal ("duck", 0.0f); p.setParamReal ("freeze", 0.0f);
    p.setParamReal ("scene", 0.0f); p.setParamReal ("im_mode", 0.0f);
    if (keepSource) p.setParam ("source", src);
}

struct Preset { const char* name; void (*fn) (const PCtx&); };

const Preset presets[] = {
    { "Dialogue - Miroir", [] (const PCtx& c) {
        c.im (MDialogue, { 0.7f, 0.3f, 0.45f, 0.35f, 0.6f, 0.15f, 0.7f, 0.5f, 0.5f, 0.0f, 0.6f, 0.75f });
        c.slot (0, TReverb, { idx (0, 5), 0.3f, 0.5f, 0.5f, 0.05f, 0.3f }, 0.2f);
        c.glob (17, 0.3f, kClockPhrase); c.macros (0, kMix); } },
    { "Dialogue - Ombre portee (sax)", [] (const PCtx& c) {
        c.im (MDialogue, { 0.75f, 0.3f, 0.5f, 0.35f, 0.5f, 0.2f, 0.75f, 0.75f, 0.5f, 0.0f, 0.7f, 0.75f });
        c.slot (0, TDelay,  { idx (6, 12), 0.45f, 0.35f, 0.7f, 0.2f, 1.0f }, 0.3f, 0.4f);
        c.slot (1, TReverb, { idx (1, 5), 0.55f, 0.6f, 0.5f, 0.05f, 0.4f }, 0.35f);
        c.glob (3, 0.35f, kClockPhrase); c.macros (1, kMix); } },
    { "Duo - Fantome granulaire", [] (const PCtx& c) {
        c.im (MDuo, { 0.65f, 0.3f, 0.55f, 0.45f, 0.5f, 0.15f, 0.8f, 0.5f, 0.5f, 0.1f, 0.0f, 0.7f });
        c.slot (0, TGranular, { 0.6f, 0.55f, 0.45f, semi (0), 0.35f, 0.5f }, 0.6f, 1.0f, { 0.2f, 0.2f, 0.2f });
        c.slot (1, TShimmer,  { 0.6f, 0.75f, 0.45f, idx (0, 8), 0.45f, 0.4f }, 0.45f);
        c.glob (16, 0.4f, kClockSeg, 0.4f, 0.2f, 1.5f); c.macros (0, kMix); } },
    { "Echo - Machine a souvenirs", [] (const PCtx& c) {
        c.im (MEcho, { 0.85f, 0.25f, 0.45f, 0.3f, 0.4f, 0.15f, 0.4f, 0.5f, 0.5f, 0.15f, 0.8f, 0.75f });
        c.slot (0, TDegrade, { 0.2f, 0.25f, 0.2f, 0.45f, 0.15f, 0.35f }, 0.5f);
        c.slot (1, TDelay,   { idx (5, 12), 0.4f, 0.4f, 0.9f, 0.25f, 1.0f }, 0.35f);
        c.slot (2, TReverse, { idx (2, 6), 0.5f, 0.6f, 0.1f, 0.3f, 0.5f }, 0.3f, 0.4f);
        c.glob (17, 0.4f, kClockPhrase); c.macros (0, kMix); } },
    { "Contrepoint - Grave", [] (const PCtx& c) {
        c.im (MCounter, { 0.7f, 0.35f, 0.5f, 0.4f, 0.55f, 0.15f, 0.8f, 0.25f, 0.5f, 0.0f, 0.0f, 0.7f });
        c.slot (0, TPitch,  { semi (-12), 0.5f, 0.6f, 0.15f, 0.0f, 0.3f }, 1.0f);
        c.slot (1, TReverb, { idx (2, 5), 0.6f, 0.7f, 0.5f, 0.05f, 0.4f }, 0.35f);
        c.glob (17, 0.3f, kClockPhrase); c.macros (1, kMix); } },
    { "Batterie - Double rythmique", [] (const PCtx& c) {
        c.im (MDuo, { 0.85f, 0.2f, 0.4f, 0.25f, 0.45f, 0.3f, 0.6f, 0.5f, 0.5f, 0.0f, 0.0f, 0.75f }, { 0, 0, 0, 0, 0.2f, 0, 0, 0, 0.1f, 0.25f });
        c.slot (0, TStutter, { idx (3, 9), 0.25f, idx (1, 5), 0.5f, 0.2f, 1.0f }, 1.0f);
        c.slot (1, TDist,    { idx (0, 5), 0.3f, 0.5f, 0.1f, 0.5f, 0.2f }, 0.35f, 0.5f);
        c.slot (2, TReverb,  { idx (0, 5), 0.3f, 0.5f, 0.5f, 0.02f, 0.3f }, 0.2f);
        c.glob (0, 0.5f, kClockSeg); c.macros (1, 1); } },
    { "Percussions - Rituel", [] (const PCtx& c) {
        c.im (MDialogue, { 0.8f, 0.25f, 0.45f, 0.3f, 0.45f, 0.25f, 0.7f, 0.5f, 0.5f, 0.1f, 0.6f, 0.75f }, { 0, 0, 0, 0, 0, 0, 0, 0, 0.15f });
        c.slot (0, TBody,   { idx (0, 5), 0.5f, 0.5f, 0.45f, 0.2f, 0.2f }, 0.3f);
        c.slot (1, TReverb, { idx (2, 5), 0.5f, 0.7f, 0.5f, 0.03f, 0.3f }, 0.3f);
        c.glob (1, 0.45f, kClockSeg); c.macros (0, kMix); } },
    { "Sax - Fausse Maria", [] (const PCtx& c) {
        c.im (MDialogue, { 0.75f, 0.3f, 0.5f, 0.35f, 0.45f, 0.2f, 0.75f, 0.6f, 0.5f, 0.05f, 0.6f, 0.75f });
        c.slot (0, TRing,    { idx (1, 3), 0.53f, 0.25f, 0.4f, 0.2f, 0.2f }, 0.4f, 0.6f);
        c.slot (1, TMod,     { idx (1, 3), 0.3f, 0.6f, 0.7f, 0.4f, 0.6f }, 0.4f);
        c.slot (2, TReverb,  { idx (1, 5), 0.5f, 0.6f, 0.5f, 0.05f, 0.4f }, 0.3f);
        c.glob (3, 0.45f, kClockPhrase); c.macros (0, kMix); } },
    { "Voix - Choeur fantome", [] (const PCtx& c) {
        c.im (MDuo, { 0.6f, 0.35f, 0.6f, 0.5f, 0.6f, 0.15f, 0.8f, 0.6f, 0.5f, 0.0f, 0.0f, 0.65f }, { 0, 0, 0, 0, 0, 0, 0, 0, 0.1f });
        c.slot (0, TPitch,   { 0.5f, 0.5f, 0.7f, 0.1f, 0.4f, 0.4f }, 0.5f);
        c.slot (1, TMod,     { idx (0, 3), 0.25f, 0.6f, 0.2f, 0.5f, 0.9f }, 0.5f);
        c.slot (2, TShimmer, { 0.65f, 0.8f, 0.45f, idx (6, 8), 0.45f, 0.5f }, 0.5f);
        c.glob (7, 0.4f, kClockSeg, 0.3f, 0.2f, 1.5f); c.macros (2, kMix); } },
    { "Kalimba - Boite a musique", [] (const PCtx& c) {
        c.im (MEcho, { 0.85f, 0.3f, 0.5f, 0.3f, 0.5f, 0.2f, 0.6f, 0.6f, 0.5f, 0.0f, 0.7f, 0.75f }, { 0, 0, 0, 0, 0, 0, 0, 0, 0.05f });
        c.slot (0, TResonator, { idx (12, 13), idx (7, 12), 0.6f, 0.6f, idx (1, 4), 0.3f }, 0.3f);
        c.slot (1, TDelay,     { idx (5, 12), 0.45f, 0.45f, 0.9f, 0.15f, 1.0f }, 0.4f);
        c.slot (2, TShimmer,   { 0.55f, 0.7f, 0.4f, idx (1, 8), 0.4f, 0.4f }, 0.4f);
        c.glob (13, 0.35f, kClockPhrase); c.macros (1, kMix); } },
    { "Contrebasse - Ombre", [] (const PCtx& c) {
        c.im (MDialogue, { 0.7f, 0.35f, 0.5f, 0.4f, 0.6f, 0.2f, 0.75f, 0.5f, 0.5f, 0.1f, 0.6f, 0.75f });
        c.slot (0, TDegrade, { 0.15f, 0.2f, 0.15f, 0.3f, 0.1f, 0.45f }, 0.3f);
        c.slot (1, TReverse, { idx (2, 6), 0.5f, 0.6f, 0.1f, 0.3f, 0.5f }, 0.3f, 0.5f);
        c.slot (2, TReverb,  { idx (0, 5), 0.35f, 0.6f, 0.5f, 0.03f, 0.3f }, 0.25f);
        c.glob (5, 0.35f, kClockPhrase); c.macros (1, kMix); } },
    { "Piano - Memoire lointaine", [] (const PCtx& c) {
        c.im (MDialogue, { 0.65f, 0.4f, 0.55f, 0.45f, 0.7f, 0.15f, 1.0f, 0.5f, 0.5f, 0.05f, 0.6f, 0.7f });
        c.slot (0, TReverse, { idx (2, 6), 0.55f, 0.7f, 0.1f, 0.4f, 0.5f }, 0.4f, 0.5f);
        c.slot (1, TDegrade, { 0.1f, 0.2f, 0.15f, 0.45f, 0.1f, 0.4f }, 0.4f);
        c.slot (2, TReverb,  { idx (2, 5), 0.7f, 0.8f, 0.5f, 0.05f, 0.4f }, 0.45f);
        c.glob (6, 0.35f, kClockPhrase, 0.5f, 0.2f, 1.4f); c.macros (0, kMix); } },
    { "Flute - Oiseaux mecaniques", [] (const PCtx& c) {
        c.im (MDuo, { 0.7f, 0.3f, 0.4f, 0.4f, 0.4f, 0.15f, 0.7f, 0.5f, 0.5f, 0.0f, 0.0f, 0.7f }, { 0, 0, 0, 0, 0, 0, 0, 0, 0.2f });
        c.slot (0, TPitch,   { semi (12), 0.5f, 0.4f, 0.1f, 0.2f, 0.2f }, 0.5f, 0.5f);
        c.slot (1, TScatter, { 0.45f, 0.4f, 0.3f, 0.6f, 0.9f, idx (6, 7) }, 0.35f);
        c.slot (2, TReverb,  { idx (1, 5), 0.5f, 0.6f, 0.4f, 0.05f, 0.4f }, 0.3f);
        c.glob (8, 0.45f, kClockSeg); c.macros (0, kMix); } },
    { "Guitare - Echos du passe", [] (const PCtx& c) {
        c.im (MEcho, { 0.85f, 0.3f, 0.5f, 0.35f, 0.5f, 0.15f, 0.5f, 0.5f, 0.5f, 0.1f, 0.7f, 0.75f });
        c.slot (0, TDegrade, { 0.2f, 0.25f, 0.2f, 0.5f, 0.15f, 0.35f }, 0.5f);
        c.slot (1, TDelay,   { idx (9, 12), 0.5f, 0.4f, 0.6f, 0.2f, 1.0f }, 0.4f);
        c.slot (2, TReverb,  { idx (1, 5), 0.5f, 0.6f, 0.5f, 0.05f, 0.3f }, 0.3f);
        c.glob (2, 0.35f, kClockPhrase); c.macros (1, kMix); } },
    { "Trompette - Metropolis", [] (const PCtx& c) {
        c.im (MDialogue, { 0.75f, 0.3f, 0.5f, 0.35f, 0.55f, 0.2f, 0.75f, 0.4f, 0.5f, 0.0f, 0.6f, 0.75f });
        c.slot (0, TDist,   { idx (1, 5), 0.3f, 0.45f, 0.3f, 0.6f, 0.0f }, 0.35f);
        c.slot (1, TDelay,  { idx (8, 12), 0.4f, 0.35f, 0.6f, 0.15f, 1.0f }, 0.3f, 0.5f);
        c.slot (2, TReverb, { idx (2, 5), 0.7f, 0.8f, 0.4f, 0.05f, 0.4f }, 0.4f);
        c.glob (10, 0.35f, kClockPhrase); c.macros (2, kMix); } },
    { "Cordes - Spectre", [] (const PCtx& c) {
        c.im (MDuo, { 0.6f, 0.4f, 0.6f, 0.5f, 0.65f, 0.15f, 0.85f, 0.5f, 0.5f, 0.1f, 0.0f, 0.65f });
        c.slot (0, TGranular, { 0.75f, 0.4f, 0.7f, semi (0), 0.2f, 0.0f }, 0.4f);
        c.slot (1, TReverse,  { idx (3, 6), 0.6f, 0.7f, 0.1f, 0.4f, 0.5f }, 0.3f);
        c.slot (2, TShimmer,  { 0.65f, 0.8f, 0.4f, idx (0, 8), 0.45f, 0.5f }, 0.4f);
        c.glob (11, 0.35f, 7, 0.8f, 0.3f, 1.5f); c.macros (0, kMix); } },
    { "EXTREME - Hantise", [] (const PCtx& c) {
        c.im (MDuo, { 0.9f, 0.2f, 0.5f, 0.25f, 0.2f, 0.1f, 0.9f, 0.3f, 0.5f, 0.4f, 0.0f, 0.75f }, { 0.2f, 0, 0.3f, 0.3f, 0.3f, 0, 0, 0.3f, 0.4f, 0.3f });
        c.slot (0, TGranular, { 0.4f, 0.6f, 0.6f, semi (0), 0.6f, 0.5f }, 0.6f, 1.0f, { 0.3f, 0.3f, 0.3f });
        c.slot (1, TDist,     { idx (3, 5), 0.5f, 0.5f, 0.4f, 0.5f, 0.4f }, 0.5f, 0.6f);
        c.slot (2, TRing,     { idx (0, 3), 0.45f, 0.6f, 0.4f, 0.3f, 0.4f }, 0.4f, 0.5f);
        c.slot (3, TReverse,  { idx (2, 6), 0.6f, 0.7f, 0.2f, 0.4f, 0.5f }, 0.5f);
        c.glob (17, 0.7f, kClockSeg, 0.0f, 0.4f, 1.6f); c.macros (1, 1); } },
    { "Init (voix nue)", [] (const PCtx& c) {
        c.im (MDialogue, { 0.7f, 0.3f, 0.45f, 0.35f, 0.6f, 0.15f, 0.7f, 0.5f, 0.5f, 0.0f, 0.6f, 0.75f });
        c.glob (17, 0.3f, kClockPhrase); } },
};
constexpr int kNumPresets = (int) (sizeof (presets) / sizeof (presets[0]));
} // namespace

int numFactoryPresets() { return kNumPresets; }
juce::String factoryPresetName (int i) { return presets[juce::jlimit (0, kNumPresets - 1, i)].name; }

void applyFactoryPreset (MariaProcessor& p, int i)
{
    reset (p, false);
    PCtx c { p };
    presets[juce::jlimit (0, kNumPresets - 1, i)].fn (c);
}

void applySourceStarter (MariaProcessor& p, int source)
{
    reset (p, true);
    PCtx c { p };
    p.setParamReal ("source", (float) source);
    const auto& pr = sourceProfile (source);
    if (pr.symMode == 0)   // batterie, percussions, objets : duo rythmique
    {
        c.im (MDuo, { 0.8f, 0.25f, 0.4f, 0.3f, 0.45f, 0.3f, 0.65f, 0.5f, 0.5f, 0.0f, 0.3f, 0.75f }, { 0, 0, 0, 0, 0.15f, 0, 0, 0, 0.1f, 0.2f });
        c.slot (0, TDelay,  { idx (5, 12), 0.35f, 0.5f, 0.8f, 0.1f, 1.0f }, 0.3f, 0.3f);
        c.slot (1, TReverb, { idx (0, 5), 0.35f, 0.5f, 0.5f, 0.02f, 0.3f }, 0.25f);
        p.setParamReal ("clock", (float) kClockSeg);
        c.macros (0, kMix);
    }
    else if (pr.symMode == 1)   // instruments melodiques : dialogue
    {
        c.im (MDialogue, { 0.75f, 0.3f, 0.5f, 0.35f, 0.55f, 0.2f, 0.75f, 0.6f, 0.5f, 0.0f, 0.65f, 0.75f });
        c.slot (0, TDelay,  { idx (6, 12), 0.4f, 0.4f, 0.7f, 0.15f, 1.0f }, 0.25f, 0.4f);
        c.slot (1, TReverb, { idx (1, 5), 0.55f, 0.6f, 0.5f, 0.05f, 0.4f }, 0.3f);
        c.macros (1, kMix);
    }
    else   // polyphoniques / hybrides
    {
        c.im (MDialogue, { 0.7f, 0.35f, 0.5f, 0.4f, 0.6f, 0.15f, 0.8f, 0.5f, 0.5f, 0.05f, 0.6f, 0.72f });
        c.slot (0, TMod,    { idx (0, 3), 0.25f, 0.5f, 0.2f, 0.5f, 0.8f }, 0.3f);
        c.slot (1, TReverb, { idx (2, 5), 0.55f, 0.65f, 0.5f, 0.05f, 0.4f }, 0.3f);
        c.macros (1, kMix);
    }
    p.setParamReal ("chaos", 0.35f);
}
} // namespace ma
