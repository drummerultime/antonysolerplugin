#pragma once

#include "ColorCommon.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include "ColorPattern.h"
#include "ColorSequencer.h"
#include "DSP/ColorVoice.h"
#include "../../Shared/DSP/Effects.h"
#include "../../Shared/DSP/Modulators.h"
#include "../../Shared/KnobHost.h"
#include "../../Shared/Link/AleaLink.h"

// COLOR : instrument harmonique polyphonique (8 voix), progressions generees selon des regles
// d'harmonie (fonctionnelle, jazz, modale, post-tonale...), voicings et conduite des voix, rythmes,
// arpeges, et synchronisation AleaLink (Talea suit les accords, Alea peut s'y accorder).
class ColorAudioProcessor : public juce::AudioProcessor, public alea::gui::KnobHost, private juce::AsyncUpdater
{
public:
    ColorAudioProcessor();
    ~ColorAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Color"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ------------------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    color::CBank bank;
    std::unique_ptr<alea::link::Link> link;
    std::unique_ptr<alea::link::LinkSync> linkSync;

    std::atomic<bool>  locks[color::kNumTP + 1];
    std::atomic<int>   playStep { -1 }, currentPattern { 0 }, songPos { -1 };
    std::atomic<float> lastNorm[color::kNumTP];
    std::atomic<float> flash { 0.0f }, outPeak { 0.0f }, duckView { 0.0f };
    std::atomic<bool>  guiTrig { false };
    std::atomic<int>   auditionChord { -1 };           // index d'accord a jouer depuis l'interface
    std::atomic<float> modValue[alea::kNumMods];
    std::atomic<double> playPpq { 0.0 }, patStartShared { 0.0 };
    std::atomic<bool>  isRunning { false };
    std::atomic<int>   linkedDrums { -1 };

    // etat harmonique visible par l'interface
    std::atomic<int>   curChordIndex { -1 }, curChordPacked { 0 }, keyChordPacked { 0 };
    std::atomic<bool>  curChordSub { false }, curChordApproach { false };
    std::atomic<int>   numSounding { 0 };
    std::atomic<int>   sounding[color::kVoices];        // notes MIDI en cours (voicing affiche)
    std::atomic<int>   lastVoicingN { 0 };
    std::atomic<int>   lastVoicing[8];

    static constexpr int kScopeLen = 512;
    std::atomic<int> scopeVersion { 0 };
    float scopeBuf[kScopeLen] {};

    // morph / scenes / favoris
    static constexpr int kNumTypes = 7;
    static const char* typeParamId (int i);
    struct Snapshot { bool valid = false; float v[color::kNumTP] {}; int types[kNumTypes] {}; };
    std::atomic<float> snapB[color::kNumTP];
    std::atomic<int>   snapBTypes[kNumTypes];
    std::atomic<bool>  hasB { false };
    std::atomic<bool>  sceneValid[color::kScenes];
    std::atomic<float> sceneVal[color::kScenes][color::kNumTP];
    std::atomic<int>   sceneTypes[color::kScenes][kNumTypes];
    std::atomic<int>   pendingScene { -1 }, lastRecalledScene { -1 };
    Snapshot favs[color::kFavs];
    void storeB(); void recallB(); void swapB();
    void storeScene (int n); void recallScene (int n);
    void keepLastNote();
    void storeFav (int slot); void recallFav (int slot);
    void breedVoice (int a, int b, int64_t seed = -1);

    // evolution (non destructive) : rythme, harmonie (substitutions), son
    std::atomic<uint32_t> evoMask[color::kPatterns];
    std::atomic<uint32_t> evoSub[color::kPatterns];
    std::atomic<float>    evoOff[color::kNumTP];
    void resetEvolution(); void commitEvolution(); int evolutionAmount() const;

    // harmonie : generation et transformations de la progression
    void generateProgression (int pat, int64_t seed = -1);
    void reharmonize (int pat, int op, int index = -1, int64_t seed = -1);
    void reharmonizeAmount (int pat, float amount, int64_t seed = -1);
    void varyProgression (int pat, float amount, int64_t seed = -1);
    void simplifyProgression (int pat);
    void enrichProgression (int pat);
    void transposeProgression (int pat, int degrees);
    void rotateProgression (int pat, int dir);
    void reverseProgression (int pat);
    void addCadence (int pat, int type);
    void setChordAt (int pat, int index, const color::theory::Chord& c);
    void insertChord (int pat, int index, const color::theory::Chord& c);
    void removeChord (int pat, int index);
    void setChordLengths (int pat, int beats);
    juce::String progressionText (int pat) const;
    bool setProgressionText (int pat, const juce::String& text);     // "Am7 D9 Gmaj7 Cmaj7/E"

