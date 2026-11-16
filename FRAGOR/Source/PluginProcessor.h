#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "Params.h"
#include "Presets.h"
#include "dsp/Engine.h"
#include "dsp/Effects.h"

class FragorProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    FragorProcessor();
    ~FragorProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FRAGOR"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- presets / créativité (thread message)
    void loadFactoryPreset (int index);
    void generate (int style);
    void mutate (float amount);
    bool saveUserPreset (const juce::File& f, const juce::String& category);
    bool loadUserPreset (const juce::File& f);
    static juce::File userPresetFolder();
    static juce::File dataFolder();

    // historique
    void pushUndo();
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // normalisation du volume (presets d'usine, GENERATE, MUTATE)
    bool autoGain = true;
    float measureRms();
    void normalizeLoudness();

    // courbes de LFO dessinées
    const fragor::LfoCurve& getLfoCurve (int i) const { return extras.curves[(size_t) i]; }
    void setLfoCurve (int i, const fragor::LfoCurve& c);
    const float* lfoTableForUi (int i) const { return lfoTab[i][lfoTabIdx[i].load()]; }

    // ordre de la chaîne d'effets
    std::array<int, fragor::numFxModules> getFxOrder() const;
    void setFxOrder (const std::array<int, fragor::numFxModules>& o);

    // wavetables utilisateur
    bool importWavetable (int slot, const juce::File& f, juce::String& error);
    const fragor::Wavetable* getUserTable (int slot) const { return userTablePtr[slot].load(); }
    juce::String userTableName[fragor::kNumUserTables];

    // matrice : ajout rapide (glisser-déposer)
    int addModulation (int src, int dst, float amount);

    // snapshots de macros
    void storeSnapshot (int i);
    void recallSnapshot (int i);
    bool snapshotUsed (int i) const { return snapUsed[i]; }

    juce::String presetName { "INIT" };
    juce::String presetCategory { "INIT" };
    juce::StringArray macroNames { "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4" };
    std::atomic<int> uiChangeCounter { 0 };

    juce::AudioProcessorValueTreeState apvts;

    // ---- moniteurs (lecture par l'UI)
    static constexpr int kScopeSize = 2048;
    float scope[kScopeSize] {};
    std::atomic<int> scopeWrite { 0 };
    std::atomic<float> lfoView[3] {};
    std::atomic<float> liveMod[fragor::numModDests] {};
    std::atomic<int> seqStepView { -1 };
    std::atomic<int> activeVoices { 0 };

    juce::MidiKeyboardState keyboardState;
    double fallbackBpm = 140.0;

private:
    void timerCallback() override;
    void snapshot (fragor::SynthParams& sp, fragor::FxParams& fp) const;
    void handleMidi (const juce::MidiMessage& m, const fragor::SynthParams& sp, const fragor::VoiceCtx& ctx);
    juce::ValueTree captureState() const;
    void applyStateTree (const juce::ValueTree& tree);
    void applyExtras();
    void setUserTable (int slot, std::vector<std::vector<float>> cycles, const juce::String& name);
    void afterPatchChange (bool normalise);

    fragor::Engine engine;
    fragor::FxChain fx;
    fragor::VoiceCtx ctx;
    juce::Random rng;
    int currentProgram = 0;
    double internalBeats = 0.0, timeSec = 0.0;
    float modWheel = 0.0f, bendNorm = 0.0f, chanPressure = 0.0f, slideVal = 0.0f;
    int lastLatency = -1;

    std::vector<float> bufL, bufR;

    // patch : courbes et ordre FX
    fragor::PatchExtras extras;
    float lfoTab[3][2][fragor::kLfoTableSize] {};
    std::atomic<int> lfoTabIdx[3] {};
    std::atomic<int> fxOrderA[fragor::numFxModules] {};

    // tables utilisateur
    std::vector<std::vector<float>> userCycles[fragor::kNumUserTables];
    std::shared_ptr<fragor::Wavetable> userTableOwned[fragor::kNumUserTables];
    std::atomic<const fragor::Wavetable*> userTablePtr[fragor::kNumUserTables] {};
    std::vector<std::shared_ptr<fragor::Wavetable>> tableGraveyard;

    // historique
    std::vector<juce::ValueTree> undoStack, redoStack;

    // snapshots
    float snaps[fragor::kNumSnapshots][fragor::kNumMacros] {};
    bool snapUsed[fragor::kNumSnapshots] {};
    float morphFrom[fragor::kNumMacros] {}, morphTo[fragor::kNumMacros] {};
    double morphPos = 1.0;

    // MIDI out
    int lastCcOut = -1, lastStepOut = -1, pendingOffSamples = -1, pendingOffNote = -1;

    // pointeurs bruts vers les paramètres
    struct OscPtrs { std::atomic<float> *on, *table, *pos, *warpMode, *warp, *uni, *detune, *blend, *spread, *oct, *semi, *fine, *level, *pan, *randPhase; };
    struct LfoPtrs { std::atomic<float> *shape, *sync, *div, *rate, *retrig, *phase, *curve, *smooth; };
    struct EnvPtrs { std::atomic<float> *a, *d, *s, *r; };
    struct FltPtrs { std::atomic<float> *on, *type, *cutoff, *reso, *drive, *morph, *key, *mix, *envAmt; };
    OscPtrs osc[2];
    LfoPtrs lfo[3];
    EnvPtrs envAmp, env2, env3;
    FltPtrs flt[2];
    std::atomic<float> *fltRouting, *quality, *mpeOn, *mpeBend;
    std::atomic<float> *subOn, *subShape, *subOct, *subLevel, *subDirect, *noiseLevel, *noiseColor;
    std::atomic<float> *ampVel, *penvAmt, *penvDecay, *voiceMode, *glide, *bendRange, *masterTune;
    std::atomic<float> *seqOn, *seqSteps, *seqLen, *seqReset, *seq[fragor::kNumSeqSteps];
    std::atomic<float> *mSrc[fragor::kNumMatrixSlots], *mDst[fragor::kNumMatrixSlots], *mAmt[fragor::kNumMatrixSlots];
    std::atomic<float> *macro[fragor::kNumMacros];
    std::atomic<float> *distOn, *distMode, *distDrive, *distBias, *distTone, *distMix;
    std::atomic<float> *dist2On, *dist2Mode, *dist2Drive, *dist2Bias, *dist2Tone, *dist2Mix;
    std::atomic<float> *crushOn, *crushBits, *crushRate, *crushMix;
    std::atomic<float> *ottOn, *ottDepth, *ottTime, *ottUp, *ottDown, *ottOut;
    std::atomic<float> *chorusOn, *chorusRate, *chorusDepth, *chorusMix;
    std::atomic<float> *dlyOn, *dlyDiv, *dlyFb, *dlyTone, *dlyPing, *dlyMix;
    std::atomic<float> *revOn, *revSize, *revDamp, *revWidth, *revMix;
    std::atomic<float> *subMono, *subMonoFreq, *width, *clip, *masterGain;
    std::atomic<float> *midiOut, *midiOutCC, *midiOutNote, *snapTime;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FragorProcessor)
};
