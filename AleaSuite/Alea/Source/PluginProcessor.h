#pragma once

#include "Common.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "Pattern.h"
#include "Sequencer.h"
#include "DSP/Voice.h"
#include "../../Shared/DSP/Effects.h"
#include "../../Shared/DSP/Modulators.h"
#include "../../Shared/KnobHost.h"
#include "../../Shared/Link/AleaLink.h"

class AleaAudioProcessor : public juce::AudioProcessor, public alea::gui::KnobHost, private juce::AsyncUpdater
{
public:
    AleaAudioProcessor();
    ~AleaAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Alea"; }
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
    alea::PatternBank bank;

    // verrous pour le de : [piste][vp] ; index kNumVP = moteur
    std::atomic<bool> locks[alea::kNumTracks][alea::kNumVP + 1];

    // retour d'etat vers l'interface
    std::atomic<int>   playStep[alea::kNumTracks];
    std::atomic<float> lastNorm[alea::kNumTracks][alea::kNumVP];
    std::atomic<float> flash[alea::kNumTracks];
    std::atomic<int>   currentPattern { 0 };
    std::atomic<bool>  stutterOn { false };
    std::atomic<float> outPeak { 0.0f };
    std::atomic<bool>  guiTrig[alea::kNumTracks];
    std::atomic<float> modValue[alea::kNumMods];
    std::atomic<int>   songPos { -1 };
    std::atomic<double> playPpq { 0.0 }, patStartShared { 0.0 };
    std::atomic<bool>  isRunning { false };
    std::atomic<float> duckView { 0.0f };

    // oscilloscope
    static constexpr int kScopeLen = 512;
    std::atomic<int> scopeTrack { 0 }, scopeVersion { 0 };
    float scopeBuf[kScopeLen] {};

    // morphing A/B
    std::atomic<float> snapB[alea::kNumTracks][alea::kNumVP];
    std::atomic<int>   snapBEngine[alea::kNumTracks];
    std::atomic<bool>  hasB { false };
    void storeB();
    void recallB();
    void swapB();

    // scenes (8 memoires de sons, rappel + crossfader)
    std::atomic<bool>  sceneValid[alea::kNumScenes];
    std::atomic<float> sceneVal[alea::kNumScenes][alea::kNumTracks][alea::kNumVP];
    std::atomic<int>   sceneEng[alea::kNumScenes][alea::kNumTracks];
    void storeScene (int n);
    void recallScene (int n);
    std::atomic<int> pendingScene { -1 }, lastRecalledScene { -1 };

    // favoris par voix + croisement
    struct Fav { bool valid = false; float v[alea::kNumVP] {}; int engine = 0; };
    Fav favs[alea::kNumTracks][alea::kNumFavs];
    void keepLastHit (int t);
    void storeFav (int t, int slot);
    void recallFav (int t, int slot);
    void breedVoice (int t, int slotA, int slotB, int64_t seed = -1);
    void breedPatterns (int patA, int patB, int dst, int64_t seed = -1);

    // evolution automatique (couche non destructive)
    std::atomic<uint32_t> evoMask[alea::kNumPatterns][alea::kNumTracks];
    std::atomic<float> evoOff[alea::kNumTracks][alea::kNumVP];
    void resetEvolution();
    void commitEvolution();
    int evolutionAmount() const;   // nombre de pas inverses + parametres derives (pour l'affichage)

    // style appris depuis des fichiers MIDI
    struct StyleModel
    {
        bool valid = false;
        juce::String name;
        int bars = 0;
        float on[alea::kNumTracks][16][2][2] {};
        float acc[alea::kNumTracks][16] {};
        int hits[alea::kNumTracks] {};
    };
    StyleModel style;
    bool learnStyleFromMidi (const juce::File& f);
    void generateFromStyle (int pat, int t, int64_t seed = -1);

    // apercu : ce que la sequence va jouer sur la mesure qui vient
    struct Preview { uint32_t play[alea::kNumTracks] {}, ghost[alea::kNumTracks] {}, skip[alea::kNumTracks] {}; };
    Preview previewNextBar() const;

    // song mode
    juce::String getChainText() const { return chainText; }
    bool setChainText (const juce::String& text);

    // echantillons
    bool loadSample (int t, const juce::File& f);
    void clearSample (int t);
    juce::String sampleName (int t) const;

