#include "Engine.h"
#include "Warp.h"

namespace fragor
{
namespace
{
    constexpr float kPi  = 3.14159265358979f;
    constexpr float kLog2Range = 9.965784f; // log2(20000/20)

    inline double fracd (double x) noexcept { return x - std::floor (x); }
    inline float  clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    inline float  noteToHz (float n) noexcept { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); }
    inline float  hzToNorm (float hz) noexcept { return std::log2 (std::max (20.0f, hz) / 20.0f) / kLog2Range; }
}

// ============================================================================ LFO
float Lfo::shapeValue (int shape, double phd, float sh, float d0, float d1, const float* table) noexcept
{
    const float p = (float) phd;
    switch ((LfoShape) shape)
    {
        case LfoShape::Sine:       return 0.5f + 0.5f * std::sin (2.0f * kPi * p);
        case LfoShape::Triangle:   return 1.0f - std::abs (2.0f * p - 1.0f);
        case LfoShape::SawDown:    return 1.0f - p;
        case LfoShape::SawUp:      return p;
        case LfoShape::Square:     return p < 0.5f ? 1.0f : 0.0f;
        case LfoShape::Wub:        return std::pow (std::max (0.0f, std::sin (kPi * std::min (p, 0.99999f))), 0.6f);
        case LfoShape::Decay:      return std::exp (-6.0f * p);
        case LfoShape::SampleHold: return sh;
        case LfoShape::Drift:
        {
            const float t = 0.5f - 0.5f * std::cos (kPi * p);
            return d0 + (d1 - d0) * t;
        }
        case LfoShape::Custom:
        {
            if (table == nullptr) return 0.0f;
            const float x = clamp01 (p) * (float) (kLfoTableSize - 1);
            const int i = std::min (kLfoTableSize - 2, (int) x);
            const float fr = x - (float) i;
            return table[i] + fr * (table[i + 1] - table[i]);
        }
        case LfoShape::Count: default: return 0.0f;
    }
}

float Lfo::applyCurve (float y, float c) noexcept
{
    y = y < 0.0f ? 0.0f : (y > 1.0f ? 1.0f : y);
    if (c > 0.001f)  return std::pow (y, 1.0f + c * 4.0f);
    if (c < -0.001f) return 1.0f - std::pow (1.0f - y, 1.0f - c * 4.0f);
    return y;
}

// ============================================================================ VoiceFilter
void VoiceFilter::reset()
{
    svf1.reset(); svf2.reset();
    for (auto& f : formant) f.reset();
    for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f);
    combLp[0] = combLp[1] = 0.0f;
    combPos = 0;
}

