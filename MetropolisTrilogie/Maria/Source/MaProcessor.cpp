#include "MaProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "MaPresets.h"
#include "MaEditor.h"

namespace ma
{
using alea::hUni;
using alea::hBi;
using alea::hash3;

static juce::ParameterID P (const juce::String& id) { return { id, 1 }; }

// Identifiants de memoire deja utilises par une instance ouverte (piste dupliquee...)
static juce::CriticalSection& idLock() { static juce::CriticalSection c; return c; }
static juce::StringArray& liveIds() { static juce::StringArray s; return s; }
static void registerId (const juce::String& id) { const juce::ScopedLock sl (idLock()); liveIds().addIfNotAlreadyThere (id); }
static void unregisterId (const juce::String& id) { const juce::ScopedLock sl (idLock()); liveIds().removeString (id); }
static bool idInUse (const juce::String& id) { const juce::ScopedLock sl (idLock()); return liveIds().contains (id); }

// ===========================================================================
juce::AudioProcessorValueTreeState::ParameterLayout MariaProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    const auto pctText = [] (float v, int) { return juce::String ((int) std::round (v * 100.0f)) + "%"; };
    const auto rndText = [] (float v, int) { return "rnd " + juce::String ((int) std::round (v * 100.0f)) + "%"; };

    for (int s = 0; s < kNumSlots; ++s)
    {
        const juce::String n = "S" + juce::String (s + 1) + " ";
        L.add (std::make_unique<juce::AudioParameterChoice> (P (sid (s, "type")), n + "Type", typeNames(), 0));
        L.add (std::make_unique<juce::AudioParameterBool> (P (sid (s, "on")), n + "On", true));
        for (int p = 0; p < kNumSP; ++p)
        {
            juce::AudioParameterFloatAttributes at;
            if (p < 6)
                at = at.withStringFromValueFunction ([this, s, p] (float v, int)
                {
                    const int t = curTypeView[s].load();
                    const int ax = auxParamFor (t, p);
                    const float aux = (ax >= 0 && pVal[s][ax] != nullptr) ? pVal[s][ax]->load() : 0.0f;
                    const float fund = pSource != nullptr ? sourceProfile ((int) std::round (pSource->load())).fund : 100.0f;
                    return paramText (t, p, v, aux, fund);
                });
            else
                at = at.withStringFromValueFunction (pctText);
            L.add (std::make_unique<juce::AudioParameterFloat> (P (pid (s, p)), n + (p < 6 ? "P" + juce::String (p + 1) : juce::String (p == kChance ? "Chance" : "Mix")),
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), p == kChance ? 1.0f : 0.5f, at));
            L.add (std::make_unique<juce::AudioParameterFloat> (P (jid (s, p)), n + (p < 6 ? "Rnd P" + juce::String (p + 1) : juce::String (p == kChance ? "Rnd Chance" : "Rnd Mix")),
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                                 juce::AudioParameterFloatAttributes().withStringFromValueFunction (rndText)));
        }
    }

    auto dB = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("mix"), "Mix", juce::NormalisableRange<float> (0, 1), 1.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("in"), "Input", juce::NormalisableRange<float> (-24, 12), 0.0f, dB));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("out"), "Output", juce::NormalisableRange<float> (-24, 12), 0.0f, dB));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("width"), "Width", juce::NormalisableRange<float> (0, 2), 1.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("duck"), "Duck", juce::NormalisableRange<float> (0, 1), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("chaos"), "Chaos", juce::NormalisableRange<float> (0, 1), 0.5f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("clock"), "Random clock", clockNames(), 3));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("rate"), "Free rate", juce::NormalisableRange<float> (0.05f, 10.0f, 0.0f, 0.35f), 1.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; })));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("glide"), "Glide", juce::NormalisableRange<float> (0, 1), 0.3f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterInt> (P ("seed"), "Seed", 0, 9999, 1));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("evolve"), "Evolve", juce::NormalisableRange<float> (0, 1), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("evobars"), "Evolve period", evolveBarNames(), 2));
    L.add (std::make_unique<juce::AudioParameterBool> (P ("freeze"), "Freeze", false));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("bpm"), "Internal BPM", juce::NormalisableRange<float> (40, 240, 0.1f), 120.0f));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("morph"), "Scene morph", juce::NormalisableRange<float> (0, 10, 0.0f, 0.5f), 2.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " s"; })));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("scene"), "Scene", [] { juce::StringArray a { "-" }; for (int i = 1; i <= kNumScenes; ++i) a.add (juce::String (i)); return a; }(), 0));
    for (int m = 0; m < kNumMacros; ++m)
        L.add (std::make_unique<juce::AudioParameterFloat> (P ("macro" + juce::String (m + 1)), "Macro " + juce::String (m + 1),
                                                             juce::NormalisableRange<float> (0, 1), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    static const int defRate[kNumRMods] { 1, 3, 2, 4, 5 };
    for (int m = 0; m < kNumRMods; ++m)
    {
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("modrate" + juce::String (m + 1)), alea::modNames()[m] + " rate", alea::modRateNames(), defRate[m]));
        L.add (std::make_unique<juce::AudioParameterFloat> (P ("modp" + juce::String (m + 1)), alea::modNames()[m] + " " + alea::modParamNames()[m],
                                                             juce::NormalisableRange<float> (0, 1), 0.5f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    }
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("envsens"), "Env sensitivity", juce::NormalisableRange<float> (0, 1), 0.5f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("source"), "Source", sourceNames(), kNumSources - 1));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("sens"), "Hit sensitivity", juce::NormalisableRange<float> (0, 1), 0.5f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("hitdec"), "Hit env decay", juce::NormalisableRange<float> (0, 1), 0.4f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String ((int) (expMap (v, 0.01f, 2.0f) * 1000)) + " ms"; })));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("envrel"), "Env release", juce::NormalisableRange<float> (0, 1), 0.3f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String ((int) (expMap (v, 0.02f, 3.0f) * 1000)) + " ms"; })));
    for (int r = 0; r < kNumRows; ++r)
    {
        const juce::String n = "row" + juce::String (r + 1);
        L.add (std::make_unique<juce::AudioParameterChoice> (P (n + "_src"), "Mod " + juce::String (r + 1) + " source", modSourceNames(), 0));
        L.add (std::make_unique<juce::AudioParameterChoice> (P (n + "_dst"), "Mod " + juce::String (r + 1) + " dest", modDestNames(), 0));
        L.add (std::make_unique<juce::AudioParameterFloat> (P (n + "_amt"), "Mod " + juce::String (r + 1) + " amount", juce::NormalisableRange<float> (-1, 1), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String ((int) std::round (v * 100)) + "%"; })));
    }
    // improvisatrice
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("im_mode"), "Maria mode", imModes(), 0));
    L.add (std::make_unique<juce::AudioParameterBool> (P ("learn"), "Maria learn", true));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("im_tempo"), "Maria tempo", tempoModes(), 0));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("im_grid"), "Maria grid", gridNames(), 2));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("im_align"), "Maria phrase start", alignNames(), 0));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("im_self"), "Maria self listening", juce::StringArray { "Apprend tout", "Ignore sa voix (auto)", "Pause pendant ses phrases" }, 1));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("im_tmode"), "Maria transpose mode", juce::StringArray { "Bande (vitesse)", "Hauteur seule", "Aleatoire" }, 1));
    L.add (std::make_unique<juce::AudioParameterBool> (P ("v2_on"), "Voice 2 on", false));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("v2_mode"), "Voice 2 mode", imModes(), 3));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("v2_level"), "Voice 2 level", juce::NormalisableRange<float> (0, 1), 0.65f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return imText (ILevel, v); })));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("v2_transp"), "Voice 2 transpose", juce::NormalisableRange<float> (0, 1), 0.5f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const float s = imTranspSemis (v); return (s > 0 ? "+" : "") + juce::String ((int) s) + " st"; })));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("v2_pan"), "Voices spread", juce::NormalisableRange<float> (0, 1), 0.6f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    {
        juce::StringArray ch { "Off" };
        for (int i = 1; i <= 16; ++i) ch.add ("Canal " + juce::String (i));
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("midi_out"), "MIDI out", ch, 0));
    }
    L.add (std::make_unique<juce::AudioParameterBool> (P ("autosave"), "Memory with session", true));
    // jeu a plusieurs : harmonie, leitmotivs, ecoute croisee, corpus, sorties
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("im_harm"), "Maria harmony", juce::NormalisableRange<float> (0, 1), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("im_harmmode"), "Maria harmony mode", juce::StringArray { "Choisir", "Choisir + transposer" }, 1));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("im_motvar"), "Leitmotiv variation", juce::NormalisableRange<float> (0, 1), 0.35f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("sc_learn"), "Learn from", juce::StringArray { "Piste", "Sidechain", "Piste + sidechain" }, 0));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("sc_react"), "React to", juce::StringArray { "Piste", "Sidechain", "Piste + sidechain" }, 0));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("corpus_mix"), "Corpus mix", juce::NormalisableRange<float> (0, 1), 0.5f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                         { return v < 0.005f ? juce::String ("direct seul") : (v > 0.995f ? juce::String ("corpus seul") : juce::String ((int) std::round (v * 100.0f)) + "% corpus"); })));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("corpus_src"), "Corpus source", sourceNames(), kNumSources - 1));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("out_route"), "Output routing", juce::StringArray { "Melange stereo", "Voix 1 a gauche / voix 2 a droite" }, 0));
    // forme : chapitres, arc d'intensite ; apprentissage MIDI
    {
        juce::StringArray nums;
        for (int i = 1; i <= 16; ++i) nums.add (juce::String (i));
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("chap_cur"), "Chapter", nums, 0));
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("chap_play"), "Chapter playback",
                                                              juce::StringArray { "Toute la memoire", "Chapitre en cours", "Chapitre choisi", "Les autres chapitres" }, 0));
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("chap_pick"), "Chosen chapter", nums, 0));
    }
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("arc_goal"), "Arc goal", juce::NormalisableRange<float> (0, 1), 0.5f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                         { return std::abs (v - 0.5f) < 0.01f ? juce::String ("neutre") : (v > 0.5f ? "intense " : "calme ") + juce::String ((int) std::round (std::abs (v - 0.5f) * 200.0f)) + "%"; })));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("arc_time"), "Arc time", juce::NormalisableRange<float> (2.0f, 600.0f, 0.0f, 0.35f), 60.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                         { return v < 60.0f ? juce::String ((int) v) + " s" : juce::String ((int) (v / 60)) + " min " + juce::String ((int) v % 60).paddedLeft ('0', 2); })));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("arc_depth"), "Arc depth", juce::NormalisableRange<float> (0, 1), 0.8f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    {
        juce::StringArray ch { "Off" }, ch2;
        for (int i = 1; i <= 16; ++i) { ch.add ("Canal " + juce::String (i)); ch2.add ("Canal " + juce::String (i)); }
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("midi_learn"), "MIDI learn channel", ch, 0));
        L.add (std::make_unique<juce::AudioParameterBool> (P ("mv_on"), "MIDI voice on", false));
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("mv_mode"), "MIDI voice mode", imModes(), 0));
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("mv_ch"), "MIDI voice channel", ch2, 2));
        L.add (std::make_unique<juce::AudioParameterChoice> (P ("fb_ch"), "Feedback channel", ch, 0));
    }
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("mach_len"), "Machine loop", juce::StringArray { "1 temps", "2 temps", "1 mesure", "2 mesures", "4 mesures" }, 2));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("mach_mut"), "Machine mutation", juce::NormalisableRange<float> (0, 1), 0.15f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterBool> (P ("drone_on"), "Drone on", false));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("drone_level"), "Drone level", juce::NormalisableRange<float> (0, 1), 0.5f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("drone_time"), "Drone renewal", juce::NormalisableRange<float> (3.0f, 60.0f, 0.0f, 0.5f), 12.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String ((int) v) + " s"; })));
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("drone_reg"), "Drone register", juce::StringArray { "-2 octaves", "-1 octave", "Original", "+1 octave" }, 2));
    for (int i = 0; i < kNumIm; ++i)
    {
        L.add (std::make_unique<juce::AudioParameterFloat> (P (imId (i)), "Maria " + imLabel (i), juce::NormalisableRange<float> (0, 1), imDefault (i),
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction ([i] (float v, int) { return imText (i, v); })));
        L.add (std::make_unique<juce::AudioParameterFloat> (P (imJid (i)), "Maria Rnd " + imLabel (i), juce::NormalisableRange<float> (0, 1), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction (rndText)));
    }
    // ajoute en fin de liste : les automations existantes ne bougent pas
    L.add (std::make_unique<juce::AudioParameterFloat> (P ("soft"), "Nappe douce", juce::NormalisableRange<float> (0, 1), 0.0f,
                                                         juce::AudioParameterFloatAttributes().withStringFromValueFunction (pctText)));
    return L;
}

