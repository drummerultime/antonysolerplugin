#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace alea;

juce::AudioProcessor::BusesProperties AleaAudioProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int t = 0; t < kNumTracks; ++t)
        b = b.withOutput ("Voice " + juce::String (t + 1), juce::AudioChannelSet::stereo(), false);
    return b;
}

AleaAudioProcessor::AleaAudioProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "ALEA", createParameterLayout())
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int vp = 0; vp < kNumVP; ++vp)
        {
            vpRaw[t][vp]  = apvts.getRawParameterValue (vpId (t, vp));
            jitRaw[t][vp] = apvts.getRawParameterValue (jitId (t, vp));
            lastNorm[t][vp] = -1.0f;
            snapB[t][vp] = defaultVP (t, vp);
        }
        for (auto& l : locks[t]) l = false;
        engineRaw[t]  = apvts.getRawParameterValue (tid (t, "engine"));
        ftypeRaw[t]   = apvts.getRawParameterValue (tid (t, "ftype"));
        chokeRaw[t]   = apvts.getRawParameterValue (tid (t, "choke"));
        muteRaw[t]    = apvts.getRawParameterValue (tid (t, "mute"));
        outRaw[t]     = apvts.getRawParameterValue (tid (t, "out"));
        velSensRaw[t] = apvts.getRawParameterValue (tid (t, "velsens"));
        linkRaw[t]    = apvts.getRawParameterValue (tid (t, "link"));
        fxTypeRaw[t]  = apvts.getRawParameterValue (tid (t, "fxtype"));
        for (int vp = 0; vp < kNumVP; ++vp) evoOff[t][vp] = 0.0f;
        snapBEngine[t] = defaultKit (t).engine;
        playStep[t] = -1;
        flash[t] = 0.0f;
        guiTrig[t] = false;
        sampleSlot[t] = nullptr;
    }
    for (int m = 0; m < kNumMods; ++m)
    {
        modRateRaw[m] = apvts.getRawParameterValue (modId (m, "rate"));
        modPRaw[m]    = apvts.getRawParameterValue (modId (m, "p"));
        modValue[m] = 0.5f;
    }
    for (int s = 0; s < kNumMxSlots; ++s)
    {
        mxSrcRaw[s] = apvts.getRawParameterValue (mxId (s, "src"));
        mxTrkRaw[s] = apvts.getRawParameterValue (mxId (s, "trk"));
        mxDstRaw[s] = apvts.getRawParameterValue (mxId (s, "dst"));
        mxAmtRaw[s] = apvts.getRawParameterValue (mxId (s, "amt"));
    }
    for (auto& c : ccMap) c = -1;
    for (auto& row : evoMask) for (auto& m : row) m = 0u;
    for (int n = 0; n < kNumScenes; ++n)
    {
        sceneValid[n] = false;
        for (int t = 0; t < kNumTracks; ++t)
        {
            sceneEng[n][t] = defaultKit (t).engine;
            for (int vp = 0; vp < kNumVP; ++vp) sceneVal[n][t][vp] = defaultVP (t, vp);
        }
    }
    for (int i = 0; i < kMaxChain; ++i) { chainPat[i] = 0; chainBars[i] = 1; }

    auto g = [this] (const char* id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    playRaw = g ("play"); bpmRaw = g ("bpm"); patternRaw = g ("pattern"); seedRaw = g ("seed");
    chaosRaw = g ("chaos"); rndModeRaw = g ("rndmode"); rndDistRaw = g ("rnddist"); scaleRaw = g ("scale");
    swingRaw = g ("swing"); accentRaw = g ("accent"); seqChaosRaw = g ("seqchaos"); ratChaosRaw = g ("ratchaos"); humanRaw = g ("humanize");
    morphRaw = g ("morph"); densityRaw = g ("density"); fillRaw = g ("fill"); modDepthRaw = g ("moddepth");
    songRaw = g ("songmode"); midiOutRaw = g ("midiout"); turLenRaw = g ("m3len");
    dlyDivRaw = g ("dlydiv"); dlyFbRaw = g ("dlyfb"); dlyToneRaw = g ("dlytone"); dlyPingRaw = g ("dlyping"); dlyRetRaw = g ("dlyret"); dlyRndRaw = g ("dlyrnd");
    revSizeRaw = g ("revsize"); revDampRaw = g ("revdamp"); revWidthRaw = g ("revwidth"); revRetRaw = g ("revret");
    stuChanceRaw = g ("stuchance"); stuDivRaw = g ("studiv"); stuLenRaw = g ("stulen");
    glueRaw = g ("glue"); mDriveRaw = g ("mdrive"); masterRaw = g ("master");
    followTrkRaw = g ("m6trk"); glideRaw = g ("glide"); keysRaw = g ("keystrack");
    duckAmtRaw = g ("duckamt"); duckSrcRaw = g ("ducksrc"); duckRelRaw = g ("duckrel");
    evoAmtRaw = g ("evoamt"); evoBarsRaw = g ("evobars"); evoTargetRaw = g ("evotarget");
    sceneARaw = g ("scenea"); sceneBRaw = g ("sceneb"); sceneXfRaw = g ("scenexf"); sceneOnRaw = g ("sceneon"); hqRaw = g ("hq");
    revTypeRaw = g ("revtype"); revPreRaw = g ("revpre");
    rootRaw = g ("root"); keyLockRaw = g ("keylock"); keyChordRaw = g ("keychord");
    lkOnRaw = g ("lk_on"); lkPlayRaw = g ("lk_play"); lkTempoRaw = g ("lk_tempo"); lkSongRaw = g ("lk_song");

    formatManager.registerBasicFormats();
    bank.initDefault();
    setChainText (chainText);
    mods.reset();
    setupLink();
}

AleaAudioProcessor::~AleaAudioProcessor()
{
    cancelPendingUpdate();
    linkSync.reset();
    link.reset();
}

void AleaAudioProcessor::setupLink()
{
    using namespace alea::link;
    link = std::make_unique<Link> (KindAlea);
    linkSync = std::make_unique<LinkSync> (*link, apvts);
    auto bindParam = [this] (Field f, const char* id)
    {
        juce::String pid (id);
        linkSync->bind (f, [this, pid] { return (double) apvts.getRawParameterValue (pid)->load(); },
                           [this, pid] (double v) { setParamReal (pid, (float) v); });
    };
    bindParam (FTempo, "bpm"); bindParam (FPlay, "play"); bindParam (FKey, "root"); bindParam (FScale, "scale");
    bindParam (FPattern, "pattern"); bindParam (FFill, "fill"); bindParam (FChaos, "chaos"); bindParam (FSwing, "swing");
    bindParam (FEvolve, "evoamt"); bindParam (FDensity, "density"); bindParam (FMorph, "morph"); bindParam (FSeed, "seed");
    linkSync->bind (FScene, [this] { return (double) lastRecalledScene.load(); },
                            [this] (double v) { if (v >= 0) recallScene ((int) v); });
    linkSync->onTick = [this]
    {
        auto* me = link->self();
        if (me == nullptr) return;
        me->root = (int) rootRaw->load();
        me->scale = (int) scaleRaw->load();
        // pattern de batterie courant (avec l'evolution) : Talea s'en sert pour ses generateurs
        const int cp = juce::jlimit (0, kNumPatterns - 1, currentPattern.load());
        const auto& pat = bank.pats[cp];
        for (int t = 0; t < kNumTracks && t < kNumDrumTracks; ++t)
        {
            const auto& tr = pat.tracks[t];
            uint32_t mask = 0;
            for (int i = 0; i < kMaxSteps; ++i)
                if (tr.steps[i].on.load() != 0) mask |= 1u << i;
            mask ^= evoMask[cp][t].load();
            if (muteRaw[t]->load() > 0.5f) mask = 0;
            link->publishTrack (t, mask, tr.length.load(), tr.rate.load());
        }
    };
}