void VoiceFilter::process (float& l, float& r, int typeI, float cutNorm, float reso, float drive, float morph, float mix, float fs) noexcept
{
    const float dryL = l, dryR = r;
    const float cutHz = juce::jlimit (20.0f, fs * 0.45f, 20.0f * std::exp2 (cutNorm * kLog2Range));

    float x[2] = { l, r };
    if (drive > 0.001f)
    {
        const float g = 1.0f + drive * 14.0f;
        const float makeup = 1.0f - 0.35f * drive;
        x[0] = std::tanh (x[0] * g) * makeup;
        x[1] = std::tanh (x[1] * g) * makeup;
    }

    float y[2] = { x[0], x[1] };
    const auto type = (FltType) typeI;

    switch (type)
    {
        case FltType::LP24:
        {
            const float g = std::tan (kPi * cutHz / fs);
            const float k1 = 2.0f - 1.96f * reso;
            for (int c = 0; c < 2; ++c)
                y[c] = svf2.tick (c, svf1.tick (c, x[c], g, k1).lp, g, 1.414f).lp;
            break;
        }
        case FltType::LP12:
        case FltType::BP:
        case FltType::HP:
        case FltType::Notch:
        {
            const float g = std::tan (kPi * cutHz / fs);
            const float k = 2.0f - 1.97f * reso;
            for (int c = 0; c < 2; ++c)
            {
                const auto o = svf1.tick (c, x[c], g, k);
                y[c] = type == FltType::LP12 ? o.lp
                     : type == FltType::BP   ? o.bp * k * 1.6f
                     : type == FltType::HP   ? o.hp
                                             : o.lp + o.hp;
            }
            break;
        }
        case FltType::Vowel:
        {
            static const float V[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2140, 2950 },
                                           { 450, 800, 2830 },  { 325, 700, 2530 } };
            static const float G[3] = { 1.0f, 0.75f, 0.45f };
            const float m = clamp01 (morph) * 4.0f;
            const int i0 = std::min (3, (int) m);
            const float fr = m - (float) i0;
            const float shift = std::exp2 ((cutNorm - 0.7f) * 3.0f);
            const float k = 0.55f - 0.47f * reso;
            y[0] = y[1] = 0.0f;
            for (int j = 0; j < 3; ++j)
            {
                const float f = std::exp (std::log (V[i0][j]) * (1.0f - fr) + std::log (V[i0 + 1][j]) * fr) * shift;
                const float g = std::tan (kPi * juce::jlimit (30.0f, fs * 0.45f, f) / fs);
                for (int c = 0; c < 2; ++c)
                    y[c] += formant[j].tick (c, x[c], g, k).bp * k * G[j] * 1.8f;
            }
            break;
        }
        case FltType::CombPlus:
        case FltType::CombMinus:
        {
            const int size = (int) comb[0].size();
            if (size < 8) break;
            const float D = juce::jlimit (2.0f, (float) size - 4.0f, fs / cutHz);
            const float fb = (0.25f + 0.73f * reso) * (type == FltType::CombPlus ? 1.0f : -1.0f);
            float rp = (float) combPos - D;
            if (rp < 0.0f) rp += (float) size;
            const int i0 = (int) rp;
            const int i1 = (i0 + 1) % size;
            const float fr = rp - (float) i0;
            // amortissement indépendant du sur-échantillonnage (~ 9 kHz)
            const float damp = 1.0f - std::exp (-2.0f * kPi * 9000.0f / fs);
            for (int c = 0; c < 2; ++c)
            {
                const float d = comb[(size_t) c][(size_t) i0] + fr * (comb[(size_t) c][(size_t) i1] - comb[(size_t) c][(size_t) i0]);
                combLp[c] += damp * (d - combLp[c]);
                float v = x[c] + fb * combLp[c];
                if (std::abs (v) > 1.5f) v = 1.5f * std::tanh (v / 1.5f);
                comb[(size_t) c][(size_t) combPos] = v;
                y[c] = v * (1.0f - 0.6f * std::abs (fb));
            }
            combPos = (combPos + 1) % size;
            break;
        }
        case FltType::Count: default: break;
    }

    l = dryL + (y[0] - dryL) * mix;
    r = dryR + (y[1] - dryR) * mix;
}

// ============================================================================ HalfbandDecimator
void HalfbandDecimator::design (int taps)
{
    N = taps;
    centre = (N - 1) / 2;
    h.assign ((size_t) N, 0.0f);
    const double beta = 8.0;
    auto bessel0 = [] (double x)
    {
        double s = 1.0, t = 1.0;
        for (int k = 1; k < 40; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; }
        return s;
    };
    double sum = 0.0;
    for (int k = 0; k < N; ++k)
    {
        const double m = (double) (k - centre);
        const double sinc = m == 0.0 ? 0.5 : std::sin (juce::MathConstants<double>::pi * 0.5 * m) / (juce::MathConstants<double>::pi * m);
        const double r = (double) (k - centre) / (double) centre;
        const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        h[(size_t) k] = (float) (sinc * w);
        sum += h[(size_t) k];
    }
    nz.clear();
    for (int k = 0; k < N; ++k)
    {
        h[(size_t) k] = (float) (h[(size_t) k] / sum);
        if (std::abs (h[(size_t) k]) > 1.0e-9f) nz.push_back (k);
    }
    hist.assign ((size_t) N * 2, 0.0f);
    pos = 0;
}

void HalfbandDecimator::reset()
{
    std::fill (hist.begin(), hist.end(), 0.0f);
    pos = 0;
}

void HalfbandDecimator::process (const float* in, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            hist[(size_t) pos] = hist[(size_t) (pos + N)] = in[2 * i + j];
            pos = (pos + 1) % N;
        }
        // échantillon le plus récent en hist[pos-1] ; la convolution parcourt la fenêtre [pos, pos+N)
        const float* w = hist.data() + pos;
        float acc = 0.0f;
        for (int k : nz) acc += h[(size_t) k] * w[N - 1 - k];
        out[i] = acc;
    }
}

