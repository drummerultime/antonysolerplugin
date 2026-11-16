#pragma once

#include "../TaleaCommon.h"

namespace talea
{
// Valeurs reelles de la voix, recalculees a chaque bloc (les potards agissent sur les notes tenues)
struct BassParams
{
    int osc1 = 0, osc2 = 0, sub = 0, ftype = 0, dtype = 0, chord = 0, glideMode = 0;
    float shape = 0.5f, o2lvl = 0, o2semi = 0, o2fine = 0, fm = 0, subLvl = 0.25f, noise = 0;
    float cutoffHz = 300, reso = 0.6f, envOct = 3, fdecay = 0.15f, accentAmt = 0.7f, keytrack = 0.3f, fdrive = 0.2f;
    float attack = 0.001f, decay = 0.3f, sustain = 0.6f, release = 0.02f, glideSec = 0.05f;
    float drive = 0.15f;
};

inline float tanhA (float x) noexcept
{
    x = juce::jlimit (-3.0f, 3.0f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

inline float blep (float t, float dt) noexcept
{
    if (t < dt)        { t /= dt;          return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

struct TSVF
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    inline void process (float v0, float fc, float q, float fs, float& lp, float& bp, float& hp) noexcept
    {
        fc = juce::jlimit (16.0f, fs * 0.45f, fc);
        const float g = std::tan (juce::MathConstants<float>::pi * fc / fs);
        const float k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = v0 - k * v1 - v2;
    }
};

class BassVoice
{
public:
    static constexpr int ksSize = 8192;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        hpCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 90.0f / fs);
        reset();
    }

    void reset()
    {
        gate = false; amp = 0; ampStage = Off; fEnv = 0; accEnv = 0; accCap = 0;
        for (auto& s : st) s = 0;
        fbLp = 0; svf.reset(); logCut = std::log (300.0f);
        std::fill (std::begin (ks), std::end (ks), 0.0f);
        crushCount = 0; crushHeld = 0;
    }

    bool isActive() const noexcept { return gate || amp > 1.0e-5f; }
    bool gateOn() const noexcept { return gate; }
    float currentMidi() const noexcept { return curMidi; }

    // legato = la porte etait encore ouverte (slide) : pas de redeclenchement des enveloppes
    void noteOn (float midi, float velocity, bool accent, bool legato, const BassParams& p)
    {
        targetMidi = midi;
        const bool glide = p.glideMode == 1 || (p.glideMode == 0 && legato);
        gliding = glide && amp > 1.0e-4f;
        if (! gliding) curMidi = midi;
        gCoef = std::exp (-1.0f / (juce::jmax (0.001f, p.glideSec) * 0.35f * fs));
        vel = velocity;
        accented = accent;
        if (accent)
        {
            accEnvTarget = 1.0f;
            accCap += (1.0f - accCap) * 0.45f;      // accents successifs : le "wow" s'accumule (303)
        }
        if (! legato || ! gate)
        {
            ampStage = Attack;
            attackStep = 1.0f / juce::jmax (1.0f, p.attack * fs);
            fEnv = 1.0f;
            if (p.osc1 == 7) pluckExcite (p);
        }
        gate = true;
    }

    void noteOff() { gate = false; if (ampStage != Off) ampStage = Release; }

    inline float process (const BassParams& p) noexcept
    {
        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        // hauteur (glide)
        if (gliding) { curMidi = targetMidi + (curMidi - targetMidi) * gCoef; if (std::abs (curMidi - targetMidi) < 0.001f) gliding = false; }
        const float f0 = 440.0f * std::exp2 ((curMidi - 69.0f) / 12.0f);

        // oscillateur 2 (sert aussi de modulateur FM)
        float o2 = 0.0f;
        if (p.osc2 != 0)
        {
            const float f2 = f0 * std::exp2 ((p.o2semi + p.o2fine * 0.01f) / 12.0f);
            const float dt = juce::jmin (0.45f, f2 / fs);
            ph2 += dt; ph2 -= std::floor (ph2);
            switch (p.osc2)
            {
                case 1: o2 = 2.0f * ph2 - 1.0f - blep (ph2, dt); break;
                case 2: { float t2 = ph2 + 0.5f; t2 -= std::floor (t2); o2 = (ph2 < 0.5f ? 1.0f : -1.0f) + blep (ph2, dt) - blep (t2, dt); break; }
                case 3: o2 = std::sin (twoPi * ph2); break;
                case 4: o2 = 4.0f * std::abs (ph2 - 0.5f) - 1.0f; break;
                default: o2 = rng.bi(); break;
            }
        }
        const float pm = p.fm * 2.5f * (p.osc2 == 0 ? std::sin (twoPi * ph2) : o2);
        if (p.osc2 == 0 && p.fm > 0.0f) { ph2 += juce::jmin (0.45f, f0 / fs); ph2 -= std::floor (ph2); }

        // oscillateur 1 (+ accords)
        int nv = 1;
        const int* iv = chordIntervals (p.chord, nv);
        if (p.osc1 == 7) nv = 1;
        float x1 = 0.0f;
        for (int v = 0; v < nv; ++v)
        {
            const float f = f0 * (iv[v] == 0 ? 1.0f : std::exp2 ((float) iv[v] / 12.0f));
            const float dt = juce::jmin (0.45f, f / fs);
            float& ph = ph1[v];
            ph += dt; ph -= std::floor (ph);
            float t = ph + pm * 0.15f; t -= std::floor (t);
            float s = 0.0f;
            switch (p.osc1)
            {
                case 0: s = 2.0f * t - 1.0f - blep (t, dt); break;
                case 1: { float t2 = t + 0.5f; t2 -= std::floor (t2); s = (t < 0.5f ? 1.0f : -1.0f) + blep (t, dt) - blep (t2, dt); break; }
                case 2:
                {
                    const float pw = 0.5f - p.shape * 0.45f;
                    float t2 = t + (1.0f - pw); t2 -= std::floor (t2);
                    s = (t < pw ? 1.0f : -1.0f) + blep (t, dt) - blep (t2, dt);
                    break;
                }
                case 3: s = 4.0f * std::abs (t - 0.5f) - 1.0f; break;
                case 4: s = std::sin (twoPi * t); break;
                case 5:
                {
                    static constexpr float off[7] { -1.0f, -0.66f, -0.33f, 0.0f, 0.33f, 0.66f, 1.0f };
                    const int nss = nv > 1 ? 3 : 7;
                    for (int k = 0; k < nss; ++k)
                    {
                        const int kk = nss == 7 ? k : k * 3;
                        const float fk = f * std::exp2 (off[kk] * p.shape * 0.5f / 12.0f);
                        const float dk = juce::jmin (0.45f, fk / fs);
                        float& q = ss[v][kk];
                        q += dk; q -= std::floor (q);
                        s += 2.0f * q - 1.0f - blep (q, dk);
                    }
                    s *= nss == 7 ? 0.32f : 0.55f;
                    break;
                }
                case 6: s = std::sin (std::sin (twoPi * t) * (1.0f + p.shape * 5.0f) * juce::MathConstants<float>::halfPi); break;
                case 7: s = pluckProcess (f, p); break;
                default: break;
            }
            x1 += s;
        }
        if (nv > 1) x1 *= 1.0f / std::sqrt ((float) nv);

        // sous-oscillateur et bruit
        const float fsub = f0 * (p.sub == 2 ? 0.25f : 0.5f);
        phSub += juce::jmin (0.45f, fsub / fs); phSub -= std::floor (phSub);
        const float sub = p.sub == 1 ? (phSub < 0.5f ? 0.8f : -0.8f) : std::sin (twoPi * phSub);
        float x = x1 + o2 * p.o2lvl + sub * p.subLvl + rng.bi() * p.noise;

        // enveloppes de filtre et d'accent
        const float fdec = accented ? juce::jmin (p.fdecay, 0.22f) : p.fdecay;
        fEnv *= std::exp (-1.0f / (juce::jmax (0.003f, fdec) * 0.35f * fs));
        accEnv += (accEnvTarget - accEnv) * (accEnvTarget > accEnv ? 0.02f : 0.0004f);
        accEnvTarget *= 0.9993f;
        accCap *= 0.99996f;
        const float accentMod = accented ? p.accentAmt * (accEnv * 2.2f + accCap * 1.3f) : p.accentAmt * accCap * 0.4f;
        const float octs = p.envOct * fEnv * (0.6f + 0.4f * vel) + accentMod + p.keytrack * (curMidi - 48.0f) / 12.0f;
        const float tgtLog = std::log (juce::jlimit (12.0f, 0.42f * fs, p.cutoffHz * std::exp2 (octs)));
        logCut += (tgtLog - logCut) * 0.02f;
        const float fc = std::exp (logCut);

        // filtre
        const float pre = tanhA (x * (0.6f + p.fdrive * 3.0f)) * 0.9f;
        float y = pre;
        switch (p.ftype)
        {
            case 0: case 1:
            {
                const float g = 1.0f - std::exp (-twoPi * fc / fs);
                const bool diode = p.ftype == 0;
                const float k = p.reso * (diode ? 3.7f : 4.1f);
                fbLp += hpCoef * (st[3] - fbLp);
                const float fb = diode ? st[3] - fbLp : st[3];
                const float in = tanhA (pre * (1.0f + k * 0.35f) - k * fb);
                const float g3 = diode ? g * 0.82f : g, g4 = diode ? g * 0.66f : g;
                st[0] += g  * (in - tanhA (st[0]));
                st[1] += g  * (tanhA (st[0]) - tanhA (st[1]));
                st[2] += g3 * (tanhA (st[1]) - tanhA (st[2]));
                st[3] += g4 * (tanhA (st[2]) - tanhA (st[3]));
                y = st[3] * (diode ? 1.5f : 1.3f);
                break;
            }
            case 2: case 3: case 4:
            {
                float lp, bp, hp;
                svf.process (pre, fc, 0.5f + p.reso * p.reso * 14.0f, fs, lp, bp, hp);
                y = p.ftype == 2 ? lp : (p.ftype == 3 ? bp * 1.4f : hp);
                break;
            }
            default: break;
        }

        // saturation de sortie
        if (p.drive > 0.001f)
        {
            const float d = p.drive;
            switch (p.dtype)
            {
                case 0: { const float g = 1.0f + d * 8.0f; y = std::tanh (y * g) / std::tanh (g); break; }
                case 1: y = juce::jlimit (-1.0f, 1.0f, y * (1.0f + d * 8.0f)) * 0.8f; break;
                case 2: y = std::sin (y * (1.0f + d * 6.0f) * juce::MathConstants<float>::halfPi) * 0.8f; break;
                case 3: { const float g = 1.0f + d * 6.0f; y = y > 0.0f ? std::tanh (y * g) : std::tanh (y * g * 0.55f) * 0.9f; y /= std::tanh (g) * 0.9f + 0.1f; break; }
                case 4:
                {
                    const float levels = std::pow (2.0f, 14.0f - d * 11.0f);
                    if (++crushCount >= 1 + (int) (d * d * 24.0f)) { crushCount = 0; crushHeld = std::round (y * levels) / levels; }
                    y = crushHeld;
                    break;
                }
                default: break;
            }
        }

        // enveloppe d'amplitude
        switch (ampStage)
        {
            case Attack:  amp += attackStep; if (amp >= 1.0f) { amp = 1.0f; ampStage = DecayS; } break;
            case DecayS:  amp = p.sustain + (amp - p.sustain) * std::exp (-1.0f / (juce::jmax (0.002f, p.decay) * 0.3f * fs)); break;
            case Release: amp *= std::exp (-1.0f / (juce::jmax (0.002f, p.release) * 0.3f * fs)); if (amp < 1.0e-5f) { amp = 0; ampStage = Off; } break;
            default: amp = 0; break;
        }
        const float accGain = 1.0f + (accented ? p.accentAmt * 0.45f : 0.0f);
        return y * amp * accGain;
    }

private:
    enum Stage { Off, Attack, DecayS, Release };

    void pluckExcite (const BassParams& p)
    {
        std::fill (std::begin (ks), std::end (ks), 0.0f);
        const int period = juce::jlimit (8, ksSize - 8, (int) (fs / (440.0f * std::exp2 ((targetMidi - 69.0f) / 12.0f))));
        float lp = 0.0f;
        for (int i = 0; i < period; ++i) { lp += (0.25f + (1.0f - p.shape) * 0.7f) * (rng.bi() - lp); ks[i] = lp; }
        ksW = period;
        ksPrev = 0;
    }

    float pluckProcess (float f, const BassParams& p) noexcept
    {
        const float period = juce::jlimit (2.0f, (float) ksSize - 4.0f, fs / juce::jmax (15.0f, f));
        float rp = (float) ksW - period;
        while (rp < 0.0f) rp += (float) ksSize;
        const int i0 = (int) rp & (ksSize - 1);
        const int i1 = (i0 + 1) & (ksSize - 1);
        const float fr = rp - std::floor (rp);
        const float yv = ks[i0] + fr * (ks[i1] - ks[i0]);
        const float damp = 0.15f + 0.8f * p.shape;
        const float filtered = yv * (1.0f - damp * 0.5f) + ksPrev * damp * 0.5f;
        ksPrev = yv;
        ks[ksW & (ksSize - 1)] = filtered * 0.9985f;
        ksW = (ksW + 1) & (ksSize - 1);
        return yv * 1.6f;
    }

    float fs = 96000.0f;
    alea::FastRng rng { 0x7A1EAu };
    bool gate = false, gliding = false, accented = false;
    float curMidi = 36.0f, targetMidi = 36.0f, gCoef = 0.99f, vel = 0.8f;
    float ph1[4] {}, ph2 = 0, phSub = 0;
    float ss[4][7] {};
    Stage ampStage = Off;
    float amp = 0, attackStep = 0.01f;
    float fEnv = 0, accEnv = 0, accEnvTarget = 0, accCap = 0;
    float st[4] {};
    float fbLp = 0, hpCoef = 0.006f;
    TSVF svf;
    float logCut = 5.7f;
    float ks[ksSize] {}; int ksW = 0; float ksPrev = 0;
    int crushCount = 0; float crushHeld = 0;
};

// Chorus stereo (2 lignes modulees en quadrature)
class Chorus
{
public:
    void prepare (double sr)
    {
        fs = (float) sr;
        size = juce::nextPowerOfTwo ((int) (fs * 0.06f));
        bufL.assign ((size_t) size, 0.0f); bufR.assign ((size_t) size, 0.0f);
        w = 0; ph = 0;
    }
    inline void process (float& l, float& r, float rateN, float depthN, float mix) noexcept
    {
        if (mix <= 0.001f) { bufL[(size_t) w] = l; bufR[(size_t) w] = r; w = (w + 1) & (size - 1); return; }
        ph += (0.05f + rateN * rateN * 4.0f) / fs; ph -= std::floor (ph);
        const float base = 0.012f * fs, dep = (0.001f + depthN * 0.008f) * fs;
        const float dl = base + dep * std::sin (juce::MathConstants<float>::twoPi * ph);
        const float dr = base + dep * std::cos (juce::MathConstants<float>::twoPi * ph);
        bufL[(size_t) w] = l; bufR[(size_t) w] = r;
        const float wl = read (bufL, dl), wr = read (bufR, dr);
        w = (w + 1) & (size - 1);
        l = l * (1.0f - mix * 0.5f) + wl * mix * 0.7f;
        r = r * (1.0f - mix * 0.5f) + wr * mix * 0.7f;
    }
private:
    inline float read (const std::vector<float>& b, float d) const noexcept
    {
        float rp = (float) w - d;
        while (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp & (size - 1);
        const int i1 = (i0 + 1) & (size - 1);
        const float fr = rp - std::floor (rp);
        return b[(size_t) i0] + fr * (b[(size_t) i1] - b[(size_t) i0]);
    }
    float fs = 48000.0f, ph = 0;
    std::vector<float> bufL, bufR;
    int size = 1, w = 0;
};

} // namespace talea
