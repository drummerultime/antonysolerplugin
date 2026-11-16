#pragma once

// MARIA - memoire, analyse (segmentation + Factor Oracle) et improvisatrice.
#include "MaParams.h"
#include "Core.h"
#include "MaEngines.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <memory>

namespace ma
{
// ---------------------------------------------------------------------------
// Detecteur d'attaques (meme principe que Grot)
struct OnsetDetector
{
    void setup (const SourceProfile& p, float sens, double fs);
    float process (float x) noexcept;    // renvoie la velocite (0 = pas d'attaque)
    void reset() { fast = slow = lp = hp = fast2 = slow2 = hp2 = 0; refrac = 0; armed = armed2 = true; }
    float fast = 0, slow = 0, lp = 0, hp = 0;
    float fast2 = 0, slow2 = 0, hp2 = 0, cHp2 = 0.2f;   // bande aiguë (les attaques aigues ne sont pas masquees par une queue grave)
    bool armed2 = true, useHigh = false;
    float cLp = 0.5f, cHp = 0.01f, fAtk = 0, fRel = 0, sAtk = 0, sRel = 0, ratio = 2, floorLin = 0.001f;
    int refrac = 0, refracS = 1000;
    bool armed = true;
};

// ---------------------------------------------------------------------------
struct Segment
{
    int64_t start = 0, end = 0;   // positions absolues dans la memoire
    float pitch = -1.0f;          // note MIDI (-1 = non accorde)
    float rmsDb = -90.0f;
    float bright = 0.0f;          // 0..6
    int sym = 0;
    int chord = -1;               // accord reconnu (0-11 majeur, 12-23 mineur, 24-35 note seule), -1 = aucun
    // memoire MIDI : notes exactes de l'evenement
    uint8_t nn = 0, vel = 0;
    uint8_t notes[6] {};
};
juce::String chordName (int chord);

class Memory
{
public:
    // 262 144 evenements : plus de 5 h de batterie dense (65 536 s'epuisait vers 80 min de concert)
    static constexpr int kMaxStates = 262144;
    static constexpr double kLiveSeconds = 1800.0;   // memoire du direct : 30 min (chapitres et leitmotivs d'une demi-heure)

    void prepare (double sampleRate, double seconds, bool withoutAudio = false);
    bool isPrepared() const noexcept { return cap > 0; }
    bool audioless() const noexcept { return noAudio; }
    // memoire sans audio (MIDI) : le temps avance, les evenements sont ajoutes directement
    void advance (int n) noexcept { wc.fetch_add (n, std::memory_order_acq_rel); }
    void addSegment (const Segment& g) { addState (g); }   // ecrivain unique : le thread d'analyse
    void clearDirect() { doClear(); }
    double sampleRate() const noexcept { return fs; }
    int64_t capacity() const noexcept { return cap; }

    // --- ecriture (un seul ecrivain a la fois : thread audio, ou chargeur de fichier)
    void write (const float* x, int n) noexcept;
    int64_t writePos() const noexcept { return wc.load (std::memory_order_acquire); }
    inline float read (double pos) const noexcept
    {
        if (pos < 0.0 || noAudio) return 0.0f;
        const int64_t i0 = (int64_t) pos;
        const float f = (float) (pos - (double) i0);
        const float a = buf[(size_t) (i0 % cap)], b = buf[(size_t) ((i0 + 1) % cap)];
        return a + f * (b - a);
    }
    float sampleAt (int64_t pos) const noexcept { return noAudio ? 0.0f : buf[(size_t) (pos % cap)]; }
    void pushEvent (int64_t pos, int kind) noexcept;     // 0 attaque, 1 coupure
    float overviewAt (int64_t block) const noexcept { return ov[(size_t) (block % ovSize)].load (std::memory_order_relaxed); }
    static constexpr int kOvBlock = 1024;

