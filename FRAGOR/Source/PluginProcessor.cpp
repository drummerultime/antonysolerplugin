#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace fragor;

namespace
{
    juce::String cyclesToBase64 (const std::vector<std::vector<float>>& cycles)
    {
        juce::MemoryOutputStream mo;
        for (auto& c : cycles) mo.write (c.data(), c.size() * sizeof (float));
        return mo.getMemoryBlock().toBase64Encoding();
    }

    std::vector<std::vector<float>> cyclesFromBase64 (const juce::String& s)
    {
        juce::MemoryBlock mb;
        std::vector<std::vector<float>> out;
        if (s.isEmpty() || ! mb.fromBase64Encoding (s)) return out;
        const size_t per = (size_t) kTableSize * sizeof (float);
        const size_t n = mb.getSize() / per;
        for (size_t f = 0; f < n; ++f)
        {
            std::vector<float> c ((size_t) kTableSize);
            std::memcpy (c.data(), static_cast<const char*> (mb.getData()) + f * per, per);
            out.push_back (std::move (c));
        }
        return out;
    }
}

FragorProcessor::FragorProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "FRAGOR", createLayout())
{
    auto P = [this] (const juce::String& id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };

    for (int o = 0; o < 2; ++o)
    {
        const juce::String p = o == 0 ? "a" : "b";
        osc[o] = { P (p + "On"), P (p + "Table"), P (p + "Pos"), P (p + "WarpMode"), P (p + "Warp"), P (p + "Uni"),
                   P (p + "Detune"), P (p + "Blend"), P (p + "Spread"), P (p + "Oct"), P (p + "Semi"), P (p + "Fine"),
                   P (p + "Level"), P (p + "Pan"), P (p + "RandPhase") };
    }
    for (int l = 0; l < 3; ++l)
    {
        const juce::String p = "l" + juce::String (l + 1);
        lfo[l] = { P (p + "Shape"), P (p + "Sync"), P (p + "Div"), P (p + "Rate"), P (p + "Retrig"),
                   P (p + "Phase"), P (p + "Curve"), P (p + "Smooth") };
    }
    envAmp = { P ("ampA"), P ("ampD"), P ("ampS"), P ("ampR") };
    env2   = { P ("e2A"), P ("e2D"), P ("e2S"), P ("e2R") };
    env3   = { P ("e3A"), P ("e3D"), P ("e3S"), P ("e3R") };
    flt[0] = { P ("fltOn"), P ("fltType"), P ("fltCutoff"), P ("fltReso"), P ("fltDrive"), P ("fltMorph"), P ("fltKey"), P ("fltMix"), P ("fltEnvAmt") };
    flt[1] = { P ("f2On"), P ("f2Type"), P ("f2Cutoff"), P ("f2Reso"), P ("f2Drive"), P ("f2Morph"), P ("f2Key"), P ("f2Mix"), P ("f2EnvAmt") };
    fltRouting = P ("fltRouting"); quality = P ("quality"); mpeOn = P ("mpeOn"); mpeBend = P ("mpeBend");

    subOn = P ("subOn"); subShape = P ("subShape"); subOct = P ("subOct"); subLevel = P ("subLevel"); subDirect = P ("subDirect");
    noiseLevel = P ("noiseLevel"); noiseColor = P ("noiseColor");
    ampVel = P ("ampVel"); penvAmt = P ("penvAmt"); penvDecay = P ("penvDecay");
    voiceMode = P ("voiceMode"); glide = P ("glide"); bendRange = P ("bendRange"); masterTune = P ("masterTune");
    seqOn = P ("seqOn"); seqSteps = P ("seqSteps"); seqLen = P ("seqLen"); seqReset = P ("seqReset");
    for (int s = 0; s < kNumSeqSteps; ++s) seq[s] = P ("seq" + juce::String (s + 1));
    for (int m = 0; m < kNumMatrixSlots; ++m)
    {
        const juce::String p = "m" + juce::String (m + 1);
        mSrc[m] = P (p + "Src"); mDst[m] = P (p + "Dst"); mAmt[m] = P (p + "Amt");
    }
    for (int m = 0; m < kNumMacros; ++m) macro[m] = P ("macro" + juce::String (m + 1));

    distOn = P ("distOn"); distMode = P ("distMode"); distDrive = P ("distDrive"); distBias = P ("distBias"); distTone = P ("distTone"); distMix = P ("distMix");
    dist2On = P ("dist2On"); dist2Mode = P ("dist2Mode"); dist2Drive = P ("dist2Drive"); dist2Bias = P ("dist2Bias"); dist2Tone = P ("dist2Tone"); dist2Mix = P ("dist2Mix");
    crushOn = P ("crushOn"); crushBits = P ("crushBits"); crushRate = P ("crushRate"); crushMix = P ("crushMix");
    ottOn = P ("ottOn"); ottDepth = P ("ottDepth"); ottTime = P ("ottTime"); ottUp = P ("ottUp"); ottDown = P ("ottDown"); ottOut = P ("ottOut");
    chorusOn = P ("chorusOn"); chorusRate = P ("chorusRate"); chorusDepth = P ("chorusDepth"); chorusMix = P ("chorusMix");
    dlyOn = P ("dlyOn"); dlyDiv = P ("dlyDiv"); dlyFb = P ("dlyFb"); dlyTone = P ("dlyTone"); dlyPing = P ("dlyPing"); dlyMix = P ("dlyMix");
    revOn = P ("revOn"); revSize = P ("revSize"); revDamp = P ("revDamp"); revWidth = P ("revWidth"); revMix = P ("revMix");
    subMono = P ("subMono"); subMonoFreq = P ("subMonoFreq"); width = P ("width"); clip = P ("clip"); masterGain = P ("masterGain");
    midiOut = P ("midiOut"); midiOutCC = P ("midiOutCC"); midiOutNote = P ("midiOutNote"); snapTime = P ("snapTime");

    WavetableBank::instance();
    extras.reset();
    applyExtras();

    // réglage global : normalisation auto
    auto settings = dataFolder().getChildFile ("settings.xml");
    if (auto xml = juce::XmlDocument::parse (settings)) autoGain = xml->getBoolAttribute ("autoGain", true);

    loadFactoryPreset (1);
    undoStack.clear();
}

