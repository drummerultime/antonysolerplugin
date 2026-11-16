#include "CantusProcessor.h"
#include "CantusEditor.h"

using namespace cantus;

const char* CantusAudioProcessor::typeParamId (int i)
{
    static const char* ids[kNumTypes] { "osc1type", "osc2type", "subtype", "ftype", "drivetype", "chord", "glidemode" };
    return ids[juce::jlimit (0, kNumTypes - 1, i)];
}

CantusAudioProcessor::CantusAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TALEA", createCantusLayout())
{
    for (int i = 0; i < kNumTP; ++i)
    {
        tpRaw[i] = apvts.getRawParameterValue (tpId (i));
        jitRaw[i] = apvts.getRawParameterValue (tpJit (i));
        lastNorm[i] = -1.0f;
        snapB[i] = tpInfo (i).def;
        evoOff[i] = 0.0f;
        noteLock[i] = -1.0f;
    }
    for (int i = 0; i < kNumTypes; ++i) { typeRaw[i] = apvts.getRawParameterValue (typeParamId (i)); snapBTypes[i] = 0; }
    for (auto& l : locks) l = false;
    for (int m = 0; m < alea::kNumMods; ++m)
    {
        modRateRaw[m] = apvts.getRawParameterValue ("m" + juce::String (m + 1) + "rate");
        modPRaw[m]    = apvts.getRawParameterValue ("m" + juce::String (m + 1) + "p");
        modValue[m] = 0.5f;
    }
    for (int s = 0; s < kMxSlots; ++s)
    {
        const juce::String pre = "mx" + juce::String (s + 1);
        mxSrcRaw[s] = apvts.getRawParameterValue (pre + "src");
        mxDstRaw[s] = apvts.getRawParameterValue (pre + "dst");
        mxAmtRaw[s] = apvts.getRawParameterValue (pre + "amt");
    }
    for (int n = 0; n < kScenes; ++n)
    {
        sceneValid[n] = false;
        for (int i = 0; i < kNumTP; ++i) sceneVal[n][i] = tpInfo (i).def;
        for (int i = 0; i < kNumTypes; ++i) sceneTypes[n][i] = 0;
    }
    for (int p = 0; p < kPatterns; ++p) { evoMask[p] = 0u; for (auto& e : evoNote[p]) e = 0; }
    for (auto& c : ccMap) c = -1;
    for (int i = 0; i < kChain; ++i) { chainPat[i] = 0; chainBars[i] = 1; }

    auto g = [this] (const char* id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    playRaw = g ("play"); bpmRaw = g ("bpm"); patternRaw = g ("pattern"); seedRaw = g ("seed"); chaosRaw = g ("chaos");
    rndModeRaw = g ("rndmode"); rndDistRaw = g ("rnddist"); rootRaw = g ("root"); scaleRaw = g ("scale"); baseOctRaw = g ("baseoct");
    velSensRaw = g ("velsens"); linkRndRaw = g ("link_rnd");
    swingRaw = g ("swing"); seqChaosRaw = g ("seqchaos"); ratChaosRaw = g ("ratchaos"); humanRaw = g ("humanize"); noteChaosRaw = g ("notechaos");
    densityRaw = g ("density"); fillRaw = g ("fill"); morphRaw = g ("morph"); modDepthRaw = g ("moddepth"); songRaw = g ("songmode");
    midiOutRaw = g ("midiout"); keysRaw = g ("keysmode"); turLenRaw = g ("m3len"); followTrkRaw = g ("m6trk");
    sceneARaw = g ("scenea"); sceneBRaw = g ("sceneb"); sceneXfRaw = g ("scenexf"); sceneOnRaw = g ("sceneon");
    evoAmtRaw = g ("evoamt"); evoBarsRaw = g ("evobars"); evoTargetRaw = g ("evotarget");
    chRateRaw = g ("chrate"); chDepthRaw = g ("chdepth"); chMixRaw = g ("chmix");
    dlyDivRaw = g ("dlydiv"); dlyFbRaw = g ("dlyfb"); dlyToneRaw = g ("dlytone"); dlyPingRaw = g ("dlyping"); dlyRetRaw = g ("dlyret"); dlyRndRaw = g ("dlyrnd");
    revSizeRaw = g ("revsize"); revDampRaw = g ("revdamp"); revWidthRaw = g ("revwidth"); revRetRaw = g ("revret"); revPreRaw = g ("revpre");
    duckAmtRaw = g ("duckamt"); duckSrcRaw = g ("ducksrc"); duckRelRaw = g ("duckrel"); glueRaw = g ("glue"); mDriveRaw = g ("mdrive"); masterRaw = g ("master");
    lkOnRaw = g ("lk_on"); lkPlayRaw = g ("lk_play"); lkTempoRaw = g ("lk_tempo"); chordFollowRaw = g ("chordfollow"); lkSongRaw = g ("lk_song");
    improvRaw = g ("improv"); dialogueRaw = g ("dialogue"); callRespRaw = g ("callresp");
    vibDepthRaw = g ("vibdepth"); vibRateRaw = g ("vibrate"); vibDelayRaw = g ("vibdelay"); genStyleRaw = g ("genstyle");

    for (auto& d : seqDrops) d = 0;
    bank.initDefault();
    setChainText (chainText);
    mods.reset();
    setupLink();
    // melodie de depart (lyrique, sur la tonique tant que Color n'est pas lie)
    generatePattern (0, 0, 1234);
    for (int p = 1; p < kPatterns; ++p) bank.pats[p].copyFrom (bank.pats[0]);
}

CantusAudioProcessor::~CantusAudioProcessor()
{
    cancelPendingUpdate();
    linkSync.reset();
    link.reset();
}

void CantusAudioProcessor::setupLink()
{
    using namespace alea::link;
    link = std::make_unique<Link> (KindCantus);
    linkSync = std::make_unique<LinkSync> (*link, apvts);
    auto bindParam = [this] (Field f, const char* id)
    {
        juce::String pid (id);
        linkSync->bind (f, [this, pid] { return (double) apvts.getRawParameterValue (pid)->load(); },
                           [this, pid] (double v) { setParamReal (pid, (float) v); });
    };
    bindParam (FTempo, "bpm"); bindParam (FPlay, "play"); bindParam (FKey, "root");
    // "Scale: Off" (defaut d'Alea) ne veut rien dire pour une basse : on garde notre gamme
    linkSync->bind (FScale, [this] { return (double) scaleRaw->load(); },
                            [this] (double v) { if (v >= 0.5) setParamReal ("scale", (float) v); });
    bindParam (FPattern, "pattern"); bindParam (FFill, "fill"); bindParam (FChaos, "chaos"); bindParam (FSwing, "swing");
    bindParam (FEvolve, "evoamt"); bindParam (FDensity, "density"); bindParam (FMorph, "morph"); bindParam (FSeed, "seed");
    linkSync->bind (FScene, [this] { return (double) lastRecalledScene.load(); },
                            [this] (double v) { if (v >= 0) recallScene ((int) v); });
    linkSync->onTick = [this]
    {
        if (auto* me = link->self())
        {
            me->root = (int) rootRaw->load();
            me->scale = (int) scaleRaw->load();
        }
    };
}

bool CantusAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.inputBuses.isEmpty();
}

void CantusAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    voice.prepare (sampleRate * 2.0);
    decim.reset();
    chorus.prepare (sampleRate);
    delay.prepare (sampleRate);
    plate.prepare (sampleRate);
    glue.prepare (sampleRate);
    revBus.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    internalRunning = wasRunning = false;
    numPendingOffs = 0; numHeld = 0; gateOffPpq = -1.0;
    condState.reset();
    scopeDecim = juce::jmax (1, (int) (sampleRate * 0.35 / kScopeLen));
    resolveParams();
}

BassContext CantusAudioProcessor::makeContext (int pattern, double patStart, double bpm) const
{
    BassContext c;
    c.pattern = pattern;
    c.patStart = patStart;
    c.seed = (uint32_t) seedRaw->load();
    c.swing = swingRaw->load();
    c.seqChaos = seqChaosRaw->load();
    c.ratChaos = ratChaosRaw->load();
    c.human = humanRaw->load();
    c.density = densityRaw->load();
    c.noteChaos = noteChaosRaw->load();
    c.fill = fillRaw->load() > 0.5f;
    c.bpm = bpm;
    c.scale = (int) scaleRaw->load();
    c.evoMask = &evoMask[juce::jlimit (0, kPatterns - 1, pattern)];
    c.evoNote = evoNote[juce::jlimit (0, kPatterns - 1, pattern)];
    return c;
}

void CantusAudioProcessor::computeMods()
{
    for (auto& v : modOff) v = 0.0f;
    for (auto& v : gOff) v = 0.0f;
    const float depth = modDepthRaw->load();
    for (int s = 0; s < kMxSlots; ++s)
    {
        const int src = (int) mxSrcRaw[s]->load();
        const float amt = mxAmtRaw[s]->load();
        if (src <= 0 || src > alea::kNumMods || amt == 0.0f) continue;
        const float v = (mods.value[src - 1] * 2.0f - 1.0f) * amt * depth * 0.5f;
        const int dst = juce::jlimit (0, kNumTP + kNumGT - 1, (int) mxDstRaw[s]->load());
        if (dst >= kNumTP) gOff[dst - kNumTP] += v;
        else modOff[dst] += v;
    }
}

void CantusAudioProcessor::resolveParams()
{
    const int sA = juce::jlimit (0, kScenes - 1, (int) sceneARaw->load());
    const int sB = juce::jlimit (0, kScenes - 1, (int) sceneBRaw->load());
    const bool sceneMix = sceneOnRaw->load() > 0.5f && sceneValid[sA].load() && sceneValid[sB].load();
    const float xf = sceneXfRaw->load();
    const float morph = hasB.load() ? morphRaw->load() : 0.0f;

    float n[kNumTP];
    for (int i = 0; i < kNumTP; ++i)
    {
        float base = sceneMix ? sceneVal[sA][i].load() + (sceneVal[sB][i].load() - sceneVal[sA][i].load()) * xf : tpRaw[i]->load();
        if (morph > 0.0f) base += (snapB[i].load() - base) * morph;
        if (noteLock[i] >= 0.0f) base = noteLock[i];
        n[i] = juce::jlimit (0.0f, 1.0f, base + noteJit[i] + modOff[i] + evoOff[i].load (std::memory_order_relaxed));
        lastNorm[i].store (n[i], std::memory_order_relaxed);
    }
    int types[kNumTypes];
    for (int i = 0; i < kNumTypes; ++i)
    {
        types[i] = (int) typeRaw[i]->load();
        if (sceneMix) types[i] = (xf < 0.5f ? sceneTypes[sA][i] : sceneTypes[sB][i]).load();
        if (morph > 0.5f) types[i] = snapBTypes[i].load();
    }
    bp.osc1 = types[0]; bp.osc2 = types[1]; bp.sub = types[2]; bp.ftype = types[3]; bp.dtype = types[4]; bp.chord = types[5]; bp.glideMode = types[6];
    bp.shape = n[Shape]; bp.o2lvl = n[O2Level]; bp.o2semi = tmap::semis (n[O2Semi]); bp.o2fine = tmap::cents (n[O2Fine]);
    bp.fm = n[FM]; bp.subLvl = n[SubLevel]; bp.noise = n[NoiseLvl] * 0.5f;
    bp.cutoffHz = tmap::cutoffHz (n[Cutoff]); bp.reso = n[Reso]; bp.envOct = tmap::envOct (n[EnvMod]); bp.fdecay = tmap::fdecaySec (n[FDecay]);
    bp.accentAmt = n[AccentAmt]; bp.keytrack = n[KeyTrack]; bp.fdrive = n[FDrive];
    bp.attack = tmap::attackSec (n[Attack]); bp.decay = tmap::decaySec (n[Decay]); bp.sustain = n[Sustain]; bp.release = tmap::releaseSec (n[Release]);
    bp.glideSec = tmap::glideSec (n[Glide]); bp.drive = n[Drive];
    { const float vd = vibDepthRaw->load(); bp.vibDepth = vd * vd * 0.6f; bp.vibRate = 2.0f + 6.0f * vibRateRaw->load(); bp.vibDelay = vibDelayRaw->load(); }
    level = tmap::levelGain (n[Level]) * 0.55f;
    const float a = (tmap::pan (n[Pan]) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    panL = std::cos (a); panR = std::sin (a);
    sDly = n[SendDly]; sRev = n[SendRev];
}

void CantusAudioProcessor::noteOnInternal (const BassEvent& e, double ppqNow, double ppqPerSample, juce::MidiBuffer& midiOut, int offset)
{
    // alea par note (comme Alea : Hit / Bar / Drift, distribution, correlation)
    const int mode = (int) rndModeRaw->load();
    const int dist = (int) rndDistRaw->load();
    const float chaos = chaosEff * 2.0f;
    const uint32_t seed = (uint32_t) seedRaw->load();
    const float lk = linkRndRaw->load();
    const float wS = std::sqrt (lk), wO = std::sqrt (1.0f - lk);
    float shared;
    if (mode == 0)      shared = shapedBi (dist, e.hitKey, 63u, seed);
    else if (mode == 1) shared = shapedBi (dist, e.barKey, 63u, seed + 7u);
    else { float& d = drift[kNumTP]; d += rng.bi() * 0.3f; d = d > 1.0f ? 2.0f - d : (d < -1.0f ? -2.0f - d : d); shared = d; }
    const BStep* step = (e.pat >= 0 && e.step >= 0) ? &bank.pats[e.pat].steps[e.step] : nullptr;
    for (int i = 0; i < kNumTP; ++i)
    {
        const float j = jitRaw[i]->load() * chaos;
        float o = 0.0f;
        if (j > 0.0f)
        {
            float own;
            if (mode == 0)      own = shapedBi (dist, e.hitKey, (uint32_t) i, seed);
            else if (mode == 1) own = shapedBi (dist, e.barKey, (uint32_t) i, seed + 7u);
            else { float& d = drift[i]; d += rng.bi() * 0.3f; d = d > 1.0f ? 2.0f - d : (d < -1.0f ? -2.0f - d : d); own = d; }
            o = (wS * shared + wO * own) * j * 0.5f;
        }
        noteJit[i] = o;
        noteLock[i] = (step != nullptr && step->plock[i].load() >= 0) ? (float) step->plock[i].load() * 0.001f : -1.0f;
    }
    resolveParams();

    const int root = (int) rootRaw->load();
    float midi = e.absMidi >= 0.0f ? e.absMidi
                                   : (float) (24 + 12 * (int) baseOctRaw->load() + root) + e.semis + transpose;
    if (e.absMidi < 0.0f) midi = followChord (midi, ppqNow);
    const float vsens = velSensRaw->load();
    curVel = juce::jlimit (0.05f, 1.0f, e.vel);
    velGain = (1.0f - vsens) + vsens * std::pow (curVel, 1.3f);
    // legato seulement si la note precedente glissait (slide) ou au clavier : un simple chevauchement
    // (swing, humanize, gate long) redeclenche normalement la note, sinon elle passerait inapercue
    const bool legato = voice.gateOn() && (prevSlide || e.fromMidi);
    prevSlide = e.slideOut;
    if (legato) legatoNotes.fetch_add (1);
    else if (voice.gateOn()) overlapNotes.fetch_add (1);   // ancienne version : note avalee
    voice.noteOn (juce::jlimit (12.0f, 108.0f, midi), curVel, e.accent, legato, bp);
    flash = 1.0f;
    lastNote = (int) std::round (midi);
    scopeWrite = legato ? scopeWrite : 0;

    if (e.absMidi >= 0.0f) gateOffPpq = 1.0e18;                  // tenue par le clavier
    else gateOffPpq = ppqNow + juce::jmax (0.01, e.gatePpq);

    if (midiOutRaw->load() > 0.5f && ! e.fromMidi)
    {
        const int note = juce::jlimit (0, 127, (int) std::round (midi));
        midiOut.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, e.accent ? 127 : (int) (curVel * 110.0f))), offset);
        if (numPendingOffs < (int) pendingOffs.size())
            pendingOffs[(size_t) numPendingOffs++] = { note, offset + (int) (e.gatePpq / ppqPerSample) };
    }
}

