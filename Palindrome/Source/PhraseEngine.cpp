#include "PhraseEngine.h"

PhraseEngine::PhraseEngine() : juce::Thread ("Palindrome render")
{
    bank.fill (-1);
    for (auto& v : bankLengthView) v.store (0.0f);
    for (auto& v : sequence) v.store (0);
    for (auto& v : dirSequence) v.store (0);
    for (auto& v : importReady) v.store (0);
    for (auto& v : bankPhraseView) v.store (-1);
    for (auto& v : bankVersions) v.store (0);
}

PhraseEngine::~PhraseEngine()
{
    stopThread (3000);
}

void PhraseEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    stopThread (3000);
    prepared.store (false);

    sr = sampleRate > 0.0 ? sampleRate : 48000.0;

    // historique court : une phrase max + marge (chaque phrase est copiée dès la fin de sa capture)
    historyLength = (int) std::ceil (sr * (maxPhraseSeconds + 20.0));
    history.setSize (2, historyLength, false, true, false);
    history.clear();

    const int maxPhrase = (int) std::ceil (sr * maxPhraseSeconds) + 1;

    {
        const juce::ScopedLock sl (rawLock);
        for (auto& ph : phrases)
        {
            ph.state.store (PhraseFree);
            ph.refs = 0;
            ph.raw.setSize (0, 0);
        }
    }

    for (auto& slot : slots)
    {
        slot.buffer.setSize (2, maxPhrase, false, true, false);
        slot.buffer.clear();
        slot.state.store (SlotFree);
        slot.owner = -1;
        slot.phrase = -1;
        const juce::SpinLock::ScopedLockType l (slot.infoLock);
        slot.info = {};
    }

    for (auto& p : passes) p = Pass {};

    bank.fill (-1);
    for (int k = 0; k < maxBankSize; ++k)
    {
        bankLengthView[(size_t) k].store (0.0f);
        bankPhraseView[(size_t) k].store (-1);
        bankVersions[(size_t) k].fetch_add (1);
    }

    now = 0;
    gridOrigin = 0;
    capState = CapIdle;
    flow = FlowNone;
    stepDone = false;
    skipSilence = false;
    seqPos = 0;
    lastBankSlot = -1;
    lastMode = -1;
    alternateReverse = true;
    heldPhrase = heldBankSlot = -1;
    captureRequest.store (false);
    nextRequest.store (false);
    clearRequest.store (false);
    displaySlot.store (-1);

    prepared.store (true);
    startThread();
}

void PhraseEngine::release()
{
    stopThread (3000);
    prepared.store (false);
}

//==============================================================================
juce::Array<int> PhraseEngine::parseSequence (const juce::String& text)
{
    juce::Array<int> out;
    juce::String digits;

    auto flush = [&]
    {
        if (digits.isNotEmpty())
        {
            const int v = digits.getIntValue();
            if (v >= 1 && v <= maxBankSize && out.size() < maxSequence) out.add (v);
            digits.clear();
        }
    };

    for (auto c : text)
    {
        if (juce::CharacterFunctions::isDigit (c)) digits += c;
        else flush();
    }
    flush();
    return out;
}

juce::Array<int> PhraseEngine::parseDirectionSequence (const juce::String& text)
{
    juce::Array<int> out;
    for (auto c : text.toUpperCase())
    {
        int v = -1;
        switch (c)
        {
            case 'R': v = DirReverse; break;
            case 'N': v = DirNormal;  break;
            case 'X': case 'S': case '+': v = DirBoth; break;
            case '-': case '.': case '_': v = DirRest; break;
            default: break;
        }
        if (v >= 0 && out.size() < maxSequence) out.add (v);
    }
    return out;
}

void PhraseEngine::setDirectionSequence (const juce::String& text)
{
    const auto seq = parseDirectionSequence (text);
    for (int i = 0; i < seq.size(); ++i)
        dirSequence[(size_t) i].store (seq[i]);
    dirSequenceLength.store (seq.size());
}

void PhraseEngine::setSlotLocked (int k, bool locked)
{
    if (k < 0 || k >= maxBankSize) return;
    int m = lockMask.load();
    m = locked ? (m | (1 << k)) : (m & ~(1 << k));
    lockMask.store (m);
}

void PhraseEngine::setSequence (const juce::String& text)
{
    const auto seq = parseSequence (text);
    for (int i = 0; i < seq.size(); ++i)
        sequence[(size_t) i].store (seq[i] - 1);
    sequenceLength.store (seq.size());
}

//==============================================================================
bool PhraseEngine::copyBankAudio (int k, juce::AudioBuffer<float>& out)
{
    if (k < 0 || k >= maxBankSize) return false;
    const int idx = bankPhraseView[(size_t) k].load();
    if (idx < 0) return false;

    const juce::ScopedLock sl (rawLock);
    auto& ph = phrases[(size_t) idx];
    if (ph.state.load() != PhraseStored || ph.raw.getNumSamples() == 0) return false;
    out.makeCopyOf (ph.raw);
    return true;
}