// ============================================================================ Voice
void Voice::allocate (double maxRate)
{
    const int combSize = (int) (maxRate / 18.0) + 16;
    for (auto& f : filt) f.allocate (combSize);
    rng.setSeedRandomly();
    noiseSeed = (uint32_t) rng.nextInt() | 1u;
    kill();
}

void Voice::setRate (double rate)
{
    sr = rate;
    amp.sr = env2.sr = env3.sr = sr;
    smoothCoef = (float) std::exp (-1.0 / (0.0012 * sr));
    kill();
    for (auto& f : filt) f.reset();
}

void Voice::start (int note, float vel, float glideFrom, int ch, const SynthParams& p, const VoiceCtx& ctx, uint64_t ord)
{
    juce::ignoreUnused (ctx);
    const bool wasActive = active;

    noteNumber = note;
    channel = ch;
    targetNote = (float) note;
    currentNote = (glideFrom >= 0.0f && p.glide > 0.0005f) ? glideFrom : (float) note;
    velocity = vel;
    order = ord;
    randomVal = rng.nextFloat();
    pitchEnv = 1.0f;
    voiceBeats = 0.0;
    voiceTime = 0.0;
    mpeBend = 0.0f; pressure = 0.0f; slide = 0.0f;

    if (! wasActive)
    {
        for (auto& f : filt) f.reset();
        noiseLp = 0.0f;
        lastOsc[0] = lastOsc[1] = lastSub = 0.0f;
        amp.reset(); env2.reset(); env3.reset();
    }

    for (int o = 0; o < 2; ++o)
        for (int v = 0; v < 8; ++v)
            phases[o][v] = p.osc[o].randPhase ? (double) rng.nextFloat() : 0.0;
    subPhase = 0.0;

    for (int l = 0; l < 3; ++l)
    {
        auto& L = lfo[l];
        if (p.lfo[l].retrig || ! wasActive)
        {
            L.phase = p.lfo[l].phase;
            L.lastStepAbs = -1;
            L.first = true;
            L.sh = rng.nextFloat();
            L.drift0 = rng.nextFloat();
            L.drift1 = rng.nextFloat();
        }
    }

    gate = true;
    amp.gateOn(); env2.gateOn(); env3.gateOn();
    active = true;
    snapNext = ! wasActive;
}

void Voice::computeUnison (int o, const OscP& op, float det)
{
    const int U = juce::jlimit (1, 8, op.uni);
    float w[8] {}, pans[8] {};
    float sum2 = 0.0f;
    for (int v = 0; v < U; ++v)
    {
        const float pos = U == 1 ? 0.0f : -1.0f + 2.0f * (float) v / (float) (U - 1);
        detRatio[o][v] = std::exp2 (pos * det * 100.0f / 1200.0f);
        const float centre = 1.0f - std::abs (pos);
        w[v] = U == 1 ? 1.0f : op.blend + (1.0f - op.blend) * centre * centre;
        pans[v] = juce::jlimit (-1.0f, 1.0f, pos * op.spread + op.pan);
        sum2 += w[v] * w[v];
    }
    const float norm = 1.0f / std::sqrt (std::max (1.0e-6f, sum2));
    for (int v = 0; v < U; ++v)
    {
        const float a = (pans[v] + 1.0f) * 0.25f * kPi;
        gainL[o][v] = w[v] * norm * std::cos (a) * 1.41421356f;
        gainR[o][v] = w[v] * norm * std::sin (a) * 1.41421356f;
    }
}

