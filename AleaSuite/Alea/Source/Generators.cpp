// Generateurs : de, mutations, patterns, generation "apprise", morph, export MIDI, presets d'usine.
#include "PluginProcessor.h"

using namespace alea;

// Graine melangee : juce::Random donne des premiers tirages proches de 0 avec une petite graine
static juce::Random makeRandom (int64_t seed)
{
    if (seed < 0) return juce::Random();
    const uint64_t hi = hash3 ((uint32_t) seed, (uint32_t) (seed >> 32), 0xA1EAu);
    const uint64_t lo = hash3 ((uint32_t) seed ^ 0x5bd1e995u, 0x1234567u, (uint32_t) (seed >> 32));
    juce::Random r ((juce::int64) ((hi << 32) | lo));
    for (int i = 0; i < 4; ++i) r.nextInt();
    return r;
}

// ---------------------------------------------------------------------------
// Sons
void AleaAudioProcessor::diceVoice (int t, bool includeEngine, int64_t seed)
{
    auto r = makeRandom (seed);
    int engine = (int) engineRaw[t]->load();
    if (includeEngine && ! locks[t][kNumVP])
    {
        engine = r.nextInt (kNumEngines - (sampleSlot[t].load() != nullptr ? 0 : 1));   // Sample seulement si charge
        setParamReal (tid (t, "engine"), (float) engine);
    }
    for (int vp = 0; vp < kNumVP; ++vp)
    {
        if (locks[t][vp]) continue;
        float lo, hi;
        diceRange (engine, vp, lo, hi);
        float v = lo + r.nextFloat() * (hi - lo);
        if (vp == Crush  && r.nextFloat() < 0.65f) v = 0.0f;
        if (vp == Attack && r.nextFloat() < 0.7f)  v = 0.0f;
        if (vp == Drive  && r.nextFloat() < 0.3f)  v = 0.0f;
        if (vp == FEnv   && r.nextFloat() < 0.4f)  v = 0.5f;
        if (vp == Reso   && r.nextFloat() < 0.4f)  v = 0.0f;
        if (vp == Punch  && r.nextFloat() < 0.4f)  v = 0.5f;
        setParamNorm (vpId (t, vp), v);
    }
}

void AleaAudioProcessor::mutateVoice (int t, float amount)
{
    juce::Random r;
    for (int vp = 0; vp < kNumVP; ++vp)
    {
        if (locks[t][vp] || vp == Level) continue;
        setParamNorm (vpId (t, vp), vpRaw[t][vp]->load() + (r.nextFloat() * 2.0f - 1.0f) * amount);
    }
}

void AleaAudioProcessor::diceAll()
{
    for (int t = 0; t < kNumTracks; ++t) diceVoice (t, false);
}

void AleaAudioProcessor::randomizeJitter (int t, int64_t seed)
{
    auto r = makeRandom (seed);
    for (int vp = 0; vp < kNumVP; ++vp)
    {
        if (locks[t][vp]) continue;
        const float v = r.nextFloat() < 0.5f ? 0.0f : r.nextFloat() * (vp == Tune ? 0.15f : 0.4f);
        setParamNorm (jitId (t, vp), v);
    }
}

void AleaAudioProcessor::clearJitter (int t)
{
    for (int vp = 0; vp < kNumVP; ++vp) setParamNorm (jitId (t, vp), 0.0f);
}

// ---------------------------------------------------------------------------
// Morph A/B
void AleaAudioProcessor::storeB()
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int vp = 0; vp < kNumVP; ++vp) snapB[t][vp] = vpRaw[t][vp]->load();
        snapBEngine[t] = (int) engineRaw[t]->load();
    }
    hasB = true;
}

void AleaAudioProcessor::recallB()
{
    if (! hasB) return;
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int vp = 0; vp < kNumVP; ++vp) setParamNorm (vpId (t, vp), snapB[t][vp].load());
        setParamReal (tid (t, "engine"), (float) snapBEngine[t].load());
    }
}