FragorProcessor::~FragorProcessor() { stopTimer(); }

bool FragorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FragorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    ctx.sr = sampleRate;
    engine.prepare (sampleRate);
    const int q = (int) quality->load();
    engine.setOversampling (q == 0 ? 1 : (q == 1 ? 2 : 4));
    fx.prepare (sampleRate, Engine::kMaxSub);
    lastLatency = fx.latency() + (int) std::lround (engine.latencySamples());
    setLatencySamples (lastLatency);
    bufL.assign ((size_t) std::max (64, samplesPerBlock), 0.0f);
    bufR.assign ((size_t) std::max (64, samplesPerBlock), 0.0f);
    keyboardState.reset();
}

// ---------------------------------------------------------------- instantané
void FragorProcessor::snapshot (SynthParams& sp, FxParams& fp) const
{
    auto f = [] (std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto i = [] (std::atomic<float>* a) { return (int) std::lround (a->load (std::memory_order_relaxed)); };
    auto b = [] (std::atomic<float>* a) { return a->load (std::memory_order_relaxed) > 0.5f; };

    for (int o = 0; o < 2; ++o)
    {
        const auto& p = osc[o];
        sp.osc[o] = { b (p.on), i (p.table), f (p.pos), i (p.warpMode), f (p.warp), i (p.uni), f (p.detune), f (p.blend),
                      f (p.spread), i (p.oct), i (p.semi), f (p.fine), f (p.level), f (p.pan), b (p.randPhase) };
    }
    sp.subOn = b (subOn); sp.subShape = i (subShape); sp.subOct = i (subOct); sp.subLevel = f (subLevel); sp.subDirect = b (subDirect);
    sp.noiseLevel = f (noiseLevel); sp.noiseColor = f (noiseColor);
    for (int k = 0; k < 2; ++k)
    {
        const auto& p = flt[k];
        sp.flt[k] = { b (p.on), i (p.type), f (p.cutoff), f (p.reso), f (p.drive), f (p.morph), f (p.key), f (p.mix), f (p.envAmt) };
    }
    sp.routing = i (fltRouting);
    sp.amp = { f (envAmp.a), f (envAmp.d), f (envAmp.s), f (envAmp.r) };
    sp.e2  = { f (env2.a), f (env2.d), f (env2.s), f (env2.r) };
    sp.e3  = { f (env3.a), f (env3.d), f (env3.s), f (env3.r) };
    sp.ampVel = f (ampVel); sp.penvAmt = f (penvAmt); sp.penvDecay = f (penvDecay);
    sp.voiceMode = i (voiceMode); sp.glide = f (glide); sp.bendRange = i (bendRange); sp.masterTune = f (masterTune);
    for (int l = 0; l < 3; ++l)
    {
        const auto& p = lfo[l];
        sp.lfo[l] = { i (p.shape), b (p.sync), i (p.div), f (p.rate), b (p.retrig), f (p.phase), f (p.curve), f (p.smooth) };
        sp.lfoTable[l] = lfoTab[l][lfoTabIdx[l].load()];
    }
    sp.seqOn = b (seqOn); sp.seqSteps = i (seqSteps); sp.seqLen = i (seqLen); sp.seqReset = b (seqReset);
    for (int s = 0; s < kNumSeqSteps; ++s) sp.seq[s] = i (seq[s]);
    for (int m = 0; m < kNumMatrixSlots; ++m) { sp.mSrc[m] = i (mSrc[m]); sp.mDst[m] = i (mDst[m]); sp.mAmt[m] = f (mAmt[m]); }
    for (int m = 0; m < kNumMacros; ++m) sp.macro[m] = f (macro[m]);
    for (int u = 0; u < kNumUserTables; ++u) sp.userTable[u] = userTablePtr[u].load();

    for (int k = 0; k < numFxModules; ++k) fp.order[k] = fxOrderA[k].load();
    fp.distOn = b (distOn); fp.distMode = i (distMode); fp.distDrive = f (distDrive); fp.distBias = f (distBias); fp.distTone = f (distTone); fp.distMix = f (distMix);
    fp.dist2On = b (dist2On); fp.dist2Mode = i (dist2Mode); fp.dist2Drive = f (dist2Drive); fp.dist2Bias = f (dist2Bias); fp.dist2Tone = f (dist2Tone); fp.dist2Mix = f (dist2Mix);
    fp.crushOn = b (crushOn); fp.crushBits = f (crushBits); fp.crushRate = f (crushRate); fp.crushMix = f (crushMix);
    fp.ottOn = b (ottOn); fp.ottDepth = f (ottDepth); fp.ottTime = f (ottTime); fp.ottUp = f (ottUp); fp.ottDown = f (ottDown); fp.ottOut = f (ottOut);
    fp.chorusOn = b (chorusOn); fp.chorusRate = f (chorusRate); fp.chorusDepth = f (chorusDepth); fp.chorusMix = f (chorusMix);
    fp.dlyOn = b (dlyOn); fp.dlyDiv = i (dlyDiv); fp.dlyFb = f (dlyFb); fp.dlyTone = f (dlyTone); fp.dlyPing = b (dlyPing); fp.dlyMix = f (dlyMix);
    fp.revOn = b (revOn); fp.revSize = f (revSize); fp.revDamp = f (revDamp); fp.revWidth = f (revWidth); fp.revMix = f (revMix);
    fp.subMono = b (subMono); fp.subMonoFreq = f (subMonoFreq); fp.width = f (width); fp.clip = f (clip); fp.masterGain = f (masterGain);
}

void FragorProcessor::handleMidi (const juce::MidiMessage& m, const SynthParams& sp, const VoiceCtx& c)
{
    const bool mpe = mpeOn->load() > 0.5f;
    const int ch = m.getChannel();
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), mpe ? ch : 1, sp, c);
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber(), mpe ? ch : -1, sp);
    else if (m.isPitchWheel())
    {
        const float v = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        if (mpe && ch != 1) engine.channelBend (ch, v * mpeBend->load());
        else bendNorm = v;
    }
    else if (m.isChannelPressure())
    {
        const float v = (float) m.getChannelPressureValue() / 127.0f;
        if (mpe && ch != 1) engine.channelPressure (ch, v, true);
        else chanPressure = v;
    }
    else if (m.isAftertouch())
        engine.polyPressure (m.getNoteNumber(), (float) m.getAfterTouchValue() / 127.0f);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const float v = (float) m.getControllerValue() / 127.0f;
        if (cc == 1)        modWheel = v;
        else if (cc == 74)  { if (mpe && ch != 1) engine.channelSlide (ch, v, true); else slideVal = v; }
        else if (cc == 64)  engine.sustainPedal (m.getControllerValue() >= 64, sp);
        else if (cc == 120) engine.reset();
        else if (cc == 123) engine.allNotesOff();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
}

void FragorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    if ((int) bufL.size() < n) { bufL.resize ((size_t) n); bufR.resize ((size_t) n); }

    SynthParams sp; FxParams fp;
    snapshot (sp, fp);

    // sur-échantillonnage
    {
        const int q = (int) quality->load();
        const int f = q == 0 ? 1 : (q == 1 ? 2 : 4);
        if (f != engine.getOversampling())
        {
            engine.setOversampling (f);
            const int lat = fx.latency() + (int) std::lround (engine.latencySamples());
            if (lat != lastLatency) { lastLatency = lat; setLatencySamples (lat); }
        }
    }

    double bpm = fallbackBpm, startBeats = internalBeats;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) { startBeats = *q; playing = true; }
        }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    ctx.bpm = bpm;
    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const double beatsPerSample = bpm / 60.0 / sr;

    auto refreshCtx = [&]
    {
        ctx.modWheel = modWheel;
        ctx.bendSemis = bendNorm * (float) sp.bendRange;
        ctx.chanPressure = chanPressure;
        ctx.slide = slideVal;
        ctx.mpe = mpeOn->load() > 0.5f;
    };
    refreshCtx();

    const int outMode = (int) midiOut->load();
    const int outCC = (int) midiOutCC->load();
    const int outNote = (int) midiOutNote->load();
    juce::MidiBuffer out;

    auto it = midi.cbegin();
    const auto end = midi.cend();
    int pos = 0;
    while (pos < n)
    {
        int nextEvent = n;
        while (it != end)
        {
            const auto md = *it;
            if (md.samplePosition <= pos)
            {
                ctx.globalBeats = startBeats + pos * beatsPerSample;
                ctx.globalTime  = timeSec + pos / sr;
                handleMidi (md.getMessage(), sp, ctx);
                refreshCtx();
                ++it;
            }
            else { nextEvent = md.samplePosition; break; }
        }

        const int len = std::min ({ 32, nextEvent - pos, n - pos });
        if (len <= 0) break;

        ctx.globalBeats = startBeats + pos * beatsPerSample;
        ctx.globalTime  = timeSec + pos / sr;

        float* L = bufL.data() + pos;
        float* R = bufR.data() + pos;
        std::fill (L, L + len, 0.0f);
        std::fill (R, R + len, 0.0f);
        engine.render (L, R, len, sp, ctx);
        fx.process (L, R, len, fp, engine.globalMod, bpm);

        // --- sortie MIDI : LFO 1 en CC et/ou notes à chaque pas du séquenceur
        if (outMode == 1 || outMode == 3)
        {
            const int v = juce::jlimit (0, 127, (int) std::lround (engine.lfoMonitor[0] * 127.0f));
            if (v != lastCcOut && engine.activeVoiceCount() > 0) { out.addEvent (juce::MidiMessage::controllerEvent (1, outCC, v), pos); lastCcOut = v; }
        }
        if (outMode >= 2)
        {
            if (pendingOffSamples >= 0)
            {
                pendingOffSamples -= len;
                if (pendingOffSamples < 0) out.addEvent (juce::MidiMessage::noteOff (1, pendingOffNote), pos);
            }
            const int st = engine.seqMonitor;
            if (st >= 0 && st != lastStepOut)
            {
                if (pendingOffSamples >= 0) out.addEvent (juce::MidiMessage::noteOff (1, pendingOffNote), pos);
                out.addEvent (juce::MidiMessage::noteOn (1, outNote, (juce::uint8) (st == 0 ? 127 : 100)), pos);
                pendingOffNote = outNote;
                pendingOffSamples = (int) (0.03 * sr);
            }
            lastStepOut = st;
        }
        pos += len;
    }
    midi.swapWith (out);

    if (buffer.getNumChannels() >= 2)
    {
        buffer.copyFrom (0, 0, bufL.data(), n);
        buffer.copyFrom (1, 0, bufR.data(), n);
    }
    else if (buffer.getNumChannels() == 1)
    {
        buffer.copyFrom (0, 0, bufL.data(), n, 0.5f);
        buffer.addFrom (0, 0, bufR.data(), n, 0.5f);
    }

    int w = scopeWrite.load();
    for (int i = 0; i < n; ++i)
    {
        scope[w] = 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]);
        w = (w + 1) % kScopeSize;
    }
    scopeWrite.store (w);
    for (int l = 0; l < 3; ++l) lfoView[l].store (engine.lfoMonitor[l]);
    for (int d = 0; d < numModDests; ++d) liveMod[d].store (engine.liveMod[d]);
    seqStepView.store (engine.seqMonitor);
    activeVoices.store (engine.activeVoiceCount());

    internalBeats = (playing ? startBeats : internalBeats) + n * beatsPerSample;
    timeSec += n / sr;
}