void Voice::control (int n, const SynthParams& p, const VoiceCtx& ctx)
{
    // --- glide
    if (p.glide > 0.0005f)
    {
        const float c = (float) std::exp (-(double) n / (p.glide * 0.3 * sr));
        currentNote = targetNote + (currentNote - targetNote) * c;
    }
    else currentNote = targetNote;

    // --- LFO
    float src[numModSources] {};
    for (int l = 0; l < 3; ++l)
    {
        const LfoP& lp = p.lfo[l];
        Lfo& L = lfo[l];
        const float rateMod = l == 0 ? mods[dstLfo1Rate] : (l == 1 ? mods[dstLfo2Rate] : 0.0f);
        bool forced = false;
        float forcedVal = 0.0f;
        double ph;

        if (l == 0 && p.seqOn)
        {
            const double beats = std::max (0.0, lp.retrig ? voiceBeats : ctx.globalBeats);
            const double stepLen = seqLenBeats (p.seqLen);
            const int64_t stepAbs = (int64_t) std::floor (beats / stepLen + 1.0e-9);
            const int step = (int) (stepAbs % (int64_t) juce::jlimit (1, kNumSeqSteps, p.seqSteps));
            if (stepAbs != L.lastStepAbs)
            {
                L.lastStepAbs = stepAbs;
                if (p.seqReset) L.phase = lp.phase;
            }
            const double b = seqStepBeats (p.seq[step]);
            double hz = 0.0;
            if (b < -1.5)      { forced = true; forcedVal = 1.0f; }
            else if (b < -0.5) { forced = true; forcedVal = 0.0f; }
            else hz = ctx.bpm / 60.0 / b * std::exp2 (rateMod * 4.0f);
            ph = L.phase;
            L.phase = fracd (L.phase + hz * n / sr);
        }
        else if (! lp.retrig && lp.sync)
        {
            ph = fracd (ctx.globalBeats / divisionBeats (lp.div) + lp.phase);
        }
        else if (! lp.retrig)
        {
            ph = fracd (ctx.globalTime * lp.rate + lp.phase);
        }
        else
        {
            double hz = lp.sync ? ctx.bpm / 60.0 / divisionBeats (lp.div) : (double) lp.rate;
            hz *= std::exp2 (rateMod * 4.0f);
            ph = L.phase;
            L.phase = fracd (L.phase + hz * n / sr);
        }

        ph = fracd (ph);
        static thread_local juce::Random lfoRng;
        if (! L.first && ph + 1.0e-9 < L.prevPh)
        {
            L.sh = lfoRng.nextFloat();
            L.drift0 = L.drift1;
            L.drift1 = lfoRng.nextFloat();
        }
        L.prevPh = ph;

        float y = forced ? forcedVal
                         : Lfo::applyCurve (Lfo::shapeValue (lp.shape, ph, L.sh, L.drift0, L.drift1, p.lfoTable[l]), lp.curve);
        if (! std::isfinite (y)) y = 0.0f;
        y = clamp01 (y);
        const float tau = 0.0004f + lp.smooth * lp.smooth * 0.12f;
        const float a = (float) std::exp (-(double) n / (tau * sr));
        if (L.first) { L.smoothed = y; L.first = false; }
        else L.smoothed = y + (L.smoothed - y) * a;
        lfoOut[l] = L.smoothed;
        src[srcLfo1 + l] = L.smoothed;
    }

    // --- enveloppes de modulation
    for (int i = 0; i < n; ++i) { env2.next (p.e2); env3.next (p.e3); }
    pitchEnv *= (float) std::pow (std::exp (-4.6 / (std::max (0.001f, p.penvDecay) * sr)), (double) n);

    src[srcEnv2]       = env2.v;
    src[srcEnv3]       = env3.v;
    src[srcVelocity]   = velocity;
    src[srcModWheel]   = ctx.modWheel;
    src[srcNote]       = juce::jlimit (-1.0f, 1.0f, (currentNote - 60.0f) / 24.0f);
    src[srcRandom]     = randomVal;
    for (int m = 0; m < kNumMacros; ++m) src[srcMacro1 + m] = p.macro[m];
    src[srcPitchEnv]   = pitchEnv;
    src[srcAftertouch] = std::max (pressure, ctx.mpe ? 0.0f : ctx.chanPressure);
    src[srcSlide]      = ctx.mpe ? slide : ctx.slide;

    // --- matrice
    for (auto& m : mods) m = 0.0f;
    for (int s = 0; s < kNumMatrixSlots; ++s)
    {
        const int si = p.mSrc[s], di = p.mDst[s];
        if (si > 0 && si < numModSources && di > 0 && di < numModDests)
            mods[di] += p.mAmt[s] * src[si];
    }

    // --- cibles
    const float base = currentNote + ctx.bendSemis + mpeBend + p.masterTune / 100.0f + p.penvAmt * pitchEnv;
    for (int o = 0; o < 2; ++o)
    {
        const OscP& op = p.osc[o];
        tables[o] = op.table >= WavetableBank::kNumBuiltin
                        ? p.userTable[juce::jlimit (0, kNumUserTables - 1, op.table - WavetableBank::kNumBuiltin)]
                        : &WavetableBank::instance().table (op.table);
        if (tables[o] == nullptr) tables[o] = &WavetableBank::instance().table (0);

        const int dP = o == 0 ? dstAPitch : dstBPitch;
        const float pitch = base + (float) op.oct * 12.0f + (float) op.semi + op.fine / 100.0f + mods[dP] * 24.0f;
        const float inc = std::min (0.45f, noteToHz (pitch) / (float) sr);
        target[sIncA + o] = inc;
        target[sPosA + o]  = clamp01 (op.pos   + mods[o == 0 ? dstAPos   : dstBPos]);
        target[sWarpA + o] = clamp01 (op.warp  + mods[o == 0 ? dstAWarp  : dstBWarp]);
        target[sLvlA + o]  = clamp01 (op.level + mods[o == 0 ? dstALevel : dstBLevel]);
        const float det = clamp01 (op.detune + mods[dstDetune]);
        computeUnison (o, op, det);
        const float maxRatio = std::exp2 (det * 100.0f / 1200.0f);
        mip[o] = WavetableBank::mipForIncrement ((double) inc * maxRatio * warpFreqFactor (op.warpMode, target[sWarpA + o]));
    }

    const float subPitch = base + 12.0f * (float) (p.subOct - 2);
    target[sIncSub] = std::min (0.45f, noteToHz (subPitch) / (float) sr);
    target[sSub]   = p.subOn ? clamp01 (p.subLevel + mods[dstSubLevel]) : 0.0f;
    target[sNoise] = clamp01 (p.noiseLevel + mods[dstNoiseLevel]);

    if (p.noiseColor < 0.5f)
    {
        noiseMode = 0;
        const float fc = 200.0f * std::pow (100.0f, p.noiseColor * 2.0f);
        noiseCoef = 1.0f - std::exp (-2.0f * kPi * fc / (float) sr);
    }
    else
    {
        noiseMode = 1;
        const float fc = 20.0f * std::pow (250.0f, (p.noiseColor - 0.5f) * 2.0f);
        noiseCoef = 1.0f - std::exp (-2.0f * kPi * fc / (float) sr);
    }

    // filtres
    {
        const FiltP& f = p.flt[0];
        target[sCut]   = clamp01 (hzToNorm (f.cutoff) + f.key * (currentNote - 60.0f) / 12.0f / kLog2Range
                                  + f.envAmt * env2.v + mods[dstCutoff]);
        target[sReso]  = clamp01 (f.reso + mods[dstReso]);
        target[sMorph] = clamp01 (f.morph + mods[dstMorph]);
        target[sDrive] = clamp01 (f.drive + mods[dstFltDrive]);
        target[sMix]   = clamp01 (f.mix + mods[dstFltMix]);
    }
    {
        const FiltP& f = p.flt[1];
        target[sCut2]   = clamp01 (hzToNorm (f.cutoff) + f.key * (currentNote - 60.0f) / 12.0f / kLog2Range
                                   + f.envAmt * env3.v + mods[dstCutoff2]);
        target[sReso2]  = clamp01 (f.reso + mods[dstReso2]);
        target[sMorph2] = clamp01 (f.morph + mods[dstMorph2]);
        target[sDrive2] = clamp01 (f.drive);
        target[sMix2]   = clamp01 (f.mix);
    }

    const float velGain = 1.0f - p.ampVel * (1.0f - velocity);
    target[sAmp] = juce::jlimit (0.0f, 2.0f, 1.0f + mods[dstAmp]) * velGain;
    target[sPan] = juce::jlimit (-1.0f, 1.0f, mods[dstPan]);

    if (snapNext)
    {
        for (int k = 0; k < numSmooth; ++k) cur[k] = target[k];
        snapNext = false;
    }

    voiceBeats += (double) n / sr * ctx.bpm / 60.0;
    voiceTime  += (double) n / sr;
}