    // --- oracle (ecrit par l'analyseur seul, lu par tous)
    int numStates() const noexcept { return nStates.load (std::memory_order_acquire); }
    const Segment& segment (int state) const noexcept { return segs[(size_t) juce::jmax (0, state - 1)]; }
    int sfx (int state) const noexcept { return sfxA[(size_t) state]; }
    int lrs (int state) const noexcept { return lrsA[(size_t) state]; }
    bool stateValid (int state) const noexcept;
    int firstValidState() const noexcept;
    int64_t oldestValidPos() const noexcept;
    int stateAtOrAfter (int64_t pos) const noexcept;     // premier etat qui commence a pos ou apres (numStates() si aucun)

    // --- analyse (thread de fond, ou appel direct dans les tests)
    void analyzeStep (const SourceProfile&, float gateDb);
    void requestClear() noexcept { clearReq.store (true); }
    std::atomic<float> lastPitch { -1.0f }, lastRmsDb { -90.0f };
    std::atomic<int> lastChord { -1 };
    std::atomic<int> clearCount { 0 };
    std::atomic<int64_t> clearPos { 0 };

private:
    void addState (const Segment&);
    void closeSegment (int64_t endPos, const SourceProfile&);
    void doClear();
    float yin (const float* x, int n, float sr, float fmin, float fmax, float& conf);

    double fs = 48000.0;
    int64_t cap = 0;
    bool noAudio = false;
    std::vector<float> buf;
    std::atomic<int64_t> wc { 0 };
    std::unique_ptr<std::atomic<float>[]> ov;
    int64_t ovSize = 1;
    float ovPeak = 0; int64_t ovBlock = 0;

    // evenements audio -> analyse
    static constexpr int kFifo = 8192;
    juce::AbstractFifo fifo { kFifo };
    std::vector<int64_t> evPos = std::vector<int64_t> (kFifo);
    std::vector<int> evKind = std::vector<int> (kFifo);

    // oracle
    std::vector<Segment> segs = std::vector<Segment> (kMaxStates);
    std::vector<int> sfxA = std::vector<int> (kMaxStates + 1), lrsA = std::vector<int> (kMaxStates + 1), symA = std::vector<int> (kMaxStates + 1);
    std::vector<std::vector<std::pair<int, int>>> trn = std::vector<std::vector<std::pair<int, int>>> (kMaxStates + 1);
    std::atomic<int> nStates { 1 };
    std::atomic<bool> clearReq { false };