// ---------------------------------------------------------------- courbes, ordre FX, tables
void FragorProcessor::setLfoCurve (int i, const LfoCurve& c)
{
    extras.curves[(size_t) i] = c;
    const int next = 1 - lfoTabIdx[i].load();
    renderCurve (c, lfoTab[i][next], kLfoTableSize);
    lfoTabIdx[i].store (next);
    ++uiChangeCounter;
}

std::array<int, numFxModules> FragorProcessor::getFxOrder() const
{
    std::array<int, numFxModules> o;
    for (int k = 0; k < numFxModules; ++k) o[(size_t) k] = fxOrderA[k].load();
    return o;
}

void FragorProcessor::setFxOrder (const std::array<int, numFxModules>& o)
{
    // validation : permutation complète
    std::array<bool, numFxModules> seen {};
    for (int v : o) if (v < 0 || v >= numFxModules || seen[(size_t) v]) return; else seen[(size_t) v] = true;
    extras.order = o;
    for (int k = 0; k < numFxModules; ++k) fxOrderA[k].store (o[(size_t) k]);
    ++uiChangeCounter;
}

void FragorProcessor::applyExtras()
{
    for (int i = 0; i < 3; ++i) setLfoCurve (i, extras.curves[(size_t) i]);
    setFxOrder (extras.order);
}