// ===========================================================================
MariaProcessor::MariaProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "MARIA", createLayout())
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        curTypeView[s].store (0);
        slotLock[s].store (false);
        slotMeter[s].store (0.0f);
        for (int p = 0; p < kNumSP; ++p) { lockP[s][p].store (false); liveP[s][p].store (0.5f); }
    }
    for (auto& m : modView) m.store (0.5f);
    for (auto& c : ccMap) c.store (-1);

    for (int s = 0; s < kNumSlots; ++s)
    {
        pType[s] = apvts.getRawParameterValue (sid (s, "type"));
        pOn[s] = apvts.getRawParameterValue (sid (s, "on"));
        for (int p = 0; p < kNumSP; ++p)
        {
            pVal[s][p] = apvts.getRawParameterValue (pid (s, p));
            pJit[s][p] = apvts.getRawParameterValue (jid (s, p));
        }
        for (int t = 0; t < kNumTypes; ++t) slots[s].eng[(size_t) t] = makeEngine (t);
    }
    pMix = apvts.getRawParameterValue ("mix");     pIn = apvts.getRawParameterValue ("in");
    pOut = apvts.getRawParameterValue ("out");     pWidth = apvts.getRawParameterValue ("width");
    pDuck = apvts.getRawParameterValue ("duck");   pChaos = apvts.getRawParameterValue ("chaos");
    pClock = apvts.getRawParameterValue ("clock"); pRate = apvts.getRawParameterValue ("rate");
    pGlide = apvts.getRawParameterValue ("glide"); pSeed = apvts.getRawParameterValue ("seed");
    pEvolve = apvts.getRawParameterValue ("evolve"); pEvoBars = apvts.getRawParameterValue ("evobars");
    pFreeze = apvts.getRawParameterValue ("freeze"); pBpm = apvts.getRawParameterValue ("bpm");
    pMorph = apvts.getRawParameterValue ("morph");
    for (int m = 0; m < kNumMacros; ++m) pMacro[m] = apvts.getRawParameterValue ("macro" + juce::String (m + 1));
    for (int m = 0; m < kNumRMods; ++m)
    {
        pModRate[m] = apvts.getRawParameterValue ("modrate" + juce::String (m + 1));
        pModP[m] = apvts.getRawParameterValue ("modp" + juce::String (m + 1));
    }
    pEnvSens = apvts.getRawParameterValue ("envsens"); pEnvRel = apvts.getRawParameterValue ("envrel");
    pSource = apvts.getRawParameterValue ("source"); pSens = apvts.getRawParameterValue ("sens"); pHitDec = apvts.getRawParameterValue ("hitdec");
    for (auto& v : scopePk) v.store (0.0f);
    for (auto& v : scopeHit) v.store (0.0f);
    for (int r = 0; r < kNumRows; ++r)
    {
        const juce::String n = "row" + juce::String (r + 1);
        pRowSrc[r] = apvts.getRawParameterValue (n + "_src");
        pRowDst[r] = apvts.getRawParameterValue (n + "_dst");
        pRowAmt[r] = apvts.getRawParameterValue (n + "_amt");
    }

    for (int i = 0; i < kNumIm; ++i)
    {
        pIm[i] = apvts.getRawParameterValue (imId (i));
        pImJ[i] = apvts.getRawParameterValue (imJid (i));
        lockIm[i].store (false); liveIm[i].store (imDefault (i)); effIm[i] = imDefault (i);
    }
    lockIm[ILevel].store (true);
    pImMode = apvts.getRawParameterValue ("im_mode");
    pLearn = apvts.getRawParameterValue ("learn");
    pTempo = apvts.getRawParameterValue ("im_tempo");
    pGrid = apvts.getRawParameterValue ("im_grid");
    pAlign = apvts.getRawParameterValue ("im_align");
    pSelf = apvts.getRawParameterValue ("im_self");     pTMode = apvts.getRawParameterValue ("im_tmode");
    pV2On = apvts.getRawParameterValue ("v2_on");       pV2Mode = apvts.getRawParameterValue ("v2_mode");
    pV2Level = apvts.getRawParameterValue ("v2_level"); pV2Transp = apvts.getRawParameterValue ("v2_transp");
    pV2Pan = apvts.getRawParameterValue ("v2_pan");     pMidiOut = apvts.getRawParameterValue ("midi_out");
    pAutosave = apvts.getRawParameterValue ("autosave");
    pHarm = apvts.getRawParameterValue ("im_harm");         pHarmMode = apvts.getRawParameterValue ("im_harmmode");
    pMotVar = apvts.getRawParameterValue ("im_motvar");     pScLearn = apvts.getRawParameterValue ("sc_learn");
    pScReact = apvts.getRawParameterValue ("sc_react");     pCorpusMix = apvts.getRawParameterValue ("corpus_mix");
    pCorpusSrc = apvts.getRawParameterValue ("corpus_src"); pOutRoute = apvts.getRawParameterValue ("out_route");
    pChapCur = apvts.getRawParameterValue ("chap_cur");     pChapPlay = apvts.getRawParameterValue ("chap_play");
    pChapPick = apvts.getRawParameterValue ("chap_pick");   pArcGoal = apvts.getRawParameterValue ("arc_goal");
    pArcTime = apvts.getRawParameterValue ("arc_time");     pArcDepth = apvts.getRawParameterValue ("arc_depth");
    pMidiLearn = apvts.getRawParameterValue ("midi_learn"); pMvOn = apvts.getRawParameterValue ("mv_on");
    pMvMode = apvts.getRawParameterValue ("mv_mode");       pMvCh = apvts.getRawParameterValue ("mv_ch");
    pMachLen = apvts.getRawParameterValue ("mach_len");     pMachMut = apvts.getRawParameterValue ("mach_mut");
    pDroneOn = apvts.getRawParameterValue ("drone_on");     pDroneLevel = apvts.getRawParameterValue ("drone_level");
    pDroneTime = apvts.getRawParameterValue ("drone_time"); pDroneReg = apvts.getRawParameterValue ("drone_reg");
    pFbCh = apvts.getRawParameterValue ("fb_ch");
    pSoft = apvts.getRawParameterValue ("soft");
    for (auto& x : fbNote) x = -1;
    for (auto& x : fbCC) x = -1;
    for (int i = 0; i < kMaxRegions; ++i) { regStart[(size_t) i].store (0); regChap[(size_t) i].store (0); }
    for (int k = 0; k < kNumMotifs; ++k) { motA[(size_t) k].store (-1); motB[(size_t) k].store (-1); pendingMotifOff[(size_t) k] = { -1, -1 }; }
    memId = juce::Uuid().toString();
    registerId (memId);

    buildSceneParamList();
    msgRng.setSeedRandomly();
    loadFactory (0);
    undoStack.clear();
    mods.reset();
#if ! MA_HEADLESS_TEST
    startTimerHz (30);
#endif
}

MariaProcessor::~MariaProcessor()
{
    unregisterId (memId);
    stopTimer();
    vmidiReady.store (false);
    if (vmidi != nullptr)
    {
        if (vmidi->out != nullptr) for (int ch = 1; ch <= 16; ++ch) vmidi->out->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
        vmidi->stopThread (500);
    }
    if (analyzer != nullptr) analyzer->stopThread (2000);
    if (fbOut != nullptr) fbOut->stopThread (300);
}

bool MariaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = l.getMainInputChannelSet();
    if (! (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono())) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.getChannelSet (true, 1);
        if (! (sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo())) return false;
    }
    return true;
}

void MariaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (! mem.isPrepared() || std::abs (mem.sampleRate() - sampleRate) > 1.0)
    {
        if (analyzer != nullptr) analyzer->stopThread (2000);
        mem.prepare (sampleRate, Memory::kLiveSeconds);
    }
    impro.prepare (sampleRate);
    impro2.prepare (sampleRate);
    listener.prepare (sampleRate);
    pulse.prepare (sampleRate);
    if (! midiMem.isPrepared() || std::abs (midiMem.sampleRate() - sampleRate) > 1.0) midiMem.prepare (sampleRate, 1800.0, true);
    improMidi.prepare (sampleRate);
    drone.prepare (sampleRate); droneLeft = 0.0; droneWasOn = false;
    kbHeld = 0; kbIdle = 1 << 30;
    arcCur = pArcGoal->load(); lastGoal = (float) arcCur; arcSpeed = 0.0; arcD = 0.0f;
    scBuf.setSize (2, juce::jmax (kSub, samplesPerBlock));
    learnEnv = 0.0f; holdL = 0;
    phOpen = false; phIdle = 0;
    if (corpusReady.load() && std::abs (corpusFs - sampleRate) > 1.0)
    {
        // le corpus a ete analyse a une autre frequence : il sera recharge
        corpusReady.store (false);
        const juce::ScopedLock sl (loadLock);
        pendingCorpus = corpusFile;
    }
    outEnv = 0.0f;
    det.reset();
    writing = false; holdW = holdP = 0; playerEnv = playerSm = segEnv = 0;
#if ! MA_HEADLESS_TEST
    if (analyzer == nullptr) analyzer = std::make_unique<AnalyzerThread> (*this);
    if (! analyzer->isThreadRunning()) analyzer->startThread (juce::Thread::Priority::low);
#endif
    fs = sampleRate;
    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& S = slots[s];
        S.cur = slotType (s); S.pending = -1; S.fade = 1.0f; S.onGain = 1.0f;
        for (int t = 0; t < kNumTypes; ++t)
        {
            // seul le moteur en service est prepare ; les autres le seront a la demande (memoire economisee)
            if (t == S.cur || t == 0) { S.eng[(size_t) t]->prepare (fs); S.st[(size_t) t].store (t == S.cur ? 3 : 2); }
            else { S.eng[(size_t) t] = makeEngine (t); S.st[(size_t) t].store (0); }
            S.lastUse[(size_t) t] = 0.0;
        }
    }
    panicPhase = 0; panicGain = 1.0f; panicResetPending = false;
    work.setSize (2, kSub);
    dry.setSize (2, juce::jmax (kSub, samplesPerBlock));
    mods.reset();
    lastTickIdx = INT64_MIN; lastEvo = INT64_MIN;
    for (auto& v : rndCur) v = 0; for (auto& v : rndTgt) v = 0;
    for (auto& v : evoCur) v = 0; for (auto& v : evoTgt) v = 0;
    envIn = envMod = duckEnv = 0; dcL = dcR = dcxL = dcxR = 0; limGain = 1.0f;
    lastMixG = *pMix; lastInG = juce::Decibels::decibelsToGain (pIn->load()); lastOutG = juce::Decibels::decibelsToGain (pOut->load());
    freeCountdown = 0;
    for (int s = 0; s < kNumSlots; ++s) { slotOpen[s] = true; slotGate[s] = 1.0f; }
}

int MariaProcessor::slotType (int s) const { return juce::jlimit (0, kNumTypes - 1, (int) std::round (pType[s]->load())); }
float MariaProcessor::paramValue (int s, int p) const { return pVal[s][p]->load(); }

// ===========================================================================
void MariaProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    const int learnCh = (int) std::round (pMidiLearn->load());
    const bool learnOn = pLearn->load() > 0.5f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (learnCh > 0 && m.getChannel() == learnCh && (m.isAllNotesOff() || m.isAllSoundOff())) { kbHeld = 0; continue; }
        if (learnCh > 0 && m.getChannel() == learnCh && (m.isNoteOn() || m.isNoteOff()))
        {
            // canal d'apprentissage : les notes sont de la matiere musicale, pas des commandes
            const bool on = m.isNoteOn();
            kbHeld = juce::jmax (0, kbHeld + (on ? 1 : -1));
            kbIdle = 0; kbSince = 0;
            if (learnOn && midiMem.isPrepared())
            {
                int s1, n1, s2, n2;
                kbFifo.prepareToWrite (1, s1, n1, s2, n2);
                const KbEv e { midiMem.writePos() + meta.samplePosition, m.getNoteNumber(), on ? juce::jmax (1, (int) m.getVelocity()) : 0 };
                if (n1 > 0) kbEv[(size_t) s1] = e; else if (n2 > 0) kbEv[(size_t) s2] = e;
                kbFifo.finishedWrite (n1 + n2);
            }
            continue;
        }
        if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const int learn = learnParam.load();
            if (learn >= 0)
            {
                for (auto& c : ccMap) if (c.load() == learn) c.store (-1);
                ccMap[(size_t) cc].store (learn);
                learnParam.store (-1);
                continue;
            }
            const int idx = ccMap[(size_t) cc].load();
            const auto& ps = getParameters();
            if (idx >= 0 && idx < ps.size())
                ps[idx]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        }
        else if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (n >= 36 && n <= 43) pendingScene.store (sceneBank.load() * kSceneBank + (n - 36));
            else if (n == 44) pendingDice.store (1);
            else if (n == 45) pendingDice.store (2);
            else if (n == 46) pendingFreezeToggle.store (1);
            else if (n == 47) panic();
            else if (n == 48) impro.forcePhrase();
            else if (n == 49) impro.forceSilence();
            else if (n == 50) pendingLearnToggle.store (1);
            else if (n == 51) clearMemory();
            else if (n >= 52 && n <= 55) markMotif (n - 52);
            else if (n >= 56 && n <= 59) triggerMotif (n - 56);
            else if (n == 60) nextChapter();
            else if (n == 61) arcTo (1.0f);
            else if (n == 62) arcTo (0.0f);
            else if (n == 63) arcTo (0.5f);
        }
    }
}

