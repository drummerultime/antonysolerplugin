#include "MaEngines.h"
#include "DSP/Effects.h"

namespace ma
{
using alea::hash3;
using alea::hUni;
using alea::hBi;
using alea::FastRng;

static constexpr float kTwoPi = juce::MathConstants<float>::twoPi;
static constexpr float kPi = juce::MathConstants<float>::pi;

static inline uint32_t salt (const Ctx& c, uint32_t s) noexcept { return hash3 (c.seed, (uint32_t) c.slot * 7919u + 13u, s); }

float randomInterval (float amount, uint32_t a, uint32_t b, uint32_t c) noexcept
{
    static constexpr float iv[] { 0, 12, -12, 7, -5, 24, 5, -7, 19, -24, 3, 4, -3, 10, 2, -2, 11, -1, 1, 6 };
    constexpr int N = (int) (sizeof (iv) / sizeof (iv[0]));
    if (amount <= 0.0f) return 0.0f;
    const int count = 1 + (int) std::round (juce::jlimit (0.0f, 1.0f, amount) * (float) (N - 1));
    return iv[juce::jlimit (0, N - 1, (int) (hUni (a, b, c) * (float) count))];
}

// Bruit lisse (marche aleatoire interpolee)
struct SmoothRnd
{
    FastRng rng;
    float a = 0, b = 0, t = 1, inc = 0.001f;
    void reset (uint32_t s) { rng = FastRng (s | 1u); a = b = 0; t = 1; }
    inline float next (float hz, double fs) noexcept
    {
        t += inc;
        if (t >= 1.0f) { t -= 1.0f; a = b; b = rng.bi(); inc = (float) (juce::jmax (0.01f, hz) / fs); }
        const float s = t * t * (3.0f - 2.0f * t);
        return a + (b - a) * s;
    }
};

// ===========================================================================
// Verb
void Verb::prepare (double sampleRate)
{
    fs = (float) sampleRate;
    lineSize = juce::nextPowerOfTwo ((int) (fs * 0.2f));
    for (auto& l : lines) l.assign ((size_t) lineSize, 0.0f);
    preSize = juce::nextPowerOfTwo ((int) (fs * 0.3f));
    static constexpr float apMs[2][4] { { 4.7f, 3.6f, 12.7f, 9.3f }, { 5.3f, 3.1f, 11.9f, 8.6f } };
    for (int c = 0; c < 2; ++c)
    {
        pre[c].assign ((size_t) preSize, 0.0f);
        for (int i = 0; i < 4; ++i)
        {
            apLen[c][i] = juce::jmax (8, (int) (apMs[c][i] * 0.001f * fs));
            ap[c][i].assign ((size_t) apLen[c][i], 0.0f);
        }
    }
    clear();
    setParams (0.6f, 2.0f, 0.5f, 0.01f, 0.3f, 1.0f);
}

void Verb::clear()
{
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        std::fill (pre[c].begin(), pre[c].end(), 0.0f);
        for (int i = 0; i < 4; ++i) { std::fill (ap[c][i].begin(), ap[c][i].end(), 0.0f); apW[c][i] = 0; }
    }
    for (auto& v : lp) v = 0.0f;
    w = 0; preW = 0;
}

void Verb::setParams (float size, float t60, float damp, float predelaySec, float mod, float width_)
{
    static constexpr float lineMs[8] { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.3f, 73.1f };
    const float scale = 0.45f + juce::jlimit (0.0f, 1.0f, size) * 1.05f;
    t60 = juce::jmax (0.1f, t60);
    for (int i = 0; i < 8; ++i)
    {
        len[i] = juce::jlimit (16.0f, (float) lineSize - 64.0f, lineMs[i] * 0.001f * fs * scale);
        g[i] = std::pow (10.0f, -3.0f * len[i] / (t60 * fs));
    }
    dampC = 0.08f + (1.0f - juce::jlimit (0.0f, 1.0f, damp)) * 0.92f;
    width = width_;
    modDepth = juce::jlimit (0.0f, 1.0f, mod) * 0.0006f * fs;   // jusqu'a 0.6 ms
    preD = juce::jlimit (1, preSize - 1, (int) (predelaySec * fs));
}

void Verb::process (float inL, float inR, float& outL, float& outR, bool freeze) noexcept
{
    float x[2] { inL, inR };
    if (freeze) x[0] = x[1] = 0.0f;
    for (int c = 0; c < 2; ++c)
    {
        auto& P = pre[c];
        P[(size_t) preW] = x[c];
        int pr = preW - preD; if (pr < 0) pr += preSize;
        float v = P[(size_t) pr];
        for (int i = 0; i < 4; ++i)
        {
            auto& b = ap[c][i];
            const float d = b[(size_t) apW[c][i]];
            const float y = -0.65f * v + d;
            b[(size_t) apW[c][i]] = v + 0.65f * y;
            if (++apW[c][i] >= apLen[c][i]) apW[c][i] = 0;
            v = y;
        }
        x[c] = v;
    }
    preW = (preW + 1) & (preSize - 1);

    static constexpr float rates[4] { 0.53f, 0.71f, 0.89f, 1.13f };
    float m[4];
    for (int k = 0; k < 4; ++k)
    {
        ph[k] += rates[k] / fs; if (ph[k] >= 1.0f) ph[k] -= 1.0f;
        m[k] = std::sin (kTwoPi * ph[k]) * modDepth;
    }

    const float dc = freeze ? 1.0f : dampC;
    float d[8];
    for (int i = 0; i < 8; ++i)
    {
        float rp = (float) w - len[i] - (i < 4 ? m[i] + modDepth + 2.0f : 0.0f);
        while (rp < 0.0f) rp += (float) lineSize;
        const int i0 = (int) rp & (lineSize - 1);
        const int i1 = (i0 + 1) & (lineSize - 1);
        const float fr = rp - std::floor (rp);
        const float v = lines[i][(size_t) i0] + fr * (lines[i][(size_t) i1] - lines[i][(size_t) i0]);
        lp[i] += dc * (v - lp[i]);
        d[i] = lp[i];
    }
    float h[8];
    for (int i = 0; i < 8; ++i) h[i] = d[i];
    for (int span = 1; span < 8; span <<= 1)
        for (int i = 0; i < 8; i += span << 1)
            for (int j = i; j < i + span; ++j)
            {
                const float a = h[j], b = h[j + span];
                h[j] = a + b; h[j + span] = a - b;
            }
    constexpr float norm = 0.35355339f;
    for (int i = 0; i < 8; ++i)
    {
        const float gi = freeze ? 1.0f : g[i];
        const float inj = (i & 1) ? x[1] : x[0];
        lines[i][(size_t) w] = (i & 2 ? -inj : inj) * 0.5f + h[i] * norm * gi;
    }
    w = (w + 1) & (lineSize - 1);

    const float l = d[0] - d[2] + d[4] - d[6];
    const float r = d[1] - d[3] + d[5] - d[7];
    const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
    outL = (mid + side) * 0.5f;
    outR = (mid - side) * 0.5f;
}

// ===========================================================================
// 1. Delay bande
class DelayE : public Engine
{
public:
    void prepare (double f) override { fs = f; l.prepare ((int) (fs * 4.2)); r.prepare ((int) (fs * 4.2)); reset(); }
    void reset() override { l.clear(); r.clear(); cur = -1; lpL = lpR = hpL = hpR = 0; ph = 0; rnd.reset (77); }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const bool sync = p[5] >= 0.5f;
        float target = sync ? (float) (delayDivBeats (pick (p[0], 12)) * c.samplesPerBeat())
                            : expMap (p[0], 10.0f, 2000.0f) * 0.001f * (float) fs;
        target = juce::jlimit (32.0f, (float) l.size() - 600.0f, target);
        if (cur < 0) cur = target;
        const float fb = c.freeze ? 1.0f : p[1] * 1.1f;
        const float tone = bip (p[2]);
        const float lpA = onePoleCoef (tone < 0 ? 18000.0f * std::pow (600.0f / 18000.0f, -tone) : 18000.0f, fs);
        const float hpA = onePoleCoef (tone > 0 ? 30.0f * std::pow (2000.0f / 30.0f, tone) : 30.0f, fs);
        const float ping = p[3], wob = p[4] * 0.004f * (float) fs;
        for (int i = 0; i < n; ++i)
        {
            cur += (target - cur) * 0.0004f;
            ph += 0.55f / (float) fs; if (ph >= 1.0f) ph -= 1.0f;
            const float wd = wob * (0.55f * (1.0f + std::sin (kTwoPi * ph)) + 0.45f * (1.0f + rnd.next (1.7f, fs)));
            float yl = l.readH (cur + wd + 2.0f), yr = r.readH (cur + wd * 0.93f + 2.0f);
            lpL += lpA * (yl - lpL); lpR += lpA * (yr - lpR);
            hpL += hpA * (lpL - hpL); hpR += hpA * (lpR - hpR);
            yl = c.freeze ? yl : lpL - hpL; yr = c.freeze ? yr : lpR - hpR;
            const float inL = c.freeze ? 0.0f : L[i], inR = c.freeze ? 0.0f : R[i];
            const float mono = 0.5f * (inL + inR);
            const float wl = inL * (1.0f - ping) + mono * ping + fb * ((1.0f - ping) * yl + ping * yr);
            const float wr = inR * (1.0f - ping) + fb * ((1.0f - ping) * yr + ping * yl);
            l.push (softClip (wl)); r.push (softClip (wr));
            L[i] = yl; R[i] = yr;
        }
    }
private:
    double fs = 48000;
    Line l, r;
    float cur = -1, lpL = 0, lpR = 0, hpL = 0, hpR = 0, ph = 0;
    SmoothRnd rnd;
};

