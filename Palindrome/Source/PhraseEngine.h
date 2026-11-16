#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <array>
#include "PhraseDSP.h"

// Moteur temps réel de Palindrome.
//
//  Modes de capture :
//   - Continue / Au seuil / Manuelle : chaque phrase capturée est rejouée N fois après le délai.
//   - Cycle  : capture -> délai -> N répétitions -> silence (plage min/max) -> nouvelle capture -> ...
//   - Banque : comme Cycle tant que la banque de N captures n'est pas pleine, puis la banque est
//              figée et rejouée pas à pas (séquence tapée ou hasard), avec un silence entre les pas.
//              Un emplacement peut être recapturé à la demande.
//  Hold : plus de nouvelle capture, la phrase en cours tourne en boucle.
//
//  Chaque phrase capturée est copiée dans sa propre mémoire par le thread de travail,
//  qui calcule aussi chaque relecture juste avant son heure de lecture.
//  Le thread audio ne fait que planifier et lire.
class PhraseEngine : private juce::Thread
{
public:
    enum CaptureMode  { Continuous = 0, Threshold, Manual, Cycle, Bank };
    enum PlayOrder    { Alternate = 0, AlternateStartReverse, AlternateStartNormal, ReverseOnly, NormalOnly, RandomDirection, Superimposed, TypedSequence };
    enum DirStep      { DirNormal = 0, DirReverse, DirBoth, DirRest };
    enum StereoMode   { StereoCentre = 0, ReverseLeft, ReverseRight, PingPong, StereoRandom };
    enum SpeedMode    { SpeedNormal = 0, SpeedHalf, SpeedDouble, SpeedAlternate, SpeedSlowingDown, SpeedSpeedingUp, SpeedRandom };
    enum Recapture    { RecaptureImmediate = 0, RecaptureThreshold };
    enum BankOrder    { BankSequence = 0, BankRandom };
    enum CaptureState { CapIdle = 0, CapArmed, CapRecording };
    enum FlowState    { FlowNone = 0, FlowCapture, FlowPlaying, FlowSilence };

    static constexpr int maxBankSize = 8;

    struct Settings
    {
        int captureMode = Threshold;
        float thresholdGain = 0.016f;
        double phraseSeconds = 6.0;
        double delaySeconds = 20.0;
        bool delayFromEnd = false;
        int order = AlternateStartReverse;
        int repetitions = 2;
        double repeatGap = 0.0;
        double silenceMin = 5.0;
        double silenceMax = 15.0;
        int recapture = RecaptureImmediate;
        int bankSize = 4;
        int bankOrder = BankSequence;
        int targetSlot = 0;
        bool hold = false;

        // synchro : départ des captures sur une grille (0 = libre)
        double gridSamples = 0.0;
        double samplesToNextGrid = 0.0;   // depuis le début du bloc
        double beatSamples = 0.0;

        // découpage & hasard
        float randomAmount = 0.3f;
        float sensitivity = 0.5f;
        float legato = 0.5f;
        int playMode = 0;              // 0 auto, 1 notes, 2 accords
        float fadeMs = 6.0f;
        float noteReverseProb = 0.0f;
        float skipProb = 0.0f;
        float repeatProb = 0.0f;
        float octaveProb = 0.0f;
        float melodicJitter = 0.0f;
        bool freezeRandom = false;

        // rythme
        float quantGridBeats = 0.0f;
        float quantAmount = 0.0f;
        float humanizeMs = 0.0f;

        // tonalité
        int keyRoot = 0;
        int scale = 0;
        float correction = 1.0f;
        int transpose = 0;
        int driftPerRep = 0;

        // écho
        float decayDb = -3.0f;
        float damping = 0.3f;
        float toneHz = 16000.0f;
        float lowCutHz = 40.0f;
        int speedMode = SpeedNormal;

        // violoning
        float attackMs = 0.0f;
        bool attackPerNote = true;
        int attackDirection = 0;
        int attackPattern = 0;
        float attackProb = 0.5f;

        // texture
        float textureMix = 0.0f;
        float grainMs = 100.0f;
        float grainDensity = 20.0f;
        float grainSpray = 0.3f;

        // espace
        int stereoMode = StereoCentre;
        float width = 1.0f;
        float diffusion = 0.0f;        // envoi vers la réverbération (0..1)
        bool diffusionGrow = true;     // l'envoi augmente au fil des répétitions

        // réglages par emplacement de banque
        std::array<int, maxBankSize> slotOrder {};      // -1 = réglage global
        std::array<int, maxBankSize> slotTranspose {};  // ajouté à la transposition globale
        std::array<int, maxBankSize> slotReps {};       // 0 = réglage global

        Settings() { slotOrder.fill (-1); slotTranspose.fill (0); slotReps.fill (0); }
    };

