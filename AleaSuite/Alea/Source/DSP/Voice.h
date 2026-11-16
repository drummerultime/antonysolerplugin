#pragma once

#include "../Common.h"

namespace alea
{
// Echantillon charge pour le moteur Sample (mono, normalise)
struct SampleData
{
    juce::AudioBuffer<float> data;   // 1 canal
    double sampleRate = 44100.0;
    juce::String path, name;
};

// Valeurs reelles resolues au moment du declenchement (apres application de l'alea)
struct VoiceSettings
{
    int   engine = EngKick;
    float freq = 50.0f, decay = 0.3f, attack = 0.0f;
    float pitchAmt = 0.0f, pitchDecay = 0.02f;
    float timbre = 0.0f, color = 0.0f, noise = 0.0f;
    float cutoff = 20000.0f, reso = 0.0f; int ftype = 0; float fenvOct = 0.0f;
    float drive = 0.0f, crush = 0.0f, punch = 0.0f;
    float gain = 1.0f, pan = 0.0f, sendDly = 0.0f, sendRev = 0.0f;
    const SampleData* sample = nullptr;
    int fxType = 0; float fxAmt = 0.0f;     // insert : 0 off, 1 fold, 2 comb, 3 ring, 4 tape
    float layer = 0.0f;                     // niveau de la couche echantillon (moteurs de synthese)
    float tuneRatio = 1.0f;                 // rapport d'accordage (pour la couche, le comb, le ring)
    float slideRatio = 1.0f, glideSec = 0.05f;   // glissement depuis la hauteur precedente
};

// Filtre a variable d'etat (topologie TPT / Cytomic)
struct SVF
{
    float ic1 = 0, ic2 = 0, a1 = 1, a2 = 0, a3 = 0, k = 1;

