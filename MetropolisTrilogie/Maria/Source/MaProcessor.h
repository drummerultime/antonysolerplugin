#pragma once

#include "MaParams.h"
#include "PulseBus.h"
#include "MaEngines.h"
#include "DSP/Modulators.h"
#include "KnobHost.h"
#include "MaMemory.h"
#include <juce_audio_devices/juce_audio_devices.h>

namespace ma
{
class MariaProcessor : public juce::AudioProcessor,
                         public alea::gui::KnobHost,
                         public ImHost,
                         private juce::Timer
{
public:
    MariaProcessor();
    ~MariaProcessor() override;

    // --- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Maria"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
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
    void recallScene (int s, bool fromSceneParam = false);   // fromSceneParam : rappel venu de l'automation "Scene" (ne pas la reecrire)
    void generateSceneVariations();
    void adaptToSource();
    bool sceneStored (int s) const { return scenes[(size_t) s].stored; }
    int  activeScene() const { return lastScene.load(); }
    juce::StringArray sceneParamIds() const { juce::StringArray a; for (auto* p : sceneParams) if (p != nullptr) a.add (p->getParameterID()); return a; }
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

    // --- Improvisatrice et memoire
    Memory mem;
    Improviser impro;
    std::atomic<bool> lockIm[kNumIm];
    std::atomic<float> liveIm[kNumIm];
    std::atomic<bool> playerView { false };
    std::atomic<float> playerLevel { 0.0f };
    void analyzeNow();                                // analyse synchrone (tests)
    void clearMemory() { mem.requestClear(); impro.forceSilence(); }
    void requestLoadFile (const juce::File& f, bool sessionRestore = false);   // charge un fichier audio dans la memoire (thread de fond)
    bool loadFileNow (const juce::File& f, bool sessionRestore = false);       // version synchrone
    bool saveMemory (const juce::File& f, int64_t from = -1);
    std::atomic<bool> loading { false };
    float imParam (int i) const { return pIm[i]->load(); }
    int imModeParam() const { return (int) std::round (pImMode->load()); }
    float gateDb() const { return -60.0f + (1.0f - pSens->load()) * 35.0f; }

    // ImHost
    void segmentStarting (bool phraseStart) override;
    const float* imValues() const override { return effIm; }
    int imMode() const override { return (int) std::round (pImMode->load()); }
    int transpMode() const override { return (int) std::round (pTMode->load()); }
    void noteEvent (int note, float vel, int sample, bool on) override { emitNote (0, note, vel, sample, on); }
    const LiveState* live() const override { return &listener.state; }
    float harmAmount() const override { return pHarm->load(); }
    bool harmTranspose() const override { return pHarmMode->load() > 0.5f; }
    Memory* corpusMem() override { return corpusReady.load (std::memory_order_acquire) ? &corpus : nullptr; }
    float corpusMix() const override { return pCorpusMix->load(); }
    float motifVar() const override { return pMotVar->load(); }
    int chapterRanges (int64_t* starts, int64_t* ends, int maxRanges) const override;
    double machineBeats() const override;
    float machineMutation() const override { return juce::jlimit (0.0f, 1.0f, pMachMut->load() + juce::jmax (0.0f, arcD) * 0.3f); }

    // --- Nappes
    Drone drone;
    std::atomic<int> droneStateView { -1 };
    std::atomic<bool> droneFromCorpus { false };

    // --- Sauvegarde de secours, fichiers de session
    juce::File recoveryAudioFile() const;
    juce::File recoveryStateFile() const;
    void autosaveNow();                               // ecrit la memoire de secours (thread message)
    std::atomic<bool> recoveryAvailable { false };
    juce::String recoveryInfo;
    void acceptRecovery();
    void dismissRecovery() { recoveryAvailable.store (false); }
    static juce::File sessionsFolder();
    static void sessionsFolderInfo (int& files, int64_t& bytes);
    static int cleanupSessions (int olderThanDays, const juce::String& keepId);   // vers la corbeille
    static int cleanupFolder (const juce::File& dir, int olderThanDays, const juce::String& keepId);
    juce::Time lastAutosave;

    // --- Retour d'etat MIDI (Wing, controleur)
    juce::String feedbackDevice() const { const juce::ScopedLock sl (fbLock); return fbDevId; }
    void setFeedbackDevice (const juce::String& identifier);   // "" = DAW + port virtuel
    std::atomic<int> feedbackSent { 0 };
    void resendFeedback() { fbResend.store (true); }

    // --- Pulsation des musiciens (mode de tempo 'Pulsation musiciens')
    PulseTracker pulse;

    // --- Chapitres de la memoire (comme les scenes du film)
    static constexpr int kMaxRegions = 512;
    int currentChapter() const { return (int) std::round (pChapCur->load()) + 1; }
    int chapterAt (int64_t pos) const;                     // chapitre d'une position de la memoire (0 = aucun)
    int numRegions() const { return regCount.load(); }
    int64_t regionStart (int i) const { return regStart[(size_t) i].load(); }
    int regionChapter (int i) const { return regChap[(size_t) i].load(); }
    double chapterSeconds (int chap) const;                // duree de ce chapitre encore en memoire
    void nextChapter() { setParamFromAnyThread ("chap_cur", (float) (currentChapter() % 16)); }
    void prevChapter() { setParamFromAnyThread ("chap_cur", (float) ((currentChapter() + 14) % 16)); }

    // --- Arc d'intensite
    std::atomic<float> arcView { 0.5f };
    void arcTo (float goal) { setParamFromAnyThread ("arc_goal", goal); }

    // --- Memoire MIDI (clavier) et voix MIDI
    Memory midiMem;
    Improviser improMidi;
    std::atomic<int> midiLearned { 0 };
    void clearMidiMemory() { pendingMidiClear.store (true); }
    bool keyboardActive() const { return kbView.load(); }

    // --- Ecoute en direct (harmonie, energie, densite des musiciens)
    LiveListener listener;
    bool sidechainConnected() const { return scChansView.load() > 0; }

    // --- Leitmotivs : passages marques de la memoire, rappeles et varies
    static constexpr int kNumMotifs = 4;
    void markMotif (int k) { if (k >= 0 && k < kNumMotifs) pendingMark.fetch_or (1 << k); }
    void triggerMotif (int k) { if (k >= 0 && k < kNumMotifs) pendingPlay.fetch_or (1 << k); }
    void clearMotif (int k) { if (k >= 0 && k < kNumMotifs) { motA[(size_t) k].store (-1); motB[(size_t) k].store (-1); } }
    float motifSeconds (int k) const;            // 0 = vide ou sorti de la memoire
    bool motifRange (int k, int64_t& a, int64_t& b) const { a = motA[(size_t) k].load(); b = motB[(size_t) k].load(); return motifSeconds (k) > 0.0f; }
    int motifStates (int k, int& lo, int& hi) const;   // nombre d'evenements du leitmotiv dans la memoire
    std::atomic<int> motifLastPlayed { -1 };

    // --- Corpus externe : une memoire preparee a l'avance, melangee (ou non) au direct
    Memory corpus;
    std::atomic<bool> corpusReady { false }, corpusLoading { false };
    void requestCorpusLoad (const juce::File& f);
    bool loadCorpusNow (const juce::File& f);
    void clearCorpus();
    juce::String corpusName() const { const juce::ScopedLock sl (loadLock); return corpusFile.getFileNameWithoutExtension(); }
    juce::File corpusPath() const { const juce::ScopedLock sl (loadLock); return corpusFile; }
    double corpusSeconds() const;

    // 2e voix
    Improviser impro2;
    bool voice2On() const { return pV2On->load() > 0.5f; }
    // memoire liee au set
    juce::File sessionMemoryFile() const;
    void serviceBackground() { analyzerTick(); }      // tests : execute le travail du thread de fond
    juce::String memoryId() const { return memId; }
    std::atomic<int> midiNotesSent { 0 };

    // --- Etat partage avec l'interface
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<bool> lockP[kNumSlots][kNumSP];
    std::atomic<bool> slotLock[kNumSlots];
    std::atomic<float> liveP[kNumSlots][kNumSP];
    std::atomic<float> modView[8];             // 5 mods + enveloppe + horloge + coup
    std::atomic<float> meterIn { 0 }, meterOut { 0 }, slotMeter[kNumSlots];
    std::atomic<int> beatView { 0 };
    std::atomic<int64_t> tickView { 0 };
    std::atomic<int> curTypeView[kNumSlots];   // type affiche (pour les libelles)
    std::atomic<float> chaosView { 0.5f };
    std::atomic<float> uiScale { 0.0f };   // 0 = taille automatique
    std::array<std::atomic<float>, 400> scopePk, scopeHit;   // entree (crete / 256 ech.) et coups detectes
    std::atomic<int> scopeW { 0 }, hitCountView { 0 };
    int sourceIndex() const { return juce::jlimit (0, kNumSources - 1, (int) std::round (pSource->load())); }

    int slotType (int s) const;
    float paramValue (int s, int p) const;     // valeur de base 0..1
    double hostBpm() const { return lastBpm.load(); }

    // Pour les tests
    int numUndo() const { return (int) undoStack.size(); }
    float effectiveValue (int s, int p) const { return liveP[s][p].load(); }
    float effectiveIm (int i) const { return liveIm[i].load(); }

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
    std::atomic<float>* pSource = nullptr, * pSens = nullptr, * pHitDec = nullptr;
    std::atomic<float>* pRowSrc[kNumRows] {}, * pRowDst[kNumRows] {}, * pRowAmt[kNumRows] {};
    std::atomic<float>* pIm[kNumIm] {}, * pImJ[kNumIm] {}, * pImMode = nullptr, * pLearn = nullptr;
    std::atomic<float>* pTempo = nullptr, * pGrid = nullptr, * pAlign = nullptr;
    const int pulseSlot = metro::PulseBus::get().claimSlot();   // pulsation partagee avec Rotwang / Grot
    std::atomic<float>* pSelf = nullptr, * pTMode = nullptr, * pV2On = nullptr, * pV2Mode = nullptr, * pV2Level = nullptr,
                      * pV2Transp = nullptr, * pV2Pan = nullptr, * pMidiOut = nullptr, * pAutosave = nullptr;
    std::atomic<float>* pChapCur = nullptr, * pChapPlay = nullptr, * pChapPick = nullptr, * pArcGoal = nullptr, * pArcTime = nullptr, * pArcDepth = nullptr,
                      * pMidiLearn = nullptr, * pMvOn = nullptr, * pMvMode = nullptr, * pMvCh = nullptr;
    std::atomic<float>* pMachLen = nullptr, * pMachMut = nullptr, * pDroneOn = nullptr, * pDroneLevel = nullptr, * pDroneTime = nullptr,
                      * pDroneReg = nullptr, * pFbCh = nullptr, * pSoft = nullptr;
    std::atomic<float>* pHarm = nullptr, * pHarmMode = nullptr, * pMotVar = nullptr, * pScLearn = nullptr, * pScReact = nullptr,
                      * pCorpusMix = nullptr, * pCorpusSrc = nullptr, * pOutRoute = nullptr;

    struct V2Host : public ImHost
    {
        explicit V2Host (MariaProcessor& pp) : p (pp) {}
        void segmentStarting (bool ph) override { p.segmentStarting (ph); }
        const float* imValues() const override
        {
            for (int k = 0; k < kNumIm; ++k) vals[k] = p.effIm[k];
            vals[ITransp] = juce::jlimit (0.0f, 1.0f, p.effIm[ITransp] + (p.pV2Transp->load() - 0.5f));
            vals[ILevel] = p.pV2Level->load();
            return vals;
        }
        int imMode() const override { return (int) std::round (p.pV2Mode->load()); }
        int transpMode() const override { return p.transpMode(); }
        void noteEvent (int note, float vel, int sample, bool on) override { p.emitNote (1, note, vel, sample, on); }
        const LiveState* live() const override { return p.live(); }
        float harmAmount() const override { return p.harmAmount(); }
        bool harmTranspose() const override { return p.harmTranspose(); }
        Memory* corpusMem() override { return p.corpusMem(); }
        float corpusMix() const override { return p.corpusMix(); }
        float motifVar() const override { return p.motifVar(); }
        MariaProcessor& p;
        mutable float vals[kNumIm] {};
    };
    V2Host v2host { *this };
    struct MidiVoiceHost : public ImHost
    {
        explicit MidiVoiceHost (MariaProcessor& pp) : p (pp) {}
        void segmentStarting (bool ph) override { p.segmentStarting (ph); }
        const float* imValues() const override { return p.effIm; }
        int imMode() const override { return (int) std::round (p.pMvMode->load()); }
        int transpMode() const override { return 0; }
        int chapterRanges (int64_t*, int64_t*, int) const override { return -1; }
        void noteEvent (int note, float vel, int sample, bool on) override { p.emitNote (2, note, vel, sample, on); }
        const LiveState* live() const override { return p.live(); }
        float harmAmount() const override { return p.harmAmount(); }
        bool harmTranspose() const override { return p.harmTranspose(); }
        float motifVar() const override { return p.motifVar(); }
        MariaProcessor& p;
    };
    MidiVoiceHost mvHost { *this };
    float mvOut[kSub] {}, mvHits[kSub] {};
    float voice2[kSub] {}, hits2[kSub] {};
    float outEnv = 0.0f;
    juce::MidiBuffer* outMidi = nullptr;
    int midiPos = 0;
    bool notesHanging[3][128] {};
    void emitNote (int voiceId, int note, float vel, int sample, bool on);
    juce::String memId;
    int64_t savedWrite = -1; int savedClear = -1;

    // Port MIDI virtuel "Maria" (macOS) : Logic, Live... le voient comme un clavier branche
    struct MidiEv { juce::uint8 s, d1, d2; double t; };
    struct VirtualMidi : public juce::Thread
    {
        VirtualMidi() : juce::Thread ("Maria MIDI") {}
        void run() override;
        bool push (const MidiEv& e) noexcept
        {
            int s1, n1, s2, n2;
            fifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) ev[(size_t) s1] = e; else if (n2 > 0) ev[(size_t) s2] = e; else return false;
            fifo.finishedWrite (1);
            return true;
        }
        std::unique_ptr<juce::MidiOutput> out;
        juce::AbstractFifo fifo { 4096 };
        std::array<MidiEv, 4096> ev {};
    };
    std::unique_ptr<VirtualMidi> vmidi;
    std::atomic<bool> vmidiReady { false };
    double blockStartMs = 0.0;
    void ensureVirtualMidi();
public:
    juce::String virtualMidiName() const { return vmidiReady.load() && vmidi != nullptr && vmidi->out != nullptr ? vmidi->out->getName() : juce::String(); }
private:
    std::atomic<bool> pendingSave { false };
    std::atomic<int64_t> saveBase { -1 };
    int64_t lastCaptureBase = 0;
    bool loadAudioInto (Memory& dst, const juce::File& f, const SourceProfile& prof, float gDb, bool raw, int64_t* startPos, double* ratio);
    // sidechain
    juce::AudioBuffer<float> scBuf;
    std::atomic<int> scChansView { 0 };
    float learnEnv = 0.0f; int holdL = 0;
    float reactIn[kSub] {};
    // leitmotivs
    std::array<std::atomic<int64_t>, kNumMotifs> motA, motB;
    std::atomic<int> pendingMark { 0 }, pendingPlay { 0 };
    bool phOpen = false; int64_t phStart = 0, phLast = 0, lastPhA = -1, lastPhB = -1; int phIdle = 0;
    std::array<std::pair<int64_t, int64_t>, kNumMotifs> pendingMotifOff;
    bool pendingMotifRestore = false, pendingLoadSession = false;
    void handleMotifRequests();
    // corpus
    juce::File corpusFile, pendingCorpus;
    std::atomic<bool> pendingCorpusClear { false };
    double corpusFs = 0.0;
    float voice2Buf[kSub] {};
    float sideMono[kSub] {}, trackMono[kSub] {};
    // chapitres
    std::array<std::atomic<int64_t>, kMaxRegions> regStart;
    std::array<std::atomic<int>, kMaxRegions> regChap;
    std::atomic<int> regCount { 0 };
    int lastChap = -1, lastClearSeen = -1;
    std::atomic<bool> chapReappend { false };
    std::vector<std::pair<int64_t, int>> pendingChapOff;
    bool pendingChapRestore = false;
    void appendRegion (int64_t pos, int chap);
    // arc
    double arcCur = 0.5, arcSpeed = 0.0; float lastGoal = -1.0f, arcD = 0.0f;
    // apprentissage MIDI
    struct KbEv { int64_t pos; int note, vel; };
    juce::AbstractFifo kbFifo { 2048 };
    std::array<KbEv, 2048> kbEv {};
    int kbHeld = 0; int64_t kbIdle = 1 << 30, kbSince = 1 << 30;
    std::atomic<bool> kbView { false }, pendingMidiClear { false };
    struct Learner { bool pend = false; int64_t start = 0, maxDur = 0, lastEv = 0; int nn = 0, vel = 0, held = 0; uint8_t notes[6] {}; int64_t onPos[128] {}; } lrn;
    void midiLearnStep();
    void closeMidiSegment (int64_t end);
    std::vector<Segment> pendingMidiRestore;
    bool pendingMidiRestoreFlag = false;
    void setParamFromAnyThread (const juce::String& id, float realValue);
    // nappes
    double droneLeft = 0.0;
    bool droneWasOn = false;
    float droneL[kSub] {}, droneR[kSub] {};
    void pickDroneFragment (bool first);
    uint32_t droneSeed = 1;
    // secours
    std::atomic<bool> pendingRecoverySave { false }, pendingReplaceLoad { false };
    std::atomic<int64_t> recoveryBase { 0 };
    int autosaveTicks = 0; int64_t lastAutosaveW = -1;
    // retour d'etat
    juce::CriticalSection fbLock;
    juce::String fbDevId;
    std::unique_ptr<VirtualMidi> fbOut;
    std::atomic<bool> fbOutReady { false }, fbResend { true }, fbDevChanged { false };
    int fbNote[128]; int fbCC[128];
    void sendFeedback (int sample);
    void fbSend (juce::uint8 status, int d1, int d2, int sample);
    void openFeedbackDevice();