    // rythme
    void applyRhythm (int pat, int preset, int64_t seed = -1);
    void generateRhythm (int pat, int64_t seed = -1);
    void mutateRhythm (int pat, float amount);
    void shiftPattern (int pat, int dir);
    void euclidPattern (int pat, int hits, int rot);
    void breedPatterns (int a, int b, int dst, int64_t seed = -1);
    void clearRhythm (int pat) { bank.pats[pat].clearRhythm(); }
    void copyPattern (int src, int dst) { if (src != dst) bank.pats[dst].copyFrom (bank.pats[src]); }
    void copyClip (int pat) { clip.copyFrom (bank.pats[pat]); clipValid = true; }
    void pasteClip (int pat) { if (clipValid) bank.pats[pat].copyFrom (clip); }
    void pasteHarmony (int pat) { if (clipValid) bank.pats[pat].copyHarmonyFrom (clip); }

    // son
    void diceSound (bool includeTypes, int64_t seed = -1);
    void mutateSound (float amount);
    void randomizeJitter (int64_t seed = -1);
    void clearJitter();

    struct Preview { uint32_t play = 0, ghost = 0, skip = 0; };
    Preview previewNextBar() const;
    juce::File exportMidi (int pat, int bars);
    bool learnChordsFromMidi (const juce::File& f, int pat);    // analyse des accords d'un fichier MIDI
    bool harmonizeMelodyFile (const juce::File& f, int pat);    // harmonise une melodie MIDI (Dobbins)
    static bool isMelodyFile (const juce::File& f);             // fichier monophonique ?
    bool rhythmFromDrums (int pat, int mode);                   // rythme d'apres la batterie d'Alea
    static bool inScaleMode (int mode, bool minor);

    // aide a la composition (onglet THEORIE)
    color::theory::VoiceOptions voiceOptions (uint32_t hash = 1) const;
    int previewVoicing (const color::theory::Chord& c, int* notes, int maxN) const;

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
    juce::String currentPresetName { "Init Pads" };
    void resetToDefaults();
    void initDefaultBank();
    std::atomic<float> uiScale { 1.0f };

