#include "Params.h"

namespace fragor
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using API = juce::AudioParameterInt;

juce::StringArray tableNames()   { return { "BASIC", "GROWL", "YOI", "METAL", "SCREECH", "FOLD", "HOLLOW", "DIGITAL", "PULSE", "REESE", "TALK", "CHAINSAW", "USER A", "USER B" }; }
juce::StringArray fxModuleNames(){ return { "DIST 1", "DIST 2", "CRUSH", "OTT", "CHORUS", "DELAY", "REVERB" }; }
juce::StringArray warpNames()    { return { "OFF", "BEND +", "BEND -", "SYNC", "PWM", "MIRROR", "QUANTIZE", "FM <OSC", "FM <SUB", "RING" }; }
juce::StringArray filterNames()  { return { "LP 24", "LP 12", "BAND", "HIGH", "NOTCH", "VOWEL", "COMB +", "COMB -" }; }
juce::StringArray distNames()    { return { "TUBE", "HARD CLIP", "FOLDBACK", "SINE FOLD", "RECTIFY", "DIODE", "OVERDRIVE" }; }
juce::StringArray lfoShapeNames(){ return { "SINE", "TRIANGLE", "SAW DOWN", "SAW UP", "SQUARE", "WUB", "DECAY", "S&H", "DRIFT", "CUSTOM" }; }