void PhraseEngine::queueImport (int k, const juce::AudioBuffer<float>& audio, double sourceRate)
{
    if (k < 0 || k >= maxBankSize || ! prepared.load() || audio.getNumSamples() < 16) return;
    if (importReady[(size_t) k].load() != 0) return;

    const double ratio = sourceRate > 0.0 ? sourceRate / sr : 1.0;
    const int maxLen = (int) (maxPhraseSeconds * sr);
    const int len = juce::jlimit (16, maxLen, (int) (audio.getNumSamples() / ratio));
    const int inCh = audio.getNumChannels();

    juce::AudioBuffer<float> buf (2, len);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* s = audio.getReadPointer (juce::jmin (ch, inCh - 1));
        float* d = buf.getWritePointer (ch);
        const int n = audio.getNumSamples();
        for (int j = 0; j < len; ++j)
        {
            const double sp = j * ratio;
            const int i0 = juce::jmin ((int) sp, n - 1);
            const int i1 = juce::jmin (i0 + 1, n - 1);
            const float f = (float) (sp - i0);
            d[j] = s[i0] + f * (s[i1] - s[i0]);
        }
    }

    importBuffers[(size_t) k] = std::move (buf);
    importReady[(size_t) k].store (1);
}

void PhraseEngine::applyImports()
{
    for (int k = 0; k < maxBankSize; ++k)
    {
        if (importReady[(size_t) k].load() != 1) continue;

        for (int i = 0; i < maxPhrases; ++i)
        {
            auto& ph = phrases[(size_t) i];
            if (ph.state.load() != PhraseFree) continue;

            // phrase libre : personne d'autre n'y touche, on échange les tampons (sans allocation)
            std::swap (ph.raw, importBuffers[(size_t) k]);
            ph.generation.fetch_add (1);
            ph.length = ph.raw.getNumSamples();
            ph.captureStart = 0;
            ph.refs = 0;
            ph.state.store (PhraseStored);
            setBankPhrase (k, i);
            importReady[(size_t) k].store (0);
            break;
        }
    }
}

//==============================================================================
juce::int64 PhraseEngine::snapToGrid (juce::int64 t, const Settings& s) const
{
    if (s.gridSamples <= 1.0) return t;
    const double k = std::round ((double) (t - gridOrigin) / s.gridSamples);
    return gridOrigin + (juce::int64) std::llround (k * s.gridSamples);
}

void PhraseEngine::startCapture (juce::int64 at, const Settings& s, int purpose, int bankSlot)
{
    capStart = juce::jmax ((juce::int64) 0, snapToGrid (at, s));
    capLength = juce::jlimit ((int) (0.25 * sr), (int) (sr * maxPhraseSeconds), (int) std::round (s.phraseSeconds * sr));
    capState = CapRecording;
    capPurpose = purpose;
    capBankSlot = bankSlot;
}

void PhraseEngine::armCapture (int purpose, int bankSlot)
{
    capState = CapArmed;
    capPurpose = purpose;
    capBankSlot = bankSlot;
}

void PhraseEngine::beginFlowCapture (const Settings& s, bool immediate)
{
    int purpose = PurposeCycle, slot = -1;

    if (s.captureMode == Bank)
    {
        purpose = PurposeBankFill;
        slot = firstEmptyBankSlot (s);
        if (slot < 0) slot = 0;
    }

    if (immediate) startCapture (now, s, purpose, slot);
    else           armCapture (purpose, slot);

    flow = FlowCapture;
}

int PhraseEngine::allocatePhrase (juce::int64 start, int length)
{
    for (int i = 0; i < maxPhrases; ++i)
    {
        auto& ph = phrases[(size_t) i];
        if (ph.state.load() != PhraseFree) continue;

        ph.captureStart = start;
        ph.length = length;
        ph.refs = 0;
        ph.state.store (PhraseStoreRequested);
        notify();
        return i;
    }
    return -1;
}

void PhraseEngine::unref (int idx)
{
    if (idx < 0) return;
    auto& ph = phrases[(size_t) idx];
    if (--ph.refs <= 0)
    {
        ph.refs = 0;
        ph.state.store (PhraseDead);
        notify();
    }
}

void PhraseEngine::setBankPhrase (int k, int idx)
{
    if (k < 0 || k >= maxBankSize) return;
    const int old = bank[(size_t) k];
    if (idx >= 0) ++phrases[(size_t) idx].refs;
    bank[(size_t) k] = idx;
    bankPhraseView[(size_t) k].store (idx);
    bankVersions[(size_t) k].fetch_add (1);
    if (old >= 0) unref (old);
}

void PhraseEngine::keepLastPhrase (int idx, int bankSlot)
{
    heldBankSlot = bankSlot;
    if (idx == heldPhrase) return;
    if (idx >= 0) ++phrases[(size_t) idx].refs;
    const int old = heldPhrase;
    heldPhrase = idx;
    if (old >= 0) unref (old);
}

int PhraseEngine::replaceTarget (const Settings& s) const
{
    const int n = juce::jlimit (1, maxBankSize, s.bankSize);
    const int k = juce::jlimit (0, n - 1, s.targetSlot);
    for (int t = 0; t < n; ++t)
    {
        const int c = (k + t) % n;
        if (! isSlotLocked (c)) return c;
    }
    return -1;   // tout est verrouillé
}

bool PhraseEngine::bankFull (const Settings& s) const
{
    const int n = juce::jlimit (1, maxBankSize, s.bankSize);
    for (int k = 0; k < n; ++k)
        if (bank[(size_t) k] < 0) return false;
    return true;
}

