#pragma once

#include "../Core.h"
#include <vector>

namespace alea
{
inline float softClip (float x) noexcept
{
    const float a = std::abs (x);
    if (a < 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}

// Delay stereo synchronise, ping-pong, tonalite dans la reinjection.
// Les changements de temps "glissent" (effet bande) au lieu de cliquer.
class StereoDelay
{
public:
    void prepare (double fs)
    {
        size = (int) (fs * 4.0) + 8;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        w = 0; lpL = lpR = 0;
        cur = target = (float) (fs * 0.25);
    }
    void setTimeSamples (float samples) noexcept { target = juce::jlimit (16.0f, (float) size - 8.0f, samples); }

    inline void process (float inL, float inR, float& outL, float& outR, float fb, float toneCoef, bool ping) noexcept
    {
        cur += (target - cur) * 0.0006f;
        const float dl = read (bufL, cur), dr = read (bufR, cur);
        lpL += toneCoef * (dl - lpL);
        lpR += toneCoef * (dr - lpR);

        float wl, wr;
        if (ping) { wl = (inL + inR) * 0.5f + lpR * fb; wr = lpL * fb; }
        else      { wl = inL + lpL * fb;             wr = inR + lpR * fb; }

        bufL[(size_t) w] = softClip (wl);
        bufR[(size_t) w] = softClip (wr);
        if (++w >= size) w = 0;
        outL = lpL; outR = lpR;
    }

private:
    inline float read (const std::vector<float>& b, float d) const noexcept
    {
        float rp = (float) w - d;
        while (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const int i1 = (i0 + 1 >= size) ? 0 : i0 + 1;
        return b[(size_t) i0] + fr * (b[(size_t) i1] - b[(size_t) i0]);
    }
    std::vector<float> bufL, bufR;
    int size = 0, w = 0;
    float cur = 0, target = 0, lpL = 0, lpR = 0;
};

// Beat-repeat : capture une tranche et la repete pendant une duree donnee.
class Stutter
{
public:
    void prepare (double fs)
    {
        size = (int) (fs * 4.0) + 8;
        L.assign ((size_t) size, 0.0f);
        R.assign ((size_t) size, 0.0f);
        w = 0; active = false;
    }
    bool isActive() const noexcept { return active; }

    void trigger (int sliceSamples, int totalSamples) noexcept
    {
        slice = juce::jlimit (64, size / 4, sliceSamples);
        total = juce::jlimit (slice, size / 2, totalSamples);
        start = w; pos = 0; active = true;
    }

    inline void process (float& l, float& r) noexcept
    {
        L[(size_t) w] = l; R[(size_t) w] = r;
        if (active)
        {
            const int inSlice = pos % slice;
            const float fade = juce::jmin (1.0f, (float) inSlice / (float) xf, (float) (slice - inSlice) / (float) xf);
            if (pos < slice)
            {
                l *= (pos < slice - xf) ? 1.0f : fade;
                r *= (pos < slice - xf) ? 1.0f : fade;
            }
            else
            {
                int idx = start + inSlice; if (idx >= size) idx -= size;
                l = L[(size_t) idx] * fade;
                r = R[(size_t) idx] * fade;
            }
            if (++pos >= total) active = false;
        }
        if (++w >= size) w = 0;
    }

private:
    std::vector<float> L, R;
    int size = 0, w = 0, start = 0, slice = 1000, pos = 0, total = 0;
    static constexpr int xf = 48;
    bool active = false;
};

// Compresseur "glue" de bus, stereo lie
class Glue
{
public:
    void prepare (double fs)
    {
        atk = std::exp (-1.0f / (0.005f * (float) fs));
        rel = std::exp (-1.0f / (0.12f * (float) fs));
        env = 0;
    }
    inline void process (float& l, float& r, float amount) noexcept
    {
        if (amount < 0.001f) return;
        const float lvl = juce::jmax (std::abs (l), std::abs (r));
        env = lvl > env ? atk * env + (1.0f - atk) * lvl : rel * env + (1.0f - rel) * lvl;
        const float thr = juce::Decibels::decibelsToGain (-amount * 30.0f);
        float gr = 1.0f;
        if (env > thr) gr = std::pow (env / thr, -0.75f);      // ratio 4:1
        const float g = gr * juce::Decibels::decibelsToGain (amount * 12.0f);
        l *= g; r *= g;
    }
private:
    float env = 0, atk = 0, rel = 0;
};

// Reverbe "plate" a reseau de retards recirculants (FDN 8 lignes, matrice de Hadamard),
// diffusion d'entree par passe-tout, amortissement par ligne et legere modulation.
class PlateReverb
{
public:
    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        lineSize = juce::nextPowerOfTwo ((int) (fs * 0.25f));
        for (auto& l : lines) l.assign ((size_t) lineSize, 0.0f);
        preSize = juce::nextPowerOfTwo ((int) (fs * 0.2f));
        pre.assign ((size_t) preSize, 0.0f);
        for (int i = 0; i < 4; ++i)
        {
            apLen[i] = juce::jmax (8, (int) (apMs[i] * 0.001f * fs));
            ap[i].assign ((size_t) apLen[i], 0.0f);
            apW[i] = 0;
        }
        w = 0; preW = 0; lfo = 0;
        for (auto& v : lp) v = 0.0f;
        setParams (0.6f, 0.5f, 1.0f, 0.01f);
    }

    void setParams (float size, float damp, float width_, float predelaySec)
    {
        const float scale = 0.6f + size * 0.9f;
        const float t60 = 0.4f + size * size * 6.0f;
        for (int i = 0; i < 8; ++i)
        {
            len[i] = juce::jlimit (16, lineSize - 32, (int) (lineMs[i] * 0.001f * fs * scale));
            g[i] = std::pow (10.0f, -3.0f * (float) len[i] / (t60 * fs));
        }
        dampC = 0.12f + (1.0f - damp) * 0.88f;
        width = width_;
        preD = juce::jlimit (1, preSize - 1, (int) (predelaySec * fs));
    }

    inline void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        // predelai
        pre[(size_t) preW] = 0.5f * (inL + inR);
        int pr = preW - preD; if (pr < 0) pr += preSize;
        float x = pre[(size_t) pr];
        preW = (preW + 1) & (preSize - 1);

        // diffusion
        for (int i = 0; i < 4; ++i)
        {
            auto& b = ap[i];
            const float d = b[(size_t) apW[i]];
            const float y = -0.7f * x + d;
            b[(size_t) apW[i]] = x + 0.7f * y;
            if (++apW[i] >= apLen[i]) apW[i] = 0;
            x = y;
        }

        // lecture des lignes (2 lignes legerement modulees)
        lfo += 0.7f / fs; if (lfo >= 1.0f) lfo -= 1.0f;
        const float mod = std::sin (juce::MathConstants<float>::twoPi * lfo) * 6.0f;
        float d[8];
        for (int i = 0; i < 8; ++i)
        {
            float rp = (float) w - (float) len[i] - (i < 2 ? (i == 0 ? mod : -mod) + 7.0f : 0.0f);
            while (rp < 0.0f) rp += (float) lineSize;
            const int i0 = (int) rp & (lineSize - 1);
            const int i1 = (i0 + 1) & (lineSize - 1);
            const float fr = rp - std::floor (rp);
            const float v = lines[i][(size_t) i0] + fr * (lines[i][(size_t) i1] - lines[i][(size_t) i0]);
            lp[i] += dampC * (v - lp[i]);
            d[i] = lp[i];
        }

        // Hadamard 8x8 (papillons), normalise
        float h[8];
        for (int i = 0; i < 8; ++i) h[i] = d[i];
        for (int span = 1; span < 8; span <<= 1)
            for (int i = 0; i < 8; i += span << 1)
                for (int j = i; j < i + span; ++j)
                {
                    const float a = h[j], b = h[j + span];
                    h[j] = a + b; h[j + span] = a - b;
                }
        constexpr float norm = 0.35355339f;   // 1/sqrt(8)
        for (int i = 0; i < 8; ++i)
            lines[i][(size_t) w] = (i & 1 ? -x : x) * 0.5f + h[i] * norm * g[i];
        w = (w + 1) & (lineSize - 1);

        const float l = d[0] - d[2] + d[4] - d[6];
        const float r = d[1] - d[3] + d[5] - d[7];
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
        outL = (mid + side) * 0.5f;
        outR = (mid - side) * 0.5f;
    }

private:
    static constexpr float lineMs[8] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.3f, 73.1f };
    static constexpr float apMs[4] { 4.7f, 3.6f, 12.7f, 9.3f };
    float fs = 48000.0f;
    std::vector<float> lines[8];
    std::vector<float> pre, ap[4];
    int lineSize = 0, w = 0, preSize = 0, preW = 0, preD = 1;
    int apLen[4] {}, apW[4] {};
    int len[8] {};
    float g[8] {}, lp[8] {};
    float dampC = 0.5f, width = 1.0f, lfo = 0.0f;
};

