#include "ColorProcessor.h"
#include "ColorEditor.h"

using namespace color;
namespace th = color::theory;

const char* ColorAudioProcessor::typeParamId (int i)
{
    static const char* ids[kNumTypes] { "osc1type", "osc2type", "subtype", "ftype", "drivetype", "glidemode", "layer" };
    return ids[juce::jlimit (0, kNumTypes - 1, i)];
}

ColorAudioProcessor::ColorAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "COLOR", createColorLayout())
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
    for (int p = 0; p < kPatterns; ++p) { evoMask[p] = 0u; evoSub[p] = 0u; }
    for (auto& c : ccMap) c = -1;
    for (int i = 0; i < kChain; ++i) { chainPat[i] = 0; chainBars[i] = 1; chainShift[i] = 0; }
    for (auto& s : sounding) s = -1;
    for (auto& s : lastVoicing) s = -1;
    for (auto& m : voiceMidiOut) m = -1;

    auto g = [this] (const char* id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    playRaw = g ("play"); bpmRaw = g ("bpm"); patternRaw = g ("pattern"); seedRaw = g ("seed"); chaosRaw = g ("chaos");
    rndModeRaw = g ("rndmode"); rndDistRaw = g ("rnddist"); rootRaw = g ("root"); scaleRaw = g ("scale"); modeRaw = g ("mode");
    velSensRaw = g ("velsens"); vdriftRaw = g ("vdrift"); vspreadRaw = g ("vspread"); linkRndRaw = g ("link_rnd");
    styleRaw = g ("style"); cycleRaw = g ("cycle"); nchordsRaw = g ("nchords"); chordLenRaw = g ("chordlen"); complexRaw = g ("complex");
    tensionRaw = g ("tension"); chromaRaw = g ("chroma"); outgoingRaw = g ("outgoing"); liveSubRaw = g ("livesub"); approachRaw = g ("approach");
    voicingRaw = g ("voicing"); voicesRaw = g ("voices"); registerRaw = g ("register"); spreadRaw = g ("spread"); smoothRaw = g ("smooth");
    bassModeRaw = g ("bassmode"); bassLvlRaw = g ("basslvl"); playModeRaw = g ("playmode"); arpModeRaw = g ("arpmode"); arpOctRaw = g ("arpoct");
    strumRaw = g ("strum");
    swingRaw = g ("swing"); seqChaosRaw = g ("seqchaos"); ratChaosRaw = g ("ratchaos"); humanRaw = g ("humanize"); noteChaosRaw = g ("notechaos");
    densityRaw = g ("density"); fillRaw = g ("fill"); morphRaw = g ("morph"); modDepthRaw = g ("moddepth"); songRaw = g ("songmode");
    midiOutRaw = g ("midiout"); keysRaw = g ("keysmode"); turLenRaw = g ("m3len"); followTrkRaw = g ("m6trk");
    sceneARaw = g ("scenea"); sceneBRaw = g ("sceneb"); sceneXfRaw = g ("scenexf"); sceneOnRaw = g ("sceneon");
    evoAmtRaw = g ("evoamt"); evoBarsRaw = g ("evobars"); evoTargetRaw = g ("evotarget");
    chRateRaw = g ("chrate"); chDepthRaw = g ("chdepth"); chMixRaw = g ("chmix");
    dlyDivRaw = g ("dlydiv"); dlyFbRaw = g ("dlyfb"); dlyToneRaw = g ("dlytone"); dlyPingRaw = g ("dlyping"); dlyRetRaw = g ("dlyret"); dlyRndRaw = g ("dlyrnd");
    revSizeRaw = g ("revsize"); revDampRaw = g ("revdamp"); revWidthRaw = g ("revwidth"); revRetRaw = g ("revret"); revPreRaw = g ("revpre");
    duckAmtRaw = g ("duckamt"); duckSrcRaw = g ("ducksrc"); duckRelRaw = g ("duckrel"); glueRaw = g ("glue"); mDriveRaw = g ("mdrive"); masterRaw = g ("master");
    lkOnRaw = g ("lk_on"); lkPlayRaw = g ("lk_play"); lkTempoRaw = g ("lk_tempo"); lkSongRaw = g ("lk_song");
    layerLvlRaw = g ("layerlvl"); layerDecRaw = g ("layerdec");

    initDefaultBank();
    setChainText (chainText);
    mods.reset();
    setupLink();
}

ColorAudioProcessor::~ColorAudioProcessor()
{
    cancelPendingUpdate();
    linkSync.reset();
    link.reset();
}

void ColorAudioProcessor::setupLink()
{
    using namespace alea::link;
    link = std::make_unique<Link> (KindColor);
    linkSync = std::make_unique<LinkSync> (*link, apvts);
    auto bindParam = [this] (Field f, const char* id)
    {
        juce::String pid (id);
        linkSync->bind (f, [this, pid] { return (double) apvts.getRawParameterValue (pid)->load(); },
                           [this, pid] (double v) { setParamReal (pid, (float) v); });
    };
    bindParam (FTempo, "bpm"); bindParam (FPlay, "play");
    // tonalite partagee = tonalite qui sonne (tonique + modulation du pattern en cours + transposition de la chaine Song) :
    // quand Color module, les autres instruments modulent avec lui ; une tonique recue est ramenee dans ce repere
    linkSync->bind (FKey, [this] { return (double) sharedKey(); },
                          [this] (double v)
                          {
                              const int shift = sharedKey() - (int) rootRaw->load();
                              setParamReal ("root", (float) ((((int) std::round (v) - shift) % 12 + 12) % 12));
                          });
    // gamme partagee (grossiere) : on garde notre mode s'il est compatible, sinon le mode canonique
    linkSync->bind (FScale, [this] { return (double) scaleRaw->load(); },
                            [this] (double v)
                            {
                                if (v < 0.5) return;
                                setParamReal ("scale", (float) v);
                                const int m = th::modeForCoarse ((int) v, (int) modeRaw->load());
                                if (m != (int) modeRaw->load()) setParamReal ("mode", (float) m);
                            });
    bindParam (FPattern, "pattern"); bindParam (FFill, "fill"); bindParam (FChaos, "chaos"); bindParam (FSwing, "swing");
    bindParam (FEvolve, "evoamt"); bindParam (FDensity, "density"); bindParam (FMorph, "morph"); bindParam (FSeed, "seed");
    linkSync->bind (FScene, [this] { return (double) lastRecalledScene.load(); },
                            [this] (double v) { if (v >= 0) recallScene ((int) v); });
    linkSync->onTick = [this]
    {
        // le mode choisi ici impose la gamme grossiere partagee
        const int coarse = th::scale ((int) modeRaw->load()).coarse;
        if (coarse != (int) scaleRaw->load()) setParamReal ("scale", (float) coarse);
        if (auto* me = link->self())
        {
            me->root = (int) rootRaw->load();
            me->scale = (int) scaleRaw->load();
        }
    };
}

bool ColorAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.inputBuses.isEmpty();
}

void ColorAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (auto& v : voices) { v.prepare (sampleRate * 2.0); v.reset(); }
    for (int v = 0; v < kVoices; ++v) { voiceOffPpq[v] = -1.0; voiceMidiOut[v] = -1; voiceGain[v] = 0.0f; }
    decimL.reset(); decimR.reset();
    chorus.prepare (sampleRate);
    delay.prepare (sampleRate);
    plate.prepare (sampleRate);
    glue.prepare (sampleRate);
    revBus.setSize (2, juce::jmax (1, samplesPerBlock), false, true, true);
    internalRunning = wasRunning = false;
    numPending = 0; numHeld = 0; keyChordActive = false;
    curN = prevN = 0; curKey = 0;
    condState.reset();
    scopeDecim = juce::jmax (1, (int) (sampleRate * 0.35 / kScopeLen));
    resolveParams();
}

ColorContext ColorAudioProcessor::makeContext (int pattern, double patStart, double bpm) const
{
    ColorContext c;
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
    c.evoMask = &evoMask[juce::jlimit (0, kPatterns - 1, pattern)];
    return c;
}

// ---------------------------------------------------------------------------
// Progression : position et accord effectif (substitutions en direct, accords d'approche, evolution)
ColorAudioProcessor::ChordPos ColorAudioProcessor::chordPosAt (int pattern, double patStart, double ppq) const
{
    const auto& P = bank.pats[juce::jlimit (0, kPatterns - 1, pattern)];
    const int n = P.chordCount();
    int beats[kMaxChords]; int total = 0;
    for (int i = 0; i < n; ++i) { beats[i] = P.chord (i).beats; total += beats[i]; }
    total = juce::jmax (1, total);
    ChordPos r;
    const double rel = ppq - patStart;
    const int64_t loop = (int64_t) std::floor (rel / total);
    double within = rel - (double) loop * total;
    double acc = 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (within < acc + beats[i] - 1.0e-9 || i == n - 1)
        {
            r.index = i;
            r.start = patStart + (double) loop * total + acc;
            r.end = r.start + beats[i];
            r.occurrence = loop * n + i;
            return r;
        }
        acc += beats[i];
    }
    return r;
}

th::Chord ColorAudioProcessor::effectiveChord (int pattern, double patStart, double ppq, bool* sub, bool* appr) const
{
    if (sub != nullptr) *sub = false;
    if (appr != nullptr) *appr = false;
    pattern = juce::jlimit (0, kPatterns - 1, pattern);
    const auto& P = bank.pats[pattern];
    const int n = P.chordCount();
    const int sc = (int) modeRaw->load();
    const uint32_t seed = (uint32_t) seedRaw->load();
    const float tension = gmod (tensionRaw->load(), GTension);
    const float live = gmod (liveSubRaw->load(), GLiveSub);
    const uint32_t evoS = evoSub[pattern].load (std::memory_order_relaxed);

    auto baseAt = [&] (const ChordPos& pos, bool* s)
    {
        th::Chord c = P.chord (pos.index);
        const th::Chord next = P.chord ((pos.index + 1) % n);
        const uint32_t h = hash3 ((uint32_t) pos.occurrence, seed, 0xC401u);
        if (((evoS >> pos.index) & 1u) != 0)
        {
            c = th::liveSubstitute (c, next, sc, hash3 ((uint32_t) pos.index, seed ^ 0xE5u, (uint32_t) pattern), juce::jmax (0.5f, tension));
            if (s != nullptr) *s = true;
        }
        else if (live > 0.0f && hUni (h, 7u, 0x5Bu) < live * 0.7f)
        {
            c = th::liveSubstitute (c, next, sc, h, tension);
            if (s != nullptr) *s = true;
        }
        return c;
    };

    const auto pos = chordPosAt (pattern, patStart, ppq);
    th::Chord c = baseAt (pos, sub);
    const float ap = approachRaw->load();
    if (ap > 0.0f && c.beats >= 2 && ppq >= pos.end - 1.0 - 1.0e-9)
    {
        const uint32_t h = hash3 ((uint32_t) pos.occurrence, seed, 0xA990u);
        if (hUni (h, 1u, 0x77u) < ap)
        {
            const auto npos = chordPosAt (pattern, patStart, pos.end + 1.0e-6);
            const th::Chord target = baseAt (npos, nullptr);
            th::Chord a = th::approachChord (target, h);
            // eviter un "accord d'approche" identique a l'accord courant
            if (! (a.root == c.root && a.qual == c.qual))
            {
                a.beats = c.beats;
                c = a;
                if (appr != nullptr) *appr = true;
            }
        }
    }
    return c;
}

int ColorAudioProcessor::sharedKey() const
{
    const int pat = juce::jlimit (0, kPatterns - 1, currentPattern.load());
    return (((int) rootRaw->load() + bank.pats[pat].keyShift.load() + songShift.load()) % 12 + 12) % 12;
}

int ColorAudioProcessor::keyRootEff() const
{
    const int pat = juce::jlimit (0, kPatterns - 1, currentPattern.load());
    return (((int) rootRaw->load() + (int) transpose + bank.pats[pat].keyShift.load() + songShift.load()) % 12 + 12) % 12;
}

th::VoiceOptions ColorAudioProcessor::voiceOptions (uint32_t hash) const
{
    th::VoiceOptions o;
    o.voicing = (int) voicingRaw->load();
    o.voices = (int) voicesRaw->load();
    o.center = tmap::registerMidi (gmod (registerRaw->load(), GRegister));
    o.spread = gmod (spreadRaw->load(), GSpread);
    o.smooth = smoothRaw->load();
    o.complexity = gmod (complexRaw->load(), GComplex);
    o.tension = gmod (tensionRaw->load(), GTension);
    o.bassMode = (int) bassModeRaw->load();
    o.bassOctave = 2;
    o.scale = (int) modeRaw->load();
    o.keyRoot = keyRootEff();
    o.hash = hash;
    return o;
}

int ColorAudioProcessor::previewVoicing (const th::Chord& c, int* notes, int maxN) const
{
    auto o = voiceOptions (1u);
    o.keyRoot = keyRoot();
    return th::voiceChord (c, o, nullptr, 0, notes, maxN);
}

int ColorAudioProcessor::computeVoicing (const th::Chord& c, uint32_t hash, int* notes, int maxN)
{
    auto o = voiceOptions (hash);
    return th::voiceChord (c, o, prevN > 0 ? prevNotes : nullptr, prevN, notes, maxN);
}

// ---------------------------------------------------------------------------
void ColorAudioProcessor::computeMods()
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