// Coups a venir (2 temps) : lus par Talea pour le sidechain, le modulateur Drums et Follow/Avoid kick.
void AleaAudioProcessor::publishLink (bool running, double ppqStart, double ppqEnd, double bpm)
{
    auto* me = link != nullptr ? link->self() : nullptr;
    if (me == nullptr) return;
    me->bpm = bpm; me->ppq = ppqEnd; me->playing = running ? 1 : 0; me->pattern = currentPattern.load();
    me->level = outPeak.load();
    int n = 0;
    if (running && lkOnRaw->load() > 0.5f)
    {
        auto ctx = makeContext (currentPattern.load(), patStartPpq, bpm);
        ctx.density  = gmod (ctx.density, GDensity);
        ctx.seqChaos = gmod (ctx.seqChaos, GSeqChaos);
        ctx.ratChaos = gmod (ctx.ratChaos, GRatChaos);
        ctx.swing    = gmod (ctx.swing, GSwing);
        CondState cs = condState;     // copie : la prevision ne doit pas modifier la memoire PRE / NEI
        runSequencer (bank, ctx, &cs, ppqStart, ppqStart + 2.0, [&] (double te, SeqEvent e)
        {
            if (n >= alea::link::kMaxHits || muteRaw[e.track]->load() > 0.5f) return;
            linkHits[n++] = { te, e.track, e.accent ? 1.0f : juce::jlimit (0.0f, 1.0f, e.vel) };
        });
    }
    juce::ignoreUnused (ppqEnd);
    link->publishHits (linkHits, n);
}

// Key lock : ramene la hauteur des voix tonales sur la tonalite partagee (tonique + gamme)
float AleaAudioProcessor::keyLockFreq (int engine, float freq) const
{
    const int mode = (int) keyLockRaw->load();
    if (mode == 0 || freq <= 1.0f) return freq;
    const bool melodic = engine == EngFM || engine == EngModal || engine == EngPluck;
    const bool tonal = melodic || engine == EngKick || engine == EngSnare || engine == EngHybrid;
    if (! (mode == 1 ? melodic : tonal)) return freq;
    const float midi = 69.0f + 12.0f * std::log2 (freq / 440.0f);
    if (colorChordMask != 0)
    {
        // accord de Color : note de l'accord la plus proche
        const int m = (int) std::round (midi);
        for (int d = 0; d <= 6; ++d)
            for (int sgn : { -1, 1 })
            {
                const int c = m + sgn * d;
                if ((colorChordMask >> (((c % 12) + 12) % 12)) & 1)
                    return 440.0f * std::exp2 (((float) c - 69.0f) / 12.0f);
            }
    }
    const float root = (float) (int) rootRaw->load();
    const int scale = juce::jmax (1, (int) scaleRaw->load());
    const float q = root + quantizeSemis (std::round (midi - root), scale);
    return 440.0f * std::exp2 ((q - 69.0f) / 12.0f);
}

bool AleaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.outputBuses.isEmpty() || layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses.getReference (i);
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo()) return false;
    }
    return layouts.inputBuses.isEmpty();
}

void AleaAudioProcessor::prepareVoices (double voiceRate)
{
    for (int t = 0; t < kNumTracks; ++t)
        for (int i = 0; i < 2; ++i)
            tracks[t].v[i].prepare (voiceRate, hash3 ((uint32_t) t, (uint32_t) i, 0xBEEFu));
    for (int t = 0; t < kNumTracks; ++t) { decL[t].reset(); decR[t].reset(); }
    decD.reset(); decV.reset();
}

void AleaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    hqActive = hqRaw->load() > 0.5f;
    prepareVoices (hqActive ? sampleRate * 2.0 : sampleRate);
    delay.prepare (sampleRate);
    stutter.prepare (sampleRate);
    glue.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();
    plate.prepare (sampleRate);
    revBus.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    internalRunning = wasRunning = false;
    numPendingOffs = 0;
    heldKeys = 0;
    duckEnv = followEnv = 0.0f;
    condState.reset();
    scopeDecim = juce::jmax (1, (int) (sampleRate * 0.25 / kScopeLen));
}

SeqContext AleaAudioProcessor::makeContext (int pattern, double patStart, double bpm) const
{
    SeqContext c;
    c.pattern = pattern;
    c.patStart = patStart;
    c.seed = (uint32_t) seedRaw->load();
    c.swing = swingRaw->load();
    c.seqChaos = seqChaosRaw->load();
    c.ratChaos = ratChaosRaw->load();
    c.human = humanRaw->load();
    c.density = densityRaw->load();
    c.fill = fillRaw->load() > 0.5f;
    c.bpm = bpm;
    return c;
}

void AleaAudioProcessor::computeModOffsets()
{
    for (auto& row : modOff) for (auto& v : row) v = 0.0f;
    for (auto& v : gOff) v = 0.0f;
    const float depth = modDepthRaw->load();
    for (int s = 0; s < kNumMxSlots; ++s)
    {
        const int src = (int) mxSrcRaw[s]->load();
        const float amt = mxAmtRaw[s]->load();
        if (src <= 0 || src > kNumMods || amt == 0.0f) continue;
        const float v = (mods.value[src - 1] * 2.0f - 1.0f) * amt * depth;
        const int trk = (int) mxTrkRaw[s]->load();
        const int dstAll = juce::jlimit (0, kNumVP + kNumGT - 1, (int) mxDstRaw[s]->load());
        if (dstAll >= kNumVP) { gOff[dstAll - kNumVP] += v * 0.5f; continue; }
        const int dst = dstAll;
        for (int t = 0; t < kNumTracks; ++t)
            if (trk == 0 || trk - 1 == t) modOff[t][dst] += v * 0.5f;
    }
}

