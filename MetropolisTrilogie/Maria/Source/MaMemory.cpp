#include "MaMemory.h"

namespace ma
{
using alea::hUni;

juce::String chordName (int ch)
{
    if (ch < 0) return {};
    static const char* n[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    const int r = ch % 12;
    return juce::String (n[r]) + (ch < 12 ? "" : (ch < 24 ? "m" : " (note)"));
}

static inline float onePole (float hz, double fs) noexcept { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) fs); }

// ===========================================================================
void OnsetDetector::setup (const SourceProfile& p, float sens, double fs)
{
    cHp = onePole (p.lo, fs);
    cLp = onePole (juce::jmin (p.hi, (float) fs * 0.45f), fs);
    fAtk = std::exp (-1.0f / (0.0002f * (float) fs)); fRel = std::exp (-1.0f / (0.010f * (float) fs));
    sAtk = std::exp (-1.0f / (0.030f * (float) fs));  sRel = std::exp (-1.0f / (juce::jlimit (0.06f, 0.2f, p.refracMs * 0.0025f) * (float) fs));
    ratio = juce::Decibels::decibelsToGain (9.0f - sens * 6.5f);
    floorLin = juce::Decibels::decibelsToGain (-28.0f - sens * 34.0f);
    refracS = (int) (p.refracMs * 0.001f * (float) fs);
    useHigh = p.hi > 4000.0f && p.lo < 1500.0f;
    cHp2 = onePole (2500.0f, fs);
}

float OnsetDetector::process (float x) noexcept
{
    lp += cLp * (x - lp);
    hp += cHp * (lp - hp);
    const float a = std::abs (lp - hp);
    fast = a > fast ? a + (fast - a) * fAtk : fast * fRel;
    slow = fast > slow ? fast + (slow - fast) * sAtk : slow * sRel + fast * (1.0f - sRel);
    float a2 = 0.0f;
    if (useHigh)
    {
        hp2 += cHp2 * (x - hp2);
        a2 = std::abs (x - hp2) * 2.0f;
        fast2 = a2 > fast2 ? a2 + (fast2 - a2) * fAtk : fast2 * fRel;
        slow2 = fast2 > slow2 ? fast2 + (slow2 - fast2) * sAtk : slow2 * sRel + fast2 * (1.0f - sRel);
    }
    if (refrac > 0) --refrac;
    if (! armed && fast < slow * 1.1f) armed = true;
    if (! armed2 && fast2 < slow2 * 1.1f) armed2 = true;
    const bool hitLow = armed && fast > floorLin && fast > slow * ratio;
    const bool hitHigh = useHigh && armed2 && fast2 > floorLin && fast2 > slow2 * ratio;
    if (refrac == 0 && (hitLow || hitHigh))
    {
        refrac = refracS; armed = armed2 = false;
        const float db = juce::Decibels::gainToDecibels (juce::jmax (fast, fast2 * 0.5f) * 2.0f + 1.0e-6f);
        return juce::jlimit (0.08f, 1.0f, (db + 48.0f) / 44.0f);
    }
    return 0.0f;
}

// ===========================================================================
void Memory::prepare (double sampleRate, double seconds, bool withoutAudio)
{
    fs = sampleRate;
    cap = (int64_t) (fs * seconds);
    noAudio = withoutAudio;
    buf.assign (noAudio ? (size_t) 1 : (size_t) cap, 0.0f);
    ovSize = cap / kOvBlock + 2;
    ov.reset (new std::atomic<float>[(size_t) ovSize]);
    for (int64_t i = 0; i < ovSize; ++i) ov[(size_t) i].store (0.0f);
    ovPeak = 0; ovBlock = 0;
    wc.store (0);
    fifo.reset();
    pending.clear(); pending.reserve (256);
    const int hop = (int) std::round (fs * 0.0213);
    frame.assign ((size_t) (hop * 2 + 256), 0.0f);
    dec.assign (frame.size() / 2, 0.0f);
    yd.assign (2048, 0.0f);
    pitches.reserve (512);
    fft = std::make_unique<juce::dsp::FFT> (12);
    fftBuf.assign (8192, 0.0f);
    hann.assign (4096, 0.0f);
    for (int i = 0; i < 4096; ++i) hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 4095.0f);
    clearPos.store (0);
    doClear();
}

void Memory::write (const float* x, int n) noexcept
{
    if (noAudio) { advance (n); return; }
    const int64_t w = wc.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const int64_t p = w + i;
        buf[(size_t) (p % cap)] = x[i];
        ovPeak = juce::jmax (ovPeak, std::abs (x[i]));
        if ((p + 1) % kOvBlock == 0)
        {
            ov[(size_t) ((p / kOvBlock) % ovSize)].store (ovPeak, std::memory_order_relaxed);
            ovPeak = 0.0f;
        }
    }
    wc.store (w + n, std::memory_order_release);
}

void Memory::pushEvent (int64_t pos, int kind) noexcept
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) { evPos[(size_t) s1] = pos; evKind[(size_t) s1] = kind; }
    else if (n2 > 0) { evPos[(size_t) s2] = pos; evKind[(size_t) s2] = kind; }
    fifo.finishedWrite (n1 + n2);
}

int64_t Memory::oldestValidPos() const noexcept
{
    return juce::jmax (writePos() - cap + (int64_t) fs, clearPos.load());
}

bool Memory::stateValid (int s) const noexcept
{
    return s >= 1 && s < numStates() && segs[(size_t) (s - 1)].start >= oldestValidPos();
}

int Memory::stateAtOrAfter (int64_t pos) const noexcept
{
    const int n = numStates();
    int lo = juce::jmax (1, firstValidState()), hi = n;
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (segs[(size_t) (mid - 1)].start >= pos) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int Memory::firstValidState() const noexcept
{
    const int n = numStates();
    const int64_t old = oldestValidPos();
    int lo = 1, hi = n;   // premier etat valide dans [1, n)
    while (lo < hi)
    {
        const int mid = (lo + hi) / 2;
        if (segs[(size_t) (mid - 1)].start >= old) hi = mid; else lo = mid + 1;
    }
    return lo;
}

void Memory::doClear()
{
    sfxA[0] = -1; lrsA[0] = 0; symA[0] = -1;
    trn[0].clear();
    nStates.store (1, std::memory_order_release);
    clearPos.store (writePos());
    clearCount.fetch_add (1);
    apos = writePos();
    inSeg = false;
    pending.clear();
    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    fifo.finishedRead (n1 + n2);
}

// ---------------------------------------------------------------------------
float Memory::yin (const float* x, int n, float sr, float fmin, float fmax, float& conf)
{
    const int W = 512;
    const int tauMin = juce::jmax (2, (int) (sr / fmax));
    const int tauMax = juce::jmin ((int) (sr / juce::jmax (20.0f, fmin)) + 1, n - W - 2, (int) yd.size() - 2);
    conf = 0.0f;
    if (tauMax <= tauMin + 2) return -1.0f;
    yd[0] = 1.0f;
    double cum = 0.0;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        double d = 0.0;
        for (int j = 0; j < W; ++j) { const float e = x[j] - x[j + tau]; d += (double) e * e; }
        cum += d;
        yd[(size_t) tau] = cum > 0.0 ? (float) (d * tau / cum) : 1.0f;
    }
    int best = -1;
    for (int tau = tauMin; tau < tauMax; ++tau)
        if (yd[(size_t) tau] < 0.15f)
        {
            while (tau + 1 < tauMax && yd[(size_t) (tau + 1)] < yd[(size_t) tau]) ++tau;
            best = tau; break;
        }
    if (best < 0)
    {
        float m = 1e9f;
        for (int tau = tauMin; tau < tauMax; ++tau) if (yd[(size_t) tau] < m) { m = yd[(size_t) tau]; best = tau; }
    }
    if (best < 1) return -1.0f;
    conf = 1.0f - yd[(size_t) best];
    float shift = 0.0f;
    if (best > 1 && best < tauMax)
    {
        const float a = yd[(size_t) (best - 1)], b = yd[(size_t) best], c = yd[(size_t) (best + 1)];
        const float den = a - 2.0f * b + c;
        if (std::abs (den) > 1.0e-9f) shift = juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
    }
    return sr / ((float) best + shift);
}