    // MIDI learn
    std::atomic<int> learnParam { -1 };
    std::atomic<int> ccMap[128];
    int paramIndex (const juce::String& paramId) const;
    int ccForParam (int paramIdx) const override;
    void clearMidiMapping (int paramIdx) override;
    void startLearn (int paramIdx) override { learnParam = paramIdx; }
    int learningParam() const override { return learnParam.load(); }

    // historique
    void pushUndo() override;
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    // presets
    static juce::StringArray factoryPresetNames();
    void loadFactoryPreset (int index);
    bool savePresetFile (const juce::File& f);
    bool loadPresetFile (const juce::File& f);
    static juce::File userPresetDir();
    juce::String currentPresetName { "Init" };
    void resetToDefaults();

    std::atomic<float> uiScale { 1.0f };

    // AleaLink : synchronisation avec les autres Alea / Talea
    std::unique_ptr<alea::link::Link> link;
    std::unique_ptr<alea::link::LinkSync> linkSync;

    // ------------------------------------------------------------------
    int  editPattern() const;
    void diceVoice (int t, bool includeEngine, int64_t seed = -1);
    void mutateVoice (int t, float amount);
    void diceAll();
    void randomizeJitter (int t, int64_t seed = -1);
    void clearJitter (int t);

    void randomizeTrackPattern (int pat, int t, int64_t seed = -1);
    void euclidTrack (int pat, int t, int hits, int rotation);
    void mutateTrackPattern (int pat, int t, float amount);
    void varyTrack (int pat, int t, float amount, int64_t seed = -1);
    void shiftTrack (int pat, int t, int dir);
    void clearTrack (int pat, int t);
    void copyTrack (int pat, int t)  { clip.grab (bank.pats[pat].tracks[t]); }
    void pasteTrack (int pat, int t) { clip.put (bank.pats[pat].tracks[t]); }
    void copyPattern (int src, int dst) { if (src != dst) bank.pats[dst].copyFrom (bank.pats[src]); }

    juce::File exportMidi (int pat, int bars);

    void setParamNorm (const juce::String& id, float norm);
    void setParamReal (const juce::String& id, float real);

public:
    int colorChordMask = 0;              // accord publie par Color (12 bits absolus), 0 = aucun (lu par le thread audio)
private:
    void handleAsyncUpdate() override;
    void addEvent (const alea::SeqEvent& e) { if (numEvents < (int) events.size()) events[(size_t) numEvents++] = e; }
    void triggerTrack (const alea::SeqEvent& e);
    void computeModOffsets();
    void evolveStep (int64_t bar);
    void prepareVoices (double voiceRate);
    alea::SeqContext makeContext (int pattern, double patStart, double bpm) const;
    juce::ValueTree captureTree();
    void restoreTree (const juce::ValueTree& state, bool keepTransport);
    void sendNoteOn (juce::MidiBuffer& out, int note, int vel, int offset);
    static BusesProperties makeBuses();
    void setupLink();
    void publishLink (bool running, double ppqStart, double ppqEnd, double bpm);
    float keyLockFreq (int engine, float freq) const;
    alea::link::Hit linkHits[alea::link::kMaxHits];
    float gmod (float raw, int gt) const { return juce::jlimit (0.0f, 1.0f, raw + gOff[gt]); }

    struct TrackVoices { alea::DrumVoice v[2]; int cur = 0; };
    TrackVoices tracks[alea::kNumTracks];
    float drift[alea::kNumTracks][alea::kNumVP + 1] {};
    float modOff[alea::kNumTracks][alea::kNumVP] {};
    float gOff[alea::kNumGT] {};
    float lastFreq[alea::kNumTracks] {};
    float chaosEff = 0.5f;
    alea::CondState condState;

    std::array<alea::SeqEvent, 2048> events {};
    int numEvents = 0;
    std::array<int, 64> beatOffsets {};
    std::array<int64_t, 64> beatIndex {};
    int numBeats = 0;

    struct PendingOff { int note; int remaining; };
    std::array<PendingOff, 64> pendingOffs {};
    int numPendingOffs = 0;