// ---------------------------------------------------------------------------
void AleaAudioProcessor::triggerTrack (const SeqEvent& e)
{
    const int t = e.track;
    if (t == (int) duckSrcRaw->load()) duckEnv = 1.0f;     // une piste muette peut servir de "kick fantome"
    if (muteRaw[t]->load() > 0.5f) return;

    const int mode = (int) rndModeRaw->load();
    const int dist = (int) rndDistRaw->load();
    const float chaos = chaosEff * 2.0f;
    const uint32_t seed = (uint32_t) seedRaw->load();
    const float morph = hasB.load() ? morphRaw->load() : 0.0f;
    const float link = linkRaw[t]->load();
    const float wShared = std::sqrt (link), wOwn = std::sqrt (1.0f - link);
    const Step* step = (e.pat >= 0 && e.step >= 0) ? &bank.pats[e.pat].tracks[t].steps[e.step] : nullptr;

    // scenes : le crossfader remplace les potards par un melange de deux scenes
    const int sA = juce::jlimit (0, kNumScenes - 1, (int) sceneARaw->load());
    const int sB = juce::jlimit (0, kNumScenes - 1, (int) sceneBRaw->load());
    const bool sceneMix = sceneOnRaw->load() > 0.5f && sceneValid[sA].load() && sceneValid[sB].load();
    const float xf = sceneXfRaw->load();

    float shared = 0.0f;
    if (mode == 0)      shared = shapedBi (dist, e.hitKey, (uint32_t) (t * 64 + 63), seed);
    else if (mode == 1) shared = shapedBi (dist, e.barKey, (uint32_t) (t * 64 + 63), seed + 7u);
    else
    {
        float& d = drift[t][kNumVP];
        d += rng.bi() * 0.3f;
        if (d > 1.0f) d = 2.0f - d;
        if (d < -1.0f) d = -2.0f - d;
        shared = d;
    }

    float n[kNumVP];
    float tunePre = 0.5f;
    for (int vp = 0; vp < kNumVP; ++vp)
    {
        float base = sceneMix ? sceneVal[sA][t][vp].load() + (sceneVal[sB][t][vp].load() - sceneVal[sA][t][vp].load()) * xf
                              : vpRaw[t][vp]->load();
        if (morph > 0.0f) base += (snapB[t][vp].load() - base) * morph;
        if (step != nullptr)
        {
            const int lk = step->plock[vp].load();
            if (lk >= 0) base = (float) lk * 0.001f;
        }
        if (vp == Tune) tunePre = base;
        base += modOff[t][vp] + evoOff[t][vp].load (std::memory_order_relaxed);

        const float j = jitRaw[t][vp]->load() * chaos;
        float o = 0.0f;
        if (j > 0.0f)
        {
            const uint32_t pid = (uint32_t) (t * 64 + vp);
            float own;
            if (mode == 0)      own = shapedBi (dist, e.hitKey, pid, seed);
            else if (mode == 1) own = shapedBi (dist, e.barKey, pid, seed + 7u);
            else
            {
                float& d = drift[t][vp];
                d += rng.bi() * 0.3f;
                if (d > 1.0f) d = 2.0f - d;
                if (d < -1.0f) d = -2.0f - d;
                own = d;
            }
            o = wShared * shared + wOwn * own;
        }
        n[vp] = juce::jlimit (0.0f, 1.0f, base + o * j * 0.5f);
    }

    const int scale = (int) scaleRaw->load();
    if (scale > 0)
    {
        const float delta = (n[Tune] - tunePre) * 48.0f;
        n[Tune] = juce::jlimit (0.0f, 1.0f, tunePre + quantizeSemis (delta, scale) / 48.0f);
    }
    for (int vp = 0; vp < kNumVP; ++vp) lastNorm[t][vp].store (n[vp], std::memory_order_relaxed);

    const float acc = e.accent ? accentRaw->load() : 0.0f;
    int engine = juce::jlimit (0, kNumEngines - 1, (int) engineRaw[t]->load());
    if (sceneMix) engine = juce::jlimit (0, kNumEngines - 1, (xf < 0.5f ? sceneEng[sA][t] : sceneEng[sB][t]).load());
    if (morph > 0.5f) engine = juce::jlimit (0, kNumEngines - 1, snapBEngine[t].load());
    static constexpr float baseFreq[kNumEngines] { 50.0f, 180.0f, 1.0f, 1.0f, 220.0f, 110.0f, 110.0f, 80.0f, 55.0f, 1.0f };

    const float semis = map::tuneSemis (n[Tune]) + e.tune;
    VoiceSettings vs;
    vs.engine     = engine;
    vs.tuneRatio  = std::exp2 (semis / 12.0f);
    vs.freq       = baseFreq[engine] * vs.tuneRatio;
    if (const float kf = keyLockFreq (engine, vs.freq); kf != vs.freq)
    {
        vs.tuneRatio *= kf / vs.freq;
        vs.freq = kf;
    }
    vs.decay      = map::decaySec (n[Decay]) * (1.0f + 0.5f * acc);
    vs.attack     = map::attackSec (n[Attack]);
    vs.pitchAmt   = map::pitchSemis (n[PitchAmt]);
    vs.pitchDecay = map::pitchDecaySec (n[PitchDecay]);
    vs.timbre = n[Timbre]; vs.color = n[Color]; vs.noise = n[Noise];
    vs.cutoff  = map::cutoffHz (n[Cutoff]);
    vs.reso    = n[Reso];
    vs.ftype   = (int) ftypeRaw[t]->load();
    vs.fenvOct = map::fenvOct (n[FEnv]);
    if (std::abs (vs.fenvOct) < 0.05f) vs.fenvOct = 0.0f;
    vs.drive = juce::jmin (1.0f, n[Drive] + 0.25f * acc);
    vs.crush = n[Crush];
    vs.punch = map::punch (n[Punch]);
    if (std::abs (vs.punch) < 0.02f) vs.punch = 0.0f;
    vs.fxType = (int) fxTypeRaw[t]->load();
    vs.fxAmt = n[FxAmt];
    vs.layer = n[Layer];
    const float vsens = velSensRaw[t]->load();
    const float velGain = (1.0f - vsens) + vsens * std::pow (juce::jlimit (0.0f, 1.0f, e.vel), 1.5f);
    vs.gain = map::levelGain (n[Level]) * velGain * (1.0f + 0.6f * acc) * 0.5f;
    vs.pan = map::pan (n[Pan]);
    vs.sendDly = n[SendDly];
    vs.sendRev = n[SendRev];
    vs.sample = sampleSlot[t].load();
    if (engine == EngSample && vs.sample == nullptr) return;

    // slide : glissement depuis la hauteur du coup precedent de la piste
    if (e.slide && lastFreq[t] > 0.0f)
    {
        vs.slideRatio = lastFreq[t] / juce::jmax (0.01f, vs.freq);
        const float gl = glideRaw->load();
        vs.glideSec = 0.01f + gl * gl * 0.49f;
    }
    lastFreq[t] = vs.freq;

    const int choke = (int) chokeRaw[t]->load();
    if (choke > 0)
        for (int u = 0; u < kNumTracks; ++u)
            if (u != t && (int) chokeRaw[u]->load() == choke)
                for (auto& v : tracks[u].v) v.release (8.0f);

    auto& tr = tracks[t];
    tr.v[tr.cur].release (3.0f);
    tr.cur ^= 1;
    tr.v[tr.cur].trigger (vs);
    flash[t].store (1.0f, std::memory_order_relaxed);
    if (t == scopeTrack.load (std::memory_order_relaxed)) scopeWrite = 0;
}

void AleaAudioProcessor::evolveStep (int64_t bar)
{
    const float amt = gmod (evoAmtRaw->load(), GEvo);
    if (amt <= 0.0f) return;
    const int target = (int) evoTargetRaw->load();
    const uint32_t seed = (uint32_t) seedRaw->load() ^ 0xE70Fu;
    const uint32_t b = (uint32_t) bar;
    if (target == 0 || target == 2)
    {
        const int pat = currentPattern.load();
        for (int t = 0; t < kNumTracks; ++t)
        {
            const int len = bank.pats[pat].tracks[t].length.load();
            for (int i = 0; i < len; ++i)
                if (hUni (b, (uint32_t) (t * 64 + i), seed) < amt * 0.10f)
                    evoMask[pat][t].fetch_xor (1u << i);
        }
    }
    if (target == 1 || target == 2)
        for (int t = 0; t < kNumTracks; ++t)
            for (int vp = 0; vp < kNumVP; ++vp)
            {
                const float lim = vp == Level ? 0.08f : 0.35f;
                const float v = evoOff[t][vp].load() + hBi (b, (uint32_t) (t * 64 + vp), seed + 1u) * amt * 0.06f;
                evoOff[t][vp].store (juce::jlimit (-lim, lim, v));
            }
}