void Memory::addState (const Segment& g)
{
    const int n = nStates.load (std::memory_order_relaxed);
    if (n >= kMaxStates) return;
    segs[(size_t) (n - 1)] = g;
    const int sym = g.sym;
    symA[(size_t) n] = sym;
    trn[(size_t) n].clear();
    auto find = [this] (int k, int s)
    {
        for (const auto& t : trn[(size_t) k]) if (t.first == s) return t.second;
        return -1;
    };
    trn[(size_t) (n - 1)].push_back ({ sym, n });
    int k = sfxA[(size_t) (n - 1)];
    while (k > -1 && find (k, sym) < 0) { trn[(size_t) k].push_back ({ sym, n }); k = sfxA[(size_t) k]; }
    sfxA[(size_t) n] = k == -1 ? 0 : find (k, sym);
    int L = 0;
    const int b = sfxA[(size_t) n];
    if (b > 0)
        while (L < 64 && n - L >= 1 && b - L >= 1 && symA[(size_t) (n - L)] == symA[(size_t) (b - L)]) ++L;
    lrsA[(size_t) n] = L;
    nStates.store (n + 1, std::memory_order_release);
}

void Memory::addChroma (const SourceProfile& p)
{
    if (fft == nullptr) return;
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    for (int i = 0; i < 4096; ++i) fftBuf[(size_t) i] = sampleAt (apos + i) * hann[(size_t) i];
    fft->performFrequencyOnlyForwardTransform (fftBuf.data());
    double ch[12] {}, tot = 0;
    const double binHz = fs / 4096.0;
    const int k0 = juce::jmax (2, (int) (juce::jmax (55.0f, p.fmin * 0.9f) / binHz)), k1 = juce::jmin (2040, (int) (4200.0 / binHz));
    float mx = 0;
    for (int k = k0; k < k1; ++k) mx = juce::jmax (mx, fftBuf[(size_t) k]);
    // pics spectraux, frequence affinee par interpolation parabolique
    for (int k = k0; k < k1; ++k)
    {
        const float b = fftBuf[(size_t) k];
        if (b < mx * 0.04f || b <= fftBuf[(size_t) (k - 1)] || b < fftBuf[(size_t) (k + 1)]) continue;
        const double la = std::log (fftBuf[(size_t) (k - 1)] + 1e-9), lb = std::log (b + 1e-9), lc = std::log (fftBuf[(size_t) (k + 1)] + 1e-9);
        const double den = la - 2.0 * lb + lc;
        const double d = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (la - lc) / den) : 0.0;
        const double f = ((double) k + d) * binHz;
        const int pc = ((int) std::round (69.0 + 12.0 * std::log2 (f / 440.0)) % 12 + 12) % 12;
        ch[pc] += b; tot += b;
    }
    if (tot <= 1.0e-9) return;
    for (int i = 0; i < 12; ++i) chromaSum[i] += ch[i] / tot;
    ++chromaFrames;
}

int Memory::matchChord() const
{
    if (chromaFrames <= 0) return -1;
    double c[12], norm = 0;
    for (int i = 0; i < 12; ++i) { c[i] = chromaSum[i] / chromaFrames; norm += c[i] * c[i]; }
    norm = std::sqrt (norm);
    if (norm <= 1.0e-9) return -1;
    double best = 0; int bi = -1;
    for (int r = 0; r < 12; ++r)
    {
        const double maj = (c[r] + c[(r + 4) % 12] + c[(r + 7) % 12]) / (std::sqrt (3.0) * norm);
        const double mnr = (c[r] + c[(r + 3) % 12] + c[(r + 7) % 12]) / (std::sqrt (3.0) * norm);
        const double one = c[r] / norm * 0.93;
        if (maj > best) { best = maj; bi = r; }
        if (mnr > best) { best = mnr; bi = 12 + r; }
        if (one > best) { best = one; bi = 24 + r; }
    }
    return best >= 0.72 ? bi : -1;
}

void Memory::closeSegment (int64_t endPos, const SourceProfile& p)
{
    const int64_t minS = (int64_t) (p.minSegMs * 0.001 * fs);
    if (inSeg && endPos - segStart >= minS && frames > 0)
    {
        Segment g;
        g.start = segStart; g.end = endPos;
        g.rmsDb = (float) (rmsSum / frames);
        g.bright = (float) (brightSum / frames);
        const float vr = (float) voiced / (float) frames;
        if (! pitches.empty())
        {
            std::vector<float> tmp (pitches);
            std::nth_element (tmp.begin(), tmp.begin() + (long) tmp.size() / 2, tmp.end());
            g.pitch = tmp[tmp.size() / 2];
        }
        const float d = g.rmsDb - energyAvg;
        energyAvg += 0.05f * (g.rmsDb - energyAvg);
        const int ec = d < -6.0f ? 0 : (d > 4.0f ? 2 : 1);
        const int bc = juce::jlimit (0, 5, (int) g.bright);
        const int timbre = 1000 + bc * 3 + ec;
        const bool usePitch = g.pitch > 0.0f && ((p.symMode == 1 && vr >= 0.5f) || (p.symMode == 2 && vr >= 0.7f));
        g.sym = usePitch ? (int) std::round (g.pitch) : timbre;
        if (! usePitch && p.symMode == 0) g.pitch = -1.0f;
        if (p.symMode == 2)
        {
            // instruments polyphoniques : un accord reconnu prime sur une hauteur unique
            const int ch = matchChord();
            if (ch >= 0 && (ch < 24 || ! usePitch))
            {
                g.chord = ch; g.sym = 2000 + ch;
                g.pitch = 60.0f + (float) (ch % 12);   // fondamentale (pour le Suivi et le MIDI)
                lastChord.store (ch);
            }
            else if (! usePitch) g.pitch = -1.0f;
        }
        addState (g);
    }
    inSeg = false;
}

void Memory::analyzeStep (const SourceProfile& p, float gateDb)
{
    if (! isPrepared()) return;
    if (clearReq.exchange (false)) doClear();

    // evenements de l'audio
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) pending.push_back ({ evPos[(size_t) (s1 + i)], evKind[(size_t) (s1 + i)] });
        for (int i = 0; i < n2; ++i) pending.push_back ({ evPos[(size_t) (s2 + i)], evKind[(size_t) (s2 + i)] });
        fifo.finishedRead (n1 + n2);
    }

    const int hop = (int) std::round (fs * 0.0213);
    const int F = (int) frame.size();
    const bool pitched = p.fmax > 0.0f;
    const int64_t maxS = (int64_t) (p.maxSegMs * 0.001 * fs), minS = (int64_t) (p.minSegMs * 0.001 * fs);

    auto startSeg = [this] (int64_t pos)
    {
        inSeg = true; segStart = pos; pitches.clear(); rmsSum = 0; brightSum = 0; frames = 0; voiced = 0; refPitch = -1; pitchDev = 0;
        for (auto& x : chromaSum) x = 0.0; chromaFrames = 0;
    };

    for (int guard = 0; guard < 20000; ++guard)
    {
        const int64_t w = writePos();
        const int64_t oldest = oldestValidPos();
        if (apos < oldest) { apos = oldest; inSeg = false; }
        if (apos + (p.symMode == 2 ? juce::jmax (F, 4096) : F) > w) break;

        for (int i = 0; i < F; ++i) frame[(size_t) i] = sampleAt (apos + i);
        double e = 0; int zc = 0;
        for (int i = 0; i < hop; ++i)
        {
            e += (double) frame[(size_t) i] * frame[(size_t) i];
            if (i > 0 && (frame[(size_t) i] >= 0.0f) != (frame[(size_t) (i - 1)] >= 0.0f)) ++zc;
        }
        const float rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (e / hop) + 1.0e-7f);
        const float zf = (float) zc / (float) hop * (float) fs * 0.5f;
        const float bright = juce::jlimit (0.0f, 5.99f, std::log2 (juce::jmax (1.0f, zf / 150.0f)));
        const bool silent = rmsDb < gateDb;
        float midi = -1.0f;
        if (pitched && ! silent)
        {
            const int nd = F / 2;
            for (int i = 0; i < nd; ++i) dec[(size_t) i] = 0.5f * (frame[(size_t) (2 * i)] + frame[(size_t) (2 * i + 1)]);
            float conf = 0.0f;
            const float f0 = yin (dec.data(), nd, (float) fs * 0.5f, p.fmin, p.fmax, conf);
            if (f0 > 0.0f && conf > 0.8f && f0 >= p.fmin * 0.95f && f0 <= p.fmax * 1.05f)
                midi = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        }
        lastRmsDb.store (rmsDb);
        if (midi > 0.0f) lastPitch.store (midi);

        // attaques et coupures dans ce pas
        std::sort (pending.begin(), pending.end());
        size_t used = 0;
        for (; used < pending.size() && pending[used].first < apos + hop; ++used)
        {
            const int64_t pos = juce::jmax (apos, pending[used].first);
            const int kind = pending[used].second;
            if (kind == 1)
            {
                if (inSeg) closeSegment (pos, p);
                if (! silent) startSeg (pos);
            }
            else if (inSeg)
            {
                if (pos - segStart >= minS) { closeSegment (pos, p); startSeg (pos); }
            }
            else if (! silent) startSeg (pos);
        }
        pending.erase (pending.begin(), pending.begin() + (long) used);

        if (inSeg)
        {
            if (silent) closeSegment (apos, p);
            else
            {
                if (midi > 0.0f)
                {
                    if (refPitch < 0.0f) refPitch = midi;
                    else if (std::abs (midi - refPitch) >= 0.8f)
                    {
                        if (++pitchDev >= 2 && apos - hop - segStart >= minS)
                        {
                            const int64_t b = apos - hop;
                            closeSegment (b, p); startSeg (b);
                            refPitch = midi;
                        }
                    }
                    else pitchDev = 0;
                }
                if (inSeg && apos + hop - segStart >= maxS) { closeSegment (apos + hop, p); startSeg (apos + hop); }
            }
        }
        else if (! silent) startSeg (apos);

        if (inSeg)
        {
            ++frames; rmsSum += rmsDb; brightSum += bright;
            if (midi > 0.0f) { ++voiced; pitches.push_back (midi); }
            if (p.symMode == 2) addChroma (p);
        }
        apos += hop;
    }
}


