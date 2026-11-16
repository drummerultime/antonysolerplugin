#include "Effects.h"

namespace fragor
{
namespace
{
    constexpr float kPi = 3.14159265358979f;
    inline float dbToGain (float db) noexcept { return std::exp (db * 0.11512925f); }
    inline float gainToDb (float g)  noexcept { return 8.6858896f * std::log (g + 1.0e-9f); }

    inline float shape (int mode, float z) noexcept
    {
        switch ((DistMode) mode)
        {
            case DistMode::Tube:      return std::tanh (z);
            case DistMode::HardClip:  return juce::jlimit (-1.0f, 1.0f, z);
            case DistMode::Foldback:
            {
                const float q = 0.25f * z + 0.25f;
                return 4.0f * std::abs (q - std::round (q)) - 1.0f;
            }
            case DistMode::SineFold:  return std::sin (z * kPi * 0.5f);
            case DistMode::Rectify:   return 1.6f * std::abs (std::tanh (z));
            case DistMode::Diode:     return z >= 0.0f ? 1.0f - std::exp (-z) : -0.45f * (1.0f - std::exp (z * 1.6f));
            case DistMode::Overdrive:
            {
                const float c = juce::jlimit (-1.5f, 1.5f, z);
                return c - c * c * c / 6.75f;
            }
            case DistMode::Count: default: return z;
        }
    }
}

// ============================================================================ Distortion
void Distortion::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    os->initProcessing ((size_t) std::max (64, maxBlock));
    lat = (int) std::round (os->getLatencyInSamples());
    for (auto& b : dryBuf) b.assign ((size_t) std::max (64, maxBlock) + (size_t) lat + 8, 0.0f);
    reset();
}

void Distortion::reset()
{
    if (os) os->reset();
    for (auto& b : dryBuf) std::fill (b.begin(), b.end(), 0.0f);
    dryPos = 0;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    dcX2[0] = dcX2[1] = dcY2[0] = dcY2[1] = 0.0f;
    t1[0] = t1[1] = t2[0] = t2[1] = 0.0f;
    mixR.init = false;
}

void Distortion::process (float* L, float* R, int n, bool on, int mode, float driveNorm, float bias, float toneHz, float mix)
{
    float* ch[2] = { L, R };
    const int size = (int) dryBuf[0].size();

    // ligne de retard du signal sec (latence constante, même effet coupé)
    float dryD[2][64];
    for (int i = 0; i < n; ++i)
    {
        int rp = dryPos - lat;
        if (rp < 0) rp += size;
        for (int c = 0; c < 2; ++c)
        {
            dryBuf[c][(size_t) dryPos] = ch[c][i];
            dryD[c][i] = dryBuf[c][(size_t) rp];
        }
        dryPos = (dryPos + 1) % size;
    }

    if (! on)
    {
        for (int c = 0; c < 2; ++c) std::copy (dryD[c], dryD[c] + n, ch[c]);
        return;
    }

    juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
    auto up = os->processSamplesUp (block);
    const int un = (int) up.getNumSamples();

    const float targetGain = dbToGain (juce::jlimit (0.0f, 1.0f, driveNorm) * 42.0f);
    const float targetBias = bias * 0.6f;
    const float sc = (float) std::exp (-1.0 / (0.003 * sr * 4.0));
    for (int i = 0; i < un; ++i)
    {
        driveSm = targetGain + (driveSm - targetGain) * sc;
        biasSm  = targetBias + (biasSm - targetBias) * sc;
        const float offset = shape (mode, biasSm);
        for (int c = 0; c < 2; ++c)
        {
            float* d = up.getChannelPointer ((size_t) c);
            d[i] = shape (mode, d[i] * driveSm + biasSm) - offset;
        }
    }
    os->processSamplesDown (block);

    // tonalité (LP 12 dB) + bloqueur de continu + mélange
    const float g = std::tan (kPi * juce::jlimit (200.0f, (float) sr * 0.45f, toneHz) / (float) sr);
    const float k = 1.414f;
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float R_ = 1.0f - (float) (2.0 * kPi * 12.0 / sr);
    mixR.setTarget (mix, n);
    for (int i = 0; i < n; ++i)
    {
        const float m = mixR.next();
        for (int c = 0; c < 2; ++c)
        {
            const float v0 = ch[c][i];
            const float v3 = v0 - t2[c];
            const float v1 = a1 * t1[c] + a2 * v3;
            const float v2 = t2[c] + a2 * t1[c] + a3 * v3;
            t1[c] = 2.0f * v1 - t1[c];
            t2[c] = 2.0f * v2 - t2[c];
            const float y = v2 - dcX[c] + R_ * dcY[c];
            dcX[c] = v2;
            dcY[c] = y;
            ch[c][i] = dryD[c][i] + (y - dryD[c][i]) * m;
        }
    }
}