void AleaAudioProcessor::swapB()
{
    if (! hasB) { storeB(); return; }
    float a[kNumTracks][kNumVP]; int ae[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int vp = 0; vp < kNumVP; ++vp) a[t][vp] = vpRaw[t][vp]->load();
        ae[t] = (int) engineRaw[t]->load();
    }
    recallB();
    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int vp = 0; vp < kNumVP; ++vp) snapB[t][vp] = a[t][vp];
        snapBEngine[t] = ae[t];
    }
}

// ---------------------------------------------------------------------------
// Patterns
void AleaAudioProcessor::randomizeTrackPattern (int pat, int t, int64_t seed)
{
    auto r = makeRandom (seed);
    auto& tp = bank.pats[pat].tracks[t];
    const int len = tp.length;
    const int engine = (int) engineRaw[t]->load();
    const float density = 0.2f + r.nextFloat() * 0.35f;
    static constexpr int scale[] { 0, 0, 3, 5, 7, 10, 12, -5, -2 };

    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto& s = tp.steps[i];
        if (i >= len) { s.reset(); continue; }

        float w = (i % 4 == 0) ? 1.0f : (i % 2 == 0 ? 0.6f : 0.35f);
        switch (engine)
        {
            case EngKick: case EngHybrid: w *= (i % 4 == 0) ? 1.7f : 0.5f; break;
            case EngSnare:
            case EngClap:  w *= (i % 8 == 4) ? 2.5f : 0.45f; break;
            case EngMetal: w = (i % 2 == 0) ? 0.9f : 0.75f; break;
            default: break;
        }
        const bool on = r.nextFloat() < juce::jmin (0.95f, density * w * 1.6f);
        const int acc = (on && r.nextFloat() < 0.2f) ? 1 : 0;
        const int prob = (on && r.nextFloat() < 0.2f) ? 40 + r.nextInt (50) : 100;
        const int rat = (on && (engine == EngMetal || engine == EngFM) && r.nextFloat() < 0.12f) ? 2 + r.nextInt (3) : 1;
        const bool melodic = engine == EngFM || engine == EngModal || engine == EngPluck || engine == EngZap;
        const int tune = (on && melodic && r.nextFloat() < 0.5f) ? scale[r.nextInt (9)] : 0;
        const int cond = (on && r.nextFloat() < 0.08f) ? 1 + r.nextInt (9) : 0;
        s.reset();
        s.set (on ? 1 : 0, acc, prob, rat, 0, tune, cond);
    }
}

void AleaAudioProcessor::euclidTrack (int pat, int t, int hits, int rotation)
{
    auto& tp = bank.pats[pat].tracks[t];
    const int n = tp.length;
    hits = juce::jlimit (0, n, hits);
    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto& s = tp.steps[i];
        if (i >= n) { s.on = 0; continue; }
        const int j = ((i + rotation) % n + n) % n;
        s.on = (int8_t) ((hits > 0 && (j * hits) % n < hits) ? 1 : 0);
    }
}

void AleaAudioProcessor::mutateTrackPattern (int pat, int t, float amount)
{
    juce::Random r;
    auto& tp = bank.pats[pat].tracks[t];
    for (int i = 0; i < tp.length; ++i)
    {
        auto& s = tp.steps[i];
        if (r.nextFloat() < amount) s.on = (int8_t) (s.on ? 0 : 1);
        if (s.on && r.nextFloat() < amount * 0.5f) s.acc = (int8_t) (s.acc ? 0 : 1);
    }
}