// ===========================================================================
// Ecoute en direct
void LiveListener::prepare (double sampleRate)
{
    fs = sampleRate;
    std::fill (ring.begin(), ring.end(), 0.0f);
    wc.store (0); lastAn = 0;
    det.reset(); detSrc = -1;
    densAcc = 0.0f; envDb = -90.0f;
    fft = std::make_unique<juce::dsp::FFT> (12);
    fftBuf.assign (8192, 0.0f);
    hann.assign (kWin, 0.0f);
    for (int i = 0; i < kWin; ++i) hann[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kWin - 1));
    win.assign (kWin, 0.0f);
    dec.assign (kWin / 2, 0.0f);
    yd.assign (1100, 0.0f);
    for (auto& c : chroma) c = 0.0f;
    silentFor = 10.0;
    state.valid.store (false); state.harmValid.store (false);
    state.pitch.store (-1.0f); state.rmsDb.store (-90.0f); state.energy.store (0.0f); state.density.store (0.0f); state.chord.store (-1);
    for (auto& c : state.chroma) c.store (0.0f);
}

void LiveListener::push (const float* x, int n, const SourceProfile& p, float sens) noexcept
{
    if (ring.empty() || n <= 0) return;
    const int srcKey = (int) (p.lo * 7.0f + p.hi) + (int) (sens * 1000.0f);
    if (srcKey != detSrc) { det.setup (p, sens, fs); detSrc = srcKey; }
    const int64_t w = wc.load (std::memory_order_relaxed);
    double e = 0.0; int hits = 0;
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) ((w + i) % kRing)] = x[i];
        e += (double) x[i] * x[i];
        if (det.process (x[i]) > 0.0f) ++hits;
    }
    wc.store (w + n, std::memory_order_release);
    // densite : accumulateur a fuite (constante 2 s) -> attaques par seconde
    const float tau = 2.0f;
    densAcc = densAcc * std::exp (-(float) n / (tau * (float) fs)) + (float) hits;
    state.density.store (densAcc / tau, std::memory_order_relaxed);
    // energie : rms du bloc, montee rapide, descente lente
    const float db = juce::Decibels::gainToDecibels ((float) std::sqrt (e / n) + 1.0e-7f);
    const float c = db > envDb ? 1.0f - std::exp (-(float) n / (0.08f * (float) fs)) : 1.0f - std::exp (-(float) n / (0.9f * (float) fs));
    envDb += (db - envDb) * c;
    state.energy.store (energy01 (envDb), std::memory_order_relaxed);
}

void LiveListener::analyze (const SourceProfile& p, float gateDb)
{
    if (fft == nullptr) return;
    const int64_t w = wc.load (std::memory_order_acquire);
    if (w < kWin || w - lastAn < 1024) return;
    const double dt = (double) (w - lastAn) / fs;
    lastAn = w;
    const int64_t a = w - kWin;
    double e = 0.0;
    for (int i = 0; i < kWin; ++i) { const float v = ring[(size_t) ((a + i) % kRing)]; win[(size_t) i] = v; if (i >= kWin - 1024) e += (double) v * v; }
    const float db = juce::Decibels::gainToDecibels ((float) std::sqrt (e / 1024.0) + 1.0e-7f);
    state.rmsDb.store (db);
    state.valid.store (true);
    if (db < gateDb)
    {
        silentFor += dt;
        if (silentFor > 2.5) state.harmValid.store (false);   // l'harmonie entendue reste valable un moment apres la derniere note
        return;
    }
    silentFor = 0.0;

    // profil harmonique (memes pics spectraux que l'analyse de la memoire)
    std::fill (fftBuf.begin(), fftBuf.end(), 0.0f);
    for (int i = 0; i < kWin; ++i) fftBuf[(size_t) i] = win[(size_t) i] * hann[(size_t) i];
    fft->performFrequencyOnlyForwardTransform (fftBuf.data());
    const double binHz = fs / (double) kWin;
    const int k0 = juce::jmax (2, (int) (juce::jmax (55.0f, p.fmin > 0.0f ? p.fmin * 0.9f : 55.0f) / binHz)), k1 = juce::jmin (2040, (int) (4200.0 / binHz));
    float mx = 0;
    for (int k = k0; k < k1; ++k) mx = juce::jmax (mx, fftBuf[(size_t) k]);
    double ch[12] {}, tot = 0;
    for (int k = k0; k < k1; ++k)
    {
        const float b = fftBuf[(size_t) k];
        if (b < mx * 0.04f || b <= fftBuf[(size_t) (k - 1)] || b < fftBuf[(size_t) (k + 1)]) continue;
        const double la = std::log (fftBuf[(size_t) (k - 1)] + 1e-9), lb = std::log (b + 1e-9), lc = std::log (fftBuf[(size_t) (k + 1)] + 1e-9);
        const double den = la - 2.0 * lb + lc;
        const double d = std::abs (den) > 1e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (la - lc) / den) : 0.0;
        const double f = ((double) k + d) * binHz;
        const int pc = ((int) std::round (69.0 + 12.0 * std::log2 (f / 440.0)) % 12 + 12) % 12;
        ch[pc] += b; tot += b;
    }
    if (tot > 1.0e-9)
    {
        const float sm = (float) (1.0 - std::exp (-dt / 0.35));    // lissage ~ 0,35 s
        float cmax = 0.0f;
        for (int i = 0; i < 12; ++i) { chroma[i] += ((float) (ch[i] / tot) - chroma[i]) * sm; cmax = juce::jmax (cmax, chroma[i]); }
        if (cmax > 1.0e-6f)
        {
            double norm = 0;
            for (int i = 0; i < 12; ++i) { state.chroma[i].store (chroma[i] / cmax); norm += (double) chroma[i] * chroma[i]; }
            norm = std::sqrt (norm);
            double best = 0; int bi = -1;
            for (int r = 0; r < 12; ++r)
            {
                const double maj = (chroma[r] + chroma[(r + 4) % 12] + chroma[(r + 7) % 12]) / (std::sqrt (3.0) * norm);
                const double mnr = (chroma[r] + chroma[(r + 3) % 12] + chroma[(r + 7) % 12]) / (std::sqrt (3.0) * norm);
                const double one = chroma[r] / norm * 0.93;
                if (maj > best) { best = maj; bi = r; }
                if (mnr > best) { best = mnr; bi = 12 + r; }
                if (one > best) { best = one; bi = 24 + r; }
            }
            state.chord.store (best >= 0.7 ? bi : -1);
            state.harmValid.store (true);
        }
    }

    // hauteur (YIN sur le signal decime)
    const float fmin = p.fmax > 0.0f ? p.fmin : 60.0f, fmax = p.fmax > 0.0f ? p.fmax : 1000.0f;
    const int nd = kWin / 2;
    for (int i = 0; i < nd; ++i) dec[(size_t) i] = 0.5f * (win[(size_t) (2 * i)] + win[(size_t) (2 * i + 1)]);
    const float sr = (float) fs * 0.5f;
    const int W = 512;
    const int tauMin = juce::jmax (2, (int) (sr / fmax));
    const int tauMax = juce::jmin ((int) (sr / juce::jmax (20.0f, fmin)) + 1, nd - W - 2, (int) yd.size() - 2);
    if (tauMax > tauMin + 2)
    {
        yd[0] = 1.0f;
        double cum = 0.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            for (int j = 0; j < W; ++j) { const float q = dec[(size_t) j] - dec[(size_t) (j + tau)]; d += (double) q * q; }
            cum += d;
            yd[(size_t) tau] = cum > 0.0 ? (float) (d * tau / cum) : 1.0f;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (yd[(size_t) tau] < 0.15f)
            {
                while (tau + 1 < tauMax && yd[(size_t) (tau + 1)] < yd[(size_t) tau]) ++tau;
                best = tau; break;
            }
        if (best > 1 && yd[(size_t) best] < 0.2f)
        {
            const float f0 = sr / (float) best;
            if (f0 >= fmin * 0.95f && f0 <= fmax * 1.05f) state.pitch.store (69.0f + 12.0f * std::log2 (f0 / 440.0f));
        }
    }
}