static const char* kDivNames[] = { "4 BARS", "2 BARS", "1/1", "1/2", "1/2T", "1/4D", "1/4", "1/4T",
                                   "1/8D", "1/8", "1/8T", "1/16D", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
static const double kDivBeats[] = { 16.0, 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
                                    0.75, 0.5, 1.0 / 3.0, 0.375, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
constexpr int kNumDivs = (int) (sizeof (kDivBeats) / sizeof (double));

juce::StringArray divisionNames()
{
    juce::StringArray s;
    for (int i = 0; i < kNumDivs; ++i) s.add (kDivNames[i]);
    return s;
}

double divisionBeats (int i) { return kDivBeats[juce::jlimit (0, kNumDivs - 1, i)]; }

// Pas du séquenceur de vitesse du LFO 1 (le cœur des wobbles rythmés)
static const char*  kSeqNames[] = { "--", "OPEN", "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
static const double kSeqBeats[] = { -1.0, -2.0, 4.0, 2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625 };
constexpr int kNumSeq = (int) (sizeof (kSeqBeats) / sizeof (double));

juce::StringArray seqStepNames()
{
    juce::StringArray s;
    for (int i = 0; i < kNumSeq; ++i) s.add (kSeqNames[i]);
    return s;
}
double seqStepBeats (int i) { return kSeqBeats[juce::jlimit (0, kNumSeq - 1, i)]; }

juce::StringArray seqLenNames() { return { "1/2", "1/4", "1/8", "1/16" }; }
double seqLenBeats (int i)      { static const double b[] = { 2.0, 1.0, 0.5, 0.25 }; return b[juce::jlimit (0, 3, i)]; }

juce::StringArray modSourceNames()
{
    return { "--", "LFO 1", "LFO 2", "LFO 3", "ENV 2", "ENV 3", "VELOCITY", "MOD WHEEL",
             "NOTE", "RANDOM", "MACRO 1", "MACRO 2", "MACRO 3", "MACRO 4", "PITCH ENV", "AFTERTOUCH", "MPE SLIDE" };
}

juce::StringArray modDestNames()
{
    return { "--",
             "A WT POS", "A WARP", "A LEVEL", "A PITCH",
             "B WT POS", "B WARP", "B LEVEL", "B PITCH",
             "SUB LEVEL", "NOISE LEVEL", "UNI DETUNE",
             "CUTOFF", "RESO", "FLT MORPH", "FLT DRIVE", "FLT MIX",
             "AMP", "PAN",
             "LFO1 RATE", "LFO2 RATE",
             "DIST DRIVE", "DIST MIX", "CRUSH", "OTT DEPTH", "DELAY MIX", "REVERB MIX",
             "CUTOFF 2", "RESO 2", "FLT2 MORPH", "DIST2 DRIVE", "DIST2 MIX" };
}

// ---------------------------------------------------------------------------
namespace
{
    struct Builder
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        void f (const juce::String& id, const juce::String& name, float lo, float hi, float def, float skewCentre = -1.0f,
                const juce::String& unit = {})
        {
            juce::NormalisableRange<float> r (lo, hi);
            if (skewCentre > lo && skewCentre < hi) r.setSkewForCentre (skewCentre);
            params.push_back (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def,
                                                     juce::AudioParameterFloatAttributes().withLabel (unit)));
        }
        void c (const juce::String& id, const juce::String& name, const juce::StringArray& items, int def)
        {
            params.push_back (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, items, def));
        }
        void b (const juce::String& id, const juce::String& name, bool def)
        {
            params.push_back (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def));
        }
        void i (const juce::String& id, const juce::String& name, int lo, int hi, int def)
        {
            params.push_back (std::make_unique<API> (juce::ParameterID { id, 1 }, name, lo, hi, def));
        }
    };
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Builder B;

    // ---- Oscillateurs A / B ----
    for (int o = 0; o < 2; ++o)
    {
        const juce::String p = o == 0 ? "a" : "b";
        const juce::String n = o == 0 ? "Osc A " : "Osc B ";
        B.b (p + "On",        n + "On",            true);
        B.c (p + "Table",     n + "Table",         tableNames(), o == 0 ? 0 : 1);
        B.f (p + "Pos",       n + "WT Pos",        0.0f, 1.0f, 0.0f);
        B.c (p + "WarpMode",  n + "Warp Mode",     warpNames(), 0);
        B.f (p + "Warp",      n + "Warp",          0.0f, 1.0f, 0.0f);
        B.i (p + "Uni",       n + "Unison",        1, 8, 1);
        B.f (p + "Detune",    n + "Detune",        0.0f, 1.0f, 0.2f, 0.25f);
        B.f (p + "Blend",     n + "Uni Blend",     0.0f, 1.0f, 0.75f);
        B.f (p + "Spread",    n + "Stereo",        0.0f, 1.0f, 0.8f);
        B.i (p + "Oct",       n + "Octave",        -3, 3, 0);
        B.i (p + "Semi",      n + "Semi",          -12, 12, 0);
        B.f (p + "Fine",      n + "Fine",          -100.0f, 100.0f, 0.0f, -1.0f, "ct");
        B.f (p + "Level",     n + "Level",         0.0f, 1.0f, o == 0 ? 0.8f : 0.0f);
        B.f (p + "Pan",       n + "Pan",           -1.0f, 1.0f, 0.0f);
        B.b (p + "RandPhase", n + "Random Phase",  true);
    }

    // ---- Sub & bruit ----
    B.b ("subOn",     "Sub On",        true);
    B.c ("subShape",  "Sub Shape",     { "SINE", "TRIANGLE", "SQUARE" }, 0);
    B.c ("subOct",    "Sub Octave",    { "-2", "-1", "0" }, 1);
    B.f ("subLevel",  "Sub Level",     0.0f, 1.0f, 0.6f);
    B.b ("subDirect", "Sub Bypass Filter", true);
    B.f ("noiseLevel","Noise Level",   0.0f, 1.0f, 0.0f);
    B.f ("noiseColor","Noise Color",   0.0f, 1.0f, 0.5f);

    // ---- Filtre ----
    B.b ("fltOn",     "Filter On",     true);
    B.c ("fltType",   "Filter Type",   filterNames(), 0);
    B.f ("fltCutoff", "Cutoff",        20.0f, 20000.0f, 18000.0f, 1000.0f, "Hz");
    B.f ("fltReso",   "Resonance",     0.0f, 1.0f, 0.2f);
    B.f ("fltDrive",  "Filter Drive",  0.0f, 1.0f, 0.0f);
    B.f ("fltMorph",  "Vowel / Morph", 0.0f, 1.0f, 0.0f);
    B.f ("fltKey",    "Key Track",     0.0f, 1.0f, 0.0f);
    B.f ("fltMix",    "Filter Mix",    0.0f, 1.0f, 1.0f);
    B.f ("fltEnvAmt", "Env2 > Cutoff", -1.0f, 1.0f, 0.0f);

    B.b ("f2On",     "Filter 2 On",     false);
    B.c ("f2Type",   "Filter 2 Type",   filterNames(), (int) FltType::Vowel);
    B.f ("f2Cutoff", "Cutoff 2",        20.0f, 20000.0f, 1500.0f, 1000.0f, "Hz");
    B.f ("f2Reso",   "Resonance 2",     0.0f, 1.0f, 0.4f);
    B.f ("f2Drive",  "Filter 2 Drive",  0.0f, 1.0f, 0.0f);
    B.f ("f2Morph",  "Filter 2 Morph",  0.0f, 1.0f, 0.0f);
    B.f ("f2Key",    "Key Track 2",     0.0f, 1.0f, 0.0f);
    B.f ("f2Mix",    "Filter 2 Mix",    0.0f, 1.0f, 1.0f);
    B.f ("f2EnvAmt", "Env3 > Cutoff 2", -1.0f, 1.0f, 0.0f);
    B.c ("fltRouting", "Filter Routing", { "SERIAL", "PARALLEL" }, 0);

    // ---- Enveloppes ----
    auto env = [&B] (const juce::String& p, const juce::String& n, float a, float d, float s, float r)
    {
        B.f (p + "A", n + " Attack",  0.0f, 10.0f, a, 0.5f, "s");
        B.f (p + "D", n + " Decay",   0.001f, 10.0f, d, 0.6f, "s");
        B.f (p + "S", n + " Sustain", 0.0f, 1.0f, s);
        B.f (p + "R", n + " Release", 0.001f, 10.0f, r, 0.6f, "s");
    };
    env ("amp", "Amp", 0.001f, 0.5f, 1.0f, 0.08f);
    B.f ("ampVel", "Velocity Sens", 0.0f, 1.0f, 0.3f);
    env ("e2", "Env 2", 0.001f, 0.35f, 0.0f, 0.2f);
    env ("e3", "Env 3", 0.001f, 1.0f, 0.0f, 0.3f);

    B.f ("penvAmt",   "Pitch Env Amount", -48.0f, 48.0f, 0.0f, -1.0f, "st");
    B.f ("penvDecay", "Pitch Env Decay",  0.001f, 4.0f, 0.12f, 0.3f, "s");

    // ---- Voix ----
    B.c ("voiceMode", "Voice Mode", { "POLY", "MONO", "LEGATO" }, 1);
    B.f ("glide",     "Glide",       0.0f, 2.0f, 0.0f, 0.25f, "s");
    B.i ("bendRange", "Bend Range",  0, 24, 7);
    B.f ("masterTune","Master Tune", -100.0f, 100.0f, 0.0f, -1.0f, "ct");
    B.c ("quality",   "Oversampling", { "1x ECO", "2x HQ", "4x ULTRA" }, 1);
    B.b ("mpeOn",     "MPE",          false);
    B.i ("mpeBend",   "MPE Bend Range", 1, 96, 48);

    // ---- LFO ----
    for (int l = 1; l <= 3; ++l)
    {
        const juce::String p = "l" + juce::String (l);
        const juce::String n = "LFO " + juce::String (l) + " ";
        B.c (p + "Shape",  n + "Shape",   lfoShapeNames(), l == 1 ? (int) LfoShape::Wub : (int) LfoShape::Sine);
        B.b (p + "Sync",   n + "Sync",    true);
        B.c (p + "Div",    n + "Division",divisionNames(), 9);
        B.f (p + "Rate",   n + "Rate",    0.02f, 40.0f, 2.0f, 4.0f, "Hz");
        B.b (p + "Retrig", n + "Retrig",  true);
        B.f (p + "Phase",  n + "Phase",   0.0f, 1.0f, 0.0f);
        B.f (p + "Curve",  n + "Curve",   -1.0f, 1.0f, 0.0f);
        B.f (p + "Smooth", n + "Smooth",  0.0f, 1.0f, 0.05f);
    }

    // ---- Séquenceur de vitesse du LFO 1 ----
    B.b ("seqOn",    "Rate Seq On",     false);
    B.i ("seqSteps", "Rate Seq Steps",  1, kNumSeqSteps, 8);
    B.c ("seqLen",   "Rate Seq Step Length", seqLenNames(), 2);
    B.b ("seqReset", "Rate Seq Phase Reset", true);
    for (int s = 1; s <= kNumSeqSteps; ++s)
        B.c ("seq" + juce::String (s), "Rate Seq Step " + juce::String (s), seqStepNames(), 7);

    // ---- Matrice de modulation ----
    for (int m = 1; m <= kNumMatrixSlots; ++m)
    {
        const juce::String p = "m" + juce::String (m);
        B.c (p + "Src", "Mod " + juce::String (m) + " Source", modSourceNames(), 0);
        B.c (p + "Dst", "Mod " + juce::String (m) + " Dest",   modDestNames(), 0);
        B.f (p + "Amt", "Mod " + juce::String (m) + " Amount", -1.0f, 1.0f, 0.0f);
    }

    // ---- Macros ----
    for (int m = 1; m <= kNumMacros; ++m)
        B.f ("macro" + juce::String (m), "Macro " + juce::String (m), 0.0f, 1.0f, 0.0f);

    // ---- FX ----

    B.b ("distOn",    "Dist On",    true);
    B.c ("distMode",  "Dist Mode",  distNames(), 0);
    B.f ("distDrive", "Dist Drive", 0.0f, 1.0f, 0.25f);
    B.f ("distBias",  "Dist Bias",  -1.0f, 1.0f, 0.0f);
    B.f ("distTone",  "Dist Tone",  400.0f, 20000.0f, 16000.0f, 3000.0f, "Hz");
    B.f ("distMix",   "Dist Mix",   0.0f, 1.0f, 1.0f);

    B.b ("dist2On",    "Dist 2 On",    false);
    B.c ("dist2Mode",  "Dist 2 Mode",  distNames(), (int) DistMode::Foldback);
    B.f ("dist2Drive", "Dist 2 Drive", 0.0f, 1.0f, 0.3f);
    B.f ("dist2Bias",  "Dist 2 Bias",  -1.0f, 1.0f, 0.0f);
    B.f ("dist2Tone",  "Dist 2 Tone",  400.0f, 20000.0f, 12000.0f, 3000.0f, "Hz");
    B.f ("dist2Mix",   "Dist 2 Mix",   0.0f, 1.0f, 1.0f);

    B.b ("crushOn",   "Crush On",   false);
    B.f ("crushBits", "Crush Bits", 1.0f, 16.0f, 10.0f);
    B.f ("crushRate", "Crush Downsample", 0.0f, 1.0f, 0.0f);
    B.f ("crushMix",  "Crush Mix",  0.0f, 1.0f, 1.0f);

    B.b ("ottOn",     "OTT On",     true);
    B.f ("ottDepth",  "OTT Depth",  0.0f, 1.0f, 0.4f);
    B.f ("ottTime",   "OTT Time",   0.0f, 1.0f, 0.3f);
    B.f ("ottUp",     "OTT Upward", 0.0f, 1.0f, 1.0f);
    B.f ("ottDown",   "OTT Downward", 0.0f, 1.0f, 1.0f);
    B.f ("ottOut",    "OTT Output", -18.0f, 12.0f, 0.0f, -1.0f, "dB");

    B.b ("chorusOn",   "Chorus On",    false);
    B.f ("chorusRate", "Chorus Rate",  0.05f, 5.0f, 0.6f, 1.0f, "Hz");
    B.f ("chorusDepth","Chorus Depth", 0.0f, 1.0f, 0.3f);
    B.f ("chorusMix",  "Chorus Mix",   0.0f, 1.0f, 0.35f);

    B.b ("dlyOn",   "Delay On",       false);
    B.c ("dlyDiv",  "Delay Time",     divisionNames(), 10);
    B.f ("dlyFb",   "Delay Feedback", 0.0f, 0.95f, 0.35f);
    B.f ("dlyTone", "Delay Tone",     500.0f, 20000.0f, 6000.0f, 3000.0f, "Hz");
    B.b ("dlyPing", "Delay Ping-Pong",true);
    B.f ("dlyMix",  "Delay Mix",      0.0f, 1.0f, 0.2f);

    B.b ("revOn",   "Reverb On",    false);
    B.f ("revSize", "Reverb Size",  0.0f, 1.0f, 0.6f);
    B.f ("revDamp", "Reverb Damp",  0.0f, 1.0f, 0.5f);
    B.f ("revWidth","Reverb Width", 0.0f, 1.0f, 1.0f);
    B.f ("revMix",  "Reverb Mix",   0.0f, 1.0f, 0.15f);

    B.b ("subMono",    "Mono Bass",       true);
    B.f ("subMonoFreq","Mono Bass Freq",  40.0f, 400.0f, 140.0f, 120.0f, "Hz");
    B.f ("width",      "Stereo Width",    0.0f, 2.0f, 1.0f);
    B.f ("clip",       "Master Clipper",  0.0f, 1.0f, 0.3f);
    B.f ("masterGain", "Master Gain",     -36.0f, 12.0f, -6.0f, -1.0f, "dB");

    // ---- MIDI out / snapshots
    B.c ("midiOut",     "MIDI Out", { "OFF", "LFO1 > CC", "STEP NOTES", "CC + NOTES" }, 0);
    B.i ("midiOutCC",   "MIDI Out CC",   1, 119, 74);
    B.i ("midiOutNote", "MIDI Out Note", 0, 127, 36);
    B.f ("snapTime",    "Snapshot Morph", 0.0f, 4.0f, 0.3f, 0.5f, "s");

    return { B.params.begin(), B.params.end() };
}
} // namespace fragor
