#include "Common.h"
#include "../../Shared/Link/AleaLink.h"

namespace alea
{
juce::String vpToText (int vp, float n)
{
    auto signedStr = [] (float v, int dec) { return (v >= 0.0f ? "+" : "") + juce::String (v, dec); };
    auto timeStr = [] (float sec)
    {
        if (sec < 1.0f) return juce::String (sec * 1000.0f, sec < 0.01f ? 1 : 0) + " ms";
        return juce::String (sec, 2) + " s";
    };

    switch (vp)
    {
        case Tune:       return signedStr (map::tuneSemis (n), 1) + " st";
        case Decay:      return timeStr (map::decaySec (n));
        case Attack:     return timeStr (map::attackSec (n));
        case PitchDecay: return timeStr (map::pitchDecaySec (n));
        case PitchAmt:   return juce::String (map::pitchSemis (n), 1) + " st";
        case Cutoff:
        {
            const float hz = map::cutoffHz (n);
            return hz < 1000.0f ? juce::String ((int) hz) + " Hz" : juce::String (hz / 1000.0f, 1) + " kHz";
        }
        case FEnv:  return signedStr (map::fenvOct (n), 1) + " oct";
        case Punch: return signedStr (map::punch (n) * 100.0f, 0) + " %";
        case Level: return n <= 0.001f ? juce::String ("-inf") : juce::String (map::levelDb (n), 1) + " dB";
        case Pan:
        {
            const float p = map::pan (n);
            if (std::abs (p) < 0.01f) return "C";
            return (p < 0.0f ? "L" : "R") + juce::String ((int) std::round (std::abs (p) * 100.0f));
        }
        default: return juce::String ((int) std::round (n * 100.0f)) + " %";
    }
}

const KitVoice& defaultKit (int t)
{
    //                         Tune  Dec   Att PAmt  PDec  Timb  Col   Noi Cut Res FEnv Drv  Cru Lvl   Pan   Dly   Rev
    static const KitVoice kit[kNumTracks] = {
        { "Kick",  EngKick,  { 0.50f, 0.66f, 0, 0.60f, 0.55f, 0.20f, 0.30f, 0, 1, 0, 0.5f, 0.15f, 0, 0.80f, 0.50f, 0.00f, 0.00f }, {}, 0 },
        { "Snare", EngSnare, { 0.50f, 0.55f, 0, 0.25f, 0.40f, 0.60f, 0.60f, 0, 1, 0, 0.5f, 0.10f, 0, 0.78f, 0.50f, 0.05f, 0.20f }, {}, 0 },
        { "Clap",  EngClap,  { 0.50f, 0.50f, 0, 0.00f, 0.30f, 0.40f, 0.40f, 0, 1, 0, 0.5f, 0.05f, 0, 0.76f, 0.55f, 0.00f, 0.30f }, {}, 0 },
        { "C.Hat", EngMetal, { 0.50f, 0.33f, 0, 0.00f, 0.30f, 0.10f, 0.60f, 0, 1, 0, 0.5f, 0.00f, 0, 0.80f, 0.42f, 0.00f, 0.05f },
                             { 0, 0.20f, 0, 0, 0, 0, 0.10f, 0, 0, 0, 0, 0, 0, 0.15f, 0, 0, 0 }, 1 },
        { "O.Hat", EngMetal, { 0.50f, 0.60f, 0, 0.00f, 0.30f, 0.10f, 0.55f, 0, 1, 0, 0.5f, 0.00f, 0, 0.76f, 0.60f, 0.15f, 0.10f },
                             { 0, 0.15f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1 },
        { "Tom",   EngModal, { 0.50f, 0.55f, 0, 0.15f, 0.40f, 0.00f, 0.50f, 0, 1, 0, 0.5f, 0.10f, 0, 0.76f, 0.40f, 0.00f, 0.15f }, {}, 0 },
        { "Perc",  EngFM,    { 0.55f, 0.40f, 0, 0.10f, 0.30f, 0.50f, 0.35f, 0, 1, 0, 0.5f, 0.00f, 0, 0.76f, 0.65f, 0.30f, 0.10f },
                             { 0, 0, 0, 0, 0, 0.30f, 0.15f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 0 },
        { "Bell",  EngModal, { 0.70f, 0.70f, 0, 0.00f, 0.30f, 1.00f, 0.80f, 0, 1, 0, 0.5f, 0.00f, 0, 0.76f, 0.35f, 0.25f, 0.35f }, {}, 0 },
    };
    return kit[juce::jlimit (0, kNumTracks - 1, t)];
}

float defaultVP (int t, int vp)
{
    if (vp == Punch) return 0.5f;
    if (vp == FxAmt) return 0.3f;
    if (vp == Layer) return 0.0f;
    return defaultKit (t).v[vp];
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const NormalisableRange<float> unit (0.0f, 1.0f);

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) std::round (v * 100.0f)) + " %"; });

    // --- Performance : en tete de liste pour que Push / les controleurs affichent ces 8 macros en premier
    {
        auto perf = std::make_unique<AudioProcessorParameterGroup> ("perf", "Performance", "|");
        perf->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "chaos", 1 }, "Chaos", unit, 0.5f,
                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "x" + String (v * 2.0f, 2); })));
        perf->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "density", 1 }, "Density", unit, 0.5f,
                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v >= 0.5f ? "+" : "") + String ((int) std::round ((v - 0.5f) * 200.0f)) + " %"; })));
        perf->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "morph", 1 }, "Morph A-B", unit, 0.0f, pct));
        perf->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "scenexf", 1 }, "Scene Crossfade", unit, 0.0f, pct));
        perf->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "seqchaos", 1 }, "Sequence Chaos", unit, 0.0f, pct));
        perf->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "evoamt", 1 }, "Evolve", unit, 0.0f, pct));
        perf->addChild (std::make_unique<AudioParameterFloat> (ParameterID { "duckamt", 1 }, "Sidechain Duck", unit, 0.0f, pct));
        perf->addChild (std::make_unique<AudioParameterBool>  (ParameterID { "fill", 1 }, "Fill", false));
        layout.add (std::move (perf));
    }

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& kit = defaultKit (t);
        const String pre = "T" + String (t + 1) + " ";

        auto group = std::make_unique<AudioProcessorParameterGroup> ("track" + String (t + 1), "Track " + String (t + 1), "|");

        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { tid (t, "engine"), 1 }, pre + "Engine", engineNames(), kit.engine));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { tid (t, "ftype"), 1 },  pre + "Filter Type", StringArray { "LP", "BP", "HP" }, 0));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { tid (t, "choke"), 1 },  pre + "Choke", StringArray { "Off", "A", "B", "C" }, kit.choke));
        group->addChild (std::make_unique<AudioParameterBool>   (ParameterID { tid (t, "mute"), 1 },   pre + "Mute", false));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { tid (t, "out"), 1 },    pre + "Output", StringArray { "Main", "Separate" }, 0));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { tid (t, "velsens"), 1 }, pre + "Velocity Sens", unit, 0.7f, pct));
        group->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { tid (t, "link"), 1 },  pre + "Rnd Link", unit, 0.0f, pct));
        group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { tid (t, "fxtype"), 1 }, pre + "FX Type", StringArray { "FX: Off", "Fold", "Comb", "Ring", "Tape" }, 0));

        for (int vp = 0; vp < kNumVP; ++vp)
        {
            auto attr = AudioParameterFloatAttributes().withStringFromValueFunction ([vp] (float v, int) { return vpToText (vp, v); });
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { vpId (t, vp), 1 }, pre + vpInfo (vp).label, unit, defaultVP (t, vp), attr));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { jitId (t, vp), 1 }, pre + vpInfo (vp).label + " Rnd", unit, kit.j[vp], pct));
        }
        layout.add (std::move (group));
    }

    // --- Global / sequenceur
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "play", 1 }, "Play (internal)", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Internal BPM", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 124.0f));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { "pattern", 1 }, "Pattern", 0, kNumPatterns - 1, 0,
                    AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String::charToString ((juce_wchar) ('A' + v)); })));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { "seed", 1 }, "Seed", 0, 999, 1));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "rndmode", 1 }, "Random Mode", StringArray { "Mode: Hit", "Mode: Bar", "Mode: Drift" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "rnddist", 1 }, "Random Distribution", StringArray { "Dist: Uniform", "Dist: Gauss", "Dist: Rare" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "scale", 1 }, "Tune Scale", scaleNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "root", 1 }, "Root Key",
                    StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 9));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "keylock", 1 }, "Key Lock",
                    StringArray { "Key lock: Off", "Key lock: Melodic", "Key lock: All" }, 0));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "keychord", 1 }, "Key Lock follows Color chords", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "moddepth", 1 }, "Mod Depth", unit, 1.0f, pct));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "songmode", 1 }, "Song Mode", false));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "midiout", 1 }, "MIDI Out", true));

    // --- Modulateurs aleatoires
    {
        static const int defRate[kNumMods] { 2, 4, 1, 0, 4, 2 };
        static const float defP[kNumMods] { 1.0f, 0.3f, 0.2f, 0.9f, 0.0f, 0.3f };
        for (int m = 0; m < kNumMods; ++m)
        {
            layout.add (std::make_unique<AudioParameterChoice>(ParameterID { modId (m, "rate"), 1 }, "Mod " + String (m + 1) + " Rate", modRateNames(), defRate[m]));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { modId (m, "p"), 1 }, "Mod " + String (m + 1) + " " + modParamNames()[m], unit, defP[m], pct));
        }
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { "m3len", 1 }, "Turing Length", 2, 16, 8));
        {
            StringArray ft;
            for (int t = 0; t < kNumTracks; ++t) ft.add ("Follow T" + String (t + 1));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "m6trk", 1 }, "Follow Track", ft, 0));
        }

        StringArray trk { "All" };
        for (int t = 0; t < kNumTracks; ++t) trk.add ("T" + String (t + 1));
        StringArray dst;
        for (int vp = 0; vp < kNumVP; ++vp) dst.add (vpInfo (vp).label);
        dst.addArray (globalTargetNames());
        StringArray src { "Off" };
        src.addArray (modNames());
        for (int sl = 0; sl < kNumMxSlots; ++sl)
        {
            layout.add (std::make_unique<AudioParameterChoice>(ParameterID { mxId (sl, "src"), 1 }, "Mx" + String (sl + 1) + " Source", src, 0));
            layout.add (std::make_unique<AudioParameterChoice>(ParameterID { mxId (sl, "trk"), 1 }, "Mx" + String (sl + 1) + " Track", trk, 0));
            layout.add (std::make_unique<AudioParameterChoice>(ParameterID { mxId (sl, "dst"), 1 }, "Mx" + String (sl + 1) + " Target", dst, Cutoff));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mxId (sl, "amt"), 1 }, "Mx" + String (sl + 1) + " Depth",
                            NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v >= 0 ? "+" : "") + String ((int) std::round (v * 100.0f)) + " %"; })));
        }
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "accent", 1 }, "Accent", unit, 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ratchaos", 1 }, "Ratchet Chaos", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize", unit, 0.0f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return "+/-" + String (v * 20.0f, 1) + " ms"; })));

    // --- Jeu, evolution, scenes, sidechain
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glide", 1 }, "Slide Time", unit, 0.3f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) (10.0f + v * v * 490.0f)) + " ms"; })));
    {
        StringArray kt { "Keys: Off" };
        for (int t = 0; t < kNumTracks; ++t) kt.add ("Keys: T" + String (t + 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "keystrack", 1 }, "Keyboard Track", kt, 0));
        StringArray ds;
        for (int t = 0; t < kNumTracks; ++t) ds.add ("Duck by T" + String (t + 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ducksrc", 1 }, "Duck Source", ds, 0));
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "duckrel", 1 }, "Duck Release", unit, 0.35f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) (30.0f + v * v * 600.0f)) + " ms"; })));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "evobars", 1 }, "Evolve Every", StringArray { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 2));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "evotarget", 1 }, "Evolve Target", StringArray { "Evolve: Pattern", "Evolve: Sound", "Evolve: Both" }, 2));
    {
        StringArray sc;
        for (int i = 0; i < kNumScenes; ++i) sc.add ("Scene " + String (i + 1));
        layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "scenea", 1 }, "Scene A", sc, 0));
        layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "sceneb", 1 }, "Scene B", sc, 1));
    }
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "sceneon", 1 }, "Scene Crossfader On", false));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "hq", 1 }, "HQ Oversampling", false));

    // --- Delay
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "dlydiv", 1 }, "Delay Time", delayDivNames(), 6));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlyfb", 1 }, "Delay Feedback", unit, 0.45f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlytone", 1 }, "Delay Tone", unit, 0.6f, pct));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "dlyping", 1 }, "Delay Ping-Pong", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlyret", 1 }, "Delay Return", unit, 0.7f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlyrnd", 1 }, "Delay Random", unit, 0.0f, pct));

    // --- Reverb
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revsize", 1 }, "Reverb Size", unit, 0.6f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revdamp", 1 }, "Reverb Damp", unit, 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revwidth", 1 }, "Reverb Width", unit, 1.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revret", 1 }, "Reverb Return", unit, 0.6f, pct));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "revtype", 1 }, "Reverb Type", StringArray { "Room", "Plate" }, 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revpre", 1 }, "Reverb Predelay", unit, 0.1f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) (v * 150.0f)) + " ms"; })));

    // --- Stutter (beat repeat aleatoire)
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "stuchance", 1 }, "Stutter Chance", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "studiv", 1 }, "Stutter Slice", StringArray { "1/8", "1/16", "1/32", "1/64", "Rnd" }, 4));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "stulen", 1 }, "Stutter Length", StringArray { "1/2 beat", "1 beat", "2 beats", "4 beats" }, 1));

    // --- Master
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glue", 1 }, "Glue", unit, 0.2f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mdrive", 1 }, "Master Drive", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", unit, 0.8f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return vpToText (Level, v); })));

    alea::link::addLinkParameters (layout);
    return layout;
}