// ===========================================================================
void MariaProcessor::computeModulation (int n, double ppq, double pps, bool tickChanged)
{
    const uint32_t seed = (uint32_t) pSeed->load() * 2654435761u + 17u;
    const double bpm = lastBpm.load();
    const int clk = (int) std::round (pClock->load());

    if (tickChanged)
        for (int i = 0; i < kNP + 4; ++i)
            rndTgt[i] = alea::shapedBi (1, (uint32_t) tick, (uint32_t) i + 1u, seed);
    if (clk == 0)
        for (int i = 0; i < kNP + 4; ++i) rndTgt[i] = 0.0f;

    const double tickSec = clk == kClockFree ? 1.0 / juce::jmax (0.05f, pRate->load())
                                             : (clk >= kClockSeg ? 0.05 : (clk > 0 ? clockBeats (clk) * 60.0 / bpm : 0.5));
    const double tau = juce::jmax (0.0005, (double) pGlide->load() * tickSec);
    const float gc = (float) (1.0 - std::exp (-(double) n / (tau * fs)));
    for (int i = 0; i < kNP + 4; ++i) rndCur[i] += (rndTgt[i] - rndCur[i]) * gc;

    // evolution lente
    const float evolve = pEvolve->load();
    {
        const double per = 4.0 * evolveBars ((int) std::round (pEvoBars->load()));
        const int64_t e = (int64_t) std::floor (ppq / per);
        if (e != lastEvo)
        {
            const bool jump = lastEvo == INT64_MIN || std::abs (e - lastEvo) > 2;
            lastEvo = e;
            if (! jump)
                for (int i = 0; i < kNP; ++i)
                {
                    bool locked;
                    if (i < kNS) { const int s = i / kNumSP, p = i % kNumSP; locked = lockP[s][p].load() || p >= kChance; }
                    else { const int k = i - kNS; locked = lockIm[k].load() || k == ILevel; }
                    if (evolve <= 0.0f || locked) { evoTgt[i] = 0.0f; continue; }
                    evoTgt[i] = juce::jlimit (-0.45f * evolve, 0.45f * evolve,
                                              evoTgt[i] * 0.75f + hBi ((uint32_t) e, (uint32_t) i, seed ^ 0xE70u) * 0.3f * evolve);
                }
        }
        if (evolve <= 0.0f) for (int i = 0; i < kNP; ++i) evoTgt[i] = 0.0f;
        const float ec = (float) (1.0 - std::exp (-(double) n / (60.0 / bpm * 2.0 * fs)));
        for (int i = 0; i < kNP; ++i) evoCur[i] += (evoTgt[i] - evoCur[i]) * ec;
    }

    // sources
    {
        int rates[kNumRMods]; float pp[kNumRMods];
        for (int m = 0; m < kNumRMods; ++m) { rates[m] = (int) std::round (pModRate[m]->load()); pp[m] = pModP[m]->load(); }
        mods.advance (ppq, rates, pp, 8, seed);
    }
    float src[kNumModSrc] {};
    for (int m = 0; m < kNumRMods; ++m) { src[1 + m] = mods.value[m] * 2.0f - 1.0f; modView[m].store (mods.value[m]); }
    const float envN = juce::jlimit (0.0f, 1.0f, envMod * expMap (pEnvSens->load(), 0.5f, 40.0f));
    src[6] = envN; modView[5].store (envN);
    src[7] = rndCur[kNP]; modView[6].store (0.5f + 0.5f * rndCur[kNP]);
    src[kSrcSeg] = segEnv; modView[7].store (segEnv);
    src[kSrcPlayer] = playerSm;
    for (int m = 0; m < kNumMacros; ++m) src[kSrcMacro + m] = pMacro[m]->load();

    float dst[kNumDests] {};
    for (int r = 0; r < kNumRows; ++r)
    {
        const int si = (int) std::round (pRowSrc[r]->load()), di = (int) std::round (pRowDst[r]->load());
        if (si <= 0 || di <= 0 || si >= kNumModSrc || di >= kNumDests) continue;
        dst[di] += src[si] * pRowAmt[r]->load();
    }

    effChaos = juce::jlimit (0.0f, 1.0f, pChaos->load() + dst[kDestChaos] + 0.25f * arcD);
    chaosView.store (effChaos);
    effMix = juce::jlimit (0.0f, 1.0f, pMix->load() + dst[kDestMix]);
    effWidth = juce::jlimit (0.0f, 2.0f, pWidth->load() + dst[kDestWidth] * 2.0f);

    for (int s = 0; s < kNumSlots; ++s)
        for (int p = 0; p < kNumSP; ++p)
        {
            const int i = s * kNumSP + p;
            float v = pVal[s][p]->load() + pJit[s][p]->load() * effChaos * rndCur[i] + evoCur[i] + dst[1 + i];
            v = juce::jlimit (0.0f, 1.0f, v);
            eff[s][p] = v;
            liveP[s][p].store (v, std::memory_order_relaxed);
        }
    for (int k = 0; k < kNumIm; ++k) dstIm[k] = dst[kDestIm + k];
    recomputeIm();
    juce::ignoreUnused (pps);
}

void MariaProcessor::recomputeIm()
{
    for (int k = 0; k < kNumIm; ++k)
    {
        const int i = kNS + k;
        // arc d'intensite : plus active, phrases plus longues, moins de silences, plus forte
        static const float arcOff[kNumIm] { 0.45f, -0.2f, 0.3f, -0.45f, -0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -0.3f, 0.12f };
        const float v = juce::jlimit (0.0f, 1.0f, pIm[k]->load() + pImJ[k]->load() * effChaos * rndCur[i] + evoCur[i] + dstIm[k] + arcD * arcOff[k]);
        effIm[k] = v;
        liveIm[k].store (v, std::memory_order_relaxed);
    }
}

void MariaProcessor::segmentStarting (bool phraseStart)
{
    const int clk = (int) std::round (pClock->load());
    if (! (clk == kClockSeg || (clk == kClockPhrase && phraseStart))) return;
    const uint32_t seed = (uint32_t) pSeed->load() * 2654435761u + 17u;
    ++tick;
    tickView.store (tick);
    for (int i = 0; i < kNP + 4; ++i) rndTgt[i] = alea::shapedBi (1, (uint32_t) tick, (uint32_t) i + 1u, seed);
    for (int i = kNS; i < kNP; ++i) rndCur[i] = rndTgt[i];   // l'improvisatrice prend les nouvelles valeurs tout de suite
    recomputeIm();
}

// ===========================================================================
void MariaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
#if MA_HEADLESS_TEST
    serviceEngines (1.0e9);   // dans les tests, pas de thread message : preparation immediate