// ===========================================================================
// 2. Scatter : 8 taps redistribues aleatoirement
class ScatterE : public Engine
{
public:
    void prepare (double f) override { fs = f; l.prepare ((int) (fs * 2.6)); r.prepare ((int) (fs * 2.6)); reset(); }
    void reset() override
    {
        l.clear(); r.clear(); lastIdx = INT64_MIN; countdown = 0; draws = 0;
        for (auto& t : taps) t = Tap();
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int rateI = pick (p[5], 7);
        const double rb = scatterRateBeats (rateI);
        const float fb = c.freeze ? 0.9f : p[2] * 0.85f;
        const int ntaps = 1 + (int) std::round (p[1] * 7.0f);
        const float norm = 1.0f / std::sqrt ((float) ntaps);
        const float xfInc = 1.0f / (0.025f * (float) fs);
        for (int i = 0; i < n; ++i)
        {
            const double ppq = c.ppq + i * c.ppqPerSample;
            bool redraw = false;
            if (rb > 0.0)
            {
                const int64_t idx = (int64_t) std::floor (ppq / rb);
                if (idx != lastIdx) { lastIdx = idx; redraw = true; }
            }
            else if (--countdown <= 0)
            {
                countdown = (int) (c.samplesPerBeat() * (0.25 + 3.75 * hUni ((uint32_t) draws, 0x5c1u, salt (c, 1))));
                redraw = true;
            }
            if (redraw && ! c.freeze) draw (c, p, ntaps);

            float yl = 0, yr = 0;
            for (int k = 0; k < 8; ++k)
            {
                auto& t = taps[k];
                if (t.x < 1.0f) t.x = juce::jmin (1.0f, t.x + xfInc);
                if (t.g0 == 0.0f && t.g1 == 0.0f) continue;
                const float a = t.x;
                const float vl = (a < 1.0f ? (1.0f - a) * l.read (t.d0) * t.g0 * t.l0 : 0.0f) + a * l.read (t.d1) * t.g1 * t.l1;
                const float vr = (a < 1.0f ? (1.0f - a) * r.read (t.d0) * t.g0 * t.r0 : 0.0f) + a * r.read (t.d1) * t.g1 * t.r1;
                yl += vl; yr += vr;
            }
            yl *= norm; yr *= norm;
            const float inL = c.freeze ? 0.0f : L[i], inR = c.freeze ? 0.0f : R[i];
            l.push (softClip (inL + fb * yl * norm));
            r.push (softClip (inR + fb * yr * norm));
            L[i] = yl; R[i] = yr;
        }
    }
private:
    struct Tap { float d0 = 100, d1 = 100, g0 = 0, g1 = 0, l0 = 0.7f, r0 = 0.7f, l1 = 0.7f, r1 = 0.7f, x = 1; };
    void draw (const Ctx& c, const float* p, int ntaps)
    {
        ++draws;
        const float range = expMap (p[0], 50.0f, 2000.0f) * 0.001f * (float) fs;
        const float q = (float) (0.25 * c.samplesPerBeat());
        for (int k = 0; k < 8; ++k)
        {
            auto& t = taps[k];
            const uint32_t s = salt (c, 0x5ca7u + (uint32_t) k);
            const auto u = [&] (uint32_t x) { return hUni ((uint32_t) draws, x, s); };
            t.d0 = t.d1; t.g0 = t.g1; t.l0 = t.l1; t.r0 = t.r1; t.x = 0.0f;
            float d;
            if (u (1) < p[3] && q > 8.0f) d = q * (float) (1 + (int) (u (2) * juce::jmax (1.0f, range / q)));
            else d = 0.02f * (float) fs + u (2) * range;
            t.d1 = juce::jlimit (8.0f, (float) l.size() - 16.0f, d);
            t.g1 = k < ntaps ? 0.35f + 0.65f * u (3) : 0.0f;
            const float pan = hBi ((uint32_t) draws, 4u, s) * p[4];
            t.l1 = std::cos ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
            t.r1 = std::sin ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
        }
    }
    double fs = 48000;
    Line l, r;
    Tap taps[8];
    int64_t lastIdx = INT64_MIN;
    int countdown = 0;
    int draws = 0;
};

// ===========================================================================
// 3. Stutter / beat-repeat
class StutterE : public Engine
{
public:
    void prepare (double f) override
    {
        fs = f;
        size = juce::nextPowerOfTwo ((int) (fs * 6.0));
        bl.assign ((size_t) size, 0.0f); br.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset() override
    {
        std::fill (bl.begin(), bl.end(), 0.0f); std::fill (br.begin(), br.end(), 0.0f);
        wc = 0; segIdx = INT64_MIN; active = false; capStart = 0; segStart = 0;
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const double spb = c.samplesPerBeat();
        const double segB = segBeats (pick (p[2], 5));
        const double slice = juce::jmax (64.0, stutterDivBeats (pick (p[0], 9)) * spb);
        const float step = bip (p[3]) * 3.0f;
        const int mask = size - 1;
        for (int i = 0; i < n; ++i)
        {
            const double ppq = c.ppq + i * c.ppqPerSample;
            const int64_t idx = (int64_t) std::floor (ppq / segB);
            if (c.freeze)
            {
                if (! active) { active = true; capStart = wc - (int64_t) slice; segStart = ppq - slice / spb; }
            }
            else if (idx != segIdx)
            {
                segIdx = idx;
                active = p[1] > 0.0f && hUni ((uint32_t) idx, 0x5713u, salt (c, 3)) < p[1];
                capStart = wc; segStart = (double) idx * segB;
            }

            const float inL = L[i], inR = R[i];
            float oL = inL * p[5], oR = inR * p[5];
            if (active)
            {
                const double posS = (ppq - segStart) * spb;
                int64_t rep = (int64_t) std::floor (posS / slice);
                const double in = posS - (double) rep * slice;
                if (rep > 16) rep = 1 + (rep - 1) % 16;
                const double xf = juce::jmin (96.0, slice * 0.25);
                const float fade = (float) juce::jmin (1.0, in / xf, (slice - in) / xf);
                if (rep <= 0) { oL = inL * (in > slice - xf ? fade : 1.0f); oR = inR * (in > slice - xf ? fade : 1.0f); }
                else
                {
                    const double rate = juce::jlimit (0.25, 4.0, std::pow (2.0, step * (double) rep / 12.0));
                    const bool rev = hUni ((uint32_t) (segIdx * 64 + rep), 0x77u, salt (c, 4)) < p[4];
                    const double rp = in * rate;
                    if (rp < slice - 2.0)
                    {
                        const double src = (double) capStart + (rev ? slice - 2.0 - rp : rp);
                        const int64_t i0 = (int64_t) std::floor (src);
                        const float fr = (float) (src - (double) i0);
                        if (wc - i0 > 2 && wc - i0 < size - 16)
                        {
                            const int a = (int) (i0 & mask), b = (int) ((i0 + 1) & mask);
                            const float g = juce::jmin (fade, (float) juce::jmin (1.0, (slice - rp) / xf));
                            oL = (bl[(size_t) a] + fr * (bl[(size_t) b] - bl[(size_t) a])) * g;
                            oR = (br[(size_t) a] + fr * (br[(size_t) b] - br[(size_t) a])) * g;
                        }
                        else { oL = oR = 0.0f; if (! c.freeze) active = false; }
                    }
                    else oL = oR = 0.0f;
                }
            }
            if (! c.freeze)
            {
                bl[(size_t) (wc & mask)] = inL; br[(size_t) (wc & mask)] = inR;
                ++wc;
            }
            L[i] = oL; R[i] = oR;
        }
    }
private:
    double fs = 48000;
    int size = 0;
    std::vector<float> bl, br;
    int64_t wc = 0, segIdx = INT64_MIN, capStart = 0;
    double segStart = 0;
    bool active = false;
};

// ===========================================================================
// 4. Granulaire
class GranularE : public Engine
{
public:
    void prepare (double f) override
    {
        fs = f;
        size = juce::nextPowerOfTwo ((int) (fs * 4.8));
        bl.assign ((size_t) size, 0.0f); br.assign ((size_t) size, 0.0f);
        for (int i = 0; i < 1024; ++i) win[i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / 1023.0f);
        reset();
    }
    void reset() override
    {
        std::fill (bl.begin(), bl.end(), 0.0f); std::fill (br.begin(), br.end(), 0.0f);
        wc = 0; countdown = 0; spawned = 0; tries = 0; norm = 1.0f; burst = 0; burstCount = 0; burstPos = 0;
        for (auto& g : grains) g.on = false;
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const float lenS = expMap (p[0], 10.0f, 500.0f) * 0.001f * (float) fs;
        const float dens = expMap (p[1], 1.0f, 80.0f);
        const float overlap = dens * lenS / (float) fs;
        const float targetNorm = 1.0f / std::sqrt (juce::jmax (1.0f, overlap));
        const int mask = size - 1;
        for (int i = 0; i < n; ++i)
        {
            if (! c.freeze) { bl[(size_t) (wc & mask)] = L[i]; br[(size_t) (wc & mask)] = R[i]; ++wc; }
            const float hv = c.hitAt (i);
            if (hv > 0.0f && p[5] > 0.02f)
            {
                burst = 1 + (int) std::round (p[5] * 10.0f);
                burstPos = wc; burstCount = 0; burstVel = hv;
            }
            if (burst > 0 && --burstCount <= 0)
            {
                spawn (c, p, lenS, burstPos, burstVel);
                --burst;
                burstCount = (int) ((float) fs / dens * 0.5f) + 1;
            }
            if (--countdown <= 0)
            {
                ++tries;
                if (hUni ((uint32_t) tries, 0x9a2u, salt (c, 5)) >= p[5]) spawn (c, p, lenS, -1, 1.0f);
                countdown = (int) ((float) fs / dens * (0.3f + 1.4f * hUni ((uint32_t) tries, 0x9a1u, salt (c, 5))));
            }
            norm += (targetNorm - norm) * 0.001f;
            float ol = 0, orr = 0;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float ph = (float) g.t / (float) g.len;
                const float wv = win[juce::jlimit (0, 1023, (int) (ph * 1023.0f))] * g.amp;
                const double pos = g.rev ? g.start + (double) (g.len - g.t) * g.inc : g.start + (double) g.t * g.inc;
                const int64_t i0 = (int64_t) std::floor (pos);
                const float fr = (float) (pos - (double) i0);
                const int a = (int) (i0 & mask), b = (int) ((i0 + 1) & mask);
                const float vl = bl[(size_t) a] + fr * (bl[(size_t) b] - bl[(size_t) a]);
                const float vr = br[(size_t) a] + fr * (br[(size_t) b] - br[(size_t) a]);
                const float m = 0.5f * (vl + vr);
                ol += wv * (g.gl * (0.5f * vl + 0.5f * m));
                orr += wv * (g.gr * (0.5f * vr + 0.5f * m));
                if (++g.t >= g.len) g.on = false;
            }
            L[i] = ol * norm; R[i] = orr * norm;
        }
    }
private:
    struct Grain { bool on = false, rev = false; double start = 0, inc = 1; int len = 1, t = 0; float gl = 0.7f, gr = 0.7f, amp = 1.0f; };
    void spawn (const Ctx& c, const float* p, float lenS, int64_t from, float vel)
    {
        Grain* g = nullptr;
        for (auto& x : grains) if (! x.on) { g = &x; break; }
        ++spawned;
        if (g == nullptr) return;
        const uint32_t s = salt (c, 6);
        const auto u = [&] (uint32_t k) { return hUni ((uint32_t) spawned, k, s); };
        const float semis = semis48 (p[3]) + randomInterval (p[4], (uint32_t) spawned, 0x11u, s);
        const double rate = std::pow (2.0, (double) semis / 12.0);
        const int len = juce::jmax (32, (int) (lenS * (0.8f + 0.4f * u (2))));
        const double span = (double) len * rate;
        const double spray = (double) (p[2] * p[2] * 3.0f) * fs * u (3);
        double start = from >= 0 ? (double) from - 64.0 + spray * 0.15 : (double) wc - span - 8.0 - spray;
        if (start + span > (double) wc - 8.0) start = (double) wc - span - 8.0;
        const double oldest = (double) wc - (double) size + 64.0;
        if (start < oldest) start = oldest;
        if (start < 0.0 && wc < (int64_t) size) start = juce::jmax (0.0, start);
        g->on = true; g->t = 0; g->len = len; g->inc = rate; g->start = start;
        g->rev = u (4) < p[4] * 0.5f;
        g->amp = vel;
        const float pan = hBi ((uint32_t) spawned, 5u, s) * 0.8f;
        g->gl = std::cos ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
        g->gr = std::sin ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
    }
    double fs = 48000;
    int size = 0;
    std::vector<float> bl, br;
    int64_t wc = 0;
    int countdown = 0, spawned = 0, burst = 0, burstCount = 0, tries = 0;
    int64_t burstPos = 0;
    float burstVel = 1.0f;
    float norm = 1.0f;
    float win[1024] {};
    Grain grains[48];
};