// Variation "apprise" : un modele de Markov (position metrique x pas precedent) est
// estime sur la piste elle-meme, puis une partie des pas est re-tiree selon ce modele.
// Le resultat garde le "style" du pattern (densite, placement, accents, notes utilisees).
void AleaAudioProcessor::varyTrack (int pat, int t, float amount, int64_t seed)
{
    auto r = makeRandom (seed);
    auto& tp = bank.pats[pat].tracks[t];
    const int len = tp.length;
    if (len < 2) return;

    float cnt[4][2][2] {};    // [position mod 4][precedent on][on]
    int onCount = 0, accCount = 0;
    juce::Array<int> tunes;
    for (int i = 0; i < len; ++i)
    {
        const int prev = tp.steps[(i + len - 1) % len].on ? 1 : 0;
        const int on = tp.steps[i].on ? 1 : 0;
        cnt[i % 4][prev][on] += 1.0f;
        if (on) { ++onCount; if (tp.steps[i].acc) ++accCount; tunes.add (tp.steps[i].tune); }
    }
    if (onCount == 0) { randomizeTrackPattern (pat, t, seed); return; }
    const float accRatio = (float) accCount / (float) onCount;

    int prev = tp.steps[len - 1].on ? 1 : 0;
    for (int i = 0; i < len; ++i)
    {
        auto& s = tp.steps[i];
        if (r.nextFloat() < amount)
        {
            const float a = cnt[i % 4][prev][1] + 0.3f, b = cnt[i % 4][prev][0] + 0.3f;
            const bool on = r.nextFloat() < a / (a + b);
            if (on != (s.on != 0))
            {
                s.on = (int8_t) (on ? 1 : 0);
                if (on)
                {
                    s.acc = (int8_t) (r.nextFloat() < accRatio ? 1 : 0);
                    s.tune = (int8_t) tunes[r.nextInt (tunes.size())];
                    s.prob = 100; s.rat = 1; s.cond = 0;
                }
            }
        }
        prev = s.on ? 1 : 0;
    }
}

void AleaAudioProcessor::shiftTrack (int pat, int t, int dir)
{
    auto& tp = bank.pats[pat].tracks[t];
    const int n = tp.length;
    TrackClip tmp; tmp.grab (tp);
    for (int i = 0; i < n; ++i)
        tp.steps[i].fromString (tmp.steps[((i - dir) % n + n) % n]);
}

void AleaAudioProcessor::clearTrack (int pat, int t) { bank.pats[pat].tracks[t].clear(); }

// ---------------------------------------------------------------------------
// Export MIDI : rend le sequenceur (avec probas, conditions, chaos...) en fichier .mid
juce::File AleaAudioProcessor::exportMidi (int pat, int bars)
{
    constexpr int ppqTicks = 960;
    const double bpm = bpmRaw->load();
    auto ctx = makeContext (pat, 0.0, bpm);

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "Alea"), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / bpm)), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    CondState cs;
    ctx.evoMask = evoMask[pat];
    runSequencer (bank, ctx, &cs, 0.0, bars * 4.0, [&] (double te, SeqEvent e)
    {
        if (muteRaw[e.track]->load() > 0.5f) return;
        const double tick = juce::jmax (0.0, std::round (te * ppqTicks));
        const int vel = e.accent ? 127 : juce::jlimit (1, 127, (int) (e.vel * 110.0f));
        seq.addEvent (juce::MidiMessage::noteOn (1, 36 + e.track, (juce::uint8) vel), tick);
        seq.addEvent (juce::MidiMessage::noteOff (1, 36 + e.track), tick + ppqTicks / 8);
    });
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppqTicks);
    mf.addTrack (seq);

    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Alea").getChildFile ("MIDI");
    dir.createDirectory();
    auto f = dir.getChildFile ("Alea Pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + pat)) + ".mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) { mf.writeTo (os); os.flush(); }
    return f;
}

// ---------------------------------------------------------------------------
// Presets d'usine
void AleaAudioProcessor::resetToDefaults()
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        {
            if (wp->paramID == "play" || wp->paramID == "pattern") continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    bank.initDefault();
    for (int t = 0; t < kNumTracks; ++t) for (auto& l : locks[t]) l = false;
    hasB = false;
    for (auto& v : sceneValid) v = false;
    for (auto& row : favs) for (auto& f : row) f.valid = false;
    for (auto& row : evoMask) for (auto& m : row) m = 0u;
    for (auto& row : evoOff) for (auto& v : row) v = 0.0f;
    setChainText ("A4 B4");
}

juce::StringArray AleaAudioProcessor::factoryPresetNames()
{
    return { "Init Groove", "Minimal Drift", "Broken Glitch", "Industrial", "Tribal Modal", "Acid Perc", "Hybrid 909" };
}

