#include "Wavetables.h"

#include <complex>
#include <functional>
#include <algorithm>
#include <cstdint>

namespace fragor
{
namespace
{
    using cd = std::complex<double>;
    constexpr double kPi = 3.14159265358979323846;

    // FFT radix-2 itérative, en place. sign = -1 : directe, +1 : inverse (non normalisée)
    void fft (std::vector<cd>& a, int sign)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * kPi / (double) len * (double) sign;
            const cd wl (std::cos (ang), std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                cd w (1.0);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const cd u = a[i + k];
                    const cd v = a[i + k + len / 2] * w;
                    a[i + k]           = u + v;
                    a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
    }

    constexpr int kGenSize = 8192;    // sur-échantillonnage pour générer les formes temporelles
    constexpr int kMaxHarm = 1023;

    using Spectrum = std::vector<cd>; // index = harmonique (0..kMaxHarm)

    // ---- Formants (Hz) pour les tables vocales ----
    struct Vowel { double f[3]; };
    const Vowel vA { { 800.0, 1150.0, 2900.0 } };
    const Vowel vE { { 400.0, 1600.0, 2700.0 } };
    const Vowel vI { { 270.0, 2140.0, 2950.0 } };
    const Vowel vO { { 450.0,  800.0, 2830.0 } };
    const Vowel vU { { 325.0,  700.0, 2530.0 } };

    Vowel vowelPath (const std::vector<Vowel>& path, double t)
    {
        const double x = t * (double) (path.size() - 1);
        size_t i = (size_t) std::floor (x);
        if (i >= path.size() - 1) i = path.size() - 2;
        const double fr = x - (double) i;
        Vowel v;
        for (int k = 0; k < 3; ++k)
            v.f[k] = std::exp (std::log (path[i].f[k]) * (1.0 - fr) + std::log (path[i + 1].f[k]) * fr);
        return v;
    }

    double formantEnv (double hz, const Vowel& v, double bwOct, const double gains[3])
    {
        double e = 0.025;
        for (int k = 0; k < 3; ++k)
        {
            const double d = std::log2 (hz / v.f[k]) / bwOct;
            e += gains[k] * std::exp (-0.5 * d * d);
        }
        return e;
    }

    Spectrum fromTime (const std::function<double (double)>& fn)
    {
        std::vector<cd> buf ((size_t) kGenSize);
        for (int n = 0; n < kGenSize; ++n)
            buf[(size_t) n] = cd (fn ((double) n / (double) kGenSize), 0.0);
        fft (buf, -1);
        Spectrum s ((size_t) kMaxHarm + 1, cd (0.0));
        for (int h = 1; h <= kMaxHarm; ++h)
            s[(size_t) h] = buf[(size_t) h] * (2.0 / (double) kGenSize);
        return s;
    }

    // Série de sinus : y = sum a_h sin(h theta)
    Spectrum fromSineAmps (const std::function<double (int)>& amp)
    {
        Spectrum s ((size_t) kMaxHarm + 1, cd (0.0));
        for (int h = 1; h <= kMaxHarm; ++h)
            s[(size_t) h] = cd (0.0, -amp (h));
        return s;
    }

    // Série de cosinus : y = sum a_h cos(h theta)
    Spectrum fromCosAmps (const std::function<double (int)>& amp)
    {
        Spectrum s ((size_t) kMaxHarm + 1, cd (0.0));
        for (int h = 1; h <= kMaxHarm; ++h)
            s[(size_t) h] = cd (amp (h), 0.0);
        return s;
    }

    double frac (double x) { return x - std::floor (x); }
    double saw  (double p) { return 2.0 * frac (p) - 1.0; }