    // etat de l'analyse
    int64_t apos = 0;
    bool inSeg = false;
    int64_t segStart = 0;
    std::vector<float> pitches;
    double rmsSum = 0, brightSum = 0; int frames = 0, voiced = 0;
    float refPitch = -1; int pitchDev = 0;
    float energyAvg = -30.0f;
    std::vector<std::pair<int64_t, int>> pending;
    std::vector<float> frame, dec;
    std::vector<float> yd;
    // analyse harmonique (instruments polyphoniques)
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftBuf, hann;
    double chromaSum[12] {};
    int chromaFrames = 0;
    void addChroma (const SourceProfile&);
    int matchChord() const;
};

// ---------------------------------------------------------------------------
// Ecoute en direct (independante de l'apprentissage) : ce que jouent les musiciens maintenant
struct LiveState
{
    std::atomic<bool> valid { false }, harmValid { false };
    std::atomic<float> pitch { -1.0f }, rmsDb { -90.0f };
    std::atomic<float> energy { 0.0f };      // 0..1 (lisse)
    std::atomic<float> density { 0.0f };     // attaques par seconde (lissees)
    std::atomic<int> chord { -1 };           // accord reconnu (meme codage que Segment::chord)
    std::atomic<float> chroma[12] {};        // profil harmonique lisse, max = 1
};

class LiveListener
{
public:
    void prepare (double sampleRate);
    // thread audio
    void push (const float* x, int n, const SourceProfile& p, float sens) noexcept;
    // thread de fond (ou tests)
    void analyze (const SourceProfile& p, float gateDb);
    LiveState state;
    static float energy01 (float db) noexcept { return juce::jlimit (0.0f, 1.0f, (db + 52.0f) / 40.0f); }
    static float density01 (float perSec) noexcept { return juce::jlimit (0.0f, 1.0f, std::log2 (juce::jmax (0.25f, perSec) / 0.5f) / 4.0f); }

private:
    static constexpr int kRing = 16384, kWin = 4096;
    double fs = 48000.0;
    std::vector<float> ring = std::vector<float> (kRing);
    std::atomic<int64_t> wc { 0 };
    int64_t lastAn = 0;
    OnsetDetector det;
    int detSrc = -1;
    float densAcc = 0.0f, envDb = -90.0f;
    // analyse
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> fftBuf, hann, win, dec, yd;
    float chroma[12] {};
    double silentFor = 10.0;
};

// ---------------------------------------------------------------------------
// Nappes : un fragment de la memoire gele en son tenu (grains), renouvele en fondu enchaine
class Drone
{
public:
    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset() { for (auto& l : layers) l = Layer(); cur = 0; xf = 1.0f; }
    // nouveau fragment [a, a + len) : fondu enchaine de "fadeSec"
    void setNext (Memory* m, double a, double len, float gain, double fadeSec, uint32_t seed);
    void process (float* L, float* R, int n, float level, float rate) noexcept;
    bool active() const noexcept { return layers[0].m != nullptr || layers[1].m != nullptr; }
    void stop (double fadeSec) noexcept { fadeOut = true; foStep = 1.0f / (float) juce::jmax (1.0, fadeSec * fs); }
    std::atomic<int> changes { 0 };
private:
    static constexpr int kGrains = 6;
    struct Grain { double pos = 0; float ph = 0, inc = 0, det = 1.0f, pan = 0.5f; };
    struct Layer { Memory* m = nullptr; double a = 0, len = 0; float gain = 0; Grain g[kGrains]; uint32_t rng = 1; };
    float rnd (Layer& l) noexcept { l.rng ^= l.rng << 13; l.rng ^= l.rng >> 17; l.rng ^= l.rng << 5; return (float) (l.rng >> 8) * (1.0f / 16777216.0f); }
    void spawn (Layer& l, Grain& g, float rate) noexcept;
    double fs = 48000.0;
    Layer layers[2];
    int cur = 0;
    float xf = 1.0f, xfStep = 0.0f, master = 1.0f, foStep = 0.0f;
    bool fadeOut = false;
};

// ---------------------------------------------------------------------------
// Pulsation des musiciens : tempo et phase deduits des attaques (sans clic)
class PulseTracker
{
public:
    void prepare (double sampleRate);
    void push (const float* x, int n) noexcept;          // thread audio : enveloppe d'attaques
    void analyze();                                       // thread de fond : tempo + phase
    // thread audio : horloge en noires calee sur la pulsation. Renvoie false si pas de pulsation fiable.
    bool clock (int n, double& ppqStart, double& ppsEff, double& bpmOut) noexcept;
    // thread audio : pulsation fiable ? tempo, secondes depuis le dernier temps, confiance
    bool estimate (double& bpm, double& secSinceBeat, float& conf) const noexcept
    {
        if (! estValid.load (std::memory_order_acquire)) return false;
        bpm = estBpm.load(); if (bpm <= 0.0) return false;
        double d = std::fmod (((double) samples - estBeat.load()) / (60.0 / bpm * fs), 1.0);
        if (d < 0) d += 1.0;
        secSinceBeat = d * 60.0 / bpm; conf = confView.load(); return true;
    }
    std::atomic<float> bpmView { 0.0f }, confView { 0.0f };
    std::atomic<bool> validView { false };
    std::atomic<float> phaseView { 0.0f };                // phase (0..1) de l'horloge au debut du dernier bloc
    std::atomic<int64_t> sampleView { 0 };                // echantillon correspondant

private:
    static constexpr int kFrames = 1024;                  // 10 s d'enveloppe (10 ms par trame)
    double fs = 48000.0;
    int hop = 480;
    std::vector<float> env = std::vector<float> (kFrames);
    std::atomic<int64_t> frameCount { 0 };
    int64_t samples = 0;
    int hopFill = 0; double hopE = 0, hopH = 0; float lp = 0, prevLe = -10, prevLh = -10;
    // estimation publiee
    std::atomic<double> estBpm { 0.0 }, estBeat { 0.0 };   // estBeat : echantillon (absolu) d'un temps
    std::atomic<bool> estValid { false };
    double curBpm = 0.0, candBpm = 0.0; int candHits = 0;
    int64_t lastAnalyzed = 0;
    std::vector<float> work = std::vector<float> (kFrames);
    // horloge
    double ppq = 0.0, clockBpm = 120.0;
};

// ---------------------------------------------------------------------------
// Ce que l'improvisatrice demande au processeur
struct ImHost
{
    virtual ~ImHost() = default;
    virtual int transpMode() const { return 0; }                                      // 0 vitesse (bande), 1 hauteur seule, 2 aleatoire
    virtual void noteEvent (int note, float vel, int sample, bool on) { juce::ignoreUnused (note, vel, sample, on); }
    virtual void segmentStarting (bool phraseStart) = 0;   // le hasard "chaque segment / phrase" se re-tire ici
    virtual const float* imValues() const = 0;             // valeurs effectives 0..1 des parametres (kNumIm)
    virtual int imMode() const = 0;
    virtual const LiveState* live() const { return nullptr; }        // ce que jouent les musiciens (ecoute en direct)
    virtual float harmAmount() const { return 0.0f; }                // reactivite harmonique 0..1
    virtual bool harmTranspose() const { return false; }             // transposer les fragments vers l'harmonie entendue
    virtual Memory* corpusMem() { return nullptr; }                  // corpus externe pret (nullptr sinon)
    virtual float corpusMix() const { return 0.0f; }                 // 0 = memoire du direct, 1 = corpus seul
    virtual float motifVar() const { return 0.35f; }                 // variation des leitmotivs
    // chapitres : plages de la memoire du direct ou elle a le droit de puiser (-1 = toute la memoire)
    virtual int chapterRanges (int64_t* starts, int64_t* ends, int maxRanges) const { juce::ignoreUnused (starts, ends, maxRanges); return -1; }
    // mode Machine : longueur de la boucle (en noires) et probabilite de mutation par tour
    virtual double machineBeats() const { return 4.0; }
    virtual float machineMutation() const { return 0.15f; }
};

// Calage rythmique de l'improvisatrice
struct TempoInfo
{
    bool sync = false;        // false = libre
    double ppq = 0.0;         // position (noires) du premier echantillon du sous-bloc
    double pps = 0.0;         // noires par echantillon
    double grid = 0.25;       // grille des evenements (en noires)
    double align = 1.0;       // depart des phrases (en noires)
};

class Improviser
{
public:
    void prepare (double sampleRate) { fs = sampleRate; shifter.prepare (sampleRate); reset(); }
    bool isPlaying() const noexcept { return inPhrase; }
    void reset();
    // n <= kSub echantillons ; out (mono) et hits (velocite au debut des segments) sont remplis
    void process (Memory&, ImHost&, const SourceProfile&, float* out, float* hits, int n,
                  bool playerActive, uint32_t seed, const TempoInfo& tempo = {});
    void forcePhrase() noexcept { forceReq.store (true); }
    void forceSilence() noexcept { silenceReq.store (true); }
    void playMotif (int lo, int hi) noexcept { motifReqHi.store (hi); motifReqLo.store (lo); }   // etats de la memoire du direct
    bool motifPlaying() const noexcept { return motifView.load(); }
    bool playingCorpus() const noexcept { return onCorpus.load(); }
    // pour les tests et l'interface
    std::atomic<int> motifPhrases { 0 }, motifSegs { 0 }, motifSegsInside { 0 }, corpusSegs { 0 }, liveSegs { 0 }, harmShifts { 0 };
    std::atomic<int> lastHarmShift { 0 };
    std::atomic<bool> chapterFilterView { false };
    std::atomic<int> machineCycles { 0 }, machineMutations { 0 }, machineLength { 0 };
    bool machinePlaying() const noexcept { return machView.load(); }