// ============================================================================ Crusher
void Crusher::process (float* L, float* R, int n, float bits, float rate, float mix)
{
    const float steps = std::exp2 (juce::jlimit (1.0f, 16.0f, bits) - 1.0f);
    const float hold = 1.0f + rate * rate * 48.0f;
    mixR.setTarget (mix, n);
    for (int i = 0; i < n; ++i)
    {
        const float m = mixR.next();
        counter += 1.0f;
        if (counter >= hold)
        {
            counter -= hold;
            held[0] = std::round (L[i] * steps) / steps;
            held[1] = std::round (R[i] * steps) / steps;
        }
        L[i] += (held[0] - L[i]) * m;
        R[i] += (held[1] - R[i]) * m;
    }
}

// ============================================================================ OTT
void Ott::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) std::max (64, maxBlock), 2 };
    xo1.prepare (spec); xo2.prepare (spec);
    xo1.setCutoffFrequency (110.0f);
    xo2.setCutoffFrequency (2500.0f);
    reset();
}

void Ott::reset()
{
    xo1.reset(); xo2.reset();
    env[0] = env[1] = env[2] = 0.0f;
    depthR.init = false;
}

void Ott::process (float* L, float* R, int n, float depth, float time, float up, float down, float outDb)
{
    const float attMs = 0.4f + time * time * 30.0f;
    const float relMs = 15.0f + time * time * 900.0f;
    const float ac = std::exp (-1.0f / (attMs * 0.001f * (float) sr));
    const float rc = std::exp (-1.0f / (relMs * 0.001f * (float) sr));
    const float outG = dbToGain (outDb);
    static const float makeup[3] = { 5.0f, 2.5f, 6.0f };
    constexpr float thrDown = -26.0f, ratioDown = 30.0f;
    constexpr float thrUp = -40.0f, ratioUp = 3.5f;

    depthR.setTarget (juce::jlimit (0.0f, 1.0f, depth), n);
    for (int i = 0; i < n; ++i)
    {
        const float d = depthR.next();
        float band[3][2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = c == 0 ? L[i] : R[i];
            float lo, hi, mid, hh;
            xo1.processSample (c, x, lo, hi);
            xo2.processSample (c, hi, mid, hh);
            band[0][c] = lo; band[1][c] = mid; band[2][c] = hh;
        }
        float outL = 0.0f, outR = 0.0f;
        for (int b = 0; b < 3; ++b)
        {
            const float lvl = std::max (std::abs (band[b][0]), std::abs (band[b][1]));
            env[b] = lvl > env[b] ? lvl + (env[b] - lvl) * ac : lvl + (env[b] - lvl) * rc;
            const float e = gainToDb (env[b]);
            float gr = 0.0f;
            if (e > thrDown) gr -= (e - thrDown) * (1.0f - 1.0f / ratioDown) * down;
            if (e < thrUp && e > -85.0f) gr += std::min (24.0f, (thrUp - e) * (1.0f - 1.0f / ratioUp)) * up;
            // mélange en phase : chaque bande passe de 1 (sec) à son gain compressé
            const float g = 1.0f + (dbToGain (gr + makeup[b]) - 1.0f) * d;
            outL += band[b][0] * g;
            outR += band[b][1] * g;
        }
        L[i] = outL * outG;
        R[i] = outR * outG;
    }
}

// ============================================================================ Delay
void SyncDelay::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& b : buf) b.assign ((size_t) (sr * 4.5) + 4, 0.0f);
    reset();
}