void ColorAudioProcessor::resolveParams()
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
    bp.osc1 = types[0]; bp.osc2 = types[1]; bp.sub = types[2]; bp.ftype = types[3]; bp.dtype = types[4]; bp.chord = 0;
    bp.layer = types[6]; bp.layerLvl = layerLvlRaw->load(); bp.layerDecay = 0.05f * std::pow (80.0f, layerDecRaw->load());
    bp.shape = n[Shape]; bp.o2lvl = n[O2Level]; bp.o2semi = tmap::semis (n[O2Semi]); bp.o2fine = tmap::cents (n[O2Fine]);
    bp.fm = n[FM]; bp.subLvl = n[SubLevel]; bp.noise = n[NoiseLvl] * 0.5f;
    bp.cutoffHz = tmap::cutoffHz (n[Cutoff]); bp.reso = n[Reso]; bp.envOct = tmap::envOct (n[EnvMod]); bp.fdecay = tmap::fdecaySec (n[FDecay]);
    bp.accentAmt = n[VelFilt]; bp.keytrack = n[KeyTrack]; bp.fdrive = n[FDrive];
    bp.attack = tmap::attackSec (n[Attack]); bp.decay = tmap::decaySec (n[Decay]); bp.sustain = n[Sustain]; bp.release = tmap::releaseSec (n[Release]);
    bp.glideSec = tmap::glideSec (n[Glide]); bp.drive = n[Drive];
    level = tmap::levelGain (n[Level]) * 0.42f;
    width = n[Width];
    sDly = n[SendDly]; sRev = n[SendRev];
}

void ColorAudioProcessor::applyStepLocks (const ColorHit& e)
{
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
    const CStep* step = (e.pat >= 0 && e.step >= 0) ? &bank.pats[e.pat].steps[e.step] : nullptr;
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
}

// ---------------------------------------------------------------------------
// Moteur de notes
int ColorAudioProcessor::allocVoice (int note)
{
    for (int v = 0; v < kVoices; ++v) if (voices[v].note == note && voices[v].isActive()) return v;   // meme note : on la reprend
    for (int v = 0; v < kVoices; ++v) if (! voices[v].isActive()) return v;
    int best = -1; uint32_t oldest = 0xffffffffu;
    for (int v = 0; v < kVoices; ++v) if (! voices[v].gateOn() && voices[v].age < oldest) { oldest = voices[v].age; best = v; }
    if (best >= 0) return best;
    for (int v = 0; v < kVoices; ++v) if (! voiceIsBass[v] && voices[v].age < oldest) { oldest = voices[v].age; best = v; }
    return best >= 0 ? best : 0;
}

void ColorAudioProcessor::releaseVoice (int v, int sampleInBlock)
{
    voices[v].noteOff();
    voiceOffPpq[v] = -1.0;
    if (voiceMidiOut[v] >= 0 && midiOutBuf != nullptr) midiOutBuf->addEvent (juce::MidiMessage::noteOff (1, voiceMidiOut[v]), sampleInBlock);
    voiceMidiOut[v] = -1;
}

void ColorAudioProcessor::releaseAll (int sampleInBlock, bool hard)
{
    for (int v = 0; v < kVoices; ++v)
    {
        if (voices[v].gateOn() || voiceMidiOut[v] >= 0) releaseVoice (v, sampleInBlock);
        if (hard) voices[v].reset();
    }
    numPending = 0;
}

void ColorAudioProcessor::startNote (const NoteOn& n, double ppqNow, int s)
{
    if (n.delay > 0)
    {
        if (numPending < (int) pending.size()) pending[(size_t) numPending++] = { n, s + n.delay };
        return;
    }
    const int note = juce::jlimit (12, 115, n.note);
    const int v = n.voiceHint >= 0 ? n.voiceHint : allocVoice (note);
    auto& vc = voices[v];
    const bool legato = n.legato && vc.gateOn();
    if (voiceMidiOut[v] >= 0 && midiOutBuf != nullptr) midiOutBuf->addEvent (juce::MidiMessage::noteOff (1, voiceMidiOut[v]), s);
    voiceMidiOut[v] = -1;
    const int gm = (int) typeRaw[5]->load();
    bp.glideMode = legato ? 0 : (gm == 2 ? 1 : 2);
    vc.driftSemi = hBi ((uint32_t) note * 31u + (uint32_t) v, (uint32_t) seedRaw->load(), 0xD71u) * vdriftRaw->load() * 0.16f;
    {
        // Voice Spread : chaque voix a son propre caractere (filtre un peu plus ouvert / ferme, niveau)
        const float sp = vspreadRaw->load();
        const uint32_t vk = hash3 ((uint32_t) v, (uint32_t) seedRaw->load(), 0x5B2Du);
        vc.cutOffsetOct = hBi (vk, 1u, 0x51u) * sp * 0.9f;
    }
    vc.note = note;
    vc.age = ++ageCounter;
    const float a = (n.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    vc.panL = std::cos (a); vc.panR = std::sin (a);
    const float vsens = velSensRaw->load();
    const float vel = juce::jlimit (0.05f, 1.0f, n.vel);
    voiceGain[v] = n.gain * ((1.0f - vsens) + vsens * std::pow (vel, 1.3f))
                   * (1.0f + hBi (hash3 ((uint32_t) v, (uint32_t) seedRaw->load(), 0x5B2Du), 2u, 0x52u) * vspreadRaw->load() * 0.25f);
    vc.noteOn ((float) note, vel, n.accent, legato, bp);
    voiceOffPpq[v] = n.durPpq >= 1.0e8 ? 1.0e18 : ppqNow + juce::jmax (0.01, n.durPpq);
    flash = 1.0f;
    if (scopeWrite >= kScopeLen * scopeDecim && ! legato) scopeWrite = 0;
    if (midiOutOn && midiOutBuf != nullptr)
    {
        midiOutBuf->addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) juce::jlimit (1, 127, n.accent ? 127 : (int) (vel * 110.0f))), s);
        voiceMidiOut[v] = note;
    }
}

namespace
{
    // gain et position stereo d'une note du voicing (la basse au centre, les voix etalees)
    inline void voiceLayout (int i, int n, bool hasBass, float bassLvl, float width, float& gain, float& pan)
    {
        const int nUp = juce::jmax (1, n - (hasBass ? 1 : 0));
        const float upGain = 1.25f / std::sqrt ((float) nUp);
        if (hasBass && i == 0) { gain = bassLvl * 1.3f; pan = 0.0f; return; }
        const int k = i - (hasBass ? 1 : 0);
        gain = upGain;
        pan = nUp > 1 ? ((float) k / (float) (nUp - 1) * 2.0f - 1.0f) * width * 0.85f : 0.0f;
        if ((k & 1) && nUp > 2) pan = -pan * 0.6f + pan * 0.4f;     // alternance : voix voisines sur des cotes opposes
    }
}