#endif
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n <= 0) return;
    // sidechain (ecoute croisee) : copie avant que les canaux ne soient reecrits
    int scCh = 0;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            const int c0 = getChannelIndexInProcessBlockBuffer (true, 1, 0);
            scCh = juce::jlimit (0, 2, juce::jmin (bus->getNumberOfChannels(), buffer.getNumChannels() - c0));
            if (scCh > 0)
            {
                if (scBuf.getNumSamples() < n) scBuf.setSize (2, n, false, false, true);
                for (int c = 0; c < scCh; ++c) scBuf.copyFrom (c, 0, buffer, c0 + c, 0, n);
            }
        }
    scChansView.store (scCh);
    const int numIn = getMainBusNumInputChannels();
    if (numIn == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    else if (numIn == 0) { buffer.clear (0, 0, n); buffer.clear (1, 0, n); }
    if (! mem.isPrepared()) { buffer.clear(); return; }

    handleMidi (midi);
    midi.clear();
    handleMotifRequests();
    blockStartMs = juce::Time::getMillisecondCounterHiRes();
    outMidi = &midi;
    if (dry.getNumSamples() < n) dry.setSize (2, n, false, false, true);

    double bpm = pBpm->load();
    bool playing = false;
    double hostPpq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) hostPpq = *q; else playing = false;
        }
    const int tempoMode = (int) std::round (pTempo->load());
    if (tempoMode == 2) { bpm = pBpm->load(); playing = false; }   // tempo manuel : Maria (et ses effets) suivent leur propre horloge
    bool pulseOk = false; double pulsePpq = 0.0, pulsePps = 0.0, pulseBpm = 120.0;
    if (tempoMode == 3)
    {
        // pulsation des musiciens : horloge deduite de leurs attaques
        pulseOk = pulse.clock (n, pulsePpq, pulsePps, pulseBpm);
        bpm = pulseBpm; playing = false;
    }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    lastBpm.store (bpm);
    const double pps = tempoMode == 3 ? pulsePps : bpm / 60.0 / fs;
    const double base = tempoMode == 3 ? pulsePpq : (playing ? hostPpq : freePpq);
    freePpq = base + pps * n;
    { double pb, ago; float pc;   // partage de la pulsation entendue (Rotwang et Grot s'y calent)
      if (pulse.estimate (pb, ago, pc)) metro::PulseBus::get().publish (pulseSlot, pb, blockStartMs - ago * 1000.0, pc, blockStartMs); }

    // chapitres : un changement de chapitre ouvre une nouvelle region de la memoire
    {
        const int cc = mem.clearCount.load();
        if (cc != lastClearSeen) { lastClearSeen = cc; regCount.store (0); lastChap = -1; }
        const int ch = currentChapter();
        if (chapReappend.exchange (false)) lastChap = -1;
        if (ch != lastChap) { appendRegion (mem.writePos(), ch); lastChap = ch; }
    }
    // arc d'intensite : glisse vers l'objectif dans le temps choisi
    {
        const float goal = pArcGoal->load();
        if (std::abs (goal - lastGoal) > 1.0e-4f)
        {
            arcSpeed = std::abs ((double) goal - arcCur) / (juce::jmax (0.1, (double) pArcTime->load()) * fs);
            lastGoal = goal;
        }
        const double step = arcSpeed * n;
        if (std::abs ((double) goal - arcCur) <= step) arcCur = goal; else arcCur += goal > arcCur ? step : -step;
        arcD = (float) ((arcCur - 0.5) * 2.0) * pArcDepth->load();
        arcView.store ((float) arcCur);
    }

    const bool freeze = pFreeze->load() > 0.5f;
    const uint32_t seed = (uint32_t) pSeed->load() * 2654435761u + 17u;
    const float inG = juce::Decibels::decibelsToGain (pIn->load());
    const float outG = juce::Decibels::decibelsToGain (pOut->load());
    const float duck = pDuck->load();
    const float envRelC = std::exp (-1.0f / (expMap (pEnvRel->load(), 0.02f, 3.0f) * (float) fs));
    const int clk = (int) std::round (pClock->load());

    const auto& prof = sourceProfile (sourceIndex());
    det.setup (prof, pSens->load(), fs);
    const float gate = juce::Decibels::decibelsToGain (gateDb());
    const float pRel = std::exp (-1.0f / (0.12f * (float) fs));
    const bool learn = pLearn->load() > 0.5f && ! loading.load();
    const int holdS = (int) (0.35 * fs);
    const float segDecC = (float) std::exp (-1.0 / (expMap (pHitDec->load(), 0.01f, 2.0f) * fs));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    float pkIn = 0, pkOut = 0;
    float slotPk[kNumSlots] {};

    if (panicReq.exchange (false) && panicPhase == 0) panicPhase = 1;
    for (int pos = 0; pos < n; pos += kSub)
    {
        if (panicResetPending)
        {
            // PANIQUE : toutes les queues sont videes, le freeze est coupe
            panicResetPending = false;
            for (auto& S : slots) { S.eng[(size_t) S.cur]->reset(); if (S.pending >= 0) S.eng[(size_t) S.pending]->reset(); }
            limGain = 1.0f; dcL = dcR = dcxL = dcxR = 0.0f;
            pendingFreezeOff.store (1);
                impro.forceSilence(); impro2.forceSilence(); improMidi.forceSilence();
                drone.reset(); droneLeft = 0.0;
                for (int vId = 0; vId < 3; ++vId) for (int nn = 0; nn < 128; ++nn) if (notesHanging[vId][nn]) emitNote (vId, nn, 0.0f, 0, false);
        }
        const int m = juce::jmin (kSub, n - pos);
        const double ppq = base + pps * pos;
        float* l = L + pos; float* r = R + pos;

        // entree : gain, ecoute des musiciens (piste et / ou sidechain), apprentissage
        float pk = 0;
        bool active = false, activeL = false;
        const int lSel = scCh > 0 ? (int) std::round (pScLearn->load()) : 0;
        const int rSel = scCh > 0 ? (int) std::round (pScReact->load()) : 0;
        for (int i = 0; i < m; ++i)
        {
            const float g = lastInG + (inG - lastInG) * (float) (i + 1) / (float) m;
            l[i] *= g; r[i] *= g;
            dry.setSample (0, pos + i, l[i]); dry.setSample (1, pos + i, r[i]);
            const float x = 0.5f * (l[i] + r[i]);
            float sx = 0.0f;
            if (scCh > 0) sx = g * (scCh == 1 ? scBuf.getSample (0, pos + i) : 0.5f * (scBuf.getSample (0, pos + i) + scBuf.getSample (1, pos + i)));
            trackMono[i] = x; sideMono[i] = sx;
            const float xl = lSel == 0 ? x : (lSel == 1 ? sx : 0.7f * (x + sx));
            const float xr = rSel == 0 ? x : (rSel == 1 ? sx : 0.7f * (x + sx));
            monoIn[i] = xl; reactIn[i] = xr;
            pk = juce::jmax (pk, std::abs (x));
            const float al = std::abs (xl), ar = std::abs (xr);
            learnEnv = al > learnEnv ? al : learnEnv * pRel;
            if (learnEnv > gate) holdL = holdS; else if (holdL > 0) --holdL;
            if (holdL > 0) activeL = true;
            playerEnv = ar > playerEnv ? ar : playerEnv * pRel;
            if (playerEnv > gate) holdP = holdS; else if (holdP > 0) --holdP;
            if (holdP > 0) active = true;
        }
        lastInG = inG;
        pkIn = juce::jmax (pkIn, pk);
        listener.push (reactIn, m, prof, pSens->load());
        pulse.push (reactIn, m);
        // anti-repiquage : si le micro entend surtout la voix de Maria, ce n'est pas toi qui joues
        const int selfMode = (int) std::round (pSelf->load());
        const bool mariaPlays = impro.isPlaying() || impro2.isPlaying();
        bool learnBlock = false;
        if (selfMode == 1)
        {
            if (active && playerEnv < outEnv * 0.5f) active = false;
            if (activeL && learnEnv < outEnv * 0.5f) learnBlock = true;
        }
        if (selfMode == 2 && mariaPlays) learnBlock = true;
        playerView.store (active);
        playerSm += ((active ? 1.0f : 0.0f) - playerSm) * 0.02f;
        playerLevel.store (playerEnv);
        envMod = pk > envMod ? pk : envMod * std::pow (envRelC, (float) m);
        duckEnv = pk > duckEnv ? duckEnv + (pk - duckEnv) * 0.5f : duckEnv * std::pow (0.99995f, (float) m * 0.4f);

        if (learn && activeL && ! learnBlock)
        {
            const int64_t w0 = mem.writePos();
            if (! writing) { mem.pushEvent (w0, 1); writing = true; det.reset(); }
            for (int i = 0; i < m; ++i)
            {
                const float hv = det.process (monoIn[i]);
                if (hv > 0.0f) { mem.pushEvent (w0 + i, 0); scopeHitAcc = juce::jmax (scopeHitAcc, hv); hitCountView.fetch_add (1); }
            }
            mem.write (monoIn, m);
            // phrase en cours (pour marquer un leitmotiv) : se ferme apres 1,5 s sans jeu
            if (! phOpen) { phOpen = true; phStart = w0; }
            phLast = mem.writePos(); phIdle = 0;
        }
        else
        {
            writing = false;
            if (phOpen && (phIdle += m) > (int) (1.5 * fs)) { lastPhA = phStart; lastPhB = phLast; phOpen = false; }
        }

        scopeAcc = juce::jmax (scopeAcc, pk);
        scopeCnt += m;
        if (scopeCnt >= 256)
        {
            const int w = (scopeW.load() + 1) % (int) scopePk.size();
            scopePk[(size_t) w].store (scopeAcc); scopeHit[(size_t) w].store (scopeHitAcc);
            scopeW.store (w);
            scopeAcc = 0; scopeHitAcc = 0; scopeCnt = 0;
        }

        // horloge aleatoire (tempo / libre)
        bool tickChanged = false;
        if (clk > 0 && clk < kClockFree)
        {
            const int64_t t = (int64_t) std::floor (ppq / clockBeats (clk));
            if (t != lastTickIdx) { lastTickIdx = t; tick = t; tickChanged = true; }
        }
        else if (clk == kClockFree)
        {
            freeCountdown -= m;
            if (freeCountdown <= 0.0)
            {
                ++tick; tickChanged = true;
                freeCountdown = fs / juce::jmax (0.05f, pRate->load()) * (0.35 + 1.3 * hUni ((uint32_t) tick, 0xF1u, seed));
            }
        }
        if (tickChanged) tickView.store (tick);
        beatView.store ((int) std::floor (ppq) & 3);

        computeModulation (m, ppq, pps, tickChanged);

        // l'improvisatrice
        TempoInfo ti;
        ti.sync = tempoMode != 0 && (tempoMode != 3 || pulseOk); ti.ppq = ppq; ti.pps = pps;
        ti.grid = gridBeats ((int) std::round (pGrid->load()));
        ti.align = alignBeats ((int) std::round (pAlign->load()));
        midiPos = pos;
        impro.process (mem, *this, prof, voice, voiceHits, m, active, seed, ti);
        const bool v2 = voice2On();
        if (v2) impro2.process (mem, v2host, prof, voice2, hits2, m, active, seed ^ 0x51ED5EEDu, ti);
        else if (impro2.isPlaying())
        {
            impro2.forceSilence();
            impro2.process (mem, v2host, prof, voice2, hits2, m, false, seed ^ 0x51ED5EEDu, ti);
        }
        else for (int i = 0; i < m; ++i) { voice2[i] = 0.0f; hits2[i] = 0.0f; }
        // voix MIDI : improvise avec la memoire du clavier, joue sur le port MIDI
        {
            const bool kbAct = kbHeld > 0 || kbIdle < (int64_t) (0.35 * fs);
            if (pMvOn->load() > 0.5f) improMidi.process (midiMem, mvHost, prof, mvOut, mvHits, m, active || kbAct, seed ^ 0x3D1D1u, ti);
            else if (improMidi.isPlaying()) { improMidi.forceSilence(); improMidi.process (midiMem, mvHost, prof, mvOut, mvHits, m, false, seed ^ 0x3D1D1u, ti); }
        }
        const float sp = pV2Pan->load();
        const float th1 = (1.0f - sp) * juce::MathConstants<float>::pi * 0.25f, th2 = (1.0f + sp) * juce::MathConstants<float>::pi * 0.25f;
        const float g1l = std::cos (th1) * 1.4142f, g1r = std::sin (th1) * 1.4142f, g2l = std::cos (th2) * 1.4142f, g2r = std::sin (th2) * 1.4142f;
        const bool anyV2 = v2 || impro2.isPlaying();
        const bool split = (int) std::round (pOutRoute->load()) == 1;
        for (int i = 0; i < m; ++i)
        {
            if (! split) voiceHits[i] = juce::jmax (voiceHits[i], hits2[i]);
            if (voiceHits[i] > 0.0f) segEnv = juce::jmax (segEnv, voiceHits[i]);
            segEnv *= segDecC;
            if (split) { l[i] = voice[i]; r[i] = voice[i]; voice2Buf[i] = anyV2 ? voice2[i] : 0.0f; }   // voix 2 a part (sans les effets)
            else if (anyV2) { l[i] = voice[i] * g1l + voice2[i] * g2l; r[i] = voice[i] * g1r + voice2[i] * g2r; }
            else { l[i] = voice[i]; r[i] = voice[i]; }
        }
        // nappes : fragments geles, renouveles en fondu enchaine, avant les effets
        {
            const bool on = pDroneOn->load() > 0.5f;
            if (on)
            {
                droneLeft -= m;
                if (! droneWasOn || droneLeft <= 0.0) { pickDroneFragment (! droneWasOn); droneLeft = pDroneTime->load() * fs; }
            }
            else if (droneWasOn) drone.stop (2.0);
            droneWasOn = on;
            if (drone.active())
            {
                static const float regRate[4] { 0.25f, 0.5f, 1.0f, 2.0f };
                const float rate = regRate[juce::jlimit (0, 3, (int) std::round (pDroneReg->load()))];
                const float lv = pDroneLevel->load();
                for (int i = 0; i < m; ++i) { droneL[i] = 0.0f; droneR[i] = 0.0f; }
                drone.process (droneL, droneR, m, lv * lv * 1.6f * (1.0f + 0.3f * arcD), rate);
                for (int i = 0; i < m; ++i) { l[i] += droneL[i]; r[i] += droneR[i]; }
            }
        }

        Ctx ctx;
        ctx.fs = fs; ctx.bpm = bpm; ctx.ppq = ppq; ctx.ppqPerSample = pps;
        ctx.freeze = freeze; ctx.seed = seed; ctx.tick = tick; ctx.env = juce::jlimit (0.0f, 1.0f, envMod * 4.0f);
        ctx.fund = prof.fund; ctx.body = prof.body; ctx.lo = prof.lo; ctx.hi = prof.hi; ctx.decayMs = prof.maxSegMs * 0.3f;
        ctx.side = scCh > 0 ? sideMono : nullptr; ctx.track = trackMono;
        const float gC = 1.0f - std::exp (-1.0f / (0.002f * (float) fs));

        float* tl = work.getWritePointer (0);
        float* tr = work.getWritePointer (1);
        for (int s = 0; s < kNumSlots; ++s)
        {
            auto& S = slots[s];
            const int want = slotType (s);
            curTypeView[s].store (want, std::memory_order_relaxed);
            if (S.pending < 0 && want != S.cur)
            {
                auto& stw = S.st[(size_t) want];
                int expect = 2;
                if (want == 0 || stw.compare_exchange_strong (expect, 3)) S.pending = want;
                else if (expect == 0) { int z = 0; stw.compare_exchange_strong (z, 1); }   // a preparer par le thread message
            }
            const bool onT = pOn[s]->load() > 0.5f;
            if (S.cur == 0)
            {
                if (S.pending >= 0) { S.cur = S.pending; S.pending = -1; S.fade = 0.0f; S.eng[(size_t) S.cur]->reset(); }
                if (S.cur == 0) { S.onGain = onT ? 1.0f : 0.0f; continue; }
            }
            if (! onT && S.onGain <= 0.0f) continue;
            if (onT && S.onGain <= 0.0f) S.eng[(size_t) S.cur]->reset();

            ctx.slot = s;
            const float chance = eff[s][kChance];
            const int kind = typeKind (S.cur);
            float sh[kSub], ga[kSub];
            for (int i = 0; i < m; ++i)
            {
                if (voiceHits[i] > 0.0f)
                    slotOpen[s] = chance >= 0.999f || hUni ((uint32_t) (segHits + i), (uint32_t) s + 0x51u, seed) < chance;
                if (chance >= 0.999f) slotOpen[s] = true;
                sh[i] = slotOpen[s] ? voiceHits[i] : 0.0f;
                slotGate[s] += ((slotOpen[s] ? 1.0f : 0.0f) - slotGate[s]) * gC;
                ga[i] = kind == KTrigger ? 1.0f : slotGate[s];
            }
            ctx.hit = sh;
            if (kind == KSend) for (int i = 0; i < m; ++i) { tl[i] = l[i] * ga[i]; tr[i] = r[i] * ga[i]; }
            else { std::copy (l, l + m, tl); std::copy (r, r + m, tr); }
            float pv[6];
            for (int p = 0; p < 6; ++p) pv[p] = eff[s][p];
            softenParams (S.cur, pv, pSoft->load());                     // nappe douce
            S.eng[(size_t) S.cur]->process (tl, tr, m, ctx, pv);

            const float mixT = eff[s][kMix];
            const float fstep = 1.0f / 256.0f, ostep = 1.0f / 512.0f;
            float spk = 0;
            for (int i = 0; i < m; ++i)
            {
                S.fade = S.pending >= 0 ? juce::jmax (0.0f, S.fade - fstep) : juce::jmin (1.0f, S.fade + fstep);
                S.onGain = onT ? juce::jmin (1.0f, S.onGain + ostep) : juce::jmax (0.0f, S.onGain - ostep);
                float a = tl[i], b = tr[i];
                if (! std::isfinite (a) || ! std::isfinite (b)) { a = b = 0.0f; S.eng[(size_t) S.cur]->reset(); }
                const float g = mixT * S.fade * S.onGain * (kind == KInsert ? ga[i] : 1.0f);
                l[i] += (a - l[i]) * g; r[i] += (b - r[i]) * g;
                spk = juce::jmax (spk, std::abs (a), std::abs (b));
            }
            slotPk[s] = juce::jmax (slotPk[s], spk);
            if (S.pending >= 0 && S.fade <= 0.0f)
            {
                if (S.cur > 0) S.st[(size_t) S.cur].store (2);
                S.cur = S.pending; S.pending = -1;
                S.eng[(size_t) S.cur]->reset();
            }
        }
        for (int i = 0; i < m; ++i) if (voiceHits[i] > 0.0f) ++segHits;

        // sortie
        const float duckG = 1.0f - duck * juce::jlimit (0.0f, 1.0f, duckEnv * 4.0f);
        for (int i = 0; i < m; ++i)
        {
            float a = l[i], b = r[i];
            if (split) { a = 0.5f * (a + b) * duckG; b = voice2Buf[i] * duckG; }      // G = voix 1 (avec effets), D = voix 2
            else
            {
                const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * effWidth;
                a = (mid + side) * duckG; b = (mid - side) * duckG;
            }
            const float ya = a - dcxL + 0.9995f * dcL; dcxL = a; dcL = ya;
            const float yb = b - dcxR + 0.9995f * dcR; dcxR = b; dcR = yb;
            a = ya; b = yb;
            const float peak = juce::jmax (std::abs (a), std::abs (b));
            if (peak * limGain > 0.97f) limGain = 0.97f / peak;
            else limGain += (1.0f - limGain) * 0.00005f;
            a = softClip (a * limGain); b = softClip (b * limGain);
            if (panicPhase == 1) { panicGain -= 1.0f / (0.03f * (float) fs); if (panicGain <= 0.0f) { panicGain = 0.0f; panicPhase = 2; panicHold = (int) (0.08 * fs); panicResetPending = true; } }
            else if (panicPhase == 2) { if (--panicHold <= 0) panicPhase = 3; }
            else if (panicPhase == 3) { panicGain += 1.0f / (0.3f * (float) fs); if (panicGain >= 1.0f) { panicGain = 1.0f; panicPhase = 0; } }
            a *= panicGain; b *= panicGain;
            { const float pkw = juce::jmax (std::abs (a), std::abs (b)); outEnv = pkw > outEnv ? pkw : outEnv * 0.99985f; }
            const float t = (float) (i + 1) / (float) m;
            const float mixG = lastMixG + (effMix - lastMixG) * t;
            const float og = lastOutG + (outG - lastOutG) * t;
            const float dl = dry.getSample (0, pos + i), dr = dry.getSample (1, pos + i);
            l[i] = (a * mixG + dl * (1.0f - mixG)) * og;
            r[i] = (b * mixG + dr * (1.0f - mixG)) * og;
            pkOut = juce::jmax (pkOut, std::abs (l[i]), std::abs (r[i]));
        }
        lastMixG = effMix; lastOutG = outG;
    }

    // temps de la memoire MIDI : il ne s'ecoule que pendant le jeu du clavier
    {
        const bool kbAct = kbHeld > 0 || kbIdle < (int64_t) (1.0 * fs);
        if (kbHeld == 0) kbIdle += n;
        kbSince += n;
        if (kbHeld > 0 && kbSince > (int64_t) (12.0 * fs)) kbHeld = 0;      // note-off perdu : on ne reste pas bloque
        kbView.store (kbHeld > 0 || kbIdle < (int64_t) (0.35 * fs));
        if (kbAct && pLearn->load() > 0.5f && midiMem.isPrepared()) midiMem.advance (n);
    }
    sendFeedback (0);
    panicPhaseView.store (panicPhase);
    meterIn.store (juce::jmax (pkIn, meterIn.load() * 0.85f));
    meterOut.store (juce::jmax (pkOut, meterOut.load() * 0.85f));
    for (int s = 0; s < kNumSlots; ++s) slotMeter[s].store (juce::jmax (slotPk[s], slotMeter[s].load() * 0.85f));
}

