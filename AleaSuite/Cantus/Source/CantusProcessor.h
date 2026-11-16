#pragma once

#include "CantusCommon.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "MelodyPattern.h"
#include "MelodySequencer.h"
#include "DSP/LeadVoice.h"
#include "../../Shared/DSP/Effects.h"
#include "../../Shared/DSP/Modulators.h"
#include "../../Shared/KnobHost.h"
#include "../../Shared/Link/AleaLink.h"

class CantusAudioProcessor : public juce::AudioProcessor, public alea::gui::KnobHost, private juce::AsyncUpdater
{
public:
    CantusAudioProcessor();
    ~CantusAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Cantus"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    cantus::BBank bank;
    std::unique_ptr<alea::link::Link> link;
    std::unique_ptr<alea::link::LinkSync> linkSync;

    std::atomic<bool>  locks[cantus::kNumTP + 1];     // index kNumTP = types (oscillateurs, filtre...)
    std::atomic<int>   playStep { -1 }, currentPattern { 0 }, songPos { -1 }, lastNote { -1 };
    std::atomic<float> lastNorm[cantus::kNumTP];
    std::atomic<float> flash { 0.0f }, outPeak { 0.0f }, duckView { 0.0f };
    std::atomic<bool>  guiTrig { false };
    std::atomic<float> modValue[alea::kNumMods];
    std::atomic<double> playPpq { 0.0 }, patStartShared { 0.0 };
    std::atomic<bool>  isRunning { false };
    std::atomic<int>   linkedDrums { -1 };               // place AleaLink de la batterie suivie
    std::atomic<int>   linkedColor { -1 }, followChordRoot { -1 };   // Color suivi (accords)

    static constexpr int kScopeLen = 512;
    std::atomic<int> scopeVersion { 0 };
    float scopeBuf[kScopeLen] {};

    // morph / scenes / favoris
    static constexpr int kNumTypes = 7;
    static const char* typeParamId (int i);
    struct Snapshot { bool valid = false; float v[cantus::kNumTP] {}; int types[kNumTypes] {}; };
    std::atomic<float> snapB[cantus::kNumTP];
    std::atomic<int>   snapBTypes[kNumTypes];
    std::atomic<bool>  hasB { false };
    std::atomic<bool>  sceneValid[cantus::kScenes];
    std::atomic<float> sceneVal[cantus::kScenes][cantus::kNumTP];
    std::atomic<int>   sceneTypes[cantus::kScenes][kNumTypes];
    std::atomic<int>   pendingScene { -1 }, lastRecalledScene { -1 };
    Snapshot favs[cantus::kFavs];
    void storeB(); void recallB(); void swapB();
    void storeScene (int n); void recallScene (int n);
    void keepLastNote();
    void storeFav (int slot); void recallFav (int slot);
    void breedVoice (int a, int b, int64_t seed = -1);

    // evolution (non destructive)
    std::atomic<uint32_t> evoMask[cantus::kPatterns];
    std::atomic<int8_t>   evoNote[cantus::kPatterns][cantus::kSteps];
    std::atomic<float>    evoOff[cantus::kNumTP];
    void resetEvolution(); void commitEvolution(); int evolutionAmount() const;

    // style appris (lignes de basse en MIDI)
    struct Style
    {
        bool valid = false; juce::String name; int bars = 0;
        float on[16][2][2] {};      // rythme : position x precedent
        float acc[16] {}, slide[16] {};
        float interval[25] {};      // ecarts melodiques (-12..+12)
        float degree[12] {};        // notes relatives a la tonique
    };
    Style style;
    bool learnStyleFromMidi (const juce::File& f);
    void generateFromStyle (int pat, int64_t seed = -1);

