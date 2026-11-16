#include "RwProcessor.h"
#include "RwPresets.h"
#include "RwEditor.h"

namespace rw
{
using alea::hUni;
using alea::hBi;
using alea::hash3;

static juce::ParameterID P (const juce::String& id) { return { id, 1 }; }

// ===========================================================================
juce::AudioProcessorValueTreeState::ParameterLayout RotwangProcessor::createLayout()
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
                    return paramText (t, p, v, aux);
                });
            else
                at = at.withStringFromValueFunction (pctText);
            L.add (std::make_unique<juce::AudioParameterFloat> (P (pid (s, p)), n + (p < 6 ? "P" + juce::String (p + 1) : juce::String ("Mix")),
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), p < 6 ? 0.5f : 0.5f, at));
            L.add (std::make_unique<juce::AudioParameterFloat> (P (jid (s, p)), n + (p < 6 ? "Rnd P" + juce::String (p + 1) : juce::String ("Rnd Mix")),
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
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("scene"), "Scene", juce::StringArray { "-", "1", "2", "3", "4", "5", "6", "7", "8" }, 0));
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
    L.add (std::make_unique<juce::AudioParameterChoice> (P ("tempo_src"), "Tempo source",
                                                          juce::StringArray { "Hote / interne", "Pulsation musiciens (Maria)" }, 1));
    return L;
}

// ===========================================================================
RotwangProcessor::RotwangProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ROTWANG", createLayout())
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
    pFreeze = apvts.getRawParameterValue ("freeze"); pBpm = apvts.getRawParameterValue ("bpm"); pTempoSrc = apvts.getRawParameterValue ("tempo_src");
    pMorph = apvts.getRawParameterValue ("morph");
    for (int m = 0; m < kNumMacros; ++m) pMacro[m] = apvts.getRawParameterValue ("macro" + juce::String (m + 1));
    for (int m = 0; m < kNumRMods; ++m)
    {
        pModRate[m] = apvts.getRawParameterValue ("modrate" + juce::String (m + 1));
        pModP[m] = apvts.getRawParameterValue ("modp" + juce::String (m + 1));
    }
    pEnvSens = apvts.getRawParameterValue ("envsens"); pEnvRel = apvts.getRawParameterValue ("envrel");
    for (int r = 0; r < kNumRows; ++r)
    {
        const juce::String n = "row" + juce::String (r + 1);
        pRowSrc[r] = apvts.getRawParameterValue (n + "_src");
        pRowDst[r] = apvts.getRawParameterValue (n + "_dst");
        pRowAmt[r] = apvts.getRawParameterValue (n + "_amt");
    }

    buildSceneParamList();
    msgRng.setSeedRandomly();
    loadFactory (0);
    undoStack.clear();
    mods.reset();
#if ! RW_HEADLESS_TEST
    startTimerHz (30);
#endif
}

RotwangProcessor::~RotwangProcessor() { stopTimer(); }

bool RotwangProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = l.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void RotwangProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
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
}

int RotwangProcessor::slotType (int s) const { return juce::jlimit (0, kNumTypes - 1, (int) std::round (pType[s]->load())); }
float RotwangProcessor::paramValue (int s, int p) const { return pVal[s][p]->load(); }

// ===========================================================================
void RotwangProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
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
            if (n >= 36 && n <= 43) pendingScene.store (n - 36);
            else if (n == 44) pendingDice.store (1);
            else if (n == 45) pendingDice.store (2);
            else if (n == 46) pendingFreezeToggle.store (1);
            else if (n == 47) panic();
        }
    }
}