// Suivi des accords de Color (AleaLink) : la ligne est transposee sur la fondamentale de l'accord
// (Root : les notes qui frottent d'un demi-ton avec l'accord glissent sur la note de l'accord la plus proche),
// ou chaque note est ramenee sur la note de l'accord la plus proche (Chord tones).
float CantusAudioProcessor::followChord (float midi, double ppq)
{
    const int mode = (int) chordFollowRaw->load();
    if (mode == 0 || link == nullptr || lkOnRaw->load() <= 0.5f) { followChordRoot = -1; return midi; }
    const int slot = link->firstPeerOfKind (alea::link::KindColor);
    linkedColor = slot;
    alea::link::ChordEvent ce;
    if (slot < 0 || ! link->chordAt (slot, isRunning.load() ? ppq : -1.0e8, ce) || ce.mask == 0) { followChordRoot = -1; return midi; }
    followChordRoot = ce.root;
    // Melodic : temps forts sur une note de l'accord, temps faibles dans la gamme de l'accord (Russell) ;
    // Chord tones : toujours une note de l'accord ; Scale : toujours dans la gamme de l'accord
    const double frac = ppq - std::floor (ppq);
    const bool strong = frac < 0.08 || frac > 0.92;
    const int scale = ce.scale != 0 ? ce.scale : ce.mask;
    const int mask = mode == 2 ? ce.mask : (mode == 3 ? scale : (strong ? ce.mask : scale));
    auto in = [mask] (int n) { return ((mask >> (((n % 12) + 12) % 12)) & 1) != 0; };
    int m = (int) std::round (midi);
    for (int d = 0; d <= 6; ++d)
    {
        if (in (m + d) && d > 0 && in (m - d)) { m += (m % 2 == 0 ? d : -d); break; }
        if (in (m - d)) { m -= d; break; }
        if (in (m + d)) { m += d; break; }
    }
    return (float) m + (midi - std::round (midi));
}

void CantusAudioProcessor::evolveStep (int64_t bar)
{
    const float amt = gmod (evoAmtRaw->load(), GEvo);
    if (amt <= 0.0f) return;
    const int target = (int) evoTargetRaw->load();
    const uint32_t seed = (uint32_t) seedRaw->load() ^ 0xE7A1u;
    const uint32_t b = (uint32_t) bar;
    const int pat = currentPattern.load();
    if (target == 0 || target == 2)
    {
        const int len = bank.pats[pat].length.load();
        for (int i = 0; i < len; ++i)
        {
            if (hUni (b, (uint32_t) i, seed) < amt * 0.07f) evoMask[pat].fetch_xor (1u << i);
            if (hUni (b, (uint32_t) i + 100u, seed) < amt * 0.10f)
            {
                const int v = juce::jlimit (-4, 4, (int) evoNote[pat][i].load() + (hUni (b, (uint32_t) i + 200u, seed) < 0.5f ? -1 : 1));
                evoNote[pat][i] = (int8_t) v;
            }
        }
    }
    if (target == 1 || target == 2)
        for (int i = 0; i < kNumTP; ++i)
        {
            if (i == Level) continue;
            const float v = evoOff[i].load() + hBi (b, (uint32_t) i + 300u, seed) * amt * 0.06f;
            evoOff[i].store (juce::jlimit (-0.3f, 0.3f, v));
        }
}