inline float Voice::renderOsc (int o, const OscP& op, double inc, float pos, float warp, float fmIn, float ringIn,
                               float& outL, float& outR) noexcept
{
    const Wavetable& t = *tables[o];
    const int U = juce::jlimit (1, 8, op.uni);
    const int mode = op.warpMode;
    float mono = 0.0f;
    outL = outR = 0.0f;

    const bool sync = mode == (int) Warp::Sync;
    for (int v = 0; v < U; ++v)
    {
        double& ph = phases[o][v];
        const double vinc = inc * detRatio[o][v];
        ph += vinc;
        bool wrapped = false;
        if (ph >= 1.0) { ph -= std::floor (ph); wrapped = true; }
        const float wp = warpPhase (mode, (float) ph, warp, fmIn);
        float s = WavetableBank::read (t, wp, pos, mip[o]);
        if (sync)
        {
            // polyBLEP sur le saut de redémarrage de l'oscillateur esclave (1 échantillon de retard)
            float out = syncPrev[o][v];
            if (wrapped && vinc > 0.0)
            {
                const float r = 1.0f + warp * 7.0f;
                const float endPh = r - std::floor (r);
                const float sEnd = WavetableBank::read (t, endPh, pos, mip[o]);
                const float s0 = WavetableBank::read (t, 0.0f, pos, mip[o]);
                const float h = s0 - sEnd;
                const float d = juce::jlimit (0.0f, 1.0f, (float) (ph / vinc));
                out += h * d * d * 0.5f;
                s -= h * (1.0f - d) * (1.0f - d) * 0.5f;
            }
            syncPrev[o][v] = s;
            s = out;
        }
        outL += s * gainL[o][v];
        outR += s * gainR[o][v];
        mono += s * (gainL[o][v] + gainR[o][v]) * 0.5f;
    }

    if (mode == (int) Warp::Ring)
    {
        const float m = 1.0f - warp + warp * ringIn;
        outL *= m; outR *= m; mono *= m;
    }
    return mono;
}

