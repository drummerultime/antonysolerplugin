#include "AleaLink.h"

namespace alea::link
{
const char* fieldLabel (int f)
{
    static const char* n[kNumFields] { "Tempo", "Lecture", "Tonique", "Gamme", "Pattern", "Fill", "Chaos", "Swing",
                                       "Scene", "Evolve", "Density", "Morph", "Seed", "Song (forme)" };
    return n[juce::jlimit (0, kNumFields - 1, f)];
}

const char* fieldParamId (int f)
{
    static const char* n[kNumFields] { "lk_tempo", "lk_play", "lk_root", "lk_scale", "lk_pattern", "lk_fill", "lk_chaos", "lk_swing",
                                       "lk_scene", "lk_evo", "lk_density", "lk_morph", "lk_seed", "lk_song" };
    return n[juce::jlimit (0, kNumFields - 1, f)];
}

bool fieldDefaultOn (int f)
{
    switch (f)
    {
        case FTempo: case FPlay: case FKey: case FScale: case FPattern: case FFill: case FSwing: case FScene: case FSong: return true;
        default: return false;
    }
}

void addLinkParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    auto g = std::make_unique<juce::AudioProcessorParameterGroup> ("link", "Link", "|");
    g->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "lk_on", 1 }, "Link", true));
    for (int f = 0; f < kNumFields; ++f)
        g->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { fieldParamId (f), 1 },
                                                                juce::String ("Link ") + fieldLabel (f), fieldDefaultOn (f)));
    layout.add (std::move (g));
}

// ---------------------------------------------------------------------------
juce::File Link::defaultFile()
{
    if (auto* e = std::getenv ("ALEALINK_FILE"))                                           // tests
        return juce::String (e) == "off" ? juce::File() : juce::File (juce::String (e));
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile (juce::String ("Application Support")).getChildFile ("AleaLink").getChildFile ("link-v3.shm");
}

Link::Link (Kind kind, const juce::File& file) : myKind (kind)
{
    if (file == juce::File()) return;            // AleaLink desactive (ALEALINK_FILE=off)
    file.getParentDirectory().createDirectory();
    const auto needed = (juce::int64) sizeof (Region);
    if (! file.existsAsFile() || file.getSize() < needed)
    {
        // agrandit le fichier avec des zeros (ne tronque jamais : d'autres processus peuvent l'utiliser)
        juce::FileOutputStream os (file);
        if (os.openedOk())
        {
            os.setPosition (juce::jmax ((juce::int64) 0, file.getSize()));
            juce::HeapBlock<char> zeros ((size_t) 65536, true);
            for (auto remaining = needed - os.getPosition(); remaining > 0; remaining -= 65536)
                os.write (zeros.get(), (size_t) juce::jmin ((juce::int64) 65536, remaining));
            os.flush();
        }
    }
    mmf = std::make_unique<juce::MemoryMappedFile> (file, juce::MemoryMappedFile::readWrite, false);
    if (mmf->getData() == nullptr || (juce::int64) mmf->getSize() < needed) { mmf.reset(); return; }

    region = static_cast<Region*> (mmf->getData());
    uint32_t expected = 0;
    region->magic.compare_exchange_strong (expected, kMagic);
    if (region->magic.load() != kMagic) { region = nullptr; return; }

    myId = ((uint32_t) juce::Random::getSystemRandom().nextInt() | 1u) ^ (uint32_t) juce::Time::getHighResolutionTicks();
    if (myId == 0) myId = 1;
    for (int i = 0; i < kMaxPeers && slot < 0; ++i)
    {
        auto& p = region->peers[i];
        uint32_t cur = p.id.load();
        if (cur != 0 && alive (p)) continue;
        if (p.id.compare_exchange_strong (cur, myId))
        {
            slot = i;
            p.heartbeatMs = juce::Time::currentTimeMillis();
            p.kind = (int32_t) kind;
            p.linked = 1;
            p.playing = 0; p.numHits = 0; p.hitSeq = 0; p.level = 0;
            p.numChords = 0; p.chordSeq = 0; p.progN = 0; p.progSeq = 0; p.songOn = 0; p.songN = 0; p.songSeq = 0;
            for (int t = 0; t < kNumDrumTracks; ++t) { p.trackMask[t] = 0; p.trackLen[t] = 16; p.trackRate[t] = 1; }
        }
    }
}