void ColorAudioProcessor::chordChanged (const th::Chord& c, uint32_t hash, double ppqNow, double untilPpq, bool padMode, int s)
{
    int notes[8];
    const int n = computeVoicing (c, hash, notes, 8);
    const bool hasBass = (int) bassModeRaw->load() > 0 && n > 0;
    // memorise pour la conduite des voix (voix superieures)
    prevN = 0;
    for (int i = hasBass ? 1 : 0; i < n; ++i) prevNotes[prevN++] = notes[i];
    for (int i = 0; i < n; ++i) curNotes[i] = notes[i];
    curN = n;
    curKey = packChord (c); curHash = hash;
    arpCounter = 0;
    for (int i = 0; i < 8; ++i) lastVoicing[i] = i < n ? notes[i] : -1;
    lastVoicingN = n;

    const int pm = (int) playModeRaw->load();
    if (! padMode && pm != 3) return;
    const bool arpBassOnly = ! padMode && pm == 3;
    if (arpBassOnly && ! hasBass) return;

    const float chaos = noteChaosRaw->load();
    const float bassLvl = bassLvlRaw->load();
    const bool glideVL = (int) typeRaw[5]->load() == 1;
    const double dur = untilPpq >= 1.0e17 ? 1.0e9 : juce::jmax (0.05, untilPpq - ppqNow);
    bool used[kVoices] {};
    int play[8]; int np = 0;
    for (int i = 0; i < n; ++i)
    {
        if (arpBassOnly && i > 0) break;
        int note = notes[i];
        if (chaos > 0.0f && ! (hasBass && i == 0))
        {
            if (hUni (hash, (uint32_t) i, 0xCA0u) < chaos * 0.3f) note += hUni (hash, (uint32_t) i, 0xCA1u) < 0.5f ? -12 : 12;
            if (n > 3 && hUni (hash, (uint32_t) i, 0xCA2u) < chaos * 0.12f) continue;
        }
        play[np++] = note;
    }

    // voix tenues actuellement (en mode arpege : seulement la basse tenue)
    int held[kVoices]; int nh = 0;
    for (int v = 0; v < kVoices; ++v)
        if (voices[v].gateOn() && (! arpBassOnly || voiceIsBass[v])) held[nh++] = v;

    if (glideVL && nh > 0)
    {
        // conduite des voix "glissee" : les voix tenues (triees) glissent vers les nouvelles notes (triees)
        std::sort (held, held + nh, [this] (int a, int b) { return voices[a].note < voices[b].note; });
        const int m = juce::jmin (nh, np);
        for (int k = 0; k < np; ++k)
        {
            float gain, pan; voiceLayout (k, np, hasBass, bassLvl, width, gain, pan);
            const int hint = k < m ? held[k] : allocVoice (play[k]);
            voiceIsBass[hint] = hasBass && k == 0;
            startNote ({ play[k], 0.75f, false, dur, 0, gain, pan, hint, k < m }, ppqNow, s);
        }
        for (int i = m; i < nh; ++i) releaseVoice (held[i], s);
        return;
    }

    // notes communes conservees (sans redeclenchement), les autres relachees / attaquees
    bool keep[8] {};
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < nh; ++i)
            if (! used[held[i]] && voices[held[i]].note == play[k])
            {
                used[held[i]] = true; keep[k] = true;
                voiceIsBass[held[i]] = hasBass && k == 0;
                voiceOffPpq[held[i]] = dur >= 1.0e8 ? 1.0e18 : ppqNow + dur;
                break;
            }
    for (int i = 0; i < nh; ++i) if (! used[held[i]]) releaseVoice (held[i], s);
    for (int k = 0; k < np; ++k)
    {
        if (keep[k]) continue;
        float gain, pan; voiceLayout (k, np, hasBass, bassLvl, width, gain, pan);
        const int v = allocVoice (play[k]);
        voiceIsBass[v] = hasBass && k == 0;
        startNote ({ play[k], 0.75f, false, dur, 0, gain, pan, v, false }, ppqNow, s);
    }
}

void ColorAudioProcessor::hitToNotes (const ColorHit& h, double ppqNow, int s)
{
    const int pm = (int) playModeRaw->load();
    if (pm == 0 || curN <= 0) return;   // Pad : le rythme est ignore
    applyStepLocks (h);
    const bool hasBass = (int) bassModeRaw->load() > 0;
    const float chaos = noteChaosRaw->load();
    const float bassLvl = bassLvlRaw->load();

    if (pm == 3)
    {
        // arpege : notes du voicing (ou gamme de l'accord en mode Lead) sur plusieurs octaves
        int arp[48]; int na = 0;
        const int octs = juce::jlimit (1, 3, (int) arpOctRaw->load());
        const int amode = (int) arpModeRaw->load();
        if (amode == 4)
        {
            auto o = voiceOptions (curHash);
            const int lo = (int) o.center - 5, hi = (int) o.center + 12 * octs;
            na = th::chordScaleNotes (unpackChord (curKey), o, outgoingRaw->load(), lo, hi, arp, 48);
        }
        else
        {
            for (int oc = 0; oc < octs; ++oc)
                for (int i = hasBass ? 1 : 0; i < curN && na < 48; ++i) arp[na++] = curNotes[i] + 12 * oc;
            std::sort (arp, arp + na);
        }
        if (na <= 0) return;
        int idx = 0;
        const int c = arpCounter++;
        switch (amode)
        {
            case 1: idx = na - 1 - c % na; break;
            case 2: { const int period = juce::jmax (1, 2 * na - 2); const int p = c % period; idx = na == 1 ? 0 : (p < na ? p : period - p); break; }
            case 3: idx = (int) (hash3 (h.hitKey, (uint32_t) c, 0xA3Bu) % (uint32_t) na); break;
            case 4:
            {
                // ligne melodique : marche aleatoire dans la gamme parente, attiree vers les notes de l'accord
                static const int steps[] { -2, -1, -1, 1, 1, 2, 0, 3, -3 };
                int j = (c == 0 ? na / 3 : arpLast) + steps[hash3 (h.hitKey, (uint32_t) c, 0x1EADu) % 9u];
                if (j < 0) j = -j;
                if (j >= na) j = 2 * (na - 1) - j;
                idx = arpLast = juce::jlimit (0, na - 1, j);
                break;
            }
            default: idx = c % na; break;
        }
        int note = arp[idx] + 12 * h.oct;
        if (chaos > 0.0f && hUni (h.hitKey, 0xC1u, 0xCA0u) < chaos * 0.3f) note += 12;
        const float pan = (hBi (h.hitKey, 0x9Au, 0x9Bu)) * width * 0.6f;
        startNote ({ note, h.vel, h.accent, h.gatePpq, 0, 0.95f, pan, -1, false }, ppqNow, s);
        return;
    }

    // Comp / Strum : tout le voicing (ou une partie) a chaque coup
    int notes[8]; bool isB[8]; int n = 0;
    for (int i = 0; i < curN; ++i)
    {
        const bool isBass = hasBass && i == 0;
        if (h.part == 1 && i != curN - 1) continue;              // note du haut
        if (h.part == 2 && i != 0) continue;                     // basse (ou note la plus grave)
        if (h.part == 3 && isBass) continue;
        int note = curNotes[i] + (isBass ? 0 : 12 * h.oct);
        if (chaos > 0.0f && ! isBass)
        {
            if (hUni (h.hitKey, (uint32_t) i, 0xCA0u) < chaos * 0.3f) note += hUni (h.hitKey, (uint32_t) i, 0xCA1u) < 0.5f ? -12 : 12;
            if (curN > 3 && hUni (h.hitKey, (uint32_t) i, 0xCA2u) < chaos * 0.15f) continue;
        }
        isB[n] = isBass;
        notes[n++] = note;
    }
    if (n == 0) return;
    int strumDir = h.strum;                                       // 1 montant, 2 descendant
    if (pm == 2 && strumDir == 0) strumDir = ((h.k / 4) & 1) ? 1 : 2;
    const float sv = strumRaw->load();
    const double totalSec = (pm == 2 || h.strum != 0) ? (double) (sv * sv) * 0.12 : 0.0;
    const int spacing = n > 1 ? (int) (totalSec * sr / (n - 1)) : 0;
    const bool anyBass = isB[0];
    for (int i = 0; i < n; ++i)
    {
        float gain, pan;
        voiceLayout (i, n, anyBass, bassLvl, width, gain, pan);
        const int order = strumDir == 2 ? (n - 1 - i) : i;
        const int delay = strumDir != 0 ? order * spacing : 0;
        const float vel = h.vel * (strumDir != 0 ? 1.0f - 0.04f * (float) order : 1.0f);
        startNote ({ notes[i], vel, h.accent, h.gatePpq, delay, gain, pan, -1, false }, ppqNow, s);
    }
}