void CantusAudioProcessor::handleAsyncUpdate()
{
    const int n = pendingScene.exchange (-1);
    if (n >= 0) recallScene (n);
    if (improvRequested.exchange (false) && ! improvReady.load())
    {
        // phrase suivante : developpement de la phrase en cours (ou nouvelle phrase selon Development)
        generateMelodyInto (improvNext, (int) genStyleRaw->load(), (int64_t) juce::Random::getSystemRandom().nextInt (1 << 30),
                            &bank.pats[currentPattern.load()]);
        improvReady = true;
    }
}

// ---------------------------------------------------------------------------
void CantusAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    if (numSamples == 0) return;
    if (revBus.getNumSamples() < numSamples) revBus.setSize (2, numSamples, false, false, true);
    revBus.clear();

    // --- transport (hote, lecture interne, ou horloge partagee AleaLink)
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
    const bool linkClock = link != nullptr && lkOnRaw->load() > 0.5f && (lkPlayRaw->load() > 0.5f || lkTempoRaw->load() > 0.5f);

    bool running = false;
    double ppqStart = 0.0;
    if (hostPlaying) { running = true; ppqStart = hostPpq; internalRunning = false; }
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
    const double tl0 = running ? ppqStart : freeModClock;     // ligne de temps pour les portes
    const bool justStarted = running && ! wasRunning;
    if (justStarted)
    {
        patStartPpq = std::floor (ppqStart / 4.0) * 4.0;
        songStartBar = (int64_t) std::floor (ppqStart / 4.0);
        condState.reset();
        lastDuckPpq = lastFollowPpq = -1.0;
    }
    if (! running && wasRunning && gateOffPpq < 1.0e17) { voice.noteOff(); gateOffPpq = -1.0; }
    wasRunning = running;

    const int paramPat = (int) patternRaw->load();
    if (paramPat != lastParamPattern) { lastParamPattern = paramPat; requestedPattern = paramPat; }
    numEvents = 0; numBeats = 0;

    // --- MIDI entrant : C0-G0 scenes, C1-G1 patterns, >= C2 clavier (transposition ou jeu), CC learn
    auto& params = getParameters();
    const int keysMode = (int) keysRaw->load();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int off = juce::jlimit (0, numSamples - 1, meta.samplePosition);
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            if (note >= 24 && note < 24 + kScenes) { pendingScene = note - 24; triggerAsyncUpdate(); }
            else if (note >= 36 && note < 36 + kPatterns) requestedPattern = note - 36;
            else if (note >= 48)
            {
                if (keysMode == 1) transpose = (float) (note - 60);
                else if (keysMode == 2)
                {
                    if (numHeld < 16) heldKeys[numHeld++] = note;
                    BassEvent e; e.offset = off; e.absMidi = (float) note; e.vel = m.getFloatVelocity(); e.accent = m.getVelocity() >= 120;
                    e.fromMidi = true; e.hitKey = e.barKey = rng.next(); e.gatePpq = 1.0e9;
                    if (numEvents < (int) events.size()) events[(size_t) numEvents++] = e;
                }
            }
        }
        else if (m.isNoteOff() && keysMode == 2 && m.getNoteNumber() >= 48)
        {
            const int note = m.getNoteNumber();
            int k = 0;
            for (int i = 0; i < numHeld; ++i) if (heldKeys[i] != note) heldKeys[k++] = heldKeys[i];
            numHeld = k;
            BassEvent e; e.offset = off; e.fromMidi = true; e.hitKey = rng.next();
            if (numHeld > 0) { e.absMidi = (float) heldKeys[numHeld - 1]; e.vel = curVel; e.gatePpq = 1.0e9; }   // retour legato
            else e.absMidi = -2.0f;                                                                               // relachement
            if (numEvents < (int) events.size()) events[(size_t) numEvents++] = e;
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const int learn = learnParam.load();
            if (learn >= 0 && learn < params.size())
            {
                for (auto& c : ccMap) if (c.load() == learn) c = -1;
                ccMap[cc] = learn; learnParam = -1;
            }
            const int idx = ccMap[cc].load();
            if (idx >= 0 && idx < params.size()) params[idx]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        }
    }
    midi.clear();
    for (int i = 0; i < numPendingOffs;)
    {
        auto& p = pendingOffs[(size_t) i];
        if (p.remaining < numSamples) { midi.addEvent (juce::MidiMessage::noteOff (1, p.note), juce::jmax (0, p.remaining)); p = pendingOffs[(size_t) --numPendingOffs]; }
        else { p.remaining -= numSamples; ++i; }
    }
    if (guiTrig.exchange (false) && numEvents < (int) events.size())
    {
        BassEvent e; e.semis = 0; e.vel = 0.9f; e.gatePpq = 0.5; e.hitKey = rng.next(); e.barKey = rng.next();
        events[(size_t) numEvents++] = e;
    }

    // --- song mode, changement de pattern
    if (running && songRaw->load() > 0.5f && chainLen.load() > 0)
    {
        int total = 0; const int len = chainLen.load();
        for (int i = 0; i < len; ++i) total += chainBars[i].load();
        if (total > 0)
        {
            int64_t bar = (int64_t) std::floor (ppqEnd / 4.0) - songStartBar;
            bar = ((bar % total) + total) % total;
            int acc = 0;
            for (int i = 0; i < len; ++i) { acc += chainBars[i].load(); if (bar < acc) { requestedPattern = chainPat[i].load(); songPos = i; break; } }
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
                requestedPattern = juce::jlimit (0, kPatterns - 1, sp);
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
    if (requestedPattern != currentPattern.load() && (! running || barCrossed || justStarted))
    {
        currentPattern = requestedPattern;
        patStartPpq = running ? std::floor ((justStarted ? ppqStart : ppqEnd) / 4.0) * 4.0 : 0.0;
    }

    // improvisation : nouvelle phrase toutes les N mesures (preparee a l'avance sur le thread message)
    {
        static constexpr int barsN[] { 0, 1, 2, 4, 8 };
        const int nb = barsN[juce::jlimit (0, 4, (int) improvRaw->load())];
        if (nb > 0)
        {
            const int64_t barNow = (int64_t) std::floor (ppqEnd / 4.0);
            if (running && barCrossed && barNow % nb == 0 && barNow != lastImprovBar && improvReady.load())
            {
                lastImprovBar = barNow;
                bank.pats[currentPattern.load()].copyFrom (improvNext);
                improvReady = false;
                improvCount.fetch_add (1);
            }
            if (! improvReady.load() && ! improvRequested.exchange (true)) triggerAsyncUpdate();
        }
    }

    // --- batterie liee (Alea) : sidechain et modulateur "Drums"
    std::array<int, 32> duckOffsets {}; int numDuck = 0;
    std::array<int, 32> followOffsets {}; int numFollow = 0;
    const int duckSrc = (int) duckSrcRaw->load() - 1;
    const int followTrk = (int) followTrkRaw->load();
    if (link != nullptr && lkOnRaw->load() > 0.5f && running)
    {
        const int slot = link->firstPeerOfKind (alea::link::KindAlea);
        linkedDrums = slot;
        if (slot >= 0)
        {
            alea::link::Hit hits[alea::link::kMaxHits];
            const int nh = link->readHits (slot, hits, alea::link::kMaxHits);
            for (int i = 0; i < nh; ++i)
            {
                const auto& h = hits[i];
                const bool inBlock = h.ppq < ppqEnd && h.ppq >= ppqStart - 0.25;    // un coup publie en retard est rattrape
                if (! inBlock) continue;
                const int off = juce::jlimit (0, numSamples - 1, (int) ((h.ppq - ppqStart) / ppqPerSample));
                if (h.track == duckSrc && h.ppq > lastDuckPpq + 1.0e-6 && numDuck < 32) { duckOffsets[(size_t) numDuck++] = off; lastDuckPpq = h.ppq; }
                if (h.track == followTrk && h.ppq > lastFollowPpq + 1.0e-6 && numFollow < 32) { followOffsets[(size_t) numFollow++] = off; lastFollowPpq = h.ppq; }
            }
        }
    }
    else linkedDrums = link != nullptr ? link->firstPeerOfKind (alea::link::KindAlea) : -1;

    // --- modulateurs
    {
        const double clock = running ? ppqEnd : (freeModClock + numSamples * ppqPerSample);
        int rates[alea::kNumMods]; float ps[alea::kNumMods];
        for (int m = 0; m < alea::kNumMods; ++m) { rates[m] = (int) modRateRaw[m]->load(); ps[m] = modPRaw[m]->load(); }
        mods.setFollow (followEnv);
        mods.advance (clock, rates, ps, (int) turLenRaw->load(), (uint32_t) seedRaw->load());
        for (int m = 0; m < alea::kNumMods; ++m) modValue[m].store (mods.value[m], std::memory_order_relaxed);
        computeMods();
    }
    if (! running) freeModClock += numSamples * ppqPerSample;
    chaosEff = gmod (chaosRaw->load(), GChaos);

    if (running && barCrossed && evoAmtRaw->load() + gOff[GEvo] > 0.0f)
    {
        static constexpr int barsN[] { 1, 2, 4, 8, 16 };
        const int64_t bar = (int64_t) std::floor (ppqEnd / 4.0);
        if (bar % barsN[juce::jlimit (0, 4, (int) evoBarsRaw->load())] == 0) evolveStep (bar);
    }

    if (running)
    {
        auto ctx = makeContext (currentPattern.load(), patStartPpq, bpm);
        ctx.density = gmod (ctx.density, GDensity);
        ctx.seqChaos = gmod (ctx.seqChaos, GSeqChaos);
        ctx.ratChaos = gmod (ctx.ratChaos, GRatChaos);
        ctx.swing = gmod (ctx.swing, GSwing);
        // dialogue avec la basse (Talea) : la melodie laisse la place aux attaques de la basse ;
        // question / reponse : la melodie se tait une phrase sur deux
        alea::link::Hit bassHits[alea::link::kMaxHits]; int nBass = 0;
        const float dialogue = dialogueRaw->load();
        if (dialogue > 0.0f && link != nullptr && lkOnRaw->load() > 0.5f)
        {
            const int ts = link->firstPeerOfKind (alea::link::KindTalea);
            if (ts >= 0) nBass = link->readHits (ts, bassHits, alea::link::kMaxHits);
        }
        static constexpr int crBars[] { 0, 1, 2, 4 };
        const int cr = crBars[juce::jlimit (0, 3, (int) callRespRaw->load())];
        ctx.drops = seqDrops;
        runBassSequencer (bank, ctx, &condState, ppqStart, ppqEnd, [&] (double te, BassEvent e)
        {
            if (cr > 0 && ((int64_t) std::floor (te / (4.0 * cr)) & 1) != 0) { callRespDrops.fetch_add (1); return; }
            {
                // la melodie ne cede que des notes de passage : jamais sur un temps ni un demi-temps,
                // une note au plus par temps, et seulement quand la basse attaque au meme instant
                const double frac = te - std::floor (te);
                const bool weak = std::abs (frac - 0.25) < 0.06 || std::abs (frac - 0.75) < 0.06;
                const int64_t stepK = (int64_t) std::floor (te * 4.0 + 0.5);
                if (weak && stepK > lastDialogueDrop + 3)
                    for (int h = 0; h < nBass; ++h)
                        if (std::abs (bassHits[h].ppq - te) < 0.04 && hUni (e.hitKey, 0xD1A1u, 0x0Bu) < dialogue * 0.4f)
                        {
                            lastDialogueDrop = stepK;
                            dialogueDrops.fetch_add (1);
                            return;
                        }
            }
            notesPlayed.fetch_add (1);
            e.offset = juce::jlimit (0, numSamples - 1, (int) ((te - ppqStart) / ppqPerSample));
            if (numEvents < (int) events.size()) events[(size_t) numEvents++] = e;
        });
        const auto& pat = bank.pats[currentPattern.load()];
        playStep = pat.stepIndex ((int64_t) std::floor (ppqEnd / ratePpq (pat.rate.load())), ctx.seed * 16u + 11u);
        for (int64_t b = (int64_t) std::ceil (ppqStart); (double) b < ppqEnd && numBeats < (int) beatOffsets.size(); ++b)
        {
            beatOffsets[(size_t) numBeats] = juce::jlimit (0, numSamples - 1, (int) (((double) b - ppqStart) / ppqPerSample));
            beatIndex[(size_t) numBeats++] = b;
        }
        if (internalRunning) internalPpq = ppqEnd;
    }
    else playStep = -1;
    playPpq = ppqEnd; patStartShared = patStartPpq; isRunning = running;

    if (link != nullptr)
        if (auto* me = link->self())
        {
            me->bpm = bpm; me->ppq = ppqEnd; me->playing = running ? 1 : 0; me->pattern = currentPattern.load();
            me->level = outPeak.load();
        }
    if (link != nullptr && link->connected())
    {
        // notes de basse a venir (2 temps) : Cantus s'en sert pour dialoguer avec la basse
        int n = 0;
        if (running && lkOnRaw->load() > 0.5f)
        {
            auto ctx = makeContext (currentPattern.load(), patStartPpq, bpm);
            ctx.density = gmod (ctx.density, GDensity);
            ctx.seqChaos = gmod (ctx.seqChaos, GSeqChaos);
            ctx.ratChaos = gmod (ctx.ratChaos, GRatChaos);
            ctx.swing = gmod (ctx.swing, GSwing);
            BassCondState cs = condState;
            runBassSequencer (bank, ctx, &cs, ppqStart, ppqStart + 2.0, [&] (double te, BassEvent e)
            {
                if (n < alea::link::kMaxHits) linkHits[(size_t) n++] = { te, juce::jlimit (0, 127, (int) (24 + 12 * (int) baseOctRaw->load() + (int) rootRaw->load() + e.semis)), e.vel };
            });
        }
        link->publishHits (linkHits.data(), n);
    }

    std::sort (events.begin(), events.begin() + numEvents, [] (const BassEvent& a, const BassEvent& b) { return a.offset < b.offset; });

    // --- parametres du bloc
    resolveParams();
    const uint32_t seed = (uint32_t) seedRaw->load();
    const int paramDiv = (int) dlyDivRaw->load();
    if (! running) dlyDivCur = paramDiv;
    const float dlyFb = gmod (dlyFbRaw->load(), GDlyFb) * 0.95f;
    const float toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (500.0f * std::pow (36.0f, dlyToneRaw->load())) / (float) sr);
    const bool ping = dlyPingRaw->load() > 0.5f;
    const float dlyRet = gmod (dlyRetRaw->load(), GDlyRet);
    const float dlyRnd = dlyRndRaw->load();
    const float chMix = gmod (chMixRaw->load(), GChorus), chRate = chRateRaw->load(), chDepth = chDepthRaw->load();
    delay.setTimeSamples ((float) (delayDivPpq (dlyDivCur) * samplesPerBeat));
    const float duckAmt = duckSrc >= 0 ? gmod (duckAmtRaw->load(), GDuck) : 0.0f;
    const float dr = duckRelRaw->load();
    const float duckCoef = std::exp (-1.0f / ((0.03f + dr * dr * 0.6f) * (float) sr));
    const float fp = modPRaw[5]->load();
    const float followCoef = std::exp (-1.0f / ((0.02f + fp * fp * 0.5f) * (float) sr));
    const int scopeTotal = kScopeLen * scopeDecim;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    auto* rvL = revBus.getWritePointer (0);
    auto* rvR = revBus.getWritePointer (1);
    int ei = 0, bi = 0, di = 0, fi = 0;
    for (int s = 0; s < numSamples; ++s)
    {
        const double now = tl0 + s * ppqPerSample;
        while (di < numDuck && duckOffsets[(size_t) di] <= s) { duckEnv = 1.0f; ++di; }
        while (fi < numFollow && followOffsets[(size_t) fi] <= s) { followEnv = 1.0f; ++fi; }
        while (bi < numBeats && beatOffsets[(size_t) bi] <= s)
        {
            const uint32_t b = (uint32_t) beatIndex[(size_t) bi];
            dlyDivCur = (dlyRnd > 0.0f && hUni (b, 0xD1u, seed) < dlyRnd) ? (int) (hUni (b, 0xD2u, seed) * 9.99f) : paramDiv;
            delay.setTimeSamples ((float) (delayDivPpq (dlyDivCur) * samplesPerBeat));
            ++bi;
        }
        if (gateOffPpq >= 0.0 && now >= gateOffPpq) { voice.noteOff(); gateOffPpq = -1.0; }
        while (ei < numEvents && events[(size_t) ei].offset <= s)
        {
            const auto& e = events[(size_t) ei++];
            if (e.absMidi == -2.0f) { voice.noteOff(); gateOffPpq = -1.0; continue; }
            noteOnInternal (e, now, ppqPerSample, midi, s);
        }

        const float a = voice.process (bp), b2 = voice.process (bp);
        float y = decim.process (a, b2);
        // anti-continu (saturation asymetrique)
        const float yy = y - dcX + 0.9995f * dcY; dcX = y; dcY = yy; y = yy;
        y *= level * velGain;

        if (scopeWrite < scopeTotal)
        {
            if (scopeWrite % scopeDecim == 0) scopeBuf[scopeWrite / scopeDecim] = y;
            if (++scopeWrite == scopeTotal) scopeVersion.fetch_add (1);
        }

        const float g = 1.0f - duckAmt * duckEnv;
        duckEnv *= duckCoef;
        followEnv *= followCoef;
        y *= g;
        float l = y * panL, r = y * panR;
        chorus.process (l, r, chRate, chDepth, chMix);
        float dl, drr;
        delay.process (y * sDly, y * sDly, dl, drr, dlyFb, toneCoef, ping);
        outL[s] = l + dl * dlyRet;
        outR[s] = r + drr * dlyRet;
        rvL[s] = y * sRev; rvR[s] = y * sRev;
    }
    duckView = duckAmt * duckEnv;

    plate.setParams (gmod (revSizeRaw->load(), GRevSize), revDampRaw->load(), revWidthRaw->load(), revPreRaw->load() * 0.15f);
    const float revRet = gmod (revRetRaw->load(), GRevRet) * 0.6f;
    const float glueAmt = glueRaw->load();
    const float mDrive = gmod (mDriveRaw->load(), GMDrive);
    const float mGain = tmap::levelGain (masterRaw->load());
    const float dg = 1.0f + mDrive * 6.0f, dNorm = 1.0f / std::tanh (dg);
    float peak = 0.0f;
    for (int s = 0; s < numSamples; ++s)
    {
        float wl, wr;
        plate.process (rvL[s], rvR[s], wl, wr);
        float l = outL[s] + wl * revRet, r = outR[s] + wr * revRet;
        glue.process (l, r, glueAmt);
        if (mDrive > 0.001f) { l = std::tanh (l * dg) * dNorm; r = std::tanh (r * dg) * dNorm; }
        l = alea::softClip (l * mGain); r = alea::softClip (r * mGain);
        outL[s] = l; outR[s] = r;
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
    }
    outPeak = juce::jmax (peak, outPeak.load() * 0.9f);
}