// ===========================================================================
void RotwangProcessor::computeModulation (int n, double ppq, double pps, bool tickChanged)
{
    const uint32_t seed = (uint32_t) pSeed->load() * 2654435761u + 17u;
    const double bpm = lastBpm.load();
    const int clk = (int) std::round (pClock->load());
    constexpr int NP = kNumSlots * kNumSP;

    // 1. pas d'horloge : nouveaux tirages
    if (tickChanged)
        for (int i = 0; i < NP + 4; ++i)
            rndTgt[i] = alea::shapedBi (1, (uint32_t) tick, (uint32_t) i + 1u, seed);
    if (clk == 0)
        for (int i = 0; i < NP + 4; ++i) rndTgt[i] = 0.0f;

    const double tickSec = clk == kClockFree ? 1.0 / juce::jmax (0.05f, pRate->load())
                                             : (clk > 0 ? clockBeats (clk) * 60.0 / bpm : 0.5);
    const double tau = juce::jmax (0.002, (double) pGlide->load() * tickSec);
    const float gc = (float) (1.0 - std::exp (-(double) n / (tau * fs)));
    for (int i = 0; i < NP + 4; ++i) rndCur[i] += (rndTgt[i] - rndCur[i]) * gc;

    // 2. evolution lente
    const float evolve = pEvolve->load();
    {
        const double per = 4.0 * evolveBars ((int) std::round (pEvoBars->load()));
        const int64_t e = (int64_t) std::floor (ppq / per);
        if (e != lastEvo)
        {
            const bool jump = lastEvo == INT64_MIN || std::abs (e - lastEvo) > 2;
            lastEvo = e;
            if (! jump)
                for (int i = 0; i < NP; ++i)
                {
                    const int s = i / kNumSP, p = i % kNumSP;
                    if (evolve <= 0.0f || lockP[s][p].load() || p == 6) { evoTgt[i] = 0.0f; continue; }
                    evoTgt[i] = juce::jlimit (-0.45f * evolve, 0.45f * evolve,
                                              evoTgt[i] * 0.75f + hBi ((uint32_t) e, (uint32_t) i, seed ^ 0xE70u) * 0.3f * evolve);
                }
        }
        if (evolve <= 0.0f) for (int i = 0; i < NP; ++i) evoTgt[i] = 0.0f;
        const float ec = (float) (1.0 - std::exp (-(double) n / (60.0 / bpm * 2.0 * fs)));
        for (int i = 0; i < NP; ++i) evoCur[i] += (evoTgt[i] - evoCur[i]) * ec;
    }

    // 3. sources de modulation
    {
        int rates[kNumRMods]; float pp[kNumRMods];
        for (int m = 0; m < kNumRMods; ++m) { rates[m] = (int) std::round (pModRate[m]->load()); pp[m] = pModP[m]->load(); }
        mods.advance (ppq, rates, pp, 8, seed);
    }
    float src[kNumSources] {};
    for (int m = 0; m < kNumRMods; ++m) { src[1 + m] = mods.value[m] * 2.0f - 1.0f; modView[m].store (mods.value[m]); }
    const float envN = juce::jlimit (0.0f, 1.0f, envMod * expMap (pEnvSens->load(), 0.5f, 40.0f));
    src[6] = envN; modView[5].store (envN);
    src[7] = rndCur[NP]; modView[6].store (0.5f + 0.5f * rndCur[NP]);
    for (int m = 0; m < kNumMacros; ++m) src[8 + m] = pMacro[m]->load();

    float dst[kNumDests] {};
    for (int r = 0; r < kNumRows; ++r)
    {
        const int si = (int) std::round (pRowSrc[r]->load()), di = (int) std::round (pRowDst[r]->load());
        if (si <= 0 || di <= 0 || si >= kNumSources || di >= kNumDests) continue;
        dst[di] += src[si] * pRowAmt[r]->load();
    }

    effChaos = juce::jlimit (0.0f, 1.0f, pChaos->load() + dst[kDestChaos]);
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
    juce::ignoreUnused (pps);
}

// ===========================================================================
void RotwangProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
#if RW_HEADLESS_TEST
    serviceEngines (1.0e9);   // dans les tests, pas de thread message : preparation immediate