// ===========================================================================
// 6. Shimmer
class ShimmerE : public Engine
{
public:
    void prepare (double f) override { fs = f; v.prepare (f); sl.prepare (f); sr.prepare (f); reset(); }
    void reset() override { v.clear(); sl.clear(); sr.clear(); fbL = fbR = hpL = hpR = lpL = lpR = 0; cnt = 0; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        v.setParams (p[1], expMap (p[0], 0.5f, 30.0f), p[4], 0.02f, p[5], 1.4f);
        const float amt = p[2] * 0.7f;
        const int iv = pick (p[3], 8);
        const float W = 0.09f * (float) fs;
        const float hpA = onePoleCoef (180.0f, fs), lpA = onePoleCoef (9000.0f, fs);
        for (int i = 0; i < n; ++i)
        {
            const int k = (int) (cnt++ / (int64_t) (fs * 0.5));
            const float ratio = std::pow (2.0f, shimmerSemis (iv, k) / 12.0f);
            float a = sl.process (fbL, ratio, W), b = sr.process (fbR, ratio, W);
            hpL += hpA * (a - hpL); hpR += hpA * (b - hpR);
            a -= hpL; b -= hpR;
            lpL += lpA * (a - lpL); lpR += lpA * (b - lpR);
            const float inL = L[i] + std::tanh (lpL * amt * 1.5f) * 0.66f;
            const float inR = R[i] + std::tanh (lpR * amt * 1.5f) * 0.66f;
            float oL, oR;
            v.process (inL, inR, oL, oR, c.freeze);
            if (c.freeze) { oL = softClip (oL); oR = softClip (oR); }
            fbL = oL; fbR = oR;
            L[i] = oL; R[i] = oR;
        }
    }
private:
    double fs = 48000;
    Verb v;
    Shifter sl, sr;
    float fbL = 0, fbR = 0, hpL = 0, hpR = 0, lpL = 0, lpR = 0;
    int64_t cnt = 0;
};

// ===========================================================================
// 7. Reverbe inversee
class ReverseE : public Engine
{
public:
    void prepare (double f) override
    {
        fs = f;
        maxLen = (int) (fs * 4.0);
        for (auto& b : buf) b.assign ((size_t) maxLen, 0.0f);
        v.prepare (f);
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        v.clear(); cur = 0; pos = 0; prevLen = 0; lastIdx = INT64_MIN;
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        v.setParams (p[2], expMap (p[1], 0.3f, 12.0f), p[5], 0.0f, 0.3f, 1.2f);
        double beats = revLenBeats (pick (p[0], 6));
        const double spb = c.samplesPerBeat();
        while (beats > 0.25 && beats * spb > maxLen) beats *= 0.5;
        const int chunk = juce::jlimit (256, maxLen, (int) (beats * spb));
        const float raw = p[3], swell = p[4] * 3.0f;
        const int edge = juce::jmin (240, chunk / 8);
        for (int i = 0; i < n; ++i)
        {
            const double ppq = c.ppq + i * c.ppqPerSample;
            const int64_t idx = (int64_t) std::floor (ppq / beats);
            if (c.freeze)
            {
                if (prevLen > 0 && pos >= prevLen) pos = 0;
            }
            else if (idx != lastIdx || pos >= maxLen)
            {
                if (lastIdx != INT64_MIN) { prevLen = juce::jmin (pos, maxLen); cur ^= 1; }
                lastIdx = idx; pos = 0;
            }
            float wl, wr;
            v.process (L[i], R[i], wl, wr, false);
            if (! c.freeze && pos < maxLen)
            {
                buf[cur * 2][(size_t) pos] = wl * (1.0f - raw) + L[i] * raw;
                buf[cur * 2 + 1][(size_t) pos] = wr * (1.0f - raw) + R[i] * raw;
            }
            float oL = 0, oR = 0;
            const int prev = cur ^ 1;
            if (prevLen > 0 && pos < prevLen)
            {
                const int rp = prevLen - 1 - pos;
                float env = juce::jmin (1.0f, (float) pos / (float) edge, (float) (prevLen - pos) / (float) edge);
                if (swell > 0.0f) env *= std::pow ((float) pos / (float) prevLen, swell);
                oL = buf[prev * 2][(size_t) rp] * env;
                oR = buf[prev * 2 + 1][(size_t) rp] * env;
            }
            ++pos;
            L[i] = oL * 1.4f; R[i] = oR * 1.4f;
        }
    }
private:
    double fs = 48000;
    Verb v;
    std::vector<float> buf[4];
    int maxLen = 0, cur = 0, pos = 0, prevLen = 0;
    int64_t lastIdx = INT64_MIN;
};

// ===========================================================================
// 8. Chorus / ensemble
class ChorusE : public Engine
{
public:
    void prepare (double f) override { fs = f; l.prepare ((int) (fs * 0.09)); r.prepare ((int) (fs * 0.09)); reset(); }
    void reset() override
    {
        l.clear(); r.clear(); ph = 0; fbL = fbR = 0;
        for (int k = 0; k < 4; ++k) rnd[k].reset (101u + (uint32_t) k * 31u);
    }
    void process (float* L, float* R, int n, const Ctx&, const float* p) override
    {
        const float rate = expMap (p[0], 0.03f, 8.0f);
        const int nv = 1 + pick (p[2], 4);
        const float base = (5.0f + 35.0f * p[3]) * 0.001f * (float) fs;
        const float depth = p[1] * base * 0.8f;
        const float fb = p[4] * 0.6f;
        const float wOff = p[5] * kPi * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            ph += rate / (float) fs; if (ph >= 1.0f) ph -= 1.0f;
            float yl = 0, yr = 0;
            for (int k = 0; k < nv; ++k)
            {
                const float a = kTwoPi * (ph + (float) k / (float) nv);
                const float dr = rnd[k].next (0.4f + rate * 0.3f, fs) * 0.25f;
                const float dl = base + depth * (0.5f + 0.5f * std::sin (a) + dr);
                const float drr = base + depth * (0.5f + 0.5f * std::sin (a + wOff) - dr);
                yl += l.read (juce::jmax (2.0f, dl)); yr += r.read (juce::jmax (2.0f, drr));
            }
            const float g = 1.0f / std::sqrt ((float) nv);
            yl *= g; yr *= g;
            l.push (softClip (L[i] + fbL * fb)); r.push (softClip (R[i] + fbR * fb));
            fbL = yl * g; fbR = yr * g;
            L[i] = yl; R[i] = yr;
        }
    }
private:
    double fs = 48000;
    Line l, r;
    float ph = 0, fbL = 0, fbR = 0;
    SmoothRnd rnd[4];
};

// ===========================================================================
// 9. Flanger
class FlangerE : public Engine
{
public:
    void prepare (double f) override { fs = f; l.prepare ((int) (fs * 0.03)); r.prepare ((int) (fs * 0.03)); reset(); }
    void reset() override { l.clear(); r.clear(); ph = 0; yl = yr = 0; rnd.reset (55); }
    void process (float* L, float* R, int n, const Ctx&, const float* p) override
    {
        const float rate = expMap (p[0], 0.02f, 5.0f);
        const float man = expMap (p[3], 0.1f, 10.0f) * 0.001f * (float) fs;
        const float depth = p[1] * 0.92f;
        const float fb = bip (p[2]) * 0.95f;
        const float off = p[4] * kPi;
        const float rmix = p[5];
        for (int i = 0; i < n; ++i)
        {
            ph += rate / (float) fs; if (ph >= 1.0f) ph -= 1.0f;
            const float rn = 0.5f + 0.5f * rnd.next (rate * 2.0f, fs);
            const float lfoL = (1.0f - rmix) * (0.5f + 0.5f * std::sin (kTwoPi * ph)) + rmix * rn;
            const float lfoR = (1.0f - rmix) * (0.5f + 0.5f * std::sin (kTwoPi * ph + off)) + rmix * (1.0f - rn * (off / kPi) - (1.0f - off / kPi) * (1.0f - rn));
            const float dL = juce::jmax (2.0f, man * (1.0f - depth * lfoL) + 2.0f);
            const float dR = juce::jmax (2.0f, man * (1.0f - depth * lfoR) + 2.0f);
            const float a = l.readH (dL), b = r.readH (dR);
            l.push (softClip (L[i] + fb * a)); r.push (softClip (R[i] + fb * b));
            L[i] = 0.5f * (L[i] + a); R[i] = 0.5f * (R[i] + b);
        }
    }
private:
    double fs = 48000;
    Line l, r;
    float ph = 0, yl = 0, yr = 0;
    SmoothRnd rnd;
};