void FragorProcessor::setUserTable (int slot, std::vector<std::vector<float>> cycles, const juce::String& name)
{
    std::shared_ptr<Wavetable> wt;
    if (! cycles.empty()) wt = WavetableBank::buildFromCycles (cycles);
    if (userTableOwned[slot]) tableGraveyard.push_back (userTableOwned[slot]);
    if (tableGraveyard.size() > 6) tableGraveyard.erase (tableGraveyard.begin());
    userTableOwned[slot] = wt;
    userCycles[slot] = std::move (cycles);
    userTableName[slot] = wt ? name : juce::String();
    userTablePtr[slot].store (wt.get());
    ++uiChangeCounter;
}

bool FragorProcessor::importWavetable (int slot, const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) { error = "Format audio non reconnu"; return false; }
    const int len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len);
    for (int c = 0; c < buf.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (c, i) / (float) buf.getNumChannels();

    std::vector<std::vector<float>> cycles;
    if (! WavetableBank::sliceAudio (mono.data(), len, cycles)) { error = "Fichier vide ou trop court"; return false; }
    pushUndo();
    setUserTable (slot, std::move (cycles), file.getFileNameWithoutExtension());
    if (auto* p = apvts.getParameter (slot == 0 ? "aTable" : "bTable"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (WavetableBank::kNumBuiltin + slot)));
    return true;
}

