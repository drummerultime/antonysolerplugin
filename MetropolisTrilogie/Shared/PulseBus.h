#pragma once
// Pulsation des musiciens partagee entre plugins : Maria publie le tempo et la phase qu'elle entend,
// Rotwang et Grot s'y calent. Tous les plugins du Mac voient le meme petit fichier en memoire.
#include <juce_core/juce_core.h>
#include <atomic>
#include <cmath>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace metro
{
struct PulseSlot
{
    std::atomic<uint32_t> seq;          // impair = ecriture en cours
    double bpm, beatMs, conf, stampMs;  // beatMs : instant (horloge haute resolution) d'un temps
};
struct PulseShared
{
    uint32_t magic;
    std::atomic<uint32_t> next;
    PulseSlot slot[8];
};

class PulseBus
{
public:
    static PulseBus& get() { static PulseBus b; return b; }
    bool ok() const noexcept { return p != nullptr; }
    int claimSlot() noexcept { return p ? (int) (p->next.fetch_add (1) % 8u) : 0; }

    void publish (int s, double bpm, double beatMs, double conf, double nowMs) noexcept
    {
        if (! p) return;
        auto& sl = p->slot[s & 7];
        sl.seq.fetch_add (1, std::memory_order_acq_rel);
        sl.bpm = bpm; sl.beatMs = beatMs; sl.conf = conf; sl.stampMs = nowMs;
        sl.seq.fetch_add (1, std::memory_order_acq_rel);
    }
    void retract (int s) noexcept { if (p) publish (s, 0.0, 0.0, 0.0, 0.0); }

    // la pulsation la plus sure, publiee il y a moins d'une seconde et demie
    bool read (double nowMs, double& bpm, double& beatMs) const noexcept
    {
        if (! p) return false;
        double best = 0.0;
        for (auto& sl : p->slot)
        {
            const uint32_t a = sl.seq.load (std::memory_order_acquire);
            if (a & 1u) continue;
            const double b = sl.bpm, t = sl.beatMs, c = sl.conf, st = sl.stampMs;
            if (sl.seq.load (std::memory_order_acquire) != a) continue;
            if (b < 30.0 || nowMs - st > 1500.0 || nowMs < st - 100.0 || c <= best) continue;
            best = c; bpm = b; beatMs = t;
        }
        return best > 0.0;
    }

private:
    PulseBus()
    {
        const auto path = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("metropolis_pulse.bin").getFullPathName();
        const int fd = ::open (path.toRawUTF8(), O_RDWR | O_CREAT, 0666);
        if (fd < 0) return;
        if (::ftruncate (fd, sizeof (PulseShared)) == 0)
        {
            void* m = ::mmap (nullptr, sizeof (PulseShared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (m != MAP_FAILED) p = static_cast<PulseShared*> (m);
        }
        ::close (fd);
    }
    PulseShared* p = nullptr;
};

// Horloge en noires calee sur la pulsation publiee (boucle a verrouillage de phase, sans retour en arriere).
struct PulseFollower
{
    double ppq = 0.0; bool active = false;
    // renvoie false si aucune pulsation fiable : l'appelant garde son horloge habituelle
    bool step (int n, double fs, double nowMs, double fallbackPpq, double& ppqStart, double& pps, double& bpmOut) noexcept
    {
        double bpm = 0.0, beatMs = 0.0;
        if (! PulseBus::get().read (nowMs, bpm, beatMs)) { active = false; return false; }
        if (! active) { ppq = fallbackPpq; active = true; }
        const double periodMs = 60000.0 / bpm;
        double target = std::fmod ((nowMs - beatMs) / periodMs, 1.0);
        if (target < 0) target += 1.0;
        double err = target - (ppq - std::floor (ppq));
        if (err > 0.5) err -= 1.0;
        if (err < -0.5) err += 1.0;
        pps = bpm / 60.0 / fs * (1.0 + juce::jlimit (-0.4, 0.4, 2.0 * err));
        ppqStart = ppq; ppq += pps * n; bpmOut = bpm;
        return true;
    }
};
} // namespace metro
