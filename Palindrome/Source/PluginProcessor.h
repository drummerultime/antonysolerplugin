#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "PhraseEngine.h"
#include "WetFX.h"

namespace ParamIDs
{
    // capture / cycle
    inline constexpr const char* captureMode = "captureMode";
    inline constexpr const char* threshold   = "threshold";
    inline constexpr const char* phraseLen   = "phraseLength";
    inline constexpr const char* delay       = "delay";
    inline constexpr const char* delayFrom   = "delayFrom";
    inline constexpr const char* repetitions = "repetitions";
    inline constexpr const char* repeatGap   = "repeatGap";
    inline constexpr const char* silenceMin  = "silenceMin";
    inline constexpr const char* silenceMax  = "silenceMax";
    inline constexpr const char* recapture   = "recapture";
    inline constexpr const char* bankSize    = "bankSize";
    inline constexpr const char* bankOrder   = "bankOrder";
    inline constexpr const char* targetSlot  = "targetSlot";
    inline constexpr const char* sequenceProp = "sequence";   // propriété texte (non automatisable)

    // tempo
    inline constexpr const char* syncMode    = "syncMode";
    inline constexpr const char* phraseBeats = "phraseBeats";
    inline constexpr const char* delayBeats  = "delayBeats";
    inline constexpr const char* gapBeats    = "gapBeats";
    inline constexpr const char* silMinBeats = "silMinBeats";
    inline constexpr const char* silMaxBeats = "silMaxBeats";
    inline constexpr const char* captureGrid = "captureGrid";
    inline constexpr const char* tempoSource = "tempoSource";
    inline constexpr const char* internalBpm = "internalBpm";

    // contrôle (mappables en MIDI dans le DAW)
    inline constexpr const char* trigger     = "trigger";
    inline constexpr const char* clearTrig   = "clearTrig";
    inline constexpr const char* nextStep    = "nextStep";
    inline constexpr const char* hold        = "hold";

    // lecture & hasard
    inline constexpr const char* order       = "order";
    inline constexpr const char* randomness  = "randomness";
    inline constexpr const char* noteReverse = "noteReverse";
    inline constexpr const char* skipProb    = "skipProb";
    inline constexpr const char* repeatProb  = "repeatProb";
    inline constexpr const char* octaveProb  = "octaveProb";
    inline constexpr const char* melodic     = "melodicJitter";
    inline constexpr const char* freeze      = "freezeRandom";
    inline constexpr const char* sensitivity = "sensitivity";
    inline constexpr const char* legato      = "legato";
    inline constexpr const char* playMode    = "playMode";
    inline constexpr const char* fade        = "fade";
    inline constexpr const char* quantGrid   = "quantGrid";
    inline constexpr const char* quantAmount = "quantAmount";
    inline constexpr const char* humanize    = "humanize";

    // tonalité
    inline constexpr const char* keyRoot     = "keyRoot";
    inline constexpr const char* scale       = "scale";
    inline constexpr const char* correction  = "correction";
    inline constexpr const char* transpose   = "transpose";
    inline constexpr const char* drift       = "drift";

    // écho
    inline constexpr const char* decay       = "decay";
    inline constexpr const char* damping     = "damping";
    inline constexpr const char* tone        = "tone";
    inline constexpr const char* lowCut      = "lowCut";
    inline constexpr const char* speedMode   = "speedMode";
    inline constexpr const char* modDepth    = "modDepth";
    inline constexpr const char* modRate     = "modRate";
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* feedback    = "feedback";

    // espace
    inline constexpr const char* stereoMode  = "stereoMode";
    inline constexpr const char* width       = "width";
    inline constexpr const char* diffusion   = "diffusion";
    inline constexpr const char* diffusionGrow = "diffusionGrow";
    inline constexpr const char* roomSize    = "roomSize";
    inline constexpr const char* duckAmount  = "duckAmount";
    inline constexpr const char* duckRelease = "duckRelease";

