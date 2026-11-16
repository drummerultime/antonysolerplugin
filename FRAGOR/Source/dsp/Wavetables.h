#pragma once

#include <vector>
#include <cmath>
#include <memory>

namespace fragor
{
    constexpr int kTableSize  = 2048;   // échantillons par cycle
    constexpr int kTableMask  = kTableSize - 1;
    constexpr int kFrames     = 16;     // positions de wavetable (interpolées)
    constexpr int kMips       = 11;     // niveaux anti-repliement (1024 -> 1 harmonique)
    constexpr int kStride     = kTableSize + 1; // +1 point de garde pour l'interpolation

    struct Wavetable
    {
        std::vector<float> data; // [frame][mip][kStride]
        const float* get (int frame, int mip) const noexcept
        {
            return data.data() + ((size_t) frame * kMips + (size_t) mip) * kStride;
        }
    };

    class WavetableBank
    {
    public:
        static const WavetableBank& instance();
        int  size() const noexcept                   { return (int) tables.size(); }
        const Wavetable& table (int i) const noexcept { return tables[(size_t) (i < 0 ? 0 : (i >= size() ? size() - 1 : i))]; }

        // Choix du niveau mip selon le nombre d'harmoniques autorisées sous Nyquist
        static int mipForIncrement (double phaseInc) noexcept
        {
            // phaseInc = f / sr ; harmoniques max = 0.5 / phaseInc
            if (phaseInc <= 0.0) return 0;
            const double maxH = 0.5 / phaseInc;
            int m = 0;
            int h = 1024;
            while (h > maxH && m < kMips - 1) { h >>= 1; ++m; }
            return m;
        }

        // Lecture interpolée (phase 0..1, position 0..1)
        static inline float read (const Wavetable& t, float phase, float pos, int mip) noexcept
        {
            const float fp = pos * (float) (kFrames - 1);
            int f0 = (int) fp;
            if (f0 >= kFrames - 1) f0 = kFrames - 2;
            if (f0 < 0) f0 = 0;
            const float ff = fp - (float) f0;

            const float x  = phase * (float) kTableSize;
            int i = (int) x;
            const float fr = x - (float) i;
            i &= kTableMask;

            const float* a = t.get (f0, mip);
            const float* b = t.get (f0 + 1, mip);
            const float sa = a[i] + fr * (a[i + 1] - a[i]);
            const float sb = b[i] + fr * (b[i + 1] - b[i]);
            return sa + ff * (sb - sa);
        }

        static constexpr int kNumBuiltin = 12;

        // Construit une table depuis kFrames cycles de kTableSize échantillons
        static std::shared_ptr<Wavetable> buildFromCycles (const std::vector<std::vector<float>>& cycles);
        // Découpe un fichier audio (mono) en kFrames cycles : format wavetable (multiples de 2048)
        // ou détection de période sur un son quelconque. Renvoie false si inutilisable.
        static bool sliceAudio (const float* data, int len, std::vector<std::vector<float>>& cycles);

    private:
        WavetableBank();
        std::vector<Wavetable> tables;
    };
}