void AleaAudioProcessor::loadFactoryPreset (int index)
{
    pushUndo();
    resetToDefaults();

    auto P = [this] (const char* id, float real) { setParamReal (id, real); };
    auto V = [this] (int t, int vp, float n) { setParamNorm (vpId (t, vp), n); };
    auto J = [this] (int t, int vp, float n) { setParamNorm (jitId (t, vp), n); };
    auto E = [this] (int t, int eng) { setParamReal (tid (t, "engine"), (float) eng); };
    auto MX = [this] (int slot, int src, int trk, int dst, float amt)
    {
        setParamReal (mxId (slot, "src"), (float) src); setParamReal (mxId (slot, "trk"), (float) trk);
        setParamReal (mxId (slot, "dst"), (float) dst); setParamReal (mxId (slot, "amt"), amt);
    };
    auto& A = bank.pats[0];
    auto clearA = [&A] { for (auto& tp : A.tracks) { tp.clear(); tp.length = 16; tp.rate = 1; tp.dir = 0; } };
    auto hit = [&A] (int t, int i, int acc = 0, int prob = 100, int rat = 1, int tune = 0, int cond = 0)
    { A.tracks[t].steps[i].set (1, acc, prob, rat, 0, tune, cond); };
    bool variations = true;

    switch (index)
    {
        case 1:   // Minimal Drift
            P ("bpm", 122); P ("rndmode", 2); P ("chaos", 0.6f); P ("swing", 0.18f); P ("scale", 5);
            P ("dlyrnd", 0.25f); P ("dlyret", 0.5f); P ("revsize", 0.75f);
            V (0, Decay, 0.6f); V (0, Punch, 0.7f);
            for (int t : { 3, 4 }) { J (t, Decay, 0.3f); J (t, Color, 0.2f); }
            J (6, Timbre, 0.4f); J (6, Tune, 0.1f); J (7, Decay, 0.25f);
            clearA();
            euclidTrack (0, 0, 4, 0);
            hit (2, 4, 0, 60); hit (2, 12, 1);
            euclidTrack (0, 3, 11, 0);
            euclidTrack (0, 4, 3, 2);
            euclidTrack (0, 5, 3, 5);
            A.tracks[6].length = 12; euclidTrack (0, 6, 5, 0);
            A.tracks[7].length = 20; euclidTrack (0, 7, 3, 0);
            MX (0, 2, 4, Color, 0.4f); MX (1, 1, 7, Tune, 0.3f); MX (2, 3, 8, Decay, 0.3f);
            break;

        case 2:   // Broken Glitch
            P ("bpm", 132); P ("seqchaos", 0.35f); P ("ratchaos", 0.4f); P ("stuchance", 0.22f); P ("studiv", 4);
            P ("rnddist", 2); P ("chaos", 0.7f);
            E (5, EngZap); E (6, EngFM); E (7, EngPluck);
            V (2, Crush, 0.4f); V (6, Crush, 0.5f);
            for (int t = 0; t < kNumTracks; ++t) { randomizeJitter (t, 100 + t); randomizeTrackPattern (0, t, 7 + t); }
            MX (0, 4, 0, Pan, 0.6f); MX (1, 1, 6, Timbre, 0.5f);
            break;

        case 3:   // Industrial
            P ("bpm", 128); P ("revsize", 0.9f); P ("revret", 0.7f); P ("glue", 0.45f); P ("mdrive", 0.25f); P ("master", 0.7f);
            V (0, Drive, 0.7f); V (0, Decay, 0.75f); V (0, Punch, 0.85f);
            V (1, Drive, 0.6f); V (1, Crush, 0.3f); V (1, SendRev, 0.4f);
            V (3, Crush, 0.35f); V (3, Drive, 0.5f);
            E (5, EngModal); V (5, Timbre, 0.6f);
            E (6, EngZap); E (7, EngMetal); V (7, Tune, 0.3f); V (7, Decay, 0.7f);
            clearA();
            for (int i : { 0, 3, 8, 11 }) hit (0, i, i == 0);
            hit (1, 4, 1); hit (1, 12, 1);
            for (int t : { 2, 3, 4, 5, 6, 7 }) randomizeTrackPattern (0, t, 3 + t);
            MX (0, 3, 4, Cutoff, -0.4f); MX (1, 4, 8, Tune, 0.5f);
            break;

        case 4:   // Tribal Modal
            P ("bpm", 110); P ("scale", 5); P ("humanize", 0.3f); P ("swing", 0.3f);
            E (0, EngModal); V (0, Tune, 0.35f); V (0, Decay, 0.6f); V (0, Timbre, 0.0f);
            E (1, EngModal); V (1, Tune, 0.45f); V (1, Timbre, 0.2f);
            E (3, EngPluck); V (3, Tune, 0.6f); V (3, Timbre, 0.3f); V (3, Decay, 0.4f);
            E (6, EngPluck); V (6, Tune, 0.7f);
            for (int t : { 3, 5, 6, 7 }) J (t, Tune, 0.25f);
            clearA();
            A.tracks[0].length = 8;  euclidTrack (0, 0, 3, 0);
            A.tracks[1].length = 12; euclidTrack (0, 1, 5, 0);
            euclidTrack (0, 2, 2, 4);
            euclidTrack (0, 3, 7, 0);
            euclidTrack (0, 5, 5, 3);
            A.tracks[6].length = 12; euclidTrack (0, 6, 7, 1);
            A.tracks[7].length = 20; euclidTrack (0, 7, 3, 0);
            MX (0, 1, 0, Decay, 0.3f);
            break;

        case 5:   // Acid Perc
            P ("bpm", 126); P ("accent", 0.9f); P ("scale", 3);
            E (5, EngFM); E (6, EngFM); E (7, EngZap);
            V (6, Reso, 0.7f); V (6, Cutoff, 0.55f); V (6, FEnv, 0.8f);
            for (int t : { 5, 6, 7 }) randomizeTrackPattern (0, t, 50 + t);
            MX (0, 2, 7, Cutoff, 0.5f); MX (1, 1, 6, Timbre, 0.5f); MX (2, 3, 8, Tune, 0.4f);
            break;

        case 6:   // Hybrid 909
            P ("bpm", 130); P ("swing", 0.1f);
            E (0, EngHybrid); V (0, Tune, 0.5f); V (0, Decay, 0.65f); V (0, PitchAmt, 0.55f); V (0, PitchDecay, 0.45f); V (0, Timbre, 0.3f); V (0, Color, 0.0f);
            E (1, EngHybrid); V (1, Tune, 0.62f); V (1, Decay, 0.45f); V (1, PitchAmt, 0.3f); V (1, Timbre, 0.2f); V (1, Color, 0.8f);
            E (5, EngHybrid); V (5, Tune, 0.58f); V (5, Decay, 0.5f); V (5, PitchAmt, 0.4f); V (5, Color, 0.1f);
            E (6, EngMetal); V (6, Decay, 0.8f); V (6, Color, 0.3f); V (6, Level, 0.7f);
            E (7, EngSnare); V (7, Decay, 0.25f); V (7, Timbre, 0.2f); V (7, Tune, 0.7f);
            clearA();
            for (int i : { 0, 4, 8, 12 }) hit (0, i);
            hit (1, 4, 1); hit (1, 12, 1); hit (1, 7, 0, 40);
            hit (2, 12);
            for (int i = 0; i < 16; ++i) if (i % 4 != 2) hit (3, i, 0, i % 2 ? 70 : 100);
            for (int i : { 2, 6, 10, 14 }) hit (4, i, 1);
            hit (5, 13, 0, 100, 1, 0, 10); hit (5, 14, 0, 100, 1, -3, 10); hit (5, 15, 0, 100, 2, -7, 10);
            for (int i = 0; i < 16; i += 2) hit (6, i, 0, 85);
            hit (7, 3, 0, 50); hit (7, 11, 0, 60);
            break;

        default:  // Init Groove
            variations = false;
            break;
    }

    // vitrines des fonctions v3
    switch (index)
    {
        case 1: P ("evoamt", 0.35f); P ("evobars", 2); P ("evotarget", 2); break;                                   // Minimal Drift : evolution
        case 3: P ("duckamt", 0.55f); P ("revtype", 1); setParamReal (tid (3, "fxtype"), 4); V (3, FxAmt, 0.6f);     // Industrial : sidechain + tape
                setParamReal (tid (6, "fxtype"), 1); V (6, FxAmt, 0.5f); break;
        case 4: setParamReal (tid (5, "fxtype"), 2); V (5, FxAmt, 0.55f); P ("revtype", 1); break;                  // Tribal : comb
        case 5: P ("keystrack", 7); P ("glide", 0.35f); setParamReal (tid (7, "fxtype"), 3); V (7, FxAmt, 0.4f);    // Acid : clavier + slides + ring
                for (int i : { 2, 5, 9, 13 }) A.tracks[6].steps[i].slide = 1;
                MX (3, 5, 0, kNumVP + GDlyFb, 0.5f); P (modId (4, "rate").toRawUTF8(), 5); break;
        case 6: P ("duckamt", 0.35f); P ("revtype", 1); P ("revsize", 0.45f); break;                                // Hybrid 909
        default: break;
    }

    if (variations)
        for (int p = 1; p < kNumPatterns; ++p)
        {
            bank.pats[p].copyFrom (A);
            for (int t = 0; t < kNumTracks; ++t) varyTrack (p, t, 0.08f * (float) p, 1000 * p + t);
        }
    // scenes 1-4 : le son du preset puis trois variations
    storeScene (0);
    for (int n = 1; n < 4; ++n)
    {
        for (int t = 0; t < kNumTracks; ++t)
        {
            sceneEng[n][t] = (int) engineRaw[t]->load();
            auto r = makeRandom (500 * index + 10 * n + t);
            for (int vp = 0; vp < kNumVP; ++vp)
                sceneVal[n][t][vp] = juce::jlimit (0.0f, 1.0f, vpRaw[t][vp]->load()
                                                   + (vp == Level ? 0.0f : (r.nextFloat() * 2.0f - 1.0f) * 0.12f * (float) n));
        }
        sceneValid[n] = true;
    }
    currentPresetName = factoryPresetNames()[juce::jlimit (0, factoryPresetNames().size() - 1, index)];
}