Link::~Link()
{
    if (connected())
    {
        uint32_t me = myId;
        region->peers[slot].id.compare_exchange_strong (me, 0u);
        uint32_t owner = myId;
        region->group.clockOwner.compare_exchange_strong (owner, 0u);
    }
}

bool Link::alive (const Peer& p) noexcept
{
    return p.id.load() != 0 && juce::Time::currentTimeMillis() - p.heartbeatMs.load() < 3000;
}

void Link::heartbeat()
{
    if (! connected()) return;
    auto& p = region->peers[slot];
    // si une autre instance a recupere notre place (gel prolonge), on la reprend si elle est libre
    if (p.id.load() != myId)
    {
        uint32_t cur = p.id.load();
        if (! ((cur == 0 || ! alive (p)) && p.id.compare_exchange_strong (cur, myId))) return;
        p.kind = (int32_t) myKind;
    }
    p.heartbeatMs = juce::Time::currentTimeMillis();
}

juce::Array<PeerInfo> Link::peers (bool includeSelf) const
{
    juce::Array<PeerInfo> out;
    if (region == nullptr) return out;
    for (int i = 0; i < kMaxPeers; ++i)
    {
        const auto& p = region->peers[i];
        if (! alive (p) || (! includeSelf && i == slot)) continue;
        PeerInfo pi;
        pi.slot = i; pi.kind = (Kind) p.kind.load(); pi.linked = p.linked.load() != 0; pi.playing = p.playing.load() != 0;
        pi.bpm = p.bpm.load(); pi.ppq = p.ppq.load(); pi.pattern = p.pattern.load();
        pi.root = p.root.load(); pi.scale = p.scale.load(); pi.level = p.level.load();
        out.add (pi);
    }
    return out;
}

int Link::countLinkedPeers() const
{
    int n = 0;
    for (auto& pi : peers()) if (pi.linked) ++n;
    return n;
}

int Link::firstPeerOfKind (Kind k) const
{
    for (auto& pi : peers()) if (pi.kind == k && pi.linked) return pi.slot;
    return -1;
}

void Link::write (Field f, double v)
{
    if (region == nullptr) return;
    auto& g = region->group;
    g.val[f].store (v);
    g.writer[f].store (myId);
    g.ver[f].fetch_add (1);
}

uint32_t Link::version (Field f) const noexcept { return region != nullptr ? region->group.ver[f].load() : 0u; }

bool Link::readNew (Field f, uint32_t& seenVer, double& v) const
{
    if (region == nullptr) return false;
    const auto& g = region->group;
    const uint32_t ver = g.ver[f].load();
    if (ver == seenVer) return false;
    seenVer = ver;
    if (g.writer[f].load() == myId) return false;
    v = g.val[f].load();
    return true;
}

void Link::publishHits (const Hit* hits, int n)
{
    auto* p = self();
    if (p == nullptr) return;
    n = juce::jlimit (0, kMaxHits, n);
    p->hitSeq.fetch_add (1);                 // impair : ecriture en cours
    for (int i = 0; i < n; ++i)
    {
        p->hitPpq[i].store (hits[i].ppq, std::memory_order_relaxed);
        p->hitTrack[i].store (hits[i].track, std::memory_order_relaxed);
        p->hitVel[i].store (hits[i].vel, std::memory_order_relaxed);
    }
    p->numHits.store (n, std::memory_order_relaxed);
    p->hitSeq.fetch_add (1);                 // pair : coherent
}