// ===========================================================================
// 10. Phaser
class PhaserE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override { for (auto& s : st) s = {}; ph = 0; lastL = lastR = 0; }
    void process (float* L, float* R, int n, const Ctx&, const float* p) override
    {
        static const int nst[] { 2, 4, 6, 8, 12 };
        const int stages = nst[pick (p[3], 5)];
        const float rate = expMap (p[0], 0.02f, 8.0f);
        const float centre = expMap (p[4], 100.0f, 4000.0f);
        const float depth = p[1] * 3.0f;
        const float fb = p[2] * 0.9f;
        const float off = p[5] * kPi;
        ph += rate * (float) n / (float) fs; ph -= std::floor (ph);
        auto coef = [&] (float lfo)
        {
            const float f = juce::jlimit (30.0f, (float) fs * 0.42f, centre * std::pow (2.0f, depth * lfo));
            const float t = std::tan (kPi * f / (float) fs);
            return (t - 1.0f) / (t + 1.0f);
        };
        const float aL = coef (std::sin (kTwoPi * ph)), aR = coef (std::sin (kTwoPi * ph + off));
        for (int i = 0; i < n; ++i)
        {
            float x = L[i] + fb * lastL, y = R[i] + fb * lastR;
            for (int k = 0; k < stages; ++k)
            {
                auto& s = st[k];
                const float ox = aL * x + s.xl - aL * s.yl; s.xl = x; s.yl = ox; x = ox;
                const float oy = aR * y + s.xr - aR * s.yr; s.xr = y; s.yr = oy; y = oy;
            }
            lastL = softClip (x); lastR = softClip (y);
            L[i] = 0.5f * (L[i] + x); R[i] = 0.5f * (R[i] + y);
        }
    }
private:
    struct St { float xl = 0, yl = 0, xr = 0, yr = 0; };
    double fs = 48000;
    St st[12];
    float ph = 0, lastL = 0, lastR = 0;
};

// ===========================================================================
// 11. Filtre multimode
class FilterE : public Engine
{
public:
    void prepare (double f) override { fs = f; cl.prepare ((int) (fs * 0.06)); cr.prepare ((int) (fs * 0.06)); reset(); }
    void reset() override
    {
        fl.clear(); fr.clear(); cl.clear(); cr.clear(); env = 0; ph = 0; lpcL = lpcR = 0; hitEnv = 0; rndOct = 0; hits = 0;
        for (auto& f : form) f.clear();
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int type = pick (p[0], 6);
        float hv = 0;
        for (int i = 0; i < n; ++i) hv = juce::jmax (hv, c.hitAt (i));
        if (hv > 0.0f)
        {
            hitEnv = juce::jmax (hitEnv, hv);
            ++hits;
            rndOct = hBi ((uint32_t) hits, 0xF17u, salt (c, 12)) * p[4] * 3.0f;
        }
        const float envN = hitEnv;
        hitEnv *= std::exp (-(float) n / (expMap (p[5], 10.0f, 1000.0f) * 0.001f * (float) fs));
        const float oct = envN * bip (p[3]) * 5.0f + rndOct;

        if (type == 5)   // formants (voyelles)
        {
            static const float F[5][3] { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            static const float G[3] { 1.0f, 0.6f, 0.3f };
            const float vpos = juce::jlimit (0.0f, 0.999f, p[1] + oct * 0.1f) * 4.0f;
            const int v0 = (int) vpos; const float t = vpos - (float) v0;
            const float k = 1.0f / (4.0f + 18.0f * p[2]);
            for (int j = 0; j < 3; ++j)
            {
                const float f = F[v0][j] + t * (F[juce::jmin (4, v0 + 1)][j] - F[v0][j]);
                form[j].set (f, (float) fs, k); form[j + 3].set (f * 1.02f, (float) fs, k);
            }
            for (int i = 0; i < n; ++i)
            {
                float yl = 0, yr = 0, lo, b, h;
                for (int j = 0; j < 3; ++j)
                {
                    form[j].process (L[i], lo, b, h); yl += b * G[j] * k * 3.0f;
                    form[j + 3].process (R[i], lo, b, h); yr += b * G[j] * k * 3.0f;
                }
                L[i] = yl * 2.0f; R[i] = yr * 2.0f;
            }
            return;
        }

        const float cut = juce::jlimit (20.0f, 18000.0f, expMap (p[1], 20.0f, 18000.0f) * std::pow (2.0f, oct));
        if (type == 4)   // comb
        {
            const float d = juce::jlimit (4.0f, (float) cl.size() - 8.0f, (float) fs / juce::jlimit (25.0f, 5000.0f, cut));
            const float fb = p[2] * 0.97f;
            const float lpA = 0.6f;
            for (int i = 0; i < n; ++i)
            {
                const float a1 = cl.readH (d), b1 = cr.readH (d * 1.003f);
                lpcL += lpA * (a1 - lpcL); lpcR += lpA * (b1 - lpcR);
                cl.push (softClip (L[i] + fb * lpcL)); cr.push (softClip (R[i] + fb * lpcR));
                L[i] = 0.5f * (L[i] + a1); R[i] = 0.5f * (R[i] + b1);
            }
            return;
        }
        const float k = 2.0f - 1.88f * p[2];
        fl.set (cut, (float) fs, k); fr.set (cut, (float) fs, k);
        for (int i = 0; i < n; ++i)
        {
            float lo, b, h, lo2, b2, h2;
            fl.process (L[i], lo, b, h); fr.process (R[i], lo2, b2, h2);
            switch (type)
            {
                case 0: L[i] = lo; R[i] = lo2; break;
                case 1: L[i] = b * k; R[i] = b2 * k; break;
                case 2: L[i] = h; R[i] = h2; break;
                default: L[i] = lo + h; R[i] = lo2 + h2; break;
            }
            L[i] = softClip (L[i]); R[i] = softClip (R[i]);
        }
    }
private:
    double fs = 48000;
    Svf fl, fr, form[6];
    Line cl, cr;
    float env = 0, ph = 0, lpcL = 0, lpcR = 0, hitEnv = 0, rndOct = 0;
    int hits = 0;
};

// ===========================================================================
// 12. Pitch
class PitchE : public Engine
{
public:
    void prepare (double f) override { fs = f; sl.prepare (f); sr.prepare (f); reset(); }
    void reset() override { sl.clear(); sr.clear(); fbL = fbR = 0; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        float semi = semis48 (p[0]) + bip (p[1]) * 0.5f;
        if (p[4] > 0.0f && hUni ((uint32_t) c.tick, 0x9177u, salt (c, 7)) < juce::jmin (1.0f, p[4] * 1.5f))
            semi += randomInterval (p[4], (uint32_t) c.tick, 0x9178u, salt (c, 7));
        const float det = p[5] * 0.3f;
        const float rl = std::pow (2.0f, (semi - det) / 12.0f), rr = std::pow (2.0f, (semi + det) / 12.0f);
        const float W = expMap (p[2], 15.0f, 150.0f) * 0.001f * (float) fs;
        const float fb = p[3] * 0.85f;
        for (int i = 0; i < n; ++i)
        {
            const float a = sl.process (L[i] + fb * fbL, rl, W);
            const float b = sr.process (R[i] + fb * fbR, rr, W);
            fbL = softClip (a); fbR = softClip (b);
            L[i] = a; R[i] = b;
        }
    }
private:
    double fs = 48000;
    Shifter sl, sr;
    float fbL = 0, fbR = 0;
};

// ===========================================================================
// 13. Degrade
class DegradeE : public Engine
{
public:
    void prepare (double f) override { fs = f; l.prepare ((int) (fs * 0.05)); r.prepare ((int) (fs * 0.05)); reset(); }
    void reset() override { l.clear(); r.clear(); hold = 1; hl = hr = 0; ph1 = ph2 = 0; t1l = t1r = t2l = t2r = 0; rng = FastRng (0xdeadu); rnd.reset (9); }
    void process (float* L, float* R, int n, const Ctx&, const float* p) override
    {
        const float drive = std::pow (10.0f, p[0] * 30.0f / 20.0f);
        const float comp = 1.0f / std::sqrt (drive);
        const float bits = expMap (p[1], 16.0f, 2.0f);
        const float q = std::pow (2.0f, bits - 1.0f);
        const float rateRatio = expMap (p[2], 48000.0f, 300.0f) / (float) fs;
        const float wow = p[3] * 0.0025f * (float) fs;
        const float noise = p[4];
        const float lpA = onePoleCoef (expMap (p[5], 18000.0f, 800.0f), fs);
        for (int i = 0; i < n; ++i)
        {
            float a = std::tanh (L[i] * drive) * (p[0] > 0.01f ? comp * 1.4f : 1.0f);
            float b = std::tanh (R[i] * drive) * (p[0] > 0.01f ? comp * 1.4f : 1.0f);
            if (p[0] <= 0.01f) { a = L[i]; b = R[i]; }
            if (bits < 15.5f) { a = std::round (a * q) / q; b = std::round (b * q) / q; }
            hold += juce::jmin (1.0f, rateRatio);
            if (hold >= 1.0f) { hold -= 1.0f; hl = a; hr = b; }
            if (rateRatio < 0.999f) { a = hl; b = hr; }
            if (wow > 0.0f)
            {
                ph1 += 0.45f / (float) fs; if (ph1 >= 1) ph1 -= 1;
                ph2 += 6.5f / (float) fs; if (ph2 >= 1) ph2 -= 1;
                const float d = 3.0f + wow * (1.0f + 0.6f * std::sin (kTwoPi * ph1) + 0.15f * std::sin (kTwoPi * ph2) + 0.35f * rnd.next (1.3f, fs));
                l.push (a); r.push (b);
                a = l.readH (d + 2.0f); b = r.readH (d + 2.0f);
            }
            if (noise > 0.0f)
            {
                const float hiss = rng.bi() * noise * 0.02f;
                float crack = 0.0f;
                if (rng.uni() < noise * 0.0004f) crack = rng.bi() * noise * 0.5f;
                a += hiss + crack; b += hiss * 0.8f + rng.bi() * noise * 0.006f + crack * 0.7f;
            }
            t1l += lpA * (a - t1l); t2l += lpA * (t1l - t2l);
            t1r += lpA * (b - t1r); t2r += lpA * (t1r - t2r);
            L[i] = t2l; R[i] = t2r;
        }
    }
private:
    double fs = 48000;
    Line l, r;
    float hold = 1, hl = 0, hr = 0, ph1 = 0, ph2 = 0, t1l = 0, t1r = 0, t2l = 0, t2r = 0;
    FastRng rng;
    SmoothRnd rnd;
};

// ===========================================================================
// 14. Ring / frequency shift / AM
class RingE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override { phL = phR = lfo = 0; hl.clear(); hr.clear(); fbL = fbR = 0; fCur = -1; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int mode = pick (p[0], 3);
        const float rj = hBi ((uint32_t) c.tick, 0x41u, salt (c, 8)) * p[2];
        lfo += 0.13f * (float) n / (float) fs; lfo -= std::floor (lfo);
        const float lm = std::sin (kTwoPi * lfo) * p[5];
        float f, fR;
        if (mode == 1)
        {
            const float x = bip (p[1]);
            f = (x < 0 ? -1.0f : 1.0f) * x * x * 2000.0f + rj * 400.0f + lm * 60.0f;
            fR = f + p[3] * 25.0f;
        }
        else
        {
            f = expMap (p[1], 0.1f, 4000.0f) * std::pow (2.0f, rj * 2.0f + lm);
            fR = f * (1.0f + p[3] * 0.5f);
        }
        if (fCur < -0.5f) { fCur = f; fCurR = fR; }
        const float fb = mode == 1 ? p[4] * 0.9f : p[4] * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            fCur += 0.004f * (f - fCur); fCurR += 0.004f * (fR - fCurR);
            phL += fCur / (float) fs; phL -= std::floor (phL);
            phR += fCurR / (float) fs; phR -= std::floor (phR);
            const float sL = std::sin (kTwoPi * phL), sR = std::sin (kTwoPi * phR);
            const float xl = L[i] + fb * fbL, xr = R[i] + fb * fbR;
            float yl, yr;
            if (mode == 1)
            {
                float re, im;
                hl.process (xl, re, im); yl = re * std::cos (kTwoPi * phL) - im * sL;
                hr.process (xr, re, im); yr = re * std::cos (kTwoPi * phR) - im * sR;
            }
            else if (mode == 2) { yl = xl * (0.5f + 0.5f * sL); yr = xr * (0.5f + 0.5f * sR); }
            else { yl = xl * sL; yr = xr * sR; }
            fbL = softClip (yl); fbR = softClip (yr);
            L[i] = yl; R[i] = yr;
        }
    }