// ---------------------------------------------------------------------------
// Keep, favoris, croisement
void AleaAudioProcessor::keepLastHit (int t)
{
    for (int vp = 0; vp < kNumVP; ++vp)
    {
        const float v = lastNorm[t][vp].load();
        if (v >= 0.0f) setParamNorm (vpId (t, vp), v);
        evoOff[t][vp] = 0.0f;
    }
}

void AleaAudioProcessor::storeFav (int t, int slot)
{
    auto& f = favs[t][juce::jlimit (0, kNumFavs - 1, slot)];
    for (int vp = 0; vp < kNumVP; ++vp) f.v[vp] = vpRaw[t][vp]->load();
    f.engine = (int) engineRaw[t]->load();
    f.valid = true;
}

void AleaAudioProcessor::recallFav (int t, int slot)
{
    const auto& f = favs[t][juce::jlimit (0, kNumFavs - 1, slot)];
    if (! f.valid) return;
    setParamReal (tid (t, "engine"), (float) f.engine);
    for (int vp = 0; vp < kNumVP; ++vp) setParamNorm (vpId (t, vp), f.v[vp]);
    guiTrig[t] = true;   // pre-ecoute
}

void AleaAudioProcessor::breedVoice (int t, int slotA, int slotB, int64_t seed)
{
    const auto& a = favs[t][juce::jlimit (0, kNumFavs - 1, slotA)];
    const auto& b = favs[t][juce::jlimit (0, kNumFavs - 1, slotB)];
    if (! a.valid || ! b.valid) return;
    auto r = makeRandom (seed);
    setParamReal (tid (t, "engine"), (float) (r.nextBool() ? a.engine : b.engine));
    for (int vp = 0; vp < kNumVP; ++vp)
    {
        if (locks[t][vp]) continue;
        // gene choisi chez un parent, tire vers l'autre, plus une petite mutation
        const bool fromA = r.nextBool();
        const float p = fromA ? a.v[vp] : b.v[vp], q = fromA ? b.v[vp] : a.v[vp];
        float v = p + (q - p) * r.nextFloat() * 0.35f;
        if (r.nextFloat() < 0.15f) v += (r.nextFloat() * 2.0f - 1.0f) * 0.08f;
        setParamNorm (vpId (t, vp), v);
    }
    guiTrig[t] = true;
}