// ===========================================================================
// Analyse, fichiers
void MariaProcessor::emitNote (int voiceId, int note, float vel, int sample, bool on)
{
    const int chSel = (int) std::round (pMidiOut->load());
    if (outMidi == nullptr || note < 0 || note > 127) return;
    if (! on && ! notesHanging[voiceId][note]) return;
    notesHanging[voiceId][note] = on;
    int ch;
    if (voiceId == 2) ch = (int) std::round (pMvCh->load()) + 1;   // voix MIDI : son propre canal
    else
    {
        if (chSel <= 0) return;
        ch = ((chSel - 1 + voiceId) % 16) + 1;     // la 2e voix sort sur le canal suivant
    }
    const int t = juce::jmax (0, midiPos + sample);
    const auto v = (juce::uint8) juce::jlimit (1, 127, (int) (vel * 127.0f));
    if (on) { outMidi->addEvent (juce::MidiMessage::noteOn (ch, note, v), t); midiNotesSent.fetch_add (1); }
    else outMidi->addEvent (juce::MidiMessage::noteOff (ch, note), t);
    if (vmidiReady.load (std::memory_order_acquire))
    {
        // horodatage : debut du bloc + position de l'echantillon (le thread d'envoi respecte ce temps)
        MidiEv e { (juce::uint8) ((on ? 0x90 : 0x80) | (ch - 1)), (juce::uint8) note, on ? v : (juce::uint8) 0,
                   blockStartMs + (double) t / fs * 1000.0 };
        vmidi->push (e);
    }
}

void MariaProcessor::VirtualMidi::run()
{
    MidiEv pendingEv {}; bool hasPending = false;
    while (! threadShouldExit())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        for (;;)
        {
            if (! hasPending)
            {
                int s1, n1, s2, n2;
                fifo.prepareToRead (1, s1, n1, s2, n2);
                if (n1 + n2 == 0) break;
                pendingEv = n1 > 0 ? ev[(size_t) s1] : ev[(size_t) s2];
                fifo.finishedRead (1);
                hasPending = true;
            }
            if (pendingEv.t > now) break;
            if (out != nullptr) out->sendMessageNow (juce::MidiMessage (pendingEv.s, pendingEv.d1, pendingEv.d2));
            hasPending = false;
        }
        wait (1);
    }
}

void MariaProcessor::ensureVirtualMidi()
{
#if JUCE_MAC && ! MA_HEADLESS_TEST
    if (vmidi != nullptr || ((int) std::round (pMidiOut->load()) <= 0 && pMvOn->load() < 0.5f
                             && ((int) std::round (pFbCh->load()) <= 0 || feedbackDevice().isNotEmpty()))) return;
    static std::atomic<int> instances { 0 };
    const int k = ++instances;
    auto v = std::make_unique<VirtualMidi>();
    v->out = juce::MidiOutput::createNewDevice (k == 1 ? juce::String ("Maria") : "Maria " + juce::String (k));
    if (v->out == nullptr) return;
    v->startThread (juce::Thread::Priority::high);
    vmidi = std::move (v);
    vmidiReady.store (true, std::memory_order_release);
#endif
}

juce::File MariaProcessor::sessionMemoryFile() const
{
    return sessionsFolder().getChildFile (memId + ".flac");
}

void MariaProcessor::analyzerTick()
{
    if (pendingSave.exchange (false))
    {
        auto f = sessionMemoryFile();
        f.getParentDirectory().createDirectory();
        if (saveMemory (f, saveBase.load()))
        {
            f.withFileExtension ("wav").deleteFile();          // ancien format
            recoveryAudioFile().deleteFile(); recoveryStateFile().deleteFile();   // le set est a jour : le secours ne sert plus
        }
    }
    if (pendingRecoverySave.exchange (false)) saveMemory (recoveryAudioFile(), recoveryBase.load());
    juce::File f, cf;
    bool session = false;
    {
        const juce::ScopedLock sl (loadLock);
        f = pendingLoad; pendingLoad = juce::File();
        session = pendingLoadSession; pendingLoadSession = false;
        cf = pendingCorpus; pendingCorpus = juce::File();
    }
    if (f.existsAsFile()) loadFileNow (f, session);
    if (pendingCorpusClear.exchange (false))
    {
        corpusReady.store (false);
        juce::Thread::sleep (40);
        if (corpus.isPrepared()) corpus.requestClear();
        corpus.analyzeStep (sourceProfile (kNumSources - 1), -50.0f);
    }
    if (cf.existsAsFile()) loadCorpusNow (cf);
    mem.analyzeStep (sourceProfile (sourceIndex()), gateDb());
    listener.analyze (sourceProfile (sourceIndex()), gateDb());
    pulse.analyze();
    midiLearnStep();
}

void MariaProcessor::analyzeNow()
{
    mem.analyzeStep (sourceProfile (sourceIndex()), gateDb());
    listener.analyze (sourceProfile (sourceIndex()), gateDb());
    pulse.analyze();
    midiLearnStep();
}

void MariaProcessor::requestLoadFile (const juce::File& f, bool sessionRestore)
{
    const juce::ScopedLock sl (loadLock);
    pendingLoad = f;
    pendingLoadSession = sessionRestore;
}

// Charge un fichier audio dans une memoire (resample, mono). raw = garde tout (memoire de session : positions conservees)
bool MariaProcessor::loadAudioInto (Memory& dst, const juce::File& f, const SourceProfile& prof, float gDb, bool raw, int64_t* startPos, double* ratioOut)
{
    if (! dst.isPrepared()) return false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return false;
    const double ratio = rd->sampleRate / dst.sampleRate();
    if (ratioOut != nullptr) *ratioOut = ratio;
    const int64_t outLen = juce::jmin ((int64_t) ((double) rd->lengthInSamples / ratio), dst.capacity() - (int64_t) dst.sampleRate() * 2);
    OnsetDetector d;
    d.setup (prof, pSens->load(), dst.sampleRate());
    const int chunk = 4096;
    juce::AudioBuffer<float> in ((int) rd->numChannels, (int) (chunk * ratio) + 8);
    if (startPos != nullptr) *startPos = dst.writePos();
    dst.pushEvent (dst.writePos(), 1);
    const float gate = juce::Decibels::decibelsToGain (gDb);
    float env = 0; const float rel = std::exp (-1.0f / (0.12f * (float) dst.sampleRate()));
    int hold = 0; bool wasW = true;
    std::vector<float> keep; keep.reserve ((size_t) chunk);
    for (int64_t o = 0; o < outLen; o += chunk)
    {
        const int nOut = (int) juce::jmin ((int64_t) chunk, outLen - o);
        const int64_t srcStart = (int64_t) ((double) o * ratio);
        const int nIn = (int) ((double) nOut * ratio) + 2;
        in.setSize ((int) rd->numChannels, nIn, false, false, true);
        rd->read (&in, 0, nIn, srcStart, true, true);
        keep.clear();
        for (int i = 0; i < nOut; ++i)
        {
            const double sp = (double) i * ratio;
            const int i0 = (int) sp; const float fr = (float) (sp - i0);
            float x = 0;
            for (int ch = 0; ch < in.getNumChannels(); ++ch)
            {
                const float a = in.getSample (ch, juce::jmin (i0, nIn - 1)), b = in.getSample (ch, juce::jmin (i0 + 1, nIn - 1));
                x += a + fr * (b - a);
            }
            x /= (float) juce::jmax (1, in.getNumChannels());
            env = std::abs (x) > env ? std::abs (x) : env * rel;
            if (env > gate) hold = (int) (0.35 * dst.sampleRate()); else if (hold > 0) --hold;
            if (raw || hold > 0)
            {
                if (! wasW) dst.pushEvent (dst.writePos() + (int64_t) keep.size(), 1);
                wasW = true;
                if (d.process (x) > 0.0f) dst.pushEvent (dst.writePos() + (int64_t) keep.size(), 0);
                keep.push_back (x);
            }
            else wasW = false;
        }
        if (! keep.empty()) dst.write (keep.data(), (int) keep.size());
        dst.analyzeStep (prof, gDb);
    }
    // termine l'analyse du fichier
    const float zeros[1024] {};
    for (int z = 0; z < 4; ++z) dst.write (zeros, 1024);
    dst.analyzeStep (prof, gDb);
    return true;
}

bool MariaProcessor::loadFileNow (const juce::File& f, bool sessionRestore)
{
    if (! mem.isPrepared()) return false;
    loading.store (true);
    juce::Thread::sleep (60);   // laisse le thread audio constater la pause d'apprentissage
    if (pendingReplaceLoad.exchange (false)) { mem.requestClear(); mem.analyzeStep (sourceProfile (sourceIndex()), gateDb()); }
    int64_t w0 = 0; double ratio = 1.0;
    const bool ok = loadAudioInto (mem, f, sourceProfile (sourceIndex()), gateDb(), sessionRestore, &w0, &ratio);
    if (ok && sessionRestore && pendingMotifRestore)
    {
        // les leitmotivs du set retrouvent leur place dans la memoire rechargee
        for (int k = 0; k < kNumMotifs; ++k)
        {
            const auto [oa, ob] = pendingMotifOff[(size_t) k];
            if (oa < 0 || ob <= oa) continue;
            const int64_t a = w0 + (int64_t) ((double) oa / ratio), b = w0 + (int64_t) ((double) ob / ratio);
            if (b <= mem.writePos()) { motA[(size_t) k].store (a); motB[(size_t) k].store (b); }
        }
    }
    pendingMotifRestore = false;
    if (ok && sessionRestore && pendingChapRestore)
    {
        // les chapitres retrouvent leurs places
        regCount.store (0);
        for (const auto& [off, ch] : pendingChapOff)
            appendRegion (w0 + juce::jmax ((int64_t) 0, (int64_t) ((double) off / ratio)), ch);
        chapReappend.store (true);
        lastClearSeen = mem.clearCount.load();
    }
    pendingChapRestore = false;
    loading.store (false);
    return ok;
}

// ---------------------------------------------------------------------------
// Corpus externe
void MariaProcessor::requestCorpusLoad (const juce::File& f)
{
    const juce::ScopedLock sl (loadLock);
    pendingCorpus = f;
}

void MariaProcessor::clearCorpus()
{
    {
        const juce::ScopedLock sl (loadLock);
        corpusFile = juce::File(); pendingCorpus = juce::File();
    }
    corpusReady.store (false);
    pendingCorpusClear.store (true);
}

bool MariaProcessor::loadCorpusNow (const juce::File& f)
{
    if (! f.existsAsFile()) return false;
    corpusLoading.store (true);
    corpusReady.store (false);
    juce::Thread::sleep (80);       // les voix qui lisaient le corpus s'eteignent
    const double sr = mem.isPrepared() ? mem.sampleRate() : (fs > 0.0 ? fs : 48000.0);
    if (! corpus.isPrepared() || std::abs (corpus.sampleRate() - sr) > 1.0) corpus.prepare (sr, 360.0);
    else { corpus.requestClear(); corpus.analyzeStep (sourceProfile (kNumSources - 1), -50.0f); }
    const auto& prof = sourceProfile (juce::jlimit (0, kNumSources - 1, (int) std::round (pCorpusSrc->load())));
    const bool ok = loadAudioInto (corpus, f, prof, -50.0f, false, nullptr, nullptr);
    if (ok)
    {
        { const juce::ScopedLock sl (loadLock); corpusFile = f; }
        corpusFs = sr;
        corpusReady.store (corpus.numStates() > 4, std::memory_order_release);
    }
    corpusLoading.store (false);
    return ok && corpusReady.load();
}

double MariaProcessor::corpusSeconds() const
{
    if (! corpusReady.load() || ! corpus.isPrepared()) return 0.0;
    return (double) (corpus.writePos() - corpus.oldestValidPos()) / corpus.sampleRate();
}



// ---------------------------------------------------------------------------
// Mode Machine, nappes
double MariaProcessor::machineBeats() const
{
    static const double b[5] { 1.0, 2.0, 4.0, 8.0, 16.0 };
    return b[juce::jlimit (0, 4, (int) std::round (pMachLen->load()))];
}

void MariaProcessor::pickDroneFragment (bool first)
{
    // memoire : le direct, ou le corpus selon le melange
    Memory* m = &mem;
    if (Memory* c = corpusMem()) if (c->numStates() > 4 && (mem.numStates() < 5 || hUni (droneSeed, 0xD70u, 3u) < pCorpusMix->load())) m = c;
    const int n = m->numStates(), fv = juce::jmax (1, m->firstValidState());
    if (n - fv < 2) return;
    const auto& L = listener.state;
    const bool harm = L.harmValid.load();
    float ch[12];
    for (int i = 0; i < 12; ++i) ch[i] = L.chroma[i].load();
    // chapitres : meme regle que l'improvisatrice
    int64_t cs[64], ce[64];
    const int nr = m == &mem ? chapterRanges (cs, ce, 64) : -1;
    int pickS = -1; float total = 0.0f;
    for (int k = 0; k < 48; ++k)
    {
        ++droneSeed;
        const int st = fv + juce::jmin (n - 1 - fv, (int) (hUni (droneSeed, 0xD71u, 7u) * (float) (n - fv)));
        if (! m->stateValid (st)) continue;
        const auto& g = m->segment (st);
        const double len = (double) (g.end - g.start) / m->sampleRate();
        if (len < 0.1 || g.rmsDb < -48.0f) continue;
        if (nr > 0)
        {
            bool ok = false;
            for (int r = 0; r < nr && ! ok; ++r) ok = g.start >= cs[r] && g.start < ce[r];
            if (! ok) continue;
        }
        float w = (float) juce::jmin (1.0, len) * (g.pitch > 0.0f ? 1.5f : 0.6f);
        if (harm && g.pitch > 0.0f)
        {
            const float f = ch[((int) std::round (g.pitch) % 12 + 12) % 12];
            w *= 0.005f + 3.0f * f * f * f * f;
        }
        total += w;
        if (hUni (droneSeed, 0xD72u, 11u) * total < w) pickS = st;
    }
    if (pickS < 0) return;
    const auto& g = m->segment (pickS);
    const double len = (double) (g.end - g.start);
    const double win = juce::jmin (0.35 * m->sampleRate(), len * 0.6);
    const double a = (double) g.start + (len - win) * 0.5;
    const float gain = juce::jlimit (0.1f, 6.0f, juce::Decibels::decibelsToGain (-20.0f - g.rmsDb));
    drone.setNext (m, a, win, gain, first ? 2.0 : juce::jmax (1.0, pDroneTime->load() * 0.5), droneSeed * 2654435761u);
    droneStateView.store (pickS);
    droneFromCorpus.store (m != &mem);
}