private:
    double fs = 48000;
    float phL = 0, phR = 0, lfo = 0, fbL = 0, fbR = 0, fCur = -1, fCurR = 0;
    Hilbert hl, hr;
};

// ===========================================================================
// 15. Gate rythmique
class GateE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override { loop = INT64_MIN; env = 0; for (auto& x : lvl) x = 0; for (auto& x : pan) x = 0; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const double div = gateDivBeats (pick (p[0], 6));
        const double bnd = 1.0 + p[4] * 0.5;
        const float shape = p[3];
        const float duty = shape > 0.5f ? 1.0f - (shape - 0.5f) * 1.3f : 1.0f;
        const double spb = c.samplesPerBeat();
        const float stepS = (float) (div * spb);
        const float atk = 1.0f - std::exp (-1.0f / juce::jmax (8.0f, (shape < 0.5f ? (0.5f - shape) * 0.8f : 0.0f) * stepS + 0.0008f * (float) fs));
        const float rel = 1.0f - std::exp (-1.0f / juce::jmax (8.0f, (shape < 0.5f ? (0.5f - shape) * 1.2f : 0.0f) * stepS + 0.002f * (float) fs));
        for (int i = 0; i < n; ++i)
        {
            const double t = (c.ppq + i * c.ppqPerSample) / div;
            const double pair = std::floor (t * 0.5);
            const double within = t - pair * 2.0;
            int64_t step; double local;
            if (within < bnd) { step = (int64_t) pair * 2; local = within / bnd; }
            else { step = (int64_t) pair * 2 + 1; local = (within - bnd) / (2.0 - bnd); }
            const int64_t lp = (int64_t) std::floor ((double) step / 16.0);
            if (lp != loop) mutate (c, p, lp);
            const int k = (int) (((step % 16) + 16) % 16);
            const float target = (lvl[k] > 0.0f && local < duty) ? lvl[k] : 0.0f;
            env += (target - env) * (target > env ? atk : rel);
            const float pn = pan[k] * p[5];
            const float gl = juce::jmin (1.0f, 1.0f - pn), gr = juce::jmin (1.0f, 1.0f + pn);
            L[i] *= env * gl; R[i] *= env * gr;
        }
    }
private:
    void mutate (const Ctx& c, const float* p, int64_t lp)
    {
        const bool first = loop == INT64_MIN || std::abs (lp - loop) > 1;
        loop = lp;
        const uint32_t s = salt (c, 9);
        for (int k = 0; k < 16; ++k)
        {
            const uint32_t gen = first ? 0u : (uint32_t) lp;
            if (first || hUni (gen, (uint32_t) k + 100u, s) < p[2])
            {
                const bool open = hUni (gen, (uint32_t) k, s) < p[1];
                lvl[k] = open ? 0.6f + 0.4f * hUni (gen, (uint32_t) k + 40u, s) : 0.0f;
                pan[k] = hBi (gen, (uint32_t) k + 70u, s);
            }
        }
    }
    double fs = 48000;
    int64_t loop = INT64_MIN;
    float env = 0, lvl[16] {}, pan[16] {};
};

// ===========================================================================
// 16. Resonateur accorde
class ResonatorE : public Engine
{
public:
    void prepare (double f) override { fs = f; for (auto& cb : comb) cb.l.prepare ((int) (fs / 18.0)); reset(); }
    void reset() override
    {
        for (auto& cb : comb) { cb.l.clear(); cb.lp = 0; cb.d = -1; }
        lastTick = INT64_MIN; lastKey = -1;
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int rsel = pick (p[0], 13);
        const int fundMidi = (int) std::round (69.0f + 12.0f * std::log2 (juce::jmax (20.0f, c.fund) / 440.0f));
        const int root = rsel == 12 ? ((fundMidi % 12) + 12) % 12 : rsel;
        const int scale = pick (p[1], 12), range = 1 + pick (p[4], 4);
        baseNote = rsel == 12 ? juce::jlimit (24, 72, fundMidi - root + 12) : 36;
        const int key = root * 1000 + scale * 10 + range;
        if (key != lastKey) { lastKey = key; for (int k = 0; k < 6; ++k) choose (c, k, root, scale, range, 0); }
        if (c.tick != lastTick)
        {
            if (lastTick != INT64_MIN)
                for (int k = 0; k < 6; ++k)
                    if (hUni ((uint32_t) c.tick, (uint32_t) k, salt (c, 10)) < p[5]) choose (c, k, root, scale, range, (uint32_t) c.tick);
            lastTick = c.tick;
        }
        const float g = c.freeze ? 0.99999f : 1.0f - 0.1f * std::pow (0.004f, p[2]);
        const float lpA = c.freeze ? 1.0f : 0.15f + 0.85f * p[3];
        const float inG = c.freeze ? 0.0f : juce::jmin (1.0f, (1.0f - g) * 10.0f) * 0.8f;
        for (int i = 0; i < n; ++i)
        {
            float yl = 0, yr = 0;
            for (int k = 0; k < 6; ++k)
            {
                auto& cb = comb[k];
                cb.d += (cb.target - cb.d) * 0.002f;
                const float v = cb.l.readH (cb.d);
                cb.lp += lpA * (v - cb.lp);
                const float x = (k < 3 ? L[i] : R[i]) * inG;
                cb.l.push (x + g * cb.lp);
                if (k < 3) yl += v; else yr += v;
            }
            L[i] = softClip (yl * 0.6f); R[i] = softClip (yr * 0.6f);
        }
    }
private:
    void choose (const Ctx& c, int k, int root, int scale, int range, uint32_t gen)
    {
        const auto& deg = alea::scaleDegrees (resScaleShared (scale));
        const uint32_t s = salt (c, 11 + (uint32_t) k);
        const int d = deg[(size_t) juce::jlimit (0, (int) deg.size() - 1, (int) (hUni (gen, 1u, s) * (float) deg.size()))];
        const int oct = (int) (hUni (gen, 2u, s) * (float) range);
        const int note = baseNote + root + d + 12 * oct + (k % 3 == 0 ? 0 : 12);
        const float hzv = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
        comb[k].target = juce::jlimit (4.0f, (float) comb[k].l.size() - 8.0f, (float) fs / hzv);
        if (comb[k].d < 0) comb[k].d = comb[k].target;
    }
    struct Comb { Line l; float lp = 0, d = -1, target = 100; };
    double fs = 48000;
    Comb comb[6];
    int64_t lastTick = INT64_MIN;
    int lastKey = -1, baseNote = 36;
};