void SyncDelay::reset()
{
    for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
    pos = 0; dSm = -1.0; lp[0] = lp[1] = 0.0f;
    mixR.init = false;
}

void SyncDelay::process (float* L, float* R, int n, double delaySamples, float fb, float toneHz, bool ping, float mix)
{
    const int size = (int) buf[0].size();
    delaySamples = juce::jlimit (1.0, (double) size - 4.0, delaySamples);
    if (dSm < 0.0) dSm = delaySamples;
    const float tc = 1.0f - std::exp (-2.0f * kPi * toneHz / (float) sr);
    const double sc = std::exp (-1.0 / (0.05 * sr));
    mixR.setTarget (mix, n);

    for (int i = 0; i < n; ++i)
    {
        dSm = delaySamples + (dSm - delaySamples) * sc;
        double rp = (double) pos - dSm;
        if (rp < 0.0) rp += size;
        const int i0 = (int) rp, i1 = (i0 + 1) % size;
        const float fr = (float) (rp - i0);
        float d[2];
        for (int c = 0; c < 2; ++c)
        {
            const float v = buf[c][(size_t) i0] + fr * (buf[c][(size_t) i1] - buf[c][(size_t) i0]);
            lp[c] += tc * (v - lp[c]);
            d[c] = lp[c];
        }
        const float inL = L[i], inR = R[i];
        if (ping)
        {
            buf[0][(size_t) pos] = std::tanh ((inL + inR) * 0.5f + d[1] * fb);
            buf[1][(size_t) pos] = std::tanh (d[0] * fb);
        }
        else
        {
            buf[0][(size_t) pos] = std::tanh (inL + d[0] * fb);
            buf[1][(size_t) pos] = std::tanh (inR + d[1] * fb);
        }
        pos = (pos + 1) % size;
        const float m = mixR.next();
        L[i] = inL + d[0] * m;
        R[i] = inR + d[1] * m;
    }
}

// ============================================================================ FxChain
void FxChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    dist.prepare (sr, maxBlock);
    dist2.prepare (sr, maxBlock);
    ott.prepare (sr, maxBlock);
    delay.prepare (sr);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) std::max (64, maxBlock), 2 };
    chorus.prepare (spec);
    chorus.setCentreDelay (8.0f);
    chorus.setFeedback (0.15f);
    reverb.setSampleRate (sr);
    reset();
}

void FxChain::reset()
{
    dist.reset(); dist2.reset(); crush.reset(); ott.reset(); delay.reset();
    chorus.reset(); reverb.reset();
    sideIc1 = sideIc2 = 0.0f;
    dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    dcX2[0] = dcX2[1] = dcY2[0] = dcY2[1] = 0.0f;
    widthR.init = gainR.init = revMixR.init = false;
}