void AleaAudioProcessor::breedPatterns (int patA, int patB, int dst, int64_t seed)
{
    auto r = makeRandom (seed);
    Pattern tmp;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& A = bank.pats[patA].tracks[t];
        const auto& B = bank.pats[patB].tracks[t];
        auto& C = tmp.tracks[t];
        C.copyFrom (r.nextBool() ? A : B);
        const int len = C.length;
        const int cut = 1 + r.nextInt (juce::jmax (1, len - 1));
        const bool aFirst = r.nextBool();
        for (int i = 0; i < len; ++i)
        {
            const auto& src = ((i < cut) == aFirst) ? A : B;
            C.steps[i].copyFrom (src.steps[i]);
            if (r.nextFloat() < 0.04f) C.steps[i].on = (int8_t) (C.steps[i].on ? 0 : 1);
        }
    }
    bank.pats[dst].copyFrom (tmp);
}

// ---------------------------------------------------------------------------
// Scenes
void AleaAudioProcessor::storeScene (int n)
{
    n = juce::jlimit (0, kNumScenes - 1, n);
    for (int t = 0; t < kNumTracks; ++t)
    {
        sceneEng[n][t] = (int) engineRaw[t]->load();
        for (int vp = 0; vp < kNumVP; ++vp) sceneVal[n][t][vp] = vpRaw[t][vp]->load();
    }
    sceneValid[n] = true;
}