    Wavetable build (const std::function<Spectrum (double)>& frameSpectrum)
    {
        Wavetable wt;
        wt.data.assign ((size_t) kFrames * kMips * kStride, 0.0f);
        std::vector<cd> buf ((size_t) kTableSize);

        for (int fr = 0; fr < kFrames; ++fr)
        {
            const double t = (double) fr / (double) (kFrames - 1);
            const Spectrum sp = frameSpectrum (t);
            float norm = 1.0f;

            for (int m = 0; m < kMips; ++m)
            {
                const int maxH = std::min (kMaxHarm, std::max (1, 1024 >> m));
                std::fill (buf.begin(), buf.end(), cd (0.0));
                for (int h = 1; h <= maxH; ++h)
                    buf[(size_t) h] = sp[(size_t) h];
                fft (buf, +1);

                float* dst = wt.data.data() + ((size_t) fr * kMips + (size_t) m) * kStride;
                for (int n = 0; n < kTableSize; ++n)
                    dst[n] = (float) buf[(size_t) n].real();

                if (m == 0)
                {
                    float peak = 0.0f;
                    for (int n = 0; n < kTableSize; ++n) peak = std::max (peak, std::abs (dst[n]));
                    norm = peak > 1.0e-6f ? 0.97f / peak : 1.0f;
                }
                for (int n = 0; n < kTableSize; ++n) dst[n] *= norm;
                dst[kTableSize] = dst[0];
            }
        }
        return wt;
    }
} // namespace

std::shared_ptr<Wavetable> WavetableBank::buildFromCycles (const std::vector<std::vector<float>>& cycles)
{
    if (cycles.empty()) return {};
    std::vector<Spectrum> spectra;
    for (int f = 0; f < kFrames; ++f)
    {
        const auto& c = cycles[(size_t) std::min ((int) cycles.size() - 1, f * (int) cycles.size() / kFrames)];
        std::vector<cd> buf ((size_t) kTableSize);
        for (int n = 0; n < kTableSize; ++n) buf[(size_t) n] = cd (n < (int) c.size() ? c[(size_t) n] : 0.0f, 0.0);
        fft (buf, -1);
        Spectrum sp ((size_t) kMaxHarm + 1, cd (0.0));
        for (int h = 1; h <= kMaxHarm; ++h) sp[(size_t) h] = buf[(size_t) h] * (2.0 / (double) kTableSize);
        spectra.push_back (std::move (sp));
    }
    auto wt = std::make_shared<Wavetable> (build ([&spectra] (double t)
    {
        return spectra[(size_t) std::lround (t * (kFrames - 1))];
    }));
    return wt;
}

bool WavetableBank::sliceAudio (const float* x, int len, std::vector<std::vector<float>>& cycles)
{
    cycles.clear();
    if (x == nullptr || len < 64) return false;

    auto resampleCycle = [] (const float* src, double period, double start, int srcLen)
    {
        std::vector<float> c ((size_t) kTableSize);
        for (int n = 0; n < kTableSize; ++n)
        {
            const double pos = start + period * (double) n / (double) kTableSize;
            const int i = (int) std::floor (pos);
            const double fr = pos - i;
            const float a = src[std::clamp (i, 0, srcLen - 1)];
            const float b = src[std::clamp (i + 1, 0, srcLen - 1)];
            c[(size_t) n] = (float) (a + (b - a) * fr);
        }
        // raccord doux : on supprime le saut entre la fin et le début du cycle
        const int xf = 64;
        const float jump = c[0] - c[(size_t) kTableSize - 1];
        for (int n = 0; n < xf; ++n)
        {
            const float w = (float) (n + 1) / (float) xf;
            c[(size_t) (kTableSize - xf + n)] += w * jump * 0.98f;
        }
        return c;
    };

    // 1) format wavetable : multiples de 2048 échantillons (exports Serum / Vital...)
    if (len % kTableSize == 0)
    {
        const int n = len / kTableSize;
        for (int f = 0; f < kFrames; ++f)
        {
            const int idx = n == 1 ? 0 : (int) std::lround ((double) f * (n - 1) / (kFrames - 1));
            cycles.emplace_back (x + (size_t) idx * kTableSize, x + (size_t) (idx + 1) * kTableSize);
        }
        return true;
    }

    // 2) son court : tout le fichier = un cycle
    if (len < 3 * kTableSize)
    {
        auto c = resampleCycle (x, (double) len, 0.0, len);
        for (int f = 0; f < kFrames; ++f) cycles.push_back (c);
        return true;
    }

    // 3) son quelconque : détection de période (autocorrélation) sur 16 fenêtres
    float peak = 0.0f;
    for (int i = 0; i < len; ++i) peak = std::max (peak, std::abs (x[i]));
    if (peak < 1.0e-5f) return false;
    int startI = 0;
    while (startI < len && std::abs (x[startI]) < peak * 0.02f) ++startI;
    const int win = 4096, minLag = 40, maxLag = 1600;
    const int usable = len - startI - win - maxLag;
    if (usable <= 0)
    {
        auto c = resampleCycle (x, (double) std::min (len, 2048), 0.0, len);
        for (int f = 0; f < kFrames; ++f) cycles.push_back (c);
        return true;
    }
    double lastPeriod = 0.0;
    for (int f = 0; f < kFrames; ++f)
    {
        const int s0 = startI + (int) ((double) usable * f / (kFrames - 1));
        double best = -1.0; int bestLag = 0;
        double e0 = 0.0;
        for (int i = 0; i < win; ++i) e0 += (double) x[s0 + i] * x[s0 + i];
        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double acc = 0.0, e1 = 0.0;
            for (int i = 0; i < win; i += 2)
            {
                acc += (double) x[s0 + i] * x[s0 + i + lag];
                e1  += (double) x[s0 + i + lag] * x[s0 + i + lag];
            }
            const double r = acc / std::sqrt (e0 * 0.5 * e1 + 1.0e-12);
            // léger biais vers les petites périodes pour éviter les sous-octaves
            const double score = r - (double) lag * 1.0e-5;
            if (score > best) { best = score; bestLag = lag; }
        }
        double period = bestLag > 0 ? (double) bestLag : (lastPeriod > 0 ? lastPeriod : 512.0);
        if (best < 0.3 && lastPeriod > 0.0) period = lastPeriod;
        lastPeriod = period;
        // départ sur un passage par zéro montant
        int z = s0;
        for (int i = s0; i < s0 + (int) period; ++i)
            if (x[i] <= 0.0f && x[i + 1] > 0.0f) { z = i; break; }
        cycles.push_back (resampleCycle (x, period, (double) z, len));
    }
    return true;
}