#endif
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    if (buffer.getNumChannels() < 2 || n <= 0) return;
    if (numIn == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    else if (numIn == 0) buffer.clear();

    handleMidi (midi);
    if (dry.getNumSamples() < n) dry.setSize (2, n, false, false, true);

    // tempo & position
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
    bpm = juce::jlimit (20.0, 400.0, bpm);
    lastBpm.store (bpm);
    double pps = bpm / 60.0 / fs;
    double base = playing ? hostPpq : freePpq;
    if (pTempoSrc->load() > 0.5f)
    {
        double fb, fpps, fbpm;   // pulsation des musiciens publiee par Maria ; sinon horloge habituelle
        if (follower.step (n, fs, juce::Time::getMillisecondCounterHiRes(), base, fb, fpps, fbpm))
        { base = fb; pps = fpps; bpm = fbpm; lastBpm.store (bpm); }
    }
    freePpq = base + pps * n;

    const bool freeze = pFreeze->load() > 0.5f;
    const uint32_t seed = (uint32_t) pSeed->load() * 2654435761u + 17u;
    const float inG = juce::Decibels::decibelsToGain (pIn->load());
    const float outG = juce::Decibels::decibelsToGain (pOut->load());
    const float duck = pDuck->load();
    const float envRelC = std::exp (-1.0f / (expMap (pEnvRel->load(), 0.02f, 3.0f) * (float) fs));
    const int clk = (int) std::round (pClock->load());

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
        }
        const int m = juce::jmin (kSub, n - pos);
        const double ppq = base + pps * pos;
        float* l = L + pos; float* r = R + pos;

        // gain d'entree (rampe) + enveloppes
        float pk = 0;
        for (int i = 0; i < m; ++i)
        {
            const float g = lastInG + (inG - lastInG) * (float) (i + 1) / (float) m;
            l[i] *= g; r[i] *= g;
            dry.setSample (0, pos + i, l[i]); dry.setSample (1, pos + i, r[i]);
            pk = juce::jmax (pk, std::abs (l[i]), std::abs (r[i]));
        }
        lastInG = inG;
        pkIn = juce::jmax (pkIn, pk);
        envMod = pk > envMod ? pk : envMod * std::pow (envRelC, (float) m);
        duckEnv = pk > duckEnv ? duckEnv + (pk - duckEnv) * 0.5f : duckEnv * std::pow (0.99995f, (float) m * 0.4f);

        // horloge aleatoire
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

        Ctx ctx;
        ctx.fs = fs; ctx.bpm = bpm; ctx.ppq = ppq; ctx.ppqPerSample = pps;
        ctx.freeze = freeze; ctx.seed = seed; ctx.tick = tick; ctx.env = juce::jlimit (0.0f, 1.0f, envMod * 4.0f);

        // chaine
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
            std::copy (l, l + m, tl); std::copy (r, r + m, tr);
            float pv[6];
            for (int p = 0; p < 6; ++p) pv[p] = eff[s][p];
            S.eng[(size_t) S.cur]->process (tl, tr, m, ctx, pv);

            const float mixT = eff[s][6];
            const float fstep = 1.0f / 256.0f, ostep = 1.0f / 512.0f;
            float spk = 0;
            for (int i = 0; i < m; ++i)
            {
                S.fade = S.pending >= 0 ? juce::jmax (0.0f, S.fade - fstep) : juce::jmin (1.0f, S.fade + fstep);
                S.onGain = onT ? juce::jmin (1.0f, S.onGain + ostep) : juce::jmax (0.0f, S.onGain - ostep);
                float a = tl[i], b = tr[i];
                if (! std::isfinite (a) || ! std::isfinite (b)) { a = b = 0.0f; S.eng[(size_t) S.cur]->reset(); }
                const float g = mixT * S.fade * S.onGain;
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

        // sortie : largeur, duck, DC, limiteur, mix
        const float duckG = 1.0f - duck * juce::jlimit (0.0f, 1.0f, duckEnv * 4.0f);
        for (int i = 0; i < m; ++i)
        {
            float a = l[i], b = r[i];
            const float mid = 0.5f * (a + b), side = 0.5f * (a - b) * effWidth;
            a = (mid + side) * duckG; b = (mid - side) * duckG;
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
            const float t = (float) (i + 1) / (float) m;
            const float mixG = lastMixG + (effMix - lastMixG) * t;
            const float og = lastOutG + (outG - lastOutG) * t;
            const float dl = dry.getSample (0, pos + i), dr = dry.getSample (1, pos + i);
            l[i] = (dl + (a - dl) * mixG) * og;
            r[i] = (dr + (b - dr) * mixG) * og;
            pkOut = juce::jmax (pkOut, std::abs (l[i]), std::abs (r[i]));
        }
        lastMixG = effMix; lastOutG = outG;
    }

    panicPhaseView.store (panicPhase);
    meterIn.store (juce::jmax (pkIn, meterIn.load() * 0.85f));
    meterOut.store (juce::jmax (pkOut, meterOut.load() * 0.85f));
    for (int s = 0; s < kNumSlots; ++s) slotMeter[s].store (juce::jmax (slotPk[s], slotMeter[s].load() * 0.85f));
}