void AleaAudioProcessor::sendNoteOn (juce::MidiBuffer& out, int note, int vel, int offset)
{
    for (int i = 0; i < numPendingOffs; ++i)
        if (pendingOffs[(size_t) i].note == note)
        {
            out.addEvent (juce::MidiMessage::noteOff (1, note), offset);
            pendingOffs[(size_t) i] = pendingOffs[(size_t) --numPendingOffs];
            break;
        }
    out.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, vel)), offset);
    if (numPendingOffs < (int) pendingOffs.size())
        pendingOffs[(size_t) numPendingOffs++] = { note, offset + (int) (0.06 * sr) };
}

// ---------------------------------------------------------------------------
void AleaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples == 0) return;

    // bascule du mode HQ (voix calculees a 2x puis decimees)
    if ((hqRaw->load() > 0.5f) != hqActive)
    {
        hqActive = ! hqActive;
        prepareVoices (hqActive ? sr * 2.0 : sr);
    }

    if (revBus.getNumSamples() < numSamples)
        revBus.setSize (2, numSamples, false, false, true);
    revBus.clear();

    auto mainBus = getBusBuffer (buffer, false, 0);
    float* outL = mainBus.getWritePointer (0);
    float* outR = mainBus.getWritePointer (1);
    float* auxL[kNumTracks] {}; float* auxR[kNumTracks] {};
    for (int t = 0; t < kNumTracks; ++t)
        if (t + 1 < getBusCount (false))
            if (auto* bus = getBus (false, t + 1); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() >= 2)
            {
                auto b = getBusBuffer (buffer, false, t + 1);
                auxL[t] = b.getWritePointer (0);
                auxR[t] = b.getWritePointer (1);
            }

    // --- transport
    double bpm = bpmRaw->load();
    bool hostPlaying = false;
    double hostPpq = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) hostPpq = *p;
        }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    const double ppqPerSample = bpm / (60.0 * sr);
    const double samplesPerBeat = 60.0 * sr / bpm;

    bool running = false;
    double ppqStart = 0.0;
    if (hostPlaying) { running = true; ppqStart = hostPpq; internalRunning = false; }
    const bool linkClock = link != nullptr && lkOnRaw->load() > 0.5f && (lkPlayRaw->load() > 0.5f || lkTempoRaw->load() > 0.5f);
    if (hostPlaying) {}
    else if (playRaw->load() > 0.5f)
    {
        if (! internalRunning) { internalRunning = true; internalPpq = 0.0; }
        running = true;
        bool followed = false;
        // horloge partagee : on se recale seulement si l'ecart depasse ~30 ms (lecture continue sinon)
        const double target = linkClock ? link->syncClock (true, internalPpq, bpm, followed) : internalPpq;
        if (followed && std::abs (target - internalPpq) > 0.03 * bpm / 60.0) internalPpq = target;
        ppqStart = internalPpq;
    }
    else
    {
        internalRunning = false;
        if (linkClock) { bool f; link->syncClock (false, 0.0, bpm, f); }
    }
    const double ppqEnd = ppqStart + numSamples * ppqPerSample;
    colorChordMask = 0;
    if (link != nullptr && keyChordRaw->load() > 0.5f && lkOnRaw->load() > 0.5f)
    {
        const int cslot = link->firstPeerOfKind (alea::link::KindColor);
        alea::link::ChordEvent ce;
        if (cslot >= 0 && link->chordAt (cslot, running ? ppqStart : -1.0e8, ce)) colorChordMask = ce.mask & 0xfff;
    }

    const bool justStarted = running && ! wasRunning;
    if (justStarted)
    {
        patStartPpq = std::floor (ppqStart / 4.0) * 4.0;
        songStartBar = (int64_t) std::floor (ppqStart / 4.0);
        condState.reset();
    }
    wasRunning = running;

    const int paramPat = (int) patternRaw->load();
    if (paramPat != lastParamPattern) { lastParamPattern = paramPat; requestedPattern = paramPat; }

    numEvents = 0;
    numBeats = 0;

    // --- MIDI entrant
    //  C0..G0 (24-31) : scenes | C1..G1 (36-43) : voix | C2..G2 (48-55) : patterns | >= C3 (60) : clavier | CC : MIDI learn
    auto& params = getParameters();
    const int keysTrack = (int) keysRaw->load() - 1;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            if (note >= 36 && note < 36 + kNumTracks)
            {
                SeqEvent e;
                e.offset = juce::jlimit (0, numSamples - 1, meta.samplePosition);
                e.track = note - 36; e.vel = m.getFloatVelocity(); e.accent = m.getVelocity() >= 120;
                e.hitKey = e.barKey = rng.next(); e.fromMidiIn = true;
                addEvent (e);
            }
            else if (note >= 48 && note < 48 + kNumPatterns)
                requestedPattern = note - 48;
            else if (note >= 24 && note < 24 + kNumScenes)
            {
                pendingScene = note - 24;
                triggerAsyncUpdate();
            }
            else if (note >= 60 && keysTrack >= 0)
            {
                SeqEvent e;
                e.offset = juce::jlimit (0, numSamples - 1, meta.samplePosition);
                e.track = keysTrack; e.vel = m.getFloatVelocity(); e.accent = m.getVelocity() >= 120;
                e.tune = (float) (note - 60);
                e.slide = heldKeys > 0;              // legato = glissement, comme une 303
                e.hitKey = e.barKey = rng.next(); e.fromMidiIn = true;
                addEvent (e);
                ++heldKeys;
            }
        }
        else if (m.isNoteOff())
        {
            if (m.getNoteNumber() >= 60 && keysTrack >= 0) heldKeys = juce::jmax (0, heldKeys - 1);
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const int learn = learnParam.load();
            if (learn >= 0 && learn < params.size())
            {
                for (auto& c : ccMap) if (c.load() == learn) c = -1;
                ccMap[cc] = learn;
                learnParam = -1;
            }
            const int idx = ccMap[cc].load();
            if (idx >= 0 && idx < params.size())
                params[idx]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        }
    }
    midi.clear();

    for (int i = 0; i < numPendingOffs;)
    {
        auto& p = pendingOffs[(size_t) i];
        if (p.remaining < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, p.note), juce::jmax (0, p.remaining));
            p = pendingOffs[(size_t) --numPendingOffs];
        }
        else { p.remaining -= numSamples; ++i; }
    }

    for (int t = 0; t < kNumTracks; ++t)
        if (guiTrig[t].exchange (false))
        {
            SeqEvent e; e.track = t; e.vel = 0.9f; e.hitKey = rng.next(); e.barKey = rng.next();
            addEvent (e);
        }

    // --- song mode
    if (running && songRaw->load() > 0.5f && chainLen.load() > 0)
    {
        int total = 0;
        const int len = chainLen.load();
        for (int i = 0; i < len; ++i) total += chainBars[i].load();
        if (total > 0)
        {
            int64_t bar = (int64_t) std::floor (ppqEnd / 4.0) - songStartBar;
            bar = ((bar % total) + total) % total;
            int acc = 0;
            for (int i = 0; i < len; ++i)
            {
                acc += chainBars[i].load();
                if (bar < acc) { requestedPattern = chainPat[i].load(); songPos = i; break; }
            }
        }
    }
    else songPos = -1;

    {
        // forme partagee (AleaLink > Song) : publie la chaine, ou suit celle d'un autre instrument
        const bool songOnNow = songRaw->load() > 0.5f && chainLen.load() > 0;
        if (! songOnNow)
        {
            int sp = 0;
            if (running && link != nullptr && lkOnRaw->load() > 0.5f && lkSongRaw->load() > 0.5f
                && link->songPatternAt ((int64_t) std::floor (ppqEnd / 4.0), sp))
                requestedPattern = juce::jlimit (0, alea::kNumPatterns - 1, sp);
        }
        if (link != nullptr)
        {
            if (songOnNow && running)
            {
                int pats[64], bars[64]; const int len = juce::jmin (64, chainLen.load());
                for (int i = 0; i < len; ++i) { pats[i] = chainPat[i].load(); bars[i] = chainBars[i].load(); }
                link->publishSong (true, songStartBar, pats, bars, len);
                songPublished = true;
            }
            else if (songPublished) { link->publishSong (false, 0, nullptr, nullptr, 0); songPublished = false; }
        }
    }

    const bool barCrossed = std::floor (ppqEnd / 4.0) > std::floor (ppqStart / 4.0);
    if (requestedPattern != currentPattern.load())
    {
        if (! running || barCrossed || justStarted)
        {
            currentPattern = requestedPattern;
            patStartPpq = running ? std::floor (ppqEnd / 4.0) * 4.0 : 0.0;
            if (justStarted) patStartPpq = std::floor (ppqStart / 4.0) * 4.0;
        }
    }

    // --- modulateurs (+ cibles globales)
    {
        const double clock = running ? ppqEnd : (freeModClock += numSamples * ppqPerSample);
        int rates[kNumMods]; float ps[kNumMods];
        for (int m = 0; m < kNumMods; ++m) { rates[m] = (int) modRateRaw[m]->load(); ps[m] = modPRaw[m]->load(); }
        mods.setFollow (followEnv * 2.0f);
        mods.advance (clock, rates, ps, (int) turLenRaw->load(), (uint32_t) seedRaw->load());
        for (int m = 0; m < kNumMods; ++m) modValue[m].store (mods.value[m], std::memory_order_relaxed);
        computeModOffsets();
    }
    chaosEff = gmod (chaosRaw->load(), GChaos);

    // --- evolution : a chaque frontiere de N mesures
    if (running && barCrossed && evoAmtRaw->load() + gOff[GEvo] > 0.0f)
    {
        static constexpr int barsN[] { 1, 2, 4, 8, 16 };
        const int64_t bar = (int64_t) std::floor (ppqEnd / 4.0);
        if (bar % barsN[juce::jlimit (0, 4, (int) evoBarsRaw->load())] == 0) evolveStep (bar);
    }

    if (running)
    {
        auto ctx = makeContext (currentPattern.load(), patStartPpq, bpm);
        ctx.density  = gmod (ctx.density, GDensity);
        ctx.seqChaos = gmod (ctx.seqChaos, GSeqChaos);
        ctx.ratChaos = gmod (ctx.ratChaos, GRatChaos);
        ctx.swing    = gmod (ctx.swing, GSwing);
        runSequencer (bank, ctx, &condState, ppqStart, ppqEnd, [&] (double te, SeqEvent e)
        {
            e.offset = juce::jlimit (0, numSamples - 1, (int) ((te - ppqStart) / ppqPerSample));
            addEvent (e);
        });
        const auto& pat = bank.pats[currentPattern.load()];
        for (int t = 0; t < kNumTracks; ++t)
        {
            const double stepLen = ratePpq (pat.tracks[t].rate.load());
            playStep[t] = pat.tracks[t].stepIndex ((int64_t) std::floor (ppqEnd / stepLen), trackSalt (ctx.seed, t));
        }
        for (int64_t b = (int64_t) std::ceil (ppqStart); (double) b < ppqEnd && numBeats < (int) beatOffsets.size(); ++b)
        {
            beatOffsets[(size_t) numBeats] = juce::jlimit (0, numSamples - 1, (int) (((double) b - ppqStart) / ppqPerSample));
            beatIndex[(size_t) numBeats] = b;
            ++numBeats;
        }
        if (internalRunning) internalPpq = ppqEnd;
    }
    else
    {
        for (auto& p : playStep) p = -1;
    }
    playPpq = ppqEnd;
    patStartShared = patStartPpq;
    isRunning = running;
    publishLink (running, ppqStart, ppqEnd, bpm);

    std::sort (events.begin(), events.begin() + numEvents,
               [] (const SeqEvent& a, const SeqEvent& b) { return a.offset < b.offset; });

    if (midiOutRaw->load() > 0.5f)
        for (int i = 0; i < numEvents; ++i)
        {
            const auto& e = events[(size_t) i];
            if (e.fromMidiIn || muteRaw[e.track]->load() > 0.5f) continue;
            sendNoteOn (midi, 36 + e.track, e.accent ? 127 : (int) (e.vel * 110.0f), e.offset);
        }

    // --- parametres FX du bloc (avec modulation globale)
    const uint32_t seed = (uint32_t) seedRaw->load();
    const int paramDiv = (int) dlyDivRaw->load();
    if (! running) dlyDivCur = paramDiv;
    const float dlyFb   = gmod (dlyFbRaw->load(), GDlyFb) * 0.95f;
    const float toneHz  = 500.0f * std::pow (36.0f, gmod (dlyToneRaw->load(), GDlyTone));
    const float toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * toneHz / (float) sr);
    const bool  ping    = dlyPingRaw->load() > 0.5f;
    const float dlyRet  = gmod (dlyRetRaw->load(), GDlyRet);
    const float dlyRnd  = dlyRndRaw->load();
    const float stuChance = gmod (stuChanceRaw->load(), GStutter);
    delay.setTimeSamples ((float) (delayDivPpq (dlyDivCur) * samplesPerBeat));

    bool toAux[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t) toAux[t] = auxL[t] != nullptr && outRaw[t]->load() > 0.5f;
    const int scopeT = scopeTrack.load (std::memory_order_relaxed);
    const int scopeTotal = kScopeLen * scopeDecim;

    // sidechain
    const float duckAmt = gmod (duckAmtRaw->load(), GDuck);
    const int duckSrc = (int) duckSrcRaw->load();
    const float dr = duckRelRaw->load();
    const float duckCoef = std::exp (-1.0f / ((0.03f + dr * dr * 0.6f) * (float) sr));
    // suiveur d'enveloppe (modulateur Follow)
    const int followT = (int) followTrkRaw->load();
    const float fp = modPRaw[5]->load();
    const float followRel = std::exp (-1.0f / ((0.02f + fp * fp * 0.5f) * (float) sr));

    auto* rvL = revBus.getWritePointer (0);
    auto* rvR = revBus.getWritePointer (1);

    // --- passe 1 : voix (+ HQ), sidechain, delay
    int ei = 0, bi = 0;
    for (int s = 0; s < numSamples; ++s)
    {
        while (ei < numEvents && events[(size_t) ei].offset <= s)
            triggerTrack (events[(size_t) ei++]);

        while (bi < numBeats && beatOffsets[(size_t) bi] <= s)
        {
            const uint32_t b = (uint32_t) beatIndex[(size_t) bi];
            if (dlyRnd > 0.0f && hUni (b, 0xD1u, seed) < dlyRnd)
                dlyDivCur = (int) (hUni (b, 0xD2u, seed) * 9.99f);
            else
                dlyDivCur = paramDiv;
            delay.setTimeSamples ((float) (delayDivPpq (dlyDivCur) * samplesPerBeat));
            ++bi;
        }

        const float duckG = 1.0f - duckAmt * duckEnv;
        duckEnv *= duckCoef;

        float L = 0, R = 0, D = 0, V = 0;
        for (int t = 0; t < kNumTracks; ++t)
        {
            auto& tr = tracks[t];
            const float g = (duckAmt > 0.0f && t != duckSrc) ? duckG : 1.0f;
            float tl = 0, trr = 0, td = 0, tv = 0;
            if (! hqActive)
            {
                tr.v[0].process (tl, trr, td, tv);
                tr.v[1].process (tl, trr, td, tv);
            }
            else
            {
                float l2[2] {}, r2[2] {}, d2[2] {}, v2[2] {};
                for (int k = 0; k < 2; ++k)
                {
                    tr.v[0].process (l2[k], r2[k], d2[k], v2[k]);
                    tr.v[1].process (l2[k], r2[k], d2[k], v2[k]);
                }
                tl  = decL[t].process (l2[0], l2[1]);
                trr = decR[t].process (r2[0], r2[1]);
                td = 0.5f * (d2[0] + d2[1]);   // envois : simple moyenne (filtres ensuite par le delay / la reverb)
                tv = 0.5f * (v2[0] + v2[1]);
            }
            tl *= g; trr *= g; td *= g; tv *= g;
            D += td; V += tv;
            if (toAux[t]) { auxL[t][s] += tl; auxR[t][s] += trr; }
            else          { L += tl; R += trr; }
            if (t == followT)
            {
                const float a = std::abs (tl) + std::abs (trr);
                followEnv = a > followEnv ? a : followEnv * followRel;
            }
            if (t == scopeT && scopeWrite < scopeTotal)
            {
                if (scopeWrite % scopeDecim == 0) scopeBuf[scopeWrite / scopeDecim] = (tl + trr) * 0.5f;
                if (++scopeWrite == scopeTotal) scopeVersion.fetch_add (1);
            }
        }

        float dl, drr;
        delay.process (D, D, dl, drr, dlyFb, toneCoef, ping);
        outL[s] = L + dl * dlyRet;
        outR[s] = R + drr * dlyRet;
        rvL[s] = V; rvR[s] = V;
    }
    duckView = duckAmt * duckEnv;

    // --- reverb
    const float revSize = gmod (revSizeRaw->load(), GRevSize);
    if ((int) revTypeRaw->load() == 1)
    {
        plate.setParams (revSize, revDampRaw->load(), revWidthRaw->load(), revPreRaw->load() * 0.15f);
        for (int s = 0; s < numSamples; ++s)
        {
            float l, r;
            plate.process (rvL[s], rvR[s], l, r);
            rvL[s] = l; rvR[s] = r;
        }
    }
    else
    {
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.3f + revSize * 0.69f;
        rp.damping  = revDampRaw->load();
        rp.width    = revWidthRaw->load();
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (rvL, rvR, numSamples);
    }

    // --- passe 2 : somme, stutter, glue, saturation, limiteur
    const float revRet = gmod (revRetRaw->load(), GRevRet) * 0.6f;
    const float glueAmt = glueRaw->load();
    const float mDrive = gmod (mDriveRaw->load(), GMDrive);
    const float mGain = map::levelGain (masterRaw->load());
    const float dg = 1.0f + mDrive * 6.0f;
    const float dNorm = 1.0f / std::tanh (dg);
    float peak = 0.0f;
    bi = 0;
    static constexpr double stuDivBeats[4] { 0.5, 0.25, 0.125, 0.0625 };
    static constexpr double stuLenBeats[4] { 0.5, 1.0, 2.0, 4.0 };

    for (int s = 0; s < numSamples; ++s)
    {
        while (bi < numBeats && beatOffsets[(size_t) bi] <= s)
        {
            const uint32_t b = (uint32_t) beatIndex[(size_t) bi];
            if (stuChance > 0.0f && ! stutter.isActive() && hUni (b, 0x57u, seed) < stuChance)
            {
                int div = (int) stuDivRaw->load();
                if (div >= 4) div = (int) (hUni (b, 0x58u, seed) * 3.99f);
                const int len = juce::jlimit (0, 3, (int) stuLenRaw->load());
                stutter.trigger ((int) (stuDivBeats[div] * samplesPerBeat), (int) (stuLenBeats[len] * samplesPerBeat));
            }
            ++bi;
        }

        float l = outL[s] + rvL[s] * revRet;
        float r = outR[s] + rvR[s] * revRet;
        stutter.process (l, r);
        glue.process (l, r, glueAmt);
        if (mDrive > 0.001f) { l = std::tanh (l * dg) * dNorm; r = std::tanh (r * dg) * dNorm; }
        l = softClip (l * mGain);
        r = softClip (r * mGain);
        outL[s] = l; outR[s] = r;
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
    }
    for (int t = 0; t < kNumTracks; ++t)
        if (toAux[t])
            for (int s = 0; s < numSamples; ++s)
            {
                auxL[t][s] = softClip (auxL[t][s] * mGain);
                auxR[t][s] = softClip (auxR[t][s] * mGain);
            }

    stutterOn = stutter.isActive();
    outPeak = juce::jmax (peak, outPeak.load() * 0.9f);
}