int FragorProcessor::addModulation (int src, int dst, float amount)
{
    auto setP = [this] (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); }
    };
    for (int s = 0; s < kNumMatrixSlots; ++s)
        if ((int) mSrc[s]->load() == src && (int) mDst[s]->load() == dst) return s;
    for (int s = 0; s < kNumMatrixSlots; ++s)
        if ((int) mSrc[s]->load() == 0 || (int) mDst[s]->load() == 0)
        {
            const juce::String p = "m" + juce::String (s + 1);
            setP (p + "Src", (float) src);
            setP (p + "Dst", (float) dst);
            setP (p + "Amt", amount);
            return s;
        }
    return -1;
}

// ---------------------------------------------------------------- snapshots
void FragorProcessor::storeSnapshot (int i)
{
    for (int m = 0; m < kNumMacros; ++m) snaps[i][m] = macro[m]->load();
    snapUsed[i] = true;
    ++uiChangeCounter;
}

void FragorProcessor::recallSnapshot (int i)
{
    if (! snapUsed[i]) return;
    for (int m = 0; m < kNumMacros; ++m) { morphFrom[m] = macro[m]->load(); morphTo[m] = snaps[i][m]; }
    morphPos = 0.0;
    if (snapTime->load() < 0.01f) { morphPos = 1.0; timerCallback(); }
    else startTimerHz (60);
}

void FragorProcessor::timerCallback()
{
    const double dur = std::max (0.01, (double) snapTime->load());
    morphPos = std::min (1.0, morphPos + 1.0 / 60.0 / dur);
    const float t = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * morphPos));
    for (int m = 0; m < kNumMacros; ++m)
        if (auto* p = apvts.getParameter ("macro" + juce::String (m + 1)))
            p->setValueNotifyingHost (p->convertTo0to1 (morphFrom[m] + (morphTo[m] - morphFrom[m]) * t));
    if (morphPos >= 1.0) stopTimer();
}

// ---------------------------------------------------------------- loudness
float FragorProcessor::measureRms()
{
    SynthParams sp; FxParams fp;
    snapshot (sp, fp);
    constexpr double fs = 44100.0, tempo = 140.0;
    auto e = std::make_unique<Engine>();
    auto f = std::make_unique<FxChain>();
    e->prepare (fs);
    e->setOversampling (1);
    f->prepare (fs, Engine::kMaxSub);

    struct N { double s, l; int note; };
    std::vector<N> notes;
    if (sp.voiceMode == 0) notes = { { 0, 3.5, 53 }, { 0, 3.5, 56 }, { 0, 3.5, 60 }, { 4, 3.5, 49 }, { 4, 3.5, 53 }, { 4, 3.5, 56 } };
    else                   notes = { { 0, 1.5, 41 }, { 1.5, 0.5, 41 }, { 2, 2, 44 }, { 4, 3, 41 }, { 7, 1, 39 } };

    VoiceCtx c;
    c.sr = fs; c.bpm = tempo;
    const int total = (int) (8.0 * 60.0 / tempo * fs);
    const double spb = 60.0 / tempo * fs;
    float L[Engine::kMaxSub], R[Engine::kMaxSub];
    double sum = 0.0; int cnt = 0;
    for (int pos = 0; pos < total; pos += 32)
    {
        for (auto& nt : notes)
        {
            const int on = (int) (nt.s * spb), off = (int) ((nt.s + nt.l) * spb);
            if (on >= pos && on < pos + 32)   e->noteOn (nt.note, 0.9f, 1, sp, c);
            if (off >= pos && off < pos + 32) e->noteOff (nt.note, -1, sp);
        }
        c.globalBeats = pos / spb;
        c.globalTime = pos / fs;
        std::fill (L, L + 32, 0.0f); std::fill (R, R + 32, 0.0f);
        e->render (L, R, 32, sp, c);
        f->process (L, R, 32, fp, e->globalMod, tempo);
        if (pos > (int) (0.05 * fs))
            for (int i = 0; i < 32; ++i) { sum += (double) L[i] * L[i] + (double) R[i] * R[i]; cnt += 2; }
    }
    return cnt > 0 ? (float) std::sqrt (sum / cnt) : 0.0f;
}