int PhraseEngine::firstEmptyBankSlot (const Settings& s) const
{
    const int n = juce::jlimit (1, maxBankSize, s.bankSize);
    for (int k = 0; k < n; ++k)
        if (bank[(size_t) k] < 0) return k;
    return -1;
}

double PhraseEngine::randomSilence (const Settings& s)
{
    double lo = juce::jlimit (0.0, maxSilenceSeconds, s.silenceMin);
    double hi = juce::jlimit (0.0, maxSilenceSeconds, s.silenceMax);
    if (hi < lo) std::swap (lo, hi);
    return lo + (hi - lo) * rng.nextDouble();
}

//==============================================================================
void PhraseEngine::finishCapture (const Settings& s)
{
    const juce::int64 end = capStart + capLength;
    const int purpose = capPurpose;
    const int k = capBankSlot;
    capState = CapIdle;

    const int idx = allocatePhrase (capStart, capLength);

    const juce::int64 delay = (juce::int64) std::round (juce::jlimit (minDelaySeconds, maxDelaySeconds, s.delaySeconds) * sr);
    const juce::int64 origin = s.delayFromEnd ? end : capStart;
    const juce::int64 first = juce::jmax (origin + delay, end + (juce::int64) (calcMarginSeconds * sr));

    switch (purpose)
    {
        case PurposeCycle:
        {
            const bool ok = idx >= 0 && schedulePasses (idx, first, s, -1);
            flow = FlowPlaying;
            if (! ok) stepDone = true;
            break;
        }

        case PurposeBankFill:
        {
            if (idx >= 0) setBankPhrase (k, idx);
            const bool ok = idx >= 0 && schedulePasses (idx, first, s, k);
            lastBankSlot = k;
            flow = FlowPlaying;
            if (! ok) stepDone = true;
            break;
        }

        case PurposeBankReplace:
            if (idx >= 0) setBankPhrase (k, idx);
            break;

        case PurposeFree:
        default:
            if (idx >= 0) schedulePasses (idx, first, s, -1);

            if (s.captureMode == Continuous && ! s.hold) startCapture (end, s, PurposeFree, -1);
            else if (s.captureMode == Threshold)         armCapture (PurposeFree, -1);
            break;
    }

    if (idx >= 0 && phrases[(size_t) idx].refs == 0)
        phrases[(size_t) idx].state.store (PhraseDead);
}

bool PhraseEngine::schedulePasses (int idx, juce::int64 first, const Settings& s, int bankSlot)
{
    if (idx < 0) return false;

    const int phraseLen = phrases[(size_t) idx].length;
    int order = s.order;
    int reps = s.repetitions;
    int transposeOffset = 0;

    if (bankSlot >= 0 && bankSlot < maxBankSize)
    {
        if (s.slotOrder[(size_t) bankSlot] >= 0) order = s.slotOrder[(size_t) bankSlot];
        if (s.slotReps[(size_t) bankSlot] > 0)   reps = s.slotReps[(size_t) bankSlot];
        transposeOffset = s.slotTranspose[(size_t) bankSlot];
    }
    reps = juce::jlimit (1, maxRepetitions, reps);

    const juce::int64 gap = (juce::int64) std::round (juce::jlimit (0.0, maxRepeatGapSeconds, s.repeatGap) * sr);
    const juce::uint32 phraseSeed = (juce::uint32) rng.nextInt();

    auto panFor = [&] (bool reverse, int r)
    {
        switch (s.stereoMode)
        {
            case ReverseLeft:  return reverse ? -1.0f : 1.0f;
            case ReverseRight: return reverse ? 1.0f : -1.0f;
            case PingPong:     return (r % 2 == 0) ? -1.0f : 1.0f;
            case StereoRandom: return rng.nextFloat() * 2.0f - 1.0f;
            case StereoCentre:
            default:           return 0.0f;
        }
    };

    auto speedFor = [&] (int r)
    {
        const float pos = reps > 1 ? (float) r / (float) (reps - 1) : 0.0f;
        switch (s.speedMode)
        {
            case SpeedHalf:        return 0.5f;
            case SpeedDouble:      return 2.0f;
            case SpeedAlternate:   return (r % 2 == 0) ? 1.0f : 0.5f;
            case SpeedSlowingDown: return std::pow (0.5f, pos);
            case SpeedSpeedingUp:  return std::pow (2.0f, pos);
            case SpeedRandom:      { const float v[3] = { 0.5f, 1.0f, 2.0f }; return v[rng.nextInt (3)]; }
            case SpeedNormal:
            default:               return 1.0f;
        }
    };

    int lastAdded = -1;
    juce::int64 t = first;
    const int dirLen = dirSequenceLength.load();

    for (int r = 0; r < reps; ++r)
    {
        bool reverse = false;
        bool both = false;
        bool rest = false;
        switch (order)
        {
            case TypedSequence:
            {
                const int d = dirLen > 0 ? dirSequence[(size_t) (r % dirLen)].load() : (r % 2 == 0 ? DirReverse : DirNormal);
                reverse = d == DirReverse;
                both = d == DirBoth;
                rest = d == DirRest;
                break;
            }
            case AlternateStartReverse: reverse = (r % 2 == 0); break;
            case AlternateStartNormal:  reverse = (r % 2 == 1); break;
            case ReverseOnly:           reverse = true;  break;
            case NormalOnly:            reverse = false; break;
            case RandomDirection:       reverse = rng.nextBool(); break;
            case Superimposed:          both = true; break;
            case Alternate:
            default:                    reverse = alternateReverse; alternateReverse = ! alternateReverse; break;
        }

        const juce::uint32 seed = s.freezeRandom ? phraseSeed : (juce::uint32) rng.nextInt();
        const float speed = speedFor (r);
        const int len = juce::jmax (1, (int) std::round (phraseLen / speed));
        bool full = false;

        if (rest) { t += len + gap; continue; }   // silence à la place de cette répétition

        for (int v = 0; v < (both ? 2 : 1); ++v)
        {
            const bool rev = both ? (v == 0) : reverse;

            Pass p;
            p.state = PassWaiting;
            p.phrase = idx;
            p.length = len;
            p.playStart = t;
            p.reverse = rev;
            p.seed = seed;
            p.repIndex = r + 1;
            p.repCount = reps;
            p.bankSlot = bankSlot;
            p.pan = panFor (rev, r);
            p.speed = speed;
            p.transposeOffset = transposeOffset;

            const int added = addPass (p);
            if (added < 0) { full = true; break; }   // file pleine
            lastAdded = added;
            ++phrases[(size_t) idx].refs;
        }
        if (full) break;
        t += len + gap;
    }

    if (lastAdded >= 0)
    {
        passes[(size_t) lastAdded].endsStep = true;
        keepLastPhrase (idx, bankSlot);
    }

    return lastAdded >= 0;
}