void ColorAudioProcessor::keyChordFromHeld()
{
    if (numHeld <= 0) { keyChordActive = false; keyChordPacked = 0; return; }
    const int root = keyRootEff();
    const int sc = (int) modeRaw->load();
    const int km = (int) keysRaw->load();
    const int last = heldKeys[numHeld - 1];
    th::Chord c;
    if (km == 2)
    {
        // selection d'accord : touches blanches a partir de C3 = accords 1, 2, 3... de la progression
        static const int white[12] { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };
        const int pc = last % 12;
        const int w = white[pc] < 0 ? white[(pc + 11) % 12] : white[pc];
        const int idx = ((last - 48) / 12) * 7 + w;
        const auto& P = bank.pats[currentPattern.load()];
        c = P.chord (juce::jmax (0, idx) % P.chordCount());
    }
    else if (km == 3)
    {
        const th::Chord cur = curKey != 0 ? unpackChord (curKey) : bank.pats[currentPattern.load()].chord (0);
        c = th::harmonizeMelody ((last - root + 120) % 12, cur, sc, rng.next());
    }
    else
    {
        // jeu direct : une touche = accord diatonique du degre ; plusieurs touches = accord reconnu
        if (numHeld == 1)
        {
            const int rel = (last - root + 120) % 12;
            const int q = th::nearestInScale (sc, rel);
            int deg = 0;
            for (int d = 0; d < th::scaleSize (sc); ++d) if ((th::scaleDegreePc (sc, d) % 12 + 12) % 12 == (q % 12 + 12) % 12) { deg = d; break; }
            const int size = 3 + (int) std::round (gmod (complexRaw->load(), GComplex) * 2.0f);
            c = th::diatonic (sc, deg, size);
        }
        else
        {
            uint16_t mask = 0; int low = 128;
            for (int i = 0; i < numHeld; ++i) { mask |= (uint16_t) (1u << ((heldKeys[i] - root + 120) % 12)); low = juce::jmin (low, heldKeys[i]); }
            if (! th::identifyChord (mask, (low - root + 120) % 12, c)) return;
        }
    }
    c.beats = 4;
    keyChord = c;
    keyChordActive = true;
    keyChordPacked = packChord (c);
}

void ColorAudioProcessor::evolveStep (int64_t bar)
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
            if (hUni (b, (uint32_t) i, seed) < amt * 0.06f) evoMask[pat].fetch_xor (1u << i);
        const int n = bank.pats[pat].chordCount();
        for (int i = 0; i < n; ++i)
            if (hUni (b, (uint32_t) i + 500u, seed) < amt * 0.08f) evoSub[pat].fetch_xor (1u << i);
    }
    if (target == 1 || target == 2)
        for (int i = 0; i < kNumTP; ++i)
        {
            if (i == Level) continue;
            const float v = evoOff[i].load() + hBi (b, (uint32_t) i + 300u, seed) * amt * 0.05f;
            evoOff[i].store (juce::jlimit (-0.3f, 0.3f, v));
        }
}

void ColorAudioProcessor::handleAsyncUpdate()
{
    const int n = pendingScene.exchange (-1);
    if (n >= 0) recallScene (n);
}

void ColorAudioProcessor::publishChords (double ppqNow, double patStart, bool running)
{
    if (link == nullptr || lkOnRaw->load() <= 0.5f) return;
    const int keyRootEff = this->keyRootEff();
    const int sc = (int) modeRaw->load();
    const float outg = outgoingRaw->load();
    auto rot = [keyRootEff] (uint32_t rel) { uint32_t a = 0; for (int i = 0; i < 12; ++i) if (rel & (1u << i)) a |= 1u << ((i + keyRootEff) % 12); return (int) a; };
    auto toEvent = [keyRootEff, sc, outg, rot] (const th::Chord& c, double ppq)
    {
        alea::link::ChordEvent e;
        e.ppq = ppq;
        e.root = (keyRootEff + c.root) % 12;
        e.bass = (keyRootEff + th::chordBassPc (c) + 12) % 12;
        const uint32_t rel = th::chordMask (c);
        uint32_t abs = 0;
        for (int i = 0; i < 12; ++i) if (rel & (1u << i)) abs |= 1u << ((i + keyRootEff) % 12);
        e.mask = (int) abs;
        e.scale = rot (th::chordScaleMask (c, sc, outg));
        return e;
    };
    {
        // grille complete du pattern courant (generateurs de Talea / Cantus)
        const auto& P = bank.pats[juce::jlimit (0, kPatterns - 1, currentPattern.load())];
        alea::link::ProgChord pc[alea::link::kMaxProg];
        const int n = juce::jmin (P.chordCount(), alea::link::kMaxProg);
        for (int i = 0; i < n; ++i)
        {
            const auto c = P.chord (i);
            const auto e = toEvent (c, 0.0);
            pc[i] = { e.root, e.bass, e.mask, e.scale, (int) c.beats };
        }
        link->publishProgression (pc, n, patStart);
    }
    alea::link::ChordEvent ev[alea::link::kMaxChordEvents];
    int n = 0;
    if (! running || keyChordActive)
    {
        if (curKey == 0 && ! keyChordActive) return;
        ev[n++] = toEvent (keyChordActive ? keyChord : unpackChord (curKey), -1.0e9);
        link->publishChords (ev, n);
        return;
    }
    const int pat = currentPattern.load();
    auto pos = chordPosAt (pat, patStart, ppqNow);
    double t = pos.start;
    int lastRoot = -1, lastMask = -1;
    for (int guard = 0; guard < 24 && n < alea::link::kMaxChordEvents; ++guard)
    {
        const auto p = chordPosAt (pat, patStart, t + 1.0e-6);
        const double pts[2] { p.start, p.end - 1.0 };
        for (int k = 0; k < 2 && n < alea::link::kMaxChordEvents; ++k)
        {
            if (k == 1 && (pts[1] <= p.start + 1.0e-6)) continue;
            const auto e = toEvent (effectiveChord (pat, patStart, pts[k] + 1.0e-6), pts[k]);
            if (e.root == lastRoot && e.mask == lastMask) continue;
            ev[n++] = e; lastRoot = e.root; lastMask = e.mask;
        }
        t = p.end;
    }
    link->publishChords (ev, n);
}

