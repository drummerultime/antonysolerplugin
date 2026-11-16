#include "CantusCommon.h"
#include "../../Shared/Link/AleaLink.h"

namespace cantus
{
const TPInfo& tpInfo (int i)
{
    //                               id         label      defaut  alea
    static const TPInfo t[kNumTP] = {
        { "shape",   "Shape",    0.45f, 0.0f },  { "o2lvl",  "Osc2 Lvl", 0.30f, 0.0f }, { "o2semi", "Osc2 Semi", 0.50f, 0.0f },
        { "o2fine",  "Osc2 Fine",0.54f, 0.0f },  { "fm",     "FM",       0.00f, 0.0f }, { "sub",    "Sub",       0.05f, 0.0f },
        { "noise",   "Noise",    0.01f, 0.0f },
        { "cutoff",  "Cutoff",   0.62f, 0.04f }, { "reso",   "Reso",     0.25f, 0.0f }, { "envmod", "Env Mod",   0.30f, 0.0f },
        { "fdecay",  "F.Decay",  0.50f, 0.0f },  { "accent", "Accent",   0.40f, 0.0f }, { "keytrk", "Key Trk",   0.50f, 0.0f },
        { "fdrive",  "F.Drive",  0.10f, 0.0f },
        { "attack",  "Attack",   0.12f, 0.0f },  { "decay",  "Decay",    0.60f, 0.0f }, { "sustain","Sustain",   0.75f, 0.0f },
        { "release", "Release",  0.35f, 0.0f },  { "glide",  "Glide",    0.35f, 0.0f },
        { "drive",   "Drive",    0.10f, 0.0f },  { "level",  "Level",    0.74f, 0.0f }, { "pan",    "Pan",       0.50f, 0.0f },
        { "sdly",    "Dly Snd",  0.30f, 0.0f },  { "srev",   "Rev Snd",  0.30f, 0.0f } };
    return t[juce::jlimit (0, kNumTP - 1, i)];
}

const std::array<int, kNumTP>& tpDisplayOrder()
{
    static const std::array<int, kNumTP> o { Shape, O2Level, O2Semi, O2Fine, FM, SubLevel, NoiseLvl, FDrive, Cutoff, Reso, EnvMod, FDecay,
                                             AccentAmt, KeyTrack, Attack, Decay, Sustain, Release, Glide, Drive, Level, Pan, SendDly, SendRev };
    return o;
}

juce::String tpToText (int tp, float n)
{
    auto timeStr = [] (float s) { return s < 1.0f ? juce::String (s * 1000.0f, s < 0.01f ? 1 : 0) + " ms" : juce::String (s, 2) + " s"; };
    switch (tp)
    {
        case O2Semi:   { const float v = tmap::semis (n); return (v >= 0 ? "+" : "") + juce::String ((int) v) + " st"; }
        case O2Fine:   { const float v = tmap::cents (n); return (v >= 0 ? "+" : "") + juce::String ((int) v) + " ct"; }
        case Cutoff:   { const float hz = tmap::cutoffHz (n); return hz < 1000.0f ? juce::String ((int) hz) + " Hz" : juce::String (hz / 1000.0f, 1) + " kHz"; }
        case EnvMod:   return juce::String (tmap::envOct (n), 1) + " oct";
        case FDecay:   return timeStr (tmap::fdecaySec (n));
        case Attack:   return timeStr (tmap::attackSec (n));
        case Decay:    return timeStr (tmap::decaySec (n));
        case Release:  return timeStr (tmap::releaseSec (n));
        case Glide:    return timeStr (tmap::glideSec (n));
        case Level:    return n <= 0.001f ? juce::String ("-inf") : juce::String (60.0f * n - 48.0f, 1) + " dB";
        case Pan:      { const float p = tmap::pan (n); if (std::abs (p) < 0.01f) return "C"; return (p < 0 ? "L" : "R") + juce::String ((int) std::round (std::abs (p) * 100.0f)); }
        default:       return juce::String ((int) std::round (n * 100.0f)) + " %";
    }
}

float scaleShift (float relSemis, int scale, int degrees)
{
    const auto& d = alea::scaleDegrees (scale <= 0 ? 1 : scale);
    const int n = (int) d.size();
    const float q = quantizeSemis (relSemis, scale <= 0 ? 1 : scale);
    int oct = (int) std::floor (q / 12.0f);
    const int rem = (int) std::round (q - (float) oct * 12.0f);
    int idx = 0;
    for (int i = 0; i < n; ++i) if (d[(size_t) i] == rem) idx = i;
    idx += degrees;
    oct += (int) std::floor ((float) idx / (float) n);
    idx = ((idx % n) + n) % n;
    return (float) (oct * 12 + d[(size_t) idx]);
}

juce::AudioProcessorValueTreeState::ParameterLayout createCantusLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const NormalisableRange<float> unit (0.0f, 1.0f);
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) std::round (v * 100.0f)) + " %"; });

    // Performance en tete (Push / controleurs) - memes identifiants qu'Alea
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

    {
        auto v = std::make_unique<AudioProcessorParameterGroup> ("voice", "Voice", "|");
        v->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "osc1type", 1 }, "Osc1 Type", osc1Names(), 0));
        v->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "osc2type", 1 }, "Osc2 Type", osc2Names(), 0));
        v->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "subtype", 1 },  "Sub Type", subNames(), 0));
        v->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "ftype", 1 },    "Filter Type", filterNames(), 0));
        v->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "drivetype", 1 },"Drive Type", driveNames(), 0));
        v->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "chord", 1 },    "Chord", chordNames(), 0));
        v->addChild (std::make_unique<AudioParameterChoice> (ParameterID { "glidemode", 1 },"Glide Mode", glideModeNames(), 0));
        v->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { "velsens", 1 },  "Velocity Sens", unit, 0.5f, pct));
        v->addChild (std::make_unique<AudioParameterInt>    (ParameterID { "baseoct", 1 },  "Base Octave", 0, 4, 3,
                         AudioParameterIntAttributes().withStringFromValueFunction ([] (int o, int) { return "Oct C" + String (o); })));
        v->addChild (std::make_unique<AudioParameterFloat>  (ParameterID { "link_rnd", 1 },  "Rnd Link", unit, 0.0f, pct));
        for (int i = 0; i < kNumTP; ++i)
        {
            auto attr = AudioParameterFloatAttributes().withStringFromValueFunction ([i] (float x, int) { return tpToText (i, x); });
            v->addChild (std::make_unique<AudioParameterFloat> (ParameterID { tpId (i), 1 }, tpInfo (i).label, unit, tpInfo (i).def, attr));
            v->addChild (std::make_unique<AudioParameterFloat> (ParameterID { tpJit (i), 1 }, String (tpInfo (i).label) + " Rnd", unit, tpInfo (i).jit, pct));
        }
        layout.add (std::move (v));
    }

    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "play", 1 }, "Play (internal)", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Internal BPM", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 124.0f));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { "pattern", 1 }, "Pattern", 0, kPatterns - 1, 0,
                    AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String::charToString ((juce_wchar) ('A' + v)); })));
    layout.add (std::make_unique<AudioParameterInt>   (ParameterID { "seed", 1 }, "Seed", 0, 999, 1));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "rndmode", 1 }, "Random Mode", StringArray { "Mode: Hit", "Mode: Bar", "Mode: Drift" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "rnddist", 1 }, "Random Distribution", StringArray { "Dist: Uniform", "Dist: Gauss", "Dist: Rare" }, 0));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "root", 1 }, "Root", rootNames(), 9));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "scale", 1 }, "Scale", scaleNames(), 3));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "swing", 1 }, "Swing", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ratchaos", 1 }, "Ratchet Chaos", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "humanize", 1 }, "Humanize", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "notechaos", 1 }, "Note Chaos", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "moddepth", 1 }, "Mod Depth", unit, 1.0f, pct));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "songmode", 1 }, "Song Mode", false));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "midiout", 1 }, "MIDI Out", true));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "keysmode", 1 }, "Keys Mode", keysModeNames(), 1));

    // modulateurs
    {
        static const int defRate[kNumMods] { 2, 4, 1, 0, 4, 2 };
        static const float defP[kNumMods] { 1.0f, 0.3f, 0.2f, 0.9f, 0.0f, 0.3f };
        static const char* pn[kNumMods] { "Chance", "Jump", "Change", "R", "Shape", "Release" };
        for (int m = 0; m < kNumMods; ++m)
        {
            layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "m" + String (m + 1) + "rate", 1 }, "Mod " + String (m + 1) + " Rate", modRateNames(), defRate[m]));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "m" + String (m + 1) + "p", 1 }, "Mod " + String (m + 1) + " " + pn[m], unit, defP[m], pct));
        }
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { "m3len", 1 }, "Turing Length", 2, 16, 8));
        StringArray ft;
        for (int t = 0; t < 8; ++t) ft.add ("Drums T" + String (t + 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "m6trk", 1 }, "Follow Drum Track", ft, 0));

        StringArray src { "Off", "S&H", "Smooth", "Turing", "Chaos", "LFO", "Drums" };
        StringArray dst;
        for (int i = 0; i < kNumTP; ++i) dst.add (tpInfo (i).label);
        dst.addArray (gtNames());
        for (int s = 0; s < kMxSlots; ++s)
        {
            const String pre = "mx" + String (s + 1);
            layout.add (std::make_unique<AudioParameterChoice>(ParameterID { pre + "src", 1 }, "Mx" + String (s + 1) + " Source", src, 0));
            layout.add (std::make_unique<AudioParameterChoice>(ParameterID { pre + "dst", 1 }, "Mx" + String (s + 1) + " Target", dst, Cutoff));
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pre + "amt", 1 }, "Mx" + String (s + 1) + " Depth",
                            NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v >= 0 ? "+" : "") + String ((int) std::round (v * 100.0f)) + " %"; })));
        }
    }

    // scenes, evolution
    {
        StringArray sc;
        for (int i = 0; i < kScenes; ++i) sc.add ("Scene " + String (i + 1));
        layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "scenea", 1 }, "Scene A", sc, 0));
        layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "sceneb", 1 }, "Scene B", sc, 1));
    }
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "sceneon", 1 }, "Scene Crossfader On", false));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "evobars", 1 }, "Evolve Every", StringArray { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" }, 2));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "evotarget", 1 }, "Evolve Target", StringArray { "Evolve: Pattern", "Evolve: Sound", "Evolve: Both" }, 2));

    // effets
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "chrate", 1 }, "Chorus Rate", unit, 0.3f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "chdepth", 1 }, "Chorus Depth", unit, 0.4f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "chmix", 1 }, "Chorus Mix", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "dlydiv", 1 }, "Delay Time", delayDivNames(), 6));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlyfb", 1 }, "Delay Feedback", unit, 0.4f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlytone", 1 }, "Delay Tone", unit, 0.5f, pct));
    layout.add (std::make_unique<AudioParameterBool>  (ParameterID { "dlyping", 1 }, "Delay Ping-Pong", true));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlyret", 1 }, "Delay Return", unit, 0.6f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dlyrnd", 1 }, "Delay Random", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revsize", 1 }, "Reverb Size", unit, 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revdamp", 1 }, "Reverb Damp", unit, 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revwidth", 1 }, "Reverb Width", unit, 1.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revret", 1 }, "Reverb Return", unit, 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "revpre", 1 }, "Reverb Predelay", unit, 0.1f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) (v * 150.0f)) + " ms"; })));
    {
        StringArray ds { "Duck: Off" };
        for (int t = 0; t < 8; ++t) ds.add ("Duck by Alea T" + String (t + 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ducksrc", 1 }, "Duck Source", ds, 1));
    }
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "duckrel", 1 }, "Duck Release", unit, 0.35f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) (30.0f + v * v * 600.0f)) + " ms"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "glue", 1 }, "Glue", unit, 0.15f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mdrive", 1 }, "Master Drive", unit, 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", unit, 0.8f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v <= 0.001f ? String ("-inf") : String (60.0f * v - 48.0f, 1) + " dB"; })));

    // generateur de lignes
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "genstyle", 1 }, "Generator Style", genStyleNames(), 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gendens", 1 }, "Gen Density", unit, 0.7f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "genrange", 1 }, "Gen Range", unit, 0.4f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "genacc", 1 }, "Gen Accents", unit, 0.3f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "genslide", 1 }, "Gen Slides", unit, 0.25f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "genoct", 1 }, "Gen Octave Jumps", unit, 0.2f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "genroot", 1 }, "Gen Root Weight", unit, 0.5f, pct));

    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "chordfollow", 1 }, "Chord Follow (Color)",
                    StringArray { "Chords: Off", "Chords: Melodic", "Chords: Chord tones", "Chords: Scale" }, 1));
    // improvisation, dialogue avec la basse, vibrato
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "improv", 1 }, "Improvise",
                    StringArray { "Improvise: Off", "Improvise: 1 bar", "Improvise: 2 bars", "Improvise: 4 bars", "Improvise: 8 bars" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "improvdev", 1 }, "Development", unit, 0.4f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dialogue", 1 }, "Dialogue (bass)", unit, 0.25f, pct));
    layout.add (std::make_unique<AudioParameterChoice>(ParameterID { "callresp", 1 }, "Call & Response",
                    StringArray { "Call/Resp: Off", "Call/Resp: 1 bar", "Call/Resp: 2 bars", "Call/Resp: 4 bars" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibdepth", 1 }, "Vibrato Depth", unit, 0.25f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) (v * v * 60.0f)) + " ct"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibrate", 1 }, "Vibrato Rate", unit, 0.45f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (2.0f + v * 6.0f, 1) + " Hz"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "vibdelay", 1 }, "Vibrato Delay", unit, 0.35f,
                    AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) (v * 1000.0f)) + " ms"; })));
    alea::link::addLinkParameters (layout);
    return layout;
}

void tpDiceRange (int tp, float& lo, float& hi)
{
    static const float r[kNumTP][2] = {
        { 0.0f, 1.0f }, { 0.0f, 0.5f }, { 0.25f, 0.75f }, { 0.4f, 0.6f }, { 0.0f, 0.35f }, { 0.0f, 0.5f }, { 0.0f, 0.15f },
        { 0.20f, 0.65f }, { 0.20f, 0.92f }, { 0.20f, 0.90f }, { 0.20f, 0.70f }, { 0.30f, 1.0f }, { 0.0f, 0.6f }, { 0.0f, 0.6f },
        { 0.0f, 0.10f }, { 0.30f, 0.80f }, { 0.0f, 0.9f }, { 0.10f, 0.45f }, { 0.30f, 0.65f },
        { 0.0f, 0.6f }, { 0.76f, 0.82f }, { 0.45f, 0.55f }, { 0.0f, 0.35f }, { 0.0f, 0.35f } };
    lo = r[tp][0]; hi = r[tp][1];
}

} // namespace cantus