// ===========================================================================
// Nappes
void Drone::spawn (Layer& l, Grain& g, float rate) noexcept
{
    const double glen = juce::jmin (0.18 * fs, l.len / juce::jmax (0.25f, rate) * 0.8);
    g.inc = (float) (1.0 / juce::jmax (64.0, glen));
    g.det = 1.0f + (rnd (l) - 0.5f) * 0.008f;
    g.pan = 0.15f + 0.7f * rnd (l);
    const double span = juce::jmax (0.0, l.len - glen * rate);
    g.pos = l.a + rnd (l) * span;
}

void Drone::setNext (Memory* m, double a, double len, float gain, double fadeSec, uint32_t seed)
{
    if (m == nullptr || len < 64.0) return;
    const int t = layers[cur].m == nullptr ? cur : 1 - cur;
    auto& L = layers[t];
    L.m = m; L.a = a; L.len = len; L.gain = gain; L.rng = seed | 1u;
    for (int k = 0; k < kGrains; ++k) { spawn (L, L.g[k], 1.0f); L.g[k].ph = (float) k / (float) kGrains; }
    if (t != cur) { cur = t; xf = 0.0f; xfStep = 1.0f / (float) juce::jmax (1.0, fadeSec * fs); }
    else { xf = 1.0f; master = 0.0f; xfStep = 1.0f / (float) juce::jmax (1.0, fadeSec * fs); }
    fadeOut = false;
    changes.fetch_add (1);
}

void Drone::process (float* outL, float* outR, int n, float level, float rate) noexcept
{
    if (! active()) return;
    const float norm = 2.0f / (float) kGrains;
    for (int i = 0; i < n; ++i)
    {
        if (xf < 1.0f) { xf = juce::jmin (1.0f, xf + xfStep); if (xf >= 1.0f) layers[1 - cur].m = nullptr; }
        if (fadeOut) { master -= foStep; if (master <= 0.0f) { reset(); master = 1.0f; fadeOut = false; return; } }
        else if (master < 1.0f) master = juce::jmin (1.0f, master + xfStep);
        const float gNew = std::sin (xf * 1.5707963f), gOld = std::cos (xf * 1.5707963f);
        float yl = 0.0f, yr = 0.0f;
        for (int li = 0; li < 2; ++li)
        {
            auto& L = layers[li];
            if (L.m == nullptr) continue;
            const float lg = (li == cur ? gNew : gOld) * L.gain;
            if (lg <= 0.0f) continue;
            for (auto& g : L.g)
            {
                const float s = std::sin (juce::MathConstants<float>::pi * g.ph);
                const float y = L.m->read (g.pos) * s * s * lg;
                yl += y * (1.0f - g.pan); yr += y * g.pan;
                g.pos += (double) (rate * g.det);
                g.ph += g.inc;
                if (g.ph >= 1.0f) { g.ph -= 1.0f; spawn (L, g, rate); }
            }
        }
        outL[i] += yl * norm * 1.4f * level * master;
        outR[i] += yr * norm * 1.4f * level * master;
    }
}

// ===========================================================================
// Pulsation des musiciens
void PulseTracker::prepare (double sampleRate)
{
    fs = sampleRate;
    hop = juce::jmax (64, (int) std::round (fs * 0.01));
    std::fill (env.begin(), env.end(), 0.0f);
    frameCount.store (0); samples = 0; hopFill = 0; hopE = hopH = 0; lp = 0; prevLe = prevLh = -10;
    estBpm.store (0.0); estBeat.store (0.0); estValid.store (false);
    curBpm = candBpm = 0.0; candHits = 0; lastAnalyzed = 0;
    ppq = 0.0; clockBpm = 120.0;
    bpmView.store (0.0f); confView.store (0.0f); validView.store (false);
}

void PulseTracker::push (const float* x, int n) noexcept
{
    const float c = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 2000.0f / (float) fs);
    for (int i = 0; i < n; ++i)
    {
        lp += c * (x[i] - lp);
        const float h = x[i] - lp;
        hopE += (double) lp * lp; hopH += (double) h * h;
        if (++hopFill >= hop)
        {
            // flux d'energie (grave + aigu), demi-redresse
            const float le = (float) std::log10 (hopE / hop + 1.0e-9), lh = (float) std::log10 (hopH / hop + 1.0e-9);
            const float f = juce::jmax (0.0f, le - prevLe) + juce::jmax (0.0f, lh - prevLh);
            prevLe = le; prevLh = lh;
            const int64_t fc = frameCount.load (std::memory_order_relaxed);
            env[(size_t) (fc % kFrames)] = le > -5.5f || lh > -5.5f ? f : 0.0f;
            frameCount.store (fc + 1, std::memory_order_release);
            hopFill = 0; hopE = hopH = 0;
        }
    }
    samples += n;
}

void PulseTracker::analyze()
{
    const int64_t fc = frameCount.load (std::memory_order_acquire);
    if (fc - lastAnalyzed < 25 || fc < 300) return;          // toutes les 250 ms, apres 3 s d'ecoute
    lastAnalyzed = fc;
    const int F = (int) juce::jmin ((int64_t) 800, fc);       // 8 s
    const int64_t f0 = fc - F;
    // enveloppe sans sa moyenne locale
    for (int i = 0; i < F; ++i) work[(size_t) i] = env[(size_t) ((f0 + i) % kFrames)];
    std::vector<float> o ((size_t) F);
    double tot = 0.0;
    for (int i = 0; i < F; ++i)
    {
        double m = 0; int c = 0;
        for (int k = juce::jmax (0, i - 8); k <= juce::jmin (F - 1, i + 8); ++k) { m += work[(size_t) k]; ++c; }
        o[(size_t) i] = juce::jmax (0.0f, work[(size_t) i] - (float) (m / c));
        tot += o[(size_t) i];
    }
    if (tot < 1.0e-3) { estValid.store (false); confView.store (0.0f); validView.store (false); return; }
    auto acf = [&] (double L)
    {
        const int Li = (int) L; const float fr = (float) (L - Li);
        double a = 0;
        for (int i = Li + 1; i < F; ++i) a += (double) o[(size_t) i] * ((1.0f - fr) * o[(size_t) (i - Li)] + fr * o[(size_t) (i - Li - 1)]);
        return a / (double) (F - Li);
    };
    double a0 = 0; for (int i = 0; i < F; ++i) a0 += (double) o[(size_t) i] * o[(size_t) i];
    a0 /= F;
    // periode : 50 a 200 BPM, avec une preference douce autour de 110 BPM
    double best = -1, bestL = 0, bestRaw = 0;
    std::vector<double> sc (160, 0.0);
    for (int L = 30; L <= 120; ++L)
    {
        const double bpm = 6000.0 / L;
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 110.0) / 0.75, 2.0));
        const double raw = acf (L) + 0.5 * acf (2 * L) + 0.25 * acf (3 * L);
        sc[(size_t) L] = raw;
        const double v = raw * (0.4 + 0.6 * prior);
        if (v > best) { best = v; bestL = L; bestRaw = acf (L); }
    }
    {
        const int L = (int) bestL;
        if (L > 30 && L < 120)
        {
            const double a = sc[(size_t) (L - 1)], b = sc[(size_t) L], c = sc[(size_t) (L + 1)], den = a - 2 * b + c;
            if (std::abs (den) > 1e-12) bestL += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den);
        }
    }
    const float conf = a0 > 0 ? (float) (bestRaw / a0) : 0.0f;
    confView.store (conf);
    const double P = bestL;
    // phase : decalage qui aligne le mieux les temps sur les attaques
    double bestPh = 0, bestS = -1;
    for (int ph = 0; ph < (int) std::ceil (P); ++ph)
    {
        double sum = 0;
        for (double t = F - 1 - ph; t >= 0; t -= P)
        {
            const int ti = (int) t;
            sum += o[(size_t) ti] + (ti > 0 ? 0.5 * o[(size_t) (ti - 1)] : 0.0) + (ti + 1 < F ? 0.5 * o[(size_t) (ti + 1)] : 0.0);
        }
        if (sum > bestS) { bestS = sum; bestPh = ph; }
    }
    const double bpm = 6000.0 / P;
    // stabilite : un changement de tempo doit etre confirme
    if (curBpm <= 0.0 || std::abs (std::log2 (bpm / curBpm)) < 0.04) curBpm = curBpm <= 0.0 ? bpm : curBpm * 0.7 + bpm * 0.3;
    else
    {
        if (candBpm > 0 && std::abs (std::log2 (bpm / candBpm)) < 0.04) ++candHits; else { candBpm = bpm; candHits = 1; }
        if (candHits >= 3) { curBpm = bpm; candHits = 0; candBpm = 0; }
    }
    const bool ok = conf > 0.12f;
    const double beatFrame = (double) (f0 + F - 1) - bestPh;
    estBeat.store (beatFrame * hop);
    estBpm.store (curBpm);
    estValid.store (ok);
    bpmView.store ((float) curBpm);
    validView.store (ok);
}