// ---------------------------------------------------------------------------
void ColorAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    const double tl0 = running ? ppqStart : freeModClock;
    const bool justStarted = running && ! wasRunning;
    midiOutBuf = &midi;
    midiOutOn = midiOutRaw->load() > 0.5f;

    // --- MIDI entrant (avant d'effacer le tampon : les sorties sont ajoutees ensuite)
    juce::MidiBuffer incoming;
    incoming.swapWith (midi);

    if (justStarted)
    {
        patStartPpq = std::floor (ppqStart / 4.0) * 4.0;
        songStartBar = (int64_t) std::floor (ppqStart / 4.0);
        condState.reset();
        lastDuckPpq = lastFollowPpq = -1.0;
        curKey = 0;
    }
    if (! running && wasRunning && ! keyChordActive) releaseAll (0);
    wasRunning = running;

    const int paramPat = (int) patternRaw->load();
    if (paramPat != lastParamPattern) { lastParamPattern = paramPat; requestedPattern = paramPat; }
    numHits = 0; numBeats = 0; numBounds = 0;

    // C0-G0 scenes, C1-G1 patterns, >= C2 clavier (transposition, selection, harmonisation, jeu direct), CC learn
    auto& params = getParameters();
    const int keysMode = (int) keysRaw->load();
    struct KeyEv { int offset; bool changed; };
    KeyEv keyEvents[32]; int numKeyEv = 0;
    for (const auto meta : incoming)
    {
        const auto m = meta.getMessage();
        const int off = juce::jlimit (0, numSamples - 1, meta.samplePosition);
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            if (note >= 24 && note < 24 + kScenes) { pendingScene = note - 24; triggerAsyncUpdate(); }
            else if (note >= 36 && note < 36 + kPatterns) requestedPattern = note - 36;
            else if (note >= 48 && keysMode > 0)
            {
                if (keysMode == 1) transpose = (float) (note - 60);
                else
                {
                    if (numHeld < 16) heldKeys[numHeld++] = note;
                    if (numKeyEv < 32) keyEvents[numKeyEv++] = { off, true };
                }
            }
        }
        else if (m.isNoteOff() && keysMode >= 2 && m.getNoteNumber() >= 48)
        {
            const int note = m.getNoteNumber();
            int k = 0;
            for (int i = 0; i < numHeld; ++i) if (heldKeys[i] != note) heldKeys[k++] = heldKeys[i];
            numHeld = k;
            if (numKeyEv < 32) keyEvents[numKeyEv++] = { off, true };
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { numHeld = 0; if (numKeyEv < 32) keyEvents[numKeyEv++] = { off, true }; }
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
    if (keysMode < 2 && numHeld > 0) { numHeld = 0; keyChordActive = false; keyChordPacked = 0; }

    // --- song mode, changement de pattern
    const bool songOn = songRaw->load() > 0.5f && chainLen.load() > 0;
    if (running && songOn)
    {
        int total = 0; const int len = chainLen.load();
        for (int i = 0; i < len; ++i) total += chainBars[i].load();
        if (total > 0)
        {
            int64_t bar = (int64_t) std::floor (ppqEnd / 4.0) - songStartBar;
            bar = ((bar % total) + total) % total;
            int acc = 0;
            for (int i = 0; i < len; ++i) { acc += chainBars[i].load(); if (bar < acc) { requestedPattern = chainPat[i].load(); songPos = i; songShift = chainShift[i].load(); break; } }
        }
    }
    else
    {
        songPos = -1; songShift = 0;
        // forme partagee : suit la chaine publiee par une autre instance (LINK > Song)
        int sp = 0;
        if (running && link != nullptr && lkOnRaw->load() > 0.5f && lkSongRaw->load() > 0.5f
            && link->songPatternAt ((int64_t) std::floor (ppqEnd / 4.0), sp))
            requestedPattern = juce::jlimit (0, kPatterns - 1, sp);
    }
    if (link != nullptr)
    {
        if (songOn && running)
        {
            int pats[kChain], bars[kChain]; const int len = chainLen.load();
            for (int i = 0; i < len; ++i) { pats[i] = chainPat[i].load(); bars[i] = chainBars[i].load(); }
            link->publishSong (true, songStartBar, pats, bars, len);
            songPublished = true;
        }
        else if (songPublished) { link->publishSong (false, 0, nullptr, nullptr, 0); songPublished = false; }
    }
    const bool barCrossed = std::floor (ppqEnd / 4.0) > std::floor (ppqStart / 4.0);
    if (requestedPattern != currentPattern.load() && (! running || barCrossed || justStarted))
    {
        currentPattern = requestedPattern;
        patStartPpq = running ? std::floor ((justStarted ? ppqStart : ppqEnd) / 4.0) * 4.0 : 0.0;
        curKey = 0;
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
            alea::link::Hit lh[alea::link::kMaxHits];
            const int nh = link->readHits (slot, lh, alea::link::kMaxHits);
            for (int i = 0; i < nh; ++i)
            {
                const auto& h = lh[i];
                if (! (h.ppq < ppqEnd && h.ppq >= ppqStart - 0.25)) continue;
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

    const int pm = (int) playModeRaw->load();
    const int pat = currentPattern.load();
    if (running)
    {
        auto ctx = makeContext (pat, patStartPpq, bpm);
        ctx.density = gmod (ctx.density, GDensity);
        ctx.seqChaos = gmod (ctx.seqChaos, GSeqChaos);
        ctx.ratChaos = gmod (ctx.ratChaos, GRatChaos);
        ctx.swing = gmod (ctx.swing, GSwing);
        runColorRhythm (bank, ctx, &condState, ppqStart, ppqEnd, [&] (double te, ColorHit e)
        {
            e.offset = juce::jlimit (0, numSamples - 1, (int) ((te - ppqStart) / ppqPerSample));
            if (numHits < (int) hits.size()) hits[(size_t) numHits++] = e;
        });
        std::sort (hits.begin(), hits.begin() + numHits, [] (const ColorHit& a, const ColorHit& b) { return a.offset < b.offset; });
        const auto& P = bank.pats[pat];
        playStep = P.stepIndex ((int64_t) std::floor (ppqEnd / ratePpq (P.rate.load())), ctx.seed * 16u + 13u);

        // frontieres d'accords (debut d'accord, accord d'approche) dans le bloc
        if (justStarted || curKey == 0) bounds[(size_t) numBounds++] = { ppqStart, 0 };
        double t = chordPosAt (pat, patStartPpq, ppqStart).start;
        for (int guard = 0; guard < 16 && numBounds < (int) bounds.size(); ++guard)
        {
            const auto p = chordPosAt (pat, patStartPpq, t + 1.0e-6);
            for (double b : { p.start, p.end - 1.0 })
                if (b >= ppqStart - 1.0e-7 && b < ppqEnd && b > p.start - 1.0e-9 && numBounds < (int) bounds.size())
                    bounds[(size_t) numBounds++] = { b, juce::jlimit (0, numSamples - 1, (int) ((b - ppqStart) / ppqPerSample)) };
            if (p.end >= ppqEnd) break;
            t = p.end;
        }
        std::sort (bounds.begin(), bounds.begin() + numBounds, [] (const Boundary& a, const Boundary& b) { return a.offset < b.offset; });

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

    // --- parametres du bloc
    resolveParams();
    for (auto& v : noteLock) v = -1.0f;
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
    const bool padMode = pm == 0;
    int sigNow = (int) voicingRaw->load() * 1000003 + (int) voicesRaw->load() * 10007 + (int) (registerRaw->load() * 60.0f) * 101
                 + (int) (spreadRaw->load() * 10.0f) * 13 + (int) bassModeRaw->load() * 7 + (int) modeRaw->load() * 3001
                 + keyRootEff() * 70001 + (int) (complexRaw->load() * 8.0f) * 31 + (int) (tensionRaw->load() * 8.0f) * 37
                 + (int) transpose * 1301 + pm * 17;
    const int audition = auditionChord.exchange (-1);

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (1);
    auto* rvL = revBus.getWritePointer (0);
    auto* rvR = revBus.getWritePointer (1);
    int hi = 0, bi = 0, di = 0, fi = 0, ci = 0, ki = 0;

    for (int s = 0; s < numSamples; ++s)
    {
        const double now = tl0 + s * ppqPerSample;
        tlNow = now;
        while (di < numDuck && duckOffsets[(size_t) di] <= s) { duckEnv = 1.0f; ++di; }
        while (fi < numFollow && followOffsets[(size_t) fi] <= s) { followEnv = 1.0f; ++fi; }

        // fin des notes
        for (int v = 0; v < kVoices; ++v)
            if (voiceOffPpq[v] >= 0.0 && now >= voiceOffPpq[v]) releaseVoice (v, s);

        // clavier
        while (ki < numKeyEv && keyEvents[ki].offset <= s)
        {
            ++ki;
            const bool wasActive = keyChordActive;
            keyChordFromHeld();
            if (keyChordActive)
            {
                if (! running || padMode)
                {
                    const double until = running ? chordPosAt (pat, patStartPpq, now).end : 1.0e18;
                    chordChanged (keyChord, rng.next(), now, until, true, s);
                }
                else curKey = 0;     // le prochain coup prendra l'accord du clavier
            }
            else if (wasActive)
            {
                if (! running) releaseAll (s);
                curKey = 0;          // reprise de la progression
            }
        }

        if (s == 0 && audition >= 0 && ! keyChordActive)
        {
            const auto& P = bank.pats[pat];
            th::Chord c = P.chord (juce::jlimit (0, P.chordCount() - 1, audition));
            if (! running) { releaseAll (0); prevN = 0; }
            chordChanged (c, rng.next(), now, now + 2.0, true, 0);
            for (int v = 0; v < kVoices; ++v) if (voices[v].gateOn()) voiceOffPpq[v] = now + 2.0;
            curChordIndex = audition;
            curChordPacked = packChord (c);
        }

        if (running)
        {
            // changement d'accord : nouvelle frontiere, ou reglage de voicing modifie (applique sur le temps)
            bool beatHere = false;
            while (bi < numBeats && beatOffsets[(size_t) bi] <= s)
            {
                const uint32_t b = (uint32_t) beatIndex[(size_t) bi];
                dlyDivCur = (dlyRnd > 0.0f && hUni (b, 0xD1u, seed) < dlyRnd) ? (int) (hUni (b, 0xD2u, seed) * 9.99f) : paramDiv;
                delay.setTimeSamples ((float) (delayDivPpq (dlyDivCur) * samplesPerBeat));
                beatHere = true;
                ++bi;
            }
            bool boundary = false;
            double evalPpq = now;      // position exacte (le debut de bloc peut tomber un epsilon avant la frontiere)
            while (ci < numBounds && bounds[(size_t) ci].offset <= s) { boundary = true; evalPpq = juce::jmax (evalPpq, bounds[(size_t) ci].ppq); ++ci; }
            const bool firstHit = hi < numHits && hits[(size_t) hi].offset <= s;
            if (firstHit) evalPpq = juce::jmax (evalPpq, hits[(size_t) hi].ppq);
            if (! keyChordActive && (boundary || firstHit || (beatHere && (sigNow != curSig || curKey == 0))))
            {
                bool isSub = false, isAppr = false;
                const th::Chord c = effectiveChord (pat, patStartPpq, evalPpq + 1.0e-6, &isSub, &isAppr);
                const int32_t key = packChord (c);
                const auto pos = chordPosAt (pat, patStartPpq, evalPpq + 1.0e-6);
                if (key != curKey || sigNow != curSig)
                {
                    const uint32_t h = hash3 ((uint32_t) pos.occurrence, seed, 0x701Cu);
                    chordChanged (c, h, now, pos.end + 0.02, padMode, s);
                    curSig = sigNow;
                }
                else if (padMode || pm == 3)
                {
                    // meme accord repete : on prolonge les notes tenues
                    for (int v = 0; v < kVoices; ++v)
                        if (voices[v].gateOn() && voiceOffPpq[v] < 1.0e17 && (padMode || voiceIsBass[v])) voiceOffPpq[v] = pos.end + 0.02;
                }
                curChordIndex = pos.index;
                curChordPacked = key;
                curChordSub = isSub; curChordApproach = isAppr;
            }
            else if (keyChordActive && firstHit && curKey == 0)
            {
                chordChanged (keyChord, rng.next(), now, now + 4.0, false, s);
            }
            while (hi < numHits && hits[(size_t) hi].offset <= s) hitToNotes (hits[(size_t) hi++], now, s);
        }

        // notes en attente (strum)
        if (numPending > 0)
            for (int i = 0; i < numPending;)
            {
                auto& p = pending[(size_t) i];
                if (p.remaining <= s)
                {
                    auto n = p.n; n.delay = 0;
                    p = pending[(size_t) --numPending];
                    startNote (n, now, s);
                }
                else ++i;
            }

        // voix (sur-echantillonnage x2)
        float l2[2] { 0.0f, 0.0f }, r2[2] { 0.0f, 0.0f };
        for (int v = 0; v < kVoices; ++v)
        {
            auto& vc = voices[v];
            if (! vc.isActive()) continue;
            const float g = voiceGain[v];
            const float a = vc.process (bp) * g, b2 = vc.process (bp) * g;
            l2[0] += a * vc.panL; l2[1] += b2 * vc.panL;
            r2[0] += a * vc.panR; r2[1] += b2 * vc.panR;
        }
        float yl = decimL.process (l2[0], l2[1]);
        float yr = decimR.process (r2[0], r2[1]);
        { const float yy = yl - dcXL + 0.9995f * dcYL; dcXL = yl; dcYL = yy; yl = yy; }
        { const float yy = yr - dcXR + 0.9995f * dcYR; dcXR = yr; dcYR = yy; yr = yy; }
        yl *= level; yr *= level;

        if (scopeWrite < scopeTotal)
        {
            if (scopeWrite % scopeDecim == 0) scopeBuf[scopeWrite / scopeDecim] = (yl + yr) * 0.5f;
            if (++scopeWrite == scopeTotal) scopeVersion.fetch_add (1);
        }

        const float g = 1.0f - duckAmt * duckEnv;
        duckEnv *= duckCoef;
        followEnv *= followCoef;
        yl *= g; yr *= g;
        float l = yl, r = yr;
        chorus.process (l, r, chRate, chDepth, chMix);
        float dl, drr;
        delay.process (yl * sDly, yr * sDly, dl, drr, dlyFb, toneCoef, ping);
        outL[s] = l + dl * dlyRet;
        outR[s] = r + drr * dlyRet;
        rvL[s] = yl * sRev; rvR[s] = yr * sRev;
    }
    for (int i = 0; i < numPending; ++i) pending[(size_t) i].remaining -= numSamples;
    duckView = duckAmt * duckEnv;
    publishChords (ppqEnd, patStartPpq, running);

    // notes jouees (interface)
    {
        int k = 0;
        for (int v = 0; v < kVoices; ++v) if (voices[v].gateOn()) sounding[k++] = voices[v].note;
        for (int i = k; i < kVoices; ++i) sounding[i] = -1;
        numSounding = k;
    }
    if (! running && keyChordActive) { curChordPacked = keyChordPacked.load(); curChordIndex = -1; }

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
    midiOutBuf = nullptr;
}

// ---------------------------------------------------------------------------
int ColorAudioProcessor::editPattern() const { return juce::jlimit (0, kPatterns - 1, (int) patternRaw->load()); }

void ColorAudioProcessor::setParamNorm (const juce::String& id, float norm)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
        p->endChangeGesture();
    }
}

void ColorAudioProcessor::setParamReal (const juce::String& id, float real)
{
    if (auto* p = apvts.getParameter (id)) setParamNorm (id, p->convertTo0to1 (real));
}

int ColorAudioProcessor::paramIndex (const juce::String& id) const
{
    if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (apvts.getParameter (id))) return p->getParameterIndex();
    return -1;
}