const WavetableBank& WavetableBank::instance()
{
    static WavetableBank bank;
    return bank;
}

WavetableBank::WavetableBank()
{
    const double fg[3] = { 1.0, 0.65, 0.3 };

    // 0 BASIC : sinus -> triangle -> dent de scie -> carré -> pulse étroite
    tables.push_back (build ([] (double t)
    {
        return fromTime ([t] (double p)
        {
            auto shape = [p] (int k) -> double
            {
                switch (k)
                {
                    case 0:  return std::sin (2.0 * kPi * p);
                    case 1:  return 1.0 - 4.0 * std::abs (frac (p + 0.25) - 0.5);
                    case 2:  return saw (p + 0.5);
                    case 3:  return p < 0.5 ? 1.0 : -1.0;
                    default: return p < 0.1 ? 1.0 : -0.111;
                }
            };
            const double x = t * 4.0;
            const int k = std::min (3, (int) x);
            const double f = x - k;
            return shape (k) * (1.0 - f) + shape (k + 1) * f;
        });
    }));

    // 1 GROWL : source carrée/scie grave filtrée par formants U -> O -> A -> E
    tables.push_back (build ([fg] (double t)
    {
        const Vowel v = vowelPath ({ vU, vO, vA, vE }, t);
        return fromSineAmps ([&] (int h)
        {
            const double src = (h % 2 ? 1.0 : 0.55) / std::pow ((double) h, 0.55);
            return src * formantEnv (h * 48.0, v, 0.16, fg);
        });
    }));

    // 2 YOI : I -> O -> I (le "yoi" se joue en balayant la position)
    tables.push_back (build ([fg] (double t)
    {
        const Vowel v = vowelPath ({ vI, vU, vO, vE, vI }, t);
        return fromSineAmps ([&] (int h)
        {
            return (1.0 / std::pow ((double) h, 0.45)) * formantEnv (h * 62.0, v, 0.14, fg);
        });
    }));

    // 3 METAL : FM à index croissant (ratios 2 & 3)
    tables.push_back (build ([] (double t)
    {
        const double I = 0.2 + 9.0 * t;
        return fromTime ([I] (double p)
        {
            const double th = 2.0 * kPi * p;
            return std::sin (th + I * std::sin (2.0 * th) + 0.45 * I * std::sin (3.0 * th));
        });
    }));

    // 4 SCREECH : hard-sync de scie (ratio 1 -> 8)
    tables.push_back (build ([] (double t)
    {
        const double r = 1.0 + 7.0 * t;
        return fromTime ([r] (double p)
        {
            return 0.75 * saw (p * r) + 0.25 * saw (p);
        });
    }));

    // 5 FOLD : sinus replié de plus en plus fort + asymétrie
    tables.push_back (build ([] (double t)
    {
        const double g = 1.0 + 11.0 * t;
        const double b = 0.25 * t;
        return fromTime ([g, b] (double p)
        {
            return std::sin (g * (std::sin (2.0 * kPi * p) + b));
        });
    }));

    // 6 HOLLOW : harmoniques impaires + peigne spectral mobile
    tables.push_back (build ([] (double t)
    {
        return fromSineAmps ([t] (int h)
        {
            const double odd  = (h % 2) ? 1.0 : 0.18 + 0.6 * t;
            const double comb = 0.5 + 0.5 * std::cos (2.0 * kPi * h * (0.02 + 0.22 * t));
            return odd * comb / (double) h;
        });
    }));

    // 7 DIGITAL : scie+sinus quantifiés (64 -> 3 marches)
    tables.push_back (build ([] (double t)
    {
        const double steps = std::round (64.0 * std::pow (3.0 / 64.0, t));
        return fromTime ([steps] (double p)
        {
            const double q = std::floor (p * steps) / steps;
            return 0.6 * saw (q + 0.5) + 0.6 * std::sin (2.0 * kPi * q * 2.0);
        });
    }));

    // 8 PULSE : pulse qui se rétrécit (50% -> 3%)
    tables.push_back (build ([] (double t)
    {
        const double w = 0.5 - 0.47 * t;
        return fromCosAmps ([w] (int h)
        {
            return 2.0 / (kPi * h) * std::sin (kPi * h * w);
        });
    }));

    // 9 REESE : deux scies déphasées (annulations en peigne qui bougent)
    tables.push_back (build ([] (double t)
    {
        const double ph = 0.5 * t;
        return fromSineAmps ([ph] (int h)
        {
            return std::cos (kPi * h * ph) / (double) h;
        });
    }));

    // 10 TALK : A -> E -> I -> O -> U sur scie
    tables.push_back (build ([fg] (double t)
    {
        const Vowel v = vowelPath ({ vA, vE, vI, vO, vU }, t);
        return fromSineAmps ([&] (int h)
        {
            return (1.0 / (double) h) * formantEnv (h * 90.0, v, 0.18, fg) * 3.0;
        });
    }));

    // 11 CHAINSAW : scie synchronisée saturée
    tables.push_back (build ([] (double t)
    {
        const double r = 1.5 + 3.5 * t;
        return fromTime ([r, t] (double p)
        {
            const double x = 0.7 * saw (p * r) + 0.5 * (p < 0.5 ? 1.0 : -1.0);
            return std::tanh ((2.0 + 4.0 * t) * x);
        });
    }));
}
} // namespace fragor