    static constexpr double maxPhraseSeconds    = 20.0;
    static constexpr double minDelaySeconds     = 1.0;
    static constexpr double maxDelaySeconds     = 60.0;
    static constexpr double calcMarginSeconds   = 0.5;   // temps réservé au calcul d'une relecture
    static constexpr double maxSilenceSeconds   = 120.0;
    static constexpr double maxRepeatGapSeconds = 10.0;
    static constexpr int maxRepetitions = 32;
    static constexpr int maxSequence    = 64;
    static constexpr int maxSlots   = 8;
    static constexpr int maxPhrases = 64;
    static constexpr int maxPasses  = 1024;
    static constexpr int numViews   = 16;

    PhraseEngine();
    ~PhraseEngine() override;

    void prepare (double sampleRate, int maxBlockSize);
    void release();
    bool isPrepared() const { return prepared.load(); }
    double getSampleRate() const { return sr; }

    // input : signal d'entrée ; wetOut : relecture directe ; sendOut : envoi vers la diffusion
    void process (const juce::AudioBuffer<float>& input, int startSample, int numSamples,
                  juce::AudioBuffer<float>& wetOut, juce::AudioBuffer<float>& sendOut, const Settings& settings);

    void requestCapture()  { captureRequest.store (true); }
    void requestNext()     { nextRequest.store (true); }
    void clearAll()        { clearRequest.store (true); }

    // séquence de la banque, ex. "3 2 4 1" (thread message)
    void setSequence (const juce::String& text);
    static juce::Array<int> parseSequence (const juce::String& text);

    // séquence de sens des répétitions, ex. "R N N R" (R reverse, N normal, X les deux, - silence)
    void setDirectionSequence (const juce::String& text);
    static juce::Array<int> parseDirectionSequence (const juce::String& text);

    // emplacements verrouillés : ni recapturés, ni vidés
    void setSlotLocked (int slot, bool locked);
    bool isSlotLocked (int slot) const { return (lockMask.load() >> juce::jlimit (0, maxBankSize - 1, slot)) & 1; }
    int  getLockMask() const { return lockMask.load(); }
    void setLockMask (int m) { lockMask.store (m & 0xff); }

    // import / export de la banque (thread message)
    bool copyBankAudio (int slot, juce::AudioBuffer<float>& out);
    void queueImport (int slot, const juce::AudioBuffer<float>& audio, double sourceRate);
    int  bankVersion (int slot) const { return bankVersions[(size_t) juce::jlimit (0, maxBankSize - 1, slot)].load(); }

    //== État lisible par l'interface ========================================
    struct PassView
    {
        std::atomic<int>   active   { 0 };
        std::atomic<int>   reverse  { 0 };
        std::atomic<int>   repIndex { 0 };
        std::atomic<int>   repCount { 0 };
        std::atomic<int>   bankSlot { -1 };
        std::atomic<float> startIn  { 0.0f };
        std::atomic<float> length   { 0.0f };
        std::atomic<float> pan      { 0.0f };   // -1..1 (ouverture comprise)
        std::atomic<float> levelDb  { 0.0f };   // niveau de la répétition
        std::atomic<float> speed    { 1.0f };
        std::atomic<int>   transpose { 0 };
    };
    std::array<PassView, numViews> passViews;

    std::atomic<int>   captureState    { CapIdle };
    std::atomic<int>   flowState       { FlowNone };
    std::atomic<float> flowRemaining   { 0.0f };
    std::atomic<float> flowSilenceTotal { 0.0f };
    std::atomic<int>   captureTarget   { -1 };
    std::atomic<bool>  captureIsReplace { false };
    std::atomic<float> captureElapsed  { 0.0f };
    std::atomic<float> captureLength   { 0.0f };
    std::atomic<float> inputPeak       { 0.0f };
    std::atomic<int>   pendingCount    { 0 };
    std::atomic<bool>  holding         { false };

    std::array<std::atomic<float>, maxBankSize> bankLengthView {};
    std::atomic<int>   bankPlayingSlot { -1 };
    std::atomic<int>   sequencePosView { -1 };

    std::atomic<int>   displaySlot     { -1 };
    std::atomic<float> displayProgress { 0.0f };

    bool getSlotInfo (int slot, pal::RenderInfo& out);

    // analyse de la dernière capture (découpage en notes, hauteurs) pour l'affichage
    struct AnalysisSnapshot
    {
        struct Mark { float start = 0, length = 0, midi = -1; bool poly = false, legato = false; };
        std::vector<float> peaks;
        std::vector<Mark> notes;
        float seconds = 0.0f;
        int version = 0;
    };
    bool getLatestAnalysis (AnalysisSnapshot& out, int knownVersion);   // false si rien de nouveau

private:
    void run() override;

    enum PhraseState { PhraseFree = 0, PhraseStoreRequested, PhraseStored, PhraseDead };

    struct Phrase
    {
        std::atomic<int> state { PhraseFree };
        std::atomic<int> generation { 0 };   // change à chaque nouveau contenu
        pal::PhraseAnalysis analysis;        // thread de travail
        juce::int64 captureStart = 0;
        int length = 0;
        int refs = 0;                   // relectures + maintien en banque (thread audio)
        juce::AudioBuffer<float> raw;   // thread de travail (ou import, sous rawLock)
    };