bool PulseTracker::clock (int n, double& ppqStart, double& ppsEff, double& bpmOut) noexcept
{
    const bool ok = estValid.load (std::memory_order_acquire);
    const double b = estBpm.load();
    if (b > 0.0) clockBpm += (b - clockBpm) * juce::jmin (1.0, (double) n / (0.5 * fs));
    const double pps = clockBpm / 60.0 / fs;
    double corr = 0.0;
    if (ok && b > 0.0)
    {
        // boucle a verrouillage de phase : on accelere / ralentit un peu, sans jamais reculer
        const double period = 60.0 / b * fs;
        const double target = std::fmod (((double) samples - estBeat.load()) / period, 1.0);
        double err = (target < 0 ? target + 1.0 : target) - (ppq - std::floor (ppq));
        if (err > 0.5) err -= 1.0;
        if (err < -0.5) err += 1.0;
        corr = juce::jlimit (-0.4, 0.4, 2.0 * err);
    }
    ppqStart = ppq;
    ppsEff = pps * (1.0 + corr);
    ppq += ppsEff * n;
    bpmOut = clockBpm;
    phaseView.store ((float) (ppqStart - std::floor (ppqStart)));
    sampleView.store (samples);
    return ok;
}

// ===========================================================================
// Improvisatrice
void Improviser::notesOff (ImHost& host, Voice& vo, int sample)
{
    if (! vo.notesOn) return;
    for (int& n : vo.notes) if (n >= 0) { host.noteEvent (n, 0.0f, sample, false); n = -1; }
    vo.notesOn = false;
}

void Improviser::reset()
{
    for (auto& v : voices) v = Voice();
    shifter.clear(); shiftRatio = 1.0f;
    pendingState = -1; pendingIsPhrase = false; pendingPpq = 0;
    inPhrase = stopReq = wasActive = pendingEcho = politeDecided = false;
    phraseLeft = gapLeft = silenceTime = activeTime = echoTimer = 0;
    echoStart = echoEnd = -1; playerStartState = 1;
    counter = 1; master = 0;
    motifActive = motifDrift = false; machActive = false; forcedSteps = 0; phraseMem = nullptr; contrastGain = 1.0f;
    motifReqLo.store (-1); motifView.store (false); onCorpus.store (false);
    curState.store (-1); status.store (SEmpty);
}

// Chapitres : plages d'etats autorisees dans la memoire du direct
void Improviser::updateChapters (Memory& m, ImHost& host)
{
    int64_t st[kMaxRanges], en[kMaxRanges];
    const int nr = host.chapterRanges (st, en, kMaxRanges);
    numRng = 0; rngTotal = 0; chapFilter = false;
    if (nr < 0) { chapterFilterView.store (false); return; }
    const int n = m.numStates();
    for (int i = 0; i < nr && numRng < kMaxRanges; ++i)
    {
        const int lo = m.stateAtOrAfter (st[i]);
        const int hi = (en[i] == INT64_MAX ? n : m.stateAtOrAfter (en[i])) - 1;
        if (hi >= lo && lo >= 1) { rngLo[numRng] = lo; rngHi[numRng] = hi; rngTotal += hi - lo + 1; ++numRng; }
    }
    chapFilter = rngTotal >= 3;          // chapitre (presque) vide : toute la memoire, en attendant
    chapterFilterView.store (chapFilter);
}

bool Improviser::allowed (Memory& m, int s) const noexcept
{
    if (! chapFilter || &m != liveMem) return true;
    for (int i = 0; i < numRng; ++i) if (s >= rngLo[i] && s <= rngHi[i]) return true;
    return false;
}

int Improviser::sampleAllowed()
{
    if (rngTotal <= 0) return -1;
    int k = juce::jmin (rngTotal - 1, (int) (rnd() * (float) rngTotal));
    for (int i = 0; i < numRng; ++i)
    {
        const int len = rngHi[i] - rngLo[i] + 1;
        if (k < len) return rngLo[i] + k;
        k -= len;
    }
    return rngHi[numRng - 1];
}

int Improviser::sampleState (Memory& m, int lo, int n)
{
    if (chapFilter && &m == liveMem) return sampleAllowed();
    return lo + juce::jmin (n - 1 - lo, (int) (rnd() * (float) (n - lo)));
}

int Improviser::regionLo (Memory& m, const float* v) const
{
    const int n = m.numStates();
    const int fv = juce::jmax (1, m.firstValidState());
    if (&m != liveMem || chapFilter) return fv;   // corpus ou chapitre : toute la memoire (le chapitre fait le tri)
    const int avail = n - fv;
    const float mm = v[IMem];
    const int count = 12 + (int) ((float) juce::jmax (0, avail - 12) * mm * mm);
    return juce::jmax (fv, n - count);
}

float Improviser::followWeight (Memory& m, int state, float follow) const
{
    if (std::abs (follow) < 0.05f) return 1.0f;
    const auto& g = m.segment (state);
    const LiveState* L = hostP != nullptr ? hostP->live() : nullptr;
    float pp, pd;
    if (L != nullptr && L->valid.load()) { pp = L->pitch.load(); pd = L->rmsDb.load(); }
    else { Memory& lm = liveMem != nullptr ? *liveMem : m; pp = lm.lastPitch.load(); pd = lm.lastRmsDb.load(); }
    float sim;
    if (pp > 0.0f && g.pitch > 0.0f)
    {
        int d = std::abs (((int) std::round (g.pitch) - (int) std::round (pp)) % 12);
        if (d > 6) d = 12 - d;
        sim = d == 0 ? 1.0f : (d <= 2 ? 0.2f : (d == 5 ? 0.5f : -0.6f));
    }
    else sim = 1.0f - juce::jmin (2.0f, std::abs (g.rmsDb - pd) / 8.0f);
    return juce::jmax (0.05f, 1.0f + follow * sim);
}

// Accord entre un fragment (transpose de "semis") et l'harmonie entendue : 0..1 (-1 = fragment sans hauteur)
float Improviser::harmFit (const Segment& g, float semis, int& bestShift, bool allowShift) const
{
    bestShift = 0;
    const LiveState* L = hostP != nullptr ? hostP->live() : nullptr;
    if (L == nullptr || ! L->harmValid.load()) return -1.0f;
    int pcs[3] { -1, -1, -1 }; int np = 0;
    const int sh = (int) std::round (semis);
    if (g.chord >= 0 && g.chord < 24)
    {
        const int r = g.chord % 12;
        pcs[0] = r; pcs[1] = r + (g.chord < 12 ? 4 : 3); pcs[2] = r + 7; np = 3;
    }
    else if (g.chord >= 24) { pcs[0] = g.chord % 12; np = 1; }
    else if (g.pitch > 0.0f) { pcs[0] = (int) std::round (g.pitch); np = 1; }
    else return -1.0f;
    float c[12];
    for (int i = 0; i < 12; ++i) c[i] = L->chroma[i].load (std::memory_order_relaxed);
    auto fitAt = [&] (int d)
    {
        float f = 0.0f;
        for (int k = 0; k < np; ++k) f += c[(((pcs[k] + sh + d) % 12) + 12) % 12];
        return f / (float) np;
    };
    float best = fitAt (0);
    if (allowShift)
    {
        float bestScore = best;
        for (int d = -5; d <= 6; ++d)
        {
            if (d == 0) continue;
            const float f = fitAt (d), sc = f - 0.04f * (float) std::abs (d);
            if (sc > bestScore + 0.08f) { bestScore = sc; best = f; bestShift = d; }
        }
    }
    return best;
}