// ---------------------------------------------------------------------------
// Fichiers de session, secours, nettoyage
juce::File MariaProcessor::sessionsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Maria Memoires").getChildFile ("Sessions");
}
juce::File MariaProcessor::recoveryAudioFile() const { return sessionsFolder().getChildFile (memId + "_secours.flac"); }
juce::File MariaProcessor::recoveryStateFile() const { return sessionsFolder().getChildFile (memId + "_secours.xml"); }

void MariaProcessor::sessionsFolderInfo (int& files, int64_t& bytes)
{
    files = 0; bytes = 0;
    for (const auto& f : sessionsFolder().findChildFiles (juce::File::findFiles, false, "*"))
        if (f.hasFileExtension ("wav;flac;xml")) { ++files; bytes += f.getSize(); }
}

int MariaProcessor::cleanupSessions (int days, const juce::String& keepId) { return cleanupFolder (sessionsFolder(), days, keepId); }

int MariaProcessor::cleanupFolder (const juce::File& dir, int days, const juce::String& keepId)
{
    int n = 0;
    const auto limit = juce::Time::getCurrentTime() - juce::RelativeTime::days (days);
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*"))
    {
        if (! f.hasFileExtension ("wav;flac;xml")) continue;
        if (keepId.isNotEmpty() && f.getFileName().startsWith (keepId)) continue;
        if (f.getLastModificationTime() < limit && (f.moveToTrash() || f.deleteFile())) ++n;
    }
    return n;
}

void MariaProcessor::autosaveNow()
{
    if (! mem.isPrepared() || mem.numStates() <= 1 || loading.load()) return;
    sessionsFolder().createDirectory();
    if (auto xml = captureState().createXml()) xml->writeTo (recoveryStateFile());
    recoveryBase.store (lastCaptureBase);
    lastAutosaveW = mem.writePos();
    lastAutosave = juce::Time::getCurrentTime();
#if MA_HEADLESS_TEST
    saveMemory (recoveryAudioFile(), recoveryBase.load());
#else
    pendingRecoverySave.store (true);
#endif
}

void MariaProcessor::acceptRecovery()
{
    recoveryAvailable.store (false);
    const auto af = recoveryAudioFile();
    auto xml = juce::XmlDocument::parse (recoveryStateFile());
    if (! af.existsAsFile() || xml == nullptr) return;
    const auto t = juce::ValueTree::fromXml (*xml);
    const auto ex = t.getChildWithName ("EXTRA");
    for (auto& o : pendingMotifOff) o = { -1, -1 };
    for (auto& tok : juce::StringArray::fromTokens (ex.getProperty ("motifs").toString(), " ", ""))
    {
        auto parts = juce::StringArray::fromTokens (tok, ":", "");
        if (parts.size() != 3) continue;
        const int k = parts[0].getIntValue();
        if (k >= 0 && k < kNumMotifs) pendingMotifOff[(size_t) k] = { parts[1].getLargeIntValue(), parts[2].getLargeIntValue() };
    }
    pendingMotifRestore = true;
    pendingChapOff.clear();
    for (auto& tok : juce::StringArray::fromTokens (ex.getProperty ("chapters").toString(), " ", ""))
        if (tok.contains (":")) pendingChapOff.push_back ({ tok.fromFirstOccurrenceOf (":", false, false).getLargeIntValue(), tok.upToFirstOccurrenceOf (":", false, false).getIntValue() });
    pendingChapRestore = ! pendingChapOff.empty();
    pendingReplaceLoad.store (true);
    requestLoadFile (af, true);
}

// ---------------------------------------------------------------------------
// Retour d'etat MIDI
void MariaProcessor::setFeedbackDevice (const juce::String& id)
{
    { const juce::ScopedLock sl (fbLock); fbDevId = id; }
    fbDevChanged.store (true);
    fbResend.store (true);
}

void MariaProcessor::openFeedbackDevice()
{
    // thread message
    fbOutReady.store (false);
    if (fbOut != nullptr) { fbOut->stopThread (300); fbOut.reset(); }
    const juce::String id = feedbackDevice();
#if ! MA_HEADLESS_TEST
    if (id.isEmpty()) return;
    auto v = std::make_unique<VirtualMidi>();
    v->out = juce::MidiOutput::openDevice (id);
    if (v->out == nullptr) return;
    v->startThread (juce::Thread::Priority::high);
    fbOut = std::move (v);
    fbOutReady.store (true);
#endif
}

void MariaProcessor::fbSend (juce::uint8 status, int d1, int d2, int sample)
{
    const juce::uint8 a = (juce::uint8) juce::jlimit (0, 127, d1), b = (juce::uint8) juce::jlimit (0, 127, d2);
    feedbackSent.fetch_add (1, std::memory_order_relaxed);
    const MidiEv e { status, a, b, blockStartMs + (double) sample / fs * 1000.0 };
    if (fbOutReady.load (std::memory_order_acquire)) { fbOut->push (e); return; }
    if (outMidi != nullptr) outMidi->addEvent (juce::MidiMessage (status, a, b), juce::jmax (0, sample));
    if (vmidiReady.load (std::memory_order_acquire)) vmidi->push (e);
}

void MariaProcessor::sendFeedback (int sample)
{
    const int chSel = (int) std::round (pFbCh->load());
    if (chSel <= 0) { fbResend.store (true); return; }
    const juce::uint8 nOn = (juce::uint8) (0x90 | (chSel - 1)), cc = (juce::uint8) (0xB0 | (chSel - 1));
    if (fbResend.exchange (false)) { for (auto& x : fbNote) x = -1; for (auto& x : fbCC) x = -1; }
    auto note = [&] (int n, bool lit) { const int v = lit ? 127 : 0; if (fbNote[n] != v) { fbNote[n] = v; fbSend (nOn, n, v, sample); } };
    auto ctl = [&] (int c, int v) { if (fbCC[c] != v) { fbCC[c] = v; fbSend (cc, c, v, sample); } };
    const int sc = lastScene.load();
    for (int k = 0; k < kSceneBank; ++k) note (36 + k, sc == sceneBank.load() * kSceneBank + k);
    ctl (23, juce::jlimit (0, 127, sc + 1));
    note (46, pFreeze->load() > 0.5f);
    note (47, panicPhase != 0);
    note (48, impro.isPlaying());
    note (49, ! impro.isPlaying() && ! impro2.isPlaying());
    note (50, pLearn->load() > 0.5f);
    for (int k = 0; k < kNumMotifs; ++k)
    {
        note (52 + k, motifSeconds (k) > 0.0f);
        note (56 + k, impro.motifPlaying() && motifLastPlayed.load() == k);
    }
    const float goal = pArcGoal->load();
    note (61, goal > 0.51f); note (62, goal < 0.49f); note (63, goal >= 0.49f && goal <= 0.51f);
    ctl (20, currentChapter());
    ctl (21, (int) std::round (arcView.load() * 127.0f));
    ctl (22, pulse.validView.load() ? juce::jlimit (1, 127, (int) std::round (pulse.bpmView.load()) - 40) : 0);
}