    // generateurs de lignes et operations sur les patterns
    void generatePattern (int pat, int style, int64_t seed = -1);
    void generatePatternLegacy (int pat, int style, int64_t seed = -1);
    struct HarmonySpan { int root, bass, mask, scale, beats; };
    void readHarmony (std::vector<HarmonySpan>& out, int& totalBeats) const;     // grille de Color (ou tonique seule)
    void generateMelodyInto (cantus::BPattern& P, int style, int64_t seed, const cantus::BPattern* develop);
    // improvisation : la phrase suivante est preparee (thread message) puis echangee sur la mesure
    cantus::BPattern improvNext;
    std::atomic<bool> improvReady { false }, improvRequested { false };
    std::atomic<int> improvCount { 0 };
    std::atomic<int> dialogueDrops { 0 }, callRespDrops { 0 }, notesPlayed { 0 }, legatoNotes { 0 }, overlapNotes { 0 };
    std::atomic<int> seqDrops[3];                     // proba / condition, density, seq chaos (diagnostic)
    void playAllNotes();                               // remet a zero tout ce qui retire des notes
    bool generateFromColor (int pat, int64_t seed = -1);       // ligne sur la grille de Color (AleaLink)
    void generateNotesOnly (int pat, int64_t seed = -1);
    void generateRhythmOnly (int pat, int64_t seed = -1);
    void mutateNotes (int pat, float amount);
    void mutateRhythm (int pat, float amount);
    void transposePattern (int pat, int degrees);
    void octavePattern (int pat, int dir);
    void reversePattern (int pat);
    void invertPattern (int pat);
    void shiftPattern (int pat, int dir);
    void euclidPattern (int pat, int hits, int rot);
    void varyPattern (int pat, float amount, int64_t seed = -1);
    void breedPatterns (int a, int b, int dst, int64_t seed = -1);
    void clearPattern (int pat) { bank.pats[pat].clear(); }
    void copyPattern (int src, int dst) { if (src != dst) bank.pats[dst].copyFrom (bank.pats[src]); }
    void copyClip (int pat) { clip.copyFrom (bank.pats[pat]); clipValid = true; }
    void pasteClip (int pat) { if (clipValid) bank.pats[pat].copyFrom (clip); }

    // son
    void diceSound (bool includeTypes, int64_t seed = -1);
    void mutateSound (float amount);
    void randomizeJitter (int64_t seed = -1);
    void clearJitter();

    struct Preview { uint32_t play = 0, ghost = 0, skip = 0; };
    Preview previewNextBar() const;
    juce::File exportMidi (int pat, int bars);

    // song mode
    juce::String getChainText() const { return chainText; }
    bool setChainText (const juce::String& text);

    // MIDI learn / historique / presets
    std::atomic<int> learnParam { -1 };
    std::atomic<int> ccMap[128];
    int paramIndex (const juce::String& id) const;
    int ccForParam (int idx) const override;
    void clearMidiMapping (int idx) override;
    void startLearn (int idx) override { learnParam = idx; }
    int learningParam() const override { return learnParam.load(); }
    void pushUndo() override;
    bool undo(); bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    static juce::StringArray factoryPresetNames();
    void loadFactoryPreset (int index);
    bool savePresetFile (const juce::File& f);
    bool loadPresetFile (const juce::File& f);
    static juce::File userPresetDir();
    juce::String currentPresetName { "Init Lead" };
    void resetToDefaults();
    std::atomic<float> uiScale { 1.0f };