// ===========================================================================
// 17. Transient shaper
class TransientE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override { e1 = e2 = e3 = e4 = 0; lpL = lpR = 0; rA = rS = 0; hits = 0; gSm = 1.0f; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const float scale = expMap (1.0f - p[2], 0.35f, 3.0f) * juce::jlimit (0.5f, 2.5f, c.decayMs / 300.0f);
        auto coef = [this] (float ms) { return std::exp (-1.0f / (ms * 0.001f * (float) fs)); };
        const float a1 = coef (0.3f), r1 = coef (40.0f * scale);
        const float a2 = coef (12.0f * scale), r2 = coef (40.0f * scale);
        const float a3 = coef (0.3f), r3 = coef (250.0f * scale);
        const float a4 = coef (0.3f), r4 = coef (25.0f * scale);
        const int band = pick (p[3], 3);
        const float split = onePoleCoef (juce::jlimit (60.0f, 4000.0f, c.body * 1.5f), fs);
        const float drive = 1.0f + p[5] * 8.0f;
        const float clipNorm = 1.0f / std::tanh (drive);
        for (int i = 0; i < n; ++i)
        {
            if (c.hitAt (i) > 0.0f)
            {
                ++hits;
                rA = hBi ((uint32_t) hits, 0x7a1u, salt (c, 20)) * p[4];
                rS = hBi ((uint32_t) hits, 0x7a2u, salt (c, 20)) * p[4];
            }
            lpL += split * (L[i] - lpL); lpR += split * (R[i] - lpR);
            const float lowL = lpL, lowR = lpR, highL = L[i] - lpL, highR = R[i] - lpR;
            const float det = band == 1 ? juce::jmax (std::abs (lowL), std::abs (lowR))
                            : band == 2 ? juce::jmax (std::abs (highL), std::abs (highR))
                                        : juce::jmax (std::abs (L[i]), std::abs (R[i]));
            auto follow = [det] (float& e, float a, float r) { e = det > e ? a * e + (1.0f - a) * det : r * e + (1.0f - r) * det; };
            follow (e1, a1, r1); follow (e2, a2, r2); follow (e3, a3, r3); follow (e4, a4, r4);
            const float atk = juce::jlimit (0.0f, 1.0f, (e1 - e2) / (e1 + 1.0e-5f));
            const float sus = juce::jlimit (0.0f, 1.0f, (e3 - e4) / (e3 + 1.0e-5f));
            const float A = juce::jlimit (-1.0f, 1.0f, bip (p[0]) + rA), S = juce::jlimit (-1.0f, 1.0f, bip (p[1]) + rS);
            const float gdb = A * 18.0f * atk + S * 18.0f * sus;
            const float g = std::pow (10.0f, juce::jlimit (-36.0f, 18.0f, gdb) / 20.0f);
            gSm += (g - gSm) * 0.5f;
            float ol, orr;
            if (band == 1)      { ol = lowL * gSm + highL;  orr = lowR * gSm + highR; }
            else if (band == 2) { ol = lowL + highL * gSm;  orr = lowR + highR * gSm; }
            else                { ol = L[i] * gSm;          orr = R[i] * gSm; }
            if (p[5] > 0.02f) { ol = std::tanh (ol * drive) * clipNorm * 0.9f; orr = std::tanh (orr * drive) * clipNorm * 0.9f; }
            L[i] = ol; R[i] = orr;
        }
    }
private:
    double fs = 48000;
    float e1 = 0, e2 = 0, e3 = 0, e4 = 0, lpL = 0, lpR = 0, rA = 0, rS = 0, gSm = 1;
    int hits = 0;
};

// ===========================================================================
// 18. Smash : compresseur d'ecrasement
class SmashE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override { env = 0; hpL = hpR = 0; gr = 0; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const float thr = -p[0] * 40.0f;
        const float atk = std::exp (-1.0f / (expMap (p[1], 0.1f, 30.0f) * 0.001f * (float) fs));
        const float rel = std::exp (-1.0f / (expMap (p[2], 20.0f, 800.0f) * juce::jlimit (0.5f, 2.0f, c.decayMs / 300.0f) * 0.001f * (float) fs));
        const float ratio = expMap (p[3], 1.5f, 30.0f);
        const float slope = 1.0f - 1.0f / ratio;
        const float hpA = onePoleCoef (expMap (p[5], 20.0f, 400.0f), fs);
        const float makeup = -thr * slope * 0.55f;
        const float drive = 1.0f + p[4] * 6.0f;
        const float dn = 1.0f / std::tanh (drive);
        for (int i = 0; i < n; ++i)
        {
            hpL += hpA * (L[i] - hpL); hpR += hpA * (R[i] - hpR);
            const float sc = juce::jmax (std::abs (L[i] - hpL), std::abs (R[i] - hpR));
            env = sc > env ? atk * env + (1.0f - atk) * sc : rel * env + (1.0f - rel) * sc;
            const float lvl = juce::Decibels::gainToDecibels (env + 1.0e-6f);
            const float over = lvl - thr;
            float red = 0.0f;
            if (over > 3.0f) red = over * slope;
            else if (over > -3.0f) red = slope * (over + 3.0f) * (over + 3.0f) / 12.0f;
            gr = red;
            const float g = std::pow (10.0f, (makeup - red) / 20.0f);
            float a = L[i] * g, b = R[i] * g;
            if (p[4] > 0.02f) { a = std::tanh (a * drive) * dn; b = std::tanh (b * drive) * dn; }
            L[i] = a; R[i] = b;
        }
    }
private:
    double fs = 48000;
    float env = 0, hpL = 0, hpR = 0, gr = 0;
};

// ===========================================================================
// 19. Distorsion (antialiasing par antiderivee, sans latence)
struct Up2
{
    float x[32] {};
    int p = 0;
    void reset() { std::fill (std::begin (x), std::end (x), 0.0f); p = 0; }
    inline void process (float in, float& y0, float& y1) noexcept
    {
        static constexpr float c[12] { 0.3160629362f, -0.0995345836f, 0.0532395992f, -0.0319062120f, 0.0195116826f, -0.0116853841f,
                                       0.0066708476f, -0.0035394678f, 0.0016906510f, -0.0006900036f, 0.0002146043f, -0.0000323681f };
        x[p] = in;
        auto at = [this] (int back) { return x[(p - back) & 31]; };
        float s = 0.0f;
        for (int k = 0; k < 12; ++k) s += c[k] * (at (11 - k) + at (12 + k));
        y0 = 2.0f * s;
        y1 = at (11);
        p = (p + 1) & 31;
    }
};

class DistE : public Engine
{
public:
    // Distorsion sans latence : antialiasing par antiderivee (ADAA du premier ordre) au lieu du surechantillonnage
    void prepare (double f) override { fs = f; reset(); }
    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch) { lp[ch] = tl[ch] = dcx[ch] = dcy[ch] = 0; xp[ch] = 0.0; }
        rnd = 0; hits = 0;
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int mode = pick (p[0], 5);
        const float bias = p[3] * 0.8f;
        const float tilt = bip (p[2]) * 0.9f;
        const float f = bip (p[4]);
        const float wl = juce::jmin (1.0f, 1.0f - f), wh = juce::jmin (1.0f, 1.0f + f);
        const float split = onePoleCoef (juce::jlimit (60.0f, 3000.0f, c.body * 1.5f), fs);
        const float tiltA = onePoleCoef (1000.0f, fs);
        float* io[2] { L, R };
        for (int i = 0; i < n; ++i)
        {
            if (c.hitAt (i) > 0.0f) { ++hits; rnd = hBi ((uint32_t) hits, 0xd15u, salt (c, 21)) * p[5] * 18.0f; }
            const float gain = std::pow (10.0f, (p[1] * 48.0f + rnd) / 20.0f);
            const float comp = gain > 1.0f ? 1.2f / std::sqrt (gain) : 1.0f;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = io[ch][i];
                lp[ch] += split * (x - lp[ch]);
                const float low = lp[ch], high = x - lp[ch];
                const float din = low * wl + high * wh;
                const float clean = low * (1.0f - wl) + high * (1.0f - wh);
                const double u = (double) din * gain;
                const double d = u - xp[ch];
                double y = std::abs (d) > 1.0e-5 ? (F (u, mode, bias) - F (xp[ch], mode, bias)) / d
                                                 : (double) shape ((float) (0.5 * (u + xp[ch])), mode, bias);
                xp[ch] = u;
                float yf = (float) y * comp;
                const float dy = yf - dcx[ch] + 0.995f * dcy[ch]; dcx[ch] = yf; dcy[ch] = dy; yf = dy;
                tl[ch] += tiltA * (yf - tl[ch]);
                yf = tl[ch] * (1.0f - tilt) + (yf - tl[ch]) * (1.0f + tilt);
                io[ch][i] = clean + yf;
            }
        }
    }
    static inline double logcosh (double z) noexcept { const double a = std::abs (z); return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531; }
    static inline float shape (float x, int mode, float bias) noexcept
    {
        switch (mode)
        {
            case 0: return std::tanh (x + bias * 0.3f) - std::tanh (bias * 0.3f);
            case 1: { const float u = x + bias; return (u > 0 ? std::tanh (u) : std::tanh (u * 0.45f) / 0.45f * 0.6f) - (bias > 0 ? std::tanh (bias) : 0.0f); }
            case 2: { const float u = x * 1.5f + bias; return juce::jlimit (-1.0f, 1.0f, u) * 0.85f + 0.15f * std::tanh (u * 4.0f); }
            case 3: return std::sin (juce::MathConstants<float>::halfPi * (x * 0.8f + bias));
            default: { const float u = x + bias; return std::tanh (std::abs (u) * 1.5f) * 1.4f - 0.7f; }
        }
    }
    // antiderivee de shape par rapport a x
    static inline double F (double x, int mode, float biasF) noexcept
    {
        const double b = biasF;
        switch (mode)
        {
            case 0: return logcosh (x + b * 0.3) - std::tanh (b * 0.3) * x;
            case 1:
            {
                const double u = x + b;
                const double g = u > 0 ? logcosh (u) : (0.6 / 0.45) * logcosh (0.45 * u) / 0.45;
                return g - (b > 0 ? std::tanh (b) : 0.0) * x;
            }
            case 2:
            {
                const double u = 1.5 * x + b;
                const double cl = std::abs (u) <= 1.0 ? 0.5 * u * u : std::abs (u) - 0.5;
                return (0.85 * cl + 0.15 * logcosh (4.0 * u) / 4.0) / 1.5;
            }
            case 3:
            {
                const double k = juce::MathConstants<double>::halfPi;
                return -std::cos (k * (0.8 * x + b)) / (0.8 * k);
            }
            default:
            {
                const double u = x + b;
                return (u >= 0 ? 1.0 : -1.0) * 1.4 * logcosh (1.5 * u) / 1.5 - 0.7 * x;
            }
        }
    }