void diceRange (int engine, int vp, float& lo, float& hi)
{
    // plages generiques
    static const float gen[kNumVP][2] = {
        { 0.30f, 0.70f }, { 0.25f, 0.70f }, { 0.0f, 0.15f }, { 0.0f, 0.50f }, { 0.10f, 0.60f },
        { 0.0f, 1.0f },   { 0.0f, 1.0f },   { 0.0f, 0.40f },
        { 0.55f, 1.0f },  { 0.0f, 0.60f },  { 0.40f, 0.75f },
        { 0.0f, 0.60f },  { 0.0f, 0.40f },
        { 0.70f, 0.80f }, { 0.30f, 0.70f }, { 0.0f, 0.40f }, { 0.0f, 0.40f },
        { 0.35f, 0.80f }, { 0.10f, 0.70f }, { 0.0f, 0.0f } };
    lo = gen[vp][0]; hi = gen[vp][1];

    auto set = [&] (int which, float a, float b) { if (which == vp) { lo = a; hi = b; } };
    switch (engine)
    {
        case EngKick:
            set (Tune, 0.38f, 0.60f); set (Decay, 0.35f, 0.72f); set (PitchAmt, 0.40f, 0.85f); set (PitchDecay, 0.15f, 0.55f);
            set (Noise, 0.0f, 0.15f); set (Pan, 0.5f, 0.5f); set (SendDly, 0.0f, 0.1f); set (SendRev, 0.0f, 0.15f); set (Cutoff, 0.7f, 1.0f);
            break;
        case EngSnare:
            set (Tune, 0.40f, 0.65f); set (Decay, 0.30f, 0.62f); set (PitchAmt, 0.10f, 0.40f); set (Timbre, 0.30f, 0.90f); set (Color, 0.2f, 1.0f);
            break;
        case EngMetal:
            set (Tune, 0.35f, 0.65f); set (Decay, 0.10f, 0.65f); set (PitchAmt, 0.0f, 0.10f); set (Cutoff, 0.75f, 1.0f);
            break;
        case EngClap:
            set (Decay, 0.30f, 0.65f); set (PitchAmt, 0.0f, 0.0f); set (Timbre, 0.10f, 0.80f);
            break;
        case EngFM:
            set (Tune, 0.30f, 0.80f); set (Decay, 0.15f, 0.55f); set (Timbre, 0.10f, 0.90f);
            break;
        case EngModal:
            set (Tune, 0.30f, 0.80f); set (Decay, 0.30f, 0.75f);
            break;
        case EngPluck:
            set (Tune, 0.35f, 0.80f); set (Decay, 0.30f, 0.75f); set (PitchAmt, 0.0f, 0.15f);
            break;
        case EngZap:
            set (Tune, 0.30f, 0.70f); set (Decay, 0.15f, 0.55f); set (PitchDecay, 0.15f, 0.6f); set (PitchAmt, 0.0f, 0.3f);
            break;
        case EngHybrid:
            set (Tune, 0.38f, 0.62f); set (Decay, 0.30f, 0.70f); set (PitchAmt, 0.30f, 0.80f); set (PitchDecay, 0.15f, 0.5f);
            break;
        case EngSample:
            set (Tune, 0.40f, 0.60f); set (Decay, 0.45f, 0.90f); set (PitchAmt, 0.0f, 0.10f); set (Timbre, 0.0f, 0.6f); set (Color, 0.0f, 0.3f);
            break;
        default: break;
    }
}

} // namespace alea