    int  editPattern() const;
    void setParamNorm (const juce::String& id, float norm);
    void setParamReal (const juce::String& id, float real);
    float paramReal (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    // lecture des coups de batterie lies (pour les generateurs)
    bool drumMask (int track, uint32_t& mask, int& len) const;

private:
    void handleAsyncUpdate() override;
    void setupLink();
    float followChord (float midi, double ppq);
    void computeMods();
    void resolveParams();
    void noteOnInternal (const cantus::BassEvent& e, double ppqNow, double ppqPerSample, juce::MidiBuffer& midiOut, int offset);
    void evolveStep (int64_t bar);
    cantus::BassContext makeContext (int pattern, double patStart, double bpm) const;
    juce::ValueTree captureTree();
    void restoreTree (const juce::ValueTree& state, bool keepTransport);
    float gmod (float raw, int g) const { return juce::jlimit (0.0f, 1.0f, raw + gOff[g]); }

    cantus::BassVoice voice;
    alea::HalfbandDecimator decim;
    cantus::Chorus chorus;
    alea::StereoDelay delay;
    alea::PlateReverb plate;
    alea::Glue glue;
    alea::RandomMods mods;
    cantus::BassCondState condState;
    cantus::BassParams bp;
    juce::AudioBuffer<float> revBus;

    float noteJit[cantus::kNumTP] {};
    float noteLock[cantus::kNumTP] {};
    float drift[cantus::kNumTP + 1] {};
    float modOff[cantus::kNumTP] {};
    float gOff[cantus::kNumGT] {};
    float level = 0.5f, panL = 0.7f, panR = 0.7f, sDly = 0, sRev = 0, velGain = 1.0f, curVel = 0.8f;
    float chaosEff = 0.5f;

    std::array<cantus::BassEvent, 512> events {};
    int numEvents = 0;
    std::array<int, 64> beatOffsets {};
    std::array<int64_t, 64> beatIndex {};
    int numBeats = 0;

    double gateOffPpq = -1.0;
    int gateOffSamplesMidi = -1;          // mode Play : porte tenue par le clavier
    int midiOutNote = -1;
    struct PendingOff { int note; int remaining; };
    std::array<PendingOff, 32> pendingOffs {};
    int numPendingOffs = 0;
    int heldKeys[16] {}; int numHeld = 0;
    float transpose = 0.0f;
    bool prevSlide = false;            // la note precedente demandait un legato (slide)
    int64_t lastDialogueDrop = -100;   // dernier pas cede a la basse (jamais deux de suite)

    double sr = 44100.0;
    bool internalRunning = false, wasRunning = false;
    double internalPpq = 0.0, freeModClock = 0.0, patStartPpq = 0.0;
    int64_t songStartBar = 0;
    int requestedPattern = 0, lastParamPattern = 0, dlyDivCur = 6;
    float duckEnv = 0.0f, followEnv = 0.0f, dcX = 0, dcY = 0;
    double lastDuckPpq = -1.0, lastFollowPpq = -1.0;
    int scopeWrite = 1 << 30, scopeDecim = 24;

    juce::String chainText { "A4 B4" };
    std::atomic<int> chainPat[cantus::kChain], chainBars[cantus::kChain], chainLen { 0 };
    bool songPublished = false;
    std::array<alea::link::Hit, alea::link::kMaxHits> linkHits {};

    cantus::BPattern clip;
    bool clipValid = false;
    alea::FastRng rng { 0x7A1EA5EDu };
    std::vector<juce::MemoryBlock> undoStack, redoStack;

    std::atomic<float>* tpRaw[cantus::kNumTP] {};
    std::atomic<float>* jitRaw[cantus::kNumTP] {};
    std::atomic<float>* typeRaw[kNumTypes] {};
    std::atomic<float>* modRateRaw[alea::kNumMods] {};
    std::atomic<float>* modPRaw[alea::kNumMods] {};
    std::atomic<float>* mxSrcRaw[cantus::kMxSlots] {};
    std::atomic<float>* mxDstRaw[cantus::kMxSlots] {};
    std::atomic<float>* mxAmtRaw[cantus::kMxSlots] {};
    std::atomic<float> *playRaw {}, *bpmRaw {}, *patternRaw {}, *seedRaw {}, *chaosRaw {}, *rndModeRaw {}, *rndDistRaw {},
                       *rootRaw {}, *scaleRaw {}, *baseOctRaw {}, *velSensRaw {}, *linkRndRaw {},
                       *swingRaw {}, *seqChaosRaw {}, *ratChaosRaw {}, *humanRaw {}, *noteChaosRaw {}, *densityRaw {}, *fillRaw {},
                       *morphRaw {}, *modDepthRaw {}, *songRaw {}, *midiOutRaw {}, *keysRaw {}, *turLenRaw {}, *followTrkRaw {},
                       *sceneARaw {}, *sceneBRaw {}, *sceneXfRaw {}, *sceneOnRaw {}, *evoAmtRaw {}, *evoBarsRaw {}, *evoTargetRaw {},
                       *chRateRaw {}, *chDepthRaw {}, *chMixRaw {},
                       *dlyDivRaw {}, *dlyFbRaw {}, *dlyToneRaw {}, *dlyPingRaw {}, *dlyRetRaw {}, *dlyRndRaw {},
                       *revSizeRaw {}, *revDampRaw {}, *revWidthRaw {}, *revRetRaw {}, *revPreRaw {},
                       *duckAmtRaw {}, *duckSrcRaw {}, *duckRelRaw {}, *glueRaw {}, *mDriveRaw {}, *masterRaw {},
                       *lkOnRaw {}, *lkPlayRaw {}, *lkTempoRaw {}, *chordFollowRaw {}, *lkSongRaw {}, *improvRaw {}, *dialogueRaw {}, *callRespRaw {},
                       *vibDepthRaw {}, *vibRateRaw {}, *vibDelayRaw {}, *genStyleRaw {};
    int64_t lastImprovBar = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CantusAudioProcessor)
};