// ---------------------------------------------------------------------------
int CantusAudioProcessor::editPattern() const { return juce::jlimit (0, kPatterns - 1, (int) patternRaw->load()); }

void CantusAudioProcessor::setParamNorm (const juce::String& id, float norm)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        p->endChangeGesture();
    }
}

void CantusAudioProcessor::setParamReal (const juce::String& id, float real)
{
    if (auto* p = apvts.getParameter (id)) setParamNorm (id, p->convertTo0to1 (real));
}

int CantusAudioProcessor::paramIndex (const juce::String& id) const
{
    if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (apvts.getParameter (id))) return p->getParameterIndex();
    return -1;
}

int CantusAudioProcessor::ccForParam (int idx) const
{
    if (idx < 0) return -1;
    for (int c = 0; c < 128; ++c) if (ccMap[c].load() == idx) return c;
    return -1;
}

void CantusAudioProcessor::clearMidiMapping (int idx)
{
    for (auto& c : ccMap) if (c.load() == idx) c = -1;
    if (learnParam.load() == idx) learnParam = -1;
}

bool CantusAudioProcessor::setChainText (const juce::String& text)
{
    auto tokens = juce::StringArray::fromTokens (text.toUpperCase(), " ,;-", "");
    tokens.removeEmptyStrings();
    int n = 0;
    for (auto& tok : tokens)
    {
        const auto c = tok[0];
        if (c < 'A' || c >= 'A' + kPatterns || n >= kChain) return false;
        const int bars = tok.length() > 1 ? tok.substring (1).getIntValue() : 1;
        if (bars < 1 || bars > 64) return false;
        chainPat[n] = (int) (c - 'A'); chainBars[n] = bars; ++n;
    }
    chainLen = n; chainText = text;
    return true;
}