// ===========================================================================
// Etat, undo
juce::ValueTree RotwangProcessor::captureState()
{
    juce::ValueTree root ("ROTWANG_STATE");
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
    ex.setProperty ("slotlocks", slocks, nullptr);
    ex.setProperty ("cc", cc.trim(), nullptr);
    ex.setProperty ("program", currentProgram, nullptr);
    ex.setProperty ("ui", uiScale.load(), nullptr);
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

void RotwangProcessor::restoreState (const juce::ValueTree& root)
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
    for (auto& c : ccMap) c.store (-1);
    for (auto& tok : juce::StringArray::fromTokens (ex.getProperty ("cc").toString(), " ", ""))
    {
        const int cc = tok.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int pi = tok.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (cc >= 0 && cc < 128 && tok.contains (":")) ccMap[(size_t) cc].store (pi);
    }
    currentProgram = (int) ex.getProperty ("program", 0);
    if (ex.hasProperty ("ui")) uiScale.store ((float) ex.getProperty ("ui"));
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

void RotwangProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = captureState().createXml()) copyXmlToBinary (*xml, dest);
}

void RotwangProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        const auto t = juce::ValueTree::fromXml (*xml);
        if (t.hasType ("ROTWANG_STATE")) restoreState (t);
        else if (t.hasType (apvts.state.getType())) apvts.replaceState (t);
    }
}

void RotwangProcessor::pushUndo()
{
    undoStack.push_back (captureState());
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
}

void RotwangProcessor::undo()
{
    if (undoStack.empty()) return;
    const auto t = undoStack.back();
    undoStack.pop_back();
    restoreState (t);
}

int RotwangProcessor::ccForParam (int idx) const
{
    if (idx < 0) return -1;
    for (int c = 0; c < 128; ++c) if (ccMap[(size_t) c].load() == idx) return c;
    return -1;
}
void RotwangProcessor::clearMidiMapping (int idx)
{
    for (auto& c : ccMap) if (c.load() == idx) c.store (-1);
}

void RotwangProcessor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}
void RotwangProcessor::setParamReal (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// ===========================================================================
// De, deplacement
void RotwangProcessor::applyDice (int s, bool types, uint32_t salt)
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
        const float target = p < 6 ? rnd() : 0.3f + 0.6f * rnd();
        setParam (pid (s, p), cur + (target - cur) * (types ? 1.0f : amt));
        if (rnd() < ch * 0.5f) setParam (jid (s, p), rnd() * rnd() * ch);
    }
}

void RotwangProcessor::dice (int slot, bool types)
{
    pushUndo();
    const uint32_t salt = (uint32_t) msgRng.nextInt();
    if (slot >= 0) { applyDice (slot, types, salt); return; }
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

void RotwangProcessor::moveSlot (int from, int to)
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

void RotwangProcessor::newSeed()
{
    pushUndo();
    setParamReal ("seed", (float) msgRng.nextInt (10000));
}

// ===========================================================================
// Scenes
void RotwangProcessor::buildSceneParamList()
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
    for (auto id : { "chaos", "clock", "rate", "glide", "evolve", "evobars", "width", "duck" })
        sceneParams.push_back (apvts.getParameter (id));
}