// Mode Contraste : energie et densite opposees a celles des musiciens
float Improviser::contrastWeight (Memory& m, int state) const
{
    const LiveState* L = hostP != nullptr ? hostP->live() : nullptr;
    if (L == nullptr) return 1.0f;
    const auto& g = m.segment (state);
    const float le = L->energy.load(), ld = LiveListener::density01 (L->density.load());
    const float se = LiveListener::energy01 (g.rmsDb);
    const float lenS = (float) ((double) (g.end - g.start) / m.sampleRate());
    const float sd = LiveListener::density01 (1.0f / juce::jmax (0.02f, lenS));
    const float d = 0.6f * std::abs (se - (1.0f - le)) + 0.4f * std::abs (sd - (1.0f - ld));
    return 0.03f + std::exp (-(d * d) / (2.0f * 0.16f * 0.16f));
}

float Improviser::choiceWeight (Memory& m, int state, float follow, const float* v) const
{
    float w = followWeight (m, state, follow);
    const float h = hostP != nullptr ? hostP->harmAmount() : 0.0f;
    if (h > 0.01f)
    {
        int shift = 0;
        const float fit = harmFit (m.segment (state), imTranspSemis (v[ITransp]), shift, hostP->harmTranspose());
        if (fit >= 0.0f) w *= (1.0f - h) + h * (0.03f + 1.97f * fit * fit);
        else w *= 1.0f - 0.5f * h;            // fragments sans hauteur : un peu moins choisis
    }
    if (curMode == MContrast) w *= contrastWeight (m, state);
    return w;
}

// Si la suite naturelle va mal avec l'harmonie entendue (ou le contraste voulu), Maria saute vers un fragment qui convient
int Improviser::steer (Memory& m, const float* v, int nx)
{
    if (nx <= 0 || hostP == nullptr || motifActive) return nx;
    const float h = hostP->harmAmount();
    const float amount = juce::jmax (h, curMode == MContrast ? 0.8f : 0.0f);
    if (amount < 0.01f) return nx;
    float q = 1.0f;
    if (h > 0.01f)
    {
        int shift = 0;
        const float fit = harmFit (m.segment (nx), imTranspSemis (v[ITransp]), shift, hostP->harmTranspose());
        if (fit >= 0.0f) q = juce::jmin (q, 1.0f - h * (1.0f - fit));
    }
    if (curMode == MContrast) q = juce::jmin (q, juce::jmin (1.0f, contrastWeight (m, nx)));
    if (rnd() >= amount * (1.0f - q)) return nx;
    const int n = m.numStates(), lo = regionLo (m, v);
    if (lo >= n) return nx;
    const float f = bip (v[IFollow]);
    int pickS = -1; float total = 0.0f;
    for (int k = 0; k < 24; ++k)
    {
        const int s = sampleState (m, lo, n);
        if (! m.stateValid (s)) continue;
        const float w = choiceWeight (m, s, f, v);
        total += w;
        if (rnd() * total < w) pickS = s;
    }
    return pickS > 0 ? pickS : nx;
}

int Improviser::chooseStart (Memory& m, const float* v, int mode)
{
    const int n = m.numStates();
    if (mode == MEcho && echoStart >= 1 && &m == liveMem)
    {
        const int a = juce::jmax (echoStart, m.firstValidState()), b = juce::jmin (echoEnd, n) - 1;
        if (b >= a)
        {
            const int s = a + (int) (rnd() * (float) juce::jmin (3, b - a + 1));
            if (m.stateValid (s)) return s;
        }
    }
    const int lo = regionLo (m, v);
    if (lo >= n) return -1;
    const float f = bip (v[IFollow]);
    int pickS = -1; float total = 0.0f;
    for (int k = 0; k < 32; ++k)
    {
        const int s = sampleState (m, lo, n);
        if (! m.stateValid (s)) continue;
        const float w = choiceWeight (m, s, f, v);
        total += w;
        if (rnd() * total < w) pickS = s;
    }
    return pickS;
}

// Leitmotiv : on suit le passage marque, avec plus ou moins de variations
int Improviser::chooseNextMotif (Memory& m, const float* v, int s)
{
    const float var = motifVarCur;
    if (motifDrift)
    {
        // derive hors du theme : retour au theme de temps en temps
        if (rnd() < 0.3f + 0.4f * (1.0f - var)) { motifDrift = false; const int r = motifLo + (int) (rnd() * (float) juce::jmax (1, (motifHi - motifLo + 1) / 3)); if (m.stateValid (r)) return r; }
        motifActive = false;
        const int nx = chooseNext (m, v, s);
        motifActive = true;
        return nx;
    }
    if (s + 1 <= motifHi && m.stateValid (s + 1) && rnd() >= var * 0.7f) return s + 1;
    if (s + 1 > motifHi && rnd() >= var * 0.6f) return -1;   // fin du theme
    if (rnd() < var * 0.3f)
    {
        motifDrift = true;
        motifActive = false;
        const int nx = chooseNext (m, v, s);
        motifActive = true;
        return nx;
    }
    // saut a l'interieur du theme, vers un moment qui partage le contexte
    int cand[64]; float wts[64]; int nc = 0;
    const int base = m.sfx (s);
    for (int j = juce::jmax (motifLo - 1, 1); j < motifHi && nc < 64; ++j)
    {
        if (j == s || ! m.stateValid (j + 1)) continue;
        const int sj = m.sfx (j);
        const bool related = (j == base && base > 0) || sj == s || (base > 0 && sj == base);
        cand[nc] = j + 1; wts[nc] = related ? 4.0f : 1.0f; ++nc;
    }
    if (nc == 0) return s + 1 <= motifHi && m.stateValid (s + 1) ? s + 1 : -1;
    float tot = 0.0f;
    for (int k = 0; k < nc; ++k) tot += wts[k];
    float u = rnd() * tot;
    for (int k = 0; k < nc; ++k) { u -= wts[k]; if (u <= 0.0f) return cand[k]; }
    return cand[nc - 1];
}

int Improviser::chooseNext (Memory& m, const float* v, int s)
{
    if (motifActive && &m == liveMem) return chooseNextMotif (m, v, s);
    if (machActive && &m == phraseMem) return machineNext (m, v);
    const int n = m.numStates();
    const int lo = regionLo (m, v);
    if (s + 1 < n && s + 1 >= lo && m.stateValid (s + 1) && allowed (m, s + 1) && rnd() < v[ICont]) return steer (m, v, s + 1);

    const int ctxMin = imCtxMin (v[ICtx]);
    const float f = bip (v[IFollow]);
    const int base = s < n ? m.sfx (s) : 0;
    int cand[64]; float wts[64]; int nc = 0, seen = 0;
    for (int j = lo; j < n - 1; ++j)
    {
        if (j == s) continue;
        const int sj = m.sfx (j);
        int ctx = -1;
        if (j == base && base > 0) ctx = m.lrs (s);
        else if (sj == s) ctx = m.lrs (j);
        else if (base > 0 && sj == base) ctx = juce::jmin (m.lrs (s), m.lrs (j));
        if (ctx < ctxMin || ! m.stateValid (j + 1) || ! allowed (m, j + 1)) continue;
        const float w = choiceWeight (m, j + 1, f, v) * (1.0f + 0.15f * (float) juce::jmin (ctx, 8));
        ++seen;
        if (nc < 64) { cand[nc] = j + 1; wts[nc] = w; ++nc; }
        else { const int r = (int) (rnd() * (float) seen); if (r < 64) { cand[r] = j + 1; wts[r] = w; } }
    }
    if (nc > 0)
    {
        float tot = 0.0f;
        for (int k = 0; k < nc; ++k) tot += wts[k];
        float u = rnd() * tot;
        for (int k = 0; k < nc; ++k) { u -= wts[k]; if (u <= 0.0f) return steer (m, v, cand[k]); }
        return steer (m, v, cand[nc - 1]);
    }
    if (s + 1 < n && m.stateValid (s + 1) && allowed (m, s + 1)) return steer (m, v, s + 1);
    return chooseStart (m, v, MDialogue);
}


// Mode Machine : une boucle de fragments consecutifs, calee sur la grille, qui mute lentement
void Improviser::buildMachine (Memory& m, const float* v, int s0, const TempoInfo& t)
{
    juce::ignoreUnused (v);
    const double beats = hostP != nullptr ? hostP->machineBeats() : 4.0;
    const double grid = juce::jmax (0.0625, t.grid);
    const double pps = t.pps > 0.0 ? t.pps : 2.0 / fs;
    machN = 0;
    double tot = 0.0;
    const int totalSteps = juce::jmax (1, (int) std::round (beats / grid));
    int usedSteps = 0;
    for (int s = s0; s < m.numStates() && machN < kMachMax; ++s)
    {
        if (! m.stateValid (s) || ! allowed (m, s)) break;
        const auto& g = m.segment (s);
        const double len = (double) (g.end - g.start);
        int steps = juce::jmax (1, (int) std::round (len * pps / grid));
        if (t.sync) steps = juce::jmin (steps, totalSteps - usedSteps);
        machState[machN] = s; machSteps[machN] = t.sync ? steps : 0; ++machN;
        usedSteps += steps;
        tot += len * pps;
        if ((t.sync && usedSteps >= totalSteps) || (! t.sync && tot >= beats - 1.0e-6)) break;
    }
    if (machN == 0) { machState[0] = s0; machSteps[0] = t.sync ? totalSteps : 0; machN = 1; }
    else if (t.sync && usedSteps < totalSteps) machSteps[machN - 1] += totalSteps - usedSteps;   // la boucle tombe juste
    machPos = 0;
    machActive = true;
    machineLength.store (machN);
}