void AleaAudioProcessor::recallScene (int n)
{
    n = juce::jlimit (0, kNumScenes - 1, n);
    if (! sceneValid[n].load()) return;
    for (int t = 0; t < kNumTracks; ++t)
    {
        setParamReal (tid (t, "engine"), (float) sceneEng[n][t].load());
        for (int vp = 0; vp < kNumVP; ++vp) setParamNorm (vpId (t, vp), sceneVal[n][t][vp].load());
    }
    lastRecalledScene = n;
}

// ---------------------------------------------------------------------------
// Evolution
void AleaAudioProcessor::resetEvolution()
{
    for (auto& row : evoMask) for (auto& m : row) m = 0u;
    for (auto& row : evoOff) for (auto& v : row) v = 0.0f;
}

void AleaAudioProcessor::commitEvolution()
{
    for (int p = 0; p < kNumPatterns; ++p)
        for (int t = 0; t < kNumTracks; ++t)
        {
            const uint32_t m = evoMask[p][t].exchange (0u);
            for (int i = 0; i < kMaxSteps; ++i)
                if ((m >> i) & 1u)
                {
                    auto& s = bank.pats[p].tracks[t].steps[i];
                    s.on = (int8_t) (s.on ? 0 : 1);
                }
        }
    for (int t = 0; t < kNumTracks; ++t)
        for (int vp = 0; vp < kNumVP; ++vp)
        {
            const float o = evoOff[t][vp].exchange (0.0f);
            if (o != 0.0f) setParamNorm (vpId (t, vp), vpRaw[t][vp]->load() + o);
        }
}