void AleaAudioProcessor::handleAsyncUpdate()
{
    const int n = pendingScene.exchange (-1);
    if (n >= 0) recallScene (n);
}

// ---------------------------------------------------------------------------
int AleaAudioProcessor::editPattern() const { return juce::jlimit (0, kNumPatterns - 1, (int) patternRaw->load()); }

void AleaAudioProcessor::setParamNorm (const juce::String& id, float norm)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        p->endChangeGesture();
    }
}

void AleaAudioProcessor::setParamReal (const juce::String& id, float real)
{
    if (auto* p = apvts.getParameter (id)) setParamNorm (id, p->convertTo0to1 (real));
}

int AleaAudioProcessor::paramIndex (const juce::String& id) const
{
    if (auto* p = apvts.getParameter (id))
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            return wp->getParameterIndex();
    return -1;
}

int AleaAudioProcessor::ccForParam (int idx) const
{
    if (idx < 0) return -1;
    for (int c = 0; c < 128; ++c) if (ccMap[c].load() == idx) return c;
    return -1;
}

void AleaAudioProcessor::clearMidiMapping (int idx)
{
    for (auto& c : ccMap) if (c.load() == idx) c = -1;
    if (learnParam.load() == idx) learnParam = -1;
}

// ---------------------------------------------------------------------------
// Song mode : "A2 B2 A2 C1" (lettre = pattern, chiffre = nombre de mesures, 1 par defaut)
bool AleaAudioProcessor::setChainText (const juce::String& text)
{
    auto tokens = juce::StringArray::fromTokens (text.toUpperCase(), " ,;-", "");
    tokens.removeEmptyStrings();
    int n = 0;
    for (auto& tok : tokens)
    {
        const auto c = tok[0];
        if (c < 'A' || c >= 'A' + kNumPatterns || n >= kMaxChain) return false;
        const int bars = tok.length() > 1 ? tok.substring (1).getIntValue() : 1;
        if (bars < 1 || bars > 64) return false;
        chainPat[n] = (int) (c - 'A');
        chainBars[n] = bars;
        ++n;
    }
    chainLen = n;
    chainText = text;
    return true;
}