int Improviser::machineNext (Memory& m, const float* v)
{
    machPos = (machPos + 1) % juce::jmax (1, machN);
    if (machPos == 0)
    {
        machineCycles.fetch_add (1);
        // mutation : un element de la boucle est remplace par un fragment apparente
        const float mu = hostP != nullptr ? hostP->machineMutation() : 0.15f;
        if (machN > 1 && rnd() < mu)
        {
            const int r = juce::jmin (machN - 1, (int) (rnd() * (float) machN));
            const int prev = machState[(r + machN - 1) % machN];
            machActive = false;
            int alt = -1;
            for (int k = 0; k < 4 && (alt <= 0 || alt == machState[r]); ++k) alt = chooseNext (m, v, prev);
            machActive = true;
            if (alt > 0 && alt != machState[r]) { machState[r] = alt; machineMutations.fetch_add (1); }
        }
    }
    const int s = machState[machPos];
    if (! m.stateValid (s)) return -1;
    forcedSteps = machSteps[machPos];
    return s;
}

int Improviser::pickVoice() const
{
    if (! voices[0].on) return 0;
    if (! voices[1].on) return 1;
    return voices[0].env <= voices[1].env ? 0 : 1;
}

void Improviser::startSegment (Memory& m, ImHost& host, const SourceProfile& p, int k, int s, bool phraseStart, float* hits, int i, const TempoInfo& t, double ppqNow)
{
    juce::ignoreUnused (phraseStart);
    if (! m.stateValid (s)) return;
    const float* v = host.imValues();
    const auto& g = m.segment (s);
    auto& vo = voices[k];
    notesOff (host, vo, i);
    vo.on = true; vo.handed = false; vo.state = s; vo.mem = &m;
    vo.a = (double) g.start; vo.b = (double) g.end;
    float semis = imTranspSemis (v[ITransp]);
    // reactivite harmonique : le fragment est transpose vers l'harmonie entendue
    const float h = host.harmAmount();
    if (h > 0.01f && host.harmTranspose())
    {
        int shift = 0;
        if (harmFit (g, semis, shift, true) >= 0.0f && shift != 0 && rnd() < juce::jmin (1.0f, h * 1.25f))
        {
            semis += (float) shift;
            lastHarmShift.store (shift);
            harmShifts.fetch_add (1, std::memory_order_relaxed);
        }
    }
    const double ratio = juce::jlimit (0.25, 4.0, std::pow (2.0, (double) semis / 12.0));
    int tm = host.transpMode();                       // 0 bande, 1 hauteur seule, 2 aleatoire (tire a chaque segment)
    if (tm == 2) tm = rnd() < 0.5f ? 0 : 1;
    tmodeCount[tm == 1 ? 1 : 0].fetch_add (1, std::memory_order_relaxed);
    if (tm == 1) { vo.rate = 1.0; shiftRatio = (float) ratio; }   // hauteur seule : la duree est gardee
    else { vo.rate = ratio; shiftRatio = 1.0f; }
    if (g.nn > 0)
    {
        // evenement MIDI appris : les notes exactes, a la velocite jouee
        const int sh = (int) std::round (semis);
        for (int q = 0; q < juce::jmin ((int) g.nn, 6); ++q) vo.notes[q] = juce::jlimit (0, 127, (int) g.notes[q] + sh);
        const float vel = juce::jlimit (0.05f, 1.0f, (float) g.vel / 127.0f);
        for (int nn : vo.notes) if (nn >= 0) host.noteEvent (nn, vel, i, true);
        vo.notesOn = true;
    }
    else if (g.pitch > 0.0f)
    {
        const int root = (int) std::round (g.pitch + semis);
        vo.notes[0] = juce::jlimit (0, 127, root);
        if (g.chord >= 0 && g.chord < 24)
        {
            vo.notes[1] = juce::jlimit (0, 127, root + (g.chord < 12 ? 4 : 3));
            vo.notes[2] = juce::jlimit (0, 127, root + 7);
        }
        const float vel = juce::jlimit (0.15f, 1.0f, (g.rmsDb + 50.0f) / 44.0f);
        for (int nn : vo.notes) if (nn >= 0) host.noteEvent (nn, vel, i, true);
        vo.notesOn = true;
    }
    vo.rev = rnd() < v[IRev];
    vo.pos = vo.rev ? vo.b - 1.0 : vo.a;
    const double outLen = (vo.b - vo.a) / vo.rate;
    const float xf = (float) juce::jmin ((double) juce::jmax (16.0f, p.xfadeMs * 0.001f * (float) fs), outLen * 0.3);
    vo.env = 0.0f; vo.fadeDir = 1; vo.fadeStep = 1.0f / juce::jmax (1.0f, xf);
    if (t.sync)
    {
        const double beats = outLen * t.pps;
        const double steps = forcedSteps > 0 ? (double) forcedSteps : juce::jmax (1.0, std::round (beats / t.grid));
        vo.endPpq = ppqNow + steps * t.grid;
    }
    forcedSteps = 0;
    hits[i] = juce::jmax (hits[i], juce::jlimit (0.1f, 1.0f, (g.rmsDb + 50.0f) / 44.0f));
    const bool corpusSeg = &m != liveMem;
    onCorpus.store (corpusSeg);
    (corpusSeg ? corpusSegs : liveSegs).fetch_add (1, std::memory_order_relaxed);
    if (motifActive && ! corpusSeg)
    {
        motifSegs.fetch_add (1, std::memory_order_relaxed);
        if (s >= motifLo && s <= motifHi) motifSegsInside.fetch_add (1, std::memory_order_relaxed);
    }
    if (! corpusSeg)
    {
        const int prev = curState.load();
        if (prev > 0 && s != prev + 1) { jumpFrom.store (prev); jumpTo.store (s); }
        curState.store (s);
    }
    segCount.fetch_add (1);
}