int Link::readHits (int peerSlot, Hit* out, int maxN) const
{
    if (region == nullptr || peerSlot < 0 || peerSlot >= kMaxPeers) return 0;
    const auto& p = region->peers[peerSlot];
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t s0 = p.hitSeq.load();
        if (s0 & 1u) continue;
        const int n = juce::jlimit (0, juce::jmin (maxN, kMaxHits), p.numHits.load());
        for (int i = 0; i < n; ++i)
        {
            out[i].ppq = p.hitPpq[i].load (std::memory_order_relaxed);
            out[i].track = p.hitTrack[i].load (std::memory_order_relaxed);
            out[i].vel = p.hitVel[i].load (std::memory_order_relaxed);
        }
        if (p.hitSeq.load() == s0) return n;
    }
    return 0;
}

const char* kindName (int k)
{
    switch (k) { case KindAlea: return "Alea"; case KindTalea: return "Talea"; case KindColor: return "Color"; case KindCantus: return "Cantus"; default: return "?"; }
}

void Link::publishChords (const ChordEvent* c, int n)
{
    auto* p = self();
    if (p == nullptr) return;
    n = juce::jlimit (0, kMaxChordEvents, n);
    p->chordSeq.fetch_add (1);
    for (int i = 0; i < n; ++i)
    {
        p->chordPpq[i].store (c[i].ppq, std::memory_order_relaxed);
        p->chordRoot[i].store (c[i].root, std::memory_order_relaxed);
        p->chordBass[i].store (c[i].bass, std::memory_order_relaxed);
        p->chordMask[i].store (c[i].mask, std::memory_order_relaxed);
        p->chordScale[i].store (c[i].scale, std::memory_order_relaxed);
    }
    p->numChords.store (n, std::memory_order_relaxed);
    p->chordSeq.fetch_add (1);
}

int Link::readChords (int peerSlot, ChordEvent* out, int maxN) const
{
    if (region == nullptr || peerSlot < 0 || peerSlot >= kMaxPeers) return 0;
    const auto& p = region->peers[peerSlot];
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t s0 = p.chordSeq.load();
        if (s0 & 1u) continue;
        const int n = juce::jlimit (0, juce::jmin (maxN, kMaxChordEvents), p.numChords.load());
        for (int i = 0; i < n; ++i)
        {
            out[i].ppq = p.chordPpq[i].load (std::memory_order_relaxed);
            out[i].root = p.chordRoot[i].load (std::memory_order_relaxed);
            out[i].bass = p.chordBass[i].load (std::memory_order_relaxed);
            out[i].mask = p.chordMask[i].load (std::memory_order_relaxed);
            out[i].scale = p.chordScale[i].load (std::memory_order_relaxed);
        }
        if (p.chordSeq.load() == s0) return n;
    }
    return 0;
}

bool Link::chordAt (int peerSlot, double ppq, ChordEvent& out) const
{
    ChordEvent ev[kMaxChordEvents];
    const int n = readChords (peerSlot, ev, kMaxChordEvents);
    if (n <= 0) return false;
    int best = -1;
    for (int i = 0; i < n; ++i)
        if (ev[i].ppq <= ppq + 1.0e-6 && (best < 0 || ev[i].ppq > ev[best].ppq)) best = i;
    if (best < 0)   // position anterieure a tous les evenements : le plus ancien
        for (int i = 0; i < n; ++i) if (best < 0 || ev[i].ppq < ev[best].ppq) best = i;
    out = ev[best];
    return out.mask != 0;
}