// ---------------------------------------------------------------------------
// Echantillons
bool AleaAudioProcessor::loadSample (int t, const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr) return false;
    const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 10.0));
    if (len < 8) return false;

    auto sd = std::make_unique<SampleData>();
    juce::AudioBuffer<float> tmp ((int) reader->numChannels, len);
    reader->read (&tmp, 0, len, 0, true, true);
    sd->data.setSize (1, len);
    sd->data.clear();
    for (int c = 0; c < tmp.getNumChannels(); ++c)
        sd->data.addFrom (0, 0, tmp, c, 0, len, 1.0f / (float) tmp.getNumChannels());
    const float pk = sd->data.getMagnitude (0, len);
    if (pk > 0.0f) sd->data.applyGain (1.0f / pk);
    sd->sampleRate = reader->sampleRate;
    sd->path = f.getFullPathName();
    sd->name = f.getFileNameWithoutExtension();

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (auto* old = sampleSlot[t].exchange (sd.get())) sampleRetired.push_back ({ now, old });
    sampleOwned.push_back (std::move (sd));

    // liberation differee (une voix peut encore lire l'ancien echantillon)
    for (auto it = sampleRetired.begin(); it != sampleRetired.end();)
    {
        if (now - it->first > 10000.0)
        {
            auto* dead = it->second;
            sampleOwned.erase (std::remove_if (sampleOwned.begin(), sampleOwned.end(),
                                               [dead] (const std::unique_ptr<SampleData>& p) { return p.get() == dead; }),
                               sampleOwned.end());
            it = sampleRetired.erase (it);
        }
        else ++it;
    }
    return true;
}