    enum SlotState { SlotFree = 0, SlotRequested, SlotRendering, SlotReady };

    struct Slot
    {
        std::atomic<int> state { SlotFree };
        int owner = -1;
        int phrase = -1;
        int length = 0;                 // longueur source
        int outLength = 0;              // longueur rendue (écrite par le thread de travail)
        pal::RenderParams params;
        juce::AudioBuffer<float> buffer;
        juce::SpinLock infoLock;
        pal::RenderInfo info;
    };

    enum PassState { PassUnused = 0, PassWaiting, PassRequested, PassPlaying };

    struct Pass
    {
        int state = PassUnused;
        int phrase = -1;
        int length = 0;                 // longueur jouée (dépend de la vitesse)
        juce::int64 playStart = 0;
        bool reverse = false;
        juce::uint32 seed = 0;
        int slot = -1;
        int repIndex = 0, repCount = 0;
        int bankSlot = -1;
        bool endsStep = false;
        float pan = 0.0f;
        float speed = 1.0f;
        int transposeOffset = 0;
    };

    enum Purpose { PurposeFree = 0, PurposeCycle, PurposeBankFill, PurposeBankReplace };

    juce::int64 snapToGrid (juce::int64 t, const Settings& s) const;
    void startCapture (juce::int64 at, const Settings& s, int purpose, int bankSlot);
    void armCapture (int purpose, int bankSlot);
    void beginFlowCapture (const Settings& s, bool immediate);
    void finishCapture (const Settings& s);
    int  allocatePhrase (juce::int64 start, int length);
    bool schedulePasses (int phraseIdx, juce::int64 first, const Settings& s, int bankSlot);
    bool playNextBankStep (juce::int64 at, const Settings& s);
    int  nextBankSlot (const Settings& s);
    bool bankFull (const Settings& s) const;
    int  firstEmptyBankSlot (const Settings& s) const;
    void setBankPhrase (int bankSlot, int phraseIdx);
    void unref (int phraseIdx);
    int  addPass (const Pass& p);
    void releasePass (Pass& p);
    void resetFlow (const Settings& s);
    double randomSilence (const Settings& s);
    void handleNextRequest (const Settings& s);
    void applyImports();
    void storePhrase (Phrase& ph);
    void publishAnalysis (const Phrase& ph);
    void refreshLatestAnalysis();
    void updateViews (const Settings& s);
    void keepLastPhrase (int phraseIdx, int bankSlot);
    int  replaceTarget (const Settings& s) const;

    double sr = 48000.0;
    int historyLength = 0;
    juce::AudioBuffer<float> history;
    juce::int64 now = 0;
    juce::int64 gridOrigin = 0;      // une ligne de grille connue (échantillons absolus)

    std::array<Phrase, maxPhrases> phrases;
    std::array<Slot, maxSlots> slots;
    std::array<Pass, maxPasses> passes;

    int capState = CapIdle;
    juce::int64 capStart = 0;
    int capLength = 0;
    int capPurpose = PurposeFree;
    int capBankSlot = -1;

    int flow = FlowNone;
    juce::int64 resumeAt = 0;
    bool stepDone = false;
    bool skipSilence = false;
    std::array<int, maxBankSize> bank;
    int seqPos = 0;
    int lastBankSlot = -1;

    bool alternateReverse = true;
    int heldPhrase = -1, heldBankSlot = -1;   // dernière phrase jouée (Hold pendant un silence)
    int lastMode = -1;
    juce::Random rng;

    std::array<std::atomic<int>, maxSequence> sequence {};
    std::atomic<int> sequenceLength { 0 };
    std::array<std::atomic<int>, maxSequence> dirSequence {};
    std::atomic<int> dirSequenceLength { 0 };
    std::atomic<int> lockMask { 0 };

    // réglages d'analyse courants : la phrase est analysée dès sa capture
    std::atomic<float> anSensitivity { 0.5f }, anLegato { 0.5f };
    std::atomic<int> anPlayMode { 0 };

    // thread de travail : dernière capture et son analyse publiée
    int latestPhrase = -1;
    juce::uint32 settingsChangedAt = 0;
    float seenSens = -1.0f, seenLegato = -1.0f;
    int seenMode = -1;
    juce::SpinLock analysisLock;
    AnalysisSnapshot latestAnalysis;

    // import de la banque (thread message -> thread audio)
    std::array<std::atomic<int>, maxBankSize> importReady {};
    std::array<juce::AudioBuffer<float>, maxBankSize> importBuffers;
    std::array<std::atomic<int>, maxBankSize> bankPhraseView {};
    std::array<std::atomic<int>, maxBankSize> bankVersions {};
    juce::CriticalSection rawLock;

    std::atomic<bool> captureRequest { false };
    std::atomic<bool> nextRequest    { false };
    std::atomic<bool> clearRequest   { false };
    std::atomic<bool> prepared       { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhraseEngine)
};