// ---------------------------------------------------------------------------
// Chapitres
void MariaProcessor::setParamFromAnyThread (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void MariaProcessor::appendRegion (int64_t pos, int chap)
{
    int c = regCount.load();
    if (c > 0 && regStart[(size_t) (c - 1)].load() >= pos) { regChap[(size_t) (c - 1)].store (chap); return; }   // rien appris depuis : on renomme
    if (c > 0 && regChap[(size_t) (c - 1)].load() == chap) return;
    if (c >= kMaxRegions)
    {
        // plein : on oublie les plus anciennes
        for (int i = 0; i < kMaxRegions - 64; ++i) { regStart[(size_t) i].store (regStart[(size_t) (i + 64)].load()); regChap[(size_t) i].store (regChap[(size_t) (i + 64)].load()); }
        c = kMaxRegions - 64;
    }
    regStart[(size_t) c].store (pos); regChap[(size_t) c].store (chap);
    regCount.store (c + 1);
}

int MariaProcessor::chapterAt (int64_t pos) const
{
    const int c = regCount.load();
    int r = 0;
    for (int i = 0; i < c; ++i) { if (regStart[(size_t) i].load() <= pos) r = regChap[(size_t) i].load(); else break; }
    return c > 0 && r == 0 ? regChap[0].load() : r;
}

double MariaProcessor::chapterSeconds (int chap) const
{
    const int c = regCount.load();
    const int64_t lo = mem.oldestValidPos(), w = mem.writePos();
    int64_t tot = 0;
    for (int i = 0; i < c; ++i)
    {
        if (regChap[(size_t) i].load() != chap) continue;
        const int64_t a = juce::jmax (lo, regStart[(size_t) i].load()), b = i + 1 < c ? regStart[(size_t) (i + 1)].load() : w;
        if (b > a) tot += b - a;
    }
    return (double) tot / mem.sampleRate();
}

int MariaProcessor::chapterRanges (int64_t* starts, int64_t* ends, int maxRanges) const
{
    const int mode = (int) std::round (pChapPlay->load());
    if (mode == 0) return -1;
    const int cur = currentChapter(), pickC = (int) std::round (pChapPick->load()) + 1;
    const int c = regCount.load();
    int k = 0;
    for (int i = 0; i < c && k < maxRanges; ++i)
    {
        const int ch = regChap[(size_t) i].load();
        const bool ok = mode == 1 ? ch == cur : (mode == 2 ? ch == pickC : ch != cur);
        if (! ok) continue;
        const int64_t a = regStart[(size_t) i].load(), b = i + 1 < c ? regStart[(size_t) (i + 1)].load() : INT64_MAX;
        if (k > 0 && ends[k - 1] == a) { ends[k - 1] = b; continue; }
        starts[k] = a; ends[k] = b; ++k;
    }
    return k;
}

// ---------------------------------------------------------------------------
// Apprentissage MIDI (thread d'analyse)
void MariaProcessor::closeMidiSegment (int64_t end)
{
    if (! lrn.pend || lrn.nn <= 0) { lrn.pend = false; return; }
    Segment g;
    g.start = lrn.start;
    g.end = juce::jmax (end, lrn.start + (int64_t) (0.03 * fs));
    g.nn = (uint8_t) lrn.nn;
    std::sort (lrn.notes, lrn.notes + lrn.nn);
    for (int q = 0; q < lrn.nn; ++q) g.notes[q] = lrn.notes[q];
    g.vel = (uint8_t) juce::jlimit (1, 127, lrn.vel);
    g.pitch = (float) g.notes[0];
    g.rmsDb = -50.0f + (float) g.vel / 127.0f * 44.0f;
    int mask = 0;
    for (int q = 0; q < lrn.nn; ++q) mask |= 1 << (g.notes[q] % 12);
    for (int r = 0; r < 12 && g.chord < 0; ++r)
    {
        const int maj = (1 << r) | (1 << ((r + 4) % 12)) | (1 << ((r + 7) % 12)), mnr = (1 << r) | (1 << ((r + 3) % 12)) | (1 << ((r + 7) % 12));
        if ((mask & maj) == maj) g.chord = r; else if ((mask & mnr) == mnr) g.chord = 12 + r;
    }
    g.sym = lrn.nn == 1 ? (int) g.notes[0] : (g.chord >= 0 ? 2000 + g.chord : 3000 + mask);
    midiMem.addSegment (g);
    midiLearned.fetch_add (lrn.nn);
    lrn.pend = false;
}

void MariaProcessor::midiLearnStep()
{
    if (! midiMem.isPrepared()) return;
    if (pendingMidiClear.exchange (false))
    {
        midiMem.clearDirect();
        lrn = Learner();
        midiLearned.store (0);
        int s1, n1, s2, n2;
        kbFifo.prepareToRead (kbFifo.getNumReady(), s1, n1, s2, n2);
        kbFifo.finishedRead (n1 + n2);
    }
    {
        // memoire MIDI du set
        std::vector<Segment> rs;
        bool doRestore = false;
        {
            const juce::ScopedLock sl (loadLock);
            if (pendingMidiRestoreFlag) { rs.swap (pendingMidiRestore); doRestore = true; pendingMidiRestoreFlag = false; }
        }
        if (doRestore && ! rs.empty())
        {
            midiMem.clearDirect();
            lrn = Learner();
            const int64_t w0 = midiMem.writePos();
            int64_t last = 0; int notes = 0;
            for (auto g : rs)
            {
                g.start += w0; g.end += w0;
                midiMem.addSegment (g);
                last = juce::jmax (last, g.end); notes += g.nn;
            }
            midiMem.advance ((int) juce::jmin ((int64_t) INT32_MAX, last - w0 + (int64_t) fs));
            midiLearned.store (notes);
        }
    }
    const int64_t thr = (int64_t) (0.03 * fs), maxLen = (int64_t) (2.5 * fs);
    int s1, n1, s2, n2;
    kbFifo.prepareToRead (kbFifo.getNumReady(), s1, n1, s2, n2);
    auto handle = [&] (const KbEv& e)
    {
        lrn.lastEv = e.pos;
        if (e.vel > 0)
        {
            if (lrn.pend && e.pos - lrn.start <= thr && lrn.nn < 6)
            {
                bool dup = false;
                for (int q = 0; q < lrn.nn; ++q) if (lrn.notes[q] == e.note) dup = true;
                if (! dup) lrn.notes[lrn.nn++] = (uint8_t) e.note;
                lrn.vel = juce::jmax (lrn.vel, e.vel);
            }
            else
            {
                if (lrn.pend) closeMidiSegment (juce::jmin (e.pos, lrn.start + maxLen));
                lrn.pend = true; lrn.start = e.pos; lrn.nn = 1; lrn.notes[0] = (uint8_t) e.note; lrn.vel = e.vel; lrn.maxDur = 0;
            }
            lrn.onPos[e.note & 127] = e.pos; ++lrn.held;
        }
        else
        {
            lrn.maxDur = juce::jmax (lrn.maxDur, e.pos - lrn.onPos[e.note & 127]);
            lrn.held = juce::jmax (0, lrn.held - 1);
        }
    };
    for (int i = 0; i < n1; ++i) handle (kbEv[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i) handle (kbEv[(size_t) (s2 + i)]);
    kbFifo.finishedRead (n1 + n2);
    // dernier evenement : ferme apres un silence du clavier
    const int64_t wp = midiMem.writePos();
    if (lrn.pend && ((lrn.held == 0 && (wp - lrn.start > maxLen || wp - lrn.lastEv > (int64_t) (0.8 * fs))) || wp - lrn.start > (int64_t) (12.0 * fs)))
        closeMidiSegment (lrn.start + juce::jlimit ((int64_t) (0.08 * fs), maxLen, juce::jmax (lrn.maxDur, wp - lrn.start - (int64_t) (0.8 * fs))));
}

// ---------------------------------------------------------------------------
// Leitmotivs
void MariaProcessor::handleMotifRequests()
{
    const int mk = pendingMark.exchange (0), pl = pendingPlay.exchange (0);
    if (mk == 0 && pl == 0) return;
    for (int k = 0; k < kNumMotifs; ++k)
    {
        if ((mk >> k) & 1)
        {
            // la phrase en cours, sinon la derniere phrase jouee (20 s au plus)
            int64_t a = phOpen ? phStart : lastPhA, b = phOpen ? mem.writePos() : lastPhB;
            if (a >= 0 && b - a >= (int64_t) (0.25 * fs))
            {
                a = juce::jmax (a, b - (int64_t) (20.0 * fs), mem.oldestValidPos());
                if (b > a) { motA[(size_t) k].store (a); motB[(size_t) k].store (b); }
            }
        }
        if ((pl >> k) & 1)
        {
            int lo = 0, hi = 0;
            if (motifStates (k, lo, hi) > 0) { impro.playMotif (lo, hi); motifLastPlayed.store (k); }
        }
    }
}

int MariaProcessor::motifStates (int k, int& lo, int& hi) const
{
    const int64_t a = motA[(size_t) k].load(), b = motB[(size_t) k].load();
    if (a < 0 || b <= a || a < mem.oldestValidPos()) return 0;
    lo = mem.stateAtOrAfter (a);
    hi = mem.stateAtOrAfter (b) - 1;
    if (lo < 1 || hi < lo || ! mem.stateValid (lo) || ! mem.stateValid (hi)) return 0;
    return hi - lo + 1;
}

float MariaProcessor::motifSeconds (int k) const
{
    const int64_t a = motA[(size_t) k].load(), b = motB[(size_t) k].load();
    if (a < 0 || b <= a || a < mem.oldestValidPos()) return 0.0f;
    return (float) ((double) (b - a) / mem.sampleRate());
}

bool MariaProcessor::saveMemory (const juce::File& f, int64_t from)
{
    if (! mem.isPrepared()) return false;
    const int64_t a = juce::jmax (mem.oldestValidPos(), from), b = mem.writePos();
    if (b <= a) return false;
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    juce::FlacAudioFormat flac;
    juce::AudioFormat& fmt = f.hasFileExtension ("flac") ? (juce::AudioFormat&) flac : (juce::AudioFormat&) wav;
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), mem.sampleRate(), 1, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    juce::AudioBuffer<float> blk (1, 8192);
    for (int64_t p = a; p < b; p += 8192)
    {
        const int nn = (int) juce::jmin ((int64_t) 8192, b - p);
        for (int i = 0; i < nn; ++i) blk.setSample (0, i, mem.sampleAt (p + i));
        w->writeFromAudioSampleBuffer (blk, 0, nn);
    }
    return true;
}

// ===========================================================================
// Etat, undo
juce::ValueTree MariaProcessor::captureState()
{
    juce::ValueTree root ("MARIA_STATE");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    juce::ValueTree ex ("EXTRA");
    juce::String locks, slocks, cc;
    for (int s = 0; s < kNumSlots; ++s)
    {
        slocks << (slotLock[s].load() ? '1' : '0');
        for (int p = 0; p < kNumSP; ++p) locks << (lockP[s][p].load() ? '1' : '0');
    }
    for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() >= 0) cc << c << ":" << ccMap[(size_t) c].load() << " ";
    ex.setProperty ("locks", locks, nullptr);
    juce::String il;
    for (int k = 0; k < kNumIm; ++k) il << (lockIm[k].load() ? '1' : '0');
    ex.setProperty ("imlocks", il, nullptr);
    ex.setProperty ("slotlocks", slocks, nullptr);
    ex.setProperty ("cc", cc.trim(), nullptr);
    ex.setProperty ("program", currentProgram, nullptr);
    ex.setProperty ("ui", uiScale.load(), nullptr);
    ex.setProperty ("memid", memId, nullptr);
    {
        // leitmotivs : positions relatives au debut de la memoire sauvee
        const int64_t base = mem.isPrepared() ? mem.oldestValidPos() : 0;
        lastCaptureBase = base;
        juce::String mt;
        for (int k = 0; k < kNumMotifs; ++k)
        {
            const int64_t a = motA[(size_t) k].load(), b = motB[(size_t) k].load();
            if (a >= base && b > a) mt << k << ":" << (a - base) << ":" << (b - base) << " ";
        }
        ex.setProperty ("motifs", mt.trim(), nullptr);
        juce::String chs;
        const int rc = regCount.load();
        for (int i = 0; i < rc; ++i)
        {
            const int64_t a = regStart[(size_t) i].load(), b = i + 1 < rc ? regStart[(size_t) (i + 1)].load() : INT64_MAX;
            if (b <= base) continue;
            chs << regChap[(size_t) i].load() << ":" << juce::jmax ((int64_t) 0, a - base) << " ";
        }
        ex.setProperty ("chapters", chs.trim(), nullptr);
        // memoire MIDI (petite) : gardee dans le set
        if (midiMem.isPrepared())
        {
            juce::String mm;
            const int n = midiMem.numStates();
            const int first = juce::jmax (midiMem.firstValidState(), n - 4000);
            const int64_t mb = first < n ? midiMem.segment (first).start : 0;
            for (int st = first; st < n; ++st)
            {
                const auto& g = midiMem.segment (st);
                if (g.nn == 0) continue;
                mm << (g.start - mb) << ":" << (g.end - g.start) << ":" << (int) g.vel << ":";
                for (int q = 0; q < g.nn; ++q) mm << (q ? "." : "") << (int) g.notes[q];
                mm << " ";
            }
            ex.setProperty ("midimem", mm.trim(), nullptr);
            ex.setProperty ("midifs", midiMem.sampleRate(), nullptr);
        }
        ex.setProperty ("corpus", corpusPath().getFullPathName(), nullptr);
        ex.setProperty ("fbdev", feedbackDevice(), nullptr);
    }
    for (int s = 0; s < kNumScenes; ++s)
    {
        if (! scenes[(size_t) s].stored) continue;
        juce::ValueTree sc ("SCENE");
        sc.setProperty ("i", s, nullptr);
        juce::String v;
        for (float x : scenes[(size_t) s].v) v << juce::String (x, 5) << " ";
        sc.setProperty ("v", v.trim(), nullptr);
        ex.appendChild (sc, nullptr);
    }
    root.appendChild (ex, nullptr);
    return root;
}

void MariaProcessor::restoreState (const juce::ValueTree& root)
{
    const auto st = root.getChildWithName (apvts.state.getType());
    if (st.isValid()) apvts.replaceState (st);
    const auto ex = root.getChildWithName ("EXTRA");
    if (! ex.isValid()) return;
    const juce::String locks = ex.getProperty ("locks").toString(), slocks = ex.getProperty ("slotlocks").toString();
    for (int s = 0; s < kNumSlots; ++s)
    {
        slotLock[s].store (slocks.length() > s && slocks[s] == '1');
        for (int p = 0; p < kNumSP; ++p)
        {
            const int i = s * kNumSP + p;
            lockP[s][p].store (locks.length() > i && locks[i] == '1');
        }
    }
    {
        const juce::String il = ex.getProperty ("imlocks").toString();
        for (int k = 0; k < kNumIm; ++k) lockIm[k].store (il.length() > k ? il[k] == '1' : k == ILevel);
    }
    for (auto& c : ccMap) c.store (-1);
    for (auto& tok : juce::StringArray::fromTokens (ex.getProperty ("cc").toString(), " ", ""))
    {
        const int cc = tok.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int pi = tok.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (cc >= 0 && cc < 128 && tok.contains (":")) ccMap[(size_t) cc].store (pi);
    }
    currentProgram = (int) ex.getProperty ("program", 0);
    if (ex.hasProperty ("ui")) uiScale.store ((float) ex.getProperty ("ui"));
    if (ex.hasProperty ("memid"))
    {
        const juce::String id = ex.getProperty ("memid").toString();
        auto file = sessionsFolder().getChildFile (id + ".flac");
        if (! file.existsAsFile()) file = sessionsFolder().getChildFile (id + ".wav");     // ancien format
        {
            // un secours plus recent que le set ? (plantage, set non sauve)
            const auto rec = sessionsFolder().getChildFile (id + "_secours.flac");
            if (rec.existsAsFile() && sessionsFolder().getChildFile (id + "_secours.xml").existsAsFile()
                && (! file.existsAsFile() || rec.getLastModificationTime() >= file.getLastModificationTime()) && id != memId)   // le secours est efface a chaque sauvegarde du set
            {
                recoveryInfo = rec.getLastModificationTime().formatted ("%d/%m %H:%M") + ", " + juce::String ((double) rec.getSize() / 1.0e6, 1) + " Mo";
                recoveryAvailable.store (true);
            }
        }
        if (id != memId)
        {
            if (! idInUse (id)) { unregisterId (memId); memId = id; registerId (memId); }   // sinon (piste dupliquee) on garde un identifiant propre
            if (pAutosave->load() > 0.5f && mem.numStates() <= 1 && file.existsAsFile())
            {
                for (auto& o : pendingMotifOff) o = { -1, -1 };
                for (auto& tok : juce::StringArray::fromTokens (ex.getProperty ("motifs").toString(), " ", ""))
                {
                    auto parts = juce::StringArray::fromTokens (tok, ":", "");
                    if (parts.size() != 3) continue;
                    const int k = parts[0].getIntValue();
                    if (k >= 0 && k < kNumMotifs) pendingMotifOff[(size_t) k] = { parts[1].getLargeIntValue(), parts[2].getLargeIntValue() };
                }
                pendingMotifRestore = true;
                pendingChapOff.clear();
                for (auto& tok : juce::StringArray::fromTokens (ex.getProperty ("chapters").toString(), " ", ""))
                    if (tok.contains (":")) pendingChapOff.push_back ({ tok.fromFirstOccurrenceOf (":", false, false).getLargeIntValue(), tok.upToFirstOccurrenceOf (":", false, false).getIntValue() });
                pendingChapRestore = ! pendingChapOff.empty();
                requestLoadFile (file, true);
                savedWrite = -2;   // la memoire chargee devra etre resauvee sous notre identifiant
            }
        }
    }
    for (auto& s : scenes) s.stored = false;
    for (const auto& sc : ex)
    {
        if (! sc.hasType ("SCENE")) continue;
        const int i = (int) sc.getProperty ("i", -1);
        if (i < 0 || i >= kNumScenes) continue;
        auto toks = juce::StringArray::fromTokens (sc.getProperty ("v").toString(), " ", "");
        auto& S = scenes[(size_t) i];
        S.v.assign (sceneParams.size(), 0.0f);
        for (size_t k = 0; k < sceneParams.size(); ++k)
            S.v[k] = (int) k < toks.size() ? toks[(int) k].getFloatValue() : sceneParams[k]->getValue();
        S.stored = true;
    }
}

void MariaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = captureState().createXml()) copyXmlToBinary (*xml, dest);
    // memoire liee au set : sauvee en tache de fond si elle a change
    if (pAutosave->load() > 0.5f && mem.isPrepared() && ! loading.load())
    {
        const int64_t w = mem.writePos(); const int cc = mem.clearCount.load();
        if ((w != savedWrite || cc != savedClear) && mem.numStates() > 1)
        {
            savedWrite = w; savedClear = cc;
            saveBase.store (lastCaptureBase);
            pendingSave.store (true);
        }
    }
}

void MariaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        const auto t = juce::ValueTree::fromXml (*xml);
        if (t.hasType ("GROT_STATE") || t.hasType ("MARIA_STATE"))
        {
            restoreState (t);
            // corpus externe du set
            const auto ex = t.getChildWithName ("EXTRA");
            const juce::File cf (ex.isValid() && ex.getProperty ("corpus").toString().isNotEmpty() ? juce::File (ex.getProperty ("corpus").toString()) : juce::File());
            if (cf.existsAsFile() && cf != corpusPath()) requestCorpusLoad (cf);
            if (ex.isValid() && ex.getProperty ("fbdev").toString() != feedbackDevice()) setFeedbackDevice (ex.getProperty ("fbdev").toString());
            // memoire MIDI du set
            if (ex.isValid() && ex.getProperty ("midimem").toString().isNotEmpty())
            {
                const double sr0 = (double) ex.getProperty ("midifs", 48000.0);
                const double k = (fs > 0 ? fs : 48000.0) / juce::jmax (1.0, sr0);
                std::vector<Segment> rs;
                for (auto& tok : juce::StringArray::fromTokens (ex.getProperty ("midimem").toString(), " ", ""))
                {
                    auto parts = juce::StringArray::fromTokens (tok, ":", "");
                    if (parts.size() != 4) continue;
                    Segment g;
                    g.start = (int64_t) ((double) parts[0].getLargeIntValue() * k);
                    g.end = g.start + juce::jmax ((int64_t) 1, (int64_t) ((double) parts[1].getLargeIntValue() * k));
                    g.vel = (uint8_t) juce::jlimit (1, 127, parts[2].getIntValue());
                    auto ns = juce::StringArray::fromTokens (parts[3], ".", "");
                    for (int q = 0; q < juce::jmin (6, ns.size()); ++q) g.notes[g.nn++] = (uint8_t) juce::jlimit (0, 127, ns[q].getIntValue());
                    if (g.nn == 0) continue;
                    g.pitch = (float) g.notes[0];
                    g.rmsDb = -50.0f + (float) g.vel / 127.0f * 44.0f;
                    int mask = 0; for (int q = 0; q < g.nn; ++q) mask |= 1 << (g.notes[q] % 12);
                    for (int r = 0; r < 12 && g.chord < 0; ++r)
                    {
                        const int maj = (1 << r) | (1 << ((r + 4) % 12)) | (1 << ((r + 7) % 12)), mnr = (1 << r) | (1 << ((r + 3) % 12)) | (1 << ((r + 7) % 12));
                        if ((mask & maj) == maj) g.chord = r; else if ((mask & mnr) == mnr) g.chord = 12 + r;
                    }
                    g.sym = g.nn == 1 ? (int) g.notes[0] : (g.chord >= 0 ? 2000 + g.chord : 3000 + mask);
                    rs.push_back (g);
                }
                const juce::ScopedLock sl (loadLock);
                pendingMidiRestore.swap (rs);
                pendingMidiRestoreFlag = true;
            }
        }
        else if (t.hasType (apvts.state.getType())) apvts.replaceState (t);
    }
}