private:
    double fs = 48000;
    float lp[2] {}, tl[2] {}, dcx[2] {}, dcy[2] {}, rnd = 0;
    double xp[2] {};
    int hits = 0;
};

// ===========================================================================
// 20. Sub 808 declenche
class SubE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override { for (auto& v : vc) v = Voice(); cur = 0; hits = 0; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const float dec = std::exp (-1.0f / (expMap (p[1], 30.0f, 2000.0f) * 0.001f * (float) fs));
        const float pdec = std::exp (-1.0f / (expMap (p[3], 5.0f, 300.0f) * 0.001f * (float) fs));
        const float drive = 1.0f + p[4] * 6.0f, dn = 1.0f / std::tanh (drive);
        const float fast = std::exp (-1.0f / (0.008f * (float) fs));
        for (int i = 0; i < n; ++i)
        {
            const float hv = c.hitAt (i);
            if (hv > 0.0f)
            {
                ++hits;
                vc[cur].fastRel = true;
                cur ^= 1;
                auto& v = vc[cur];
                const float r = hBi ((uint32_t) hits, 0x5b1u, salt (c, 22)) * p[5];
                v.f0 = juce::jlimit (25.0f, 400.0f, c.fund * std::pow (2.0f, (subTuneSemis (p[0]) + std::round (r * 7.0f)) / 12.0f));
                v.amp = 0.35f + 0.65f * hv;
                v.decMul = 1.0f + r * 0.5f;
                v.pe = 1.0f; v.ph = 0.0f; v.on = true; v.fastRel = false; v.atk = 0.0f;
            }
            float s = 0.0f;
            for (auto& v : vc)
            {
                if (! v.on) continue;
                v.pe *= pdec;
                const float f = v.f0 * (1.0f + p[2] * 4.0f * v.pe);
                v.ph += f / (float) fs; if (v.ph >= 1.0f) v.ph -= 1.0f;
                v.atk = juce::jmin (1.0f, v.atk + 1.0f / (0.0007f * (float) fs));
                s += std::sin (kTwoPi * v.ph) * v.amp * v.atk;
                v.amp *= v.fastRel ? fast : std::pow (dec, 1.0f / juce::jmax (0.3f, v.decMul));
                if (v.amp < 1.0e-4f) v.on = false;
            }
            if (p[4] > 0.02f) s = std::tanh (s * drive) * dn;
            L[i] += s * 0.8f; R[i] += s * 0.8f;
        }
    }
private:
    struct Voice { bool on = false, fastRel = false; float f0 = 50, amp = 0, pe = 0, ph = 0, decMul = 1, atk = 0; };
    double fs = 48000;
    Voice vc[2];
    int cur = 0, hits = 0;
};

// ===========================================================================
// 21. Couche de bruit declenchee
class NoiseE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override
    {
        env = 0; follow = 0; hits = 0; rnd = 0; rdec = 1; rngL = FastRng (0x1234u); rngR = FastRng (0x9876u);
        for (auto& s : sv) s.clear();
        for (auto& ph : sq) ph = 0;
        hpL = hpR = pinkL = pinkR = 0;
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int mode = pick (p[0], 4);
        const float colour = juce::jlimit (200.0f, 16000.0f, expMap (p[1], 300.0f, 14000.0f) * std::pow (2.0f, rnd));
        const float dec = std::exp (-1.0f / (expMap (p[2], 10.0f, 1500.0f) * rdec * 0.001f * (float) fs));
        const float k = 2.0f - 1.9f * (1.0f - p[3]);
        sv[0].set (colour, (float) fs, k); sv[1].set (colour * 1.03f, (float) fs, k);
        const float fRel = std::exp (-1.0f / (0.02f * (float) fs));
        const float hpA = onePoleCoef (juce::jmin (6000.0f, colour * 0.5f), fs);
        static const float metal[6] { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };
        for (int i = 0; i < n; ++i)
        {
            const float hv = c.hitAt (i);
            if (hv > 0.0f)
            {
                ++hits;
                env = juce::jmax (env, 0.3f + 0.7f * hv);
                rnd = hBi ((uint32_t) hits, 0x401u, salt (c, 23)) * p[5];
                rdec = std::pow (2.0f, hBi ((uint32_t) hits, 0x402u, salt (c, 23)) * p[5]);
            }
            const float x = juce::jmax (std::abs (L[i]), std::abs (R[i]));
            follow = x > follow ? x : follow * fRel;
            const float amp = env * (1.0f - p[4]) + juce::jlimit (0.0f, 1.0f, follow * 3.0f) * p[4];
            env *= dec;
            float a = 0, b = 0;
            switch (mode)
            {
                case 0: { float lo, bd, hi; sv[0].process (rngL.bi(), lo, bd, hi); a = bd * k * 2.0f; sv[1].process (rngR.bi(), lo, bd, hi); b = bd * k * 2.0f; break; }
                case 1:
                {
                    float m = 0;
                    for (int j = 0; j < 6; ++j)
                    {
                        sq[j] += metal[j] * (colour / 3000.0f) / (float) fs; sq[j] -= std::floor (sq[j]);
                        m += sq[j] < 0.5f ? 1.0f : -1.0f;
                    }
                    m *= 0.17f;
                    hpL += hpA * (m - hpL);
                    float lo, bd, hi; sv[0].process (m - hpL, lo, bd, hi);
                    a = b = (hi * 0.6f + bd * 0.6f);
                    break;
                }
                case 2:
                {
                    const float dens = 0.0005f + p[3] * 0.02f;
                    float lo, bd, hi;
                    sv[0].process (rngL.uni() < dens ? rngL.bi() * 4.0f : 0.0f, lo, bd, hi); a = bd + hi * 0.5f;
                    sv[1].process (rngR.uni() < dens ? rngR.bi() * 4.0f : 0.0f, lo, bd, hi); b = bd + hi * 0.5f;
                    break;
                }
                default:
                {
                    pinkL += onePoleCoef (colour, fs) * (rngL.bi() - pinkL);
                    pinkR += onePoleCoef (colour, fs) * (rngR.bi() - pinkR);
                    a = pinkL * 2.0f; b = pinkR * 2.0f;
                    break;
                }
            }
            L[i] += a * amp * 0.5f; R[i] += b * amp * 0.5f;
        }
    }
private:
    double fs = 48000;
    float env = 0, follow = 0, rnd = 0, rdec = 1, hpL = 0, hpR = 0, pinkL = 0, pinkR = 0;
    int hits = 0;
    FastRng rngL, rngR;
    Svf sv[2];
    float sq[6] {};
};

// ===========================================================================
// 22. Corps modal
class BodyE : public Engine
{
public:
    void prepare (double f) override { fs = f; reset(); }
    void reset() override { for (auto& m : modes) m.clear(); hits = 0; tr = 0; lastKey = -1.0f; inh.fill (0.0f); }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        static const float ratios[5][8] = {
            { 1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.50f },
            { 1.0f, 2.57f, 4.20f, 5.40f, 6.80f, 8.93f, 10.9f, 13.3f },
            { 1.0f, 1.42f, 2.16f, 2.66f, 3.06f, 3.58f, 4.27f, 4.88f },
            { 0.5f, 1.0f, 1.19f, 1.5f, 2.0f, 2.5f, 2.66f, 3.01f },
            { 1.0f, 2.0f, 3.0f, 4.03f, 5.1f, 6.2f, 7.0f, 8.1f } };
        const int mat = pick (p[0], 5);
        bool retune = false;
        for (int i = 0; i < n; ++i)
            if (c.hitAt (i) > 0.0f)
            {
                ++hits;
                tr = std::round (hBi ((uint32_t) hits, 0xb0du, salt (c, 24)) * p[5] * 7.0f);
                for (int k = 0; k < 8; ++k) inh[(size_t) k] = hBi ((uint32_t) hits, (uint32_t) k + 0x30u, salt (c, 24)) * (p[5] > 0.5f ? 1.0f : 0.0f);
                retune = true;
                break;
            }
        const float base = juce::jlimit (20.0f, 8000.0f, c.fund * std::pow (2.0f, (bodyTuneSemis (p[1]) + tr) / 12.0f));
        const float tau = expMap (p[2], 40.0f, 4000.0f) * 0.001f;
        const float key = base * 7.0f + (float) mat * 1000.0f + tau * 13.0f + p[3] * 3.0f + p[4] * 17.0f;
        if (retune || std::abs (key - lastKey) > 1.0e-3f)
        {
            lastKey = key;
            for (int k = 0; k < 8; ++k)
            {
                const float det = 1.0f + (hBi ((uint32_t) k, 0x1a1u, salt (c, 25)) + inh[(size_t) k]) * p[4] * 0.06f;
                const float f = base * ratios[mat][k] * det;
                active[k] = f < (float) fs * 0.45f;
                if (! active[k]) continue;
                const float tk = tau / (1.0f + (float) k * 0.25f * (1.0f - p[3]));
                const float q = juce::jlimit (0.7f, 3000.0f, juce::MathConstants<float>::pi * f * tk);
                modes[k].set (f, (float) fs, 1.0f / q);
                modes[k + 8].set (f * 1.002f, (float) fs, 1.0f / q);
                amp[k] = std::pow ((float) k + 1.0f, -(1.0f - p[3]) * 1.2f) * std::sqrt (q) * 0.12f;
                pan[k] = 0.5f + 0.4f * hBi ((uint32_t) k, 0x9a9u, salt (c, 26));
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const float xl = L[i], xr = R[i];
            float yl = 0, yr = 0, lo, bd, hi;
            for (int k = 0; k < 8; ++k)
            {
                if (! active[k]) continue;
                modes[k].process (xl, lo, bd, hi); yl += bd * amp[k] * (1.0f - pan[k]) * 2.0f;
                modes[k + 8].process (xr, lo, bd, hi); yr += bd * amp[k] * pan[k] * 2.0f;
            }
            L[i] = softClip (yl); R[i] = softClip (yr);
        }
    }
private:
    double fs = 48000;
    Svf modes[16];
    float amp[8] {}, pan[8] {};
    bool active[8] {};
    std::array<float, 8> inh {};
    float tr = 0, lastKey = -1;
    int hits = 0;
};