// Decimateur demi-bande x2 (47 points, fenetre de Kaiser) pour le mode HQ
class HalfbandDecimator
{
public:
    void reset() { std::fill (std::begin (buf), std::end (buf), 0.0f); pos = 0; }

    // pousse deux echantillons a 2x et renvoie un echantillon a 1x
    inline float process (float a, float b) noexcept
    {
        buf[pos] = a; pos = (pos + 1) & mask;
        buf[pos] = b; pos = (pos + 1) & mask;
        // le plus recent est en pos-1 ; le centre du filtre (47 points) est 23 echantillons en arriere
        auto at = [this] (int back) { return buf[(pos - 1 - back) & mask]; };
        float y = 0.5f * at (23);
        for (int k = 0; k < 12; ++k)
            y += c[k] * (at (23 - (2 * k + 1)) + at (23 + (2 * k + 1)));
        return y;
    }

private:
    static constexpr float c[12] { 0.3160629362f, -0.0995345836f, 0.0532395992f, -0.0319062120f, 0.0195116826f, -0.0116853841f,
                                   0.0066708476f, -0.0035394678f, 0.0016906510f, -0.0006900036f, 0.0002146043f, -0.0000323681f };
    static constexpr int mask = 63;
    float buf[64] {};
    int pos = 0;
};

} // namespace alea