int AleaAudioProcessor::evolutionAmount() const
{
    int n = 0;
    for (auto& row : evoMask) for (auto& m : row) { uint32_t v = m.load(); while (v) { n += (int) (v & 1u); v >>= 1; } }
    for (auto& row : evoOff) for (auto& v : row) if (std::abs (v.load()) > 0.01f) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Style appris depuis un fichier MIDI (Markov par position dans la mesure)
static int gmToTrack (int note, bool aleaMap)
{
    if (aleaMap && note >= 36 && note < 36 + kNumTracks) return note - 36;
    switch (note)
    {
        case 35: case 36: return 0;
        case 38: case 40: return 1;
        case 39: return 2;
        case 42: case 44: return 3;
        case 46: return 4;
        case 41: case 43: case 45: case 47: case 48: case 50: return 5;
        case 37: case 49: case 51: case 52: case 53: case 55: case 57: case 59: return 7;
        default: return 6;
    }
}

bool AleaAudioProcessor::learnStyleFromMidi (const juce::File& f)
{
    juce::MidiFile mf;
    juce::FileInputStream in (f);
    if (! in.openedOk() || ! mf.readFrom (in)) return false;
    const int tpq = mf.getTimeFormat();
    if (tpq <= 0) return false;

    struct Hit { int track; int64_t pos16; int vel; };
    std::vector<Hit> hits;
    bool aleaMap = false;   // fichier exporte par Alea (nom de piste "Alea") : notes 36-43 = voix 1-8, sinon General MIDI
    for (int tr = 0; tr < mf.getNumTracks(); ++tr)
        for (const auto* ev : *mf.getTrack (tr))
        {
            if (ev->message.isTrackNameEvent() && ev->message.getTextFromTextMetaEvent() == "Alea") aleaMap = true;
            if (ev->message.isNoteOn())
                hits.push_back ({ ev->message.getNoteNumber(), (int64_t) std::llround (ev->message.getTimeStamp() / (tpq / 4.0)), ev->message.getVelocity() });
        }
    if (hits.empty()) return false;

    int64_t maxPos = 0;
    for (auto& h : hits) { h.track = juce::jlimit (0, kNumTracks - 1, gmToTrack (h.track, aleaMap)); maxPos = juce::jmax (maxPos, h.pos16); }
    const int bars = (int) (maxPos / 16) + 1;
    std::vector<uint8_t> grid ((size_t) (kNumTracks * bars * 16), 0);
    for (auto& h : hits)
    {
        auto& g = grid[(size_t) (h.track * bars * 16 + h.pos16)];
        g = (uint8_t) juce::jmax ((int) g, h.vel >= 110 ? 2 : 1);
    }

    StyleModel m;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const uint8_t* g = &grid[(size_t) (t * bars * 16)];
        float accCount[16] {}, onCount[16] {};
        for (int i = 0; i < bars * 16; ++i)
        {
            const int prev = i > 0 ? (g[i - 1] > 0) : (g[bars * 16 - 1] > 0);
            const int cur = g[i] > 0;
            m.on[t][i % 16][prev][cur] += 1.0f;
            if (cur) { onCount[i % 16] += 1.0f; m.hits[t]++; if (g[i] == 2) accCount[i % 16] += 1.0f; }
        }
        for (int i = 0; i < 16; ++i) m.acc[t][i] = onCount[i] > 0 ? accCount[i] / onCount[i] : 0.0f;
    }
    m.valid = true;
    m.bars = bars;
    m.name = f.getFileNameWithoutExtension();
    style = m;
    return true;
}

void AleaAudioProcessor::generateFromStyle (int pat, int t, int64_t seed)
{
    if (! style.valid || style.hits[t] == 0) return;
    auto r = makeRandom (seed);
    auto& tp = bank.pats[pat].tracks[t];
    tp.length = 16; tp.rate = 1;
    int prev = 0;
    for (int i = 0; i < kMaxSteps; ++i)
    {
        auto& s = tp.steps[i];
        if (i >= 16) { s.reset(); continue; }
        const float a = style.on[t][i][prev][1] + 0.15f, b = style.on[t][i][prev][0] + 0.15f;
        const bool on = r.nextFloat() < a / (a + b);
        s.reset();
        s.set (on ? 1 : 0, (on && r.nextFloat() < style.acc[t][i]) ? 1 : 0, 100, 1, 0, 0);
        prev = on ? 1 : 0;
    }
}

// ---------------------------------------------------------------------------
// Apercu de la mesure a venir (pour l'interface)
AleaAudioProcessor::Preview AleaAudioProcessor::previewNextBar() const
{
    Preview pv;
    const bool run = isRunning.load();
    const double from = run ? playPpq.load() : 0.0;
    auto ctx = makeContext (currentPattern.load(), run ? patStartShared.load() : 0.0, bpmRaw->load());
    ctx.evoMask = evoMask[ctx.pattern];
    CondState cs;
    const auto& pat = bank.pats[ctx.pattern];
    runSequencer (bank, ctx, &cs, from, from + 4.0, [&] (double, SeqEvent e)
    {
        if (e.step < 0) return;
        (e.ghost ? pv.ghost : pv.play)[e.track] |= 1u << e.step;
    });
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tp = pat.tracks[t];
        const uint32_t evo = evoMask[ctx.pattern][t].load();
        for (int i = 0; i < tp.length; ++i)
        {
            const bool on = (tp.steps[i].on != 0) != (((evo >> i) & 1u) != 0);
            if (on && ! ((pv.play[t] >> i) & 1u)) pv.skip[t] |= 1u << i;
        }
    }
    return pv;
}