void Voice::render (float* L, float* R, int n, const SynthParams& p)
{
    const float c = smoothCoef;
    const float fs = (float) sr;
    const bool f1 = p.flt[0].on, f2 = p.flt[1].on;
    const bool parallel = p.routing == 1 && f1 && f2;

    for (int i = 0; i < n; ++i)
    {
        for (int k = 0; k < numSmooth; ++k)
            cur[k] = target[k] + (cur[k] - target[k]) * c;

        // --- sub
        float sub = 0.0f;
        if (p.subOn)
        {
            subPhase += cur[sIncSub];
            if (subPhase >= 1.0) subPhase -= 1.0;
            const float sp = (float) subPhase;
            switch (p.subShape)
            {
                case 0:  sub = std::sin (2.0f * kPi * sp); break;
                case 1:  sub = 1.0f - 4.0f * std::abs (sp - 0.5f); break;
                default:
                {
                    const float dt = cur[sIncSub];
                    sub = sp < 0.5f ? 1.0f : -1.0f;
                    auto blep = [dt] (float t) -> float
                    {
                        if (t < dt)       { t /= dt; return t + t - t * t - 1.0f; }
                        if (t > 1.0f - dt){ t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
                        return 0.0f;
                    };
                    sub += blep (sp);
                    float t2 = sp + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                    sub -= blep (t2);
                    sub *= 0.8f;
                    break;
                }
            }
        }
        lastSub = sub;

        float l = 0.0f, r = 0.0f;

        if (p.osc[0].on)
        {
            float oL, oR;
            const float fm = p.osc[0].warpMode == (int) Warp::FmSub ? lastSub : lastOsc[1];
            lastOsc[0] = renderOsc (0, p.osc[0], cur[sIncA], cur[sPosA], cur[sWarpA], fm, lastOsc[1], oL, oR);
            l += oL * cur[sLvlA]; r += oR * cur[sLvlA];
        }
        if (p.osc[1].on)
        {
            float oL, oR;
            const float fm = p.osc[1].warpMode == (int) Warp::FmSub ? lastSub : lastOsc[0];
            lastOsc[1] = renderOsc (1, p.osc[1], cur[sIncB], cur[sPosB], cur[sWarpB], fm, lastOsc[0], oL, oR);
            l += oL * cur[sLvlB]; r += oR * cur[sLvlB];
        }

        if (cur[sNoise] > 1.0e-5f)
        {
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            const float w = (float) (int32_t) noiseSeed * (1.0f / 2147483648.0f);
            noiseLp += noiseCoef * (w - noiseLp);
            const float nz = noiseMode == 0 ? noiseLp * (1.0f + (1.0f - noiseCoef)) : (w - noiseLp);
            l += nz * cur[sNoise] * 0.7f;
            r += nz * cur[sNoise] * 0.7f;
        }

        const float subOut = sub * cur[sSub];
        if (! p.subDirect) { l += subOut; r += subOut; }

        // --- filtres (série ou parallèle)
        if (parallel)
        {
            float l2 = l, r2 = r;
            filt[0].process (l, r, p.flt[0].type, cur[sCut], cur[sReso], cur[sDrive], cur[sMorph], cur[sMix], fs);
            filt[1].process (l2, r2, p.flt[1].type, cur[sCut2], cur[sReso2], cur[sDrive2], cur[sMorph2], cur[sMix2], fs);
            l = (l + l2) * 0.7071f;
            r = (r + r2) * 0.7071f;
        }
        else
        {
            if (f1) filt[0].process (l, r, p.flt[0].type, cur[sCut], cur[sReso], cur[sDrive], cur[sMorph], cur[sMix], fs);
            if (f2) filt[1].process (l, r, p.flt[1].type, cur[sCut2], cur[sReso2], cur[sDrive2], cur[sMorph2], cur[sMix2], fs);
        }

        if (p.subDirect) { l += subOut; r += subOut; }

        const float e = amp.next (p.amp);
        float g = e * cur[sAmp] * 0.5f;
        float gl = g, gr = g;
        if (std::abs (cur[sPan]) > 1.0e-4f)
        {
            const float a = (cur[sPan] + 1.0f) * 0.25f * kPi;
            gl *= std::cos (a) * 1.41421356f;
            gr *= std::sin (a) * 1.41421356f;
        }
        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            l = r = 0.0f;
            for (auto& f : filt) f.reset();
            lastOsc[0] = lastOsc[1] = 0.0f;
        }
        L[i] += l * gl;
        R[i] += r * gr;
    }

    if (! amp.active()) active = false;
}