    int  editPattern() const;
    void setParamNorm (const juce::String& id, float norm);
    void setParamReal (const juce::String& id, float real);
    float paramReal (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    int keyRootFor (int pat) const { return (((int) rootRaw->load() + bank.pats[juce::jlimit (0, color::kPatterns - 1, pat)].keyShift.load()) % 12 + 12) % 12; }
    int keyRoot() const { return keyRootFor (editPattern()); }   // tonalite du pattern edite (tonique + modulation)
    int sharedKey() const;                                        // tonalite publiee aux autres instruments
    std::atomic<int> songShift { 0 };                             // transposition de la chaine (A4+5)
    void modulateToNext (int pat, int semis, int method);         // copie vers le pattern suivant dans la nouvelle tonalite + transition
    int mode() const { return (int) modeRaw->load(); }

    // position dans la progression
    struct ChordPos { int index = 0; double start = 0, end = 4; int64_t occurrence = 0; };
    ChordPos chordPosAt (int pattern, double patStart, double ppq) const;
    color::theory::Chord effectiveChord (int pattern, double patStart, double ppq, bool* sub = nullptr, bool* appr = nullptr) const;

private:
    void handleAsyncUpdate() override;
    void setupLink();
    void computeMods();
    void resolveParams();
    void evolveStep (int64_t bar);
    color::ColorContext makeContext (int pattern, double patStart, double bpm) const;
    juce::ValueTree captureTree();
    void restoreTree (const juce::ValueTree& state, bool keepTransport);
    float gmod (float raw, int g) const { return juce::jlimit (0.0f, 1.0f, raw + gOff[g]); }
    void publishChords (double ppqNow, double patStart, bool running);

    // moteur de notes
    struct NoteOn { int note; float vel; bool accent; double durPpq; int delay; float gain; float pan; int voiceHint; bool legato; };
    void hitToNotes (const color::ColorHit& h, double ppqNow, int sampleInBlock);
    void chordChanged (const color::theory::Chord& c, uint32_t hash, double ppqNow, double untilPpq, bool padMode, int sampleInBlock);
    int computeVoicing (const color::theory::Chord& c, uint32_t hash, int* notes, int maxN);
    void startNote (const NoteOn& n, double ppqNow, int sampleInBlock);
    void releaseVoice (int v, int sampleInBlock);
    void releaseAll (int sampleInBlock, bool hard = false);
    int allocVoice (int note);
    void applyStepLocks (const color::ColorHit& h);
    void keyChordFromHeld();

    color::ColorVoice voices[color::kVoices];
    double voiceOffPpq[color::kVoices] {};
    float voiceGain[color::kVoices] {};
    int voiceMidiOut[color::kVoices] {};
    bool voiceIsBass[color::kVoices] {};
    uint32_t ageCounter = 0;

    juce::MidiBuffer* midiOutBuf = nullptr;
    bool midiOutOn = true;

    struct Pending { NoteOn n; int remaining; };
    std::array<Pending, 64> pending {};
    int numPending = 0;

    alea::HalfbandDecimator decimL, decimR;
    color::Chorus chorus;
    alea::StereoDelay delay;
    alea::PlateReverb plate;
    alea::Glue glue;
    alea::RandomMods mods;
    color::ColorCondState condState;
    color::BassParams bp;
    juce::AudioBuffer<float> revBus;

    float noteJit[color::kNumTP] {};
    float noteLock[color::kNumTP] {};
    float drift[color::kNumTP + 1] {};
    float modOff[color::kNumTP] {};
    float gOff[color::kNumGT] {};
    float level = 0.5f, width = 0.7f, sDly = 0, sRev = 0;
    float chaosEff = 0.5f;

    std::array<color::ColorHit, 256> hits {};
    int numHits = 0;
    struct Boundary { double ppq; int offset; };
    std::array<Boundary, 32> bounds {};
    int numBounds = 0;
    std::array<int, 64> beatOffsets {};
    std::array<int64_t, 64> beatIndex {};
    int numBeats = 0;

    // voicing courant et conduite des voix
    int curNotes[8] {}; int curN = 0;
    int prevNotes[8] {}; int prevN = 0;
    int32_t curKey = 0; uint32_t curHash = 0; int curSig = 0;
    int arpCounter = 0, arpLast = 0;
    double lastPublish = -1.0;

    // clavier
    int heldKeys[16] {}; int numHeld = 0;
    bool keyChordActive = false;
    color::theory::Chord keyChord;
    float transpose = 0.0f;

    double sr = 44100.0;
    bool internalRunning = false, wasRunning = false;
    double internalPpq = 0.0, freeModClock = 0.0, patStartPpq = 0.0, tlNow = 0.0;
    int64_t songStartBar = 0;
    int requestedPattern = 0, lastParamPattern = 0, dlyDivCur = 6;
    float duckEnv = 0.0f, followEnv = 0.0f, dcXL = 0, dcYL = 0, dcXR = 0, dcYR = 0;
    double lastDuckPpq = -1.0, lastFollowPpq = -1.0;
    int scopeWrite = 1 << 30, scopeDecim = 24;

    juce::String chainText { "A4 B4" };
    std::atomic<int> chainPat[color::kChain], chainBars[color::kChain], chainShift[color::kChain], chainLen { 0 };
    bool songPublished = false;
    int keyRootEff() const;

    color::CPattern clip;
    bool clipValid = false;
    alea::FastRng rng { 0xC0102u };
    std::vector<juce::MemoryBlock> undoStack, redoStack;

    std::atomic<float>* tpRaw[color::kNumTP] {};
    std::atomic<float>* jitRaw[color::kNumTP] {};
    std::atomic<float>* typeRaw[kNumTypes] {};
    std::atomic<float>* modRateRaw[alea::kNumMods] {};
    std::atomic<float>* modPRaw[alea::kNumMods] {};
    std::atomic<float>* mxSrcRaw[color::kMxSlots] {};
    std::atomic<float>* mxDstRaw[color::kMxSlots] {};
    std::atomic<float>* mxAmtRaw[color::kMxSlots] {};
    std::atomic<float> *playRaw {}, *bpmRaw {}, *patternRaw {}, *seedRaw {}, *chaosRaw {}, *rndModeRaw {}, *rndDistRaw {},
                       *rootRaw {}, *scaleRaw {}, *modeRaw {}, *velSensRaw {}, *vdriftRaw {}, *vspreadRaw {}, *linkRndRaw {},
                       *styleRaw {}, *cycleRaw {}, *nchordsRaw {}, *chordLenRaw {}, *complexRaw {}, *tensionRaw {}, *chromaRaw {},
                       *outgoingRaw {}, *liveSubRaw {}, *approachRaw {}, *voicingRaw {}, *voicesRaw {}, *registerRaw {}, *spreadRaw {},
                       *smoothRaw {}, *bassModeRaw {}, *bassLvlRaw {}, *playModeRaw {}, *arpModeRaw {}, *arpOctRaw {}, *strumRaw {},
                       *swingRaw {}, *seqChaosRaw {}, *ratChaosRaw {}, *humanRaw {}, *noteChaosRaw {}, *densityRaw {}, *fillRaw {},
                       *morphRaw {}, *modDepthRaw {}, *songRaw {}, *midiOutRaw {}, *keysRaw {}, *turLenRaw {}, *followTrkRaw {},
                       *sceneARaw {}, *sceneBRaw {}, *sceneXfRaw {}, *sceneOnRaw {}, *evoAmtRaw {}, *evoBarsRaw {}, *evoTargetRaw {},
                       *chRateRaw {}, *chDepthRaw {}, *chMixRaw {},
                       *dlyDivRaw {}, *dlyFbRaw {}, *dlyToneRaw {}, *dlyPingRaw {}, *dlyRetRaw {}, *dlyRndRaw {},
                       *revSizeRaw {}, *revDampRaw {}, *revWidthRaw {}, *revRetRaw {}, *revPreRaw {},
                       *duckAmtRaw {}, *duckSrcRaw {}, *duckRelRaw {}, *glueRaw {}, *mDriveRaw {}, *masterRaw {},
                       *lkOnRaw {}, *lkPlayRaw {}, *lkTempoRaw {}, *lkSongRaw {}, *layerLvlRaw {}, *layerDecRaw {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorAudioProcessor)
};
