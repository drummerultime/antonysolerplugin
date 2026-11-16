#include "RwEngines.h"

namespace rw
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
        wc = 0; countdown = 0; spawned = 0; norm = 1.0f;
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
            if (--countdown <= 0)
            {
                spawn (c, p, lenS);
                countdown = (int) ((float) fs / dens * (0.3f + 1.4f * hUni ((uint32_t) spawned, 0x9a1u, salt (c, 5))));
            }
            norm += (targetNorm - norm) * 0.001f;
            float ol = 0, orr = 0;
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float ph = (float) g.t / (float) g.len;
                const float wv = win[juce::jlimit (0, 1023, (int) (ph * 1023.0f))];
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
    struct Grain { bool on = false, rev = false; double start = 0, inc = 1; int len = 1, t = 0; float gl = 0.7f, gr = 0.7f; };
    void spawn (const Ctx& c, const float* p, float lenS)
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
        double start = (double) wc - span - 8.0 - spray;
        const double oldest = (double) wc - (double) size + 64.0;
        if (start < oldest) start = oldest;
        if (start < 0.0 && wc < (int64_t) size) start = juce::jmax (0.0, start);
        g->on = true; g->t = 0; g->len = len; g->inc = rate; g->start = start;
        g->rev = u (4) < p[5];
        const float pan = hBi ((uint32_t) spawned, 5u, s) * 0.8f;
        g->gl = std::cos ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
        g->gr = std::sin ((pan + 1.0f) * kPi * 0.25f) * 1.414f;
    }
    double fs = 48000;
    int size = 0;
    std::vector<float> bl, br;
    int64_t wc = 0;
    int countdown = 0, spawned = 0;
    float norm = 1.0f;
    float win[1024] {};
    Grain grains[48];
};

// ===========================================================================
// 5. Plate
class PlateE : public Engine
{
public:
    void prepare (double f) override { v.prepare (f); }
    void reset() override { v.clear(); }
    void process (float* L, float* R, int n, const Ctx& c, const float* p) override
    {
        v.setParams (p[1], expMap (p[0], 0.3f, 20.0f), p[2], p[3] * 0.25f, p[4], p[5] * 2.0f);
        for (int i = 0; i < n; ++i) v.process (L[i], R[i], L[i], R[i], c.freeze);
    }
private:
    Verb v;
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
        fl.clear(); fr.clear(); cl.clear(); cr.clear(); env = 0; ph = 0; lpcL = lpcR = 0;
        for (auto& f : form) f.clear();
    }
    void process (float* L, float* R, int n, const Ctx&, const float* p) override
    {
        const int type = pick (p[0], 6);
        float pk = 0;
        for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
        const float a = pk > env ? 0.5f : std::exp (-(float) n / (0.15f * (float) fs));
        env = pk > env ? env + a * (pk - env) : env * a + pk * (1.0f - a);
        const float envN = juce::jlimit (0.0f, 1.0f, env * 3.0f);
        ph += expMap (p[5], 0.02f, 10.0f) * (float) n / (float) fs; ph -= std::floor (ph);
        const float lfo = std::sin (kTwoPi * ph);
        const float oct = envN * bip (p[3]) * 5.0f + lfo * p[4] * 3.0f;

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
        const float k = 2.0f - 1.95f * p[2];
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
        }
    }
private:
    double fs = 48000;
    Svf fl, fr, form[6];
    Line cl, cr;
    float env = 0, ph = 0, lpcL = 0, lpcR = 0;
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
        const int root = pick (p[0], 12), scale = pick (p[1], 12), range = 1 + pick (p[4], 4);
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
        const int note = 36 + root + d + 12 * oct + (k % 3 == 0 ? 0 : 12);
        const float hzv = 440.0f * std::pow (2.0f, (float) (note - 69) / 12.0f);
        comb[k].target = juce::jlimit (4.0f, (float) comb[k].l.size() - 8.0f, (float) fs / hzv);
        if (comb[k].d < 0) comb[k].d = comb[k].target;
    }
    struct Comb { Line l; float lp = 0, d = -1, target = 100; };
    double fs = 48000;
    Comb comb[6];
    int64_t lastTick = INT64_MIN;
    int lastKey = -1;
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
        case TPlate:     return std::make_unique<PlateE>();
        case TShimmer:   return std::make_unique<ShimmerE>();
        case TReverse:   return std::make_unique<ReverseE>();
        case TChorus:    return std::make_unique<ChorusE>();
        case TFlanger:   return std::make_unique<FlangerE>();
        case TPhaser:    return std::make_unique<PhaserE>();
        case TFilter:    return std::make_unique<FilterE>();
        case TPitch:     return std::make_unique<PitchE>();
        case TDegrade:   return std::make_unique<DegradeE>();
        case TRing:      return std::make_unique<RingE>();
        case TGate:      return std::make_unique<GateE>();
        case TResonator: return std::make_unique<ResonatorE>();
        default:         return std::make_unique<NullE>();
    }
}
} // namespace rw