void Improviser::process (Memory& m, ImHost& host, const SourceProfile& p, float* out, float* hits, int n, bool playerActive, uint32_t sd, const TempoInfo& tp)
{
    seed = sd;
    hostP = &host;
    liveMem = &m;
    const float* v = host.imValues();
    const int mode = host.imMode();
    curMode = mode;
    const int N = m.numStates();
    updateChapters (m, host);
    for (int i = 0; i < n; ++i) { out[i] = 0.0f; hits[i] = 0.0f; }
    const LiveState* live = host.live();
    const float liveE = live != nullptr ? live->energy.load() : 0.0f;
    const float liveD = live != nullptr ? LiveListener::density01 (live->density.load()) : 0.0f;

    // corpus externe
    Memory* corpus = host.corpusMem();
    const bool corpusOk = corpus != nullptr && corpus->isPrepared() && corpus->numStates() - corpus->firstValidState() >= 4;
    const float cmix = corpusOk ? juce::jlimit (0.0f, 1.0f, host.corpusMix()) : 0.0f;
    if (! corpusOk)
    {
        // le corpus a disparu (rechargement, effacement) : ses voix s'eteignent vite
        for (auto& vo : voices)
            if (vo.on && vo.mem != &m && vo.fadeDir >= 0) { vo.fadeDir = -1; vo.fadeStep = 1.0f / (0.02f * (float) fs); vo.handed = true; }
        if (phraseMem != nullptr && phraseMem != &m) { stopReq = inPhrase; pendingState = -1; }
    }

    // suivi du joueur
    if (playerActive)
    {
        if (! wasActive) { playerStartState = N; politeDecided = false; }
        activeTime += n; silenceTime = 0;
    }
    else
    {
        if (wasActive && activeTime > 0.4 * fs) { echoStart = playerStartState; pendingEcho = true; }
        silenceTime += n;
        if (silenceTime > 0.3 * fs) activeTime = 0;
    }
    wasActive = playerActive;
    if (pendingEcho) echoEnd = N;

    const double react = imReactSec (v[IReact]) * fs;
    bool want = false, stopCond = false;
    switch (mode)
    {
        case MDialogue: want = ! playerActive && silenceTime > react; stopCond = playerActive; break;
        case MDuo:      want = true; break;
        case MEcho:     want = pendingEcho && ! playerActive && silenceTime > react; stopCond = playerActive; break;
        case MContrast: want = true; break;
        case MMachine:  want = true; break;
        default:        want = playerActive && activeTime > react; break;
    }
    const bool liveOk = N - m.firstValidState() >= 4;
    const bool haveMemory = liveOk || (corpusOk && cmix > 0.0f);

    if (silenceReq.exchange (false))
    {
        stopReq = inPhrase;
        gapLeft = imGapSec (v[IGap]) * fs;
        pendingEcho = false;
        motifReqLo.store (-1);
    }
    const bool force = forceReq.exchange (false);

    // leitmotiv demande : il remplace ce qui joue
    {
        const int mlo = motifReqLo.exchange (-1);
        const int mhi = motifReqHi.load();
        if (mlo >= 1 && mhi >= mlo && m.stateValid (mlo) && m.stateValid (mhi))
        {
            for (auto& vo : voices)
                if (vo.on && vo.fadeDir >= 0) { vo.fadeDir = -1; vo.fadeStep = 1.0f / (0.03f * (float) fs); vo.handed = true; }
            pendingState = -1; pendingIsPhrase = false;
            inPhrase = true; stopReq = false; politeDecided = true;
            motifActive = true; motifDrift = false; motifLo = mlo; motifHi = mhi; machActive = false;
            motifVarCur = juce::jlimit (0.0f, 1.0f, host.motifVar());
            phraseMem = &m; onCorpus.store (false);
            const double dur = (double) (m.segment (mhi).end - m.segment (mlo).start);
            phraseLeft = juce::jmax (dur, 0.5 * fs) * (1.0 + 1.5 * motifVarCur);
            motifPhrases.fetch_add (1);
            host.segmentStarting (true);
            v = host.imValues();
            if (tp.sync)
            {
                const double al = juce::jmax (0.0625, tp.align);
                pendingState = mlo; pendingIsPhrase = true;
                pendingPpq = std::ceil (tp.ppq / al - 1.0e-6) * al;
            }
            else
            {
                startSegment (m, host, p, pickVoice(), mlo, true, hits, 0, tp, tp.ppq);
                phraseCount.fetch_add (1);
            }
        }
    }

    if (! inPhrase)
    {
        gapLeft -= n;
        if (haveMemory && (force || (want && gapLeft <= 0.0)))
        {
            if (force || rnd() < 0.1f + 0.9f * v[IAct])
            {
                // memoire de la phrase : le direct, ou le corpus externe
                Memory* pm = &m;
                if (corpusOk && mode != MEcho)
                {
                    if (! liveOk) pm = corpus;
                    else if (cmix > 0.0f && rnd() < cmix) pm = corpus;
                }
                host.segmentStarting (true);
                v = host.imValues();
                const int s0 = chooseStart (*pm, v, pm == &m ? mode : MDialogue);
                if (s0 > 0)
                {
                    inPhrase = true; stopReq = false;
                    motifActive = false; motifDrift = false; machActive = false;
                    phraseMem = pm;
                    phraseLeft = imPhraseSec (v[IPhrase]) * fs * (0.6 + 0.8 * rnd());
                    if (mode == MMachine) { buildMachine (*pm, v, s0, tp); phraseLeft *= 3.0; forcedSteps = machSteps[0]; }
                    if (mode == MContrast) phraseLeft *= 1.3 - 0.7 * liveE;     // musiciens forts -> phrases plus courtes
                    politeDecided = playerActive;
                    if (tp.sync)
                    {
                        // la phrase partira sur le prochain temps / la prochaine mesure
                        const double al = juce::jmax (0.0625, tp.align);
                        pendingState = s0; pendingIsPhrase = true;
                        pendingPpq = std::ceil (tp.ppq / al - 1.0e-6) * al;
                    }
                    else
                    {
                        startSegment (*pm, host, p, freeVoice(), s0, true, hits, 0, tp, tp.ppq);
                        phraseCount.fetch_add (1);
                    }
                    if (mode == MEcho) pendingEcho = false;
                }
            }
            else gapLeft = imGapSec (v[IGap]) * fs * (0.5 + rnd());
        }
    }
    else if (! motifActive)
    {
        phraseLeft -= n;
        if (mode == MCounter && silenceTime > react) stopReq = true;
        else if (stopCond && ! politeDecided) { politeDecided = true; if (rnd() < v[IPolite]) stopReq = true; }
    }
    else phraseLeft -= n;

    if (stopReq) pendingState = -1;
    if (stopReq)
        for (auto& vo : voices)
            if (vo.on && vo.fadeDir >= 0) { vo.fadeDir = -1; vo.fadeStep = 1.0f / (0.15f * (float) fs); vo.handed = true; }

    // Contraste : plus doux quand les musiciens jouent fort
    const float cgT = mode == MContrast ? 1.15f - 0.75f * liveE : 1.0f;
    contrastGain += (cgT - contrastGain) * (1.0f - std::exp (-(float) n / (0.3f * (float) fs)));
    const float level = v[ILevel] * v[ILevel] * 2.0f * contrastGain;
    const float xfS = juce::jmax (16.0f, p.xfadeMs * 0.001f * (float) fs);
    Memory& pmem = phraseMem != nullptr ? *phraseMem : m;
    for (int i = 0; i < n; ++i)
    {
        float y = 0.0f;
        const double ppqI = tp.ppq + (double) i * tp.pps;
        if (pendingState > 0)
        {
            if (! tp.sync || pendingPpq - ppqI > 16.0) pendingPpq = ppqI;   // retour en arriere du transport
            if (ppqI >= pendingPpq - 0.5 * tp.pps)
            {
                const int st = pendingState; pendingState = -1;
                if (pendingIsPhrase) phraseCount.fetch_add (1);
                startSegment (pmem, host, p, pendingIsPhrase ? pickVoice() : freeVoice(), st, pendingIsPhrase, hits, i, tp, ppqI);
                pendingIsPhrase = false;
            }
        }
        for (int k = 0; k < 2; ++k)
        {
            auto& vo = voices[k];
            if (! vo.on) continue;
            if (vo.fadeDir > 0) { vo.env += vo.fadeStep; if (vo.env >= 1.0f) { vo.env = 1.0f; vo.fadeDir = 0; } }
            else if (vo.fadeDir < 0) { vo.env -= vo.fadeStep; if (vo.env <= 0.0f) { vo.on = false; notesOff (host, vo, i); continue; } }
            Memory& vm = vo.mem != nullptr ? *vo.mem : m;
            y += vm.read (vo.pos) * vo.env;
            vo.pos += vo.rev ? -vo.rate : vo.rate;
            const double remain = (vo.rev ? vo.pos - vo.a : vo.b - vo.pos) / vo.rate;
            const bool gridEnd = tp.sync && ppqI >= vo.endPpq - (double) xfS * tp.pps;
            if (! vo.handed && (remain <= (double) xfS || gridEnd))
            {
                vo.handed = true;
                notesOff (host, vo, i);
                vo.fadeDir = -1; vo.fadeStep = 1.0f / (float) juce::jmax (1.0, juce::jmin (remain, (double) xfS));
                if (inPhrase && ! stopReq && phraseLeft > 0.0 && &vm == &pmem)
                {
                    const int nx = chooseNext (pmem, v, vo.state);
                    if (nx > 0)
                    {
                        host.segmentStarting (false);
                        v = host.imValues();
                        if (tp.sync) { pendingState = nx; pendingIsPhrase = false; pendingPpq = vo.endPpq; }
                        else startSegment (pmem, host, p, 1 - k, nx, false, hits, i, tp, ppqI);
                    }
                }
            }
            if (remain <= 0.0) { vo.on = false; notesOff (host, vo, i); }
        }
        if (std::abs (shiftRatio - 1.0f) > 1.0e-3f) y = shifter.process (y, shiftRatio, (float) (0.045 * fs));
        out[i] = y * level;
    }

    if (inPhrase && ! voices[0].on && ! voices[1].on && pendingState < 0)
    {
        inPhrase = false; stopReq = false; motifActive = false; motifDrift = false; machActive = false;
        gapLeft = imGapSec (v[IGap]) * fs * (0.5 + rnd());
        if (mode == MContrast) gapLeft *= 0.5 + 2.0 * liveD;     // musiciens tres actifs -> elle laisse de la place
    }
    motifView.store (motifActive && inPhrase);
    machView.store (machActive && inPhrase);
    const bool any = liveOk || corpusOk;
    status.store (! any && N < 5 ? SEmpty : (inPhrase ? SPlay : (playerActive ? SListen : SWait)));
}
} // namespace ma