void FragorProcessor::normalizeLoudness()
{
    constexpr float targetDb = -10.0f;
    auto* gain = apvts.getParameter ("masterGain");
    for (int pass = 0; pass < 2; ++pass)
    {
        const float rms = measureRms();
        if (rms < 1.0e-4f) return;
        const float trim = juce::jlimit (-15.0f, 15.0f, targetDb - juce::Decibels::gainToDecibels (rms));
        if (std::abs (trim) < 0.5f) return;
        const float g = juce::jlimit (-36.0f, 12.0f, masterGain->load() + trim);
        gain->setValueNotifyingHost (gain->convertTo0to1 (g));
    }
}

void FragorProcessor::afterPatchChange (bool normalise)
{
    applyExtras();
    if (normalise && autoGain) normalizeLoudness();
    ++uiChangeCounter;
}

// ---------------------------------------------------------------- historique
void FragorProcessor::pushUndo()
{
    undoStack.push_back (captureState());
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool FragorProcessor::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (captureState());
    auto t = undoStack.back();
    undoStack.pop_back();
    applyStateTree (t);
    return true;
}

bool FragorProcessor::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (captureState());
    auto t = redoStack.back();
    redoStack.pop_back();
    applyStateTree (t);
    return true;
}

// ---------------------------------------------------------------- programmes
int FragorProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

void FragorProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < getNumPrograms()) loadFactoryPreset (index);
}

const juce::String FragorProcessor::getProgramName (int index)
{
    const auto& p = factoryPresets();
    return index >= 0 && index < (int) p.size() ? p[(size_t) index].name : juce::String();
}

void FragorProcessor::loadFactoryPreset (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    pushUndo();
    PatchBuilder b (apvts, macroNames, extras);
    list[(size_t) index].build (b);
    currentProgram = index;
    presetName = list[(size_t) index].name;
    presetCategory = list[(size_t) index].category;
    afterPatchChange (index != 0);
}

void FragorProcessor::generate (int style)
{
    pushUndo();
    PatchBuilder b (apvts, macroNames, extras);
    rng.setSeedRandomly();
    generatePatch (b, style, rng);
    presetName = "GEN " + generatorStyles()[style] + " #" + juce::String (rng.nextInt (9000) + 1000);
    presetCategory = "GENERATED";
    afterPatchChange (true);
}

void FragorProcessor::mutate (float amount)
{
    pushUndo();
    rng.setSeedRandomly();
    mutatePatch (apvts, extras, rng, amount);
    if (! presetName.endsWith ("*")) presetName << "*";
    afterPatchChange (true);
}

juce::File FragorProcessor::dataFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("FRAGOR");
    f.createDirectory();
    return f;
}

juce::File FragorProcessor::userPresetFolder()
{
    auto f = dataFolder().getChildFile ("Presets");
    f.createDirectory();
    return f;
}

bool FragorProcessor::saveUserPreset (const juce::File& file, const juce::String& category)
{
    auto state = captureState();
    state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);
    state.setProperty ("category", category.isEmpty() ? juce::String ("USER") : category.toUpperCase(), nullptr);
    if (auto xml = state.createXml())
        if (xml->writeTo (file))
        {
            presetName = file.getFileNameWithoutExtension();
            presetCategory = category.toUpperCase();
            ++uiChangeCounter;
            return true;
        }
    return false;
}