    std::atomic<int> curState { -1 }, jumpFrom { -1 }, jumpTo { -1 }, status { 0 }, segCount { 0 }, phraseCount { 0 };
    std::atomic<int> tmodeCount[2] { {0}, {0} };      // segments joues en bande / en hauteur seule
    enum { SEmpty = 0, SListen, SPlay, SWait };

private:
    struct Voice { bool on = false, handed = false, rev = false; int state = 0; double pos = 0, a = 0, b = 0, rate = 1, endPpq = 0; float env = 0, fadeStep = 0; int fadeDir = 0;
                   int notes[6] { -1, -1, -1, -1, -1, -1 }; bool notesOn = false; Memory* mem = nullptr; };
    void notesOff (ImHost&, Voice&, int sample);
    Shifter shifter;
    float shiftRatio = 1.0f;
    void startSegment (Memory&, ImHost&, const SourceProfile&, int voice, int state, bool phraseStart, float* hits, int i, const TempoInfo& t, double ppqNow);
    int freeVoice() const { if (! voices[0].on) return 0; if (! voices[1].on) return 1; return voices[0].handed ? 0 : 1; }
    int pendingState = -1;
    double pendingPpq = 0.0;
    bool pendingIsPhrase = false;
    int chooseStart (Memory&, const float* v, int mode);
    int chooseNext (Memory&, const float* v, int s);
    int chooseNextMotif (Memory&, const float* v, int s);
    int regionLo (Memory&, const float* v) const;
    float followWeight (Memory&, int state, float follow) const;
    float choiceWeight (Memory&, int state, float follow, const float* v) const;   // suivi + harmonie + contraste
    float harmFit (const Segment&, float semis, int& bestShift, bool allowShift) const;
    float contrastWeight (Memory&, int state) const;
    int steer (Memory&, const float* v, int next);              // harmonie / contraste : quitte une suite qui ne convient pas
    int pickVoice() const;
    // chapitres
    static constexpr int kMaxRanges = 128;
    int rngLo[kMaxRanges] {}, rngHi[kMaxRanges] {}, numRng = 0, rngTotal = 0;
    bool chapFilter = false;
    void updateChapters (Memory& m, ImHost& host);
    bool allowed (Memory& m, int s) const noexcept;
    int sampleAllowed();
    int sampleState (Memory& m, int lo, int n);
    // mode Machine
    static constexpr int kMachMax = 16;
    int machState[kMachMax] {}, machSteps[kMachMax] {}, machN = 0, machPos = 0, forcedSteps = 0;
    bool machActive = false;
    std::atomic<bool> machView { false };
    void buildMachine (Memory& m, const float* v, int s0, const TempoInfo& t);
    int machineNext (Memory& m, const float* v);
    // contexte du bloc courant
    ImHost* hostP = nullptr;
    Memory* liveMem = nullptr;
    Memory* phraseMem = nullptr;
    int curMode = 0;
    bool motifActive = false, motifDrift = false;
    int motifLo = 0, motifHi = 0;
    float motifVarCur = 0.35f, contrastGain = 1.0f;
    std::atomic<int> motifReqLo { -1 }, motifReqHi { -1 };
    std::atomic<bool> motifView { false }, onCorpus { false };
    float rnd() noexcept { return alea::hUni (counter++, 0x3a7u, seed); }

    double fs = 48000.0;
    Voice voices[2];
    bool inPhrase = false, stopReq = false, wasActive = false, pendingEcho = false, politeDecided = false;
    double phraseLeft = 0, gapLeft = 0, silenceTime = 0, activeTime = 0, echoTimer = 0;
    int echoStart = -1, echoEnd = -1, playerStartState = 1;
    float master = 0;
    uint32_t counter = 1, seed = 1;
    std::atomic<bool> forceReq { false }, silenceReq { false };
};
} // namespace ma