void FxChain::process (float* L, float* R, int n, const FxParams& p, const float* gm, double bpm)
{
    for (int slot = 0; slot < numFxModules; ++slot)
    {
        switch (p.order[slot])
        {
            case fxDist1:
                dist.process (L, R, n, p.distOn, p.distMode,
                              juce::jlimit (0.0f, 1.0f, p.distDrive + gm[dstDistDrive]),
                              p.distBias, p.distTone,
                              juce::jlimit (0.0f, 1.0f, p.distMix + gm[dstDistMix]));
                break;
            case fxDist2:
                dist2.process (L, R, n, p.dist2On, p.dist2Mode,
                               juce::jlimit (0.0f, 1.0f, p.dist2Drive + gm[dstDist2Drive]),
                               p.dist2Bias, p.dist2Tone,
                               juce::jlimit (0.0f, 1.0f, p.dist2Mix + gm[dstDist2Mix]));
                break;
            case fxCrush:
            {
                const float crushAmt = gm[dstCrush];
                if (p.crushOn || crushAmt > 0.001f)
                    crush.process (L, R, n,
                                   p.crushOn ? p.crushBits - crushAmt * 12.0f : 16.0f - crushAmt * 14.0f,
                                   juce::jlimit (0.0f, 1.0f, (p.crushOn ? p.crushRate : 0.0f) + crushAmt),
                                   p.crushOn ? p.crushMix : 1.0f);
                break;
            }
            case fxOtt:
                if (p.ottOn)
                    ott.process (L, R, n, juce::jlimit (0.0f, 1.0f, p.ottDepth + gm[dstOttDepth]),
                                 p.ottTime, p.ottUp, p.ottDown, p.ottOut);
                break;
            case fxChorus:
                if (p.chorusOn)
                {
                    if (! chorusWasOn) chorus.reset();
                    chorus.setRate (p.chorusRate);
                    chorus.setDepth (p.chorusDepth);
                    chorus.setMix (p.chorusMix);
                    float* ch[2] = { L, R };
                    juce::dsp::AudioBlock<float> block (ch, 2, (size_t) n);
                    chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
                }
                chorusWasOn = p.chorusOn;
                break;
            case fxDelay:
            {
                const float dMix = juce::jlimit (0.0f, 1.0f, p.dlyMix + gm[dstDelayMix]);
                if (p.dlyOn)
                    delay.process (L, R, n, divisionBeats (p.dlyDiv) * 60.0 / std::max (20.0, bpm) * sr, p.dlyFb, p.dlyTone, p.dlyPing, dMix);
                break;
            }
            case fxReverb:
                if (p.revOn)
                {
                    if (! revWasOn) reverb.reset();
                    const float m = juce::jlimit (0.0f, 1.0f, p.revMix + gm[dstReverbMix]);
                    juce::Reverb::Parameters rp;
                    rp.roomSize = p.revSize;
                    rp.damping  = p.revDamp;
                    rp.width    = p.revWidth;
                    rp.wetLevel = m / 3.0f * 1.2f;
                    rp.dryLevel = (1.0f - 0.45f * m) * 0.5f;
                    rp.freezeMode = 0.0f;
                    reverb.setParameters (rp);
                    reverb.processStereo (L, R, n);
                }
                revWasOn = p.revOn;
                break;
            default: break;
        }
    }

    // --- master : largeur, basses mono, clipper, gain
    const float hg = std::tan (kPi * p.subMonoFreq / (float) sr);
    const float hk = 1.414f;
    const float a1 = 1.0f / (1.0f + hg * (hg + hk)), a2 = hg * a1, a3 = hg * a2;
    const float dcR = 1.0f - (float) (2.0 * kPi * 8.0 / sr);
    const float clipDrive = 1.0f + p.clip * 5.0f;
    const float clipNorm = 0.94f / std::tanh (clipDrive);
    widthR.setTarget (p.width, n);
    gainR.setTarget (dbToGain (p.masterGain), n);

    for (int i = 0; i < n; ++i)
    {
        const float w = widthR.next();
        const float g = gainR.next();
        const float m = 0.5f * (L[i] + R[i]);
        float s = 0.5f * (L[i] - R[i]) * w;
        if (p.subMono)
        {
            const float v3 = s - sideIc2;
            const float v1 = a1 * sideIc1 + a2 * v3;
            const float v2 = sideIc2 + a2 * sideIc1 + a3 * v3;
            sideIc1 = 2.0f * v1 - sideIc1;
            sideIc2 = 2.0f * v2 - sideIc2;
            s = s - hk * v1 - v2;
        }
        float l = (m + s) * g, r = (m - s) * g;
        {
            const float yl = l - dcX[0] + dcR * dcY[0]; dcX[0] = l; dcY[0] = yl; l = yl;
            const float yr = r - dcX[1] + dcR * dcY[1]; dcX[1] = r; dcY[1] = yr; r = yr;
        }
        if (p.clip > 0.001f)
        {
            l = std::tanh (l * clipDrive) * clipNorm;
            r = std::tanh (r * clipDrive) * clipNorm;
        }
        {
            const float yl = l - dcX2[0] + dcR * dcY2[0]; dcX2[0] = l; dcY2[0] = yl; l = yl;
            const float yr = r - dcX2[1] + dcR * dcY2[1]; dcX2[1] = r; dcY2[1] = yr; r = yr;
        }
        if (! std::isfinite (l)) l = 0.0f;
        if (! std::isfinite (r)) r = 0.0f;
        L[i] = juce::jlimit (-4.0f, 4.0f, l);
        R[i] = juce::jlimit (-4.0f, 4.0f, r);
    }
}
} // namespace fragor