bool FragorProcessor::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (apvts.state.getType()))
        {
            pushUndo();
            applyStateTree (tree);
            return true;
        }
    }
    return false;
}

juce::ValueTree FragorProcessor::captureState() const
{
    auto state = const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState();
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("category", presetCategory, nullptr);
    state.setProperty ("macroNames", macroNames.joinIntoString ("|"), nullptr);
    for (int i = 0; i < 3; ++i) state.setProperty ("lfoCurve" + juce::String (i + 1), curveToString (extras.curves[(size_t) i]), nullptr);
    juce::StringArray ord;
    for (int v : extras.order) ord.add (juce::String (v));
    state.setProperty ("fxOrder", ord.joinIntoString (","), nullptr);
    juce::StringArray sn;
    for (int i = 0; i < kNumSnapshots; ++i)
        sn.add (snapUsed[i] ? juce::String (snaps[i][0]) + ":" + juce::String (snaps[i][1]) + ":" + juce::String (snaps[i][2]) + ":" + juce::String (snaps[i][3]) : juce::String ("-"));
    state.setProperty ("snapshots", sn.joinIntoString ("|"), nullptr);
    for (int u = 0; u < kNumUserTables; ++u)
    {
        state.setProperty ("userTable" + juce::String (u), cyclesToBase64 (userCycles[u]), nullptr);
        state.setProperty ("userTableName" + juce::String (u), userTableName[u], nullptr);
    }
    return state;
}

void FragorProcessor::applyStateTree (const juce::ValueTree& tree)
{
    apvts.replaceState (tree.createCopy());
    presetName = tree.getProperty ("presetName", "INIT").toString();
    presetCategory = tree.getProperty ("category", "USER").toString();
    auto m = juce::StringArray::fromTokens (tree.getProperty ("macroNames", "MACRO 1|MACRO 2|MACRO 3|MACRO 4").toString(), "|", "");
    while (m.size() < kNumMacros) m.add ("MACRO " + juce::String (m.size() + 1));
    macroNames = m;

    PatchExtras ex;
    ex.reset();
    for (int i = 0; i < 3; ++i)
    {
        auto s = tree.getProperty ("lfoCurve" + juce::String (i + 1)).toString();
        if (s.isNotEmpty()) ex.curves[(size_t) i] = curveFromString (s);
    }
    auto ord = juce::StringArray::fromTokens (tree.getProperty ("fxOrder").toString(), ",", "");
    if (ord.size() == numFxModules)
        for (int k = 0; k < numFxModules; ++k) ex.order[(size_t) k] = ord[k].getIntValue();
    extras = ex;
    applyExtras();

    auto sn = juce::StringArray::fromTokens (tree.getProperty ("snapshots").toString(), "|", "");
    for (int i = 0; i < kNumSnapshots; ++i)
    {
        snapUsed[i] = false;
        if (i < sn.size() && sn[i] != "-" && sn[i].isNotEmpty())
        {
            auto v = juce::StringArray::fromTokens (sn[i], ":", "");
            if (v.size() == kNumMacros)
            {
                for (int k = 0; k < kNumMacros; ++k) snaps[i][k] = v[k].getFloatValue();
                snapUsed[i] = true;
            }
        }
    }
    for (int u = 0; u < kNumUserTables; ++u)
        setUserTable (u, cyclesFromBase64 (tree.getProperty ("userTable" + juce::String (u)).toString()),
                      tree.getProperty ("userTableName" + juce::String (u)).toString());
    ++uiChangeCounter;
}

void FragorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = captureState().createXml())
        copyXmlToBinary (*xml, destData);
}

void FragorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (apvts.state.getType()))
            applyStateTree (tree);
    }
}

juce::AudioProcessorEditor* FragorProcessor::createEditor() { return new FragorEditor (*this); }

#ifndef FRAGOR_RENDER_TOOL
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FragorProcessor(); }
#endif
