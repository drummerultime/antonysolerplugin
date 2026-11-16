#pragma once

// AleaLink : synchronisation automatique entre toutes les instances d'Alea et de Talea
// ouvertes sur la machine (Alea, Talea, Color ; meme DAW ou plusieurs applications autonomes).
//
// Principe : un petit fichier projete en memoire (mmap), partage par tous les processus.
//  - chaque instance y reserve une "place" (Peer) qu'elle garde vivante par un battement regulier ;
//  - un "groupe" contient les valeurs partagees (tempo, tonalite, pattern, fill, chaos...) :
//    la derniere instance qui modifie une valeur la publie, les autres l'adoptent ;
//  - Alea y publie ses coups de batterie a venir (2 temps d'avance) et ses patterns :
//    Talea s'en sert pour le sidechain, la modulation "Drums" et ses generateurs de lignes ;
//  - une horloge partagee synchronise la lecture interne (applications autonomes, DAW a l'arret).
// Tout est sans verrou (atomiques), utilisable depuis le thread audio.

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace alea::link
{
constexpr uint32_t kMagic = 0xA1EA3006u;   // v3 : accords, grille, forme (song), Cantus
constexpr int kMaxPeers = 16;
constexpr int kMaxHits = 64;
constexpr int kNumDrumTracks = 8;
constexpr int kMaxChordEvents = 8;
constexpr int kMaxProg = 16;
constexpr int kMaxSong = 32;

enum Kind : int32_t { KindNone = 0, KindAlea = 1, KindTalea = 2, KindColor = 3, KindCantus = 4 };
const char* kindName (int k);

enum Field : int { FTempo = 0, FPlay, FKey, FScale, FPattern, FFill, FChaos, FSwing, FScene, FEvolve, FDensity, FMorph, FSeed, FSong, kNumFields };

const char* fieldLabel (int f);       // libelle (interface)
const char* fieldParamId (int f);     // parametre d'activation "lk_..."
bool fieldDefaultOn (int f);

struct Group
{
    std::atomic<uint32_t> ver[kNumFields];
    std::atomic<uint32_t> writer[kNumFields];
    std::atomic<double>   val[kNumFields];
    // horloge partagee
    std::atomic<uint32_t> clockOwner;
    std::atomic<double>   clockPpq, clockBpm;
    std::atomic<int64_t>  clockTicks;
    std::atomic<int32_t>  clockPlaying;
};

struct Peer
{
    std::atomic<uint32_t> id;
    std::atomic<int64_t>  heartbeatMs;
    std::atomic<int32_t>  kind, linked;
    std::atomic<double>   bpm, ppq;
    std::atomic<int32_t>  playing, pattern, root, scale;
    std::atomic<float>    level;
    // coups de batterie a venir (Alea) : seqlock (impair = ecriture en cours)
    std::atomic<uint32_t> hitSeq;
    std::atomic<int32_t>  numHits;
    std::atomic<double>   hitPpq[kMaxHits];
    std::atomic<int32_t>  hitTrack[kMaxHits];
    std::atomic<float>    hitVel[kMaxHits];
    // pattern de batterie courant
    std::atomic<uint32_t> trackMask[kNumDrumTracks];
    std::atomic<int32_t>  trackLen[kNumDrumTracks], trackRate[kNumDrumTracks];
    // accords (Color) : accord courant et changements a venir, en hauteurs ABSOLUES (0..11)
    std::atomic<uint32_t> chordSeq;
    std::atomic<int32_t>  numChords;
    std::atomic<double>   chordPpq[kMaxChordEvents];
    std::atomic<int32_t>  chordRoot[kMaxChordEvents], chordBass[kMaxChordEvents], chordMask[kMaxChordEvents], chordScale[kMaxChordEvents];
    // grille complete du pattern courant (Color) : accords successifs avec leur duree en temps
    std::atomic<uint32_t> progSeq;
    std::atomic<int32_t>  progN;
    std::atomic<double>   progStart;               // position (temps) du debut de la grille
    std::atomic<int32_t>  progRoot[kMaxProg], progBass[kMaxProg], progMask[kMaxProg], progScale[kMaxProg], progBeats[kMaxProg];
    // forme (song mode) : chaine de patterns, mesure de depart
    std::atomic<uint32_t> songSeq;
    std::atomic<int32_t>  songOn, songN;
    std::atomic<int64_t>  songStartBar;
    std::atomic<int32_t>  songPat[kMaxSong], songBars[kMaxSong];
};

struct Region
{
    std::atomic<uint32_t> magic;
    std::atomic<uint32_t> reserved;
    Group group;
    Peer peers[kMaxPeers];
};

struct Hit { double ppq = 0; int track = 0; float vel = 1.0f; };
struct ChordEvent { double ppq = 0; int root = 0; int bass = 0; int mask = 0; int scale = 0; };   // masks : 12 bits absolus (accord, gamme de l'accord)
struct ProgChord { int root = 0, bass = 0, mask = 0, scale = 0, beats = 4; };

struct PeerInfo
{
    int slot = -1;
    Kind kind = KindNone;
    bool linked = false, playing = false;
    double bpm = 0, ppq = 0;
    int pattern = 0, root = 0, scale = 0;
    float level = 0;
};

class Link
{
public:
    explicit Link (Kind kind, const juce::File& sharedFile = defaultFile());
    ~Link();

    static juce::File defaultFile();

    bool connected() const noexcept { return region != nullptr && slot >= 0; }
    uint32_t id() const noexcept { return myId; }
    int number() const noexcept { return slot + 1; }
    Kind kind() const noexcept { return myKind; }
    Peer* self() noexcept { return connected() ? &region->peers[slot] : nullptr; }

    void heartbeat();
    static bool alive (const Peer& p) noexcept;

    juce::Array<PeerInfo> peers (bool includeSelf = false) const;
    int countLinkedPeers() const;
    int firstPeerOfKind (Kind k) const;                   // place d'un pair lie et vivant, ou -1

    // champs du groupe
    void write (Field f, double v);
    bool readNew (Field f, uint32_t& seenVer, double& v) const;   // nouvelle valeur ecrite par un autre
    uint32_t version (Field f) const noexcept;

    // coups de batterie
    void publishHits (const Hit* hits, int n);
    int readHits (int peerSlot, Hit* out, int maxN) const;
    void publishTrack (int t, uint32_t mask, int len, int rate);
    bool readTrack (int peerSlot, int t, uint32_t& mask, int& len, int& rate) const;

    // accords (publies par Color, lus par Talea / Alea)
    void publishChords (const ChordEvent* c, int n);
    int readChords (int peerSlot, ChordEvent* out, int maxN) const;
    bool chordAt (int peerSlot, double ppq, ChordEvent& out) const;   // accord en vigueur a cette position
    void publishProgression (const ProgChord* c, int n, double startPpq);
    int readProgression (int peerSlot, ProgChord* out, int maxN, double& startPpq) const;

    // forme partagee : l'instance en song mode publie sa chaine ; les autres (LINK > Song) la suivent
    void publishSong (bool on, int64_t startBar, const int* pats, const int* bars, int n);
    bool songPatternAt (int64_t absBar, int& pattern) const;   // pattern de la forme publiee par un autre pair

    // horloge partagee pour la lecture interne. Renvoie la position a utiliser.
    // Si un autre pair joue deja, on suit sa position ; sinon on devient la reference.
    double syncClock (bool playing, double localPpq, double bpm, bool& followed);

private:
    Region* region = nullptr;
    std::unique_ptr<juce::MemoryMappedFile> mmf;
    int slot = -1;
    uint32_t myId = 0;
    Kind myKind;
};

// ---------------------------------------------------------------------------
// Parametres d'activation (a ajouter au layout de chaque plugin)
void addLinkParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout);

// Synchronisation des parametres (thread message, 20 Hz) : adopte les valeurs des autres,
// publie les changements locaux. Les valeurs d'une session precedente sont ignorees.
class LinkSync : private juce::Timer
{
public:
    LinkSync (Link& l, juce::AudioProcessorValueTreeState& s);
    ~LinkSync() override { stopTimer(); }

    void bind (Field f, std::function<double()> get, std::function<void (double)> set);
    std::function<void()> onTick;     // travail periodique supplementaire (publication des patterns...)
    bool masterOn() const;

private:
    void timerCallback() override;
    Link& link;
    juce::AudioProcessorValueTreeState& state;
    std::function<double()> getters[kNumFields];
    std::function<void (double)> setters[kNumFields];
    uint32_t seen[kNumFields] {};
    double last[kNumFields] {};
    bool joined = false;
};

} // namespace alea::link