static bool isDiscrete (juce::RangedAudioParameter* p)
{
    return dynamic_cast<juce::AudioParameterChoice*> (p) != nullptr || dynamic_cast<juce::AudioParameterBool*> (p) != nullptr
        || dynamic_cast<juce::AudioParameterInt*> (p) != nullptr;
}

void RotwangProcessor::storeScene (int s)
{
    if (s < 0 || s >= kNumScenes) return;
    auto& S = scenes[(size_t) s];
    S.v.resize (sceneParams.size());
    for (size_t k = 0; k < sceneParams.size(); ++k) S.v[k] = sceneParams[k]->getValue();
    S.stored = true;
    lastScene.store (s);
}

void RotwangProcessor::recallScene (int s)
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
    if (auto* sp = apvts.getParameter ("scene"))
    {
        lastSceneParam = s + 1;
        sp->setValueNotifyingHost (sp->convertTo0to1 ((float) (s + 1)));
    }
}

void RotwangProcessor::generateSceneVariations()
{
    storeScene (0);
    const auto base = scenes[0].v;
    for (int k = 1; k < kNumScenes; ++k)
    {
        auto v = base;
        const float amt = 0.12f + 0.6f * (float) k / (float) (kNumScenes - 1);
        size_t idx = 0;
        for (int s = 0; s < kNumSlots; ++s)
        {
            idx += 2;   // type, on
            for (int p = 0; p < kNumSP; ++p)
            {
                if (! lockP[s][p].load())
                {
                    const float t = p < 6 ? msgRng.nextFloat() : 0.3f + 0.6f * msgRng.nextFloat();
                    v[idx] += (t - v[idx]) * amt;
                    if (msgRng.nextFloat() < amt * 0.5f) v[idx + 1] = msgRng.nextFloat() * 0.5f;
                }
                idx += 2;
            }
        }
        auto& S = scenes[(size_t) k];
        S.v = v; S.stored = true;
    }
    lastScene.store (0);
}

void RotwangProcessor::morphTick()
{
    // actions demandees par le MIDI (notes)
    const int ps = pendingScene.exchange (-1);
    if (ps >= 0) recallScene (ps);
    const int pd = pendingDice.exchange (0);
    if (pd > 0) dice (-1, pd == 2);
    if (pendingFreezeToggle.exchange (0) > 0) setParam ("freeze", pFreeze->load() > 0.5f ? 0.0f : 1.0f);
    if (pendingFreezeOff.exchange (0) > 0) setParam ("freeze", 0.0f);

    // parametre "scene" automatise
    if (auto* sp = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("scene")))
    {
        const int v = sp->getIndex();
        if (v != lastSceneParam) { lastSceneParam = v; if (v > 0) recallScene (v - 1); }
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

void RotwangProcessor::timerCallback() { morphTick(); serviceEngines(); }

void RotwangProcessor::serviceEngines (double idle)
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

int RotwangProcessor::preparedEngines() const
{
    int c = 0;
    for (auto& S : slots) for (int t = 1; t < kNumTypes; ++t) if (S.st[(size_t) t].load() != 0) ++c;
    return c;
}

// ===========================================================================
int RotwangProcessor::getNumPrograms() { return numFactoryPresets(); }
const juce::String RotwangProcessor::getProgramName (int i) { return factoryPresetName (i); }
void RotwangProcessor::setCurrentProgram (int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    loadFactory (i);
}

void RotwangProcessor::loadFactory (int i)
{
    if (! undoStack.empty() || i != currentProgram) pushUndo();
    currentProgram = juce::jlimit (0, numFactoryPresets() - 1, i);
    applyFactoryPreset (*this, currentProgram);
    generateSceneVariations();
}

juce::AudioProcessorEditor* RotwangProcessor::createEditor() { return new RotwangEditor (*this); }
} // namespace rw

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new rw::RotwangProcessor(); }