void MariaProcessor::pushUndo()
{
    undoStack.push_back (captureState());
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
}

void MariaProcessor::undo()
{
    if (undoStack.empty()) return;
    const auto t = undoStack.back();
    undoStack.pop_back();
    restoreState (t);
}

int MariaProcessor::ccForParam (int idx) const
{
    if (idx < 0) return -1;
    for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() == idx) return c;
    return -1;
}
void MariaProcessor::clearMidiMapping (int idx)
{
    for (auto& c : ccMap) if (c.load() == idx) c.store (-1);
}

void MariaProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}
void MariaProcessor::setParamReal (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// ===========================================================================
// De, deplacement
void MariaProcessor::applyDice (int s, bool types, uint32_t salt)
{
    const float ch = pChaos->load();
    const float amt = 0.3f + 0.7f * ch;
    auto rnd = [this] { return msgRng.nextFloat(); };
    juce::ignoreUnused (salt);
    if (types && ! slotLock[s].load())
    {
        int t = 1 + msgRng.nextInt (kNumTypes - 1);
        setParam (sid (s, "type"), (float) t / (float) (kNumTypes - 1));
        setParam (sid (s, "on"), 1.0f);
    }
    for (int p = 0; p < kNumSP; ++p)
    {
        if (lockP[s][p].load()) continue;
        const float cur = pVal[s][p]->load();
        const float target = p < 6 ? rnd() : (p == kChance ? (rnd() < 0.55f ? 1.0f : 0.25f + 0.75f * rnd()) : 0.3f + 0.6f * rnd());
        setParam (pid (s, p), cur + (target - cur) * (types ? 1.0f : amt));
        if (rnd() < ch * 0.5f) setParam (jid (s, p), rnd() * rnd() * ch);
    }
}

void MariaProcessor::dice (int slot, bool types)
{
    pushUndo();
    const uint32_t salt = (uint32_t) msgRng.nextInt();
    if (slot >= 0) { applyDice (slot, types, salt); return; }
    {
        const float ch = pChaos->load(), amt = 0.3f + 0.7f * ch;
        for (int k = 0; k < kNumIm; ++k)
        {
            if (lockIm[k].load()) continue;
            const float cur = pIm[k]->load();
            setParam (imId (k), cur + (msgRng.nextFloat() - cur) * amt * 0.7f);
        }
        if (types && msgRng.nextFloat() < 0.4f) setParam ("im_mode", (float) msgRng.nextInt (kNumImModes) / (float) (kNumImModes - 1));
    }
    for (int s = 0; s < kNumSlots; ++s)
    {
        if (types && ! slotLock[s].load())
        {
            // la mutation laisse environ un slot sur trois vide
            if (msgRng.nextFloat() < 0.25f && s > 0) { setParam (sid (s, "type"), 0.0f); continue; }
        }
        if (slotType (s) == 0 && ! types) continue;
        applyDice (s, types, salt);
    }
}

void MariaProcessor::moveSlot (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= kNumSlots || to >= kNumSlots) return;
    pushUndo();
    auto swapId = [this] (const juce::String& a, const juce::String& b)
    {
        auto* pa = apvts.getParameter (a); auto* pb = apvts.getParameter (b);
        const float va = pa->getValue(), vb = pb->getValue();
        pa->setValueNotifyingHost (vb); pb->setValueNotifyingHost (va);
    };
    swapId (sid (from, "type"), sid (to, "type"));
    swapId (sid (from, "on"), sid (to, "on"));
    for (int p = 0; p < kNumSP; ++p)
    {
        swapId (pid (from, p), pid (to, p));
        swapId (jid (from, p), jid (to, p));
        const bool a = lockP[from][p].load(); lockP[from][p].store (lockP[to][p].load()); lockP[to][p].store (a);
    }
    const bool a = slotLock[from].load(); slotLock[from].store (slotLock[to].load()); slotLock[to].store (a);
}

void MariaProcessor::newSeed()
{
    pushUndo();
    setParamReal ("seed", (float) msgRng.nextInt (10000));
}

// ===========================================================================
// Scenes
void MariaProcessor::buildSceneParamList()
{
    sceneParams.clear();
    for (int s = 0; s < kNumSlots; ++s)
    {
        sceneParams.push_back (apvts.getParameter (sid (s, "type")));
        sceneParams.push_back (apvts.getParameter (sid (s, "on")));
        for (int p = 0; p < kNumSP; ++p)
        {
            sceneParams.push_back (apvts.getParameter (pid (s, p)));
            sceneParams.push_back (apvts.getParameter (jid (s, p)));
        }
    }
    sceneParams.push_back (apvts.getParameter ("im_mode"));
    sceneParams.push_back (apvts.getParameter ("im_grid"));
    for (auto id : { "v2_on", "v2_mode", "v2_level", "v2_transp", "v2_pan" }) sceneParams.push_back (apvts.getParameter (id));
    for (int i = 0; i < kNumIm; ++i) { sceneParams.push_back (apvts.getParameter (imId (i))); sceneParams.push_back (apvts.getParameter (imJid (i))); }
    for (auto id : { "chaos", "clock", "rate", "glide", "evolve", "evobars", "width", "duck" })
        sceneParams.push_back (apvts.getParameter (id));
    sceneParams.push_back (apvts.getParameter ("im_tmode"));      // a la fin : les scenes deja enregistrees restent compatibles
    for (auto id : { "im_harm", "im_harmmode", "im_motvar", "corpus_mix" }) sceneParams.push_back (apvts.getParameter (id));
    for (auto id : { "chap_play", "chap_pick" }) sceneParams.push_back (apvts.getParameter (id));
    for (auto id : { "mach_len", "mach_mut", "drone_on", "drone_level", "drone_reg" }) sceneParams.push_back (apvts.getParameter (id));
    sceneParams.push_back (apvts.getParameter ("soft"));          // a la fin : compatibilite des scenes deja enregistrees
}

static bool isDiscrete (juce::RangedAudioParameter* p)
{
    return dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr || dynamic_cast<juce::AudioParameterBool*> (p) != nullptr
        || dynamic_cast<juce::AudioParameterInt*> (p) != nullptr;
}

void MariaProcessor::storeScene (int s)
{
    if (s < 0 || s >= kNumScenes) return;
    auto& S = scenes[(size_t) s];
    S.v.resize (sceneParams.size());
    for (size_t k = 0; k < sceneParams.size(); ++k) S.v[k] = sceneParams[k]->getValue();
    S.stored = true;
    lastScene.store (s);
}

void MariaProcessor::recallScene (int s, bool fromSceneParam)
{
    if (s < 0 || s >= kNumScenes || ! scenes[(size_t) s].stored) return;
    const auto& S = scenes[(size_t) s];
    morphFrom.resize (sceneParams.size()); morphTo = S.v;
    const float dur = pMorph->load();
    for (size_t k = 0; k < sceneParams.size(); ++k)
    {
        morphFrom[k] = sceneParams[k]->getValue();
        if (isDiscrete (sceneParams[k]) || dur <= 0.01f) sceneParams[k]->setValueNotifyingHost (morphTo[k]);
    }
    morphing = dur > 0.01f;
    morphStart = juce::Time::getMillisecondCounterHiRes() * 0.001;
    morphDur = dur;
    lastScene.store (s);
    sceneBank.store (s / kSceneBank);
    lastSceneParam = s + 1;
    // si le rappel vient deja du parametre Scene (automation de Live), ne pas le reecrire :
    // Live prendrait ce retour pour un geste manuel et suspendrait l'automation
    if (! fromSceneParam)
        if (auto* sp = apvts.getParameter ("scene"))
            sp->setValueNotifyingHost (sp->convertTo0to1 ((float) (s + 1)));
}

void MariaProcessor::generateSceneVariations()
{
    const int b0 = juce::jlimit (0, kNumScenes / kSceneBank - 1, sceneBank.load()) * kSceneBank;
    storeScene (b0);
    const auto base = scenes[(size_t) b0].v;
    for (int k = 1; k < kSceneBank; ++k)
    {
        auto v = base;
        const float amt = 0.12f + 0.6f * (float) k / (float) (kSceneBank - 1);
        size_t idx = 0;
        for (int s = 0; s < kNumSlots; ++s)
        {
            idx += 2;   // type, on
            for (int p = 0; p < kNumSP; ++p)
            {
                if (! lockP[s][p].load())
                {
                    const float t = p < 6 ? msgRng.nextFloat() : (p == kChance ? 0.4f + 0.6f * msgRng.nextFloat() : 0.3f + 0.6f * msgRng.nextFloat());
                    v[idx] += (t - v[idx]) * amt;
                    if (msgRng.nextFloat() < amt * 0.5f) v[idx + 1] = msgRng.nextFloat() * 0.5f;
                }
                idx += 2;
            }
        }
        auto& S = scenes[(size_t) (b0 + k)];
        S.v = v; S.stored = true;
    }
    lastScene.store (b0);
}

void MariaProcessor::adaptToSource()
{
    pushUndo();
    applySourceStarter (*this, sourceIndex());
    generateSceneVariations();
}

void MariaProcessor::morphTick()
{
    // actions demandees par le MIDI (notes)
    const int ps = pendingScene.exchange (-1);
    if (ps >= 0) recallScene (ps);
    const int pd = pendingDice.exchange (0);
    if (pd > 0) dice (-1, pd == 2);
    if (pendingFreezeToggle.exchange (0) > 0) setParam ("freeze", pFreeze->load() > 0.5f ? 0.0f : 1.0f);
    if (pendingFreezeOff.exchange (0) > 0) setParam ("freeze", 0.0f);
    if (pendingLearnToggle.exchange (0) > 0) setParam ("learn", pLearn->load() > 0.5f ? 0.0f : 1.0f);

    // parametre "scene" automatise
    if (auto* sp = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("scene")))
    {
        const int v = sp->getIndex();
        if (v != lastSceneParam) { lastSceneParam = v; if (v > 0) recallScene (v - 1, true); }
    }

    if (morphing)
    {
        const double t = (juce::Time::getMillisecondCounterHiRes() * 0.001 - morphStart) / juce::jmax (0.01, morphDur);
        const float a = (float) juce::jlimit (0.0, 1.0, t);
        const float e = a * a * (3.0f - 2.0f * a);
        for (size_t k = 0; k < sceneParams.size(); ++k)
            if (! isDiscrete (sceneParams[k]))
                sceneParams[k]->setValueNotifyingHost (morphFrom[k] + (morphTo[k] - morphFrom[k]) * e);
        if (a >= 1.0f) morphing = false;
    }
}

void MariaProcessor::timerCallback()
{
    morphTick(); serviceEngines(); ensureVirtualMidi();
    if (fbDevChanged.exchange (false)) openFeedbackDevice();
    // sauvegarde de secours toutes les 2 minutes, si la memoire a change
    if (++autosaveTicks >= 30 * 120)
    {
        autosaveTicks = 0;
        if (pAutosave->load() > 0.5f && mem.writePos() != lastAutosaveW) autosaveNow();
    }
}

void MariaProcessor::serviceEngines (double idle)
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    for (auto& S : slots)
        for (int t = 1; t < kNumTypes; ++t)
        {
            auto& st = S.st[(size_t) t];
            const int v = st.load();
            if (v == 1) { S.eng[(size_t) t]->prepare (fs); S.lastUse[(size_t) t] = now; st.store (2); }
            else if (v == 3) S.lastUse[(size_t) t] = now;
            else if (v == 2 && now - S.lastUse[(size_t) t] > idle)
            {
                int e = 2;
                if (st.compare_exchange_strong (e, 0)) { auto fresh = makeEngine (t); std::swap (S.eng[(size_t) t], fresh); }
            }
        }
}

int MariaProcessor::preparedEngines() const
{
    int c = 0;
    for (auto& S : slots) for (int t = 1; t < kNumTypes; ++t) if (S.st[(size_t) t].load() != 0) ++c;
    return c;
}

// ===========================================================================
int MariaProcessor::getNumPrograms() { return numFactoryPresets(); }
const juce::String MariaProcessor::getProgramName (int i) { return factoryPresetName (i); }
void MariaProcessor::setCurrentProgram (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    loadFactory (i);
}

void MariaProcessor::loadFactory (int i)
{
    if (! undoStack.empty() || i != currentProgram) pushUndo();
    currentProgram = juce::jlimit (0, numFactoryPresets() - 1, i);
    applyFactoryPreset (*this, currentProgram);
    generateSceneVariations();
}

juce::AudioProcessorEditor* MariaProcessor::createEditor() { return new MariaEditor (*this); }
} // namespace ma

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ma::MariaProcessor(); }