int PhraseEngine::nextBankSlot (const Settings& s)
{
    const int n = juce::jlimit (1, maxBankSize, s.bankSize);

    if (s.bankOrder == BankRandom)
    {
        int candidates[maxBankSize], count = 0;
        for (int k = 0; k < n; ++k)
            if (bank[(size_t) k] >= 0 && ! (k == lastBankSlot && n > 1))
                candidates[count++] = k;

        if (count == 0)
            for (int k = 0; k < n; ++k)
                if (bank[(size_t) k] >= 0) candidates[count++] = k;

        sequencePosView.store (-1);
        return count > 0 ? candidates[rng.nextInt (count)] : -1;
    }

    const int len = sequenceLength.load();
    for (int t = 0; t < len; ++t)
    {
        const int pos = seqPos % len;
        const int v = sequence[(size_t) pos].load();
        seqPos = (pos + 1) % len;

        if (v >= 0 && v < n && bank[(size_t) v] >= 0)
        {
            sequencePosView.store (pos);
            return v;
        }
    }

    sequencePosView.store (-1);
    for (int t = 1; t <= n; ++t)
    {
        const int k = (lastBankSlot + t + n) % n;
        if (bank[(size_t) k] >= 0) return k;
    }
    return -1;
}

bool PhraseEngine::playNextBankStep (juce::int64 at, const Settings& s)
{
    const int k = nextBankSlot (s);
    if (k < 0) return false;

    lastBankSlot = k;
    return schedulePasses (bank[(size_t) k], at, s, k);
}

int PhraseEngine::addPass (const Pass& p)
{
    for (int i = 0; i < maxPasses; ++i)
        if (passes[(size_t) i].state == PassUnused)
        {
            passes[(size_t) i] = p;
            return i;
        }
    return -1;
}

void PhraseEngine::releasePass (Pass& p)
{
    if (p.slot >= 0)
    {
        auto& slot = slots[(size_t) p.slot];
        slot.owner = -1;
        int expected = SlotReady;
        slot.state.compare_exchange_strong (expected, SlotFree);
        expected = SlotRequested;
        slot.state.compare_exchange_strong (expected, SlotFree);
        if (displaySlot.load() == p.slot) displaySlot.store (-1);
    }

    unref (p.phrase);

    if (p.endsStep)
        stepDone = true;

    p = Pass {};
}

void PhraseEngine::resetFlow (const Settings& s)
{
    stepDone = false;
    skipSilence = false;

    if (s.captureMode == Cycle || s.captureMode == Bank)
    {
        flow = FlowSilence;
        resumeAt = now;
        if (capState == CapArmed) capState = CapIdle;
    }
    else
    {
        flow = FlowNone;
        if (capState != CapRecording)
        {
            if (s.captureMode == Threshold) armCapture (PurposeFree, -1);
            else                            capState = CapIdle;
        }
    }
}

void PhraseEngine::handleNextRequest (const Settings& s)
{
    if (flow == FlowSilence)
    {
        resumeAt = now;
        return;
    }

    if (flow == FlowCapture)
    {
        if (capState == CapArmed) startCapture (now, s, capPurpose, capBankSlot);
        return;
    }

    if (flow != FlowPlaying) return;

    // annule ce qui n'a pas commencé, laisse finir ce qui joue, puis enchaîne sans silence
    int lastPlaying = -1;
    juce::int64 lastEnd = -1;

    for (int i = 0; i < maxPasses; ++i)
    {
        auto& p = passes[(size_t) i];
        if (p.state == PassPlaying)
        {
            const juce::int64 e = p.playStart + p.length;
            if (e > lastEnd) { lastEnd = e; lastPlaying = i; }
        }
    }

    for (auto& p : passes)
        if (p.state == PassWaiting || p.state == PassRequested)
            releasePass (p);

    stepDone = false;
    skipSilence = true;

    if (lastPlaying >= 0) passes[(size_t) lastPlaying].endsStep = true;
    else                  stepDone = true;
}