// ===========================================================================
// 23. Reverb multi-mode (room, plate, hall, gated, non-lin)
class ReverbE : public Engine
{
public:
    void prepare (double f) override { fs = f; v.prepare (f); reset(); }
    void reset() override { v.clear(); gate = 0; hold = 0; ramp = 0; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int mode = pick (p[0], 5);
        float size = p[2], t60 = expMap (p[1], 0.15f, 12.0f), pre = p[4] * 0.2f, mod = p[5];
        switch (mode)
        {
            case 0: size = p[2] * 0.35f; mod *= 0.3f; break;
            case 2: size = 0.55f + 0.45f * p[2]; pre += 0.015f; break;
            case 3: case 4: t60 = juce::jmax (t60, 2.5f); mod = 0.2f; break;
            default: break;
        }
        v.setParams (size, t60, p[3], pre, mod, 1.2f);
        const int holdS = (int) (expMap (p[5], 60.0f, 700.0f) * 0.001f * (float) fs);
        const float relC = std::exp (-1.0f / (0.008f * (float) fs));
        for (int i = 0; i < n; ++i)
        {
            float a, b;
            v.process (L[i], R[i], a, b, c.freeze);
            if (mode >= 3)
            {
                if (c.hitAt (i) > 0.0f) { hold = holdS; ramp = 0; }
                float g;
                if (hold > 0) { --hold; ++ramp; g = mode == 3 ? 1.0f : 0.25f + 0.75f * (float) ramp / (float) juce::jmax (1, holdS); gate += (g - gate) * 0.05f; }
                else gate *= relC;
                if (c.freeze) gate = 1.0f;
                a *= gate * 1.4f; b *= gate * 1.4f;
            }
            L[i] = a; R[i] = b;
        }
    }
private:
    double fs = 48000;
    Verb v;
    float gate = 0;
    int hold = 0, ramp = 0;
};

// ===========================================================================
// 24. Modulation (chorus / flanger / phaser)
class ModE : public Engine
{
public:
    void prepare (double f) override { for (auto& e : e3) e->prepare (f); reset(); }
    void reset() override { for (auto& e : e3) e->reset(); cur = -1; }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int m = pick (p[0], 3);
        if (m != cur) { cur = m; e3[(size_t) m]->reset(); }
        float q[6];
        switch (m)
        {
            case 0: q[0] = p[1]; q[1] = p[2]; q[2] = 0.7f; q[3] = p[4]; q[4] = p[3]; q[5] = p[5]; break;
            case 1: q[0] = p[1]; q[1] = p[2]; q[2] = p[3]; q[3] = p[4]; q[4] = p[5]; q[5] = 0.3f; break;
            default: q[0] = p[1]; q[1] = p[2]; q[2] = p[3]; q[3] = 0.7f; q[4] = p[4]; q[5] = p[5]; break;
        }
        e3[(size_t) m]->process (L, R, n, c, q);
    }
    ModE() { e3[0] = std::make_unique<ChorusE>(); e3[1] = std::make_unique<FlangerE>(); e3[2] = std::make_unique<PhaserE>(); }
private:
    std::array<std::unique_ptr<Engine>, 3> e3;
    int cur = -1;
};


// ===========================================================================
// Cross-synthese : vocodeur a bandes. Sens 0 : l'enveloppe spectrale de Maria (la forme) est imposee au son d'un autre ;
// sens 1 : le son de Maria prend l'enveloppe (les attaques, les formants) de l'autre.
class CrossE : public Engine
{
public:
    static constexpr int kMaxBands = 32;
    void prepare (double sr) override { fs = (float) sr; reset(); }
    void reset() override
    {
        for (auto& b : bands) b = Band();
        gainSm = 1.0f; modPow = carPow = outPow = 1.0e-6f; noise = alea::FastRng (0x51ab); lastN = -1; ctr = 0;
    }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        const int nb = (int) std::round (8 + p[4] * 24);
        if (nb != lastN) design (nb);
        const int src = pick (p[0], 3);
        const bool flip = p[1] >= 0.5f;
        const float tEnv = expMap (p[2], 2.0f, 120.0f) * 0.001f;
        const float aA = std::exp (-1.0f / (0.25f * tEnv * fs)), aR = std::exp (-1.0f / (tEnv * fs));
        const float shiftB = std::round (bip (p[3]) * 12.0f) / 12.0f * (float) nb / octaves;   // decalage en bandes
        const float white = p[5];
        const float* ext = src == 0 ? (c.side != nullptr ? c.side : c.track) : (src == 1 ? c.track : nullptr);
        const float pc = std::exp (-1.0f / (0.3f * fs));
        for (int i = 0; i < n; ++i)
        {
            const float maria = 0.5f * (L[i] + R[i]);
            float other = ext != nullptr ? ext[i] : 0.0f;
            if (ext == nullptr) other = noise.bi() * 0.3f;
            const float car = flip ? maria : other, mod = flip ? other : maria;
            // suivi des enveloppes du modulateur par bande
            float yL = 0.0f, yR = 0.0f;
            for (int b = 0; b < nb; ++b)
            {
                auto& B = bands[(size_t) b];
                const float mb = B.m2.process (B.m1.process (mod));
                const float am = std::abs (mb);
                B.envM = am > B.envM ? am + (B.envM - am) * aA : am + (B.envM - am) * aR;
            }
            for (int b = 0; b < nb; ++b)
            {
                auto& B = bands[(size_t) b];
                const float cb = B.c2.process (B.c1.process (car));
                const float ac = std::abs (cb);
                B.envC = ac > B.envC ? ac + (B.envC - ac) * aA : ac + (B.envC - ac) * aR;
                if ((ctr & 7) == 0)
                {
                    // enveloppe du modulateur, decalee (formants) ; blanchiment du porteur
                    const float fb = juce::jlimit (0.0f, (float) (nb - 1), (float) b - shiftB);
                    const int b0 = (int) fb; const float fr = fb - (float) b0;
                    const float em = bands[(size_t) b0].envM * (1.0f - fr) + bands[(size_t) juce::jmin (nb - 1, b0 + 1)].envM * fr;
                    B.g = em / std::pow (B.envC + 1.0e-4f, white);
                }
                const float y = cb * B.g;
                if (b & 1) { yL += y * 0.7f; yR += y * 1.3f; } else { yL += y * 1.3f; yR += y * 0.7f; }
            }
            // gain automatique : la sortie suit le niveau du modulateur
            modPow = mod * mod + (modPow - mod * mod) * pc;
            outPow = 0.25f * (yL + yR) * (yL + yR) + (outPow - 0.25f * (yL + yR) * (yL + yR)) * pc;
            const float target = juce::jlimit (0.0f, 40.0f, std::sqrt (modPow / (outPow + 1.0e-9f)));
            gainSm += (target - gainSm) * 0.0005f;
            L[i] = softClip (yL * gainSm);
            R[i] = softClip (yR * gainSm);
            ++ctr;
        }
    }
private:
    struct Bp
    {
        float b0 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (float f, float q, float sr)
        {
            const float w = juce::MathConstants<float>::twoPi * f / sr, al = std::sin (w) / (2.0f * q), a0 = 1.0f + al;
            b0 = al / a0; b2 = -al / a0; a1 = -2.0f * std::cos (w) / a0; a2 = (1.0f - al) / a0;
        }
        inline float process (float x) noexcept { const float y = b0 * x + z1; z1 = -a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    struct Band { Bp m1, m2, c1, c2; float envM = 0, envC = 0, g = 0; };
    void design (int nb)
    {
        lastN = nb;
        const float lo = 90.0f, hi = juce::jmin (11000.0f, fs * 0.42f);
        octaves = std::log2 (hi / lo);
        const float q = juce::jmax (1.5f, (float) nb / octaves * 1.1f);
        for (int b = 0; b < nb; ++b)
        {
            const float f = lo * std::pow (hi / lo, ((float) b + 0.5f) / (float) nb);
            auto& B = bands[(size_t) b];
            B.m1.set (f, q, fs); B.m2.set (f, q, fs); B.c1.set (f, q, fs); B.c2.set (f, q, fs);
        }
    }
    float fs = 48000.0f, octaves = 7.0f;
    std::array<Band, kMaxBands> bands {};
    int lastN = -1; uint32_t ctr = 0;
    float gainSm = 1.0f, modPow = 1e-6f, carPow = 1e-6f, outPow = 1e-6f;
    alea::FastRng noise { 7 };
};

// ===========================================================================
class NullE : public Engine
{
public:
    void prepare (double) override {}
    void reset() override {}
    void process (float*, float*, int, const Ctx&, const float*) override {}
};

std::unique_ptr<Engine> makeEngine (int type)
{
    switch (type)
    {
        case TDelay:     return std::make_unique<DelayE>();
        case TScatter:   return std::make_unique<ScatterE>();
        case TStutter:   return std::make_unique<StutterE>();
        case TGranular:  return std::make_unique<GranularE>();
        case TReverb:    return std::make_unique<ReverbE>();
        case TShimmer:   return std::make_unique<ShimmerE>();
        case TReverse:   return std::make_unique<ReverseE>();
        case TMod:       return std::make_unique<ModE>();
        case TFilter:    return std::make_unique<FilterE>();
        case TPitch:     return std::make_unique<PitchE>();
        case TDegrade:   return std::make_unique<DegradeE>();
        case TDist:      return std::make_unique<DistE>();
        case TRing:      return std::make_unique<RingE>();
        case TGate:      return std::make_unique<GateE>();
        case TResonator: return std::make_unique<ResonatorE>();
        case TBody:      return std::make_unique<BodyE>();
        case TCross:     return std::make_unique<CrossE>();
        default:         return std::make_unique<NullE>();
    }
}
} // namespace ma