bool CantusAudioProcessor::drumMask (int track, uint32_t& mask, int& len) const
{
    if (link == nullptr) return false;
    const int slot = link->firstPeerOfKind (alea::link::KindAlea);
    int rate = 1;
    return slot >= 0 && link->readTrack (slot, track, mask, len, rate);
}

// ---------------------------------------------------------------------------
// Etat
juce::ValueTree CantusAudioProcessor::captureTree()
{
    auto state = apvts.copyState();
    for (auto c : { "BASSPATTERNS", "LOCKS", "MORPHB", "SCENES", "FAVS", "EVO", "STYLE", "CHAIN", "MIDIMAP", "UI" })
        if (auto old = state.getChildWithName (c); old.isValid()) state.removeChild (old, nullptr);
    state.appendChild (bank.toTree(), nullptr);

    auto floats = [] (auto getter, int n)
    {
        juce::StringArray a;
        for (int i = 0; i < n; ++i) a.add (juce::String (getter (i), 4));
        return a.joinIntoString (",");
    };
    juce::ValueTree lk ("LOCKS");
    juce::String bits;
    for (auto& l : locks) bits << (l.load() ? '1' : '0');
    lk.setProperty ("bits", bits, nullptr);
    state.appendChild (lk, nullptr);

    juce::ValueTree mb ("MORPHB");
    mb.setProperty ("has", hasB.load(), nullptr);
    mb.setProperty ("v", floats ([&] (int i) { return snapB[i].load(); }, kNumTP), nullptr);
    mb.setProperty ("t", floats ([&] (int i) { return (float) snapBTypes[i].load(); }, kNumTypes), nullptr);
    state.appendChild (mb, nullptr);

    juce::ValueTree sc ("SCENES");
    for (int n = 0; n < kScenes; ++n)
        if (sceneValid[n].load())
        {
            juce::ValueTree e ("S");
            e.setProperty ("i", n, nullptr);
            e.setProperty ("v", floats ([&] (int i) { return sceneVal[n][i].load(); }, kNumTP), nullptr);
            e.setProperty ("t", floats ([&] (int i) { return (float) sceneTypes[n][i].load(); }, kNumTypes), nullptr);
            sc.appendChild (e, nullptr);
        }
    state.appendChild (sc, nullptr);

    juce::ValueTree fv ("FAVS");
    for (int f = 0; f < kFavs; ++f)
        if (favs[f].valid)
        {
            juce::ValueTree e ("F");
            e.setProperty ("i", f, nullptr);
            e.setProperty ("v", floats ([&] (int i) { return favs[f].v[i]; }, kNumTP), nullptr);
            e.setProperty ("t", floats ([&] (int i) { return (float) favs[f].types[i]; }, kNumTypes), nullptr);
            fv.appendChild (e, nullptr);
        }
    state.appendChild (fv, nullptr);

    juce::ValueTree ev ("EVO");
    {
        juce::StringArray m, nn;
        for (int p = 0; p < kPatterns; ++p)
        {
            m.add (juce::String ((juce::int64) evoMask[p].load()));
            for (int i = 0; i < kSteps; ++i) nn.add (juce::String ((int) evoNote[p][i].load()));
        }
        ev.setProperty ("mask", m.joinIntoString (","), nullptr);
        ev.setProperty ("note", nn.joinIntoString (","), nullptr);
        ev.setProperty ("off", floats ([&] (int i) { return evoOff[i].load(); }, kNumTP), nullptr);
    }
    state.appendChild (ev, nullptr);

    juce::ValueTree st ("STYLE");
    if (style.valid)
    {
        st.setProperty ("name", style.name, nullptr);
        st.setProperty ("bars", style.bars, nullptr);
        st.setProperty ("on", floats ([&] (int i) { return (&style.on[0][0][0])[i]; }, 64), nullptr);
        st.setProperty ("acc", floats ([&] (int i) { return style.acc[i]; }, 16), nullptr);
        st.setProperty ("slide", floats ([&] (int i) { return style.slide[i]; }, 16), nullptr);
        st.setProperty ("iv", floats ([&] (int i) { return style.interval[i]; }, 25), nullptr);
        st.setProperty ("deg", floats ([&] (int i) { return style.degree[i]; }, 12), nullptr);
    }
    state.appendChild (st, nullptr);

    juce::ValueTree ch ("CHAIN");
    ch.setProperty ("text", chainText, nullptr);
    state.appendChild (ch, nullptr);

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

    juce::ValueTree ui ("UI");
    ui.setProperty ("scale", uiScale.load(), nullptr);
    ui.setProperty ("preset", currentPresetName, nullptr);
    state.appendChild (ui, nullptr);
    return state;
}