void AleaAudioProcessor::clearSample (int t)
{
    if (auto* old = sampleSlot[t].exchange (nullptr))
        sampleRetired.push_back ({ juce::Time::getMillisecondCounterHiRes(), old });
}

juce::String AleaAudioProcessor::sampleName (int t) const
{
    if (auto* s = sampleSlot[t].load()) return s->name;
    return {};
}

// ---------------------------------------------------------------------------
// Etat
juce::ValueTree AleaAudioProcessor::captureTree()
{
    auto state = apvts.copyState();
    for (auto c : { "PATTERNS", "LOCKS", "MORPHB", "CHAIN", "SAMPLES", "MIDIMAP", "UI", "FAVS", "SCENES", "STYLE", "EVO" })
        if (auto old = state.getChildWithName (c); old.isValid()) state.removeChild (old, nullptr);

    state.appendChild (bank.toTree(), nullptr);

    juce::ValueTree lk ("LOCKS");
    juce::String bits;
    for (int t = 0; t < kNumTracks; ++t)
        for (int i = 0; i <= kNumVP; ++i) bits << (locks[t][i] ? '1' : '0');
    lk.setProperty ("bits", bits, nullptr);
    state.appendChild (lk, nullptr);

    juce::ValueTree mb ("MORPHB");
    mb.setProperty ("has", hasB.load(), nullptr);
    juce::StringArray vals;
    for (int t = 0; t < kNumTracks; ++t)
    {
        vals.add (juce::String (snapBEngine[t].load()));
        for (int vp = 0; vp < kNumVP; ++vp) vals.add (juce::String (snapB[t][vp].load(), 4));
    }
    mb.setProperty ("v", vals.joinIntoString (","), nullptr);
    state.appendChild (mb, nullptr);

    juce::ValueTree ch ("CHAIN");
    ch.setProperty ("text", chainText, nullptr);
    state.appendChild (ch, nullptr);

    juce::ValueTree sm ("SAMPLES");
    for (int t = 0; t < kNumTracks; ++t)
        if (auto* s = sampleSlot[t].load()) sm.setProperty ("t" + juce::String (t), s->path, nullptr);
    state.appendChild (sm, nullptr);

    juce::ValueTree mm ("MIDIMAP");
    auto& params = getParameters();
    for (int c = 0; c < 128; ++c)
    {
        const int idx = ccMap[c].load();
        if (idx >= 0 && idx < params.size())
            if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (params[idx]))
                mm.setProperty ("cc" + juce::String (c), wp->paramID, nullptr);
    }
    state.appendChild (mm, nullptr);

    auto floats = [] (auto getter, int n)
    {
        juce::StringArray a;
        for (int i = 0; i < n; ++i) a.add (juce::String (getter (i), 4));
        return a.joinIntoString (",");
    };

    juce::ValueTree fv ("FAVS");
    for (int t = 0; t < kNumTracks; ++t)
        for (int f = 0; f < kNumFavs; ++f)
            if (favs[t][f].valid)
            {
                juce::ValueTree e ("F");
                e.setProperty ("t", t, nullptr); e.setProperty ("i", f, nullptr); e.setProperty ("eng", favs[t][f].engine, nullptr);
                e.setProperty ("v", floats ([&] (int i) { return favs[t][f].v[i]; }, kNumVP), nullptr);
                fv.appendChild (e, nullptr);
            }
    state.appendChild (fv, nullptr);

    juce::ValueTree sc ("SCENES");
    for (int n = 0; n < kNumScenes; ++n)
        if (sceneValid[n].load())
        {
            juce::ValueTree e ("S");
            e.setProperty ("i", n, nullptr);
            e.setProperty ("eng", floats ([&] (int t) { return (float) sceneEng[n][t].load(); }, kNumTracks), nullptr);
            e.setProperty ("v", floats ([&] (int i) { return sceneVal[n][i / kNumVP][i % kNumVP].load(); }, kNumTracks * kNumVP), nullptr);
            sc.appendChild (e, nullptr);
        }
    state.appendChild (sc, nullptr);

    juce::ValueTree st ("STYLE");
    if (style.valid)
    {
        st.setProperty ("name", style.name, nullptr);
        st.setProperty ("bars", style.bars, nullptr);
        st.setProperty ("on", floats ([&] (int i) { return (&style.on[0][0][0][0])[i]; }, kNumTracks * 16 * 4), nullptr);
        st.setProperty ("acc", floats ([&] (int i) { return (&style.acc[0][0])[i]; }, kNumTracks * 16), nullptr);
        st.setProperty ("hits", floats ([&] (int i) { return (float) style.hits[i]; }, kNumTracks), nullptr);
    }
    state.appendChild (st, nullptr);

    juce::ValueTree ev ("EVO");
    {
        juce::StringArray m;
        for (int i = 0; i < kNumPatterns * kNumTracks; ++i) m.add (juce::String ((juce::int64) evoMask[i / kNumTracks][i % kNumTracks].load()));
        ev.setProperty ("mask", m.joinIntoString (","), nullptr);
    }
    ev.setProperty ("off", floats ([&] (int i) { return evoOff[i / kNumVP][i % kNumVP].load(); }, kNumTracks * kNumVP), nullptr);
    state.appendChild (ev, nullptr);

    juce::ValueTree ui ("UI");
    ui.setProperty ("scale", uiScale.load(), nullptr);
    ui.setProperty ("preset", currentPresetName, nullptr);
    state.appendChild (ui, nullptr);
    return state;
}