int ColorAudioProcessor::ccForParam (int idx) const
{
    if (idx < 0) return -1;
    for (int c = 0; c < 128; ++c) if (ccMap[c].load() == idx) return c;
    return -1;
}

void ColorAudioProcessor::clearMidiMapping (int idx)
{
    for (auto& c : ccMap) if (c.load() == idx) c = -1;
    if (learnParam.load() == idx) learnParam = -1;
}

bool ColorAudioProcessor::setChainText (const juce::String& text)
{
    auto tokens = juce::StringArray::fromTokens (text.toUpperCase().replace ("-", "+-"), " ,;", "");
    tokens.removeEmptyStrings();
    int n = 0;
    for (auto& tok : tokens)
    {
        const auto c = tok[0];
        if (c < 'A' || c >= 'A' + kPatterns || n >= kChain) return false;
        // A4 = 4 mesures de A ; A4+5 = transpose de 5 demi-tons (modulation de la forme)
        auto rest = tok.substring (1);
        int shift = 0;
        const int sgn = rest.indexOfAnyOf ("+");
        if (sgn >= 0) { shift = rest.substring (sgn + 1).getIntValue(); rest = rest.substring (0, sgn); }
        const int bars = rest.isNotEmpty() ? rest.getIntValue() : 1;
        if (bars < 1 || bars > 64 || shift < -11 || shift > 11) return false;
        chainPat[n] = (int) (c - 'A'); chainBars[n] = bars; chainShift[n] = shift; ++n;
    }
    chainLen = n; chainText = text;
    return true;
}