void CantusAudioProcessor::restoreTree (const juce::ValueTree& stateIn, bool keepTransport)
{
    auto state = stateIn.createCopy();
    if (keepTransport)
        for (auto id : { "play", "pattern", "fill" })
            for (auto child : state)
                if (child.getProperty ("id").toString() == id)
                    child.setProperty ("value", apvts.getRawParameterValue (id)->load(), nullptr);

    auto parse = [] (const juce::var& v) { return juce::StringArray::fromTokens (v.toString(), ",", ""); };
    bank.fromTree (state.getChildWithName ("BASSPATTERNS"));
    if (auto lk = state.getChildWithName ("LOCKS"); lk.isValid())
    {
        const auto bits = lk.getProperty ("bits").toString();
        for (int i = 0; i <= kNumTP; ++i) locks[i] = i < bits.length() && bits[i] == '1';
    }
    if (auto mb = state.getChildWithName ("MORPHB"); mb.isValid())
    {
        hasB = (bool) mb.getProperty ("has", false);
        auto v = parse (mb.getProperty ("v")), t = parse (mb.getProperty ("t"));
        for (int i = 0; i < kNumTP && i < v.size(); ++i) snapB[i] = v[i].getFloatValue();
        for (int i = 0; i < kNumTypes && i < t.size(); ++i) snapBTypes[i] = (int) t[i].getFloatValue();
    }
    for (auto& v : sceneValid) v = false;
    if (auto sc = state.getChildWithName ("SCENES"); sc.isValid())
        for (auto e : sc)
        {
            const int n = e.getProperty ("i", -1);
            if (n < 0 || n >= kScenes) continue;
            auto v = parse (e.getProperty ("v")), t = parse (e.getProperty ("t"));
            for (int i = 0; i < kNumTP; ++i) sceneVal[n][i] = i < v.size() ? v[i].getFloatValue() : tpInfo (i).def;
            for (int i = 0; i < kNumTypes; ++i) sceneTypes[n][i] = i < t.size() ? (int) t[i].getFloatValue() : 0;
            sceneValid[n] = true;
        }
    for (auto& f : favs) f.valid = false;
    if (auto fv = state.getChildWithName ("FAVS"); fv.isValid())
        for (auto e : fv)
        {
            const int f = e.getProperty ("i", -1);
            if (f < 0 || f >= kFavs) continue;
            auto v = parse (e.getProperty ("v")), t = parse (e.getProperty ("t"));
            for (int i = 0; i < kNumTP; ++i) favs[f].v[i] = i < v.size() ? v[i].getFloatValue() : tpInfo (i).def;
            for (int i = 0; i < kNumTypes; ++i) favs[f].types[i] = i < t.size() ? (int) t[i].getFloatValue() : 0;
            favs[f].valid = true;
        }
    if (auto ev = state.getChildWithName ("EVO"); ev.isValid())
    {
        auto m = parse (ev.getProperty ("mask")), nn = parse (ev.getProperty ("note")), off = parse (ev.getProperty ("off"));
        for (int p = 0; p < kPatterns; ++p)
        {
            evoMask[p] = p < m.size() ? (uint32_t) m[p].getLargeIntValue() : 0u;
            for (int i = 0; i < kSteps; ++i) { const int k = p * kSteps + i; evoNote[p][i] = (int8_t) (k < nn.size() ? nn[k].getIntValue() : 0); }
        }
        for (int i = 0; i < kNumTP; ++i) evoOff[i] = i < off.size() ? off[i].getFloatValue() : 0.0f;
    }
    style = Style();
    if (auto st = state.getChildWithName ("STYLE"); st.isValid() && st.hasProperty ("on"))
    {
        auto on = parse (st.getProperty ("on")), acc = parse (st.getProperty ("acc")), sl = parse (st.getProperty ("slide")),
             iv = parse (st.getProperty ("iv")), dg = parse (st.getProperty ("deg"));
        for (int i = 0; i < 64 && i < on.size(); ++i) (&style.on[0][0][0])[i] = on[i].getFloatValue();
        for (int i = 0; i < 16 && i < acc.size(); ++i) style.acc[i] = acc[i].getFloatValue();
        for (int i = 0; i < 16 && i < sl.size(); ++i) style.slide[i] = sl[i].getFloatValue();
        for (int i = 0; i < 25 && i < iv.size(); ++i) style.interval[i] = iv[i].getFloatValue();
        for (int i = 0; i < 12 && i < dg.size(); ++i) style.degree[i] = dg[i].getFloatValue();
        style.name = st.getProperty ("name").toString(); style.bars = st.getProperty ("bars", 0); style.valid = true;
    }
    if (auto ch = state.getChildWithName ("CHAIN"); ch.isValid()) setChainText (ch.getProperty ("text").toString());
    if (auto mm = state.getChildWithName ("MIDIMAP"); mm.isValid())
    {
        for (auto& c : ccMap) c = -1;
        for (int c = 0; c < 128; ++c)
        {
            const auto id = mm.getProperty ("cc" + juce::String (c)).toString();
            if (id.isNotEmpty()) ccMap[c] = paramIndex (id);
        }
    }
    if (auto ui = state.getChildWithName ("UI"); ui.isValid())
    {
        uiScale = (float) ui.getProperty ("scale", 1.0f);
        currentPresetName = ui.getProperty ("preset", currentPresetName).toString();
    }
    apvts.replaceState (state);
}

void CantusAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureTree().createXml()) copyXmlToBinary (*xml, destData);
}

void CantusAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    restoreTree (juce::ValueTree::fromXml (*xml), false);
}

void CantusAudioProcessor::pushUndo()
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    if (! undoStack.empty() && undoStack.back() == mb) return;
    undoStack.push_back (std::move (mb));
    if (undoStack.size() > 80) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool CantusAudioProcessor::undo()
{
    if (undoStack.empty()) return false;
    juce::MemoryBlock cur; getStateInformation (cur);
    redoStack.push_back (std::move (cur));
    auto mb = std::move (undoStack.back()); undoStack.pop_back();
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize())) restoreTree (juce::ValueTree::fromXml (*xml), true);
    return true;
}

bool CantusAudioProcessor::redo()
{
    if (redoStack.empty()) return false;
    juce::MemoryBlock cur; getStateInformation (cur);
    undoStack.push_back (std::move (cur));
    auto mb = std::move (redoStack.back()); redoStack.pop_back();
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize())) restoreTree (juce::ValueTree::fromXml (*xml), true);
    return true;
}

juce::File CantusAudioProcessor::userPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Cantus").getChildFile ("Presets");
}

bool CantusAudioProcessor::savePresetFile (const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    currentPresetName = f.getFileNameWithoutExtension();
    auto xml = captureTree().createXml();
    return xml != nullptr && xml->writeTo (f);
}

bool CantusAudioProcessor::loadPresetFile (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    pushUndo();
    restoreTree (juce::ValueTree::fromXml (*xml), true);
    currentPresetName = f.getFileNameWithoutExtension();
    return true;
}

void CantusAudioProcessor::playAllNotes()
{
    pushUndo();
    setParamReal ("density", 0.5f);
    setParamReal ("seqchaos", 0.0f);
    setParamReal ("callresp", 0.0f);
    setParamReal ("dialogue", 0.0f);
    auto& P = bank.pats[editPattern()];
    for (auto& st : P.steps) { st.prob = 100; st.cond = 0; }
    evoMask[editPattern()] = 0u;
}

juce::AudioProcessorEditor* CantusAudioProcessor::createEditor() { return new CantusAudioProcessorEditor (*this); }

#ifndef ALEA_NO_PLUGIN_FILTER
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CantusAudioProcessor(); }
#endif