//==============================================================================
void PhraseEngine::process (const juce::AudioBuffer<float>& input, int startSample, int n,
                            juce::AudioBuffer<float>& wetOut, juce::AudioBuffer<float>& sendOut, const Settings& s)
{
    for (int ch = 0; ch < wetOut.getNumChannels(); ++ch)  wetOut.clear (ch, 0, n);
    for (int ch = 0; ch < sendOut.getNumChannels(); ++ch) sendOut.clear (ch, 0, n);

    if (! prepared.load() || n <= 0)
        return;

    const bool flowMode = (s.captureMode == Cycle || s.captureMode == Bank);
    const juce::int64 calcMargin = (juce::int64) (calcMarginSeconds * sr);
    const juce::int64 blockEnd = now + n;

    anSensitivity.store (s.sensitivity);
    anLegato.store (s.legato);
    anPlayMode.store (s.playMode);

    if (s.gridSamples > 1.0)
        gridOrigin = now + (juce::int64) std::llround (s.samplesToNextGrid);

    // --- vider tout (relectures + banque) ---------------------------------------
    if (clearRequest.exchange (false))
    {
        for (auto& p : passes)
            if (p.state != PassUnused) releasePass (p);

        for (int k = 0; k < maxBankSize; ++k)
            if (! isSlotLocked (k)) setBankPhrase (k, -1);
        keepLastPhrase (-1, -1);

        capState = CapIdle;
        seqPos = 0;
        lastBankSlot = -1;
        bankPlayingSlot.store (-1);
        displaySlot.store (-1);
        resetFlow (s);
    }

    applyImports();

    // --- ramasse-miettes des slots orphelins ------------------------------------
    for (auto& slot : slots)
    {
        if (slot.owner != -1) continue;
        int expected = SlotReady;
        slot.state.compare_exchange_strong (expected, SlotFree);
        expected = SlotRequested;
        slot.state.compare_exchange_strong (expected, SlotFree);
    }

    // --- écriture dans l'historique ----------------------------------------------
    const int numIn = input.getNumChannels();
    float peak = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const int pos = (int) (now % historyLength);
        const int first = juce::jmin (n, historyLength - pos);

        if (numIn > 0)
        {
            const float* src = input.getReadPointer (juce::jmin (ch, numIn - 1), startSample);
            history.copyFrom (ch, pos, src, first);
            if (n > first) history.copyFrom (ch, 0, src + first, n - first);
            peak = juce::jmax (peak, input.getMagnitude (juce::jmin (ch, numIn - 1), startSample, n));
        }
        else
        {
            history.clear (ch, pos, first);
            if (n > first) history.clear (ch, 0, n - first);
        }
    }
    inputPeak.store (juce::jmax (peak, inputPeak.load() * 0.92f));

    // --- changement de mode ----------------------------------------------------------
    if (s.captureMode != lastMode)
    {
        lastMode = s.captureMode;
        resetFlow (s);
    }

    if (! flowMode) stepDone = false;

    // --- fin de capture ------------------------------------------------------------
    juce::int64 scanFrom = now;

    if (capState == CapRecording && capStart + capLength <= blockEnd)
    {
        scanFrom = capStart + capLength;
        finishCapture (s);
    }

    // --- pas suivant ------------------------------------------------------------------
    if (nextRequest.exchange (false) && flowMode)
        handleNextRequest (s);

    // --- déroulement Cycle / Banque ---------------------------------------------------
    if (flowMode)
    {
        if (flow == FlowPlaying && stepDone)
        {
            stepDone = false;
            const double silence = skipSilence ? 0.0 : randomSilence (s);
            skipSilence = false;

            if (s.captureMode == Bank && bankFull (s))
            {
                const juce::int64 at = now + juce::jmax (calcMargin, (juce::int64) std::round (silence * sr));
                resumeAt = at;
                if (! playNextBankStep (at, s)) { flow = FlowSilence; resumeAt = now; }
            }
            else
            {
                flow = FlowSilence;
                resumeAt = now + (juce::int64) std::round (silence * sr);
                flowSilenceTotal.store ((float) silence);
            }
        }

        if (flow == FlowSilence && now >= resumeAt && capState == CapIdle && ! s.hold)
        {
            if (s.captureMode == Bank && bankFull (s))
            {
                flow = FlowPlaying;
                if (! playNextBankStep (now + calcMargin, s)) stepDone = true;
            }
            else
            {
                beginFlowCapture (s, s.recapture == RecaptureImmediate);
            }
        }

        if (flow == FlowNone)
            resetFlow (s);
    }

    // --- capture à la demande -----------------------------------------------------------
    if (captureRequest.exchange (false))
    {
        if (s.captureMode == Bank)
        {
            const int k = replaceTarget (s);
            if (capState == CapIdle && k >= 0)
            {
                if (s.recapture == RecaptureImmediate) startCapture (now, s, PurposeBankReplace, k);
                else                                   armCapture (PurposeBankReplace, k);
            }
        }
        else if (s.captureMode != Cycle && capState != CapRecording)
        {
            startCapture (now, s, PurposeFree, -1);
        }
    }

    if (s.captureMode == Continuous && capState != CapRecording && ! s.hold)
        startCapture (now, s, PurposeFree, -1);

    // --- déclenchement au seuil -------------------------------------------------------
    const bool armBlocked = s.hold && capPurpose != PurposeBankReplace;
    if (capState == CapArmed && numIn > 0 && ! armBlocked)
    {
        const int from = (int) juce::jlimit ((juce::int64) 0, (juce::int64) n, scanFrom - now);
        const int preRoll = (int) (0.03 * sr);

        for (int i = from; i < n && capState == CapArmed; ++i)
            for (int ch = 0; ch < numIn; ++ch)
                if (std::abs (input.getSample (ch, startSample + i)) > s.thresholdGain)
                {
                    startCapture (now + i - preRoll, s, capPurpose, capBankSlot);
                    break;
                }
    }

    // --- Hold pendant un silence : la dernière phrase repart en boucle ---------------------
    if (s.hold && heldPhrase >= 0 && capState != CapRecording)
    {
        bool anyPass = false;
        for (auto& p : passes) if (p.state != PassUnused) { anyPass = true; break; }

        if (! anyPass && phrases[(size_t) heldPhrase].state.load() != PhraseFree)
        {
            if (schedulePasses (heldPhrase, now + calcMargin, s, heldBankSlot) && flowMode)
            {
                flow = FlowPlaying;
                stepDone = false;
                if (capState == CapArmed) capState = CapIdle;
            }
        }
    }

    // --- relectures --------------------------------------------------------------
    // le calcul d'une relecture est demandé jusqu'à 1 s avant son heure (au moins la marge de calcul)
    const juce::int64 lookahead = (juce::int64) (juce::jmax (calcMarginSeconds - 0.05, 1.0) * sr);
    const juce::int64 holdGap = (juce::int64) std::round (juce::jlimit (0.0, maxRepeatGapSeconds, s.repeatGap) * sr);

    for (int i = 0; i < maxPasses; ++i)
    {
        auto& p = passes[(size_t) i];
        if (p.state == PassUnused) continue;

        if (p.state == PassWaiting && blockEnd + lookahead >= p.playStart)
        {
            for (int k = 0; k < maxSlots; ++k)
            {
                auto& slot = slots[(size_t) k];
                if (slot.owner != -1 || slot.state.load() != SlotFree) continue;

                slot.owner = i;
                slot.phrase = p.phrase;
                slot.length = phrases[(size_t) p.phrase].length;

                pal::RenderParams rp;
                rp.reverse = p.reverse;
                rp.seed = p.seed;
                rp.sampleRate = sr;
                rp.randomAmount = s.randomAmount;
                rp.sensitivity = s.sensitivity;
                rp.legato = s.legato;
                rp.playMode = s.playMode;
                rp.fadeMs = s.fadeMs;
                rp.keyRoot = s.keyRoot;
                rp.scale = (pal::Scale) juce::jlimit (0, (int) pal::Scale::NumScales - 1, s.scale);
                rp.correction = s.correction;
                rp.noteReverseProb = s.noteReverseProb;
                rp.skipProb = s.skipProb;
                rp.repeatProb = s.repeatProb;
                rp.octaveProb = s.octaveProb;
                rp.melodicJitter = s.melodicJitter;

                const int r = juce::jmax (0, p.repIndex - 1);
                rp.transpose = juce::jlimit (-48, 48, s.transpose + p.transposeOffset + s.driftPerRep * r);
                rp.gain = juce::Decibels::decibelsToGain (s.decayDb * (float) r, -100.0f);
                rp.highpassHz = s.lowCutHz;
                rp.lowpassHz = juce::jmax (200.0f, s.toneHz * std::pow (1.0f - 0.5f * s.damping, (float) r));
                rp.attackMs = s.attackMs;
                rp.attackPerNote = s.attackPerNote;
                rp.attackDirection = s.attackDirection;
                rp.attackPattern = s.attackPattern;
                rp.attackProb = s.attackProb;
                rp.repIndex = r;
                rp.beatSamples = s.beatSamples;
                rp.quantGridBeats = s.quantGridBeats;
                rp.quantAmount = s.quantAmount;
                rp.humanizeMs = s.humanizeMs;
                rp.speed = p.speed;
                rp.textureMix = s.textureMix;
                rp.grainMs = s.grainMs;
                rp.grainDensity = s.grainDensity;
                rp.grainSpray = s.grainSpray;
                slot.params = rp;

                slot.state.store (SlotRequested);
                p.slot = k;
                p.state = PassRequested;
                notify();

                // Hold : la phrase repart en boucle juste après cette répétition
                if (s.hold && p.endsStep)
                {
                    p.endsStep = false;
                    schedulePasses (p.phrase, p.playStart + p.length + holdGap, s, p.bankSlot);
                }
                break;
            }
        }

        if (p.state == PassRequested && p.playStart < blockEnd
            && slots[(size_t) p.slot].state.load() == SlotReady)
        {
            if (p.playStart < now) p.playStart = now;
            p.state = PassPlaying;
            displaySlot.store (p.slot);
            if (p.bankSlot >= 0) bankPlayingSlot.store (p.bankSlot);
        }

        if (p.state == PassPlaying)
        {
            const auto& buf = slots[(size_t) p.slot].buffer;
            const int offset = (int) juce::jmax ((juce::int64) 0, p.playStart - now);
            const int readPos = (int) (now + offset - p.playStart);
            const int count = juce::jmin (n - offset, p.length - readPos, buf.getNumSamples() - readPos);

            if (count > 0)
            {
                const float pan = juce::jlimit (-1.0f, 1.0f, p.pan * s.width);
                const float send = s.diffusion * (s.diffusionGrow ? (float) p.repIndex / (float) juce::jmax (1, p.repCount) : 1.0f);

                auto mixInto = [&] (juce::AudioBuffer<float>& out, float gain)
                {
                    if (std::abs (pan) < 1.0e-3f || out.getNumChannels() < 2)
                    {
                        for (int ch = 0; ch < out.getNumChannels(); ++ch)
                            out.addFrom (ch, offset, buf, juce::jmin (ch, 1), readPos, count, gain);
                        return;
                    }

                    // panoramique : on glisse du stéréo d'origine vers un mono placé (puissance constante)
                    const float amt = std::abs (pan);
                    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
                    const float gL = juce::MathConstants<float>::sqrt2 * std::cos (angle);
                    const float gR = juce::MathConstants<float>::sqrt2 * std::sin (angle);
                    const float* inL = buf.getReadPointer (0, readPos);
                    const float* inR = buf.getReadPointer (1, readPos);
                    float* outL = out.getWritePointer (0, offset);
                    float* outR = out.getWritePointer (1, offset);

                    for (int j = 0; j < count; ++j)
                    {
                        const float mid = 0.5f * (inL[j] + inR[j]);
                        outL[j] += gain * ((1.0f - amt) * inL[j] + amt * mid * gL);
                        outR[j] += gain * ((1.0f - amt) * inR[j] + amt * mid * gR);
                    }
                };

                mixInto (wetOut, 1.0f);
                if (send > 1.0e-3f) mixInto (sendOut, send);
            }

            if (displaySlot.load() == p.slot)
                displayProgress.store ((float) (readPos + juce::jmax (0, count)) / (float) p.length);

            if (readPos + juce::jmax (0, count) >= p.length || count <= 0)
                releasePass (p);
        }
    }

    now = blockEnd;
    holding.store (s.hold);
    updateViews (s);
}

