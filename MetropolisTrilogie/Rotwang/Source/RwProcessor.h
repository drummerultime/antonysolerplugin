#pragma once

#include "RwParams.h"
#include "RwEngines.h"
#include "DSP/Modulators.h"
#include "KnobHost.h"
#include "PulseBus.h"

namespace rw
{
class RotwangProcessor : public juce::AudioProcessor,
                         public alea::gui::KnobHost,
                         private juce::Timer
{
public:
    RotwangProcessor();
    ~RotwangProcessor() override;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Rotwang"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- KnobHost
    void pushUndo() override;
    int  ccForParam (int paramIdx) const override;
    void clearMidiMapping (int paramIdx) override;
    void startLearn (int paramIdx) override { learnParam.store (paramIdx); }
    int  learningParam() const override { return learnParam.load(); }

    // --- Actions (thread message)
    void undo();
    bool canUndo() const { return ! undoStack.empty(); }
    void dice (int slot = -1, bool types = false);      // slot -1 = toute la chaine
    void moveSlot (int from, int to);
    void newSeed();
    void storeScene (int s);
    void recallScene (int s);
    void generateSceneVariations();
    bool sceneStored (int s) const { return scenes[(size_t) s].stored; }
    int  activeScene() const { return lastScene.load(); }
    void loadFactory (int i);
    juce::ValueTree captureState();
    void restoreState (const juce::ValueTree&);
    void setParam (const juce::String& id, float normalised);
    void setParamReal (const juce::String& id, float realValue);
    void morphTick();                 // appele par le timer (ou les tests)
    void serviceEngines (double idleSeconds = 20.0);   // prepare / libere les moteurs a la demande (thread message)
    int preparedEngines() const;
    void panic() { panicReq.store (true); }
    bool panicActive() const { return panicPhaseView.load() != 0; }

    // --- Etat partage avec l'interface
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> lockP[kNumSlots][kNumSP];
    std::atomic<bool> slotLock[kNumSlots];
    std::atomic<float> liveP[kNumSlots][kNumSP];
    std::atomic<float> modView[8];             // 5 mods + enveloppe + horloge (+1 libre)
    std::atomic<float> meterIn { 0 }, meterOut { 0 }, slotMeter[kNumSlots];
    std::atomic<int> beatView { 0 };
    std::atomic<int64_t> tickView { 0 };
    std::atomic<int> curTypeView[kNumSlots];   // type affiche (pour les libelles)
    std::atomic<float> chaosView { 0.5f };
    std::atomic<float> uiScale { 0.0f };   // 0 = taille automatique

    int slotType (int s) const;
    float paramValue (int s, int p) const;     // valeur de base 0..1
    double hostBpm() const { return lastBpm.load(); }

    // Pour les tests
    int numUndo() const { return (int) undoStack.size(); }
    float effectiveValue (int s, int p) const { return liveP[s][p].load(); }

private:
    struct SlotRT
    {
        std::array<std::unique_ptr<Engine>, kNumTypes> eng;
        std::array<std::atomic<int>, kNumTypes> st {};      // 0 non prepare, 1 demande, 2 pret, 3 en service
        std::array<double, kNumTypes> lastUse {};
        int cur = 0, pending = -1;
        float fade = 1.0f, onGain = 1.0f;
    };
    struct Scene { bool stored = false; std::vector<float> v; };

    void timerCallback() override;
    void handleMidi (const juce::MidiBuffer&);
    void computeModulation (int n, double ppq, double ppqPerSample, bool tickChanged);
    void buildSceneParamList();
    void applyDice (int s, bool types, uint32_t salt);

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Pointeurs bruts
    std::atomic<float>* pType[kNumSlots] {};
    std::atomic<float>* pOn[kNumSlots] {};
    std::atomic<float>* pVal[kNumSlots][kNumSP] {};
    std::atomic<float>* pJit[kNumSlots][kNumSP] {};
    std::atomic<float>* pMix = nullptr, * pIn = nullptr, * pOut = nullptr, * pWidth = nullptr, * pDuck = nullptr;
    std::atomic<float>* pChaos = nullptr, * pClock = nullptr, * pRate = nullptr, * pGlide = nullptr, * pSeed = nullptr;
    std::atomic<float>* pEvolve = nullptr, * pEvoBars = nullptr, * pFreeze = nullptr, * pBpm = nullptr, * pMorph = nullptr;
    std::atomic<float>* pMacro[kNumMacros] {};
    std::atomic<float>* pModRate[kNumRMods] {}, * pModP[kNumRMods] {};
    std::atomic<float>* pEnvSens = nullptr, * pEnvRel = nullptr;
    std::atomic<float>* pRowSrc[kNumRows] {}, * pRowDst[kNumRows] {}, * pRowAmt[kNumRows] {};

    double fs = 48000.0;
    SlotRT slots[kNumSlots];
    alea::RandomMods mods;
    juce::AudioBuffer<float> work, dry;

    // horloge & hasard
    double freePpq = 0.0;
    metro::PulseFollower follower;
    std::atomic<float>* pTempoSrc = nullptr;
    int64_t tick = 0, lastTickIdx = INT64_MIN;
    double freeCountdown = 0.0;
    float rndCur[kNumSlots * kNumSP + 4] {}, rndTgt[kNumSlots * kNumSP + 4] {};
    float evoCur[kNumSlots * kNumSP] {}, evoTgt[kNumSlots * kNumSP] {};
    int64_t lastEvo = INT64_MIN;
    float envIn = 0.0f, envMod = 0.0f, duckEnv = 0.0f;
    float dcL = 0, dcR = 0, dcxL = 0, dcxR = 0;
    float limGain = 1.0f;
    float lastMixG = 1.0f, lastInG = 1.0f, lastOutG = 1.0f;
    std::atomic<double> lastBpm { 120.0 };

    // valeurs effectives du sous-bloc
    float eff[kNumSlots][kNumSP] {};
    float effMix = 1.0f, effChaos = 0.5f, effWidth = 1.0f;

    // MIDI
    std::array<std::atomic<int>, 128> ccMap;   // cc -> index de parametre (-1)
    std::atomic<int> learnParam { -1 };
    std::atomic<bool> panicReq { false };
    std::atomic<int> panicPhaseView { 0 }, pendingFreezeOff { 0 };
    int panicPhase = 0, panicHold = 0;
    float panicGain = 1.0f;
    bool panicResetPending = false;
    std::atomic<int> pendingScene { -1 }, pendingDice { 0 }, pendingFreezeToggle { 0 };

    // Undo
    std::vector<juce::ValueTree> undoStack;

    // Scenes
    std::array<Scene, kNumScenes> scenes;
    std::vector<juce::RangedAudioParameter*> sceneParams;
    std::vector<float> morphFrom, morphTo;
    double morphStart = 0.0, morphDur = 0.0;
    bool morphing = false;
    std::atomic<int> lastScene { -1 };
    int lastSceneParam = 0;

    int currentProgram = 0;
    juce::Random msgRng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RotwangProcessor)
};
} // namespace rw