    // violoning
    inline constexpr const char* attack      = "attack";
    inline constexpr const char* attackScope = "attackScope";
    inline constexpr const char* attackDir   = "attackDir";
    inline constexpr const char* attackPattern = "attackPattern";
    inline constexpr const char* attackProb  = "attackProb";

    // texture
    inline constexpr const char* textureMix  = "textureMix";
    inline constexpr const char* grainSize   = "grainSize";
    inline constexpr const char* grainDensity = "grainDensity";
    inline constexpr const char* grainSpray  = "grainSpray";

    // sortie
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* wetGain     = "wetGain";
    inline constexpr const char* killDry     = "killDry";

    // propriétés texte / état (non automatisables)
    inline constexpr const char* dirSequenceProp = "dirSequence";
    inline constexpr const char* locksProp       = "slotLocks";
    inline constexpr const char* uiSizeProp      = "uiSize";

    // réglages par emplacement de banque (k = 1..8)
    inline juce::String slotOrder (int k)     { return "slotOrder" + juce::String (k); }
    inline juce::String slotTranspose (int k) { return "slotTranspose" + juce::String (k); }
    inline juce::String slotReps (int k)      { return "slotReps" + juce::String (k); }
}

//==============================================================================
// Commandes MIDI apprises (MIDI learn) : CC ou note -> réglage ou action
namespace MidiActions
{
    inline constexpr const char* capture = "@capture";
    inline constexpr const char* next    = "@next";
    inline constexpr const char* clear   = "@clear";
    inline constexpr const char* tap     = "@tap";
    inline constexpr const char* undo    = "@undo";
    inline constexpr const char* abSwap  = "@ab";
}

class PalindromeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PalindromeProcessor();
    ~PalindromeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Palindrome"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return PhraseEngine::maxDelaySeconds + PhraseEngine::maxPhraseSeconds; }

    // presets d'usine exposés comme programmes de l'hôte
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    PhraseEngine engine;

    static juce::StringArray noteNames();

    juce::String getSequenceText() const;
    void setSequenceText (const juce::String& text);

    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    // presets utilisateur
    static juce::File getUserPresetFolder();
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    void loadUserPreset (const juce::File& file);

    // banque <-> fichiers
    bool importFileToSlot (int slot, const juce::File& file);
    juce::File exportSlotToFile (int slot);

    // tempo (pour l'affichage)
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<double> currentPpq { 0.0 };
    std::atomic<double> currentBeatsPerBar { 4.0 };
    std::atomic<bool> tempoFromHost { false };
    void tapTempo();                       // thread message ou audio

    // Vider avec un fondu
    void requestClear() { clearRequested.store (true); }

    // séquence de sens "R N N R"
    juce::String getDirSequenceText() const;
    void setDirSequenceText (const juce::String& text);

    // verrous de la banque
    bool isSlotLocked (int k) const { return engine.isSlotLocked (k); }
    void setSlotLocked (int k, bool locked);

    //== MIDI learn (thread message) ==
    void startMidiLearn (const juce::String& target);
    void cancelMidiLearn();
    juce::String getLearningTarget() const;
    juce::String getMidiMappingText (const juce::String& target) const;   // "CC 64", "Note C3" ou ""
    void forgetMidiMapping (const juce::String& target);
    void forgetAllMidiMappings();
    juce::StringArray getMappedTargets() const;
    static juce::String describeTarget (const PalindromeProcessor&, const juce::String& target);
    std::atomic<int> midiActivity { 0 };   // incrémenté à chaque message reçu
    std::atomic<int> learnVersion { 0 };   // incrémenté à chaque apprentissage terminé

    //== annuler / rétablir, comparaison A/B (thread message) ==
    void undo();
    void redo();
    bool canUndo() const;
    bool canRedo() const;
    void selectAB (int which);
    void copyToOtherAB();
    int  getCurrentAB() const { return abCurrent; }

    // travail du thread message (appelé par le minuteur ; public pour les tests)
    void serviceMessageThread() { timerCallback(); }

    //== réglages globaux (taille de l'interface, MIDI par défaut) ==
    juce::PropertiesFile& globalSettings();
    juce::Point<int> getUiSize() const;     // taille de la fenêtre (mémorisée)
    void setUiSize (juce::Point<int>);