void Link::publishProgression (const ProgChord* c, int n, double startPpq)
{
    auto* p = self();
    if (p == nullptr) return;
    n = juce::jlimit (0, kMaxProg, n);
    p->progSeq.fetch_add (1);
    for (int i = 0; i < n; ++i)
    {
        p->progRoot[i].store (c[i].root, std::memory_order_relaxed);
        p->progBass[i].store (c[i].bass, std::memory_order_relaxed);
        p->progMask[i].store (c[i].mask, std::memory_order_relaxed);
        p->progScale[i].store (c[i].scale, std::memory_order_relaxed);
        p->progBeats[i].store (c[i].beats, std::memory_order_relaxed);
    }
    p->progN.store (n, std::memory_order_relaxed);
    p->progStart.store (startPpq, std::memory_order_relaxed);
    p->progSeq.fetch_add (1);
}

int Link::readProgression (int peerSlot, ProgChord* out, int maxN, double& startPpq) const
{
    if (region == nullptr || peerSlot < 0 || peerSlot >= kMaxPeers) return 0;
    const auto& p = region->peers[peerSlot];
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t s0 = p.progSeq.load();
        if (s0 & 1u) continue;
        const int n = juce::jlimit (0, juce::jmin (maxN, kMaxProg), p.progN.load());
        for (int i = 0; i < n; ++i)
        {
            out[i].root = p.progRoot[i].load (std::memory_order_relaxed);
            out[i].bass = p.progBass[i].load (std::memory_order_relaxed);
            out[i].mask = p.progMask[i].load (std::memory_order_relaxed);
            out[i].scale = p.progScale[i].load (std::memory_order_relaxed);
            out[i].beats = juce::jmax (1, (int) p.progBeats[i].load (std::memory_order_relaxed));
        }
        startPpq = p.progStart.load();
        if (p.progSeq.load() == s0) return n;
    }
    return 0;
}

void Link::publishSong (bool on, int64_t startBar, const int* pats, const int* bars, int n)
{
    auto* p = self();
    if (p == nullptr) return;
    n = juce::jlimit (0, kMaxSong, n);
    p->songSeq.fetch_add (1);
    for (int i = 0; i < n; ++i) { p->songPat[i].store (pats[i], std::memory_order_relaxed); p->songBars[i].store (bars[i], std::memory_order_relaxed); }
    p->songN.store (n, std::memory_order_relaxed);
    p->songStartBar.store (startBar, std::memory_order_relaxed);
    p->songOn.store (on ? 1 : 0, std::memory_order_relaxed);
    p->songSeq.fetch_add (1);
}

bool Link::songPatternAt (int64_t absBar, int& pattern) const
{
    if (region == nullptr) return false;
    for (int s = 0; s < kMaxPeers; ++s)
    {
        if (s == slot) continue;
        const auto& p = region->peers[s];
        if (p.id.load() == 0 || ! alive (p) || p.linked.load() == 0 || p.songOn.load() == 0) continue;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t s0 = p.songSeq.load();
            if (s0 & 1u) continue;
            const int n = juce::jlimit (0, kMaxSong, p.songN.load());
            int total = 0;
            for (int i = 0; i < n; ++i) total += juce::jmax (1, (int) p.songBars[i].load (std::memory_order_relaxed));
            if (total <= 0) break;
            int64_t bar = absBar - p.songStartBar.load();
            bar = ((bar % total) + total) % total;
            int acc = 0, pat = -1;
            for (int i = 0; i < n; ++i) { acc += juce::jmax (1, (int) p.songBars[i].load (std::memory_order_relaxed)); if (bar < acc) { pat = p.songPat[i].load (std::memory_order_relaxed); break; } }
            if (p.songSeq.load() == s0 && pat >= 0) { pattern = pat; return true; }
        }
    }
    return false;
}

void Link::publishTrack (int t, uint32_t mask, int len, int rate)
{
    auto* p = self();
    if (p == nullptr || t < 0 || t >= kNumDrumTracks) return;
    p->trackMask[t] = mask; p->trackLen[t] = len; p->trackRate[t] = rate;
}