void PhraseEngine::updateViews (const Settings& s)
{
    std::array<int, numViews> best;
    best.fill (-1);
    int pending = 0;

    for (int i = 0; i < maxPasses; ++i)
    {
        if (passes[(size_t) i].state == PassUnused) continue;
        ++pending;

        int cand = i;
        for (auto& b : best)
        {
            if (b < 0) { b = cand; break; }
            if (passes[(size_t) cand].playStart < passes[(size_t) b].playStart) std::swap (b, cand);
        }
    }

    for (int v = 0; v < numViews; ++v)
    {
        auto& view = passViews[(size_t) v];
        if (best[(size_t) v] < 0) { view.active.store (0); continue; }

        const auto& p = passes[(size_t) best[(size_t) v]];
        view.reverse.store (p.reverse ? 1 : 0);
        view.repIndex.store (p.repIndex);
        view.repCount.store (p.repCount);
        view.bankSlot.store (p.bankSlot);
        view.startIn.store ((float) ((double) (p.playStart - now) / sr));
        view.length.store ((float) (p.length / sr));
        view.pan.store (juce::jlimit (-1.0f, 1.0f, p.pan * s.width));
        view.levelDb.store (s.decayDb * (float) juce::jmax (0, p.repIndex - 1));
        view.speed.store (p.speed);
        view.transpose.store (s.transpose + p.transposeOffset + s.driftPerRep * juce::jmax (0, p.repIndex - 1));
        view.active.store (1);
    }

    for (int k = 0; k < maxBankSize; ++k)
        bankLengthView[(size_t) k].store (bank[(size_t) k] >= 0 ? (float) (phrases[(size_t) bank[(size_t) k]].length / sr) : 0.0f);

    pendingCount.store (pending);
    captureState.store (capState);
    flowState.store (flow);
    flowRemaining.store (flow == FlowSilence ? (float) juce::jmax (0.0, (double) (resumeAt - now) / sr) : 0.0f);
    captureTarget.store (capState != CapIdle && capPurpose >= PurposeBankFill ? capBankSlot : -1);
    captureIsReplace.store (capPurpose == PurposeBankReplace);
    captureLength.store (capState == CapRecording ? (float) (capLength / sr) : 0.0f);
    captureElapsed.store (capState == CapRecording ? (float) juce::jmax (0.0, (double) (now - capStart) / sr) : 0.0f);
}