private:
    void timerCallback() override;
    void handleMidi (const juce::MidiBuffer&);
    void applyMidiValue (int code, int number, bool isNote, int value);
    int  codeForTarget (const juce::String& target) const;
    juce::String targetForCode (int code) const;
    juce::XmlElement* writeMidiMap (juce::XmlElement& parent) const;
    void readMidiMap (const juce::XmlElement* xml);
    void saveMidiMapGlobally();

    struct Snapshot
    {
        std::vector<float> values;
        juce::String sequence, dirSequence;
        bool operator== (const Snapshot& o) const { return values == o.values && sequence == o.sequence && dirSequence == o.dirSequence; }
        bool operator!= (const Snapshot& o) const { return ! (*this == o); }
    };
    Snapshot captureSnapshot() const;
    void applySnapshot (const Snapshot&);
    void updateHistory();

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyParamValues (const juce::ValueTree& paramsTree);
    void queuePendingBank();

    juce::AudioBuffer<float> wetBuffer, sendBuffer, inputMix, prevWet;
    int prevWetLen = 0;
    juce::SmoothedValue<float> dryGain, wetGainSmoothed;
    WetFX wetFX;
    juce::Reverb reverb;
    float duckEnv = 0.0f;
    bool lastTrigger = false, lastClear = false, lastNext = false;

    // Vider en fondu
    std::atomic<bool> clearRequested { false };
    int clearStage = 0;              // 0 rien, 1 fondu, 2 moteur vidé au prochain bloc
    float clearGain = 1.0f;

    // horloge interne (transport arrêté ou tempo interne)
    double internalPpq = 0.0;
    std::atomic<bool> tapReset { false };
    std::atomic<double> pendingTapBpm { 0.0 };
    juce::SpinLock tapLock;
    double lastTapTime = 0.0;
    std::array<double, 4> tapIntervals {};
    int tapCount = 0;

    // MIDI learn
    static constexpr int noMap = -1, actionBase = 100000;
    std::vector<juce::RangedAudioParameter*> mappable;   // réglages assignables
    std::array<std::atomic<int>, 128> ccMap {}, noteMap {};
    std::array<bool, 128> ccHigh {};
    std::atomic<int> learnCode { noMap };
    struct ParamChange { int index; float value; };
    juce::AbstractFifo midiFifo { 256 };
    std::array<ParamChange, 256> midiChanges {};
    std::atomic<int> pendingUndo { 0 }, pendingAB { 0 };
    int lastLearnSeen = 0;

    // historique
    std::vector<juce::RangedAudioParameter*> snapshotParams;
    std::vector<Snapshot> history;
    int historyIndex = -1;
    Snapshot lastSeen;
    int stableTicks = 0, historyTick = 0;
    std::array<Snapshot, 2> abSlots;
    std::array<bool, 2> abValid { false, false };
    int abCurrent = 0;

    std::unique_ptr<juce::PropertiesFile> settingsFile;
    int currentProgram = 0;

    // banque restaurée en attente de préparation du moteur
    struct PendingSlot { int slot; juce::AudioBuffer<float> audio; double rate; };
    std::vector<PendingSlot> pendingBank;
    juce::CriticalSection pendingLock;

    // cache d'encodage FLAC de la banque (évite de réencoder à chaque sauvegarde)
    std::array<juce::String, PhraseEngine::maxBankSize> bankCache;
    std::array<int, PhraseEngine::maxBankSize> bankCacheVersion {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PalindromeProcessor)
};