    void reset() noexcept { ic1 = ic2 = 0; }
    void set (float fc, float q, float fs) noexcept
    {
        fc = juce::jlimit (16.0f, fs * 0.45f, fc);
        const float g = std::tan (juce::MathConstants<float>::pi * fc / fs);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void process (float v0, float& lp, float& bp, float& hp) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
};

// Correction PolyBLEP (reduit le repliement des formes d'onde a discontinuite)
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)        { t /= dt;          return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

// ln(cosh(u)), primitive de tanh, pour la saturation anti-repliement (ADAA 1er ordre)
inline float logCosh (float u) noexcept
{
    const float a = std::abs (u);
    return a + std::log1p (std::exp (-2.0f * a)) - 0.69314718f;
}

class DrumVoice
{
public:
    static constexpr int ksSize = 8192;

    void prepare (double sampleRate, uint32_t seed) { fs = (float) sampleRate; rng = FastRng (seed); active = false; }

    bool isActive() const noexcept { return active; }

    // relachement rapide (re-declenchement de la meme piste ou choke)
    void release (float ms = 4.0f) noexcept
    {
        if (! active) return;
        releasing = true;
        relCoef = std::exp (-1.0f / (ms * 0.001f * fs));
    }

    void trigger (const VoiceSettings& vs) noexcept
    {
        s = vs;
        active = true; releasing = false; relEnv = 1.0f;

        attackLen = (int) (s.attack * fs); attackPos = 0;
        holdLen = 0; holdPos = 0;

        float ampDecay = s.decay;
        if (s.engine == EngModal || s.engine == EngPluck) ampDecay *= 2.0f;   // le resonateur sculpte deja la decroissance
        ampEnv = 1.0f;
        ampCoef = std::exp (std::log (0.001f) / juce::jmax (0.002f, ampDecay) / fs);

        pEnv = 1.0f; pCoef = std::exp (-1.0f / (juce::jmax (0.0005f, s.pitchDecay) * fs));
        fEnv = 1.0f; fCoef = std::exp (-1.0f / (juce::jmax (0.002f, s.decay * 0.4f) * fs));
        noiseEnv = 1.0f; noiseCoef = std::exp (-1.0f / (juce::jmax (0.002f, s.decay * 0.35f) * fs));
        tEnv = 1.0f; tCoef = std::exp (-1.0f / (0.006f * fs));

        ph1 = ph2 = phMod = 0.0f;
        for (auto& p : metalPh) p = rng.uni();
        hpState = 0; hpPrev = 0; lpState = 0;
        clickEnv = 1.0f; clickCoef = std::exp (-1.0f / (0.0008f * fs));
        prevU = 0.0f;
        sampleEnded = false;
        slideLog = (s.slideRatio > 0.0f && std::abs (s.slideRatio - 1.0f) > 1.0e-4f) ? std::log2 (s.slideRatio) : 0.0f;
        gEnv = 1.0f; gCoef = std::exp (-1.0f / (juce::jmax (0.002f, s.glideSec * 0.35f) * fs));
        layerPos = 0.0;
        if (s.fxType == 2) { std::fill (std::begin (comb), std::end (comb), 0.0f); combW = 0; }
        ringPh = 0.0f; tapeLp = 0.0f;

        switch (s.engine)
        {
            case EngSnare:
                toneEnv = 1.0f; toneCoef = std::exp (-1.0f / (juce::jmax (0.002f, s.decay * 0.25f) * fs));
                snapCoefHP = std::exp (-2.0f * juce::MathConstants<float>::pi * (1000.0f + s.color * 7000.0f) / fs);
                break;
            case EngMetal:
                metalHP.reset();
                metalHP.set (2000.0f + s.color * 8000.0f, 0.9f, fs);
                break;
            case EngClap:
            {
                clapSpacing = juce::jmax (8, (int) ((0.004f + s.timbre * 0.02f) * fs));
                clapPos = 0; burstEnv = 1.0f;
                burstCoef = std::exp (-1.0f / (0.0025f * fs));
                holdLen = clapSpacing * 3;
                const float fc = (700.0f + s.color * 2800.0f) * s.freq;   // pour Clap, freq = ratio d'accordage
                clapBP.reset();
                clapBP.set (fc, 2.0f, fs);
                clapGain = 1.8f * std::pow (1800.0f / juce::jmax (100.0f, fc), 0.35f);
                break;
            }
            case EngFM:
                idxEnv = 1.0f; idxCoef = std::exp (-1.0f / ((s.decay * 0.3f + 0.002f) * fs));
                modRatio = 0.5f + s.color * 7.5f;
                break;
            case EngModal:
                exLen = juce::jmax (4, (int) ((0.0005f + (1.0f - s.color) * 0.008f) * fs));
                exPos = 0;
                for (int m = 0; m < 4; ++m) { y1[m] = y2[m] = 0; }
                updateModal (1.0f);
                break;
            case EngPluck:
                std::fill (std::begin (ks), std::end (ks), 0.0f);
                ksW = 0; ksPrev = 0;
                exLen = juce::jlimit (8, ksSize / 2, (int) (fs / juce::jmax (20.0f, s.freq)));
                exPos = 0;
                break;
            case EngZap:
                zEnv = 1.0f; zCoef = std::exp (-1.0f / (juce::jmax (0.001f, s.pitchDecay * 0.6f) * fs));
                break;
            case EngHybrid:
                clapBP.reset();
                clapBP.set (2500.0f, 1.2f, fs);
                snapCoefHP = std::exp (-2.0f * juce::MathConstants<float>::pi * 3000.0f / fs);
                clickCoef = std::exp (-1.0f / (0.0015f * fs));
                break;
            case EngSample:
                if (s.sample != nullptr && s.sample->data.getNumSamples() > 4)
                {
                    const int n = s.sample->data.getNumSamples();
                    reverse = s.timbre > 0.5f;
                    const double start = s.color * 0.5 * n;
                    samplePos = reverse ? (double) (n - 2) - start : start;
                }
                else sampleEnded = true;
                break;
            default: break;
        }

        // filtre de voix
        filt.reset();
        q = 0.5f + s.reso * s.reso * 15.0f;
        filt.set (s.cutoff * std::exp2 (s.fenvOct), q, fs);
        filterCounter = 0;

        // saturation
        driveG = 1.0f + s.drive * s.drive * 30.0f;
        driveNorm = 1.0f / std::tanh (driveG);

        // bitcrusher
        crushLevels = std::pow (2.0f, 16.0f - s.crush * 13.0f) * 0.5f;
        crushHold = 1 + (int) (s.crush * s.crush * 30.0f);
        crushCount = 0; crushHeld = 0;

        const float a = (s.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        panL = std::cos (a); panR = std::sin (a);
    }

    // ajoute la sortie de la voix dans les bus (L/R, envoi delay, envoi reverb)
    inline void process (float& L, float& R, float& D, float& V) noexcept
    {
        if (! active) return;

        float amp;
        bool decaying = false;
        if (attackPos < attackLen)      { amp = (float) attackPos / (float) attackLen; ++attackPos; }
        else if (holdPos < holdLen)     { amp = 1.0f; ++holdPos; }
        else                            { amp = ampEnv; ampEnv *= ampCoef; decaying = true; }

        if (releasing)
        {
            relEnv *= relCoef;
            if (relEnv < 1.0e-4f) { active = false; return; }
        }

        const float penv = pEnv; pEnv *= pCoef;
        float pf = s.pitchAmt > 0.0f ? std::exp2 (s.pitchAmt * penv * (1.0f / 12.0f)) : 1.0f;
        if (slideLog != 0.0f) { pf *= std::exp2 (slideLog * gEnv); gEnv *= gCoef; }
        const float f  = s.freq * pf;
        const float n  = rng.bi();
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        float x = 0.0f;
        switch (s.engine)
        {
            case EngKick:
            {
                ph1 += f / fs; if (ph1 >= 1.0f) ph1 -= 1.0f;
                const float sn = std::sin (twoPi * ph1);
                const float g = 1.0f + s.timbre * 8.0f;
                x = s.timbre > 0.001f ? std::tanh (sn * g) / std::tanh (g) : sn;
                x += s.color * clickEnv * (0.6f * n + 1.0f) * 0.8f;
                clickEnv *= clickCoef;
                break;
            }
            case EngSnare:
            {
                ph1 += f / fs;          if (ph1 >= 1.0f) ph1 -= 1.0f;
                ph2 += f * 1.47f / fs;  if (ph2 >= 1.0f) ph2 -= 1.0f;
                const float tone = (std::sin (twoPi * ph1) + 0.5f * std::sin (twoPi * ph2)) * toneEnv;
                toneEnv *= toneCoef;
                hpState = snapCoefHP * (hpState + n - hpPrev); hpPrev = n;
                const float snap = hpState * noiseEnv;
                x = tone * (1.0f - s.timbre) + snap * s.timbre * 1.9f;
                break;
            }
            case EngMetal:
            {
                static constexpr float base[6]   { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
                static constexpr float spread[6] { 0.0f, 0.13f, -0.21f, 0.37f, -0.11f, 0.29f };
                float sum = 0.0f;
                for (int i = 0; i < 6; ++i)
                {
                    const float dt = juce::jmin (0.45f, base[i] * f * (1.0f + s.timbre * spread[i]) / fs);
                    metalPh[i] += dt;
                    metalPh[i] -= std::floor (metalPh[i]);
                    float sq = metalPh[i] < 0.5f ? 1.0f : -1.0f;
                    sq += polyBlep (metalPh[i], dt);
                    float t2 = metalPh[i] + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                    sq -= polyBlep (t2, dt);
                    sum += sq;
                }
                float lp, bp, hp;
                metalHP.process (sum * (1.0f / 6.0f), lp, bp, hp);
                x = hp * (1.5f + s.color * 3.0f);
                break;
            }
            case EngClap:
            {
                float lp, bp, hp;
                clapBP.process (n, lp, bp, hp);
                if (clapPos == clapSpacing || clapPos == clapSpacing * 2) burstEnv = 1.0f;
                const float e = clapPos < clapSpacing * 3 ? burstEnv : 1.0f;
                burstEnv *= burstCoef;
                ++clapPos;
                x = bp * e * clapGain;
                break;
            }
            case EngFM:
            {
                phMod += f * modRatio / fs; phMod -= std::floor (phMod);
                ph1 += f / fs;              ph1 -= std::floor (ph1);
                const float index = s.timbre * 12.0f * idxEnv;
                idxEnv *= idxCoef;
                x = std::sin (twoPi * ph1 + index * std::sin (twoPi * phMod));
                break;
            }
            case EngModal:
            {
                float e = 0.0f;
                if (exPos < exLen) { e = n * (1.0f - (float) exPos / (float) exLen); ++exPos; }
                lpState += (0.05f + s.color * 0.9f) * (e - lpState);
                if (s.pitchAmt > 0.0f && (filterCounter & 15) == 0) updateModal (pf);
                float sum = 0.0f;
                for (int m = 0; m < 4; ++m)
                {
                    const float y = b1[m] * y1[m] - b2[m] * y2[m] + lpState * inGain[m];
                    y2[m] = y1[m]; y1[m] = y;
                    sum += y * modeAmp[m];
                }
                x = sum * 0.9f;
                break;
            }
            case EngPluck:
            {
                // Karplus-Strong : bruit filtre injecte dans une ligne a retard bouclee
                float e = 0.0f;
                if (exPos < exLen) { e = n; ++exPos; }
                lpState += (0.1f + s.color * 0.9f) * (e - lpState);
                const float period = juce::jlimit (2.0f, (float) ksSize - 4.0f, fs / juce::jmax (20.0f, f));
                float rp = (float) ksW - period;
                while (rp < 0.0f) rp += (float) ksSize;
                const int i0 = (int) rp; const float fr = rp - (float) i0;
                const int i1 = (i0 + 1) & (ksSize - 1);
                const float y = ks[i0] + fr * (ks[i1] - ks[i0]);
                const float damp = 0.5f * s.timbre;                       // plus de timbre = plus mat
                const float filtered = y * (1.0f - damp) + (y + ksPrev) * 0.5f * damp;
                ksPrev = y;
                const float loopGain = std::pow (0.001f, period / (juce::jmax (0.01f, s.decay) * fs));
                ks[ksW] = lpState + filtered * loopGain;
                ksW = (ksW + 1) & (ksSize - 1);
                x = y * 1.4f;
                break;
            }
            case EngZap:
            {
                const float zf = f * std::exp2 ((2.0f + s.timbre * 6.0f) * zEnv);
                zEnv *= zCoef;
                ph1 += juce::jmin (0.45f, zf / fs); ph1 -= std::floor (ph1);
                const float sn = std::sin (twoPi * ph1);
                x = s.color > 0.001f ? std::sin (sn * (1.0f + s.color * 4.0f) * juce::MathConstants<float>::halfPi) : sn;
                x *= 0.9f;
                break;
            }
            case EngHybrid:
            {
                ph1 += f / fs; if (ph1 >= 1.0f) ph1 -= 1.0f;
                const float sn = std::sin (twoPi * ph1);
                float tp = ph1 + 0.25f; if (tp >= 1.0f) tp -= 1.0f;
                const float tri = 1.0f - 4.0f * std::abs (tp - 0.5f);
                const float body = sn * (1.0f - s.timbre) + tri * s.timbre;
                float lp, bp, hp;
                clapBP.process (n, lp, bp, hp);
                const float click = bp * clickEnv * 1.6f;
                clickEnv *= clickCoef;
                hpState = snapCoefHP * (hpState + n - hpPrev); hpPrev = n;
                x = body + click + hpState * noiseEnv * s.color * 1.5f;
                break;
            }
            case EngSample:
            {
                if (! sampleEnded)
                {
                    const auto* d = s.sample->data.getReadPointer (0);
                    const int len = s.sample->data.getNumSamples();
                    if (samplePos >= 0.0 && samplePos < (double) (len - 1))
                    {
                        const int i0 = (int) samplePos;
                        const float fr = (float) (samplePos - i0);
                        x = d[i0] + fr * (d[i0 + 1] - d[i0]);
                        const double inc = (double) f * s.sample->sampleRate / (double) fs;
                        samplePos += reverse ? -inc : inc;
                    }
                    else sampleEnded = true;
                }
                break;
            }
            default: break;
        }

        // couche echantillon superposee a la synthese
        if (s.layer > 0.001f && s.sample != nullptr && s.engine != EngSample)
        {
            const int len = s.sample->data.getNumSamples();
            if (layerPos < (double) (len - 1))
            {
                const auto* d = s.sample->data.getReadPointer (0);
                const int i0 = (int) layerPos;
                const float fr = (float) (layerPos - i0);
                x += s.layer * (d[i0] + fr * (d[i0 + 1] - d[i0]));
                layerPos += (double) (s.tuneRatio * pf) * s.sample->sampleRate / (double) fs;
            }
        }

        // couche de bruit commune
        if (s.noise > 0.0f) x += s.noise * n * noiseEnv;
        noiseEnv *= noiseCoef;

        x *= amp;

        // transitoire (punch) : accentue ou adoucit les premieres millisecondes
        if (s.punch != 0.0f)
        {
            x *= 1.0f + (s.punch > 0.0f ? s.punch * 1.6f : s.punch * 0.9f) * tEnv;
            tEnv *= tCoef;
        }

        // saturation tanh anti-repliement (ADAA)
        if (s.drive > 0.001f)
        {
            const float u = x * driveG;
            const float d = u - prevU;
            const float y = std::abs (d) > 1.0e-4f ? (logCosh (u) - logCosh (prevU)) / d : std::tanh (0.5f * (u + prevU));
            prevU = u;
            x = y * driveNorm;
        }

        // effet d'insertion par voix
        if (s.fxType != 0 && s.fxAmt > 0.001f)
        {
            const float a = s.fxAmt;
            switch (s.fxType)
            {
                case 1:   // wavefolder
                    x = std::sin (x * (1.0f + a * 7.0f) * juce::MathConstants<float>::halfPi) * (0.6f + 0.4f * (1.0f - a));
                    break;
                case 2:   // filtre en peigne accorde sur la voix
                {
                    const float hz = juce::jmax (30.0f, 110.0f * s.tuneRatio * pf);
                    const int d = juce::jlimit (8, combSize - 2, (int) (fs / hz));
                    int rp = combW - d; if (rp < 0) rp += combSize;
                    const float y = x + comb[rp] * a * 0.93f;
                    comb[combW] = y;
                    combW = (combW + 1) & (combSize - 1);
                    x = y * (1.0f - 0.45f * a);
                    break;
                }
                case 3:   // modulation en anneau
                {
                    const float hz = 60.0f * s.tuneRatio * std::exp2 (a * 6.0f);
                    ringPh += hz / fs; ringPh -= std::floor (ringPh);
                    const float m = std::sin (juce::MathConstants<float>::twoPi * ringPh);
                    x = x * (1.0f - juce::jmin (1.0f, a * 1.5f)) + x * m * juce::jmin (1.0f, a * 1.5f);
                    break;
                }
                case 4:   // bande : saturation asymetrique + perte d'aigus
                {
                    const float g = 1.0f + a * 4.0f, bias = 0.2f * a;
                    x = (std::tanh ((x + bias) * g) - std::tanh (bias * g)) / std::tanh (g);
                    const float lpc = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * (14000.0f - a * 10000.0f) / fs);
                    tapeLp += lpc * (x - tapeLp);
                    x = tapeLp;
                    break;
                }
                default: break;
            }
        }

        // bitcrusher (quantification + sous-echantillonnage)
        if (s.crush > 0.001f)
        {
            if (++crushCount >= crushHold) { crushCount = 0; crushHeld = std::round (x * crushLevels) / crushLevels; }
            x = crushHeld;
        }

        // filtre avec enveloppe
        if (s.fenvOct != 0.0f && (filterCounter & 7) == 0)
            filt.set (s.cutoff * std::exp2 (s.fenvOct * fEnv), q, fs);
        fEnv *= fCoef;
        ++filterCounter;

        float lp, bp, hp;
        filt.process (x, lp, bp, hp);
        x = s.ftype == 0 ? lp : (s.ftype == 1 ? bp : hp);

        x *= s.gain * relEnv;

        L += x * panL;
        R += x * panR;
        D += x * s.sendDly;
        V += x * s.sendRev;

        if (decaying && amp < 2.0e-5f) active = false;
    }

private:
    void updateModal (float pitchFactor) noexcept
    {
        static constexpr float membrane[4] { 1.0f, 1.59f, 2.14f, 2.65f };
        static constexpr float bar[4]      { 1.0f, 2.76f, 5.40f, 8.93f };
        static constexpr float decMul[4]   { 1.0f, 0.7f, 0.5f, 0.35f };
        static constexpr float amps[4]     { 1.0f, 0.7f, 0.5f, 0.4f };
        for (int m = 0; m < 4; ++m)
        {
            const float ratio = membrane[m] + (bar[m] - membrane[m]) * s.timbre;
            const float w = juce::MathConstants<float>::twoPi * s.freq * pitchFactor * ratio / fs;
            if (w >= juce::MathConstants<float>::pi * 0.9f) { b1[m] = b2[m] = 0; inGain[m] = 0; modeAmp[m] = 0; continue; }
            const float T = juce::jmax (0.01f, s.decay * decMul[m]);
            const float r = std::exp (std::log (0.001f) / (T * fs));
            b1[m] = 2.0f * r * std::cos (w);
            b2[m] = r * r;
            inGain[m] = std::sin (w) * 0.3f * std::pow (0.15f / juce::jmax (0.01f, s.decay), 0.4f);
            modeAmp[m] = amps[m];
        }
    }

    float fs = 44100.0f;
    FastRng rng;
    VoiceSettings s;
    bool active = false, releasing = false;
    float relEnv = 1, relCoef = 1;

    int attackLen = 0, attackPos = 0, holdLen = 0, holdPos = 0;
    float ampEnv = 0, ampCoef = 0, pEnv = 0, pCoef = 0, fEnv = 0, fCoef = 0, noiseEnv = 0, noiseCoef = 0, tEnv = 0, tCoef = 0;

    float ph1 = 0, ph2 = 0, phMod = 0;
    float metalPh[6] {};
    float clickEnv = 0, clickCoef = 0;
    float toneEnv = 0, toneCoef = 0, snapCoefHP = 0, hpState = 0, hpPrev = 0;
    SVF metalHP, clapBP, filt;
    int clapSpacing = 100, clapPos = 0; float burstEnv = 0, burstCoef = 0, clapGain = 1;
    float idxEnv = 0, idxCoef = 0, modRatio = 1;
    int exLen = 10, exPos = 0; float lpState = 0;
    float y1[4] {}, y2[4] {}, b1[4] {}, b2[4] {}, inGain[4] {}, modeAmp[4] {};
    float ks[ksSize] {}; int ksW = 0; float ksPrev = 0;
    float zEnv = 0, zCoef = 0;
    double samplePos = 0; bool reverse = false, sampleEnded = false;
    float slideLog = 0, gEnv = 0, gCoef = 0;
    double layerPos = 0;
    static constexpr int combSize = 4096;
    float comb[combSize] {}; int combW = 0;
    float ringPh = 0, tapeLp = 0;

    float q = 0.7f; int filterCounter = 0;
    float driveG = 1, driveNorm = 1, prevU = 0;
    float crushLevels = 32768, crushHeld = 0; int crushHold = 1, crushCount = 0;
    float panL = 0.707f, panR = 0.707f;
};

} // namespace alea