// ---------------------------------------------------------------------------
// Etat
juce::ValueTree ColorAudioProcessor::captureTree()
{
    auto state = apvts.copyState();
    for (auto c : { "COLORPATTERNS", "LOCKS", "MORPHB", "SCENES", "FAVS", "EVO", "CHAIN", "MIDIMAP", "UI" })
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
        juce::StringArray m, su;
        for (int p = 0; p < kPatterns; ++p) { m.add (juce::String ((juce::int64) evoMask[p].load())); su.add (juce::String ((juce::int64) evoSub[p].load())); }
        ev.setProperty ("mask", m.joinIntoString (","), nullptr);
        ev.setProperty ("sub", su.joinIntoString (","), nullptr);
        ev.setProperty ("off", floats ([&] (int i) { return evoOff[i].load(); }, kNumTP), nullptr);
    }
    state.appendChild (ev, nullptr);

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

void ColorAudioProcessor::restoreTree (const juce::ValueTree& stateIn, bool keepTransport)
{
    auto state = stateIn.createCopy();
    if (keepTransport)
        for (auto id : { "play", "pattern", "fill" })
            for (auto child : state)
                if (child.getProperty ("id").toString() == id)
                    child.setProperty ("value", apvts.getRawParameterValue (id)->load(), nullptr);

    auto parse = [] (const juce::var& v) { return juce::StringArray::fromTokens (v.toString(), ",", ""); };
    bank.fromTree (state.getChildWithName ("COLORPATTERNS"));
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
        auto m = parse (ev.getProperty ("mask")), su = parse (ev.getProperty ("sub")), off = parse (ev.getProperty ("off"));
        for (int p = 0; p < kPatterns; ++p)
        {
            evoMask[p] = p < m.size() ? (uint32_t) m[p].getLargeIntValue() : 0u;
            evoSub[p] = p < su.size() ? (uint32_t) su[p].getLargeIntValue() : 0u;
        }
        for (int i = 0; i < kNumTP; ++i) evoOff[i] = i < off.size() ? off[i].getFloatValue() : 0.0f;
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

void ColorAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureTree().createXml()) copyXmlToBinary (*xml, destData);
}

void ColorAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    restoreTree (juce::ValueTree::fromXml (*xml), false);
}

void ColorAudioProcessor::pushUndo()
{
    juce::MemoryBlock mb;
    getStateInformation (mb);
    if (! undoStack.empty() && undoStack.back() == mb) return;
    undoStack.push_back (std::move (mb));
    if (undoStack.size() > 80) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool ColorAudioProcessor::undo()
{
    if (undoStack.empty()) return false;
    juce::MemoryBlock cur; getStateInformation (cur);
    redoStack.push_back (std::move (cur));
    auto mb = std::move (undoStack.back()); undoStack.pop_back();
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize())) restoreTree (juce::ValueTree::fromXml (*xml), true);
    return true;
}

bool ColorAudioProcessor::redo()
{
    if (redoStack.empty()) return false;
    juce::MemoryBlock cur; getStateInformation (cur);
    undoStack.push_back (std::move (cur));
    auto mb = std::move (redoStack.back()); redoStack.pop_back();
    if (auto xml = getXmlFromBinary (mb.getData(), (int) mb.getSize())) restoreTree (juce::ValueTree::fromXml (*xml), true);
    return true;
}

juce::File ColorAudioProcessor::userPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Color").getChildFile ("Presets");
}

bool ColorAudioProcessor::savePresetFile (const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    currentPresetName = f.getFileNameWithoutExtension();
    auto xml = captureTree().createXml();
    return xml != nullptr && xml->writeTo (f);
}

bool ColorAudioProcessor::loadPresetFile (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    pushUndo();
    restoreTree (juce::ValueTree::fromXml (*xml), true);
    currentPresetName = f.getFileNameWithoutExtension();
    return true;
}

juce::AudioProcessorEditor* ColorAudioProcessor::createEditor() { return new ColorAudioProcessorEditor (*this); }

#ifndef ALEA_NO_PLUGIN_FILTER
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ColorAudioProcessor(); }
#endif