bool PhraseEngine::getSlotInfo (int slot, pal::RenderInfo& out)
{
    if (slot < 0 || slot >= maxSlots) return false;
    auto& s = slots[(size_t) slot];
    const juce::SpinLock::ScopedLockType l (s.infoLock);
    out = s.info;
    return true;
}

//==============================================================================
// Thread de travail
void PhraseEngine::storePhrase (Phrase& ph)
{
    const juce::ScopedLock sl (rawLock);
    ph.generation.fetch_add (1);
    const int len = ph.length;
    ph.raw.setSize (2, len, false, false, true);

    const int pos = (int) (ph.captureStart % historyLength);
    const int first = juce::jmin (len, historyLength - pos);

    for (int ch = 0; ch < 2; ++ch)
    {
        ph.raw.copyFrom (ch, 0, history, ch, pos, first);
        if (len > first) ph.raw.copyFrom (ch, first, history, ch, 0, len - first);
    }
}

void PhraseEngine::run()
{
    while (! threadShouldExit())
    {
        bool didWork = false;

        for (auto& ph : phrases)
        {
            const int st = ph.state.load();
            if (st == PhraseStoreRequested)
            {
                storePhrase (ph);
                int expected = PhraseStoreRequested;
                ph.state.compare_exchange_strong (expected, PhraseStored);

                // analyse tout de suite : la première relecture n'aura plus qu'à être rendue
                pal::analysePhrase (ph.raw, ph.length, sr, anSensitivity.load(), anLegato.load(), anPlayMode.load(),
                                    ph.analysis, ph.generation.load(), true);
                latestPhrase = (int) (&ph - phrases.data());
                publishAnalysis (ph);
                didWork = true;
            }
            else if (st == PhraseDead)
            {
                {
                    const juce::ScopedLock sl (rawLock);
                    ph.raw.setSize (0, 0);
                }
                ph.analysis.invalidate();
                ph.state.store (PhraseFree);
                didWork = true;
            }
        }

        for (auto& slot : slots)
        {
            int expected = SlotRequested;
            if (! slot.state.compare_exchange_strong (expected, SlotRendering))
                continue;

            pal::RenderInfo info;
            const int len = slot.length;
            const int expectedOut = juce::jmax (1, (int) std::round (len / juce::jmax (0.05f, slot.params.speed)));
            bool rendered = false;

            if (slot.phrase >= 0 && slot.phrase < maxPhrases)
            {
                auto& ph = phrases[(size_t) slot.phrase];
                if (ph.state.load() == PhraseStoreRequested)
                {
                    storePhrase (ph);
                    int e = PhraseStoreRequested;
                    ph.state.compare_exchange_strong (e, PhraseStored);
                }

                if (ph.raw.getNumSamples() >= len && len > 0)
                {
                    slot.outLength = pal::renderPhrase (ph.raw, len, slot.params, slot.buffer, &info,
                                                        &ph.analysis, ph.generation.load());
                    rendered = true;
                }
            }

            if (! rendered)
            {
                if (slot.buffer.getNumSamples() < expectedOut)
                    slot.buffer.setSize (2, expectedOut, false, false, true);
                slot.buffer.clear();
                slot.outLength = expectedOut;
            }

            {
                const juce::SpinLock::ScopedLockType l (slot.infoLock);
                slot.info = std::move (info);
            }

            slot.state.store (SlotReady);
            didWork = true;
        }

        if (! didWork)
        {
            refreshLatestAnalysis();
            wait (20);
        }
    }
}