    struct AnalyzerThread : public juce::Thread
    {
        explicit AnalyzerThread (MariaProcessor& p) : juce::Thread ("Maria analyse"), proc (p) {}
        void run() override { while (! threadShouldExit()) { proc.analyzerTick(); wait (15); } }
        MariaProcessor& proc;
    };
    void analyzerTick();
    void recomputeIm();
    std::unique_ptr<AnalyzerThread> analyzer;
    juce::CriticalSection loadLock;
    juce::File pendingLoad;
    OnsetDetector det;
    bool writing = false;
    int holdW = 0, holdP = 0;
    float playerEnv = 0, playerSm = 0, segEnv = 0;
    int segHits = 0;
    float effIm[kNumIm] {};
    float dstIm[kNumIm] {};
    float voice[kSub] {}, voiceHits[kSub] {}, monoIn[kSub] {};

    double fs = 48000.0;
    SlotRT slots[kNumSlots];
    alea::RandomMods mods;
    juce::AudioBuffer<float> work, dry;

    // horloge & hasard
    double freePpq = 0.0;
    int64_t tick = 0, lastTickIdx = INT64_MIN;
    double freeCountdown = 0.0;
    static constexpr int kNS = kNumSlots * kNumSP, kNP = kNS + kNumIm;
    float rndCur[kNP + 4] {}, rndTgt[kNP + 4] {};
    float evoCur[kNP] {}, evoTgt[kNP] {};
    int64_t lastEvo = INT64_MIN;
    float envIn = 0.0f, envMod = 0.0f, duckEnv = 0.0f;
    float dcL = 0, dcR = 0, dcxL = 0, dcxR = 0;
    float limGain = 1.0f;
    // detecteur de coups
    float scopeAcc = 0, scopeHitAcc = 0;
    int scopeCnt = 0;
    bool slotOpen[kNumSlots] { true, true, true, true };
    float slotGate[kNumSlots] { 1, 1, 1, 1 };
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
    std::atomic<int> pendingScene { -1 }, pendingDice { 0 }, pendingFreezeToggle { 0 }, pendingLearnToggle { 0 };

    // Undo
    std::vector<juce::ValueTree> undoStack;

    // Scenes
    std::array<Scene, kNumScenes> scenes;
    std::vector<juce::RangedAudioParameter*> sceneParams;
    std::vector<float> morphFrom, morphTo;
    double morphStart = 0.0, morphDur = 0.0;
    bool morphing = false;
    std::atomic<int> lastScene { -1 };
public:
    std::atomic<int> sceneBank { 0 };        // banque de 8 scenes affichee / pilotee par les notes 36-43
private:
    int lastSceneParam = 0;

    int currentProgram = 0;
    juce::Random msgRng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MariaProcessor)
};
} // namespace ma