void AleaAudioProcessor::restoreTree (const juce::ValueTree& stateIn, bool keepTransport)
{
    auto state = stateIn.createCopy();
    if (keepTransport)
        for (auto id : { "play", "pattern", "fill" })
            for (auto child : state)
                if (child.getProperty ("id").toString() == id)
                    child.setProperty ("value", apvts.getRawParameterValue (id)->load(), nullptr);

    bank.fromTree (state.getChildWithName ("PATTERNS"));
    if (auto lk = state.getChildWithName ("LOCKS"); lk.isValid())
    {
        const auto bits = lk.getProperty ("bits").toString();
        int k = 0;
        for (int t = 0; t < kNumTracks; ++t)
            for (int i = 0; i <= kNumVP; ++i, ++k)
                locks[t][i] = k < bits.length() && bits[k] == '1';
    }
    if (auto mb = state.getChildWithName ("MORPHB"); mb.isValid())
    {
        hasB = (bool) mb.getProperty ("has", false);
        auto vals = juce::StringArray::fromTokens (mb.getProperty ("v").toString(), ",", "");
        int k = 0;
        for (int t = 0; t < kNumTracks; ++t)
        {
            if (k < vals.size()) snapBEngine[t] = vals[k].getIntValue();
            ++k;
            for (int vp = 0; vp < kNumVP; ++vp, ++k)
                if (k < vals.size()) snapB[t][vp] = vals[k].getFloatValue();
        }
    }
    if (auto ch = state.getChildWithName ("CHAIN"); ch.isValid()) setChainText (ch.getProperty ("text").toString());
    if (auto sm = state.getChildWithName ("SAMPLES"); sm.isValid())
        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto path = sm.getProperty ("t" + juce::String (t)).toString();
            auto* cur = sampleSlot[t].load();
            if (path.isEmpty()) { if (cur != nullptr) clearSample (t); }
            else if (cur == nullptr || cur->path != path)
            {
                juce::File f (path);
                if (f.existsAsFile()) loadSample (t, f);
            }
        }
    if (auto mm = state.getChildWithName ("MIDIMAP"); mm.isValid())
    {
        for (auto& c : ccMap) c = -1;
    for (auto& row : evoMask) for (auto& m : row) m = 0u;
    for (int n = 0; n < kNumScenes; ++n)
    {
        sceneValid[n] = false;
        for (int t = 0; t < kNumTracks; ++t)
        {
            sceneEng[n][t] = defaultKit (t).engine;
            for (int vp = 0; vp < kNumVP; ++vp) sceneVal[n][t][vp] = defaultVP (t, vp);
        }
    }
        for (int c = 0; c < 128; ++c)
        {
            const auto id = mm.getProperty ("cc" + juce::String (c)).toString();
            if (id.isNotEmpty()) ccMap[c] = paramIndex (id);
        }
    }
    auto parse = [] (const juce::var& v) { return juce::StringArray::fromTokens (v.toString(), ",", ""); };
    for (auto& row : favs) for (auto& f : row) f.valid = false;
    if (auto fv = state.getChildWithName ("FAVS"); fv.isValid())
        for (auto e : fv)
        {
            const int t = e.getProperty ("t", -1), f = e.getProperty ("i", -1);
            if (t < 0 || t >= kNumTracks || f < 0 || f >= kNumFavs) continue;
            auto vals = parse (e.getProperty ("v"));
            auto& fav = favs[t][f];
            fav.valid = true; fav.engine = e.getProperty ("eng", 0);
            for (int i = 0; i < kNumVP; ++i) fav.v[i] = i < vals.size() ? vals[i].getFloatValue() : defaultVP (t, i);
        }
    for (auto& v : sceneValid) v = false;
    if (auto sc = state.getChildWithName ("SCENES"); sc.isValid())
        for (auto e : sc)
        {
            const int n = e.getProperty ("i", -1);
            if (n < 0 || n >= kNumScenes) continue;
            auto eng = parse (e.getProperty ("eng"));
            auto vals = parse (e.getProperty ("v"));
            for (int t = 0; t < kNumTracks; ++t)
            {
                sceneEng[n][t] = t < eng.size() ? (int) eng[t].getFloatValue() : 0;
                for (int vp = 0; vp < kNumVP; ++vp)
                {
                    const int k = t * kNumVP + vp;
                    sceneVal[n][t][vp] = k < vals.size() ? vals[k].getFloatValue() : defaultVP (t, vp);
                }
            }
            sceneValid[n] = true;
        }
    style = StyleModel();
    if (auto st = state.getChildWithName ("STYLE"); st.isValid() && st.hasProperty ("on"))
    {
        auto on = parse (st.getProperty ("on")), acc = parse (st.getProperty ("acc")), hits = parse (st.getProperty ("hits"));
        float* pOn = &style.on[0][0][0][0];
        for (int i = 0; i < kNumTracks * 16 * 4 && i < on.size(); ++i) pOn[i] = on[i].getFloatValue();
        float* pAcc = &style.acc[0][0];
        for (int i = 0; i < kNumTracks * 16 && i < acc.size(); ++i) pAcc[i] = acc[i].getFloatValue();
        for (int i = 0; i < kNumTracks && i < hits.size(); ++i) style.hits[i] = (int) hits[i].getFloatValue();
        style.name = st.getProperty ("name").toString();
        style.bars = st.getProperty ("bars", 0);
        style.valid = true;
    }
    if (auto ev = state.getChildWithName ("EVO"); ev.isValid())
    {
        auto mask = parse (ev.getProperty ("mask")), off = parse (ev.getProperty ("off"));
        for (int i = 0; i < kNumPatterns * kNumTracks; ++i)
            evoMask[i / kNumTracks][i % kNumTracks] = i < mask.size() ? (uint32_t) mask[i].getLargeIntValue() : 0u;
        for (int i = 0; i < kNumTracks * kNumVP; ++i)
            evoOff[i / kNumVP][i % kNumVP] = i < off.size() ? off[i].getFloatValue() : 0.0f;
    }

    if (auto ui = state.getChildWithName ("UI"); ui.isValid())
    {
        uiScale = (float) ui.getProperty ("scale", 1.0f);
        currentPresetName = ui.getProperty ("preset", currentPresetName).toString();
    }
    apvts.replaceState (state);
}

void AleaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureTree().createXml()) copyXmlToBinary (*xml, destData);
}

void AleaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    restoreTree (juce::ValueTree::fromXml (*xml), false);
}

// ---------------------------------------------------------------------------
// Historique
void AleaAudioProcessor::pushUndo()
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    if (! undoStack.empty() && undoStack.back() == mb) return;
    undoStack.push_back (std::move (mb));
    if (undoStack.size() > 80) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool AleaAudioProcessor::undo()
{
    if (undoStack.empty()) return false;
    juce::MemoryBlock cur;
    getStateInformation (cur);
    redoStack.push_back (std::move (cur));
    auto mb = std::move (undoStack.back());
    undoStack.pop_back();
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize())) restoreTree (juce::ValueTree::fromXml (*xml), true);
    return true;
}

bool AleaAudioProcessor::redo()
{
    if (redoStack.empty()) return false;
    juce::MemoryBlock cur;
    getStateInformation (cur);
    undoStack.push_back (std::move (cur));
    auto mb = std::move (redoStack.back());
    redoStack.pop_back();
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize())) restoreTree (juce::ValueTree::fromXml (*xml), true);
    return true;
}

// ---------------------------------------------------------------------------
// Presets (fichiers)
juce::File AleaAudioProcessor::userPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Alea").getChildFile ("Presets");
}

bool AleaAudioProcessor::savePresetFile (const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    currentPresetName = f.getFileNameWithoutExtension();
    auto xml = captureTree().createXml();
    return xml != nullptr && xml->writeTo (f);
}

bool AleaAudioProcessor::loadPresetFile (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    pushUndo();
    restoreTree (juce::ValueTree::fromXml (*xml), true);
    currentPresetName = f.getFileNameWithoutExtension();
    return true;
}

juce::AudioProcessorEditor* AleaAudioProcessor::createEditor() { return new AleaAudioProcessorEditor (*this); }

#ifndef ALEA_NO_PLUGIN_FILTER
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AleaAudioProcessor(); }
#endif