// la dernière capture est réanalysée quand les réglages de découpage changent (après 150 ms de stabilité)
void PhraseEngine::refreshLatestAnalysis()
{
    const float sens = anSensitivity.load(), leg = anLegato.load();
    const int mode = anPlayMode.load();
    const auto t = juce::Time::getMillisecondCounter();

    if (std::abs (sens - seenSens) > 1.0e-5f || std::abs (leg - seenLegato) > 1.0e-5f || mode != seenMode)
    {
        seenSens = sens; seenLegato = leg; seenMode = mode;
        settingsChangedAt = t;
        return;
    }
    if (t - settingsChangedAt < 150 || latestPhrase < 0) return;

    auto& ph = phrases[(size_t) latestPhrase];
    if (ph.state.load() != PhraseStored || ph.raw.getNumSamples() < ph.length) return;
    if (ph.analysis.matches (ph.generation.load(), ph.length, sr, sens, leg, mode)) return;

    pal::analysePhrase (ph.raw, ph.length, sr, sens, leg, mode, ph.analysis, ph.generation.load(), true);
    publishAnalysis (ph);
}

void PhraseEngine::publishAnalysis (const Phrase& ph)
{
    AnalysisSnapshot snap;
    const int n = ph.length;
    const auto& mono = ph.analysis.mono;
    if (n <= 0 || (int) mono.size() < n) return;

    constexpr int cols = 400;
    snap.peaks.assign (cols, 0.0f);
    float mx = 1.0e-6f;
    for (int c = 0; c < cols; ++c)
    {
        const int s0 = (int) ((juce::int64) c * n / cols), s1 = juce::jmax (s0 + 1, (int) ((juce::int64) (c + 1) * n / cols));
        float m = 0.0f;
        for (int i = s0; i < s1; ++i) m = juce::jmax (m, std::abs (mono[(size_t) i]));
        snap.peaks[(size_t) c] = m;
        mx = juce::jmax (mx, m);
    }
    for (auto& v : snap.peaks) v /= mx;

    for (const auto& note : ph.analysis.notes)
        snap.notes.push_back ({ (float) note.start / (float) n, (float) note.length / (float) n, note.midi, note.poly, note.legatoCut });
    snap.seconds = (float) (n / sr);

    const juce::SpinLock::ScopedLockType l (analysisLock);
    snap.version = latestAnalysis.version + 1;
    latestAnalysis = std::move (snap);
}

bool PhraseEngine::getLatestAnalysis (AnalysisSnapshot& out, int knownVersion)
{
    const juce::SpinLock::ScopedLockType l (analysisLock);
    if (latestAnalysis.version == knownVersion) return false;
    out = latestAnalysis;
    return true;
}