// ============================================================================ Engine
void Engine::prepare (double sampleRate)
{
    baseRate = sampleRate;
    for (auto& v : voices) v.allocate (sampleRate * 4.0);
    decA[0].design (15); decA[1].design (15);
    decB[0].design (95); decB[1].design (95);
    osL.assign ((size_t) kMaxSub * 4, 0.0f);
    osR.assign ((size_t) kMaxSub * 4, 0.0f);
    midL.assign ((size_t) kMaxSub * 2, 0.0f);
    midR.assign ((size_t) kMaxSub * 2, 0.0f);
    const int keep = os;
    os = 0;
    setOversampling (keep);
    reset();
}

void Engine::setOversampling (int factor)
{
    factor = factor >= 4 ? 4 : (factor >= 2 ? 2 : 1);
    if (factor == os) return;
    os = factor;
    for (auto& v : voices) v.setRate (baseRate * os);
    for (auto& d : decA) d.reset();
    for (auto& d : decB) d.reset();
    monoStack.clear();
}

float Engine::latencySamples() const
{
    if (os == 1) return 0.0f;
    const float b = decB[0].latencyIn() * 0.5f;                       // en échantillons de base
    return os == 2 ? b : b + decA[0].latencyIn() * 0.25f;
}

void Engine::reset()
{
    for (auto& v : voices) v.kill();
    monoStack.clear();
    sustained.clear();
    pedal = false;
    lastNote = -1.0f;
    for (auto& g : globalMod) g = 0.0f;
    for (auto& d : decA) d.reset();
    for (auto& d : decB) d.reset();
}

void Engine::allNotesOff()
{
    for (auto& v : voices) v.release();
    monoStack.clear();
    sustained.clear();
}

void Engine::noteOn (int note, float vel, int ch, const SynthParams& p, const VoiceCtx& ctx)
{
    if (p.voiceMode != lastMode)
    {
        if (lastMode != -1) for (auto& v : voices) v.release();
        monoStack.clear();
        lastMode = p.voiceMode;
    }

    sustained.erase (std::remove (sustained.begin(), sustained.end(), note), sustained.end());

    if (p.voiceMode == (int) VoiceMode::Poly)
    {
        Voice* target = nullptr;
        for (auto& v : voices) if (v.active && v.noteNumber == note && v.channel == ch) { target = &v; break; }
        if (target == nullptr) for (auto& v : voices) if (! v.active) { target = &v; break; }
        if (target == nullptr)
        {
            for (auto& v : voices) if (! v.gate && (target == nullptr || v.order < target->order)) target = &v;
            if (target == nullptr)
                for (auto& v : voices) if (target == nullptr || v.order < target->order) target = &v;
        }
        target->start (note, vel, lastNote, ch, p, ctx, ++counter);
    }
    else
    {
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        const bool othersHeld = ! monoStack.empty();
        monoStack.push_back (note);
        Voice& v = voices[0];
        if (v.active && v.gate && othersHeld && p.voiceMode == (int) VoiceMode::Legato)
        {
            v.legatoTo (note);
            v.channel = ch;
        }
        else
            v.start (note, vel, lastNote, ch, p, ctx, ++counter);
    }
    lastNote = (float) note;
}