    alea::StereoDelay delay;
    alea::Stutter stutter;
    alea::Glue glue;
    juce::Reverb reverb;
    alea::PlateReverb plate;
    juce::AudioBuffer<float> revBus;
    alea::RandomMods mods;
    alea::HalfbandDecimator decL[alea::kNumTracks], decR[alea::kNumTracks], decD, decV;
    bool hqActive = false;

    alea::FastRng rng { 0xA1EA5EEDu };
    alea::TrackClip clip;

    double sr = 44100.0;
    bool internalRunning = false, wasRunning = false;
    double internalPpq = 0.0, freeModClock = 0.0, patStartPpq = 0.0;
    int64_t songStartBar = 0;
    int requestedPattern = 0, lastParamPattern = 0;
    int dlyDivCur = 6;
    int scopeWrite = 1 << 30, scopeDecim = 24;
    int heldKeys = 0;
    float duckEnv = 0.0f, followEnv = 0.0f;

    juce::String chainText { "A4 B4" };
    std::atomic<int> chainPat[alea::kMaxChain], chainBars[alea::kMaxChain], chainLen { 0 };
    bool songPublished = false;

    std::atomic<alea::SampleData*> sampleSlot[alea::kNumTracks];
    std::vector<std::unique_ptr<alea::SampleData>> sampleOwned;
    std::vector<std::pair<double, alea::SampleData*>> sampleRetired;
    juce::AudioFormatManager formatManager;

    std::vector<juce::MemoryBlock> undoStack, redoStack;

    std::atomic<float>* vpRaw[alea::kNumTracks][alea::kNumVP] {};
    std::atomic<float>* jitRaw[alea::kNumTracks][alea::kNumVP] {};
    std::atomic<float>* engineRaw[alea::kNumTracks] {};
    std::atomic<float>* ftypeRaw[alea::kNumTracks] {};
    std::atomic<float>* chokeRaw[alea::kNumTracks] {};
    std::atomic<float>* muteRaw[alea::kNumTracks] {};
    std::atomic<float>* outRaw[alea::kNumTracks] {};
    std::atomic<float>* velSensRaw[alea::kNumTracks] {};
    std::atomic<float>* linkRaw[alea::kNumTracks] {};
    std::atomic<float>* fxTypeRaw[alea::kNumTracks] {};
    std::atomic<float>* modRateRaw[alea::kNumMods] {};
    std::atomic<float>* modPRaw[alea::kNumMods] {};
    std::atomic<float>* mxSrcRaw[alea::kNumMxSlots] {};
    std::atomic<float>* mxTrkRaw[alea::kNumMxSlots] {};
    std::atomic<float>* mxDstRaw[alea::kNumMxSlots] {};
    std::atomic<float>* mxAmtRaw[alea::kNumMxSlots] {};
    std::atomic<float> *playRaw {}, *bpmRaw {}, *patternRaw {}, *seedRaw {}, *chaosRaw {}, *rndModeRaw {}, *rndDistRaw {}, *scaleRaw {},
                       *swingRaw {}, *accentRaw {}, *seqChaosRaw {}, *ratChaosRaw {}, *humanRaw {},
                       *morphRaw {}, *densityRaw {}, *fillRaw {}, *modDepthRaw {}, *songRaw {}, *midiOutRaw {}, *turLenRaw {}, *followTrkRaw {},
                       *glideRaw {}, *keysRaw {}, *duckAmtRaw {}, *duckSrcRaw {}, *duckRelRaw {},
                       *evoAmtRaw {}, *evoBarsRaw {}, *evoTargetRaw {}, *sceneARaw {}, *sceneBRaw {}, *sceneXfRaw {}, *sceneOnRaw {}, *hqRaw {},
                       *dlyDivRaw {}, *dlyFbRaw {}, *dlyToneRaw {}, *dlyPingRaw {}, *dlyRetRaw {}, *dlyRndRaw {},
                       *revSizeRaw {}, *revDampRaw {}, *revWidthRaw {}, *revRetRaw {}, *revTypeRaw {}, *revPreRaw {},
                       *stuChanceRaw {}, *stuDivRaw {}, *stuLenRaw {},
                       *glueRaw {}, *mDriveRaw {}, *masterRaw {},
                       *rootRaw {}, *keyLockRaw {}, *keyChordRaw {}, *lkSongRaw {}, *lkOnRaw {}, *lkPlayRaw {}, *lkTempoRaw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AleaAudioProcessor)
};