bool Link::readTrack (int peerSlot, int t, uint32_t& mask, int& len, int& rate) const
{
    if (region == nullptr || peerSlot < 0 || peerSlot >= kMaxPeers || t < 0 || t >= kNumDrumTracks) return false;
    const auto& p = region->peers[peerSlot];
    mask = p.trackMask[t].load(); len = p.trackLen[t].load(); rate = p.trackRate[t].load();
    return true;
}

double Link::syncClock (bool playing, double localPpq, double bpm, bool& followed)
{
    followed = false;
    if (! connected()) return localPpq;
    auto& g = region->group;
    const uint32_t owner = g.clockOwner.load();
    bool ownerAlive = false;
    if (owner != 0 && owner != myId)
        for (int i = 0; i < kMaxPeers; ++i)
            if (region->peers[i].id.load() == owner) { ownerAlive = alive (region->peers[i]); break; }

    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - g.clockTicks.load());
    if (ownerAlive && g.clockPlaying.load() != 0 && secs < 0.3)     // horloge figee (instance desactivee) : on prend le relais
    {
        if (! playing) return localPpq;
        followed = true;
        return g.clockPpq.load() + juce::jmax (0.0, secs) * g.clockBpm.load() / 60.0;
    }
    if (! playing)
    {
        if (owner == myId) g.clockPlaying = 0;
        return localPpq;
    }
    uint32_t cur = owner;
    if (owner != myId && ! g.clockOwner.compare_exchange_strong (cur, myId)) return localPpq;
    g.clockPpq = localPpq;
    g.clockBpm = bpm;
    g.clockTicks = juce::Time::getHighResolutionTicks();
    g.clockPlaying = 1;
    return localPpq;
}

// ---------------------------------------------------------------------------
LinkSync::LinkSync (Link& l, juce::AudioProcessorValueTreeState& s) : link (l), state (s)
{
    startTimerHz (20);
}

void LinkSync::bind (Field f, std::function<double()> get, std::function<void (double)> set)
{
    getters[f] = std::move (get);
    setters[f] = std::move (set);
    last[f] = getters[f]();
}

bool LinkSync::masterOn() const
{
    auto* p = state.getRawParameterValue ("lk_on");
    return p != nullptr && p->load() > 0.5f;
}

void LinkSync::timerCallback()
{
    link.heartbeat();
    const bool master = masterOn();
    if (auto* me = link.self()) me->linked = master ? 1 : 0;

    if (! joined)
    {
        // a l'arrivee : on n'adopte les valeurs du groupe que si un autre pair lie est vivant
        // (sinon ce sont des restes d'une session precedente)
        // Seul : nos valeurs deviennent la reference du groupe. Sinon on adopte celles du groupe,
        // sauf pour les champs que personne n'a encore publies.
        joined = true;
        const bool alone = link.countLinkedPeers() == 0;
        for (int f = 0; f < kNumFields; ++f)
        {
            if (! getters[f]) continue;
            if (alone || link.version ((Field) f) == 0)
            {
                auto* en = state.getRawParameterValue (fieldParamId (f));
                const double cur = getters[f]();
                if (master && en != nullptr && en->load() > 0.5f) link.write ((Field) f, cur);
                seen[f] = link.version ((Field) f);
                last[f] = cur;
            }
        }
    }

    for (int f = 0; f < kNumFields; ++f)
    {
        if (! getters[f]) continue;
        auto* en = state.getRawParameterValue (fieldParamId (f));
        const bool enabled = master && en != nullptr && en->load() > 0.5f;
        double v = 0;
        if (link.readNew ((Field) f, seen[f], v))
        {
            if (enabled && std::abs (v - getters[f]()) > 1.0e-6) setters[f] (v);
            last[f] = getters[f]();
            continue;
        }
        const double cur = getters[f]();
        if (enabled && std::abs (cur - last[f]) > 1.0e-6)
        {
            link.write ((Field) f, cur);
            seen[f] = link.version ((Field) f);
        }
        last[f] = cur;
    }
    if (onTick) onTick();
}

} // namespace alea::link