void Engine::noteOff (int note, int ch, const SynthParams& p)
{
    if (p.voiceMode == (int) VoiceMode::Poly)
    {
        if (pedal) { sustained.push_back (note); return; }
        for (auto& v : voices) if (v.active && v.gate && v.noteNumber == note && (v.channel == ch || ch <= 0)) v.release();
    }
    else
    {
        monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
        Voice& v = voices[0];
        if (monoStack.empty())
        {
            if (pedal) { sustained.push_back (note); return; }
            if (v.noteNumber == note) v.release();
        }
        else if (v.noteNumber == note)
        {
            v.legatoTo (monoStack.back());
            lastNote = (float) monoStack.back();
        }
    }
}

void Engine::sustainPedal (bool down, const SynthParams& p)
{
    juce::ignoreUnused (p);
    pedal = down;
    if (! down)
    {
        for (int n : sustained)
            for (auto& v : voices) if (v.active && v.gate && v.noteNumber == n) v.release();
        sustained.clear();
    }
}

void Engine::channelBend (int ch, float semis)        { for (auto& v : voices) if (v.active && v.channel == ch) v.mpeBend = semis; }
void Engine::polyPressure (int note, float val)       { for (auto& v : voices) if (v.active && v.noteNumber == note) v.pressure = val; }
void Engine::channelPressure (int ch, float val, bool mpe)
{
    if (mpe) for (auto& v : voices) if (v.active && v.channel == ch) v.pressure = val;
}
void Engine::channelSlide (int ch, float val, bool mpe)
{
    if (mpe) for (auto& v : voices) if (v.active && v.channel == ch) v.slide = val;
}

void Engine::render (float* L, float* R, int n, const SynthParams& p, const VoiceCtx& ctx)
{
    Voice* newest = nullptr;
    const int on = n * os;
    float* bl = os == 1 ? L : osL.data();
    float* br = os == 1 ? R : osR.data();
    if (os > 1) { std::fill (bl, bl + on, 0.0f); std::fill (br, br + on, 0.0f); }

    for (auto& v : voices)
    {
        if (! v.active) continue;
        v.control (on, p, ctx);
        v.render (bl, br, on, p);
        if (newest == nullptr || v.order > newest->order) newest = &v;
    }

    if (os == 2)
    {
        float tl[kMaxSub], tr[kMaxSub];
        decB[0].process (bl, tl, n);
        decB[1].process (br, tr, n);
        for (int i = 0; i < n; ++i) { L[i] += tl[i]; R[i] += tr[i]; }
    }
    else if (os == 4)
    {
        decA[0].process (bl, midL.data(), 2 * n);
        decA[1].process (br, midR.data(), 2 * n);
        float tl[kMaxSub], tr[kMaxSub];
        decB[0].process (midL.data(), tl, n);
        decB[1].process (midR.data(), tr, n);
        for (int i = 0; i < n; ++i) { L[i] += tl[i]; R[i] += tr[i]; }
    }

    for (int d = 0; d < numModDests; ++d)
    {
        const float m = newest != nullptr ? newest->mods[d] : 0.0f;
        globalMod[d] = isGlobalDest (d) ? m : 0.0f;
        liveMod[d] = m;
    }
    if (newest != nullptr)
    {
        for (int l = 0; l < 3; ++l) lfoMonitor[l] = newest->getLfo (l);
        const int64_t s = newest->getSeqStepAbs();
        seqMonitor = (p.seqOn && s >= 0) ? (int) (s % juce::jlimit (1, kNumSeqSteps, p.seqSteps)) : -1;
    }
    else
    {
        seqMonitor = -1;
    }
}

int Engine::activeVoiceCount() const
{
    int c = 0;
    for (auto& v : voices) if (v.active) ++c;
    return c;
}
} // namespace fragor
