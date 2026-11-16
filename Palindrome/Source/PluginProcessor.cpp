#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

static juce::String u8 (const char* t) { return juce::String::fromUTF8 (t); }

juce::StringArray PalindromeProcessor::noteNames()
{
    return { "Do", "Do#", u8 ("Ré"), "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si" };
}

static juce::String secondsText (float v, int) { return juce::String (v, 1) + " s"; }
static juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
static juce::String dbText (float v, int)      { return (v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1)) + " dB"; }
static juce::String msText (float v, int)      { return juce::String (v, 1) + " ms"; }

static juce::String hzText (float v, int)      { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
static juce::String dbPerRepText (float v, int) { return juce::String (v, 1) + " dB"; }

static juce::String beatsText (float v, int)
{
    const int b = juce::roundToInt (v);
    juce::String s (b);
    s << (b > 1 ? " temps" : " temps");
    if (b >= 4 && b % 4 == 0) s << " (" << (b / 4) << " mes.)";
    return s;
}

juce::AudioProcessorValueTreeState::ParameterLayout PalindromeProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto floatParam = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                           String (*text) (float, int))
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                        AudioParameterFloatAttributes().withStringFromValueFunction (text)));
    };
    auto percent = [&] (const String& id, const String& name, float def)
    {
        floatParam (id, name, { 0.0f, 100.0f, 0.1f }, def, percentText);
    };
    auto choice = [&] (const String& id, const String& name, const StringArray& items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };
    auto boolean = [&] (const String& id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto integer = [&] (const String& id, const String& name, int lo, int hi, int def, String (*text) (int, int))
    {
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def,
                        AudioParameterIntAttributes().withStringFromValueFunction (text)));
    };
    auto signedText = [] (int v, int) { return (v > 0 ? "+" : "") + juce::String (v); };

    const StringArray orders { u8 ("Alterné (suite continue)"), u8 ("Alterné, commence reverse"), u8 ("Alterné, commence normal"),
                               "Reverse uniquement", "Normal uniquement", u8 ("Sens au hasard"), u8 ("Superposé (reverse + normal)"),
                               u8 ("Séquence tapée (R N X -)") };

    //== Capture / cycle ======================================================
    choice (ParamIDs::captureMode, "Capture", { "Continue", "Au seuil", "Manuelle", "Cycle", "Banque" }, 3);
    floatParam (ParamIDs::threshold, "Seuil", { -60.0f, 0.0f, 0.1f }, -36.0f, dbText);

    NormalisableRange<float> lenRange { 1.0f, (float) PhraseEngine::maxPhraseSeconds, 0.1f };
    lenRange.setSkewForCentre (6.0f);
    floatParam (ParamIDs::phraseLen, "Longueur phrase", lenRange, 6.0f, secondsText);

    NormalisableRange<float> delayRange { (float) PhraseEngine::minDelaySeconds, (float) PhraseEngine::maxDelaySeconds, 0.1f };
    delayRange.setSkewForCentre (12.0f);
    floatParam (ParamIDs::delay, u8 ("Délai"), delayRange, 20.0f, secondsText);
    choice (ParamIDs::delayFrom, u8 ("Délai compté depuis"), { u8 ("Début de la capture"), "Fin de la capture" }, 0);

    integer (ParamIDs::repetitions, u8 ("Répétitions"), 1, PhraseEngine::maxRepetitions, 4, [] (int v, int) { return juce::String (v) + "x"; });
    floatParam (ParamIDs::repeatGap, u8 ("Écart répétitions"), { 0.0f, (float) PhraseEngine::maxRepeatGapSeconds, 0.1f }, 0.0f, secondsText);

    NormalisableRange<float> silRange { 0.0f, (float) PhraseEngine::maxSilenceSeconds, 0.1f };
    silRange.setSkewForCentre (20.0f);
    floatParam (ParamIDs::silenceMin, "Silence min", silRange, 5.0f, secondsText);
    floatParam (ParamIDs::silenceMax, "Silence max", silRange, 15.0f, secondsText);
    choice (ParamIDs::recapture, "Recapture", { u8 ("Immédiate"), "Au seuil" }, 0);

    integer (ParamIDs::bankSize, "Captures en banque", 2, PhraseEngine::maxBankSize, 4, [] (int v, int) { return juce::String (v); });
    choice (ParamIDs::bankOrder, "Ordre banque", { u8 ("Séquence"), "Hasard" }, 0);
    choice (ParamIDs::targetSlot, u8 ("Emplacement à recapturer"), { "1", "2", "3", "4", "5", "6", "7", "8" }, 0);

    //== Tempo =================================================================
    choice (ParamIDs::syncMode, u8 ("Unité de temps"), { "Secondes", "Tempo (temps)" }, 0);
    choice (ParamIDs::captureGrid, u8 ("Départ des captures"), { "Libre", "Sur le temps", "Sur la mesure" }, 0);
    floatParam (ParamIDs::phraseBeats, "Longueur (temps)", { 1.0f, 64.0f, 1.0f }, 8.0f, beatsText);
    floatParam (ParamIDs::delayBeats, u8 ("Délai (temps)"), { 1.0f, 128.0f, 1.0f }, 8.0f, beatsText);
    floatParam (ParamIDs::gapBeats, u8 ("Écart (temps)"), { 0.0f, 32.0f, 1.0f }, 0.0f, beatsText);
    floatParam (ParamIDs::silMinBeats, "Silence min (temps)", { 0.0f, 256.0f, 1.0f }, 8.0f, beatsText);
    floatParam (ParamIDs::silMaxBeats, "Silence max (temps)", { 0.0f, 256.0f, 1.0f }, 16.0f, beatsText);
    choice (ParamIDs::tempoSource, "Source du tempo", { "Projet", "Interne (tap)" }, 0);
    floatParam (ParamIDs::internalBpm, "Tempo interne", { 40.0f, 240.0f, 0.1f }, 120.0f,
                [] (float v, int) { return juce::String (v, 1) + " BPM"; });

    //== Contrôle ==============================================================
    boolean (ParamIDs::trigger, u8 ("Déclencher capture"), false);
    boolean (ParamIDs::clearTrig, "Vider", false);
    boolean (ParamIDs::nextStep, "Pas suivant", false);
    boolean (ParamIDs::hold, "Hold", false);

    //== Lecture & hasard ======================================================
    choice (ParamIDs::order, "Sens", orders, 1);
    percent (ParamIDs::randomness,  u8 ("Désordre des notes"), 30.0f);
    percent (ParamIDs::noteReverse, u8 ("Notes inversées"), 0.0f);
    percent (ParamIDs::skipProb,    u8 ("Notes muettes"), 0.0f);
    percent (ParamIDs::repeatProb,  u8 ("Notes répétées"), 0.0f);
    percent (ParamIDs::octaveProb,  u8 ("Sauts d'octave"), 0.0f);
    percent (ParamIDs::melodic,     u8 ("Hasard mélodique"), 0.0f);
    boolean (ParamIDs::freeze, u8 ("Hasard figé"), false);
    percent (ParamIDs::sensitivity, u8 ("Sensibilité détection"), 50.0f);
    percent (ParamIDs::legato, u8 ("Détection legato"), 50.0f);
    choice (ParamIDs::playMode, u8 ("Jeu"), { "Auto", "Notes seules", "Accords" }, 0);
    floatParam (ParamIDs::fade, "Fondu", { 1.0f, 40.0f, 0.1f }, 6.0f, msText);
    choice (ParamIDs::quantGrid, "Grille rythmique", { "Off", "1/4", "1/8", "1/8 triolet", "1/16", "1/16 triolet", "1/32" }, 0);
    percent (ParamIDs::quantAmount, "Quantification", 100.0f);
    floatParam (ParamIDs::humanize, "Flou rythmique", { 0.0f, 120.0f, 0.1f }, 0.0f, msText);

    //== Tonalité ==============================================================
    choice (ParamIDs::keyRoot, "Tonique", noteNames(), 0);
    choice (ParamIDs::scale, "Gamme", pal::scaleNames(), 0);
    percent (ParamIDs::correction, "Correction", 100.0f);
    integer (ParamIDs::transpose, "Transposition", -14, 14, 0, signedText);
    integer (ParamIDs::drift, u8 ("Dérive par répétition"), -7, 7, 0, signedText);

    //== Écho ===================================================================
    floatParam (ParamIDs::decay, u8 ("Décroissance"), { -24.0f, 0.0f, 0.1f }, -3.0f, dbPerRepText);
    percent (ParamIDs::damping, "Assombrissement", 30.0f);
    NormalisableRange<float> toneRange { 500.0f, 20000.0f, 1.0f };
    toneRange.setSkewForCentre (4000.0f);
    floatParam (ParamIDs::tone, "Couleur", toneRange, 16000.0f, hzText);
    NormalisableRange<float> cutRange { 20.0f, 2000.0f, 1.0f };
    cutRange.setSkewForCentre (200.0f);
    floatParam (ParamIDs::lowCut, "Coupe-bas", cutRange, 40.0f, hzText);
    choice (ParamIDs::speedMode, "Vitesse de bande", { "Normale", "Demi-vitesse", "Double vitesse", "Alternance normale / demi",
                                                       u8 ("Ralentit à chaque répétition"), u8 ("Accélère à chaque répétition"), "Au hasard" }, 0);
    percent (ParamIDs::modDepth, "Modulation", 0.0f);
    NormalisableRange<float> rateRange { 0.05f, 8.0f, 0.01f };
    rateRange.setSkewForCentre (1.0f);
    floatParam (ParamIDs::modRate, "Vitesse", rateRange, 0.6f, [] (float v, int) { return juce::String (v, 2) + " Hz"; });
    percent (ParamIDs::drive, "Saturation", 0.0f);
    floatParam (ParamIDs::feedback, u8 ("Réinjection"), { 0.0f, 95.0f, 0.1f }, 0.0f, percentText);

    //== Espace =================================================================
    choice (ParamIDs::stereoMode, u8 ("Stéréo"), { "Centre", u8 ("Reverse à gauche / normal à droite"), u8 ("Normal à gauche / reverse à droite"),
                                                   "Ping-pong", "Au hasard" }, 0);
    percent (ParamIDs::width, "Ouverture", 100.0f);
    percent (ParamIDs::diffusion, "Diffusion", 0.0f);
    boolean (ParamIDs::diffusionGrow, "Diffusion croissante", true);
    percent (ParamIDs::roomSize, "Taille", 70.0f);
    floatParam (ParamIDs::duckAmount, "Ducking", { -36.0f, 0.0f, 0.1f }, 0.0f, dbPerRepText);
    NormalisableRange<float> relRange { 50.0f, 3000.0f, 1.0f };
    relRange.setSkewForCentre (400.0f);
    floatParam (ParamIDs::duckRelease, u8 ("Retour ducking"), relRange, 400.0f, [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; });

    //== Violoning ===============================================================
    NormalisableRange<float> atkRange { 0.0f, 2000.0f, 1.0f };
    atkRange.setSkewForCentre (250.0f);
    floatParam (ParamIDs::attack, "Attaque", atkRange, 0.0f,
                [] (float v, int) { return v < 1.0f ? juce::String ("off") : juce::String (juce::roundToInt (v)) + " ms"; });
    choice (ParamIDs::attackScope, u8 ("Violoning : portée"), { "Chaque note", u8 ("Chaque répétition") }, 0);
    choice (ParamIDs::attackDir, u8 ("Violoning : lectures"), { "Normales et reverse", "Normales seulement", "Reverse seulement" }, 0);
    choice (ParamIDs::attackPattern, u8 ("Violoning : lesquelles"), { "Toutes", "Une sur deux", "Une sur trois", "Une sur quatre", "Au hasard" }, 0);
    percent (ParamIDs::attackProb, u8 ("Violoning : probabilité"), 50.0f);

    //== Texture =================================================================
    percent (ParamIDs::textureMix, "Texture", 0.0f);
    NormalisableRange<float> grainRange { 10.0f, 500.0f, 1.0f };
    grainRange.setSkewForCentre (100.0f);
    floatParam (ParamIDs::grainSize, "Taille des grains", grainRange, 100.0f, [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; });
    NormalisableRange<float> densRange { 1.0f, 80.0f, 0.1f };
    densRange.setSkewForCentre (15.0f);
    floatParam (ParamIDs::grainDensity, u8 ("Densité"), densRange, 20.0f, [] (float v, int) { return juce::String (v, 1) + " /s"; });
    percent (ParamIDs::grainSpray, "Dispersion", 30.0f);

    //== Sortie ==================================================================
    percent (ParamIDs::mix, "Mix", 50.0f);
    floatParam (ParamIDs::wetGain, "Niveau relecture", { -60.0f, 12.0f, 0.1f }, 0.0f, dbText);
    boolean (ParamIDs::killDry, u8 ("Relecture seule (départ aux.)"), false);

    //== Réglages par emplacement de banque ======================================
    StringArray slotOrders { "Global" };
    slotOrders.addArray (orders);
    for (int k = 1; k <= PhraseEngine::maxBankSize; ++k)
    {
        choice (ParamIDs::slotOrder (k), "Capture " + String (k) + " : sens", slotOrders, 0);
        integer (ParamIDs::slotTranspose (k), "Capture " + String (k) + " : transposition", -14, 14, 0, signedText);
        integer (ParamIDs::slotReps (k), u8 ("Capture ") + String (k) + u8 (" : répétitions"), 0, PhraseEngine::maxRepetitions, 0,
                 [] (int v, int) { return v == 0 ? juce::String ("global") : juce::String (v) + "x"; });
    }

    return layout;
}

PalindromeProcessor::PalindromeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PALINDROME", createLayout())
{
    apvts.state.setProperty (ParamIDs::sequenceProp, "1 2 3 4", nullptr);
    apvts.state.setProperty (ParamIDs::dirSequenceProp, "R N N R", nullptr);
    engine.setSequence ("1 2 3 4");
    engine.setDirectionSequence ("R N N R");
    bankCacheVersion.fill (-1);

    for (auto& m : ccMap) m.store (noMap);
    for (auto& m : noteMap) m.store (noMap);
    ccHigh.fill (false);

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            const bool momentary = id == ParamIDs::trigger || id == ParamIDs::clearTrig || id == ParamIDs::nextStep;
            if (! momentary) mappable.push_back (rp);
            if (! momentary && id != ParamIDs::hold) snapshotParams.push_back (rp);
        }

    // assignations MIDI par défaut (celles du pédalier, mémorisées pour toutes les sessions)
    if (auto xml = juce::parseXML (globalSettings().getValue ("midiMap")))
        readMidiMap (xml.get());

    startTimerHz (30);
}

PalindromeProcessor::~PalindromeProcessor()
{
    stopTimer();
}

bool PalindromeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void PalindromeProcessor::queuePendingBank()
{
    const juce::ScopedLock sl (pendingLock);
    if (! engine.isPrepared()) return;

    for (auto& p : pendingBank)
        engine.queueImport (p.slot, p.audio, p.rate);
    pendingBank.clear();
}

void PalindromeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // préparer à nouveau le moteur vide la banque : on la garde de côté
    if (engine.isPrepared())
    {
        const juce::ScopedLock sl (pendingLock);
        for (int k = 0; k < PhraseEngine::maxBankSize; ++k)
        {
            juce::AudioBuffer<float> b;
            if (engine.copyBankAudio (k, b))
                pendingBank.push_back ({ k, std::move (b), engine.getSampleRate() });
        }
    }

    const int maxBlock = juce::jmax (512, samplesPerBlock);
    wetBuffer.setSize (2, maxBlock, false, true, false);
    sendBuffer.setSize (2, maxBlock, false, true, false);
    inputMix.setSize (2, maxBlock, false, true, false);
    prevWet.setSize (2, maxBlock, false, true, false);
    prevWet.clear();
    prevWetLen = 0;

    engine.prepare (sampleRate, samplesPerBlock);
    wetFX.prepare (sampleRate);
    reverb.setSampleRate (sampleRate);
    reverb.reset();

    dryGain.reset (sampleRate, 0.05);
    wetGainSmoothed.reset (sampleRate, 0.05);
    duckEnv = 0.0f;
    lastTrigger = lastClear = lastNext = false;
    clearStage = 0;
    clearGain = 1.0f;

    queuePendingBank();
}

void PalindromeProcessor::releaseResources()
{
    engine.release();
}

void PalindromeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    handleMidi (midi);
    midi.clear();

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;

    //== tempo de l'hôte ==
    double bpm = 120.0, ppq = 0.0, barStartPpq = 0.0, beatsPerBar = 4.0;
    bool playing = false, havePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; havePpq = true; }
            if (auto bs = pos->getPpqPositionOfLastBarStart()) barStartPpq = *bs;
            if (auto ts = pos->getTimeSignature())
                if (ts->denominator > 0) beatsPerBar = ts->numerator * 4.0 / ts->denominator;
        }
    hostBpm.store (bpm);
    hostPlaying.store (playing);

    // tempo : projet (avec sa position quand il joue), sinon horloge interne
    const bool internalTempo = param (ParamIDs::tempoSource) > 0.5f;
    if (internalTempo) bpm = juce::jlimit (20.0, 400.0, (double) param (ParamIDs::internalBpm));
    const bool hostGrid = ! internalTempo && playing && havePpq;
    if (tapReset.exchange (false)) internalPpq = 0.0;
    if (hostGrid)
        internalPpq = ppq;
    else
    {
        ppq = internalPpq;
        barStartPpq = std::floor (internalPpq / beatsPerBar) * beatsPerBar;
        havePpq = true;
        playing = true;
    }
    currentBpm.store (bpm);
    currentPpq.store (ppq);
    currentBeatsPerBar.store (beatsPerBar);
    tempoFromHost.store (hostGrid);

    const double beatSec = 60.0 / bpm;
    const bool sync = param (ParamIDs::syncMode) > 0.5f;

    PhraseEngine::Settings s;
    s.captureMode   = (int) param (ParamIDs::captureMode);
    s.thresholdGain = juce::Decibels::decibelsToGain (param (ParamIDs::threshold), -100.0f);
    s.phraseSeconds = sync ? param (ParamIDs::phraseBeats) * beatSec : param (ParamIDs::phraseLen);
    s.delaySeconds  = sync ? param (ParamIDs::delayBeats) * beatSec  : param (ParamIDs::delay);
    s.delayFromEnd  = param (ParamIDs::delayFrom) > 0.5f;
    s.order         = (int) param (ParamIDs::order);
    s.repetitions   = juce::roundToInt (param (ParamIDs::repetitions));
    s.repeatGap     = sync ? param (ParamIDs::gapBeats) * beatSec    : param (ParamIDs::repeatGap);
    s.silenceMin    = sync ? param (ParamIDs::silMinBeats) * beatSec : param (ParamIDs::silenceMin);
    s.silenceMax    = sync ? param (ParamIDs::silMaxBeats) * beatSec : param (ParamIDs::silenceMax);
    s.recapture     = (int) param (ParamIDs::recapture);
    s.bankSize      = juce::roundToInt (param (ParamIDs::bankSize));
    s.bankOrder     = (int) param (ParamIDs::bankOrder);
    s.targetSlot    = (int) param (ParamIDs::targetSlot);
    s.hold          = param (ParamIDs::hold) > 0.5f;
    s.beatSamples   = beatSec * sr;

    // grille de départ des captures (seulement si l'hôte joue et donne sa position)
    const int gridMode = (int) param (ParamIDs::captureGrid);
    double samplesToNextGrid = 0.0;
    if (sync && gridMode > 0 && playing && havePpq)
    {
        const double gridBeats = gridMode == 2 ? beatsPerBar : 1.0;
        const double ref = gridMode == 2 ? barStartPpq : 0.0;
        const double since = std::fmod (ppq - ref, gridBeats);
        const double toNext = since < 1.0e-6 ? 0.0 : gridBeats - (since < 0 ? since + gridBeats : since);
        s.gridSamples = gridBeats * beatSec * sr;
        samplesToNextGrid = toNext * beatSec * sr;
    }

    s.randomAmount    = param (ParamIDs::randomness) * 0.01f;
    s.noteReverseProb = param (ParamIDs::noteReverse) * 0.01f;
    s.skipProb        = param (ParamIDs::skipProb) * 0.01f;
    s.repeatProb      = param (ParamIDs::repeatProb) * 0.01f;
    s.octaveProb      = param (ParamIDs::octaveProb) * 0.01f;
    s.melodicJitter   = param (ParamIDs::melodic) * 0.01f;
    s.freezeRandom    = param (ParamIDs::freeze) > 0.5f;
    s.sensitivity     = param (ParamIDs::sensitivity) * 0.01f;
    s.legato          = param (ParamIDs::legato) * 0.01f;
    s.playMode        = (int) param (ParamIDs::playMode);
    s.fadeMs          = param (ParamIDs::fade);

    static const float grids[] = { 0.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
    s.quantGridBeats  = grids[juce::jlimit (0, 6, (int) param (ParamIDs::quantGrid))];
    s.quantAmount     = param (ParamIDs::quantAmount) * 0.01f;
    s.humanizeMs      = param (ParamIDs::humanize);

    s.keyRoot       = (int) param (ParamIDs::keyRoot);
    s.scale         = (int) param (ParamIDs::scale);
    s.correction    = param (ParamIDs::correction) * 0.01f;
    s.transpose     = juce::roundToInt (param (ParamIDs::transpose));
    s.driftPerRep   = juce::roundToInt (param (ParamIDs::drift));

    s.decayDb       = param (ParamIDs::decay);
    s.damping       = param (ParamIDs::damping) * 0.01f;
    s.toneHz        = param (ParamIDs::tone);
    s.lowCutHz      = param (ParamIDs::lowCut);
    s.speedMode     = (int) param (ParamIDs::speedMode);

    s.attackMs        = param (ParamIDs::attack);
    s.attackPerNote   = param (ParamIDs::attackScope) < 0.5f;
    s.attackDirection = (int) param (ParamIDs::attackDir);
    s.attackPattern   = (int) param (ParamIDs::attackPattern);
    s.attackProb      = param (ParamIDs::attackProb) * 0.01f;

    s.textureMix    = param (ParamIDs::textureMix) * 0.01f;
    s.grainMs       = param (ParamIDs::grainSize);
    s.grainDensity  = param (ParamIDs::grainDensity);
    s.grainSpray    = param (ParamIDs::grainSpray) * 0.01f;

    s.stereoMode    = (int) param (ParamIDs::stereoMode);
    s.width         = param (ParamIDs::width) * 0.01f;
    s.diffusion     = param (ParamIDs::diffusion) * 0.01f;
    s.diffusionGrow = param (ParamIDs::diffusionGrow) > 0.5f;

    for (int k = 0; k < PhraseEngine::maxBankSize; ++k)
    {
        s.slotOrder[(size_t) k]     = (int) param (ParamIDs::slotOrder (k + 1)) - 1;
        s.slotTranspose[(size_t) k] = juce::roundToInt (param (ParamIDs::slotTranspose (k + 1)));
        s.slotReps[(size_t) k]      = juce::roundToInt (param (ParamIDs::slotReps (k + 1)));
    }

    const float modDepth = param (ParamIDs::modDepth) * 0.01f;
    const float modRate  = param (ParamIDs::modRate);
    const float drive    = param (ParamIDs::drive) * 0.01f;
    const float fb       = param (ParamIDs::feedback) * 0.01f;
    const float duckDb   = param (ParamIDs::duckAmount);
    const float duckRel  = (float) std::exp (-1.0 / (param (ParamIDs::duckRelease) * 0.001 * sr));
    const float duckAtk  = (float) std::exp (-1.0 / (0.005 * sr));

    juce::Reverb::Parameters rp;
    rp.roomSize = 0.3f + 0.69f * param (ParamIDs::roomSize) * 0.01f;
    rp.damping = 0.45f; rp.wetLevel = 0.6f; rp.dryLevel = 0.0f; rp.width = 1.0f;
    reverb.setParameters (rp);

    //== déclencheurs (mappables en MIDI dans le DAW) ==
    auto edge = [] (bool v, bool& last) { const bool e = v && ! last; last = v; return e; };
    if (edge (param (ParamIDs::trigger) > 0.5f, lastTrigger)) engine.requestCapture();
    if (edge (param (ParamIDs::clearTrig) > 0.5f, lastClear)) requestClear();
    if (clearRequested.exchange (false) && clearStage == 0) clearStage = 1;
    if (edge (param (ParamIDs::nextStep) > 0.5f, lastNext))   engine.requestNext();

    const float mix = param (ParamIDs::mix) * 0.01f;
    const bool auxMode = param (ParamIDs::killDry) > 0.5f;   // relecture seule, à plein niveau
    dryGain.setTargetValue (auxMode ? 0.0f : juce::jmin (1.0f, 2.0f * (1.0f - mix)));
    wetGainSmoothed.setTargetValue ((auxMode ? 1.0f : juce::jmin (1.0f, 2.0f * mix))
                                    * juce::Decibels::decibelsToGain (param (ParamIDs::wetGain), -60.0f));
    const float clearStep = (float) (1.0 / (0.03 * sr));   // fondu de 30 ms

    const int total = buffer.getNumSamples();
    const int inCh = juce::jmax (0, juce::jmin (numIn, buffer.getNumChannels()));

    for (int start = 0; start < total; )
    {
        const int n = juce::jmin (wetBuffer.getNumSamples(), total - start);

        // entrée du moteur = signal direct + réinjection de la relecture (sound on sound)
        for (int ch = 0; ch < 2; ++ch)
        {
            if (inCh > 0) inputMix.copyFrom (ch, 0, buffer, juce::jmin (ch, inCh - 1), start, n);
            else          inputMix.clear (ch, 0, n);
            if (fb > 0.0f && prevWetLen > 0)
                inputMix.addFrom (ch, 0, prevWet, ch, 0, juce::jmin (n, prevWetLen), fb);
        }
        juce::AudioBuffer<float> engineIn (inputMix.getArrayOfWritePointers(), 2, n);

        PhraseEngine::Settings sc = s;
        if (s.gridSamples > 1.0)
        {
            double t = samplesToNextGrid - start;
            while (t < 0.0) t += s.gridSamples;
            sc.samplesToNextGrid = t;
        }

        const bool clearedThisChunk = clearStage == 2;
        engine.process (engineIn, 0, n, wetBuffer, sendBuffer, sc);
        if (clearedThisChunk)
        {
            // le moteur vient d'être vidé : on repart propre
            reverb.reset();
            wetFX.prepare (sr);
            prevWet.clear();
            clearStage = 0;
            clearGain = 1.0f;
        }
        wetFX.process (wetBuffer, n, modDepth, modRate, drive);

        // diffusion (la réverbération continue de sonner après que l'envoi retombe à zéro)
        {
            reverb.processStereo (sendBuffer.getWritePointer (0), sendBuffer.getWritePointer (1), n);
            for (int ch = 0; ch < 2; ++ch)
                wetBuffer.addFrom (ch, 0, sendBuffer, ch, 0, n);
        }

        // Vider : fondu de sortie, puis le moteur est vidé au bloc suivant
        if (clearStage == 1)
        {
            for (int i = 0; i < n; ++i)
            {
                clearGain = juce::jmax (0.0f, clearGain - clearStep);
                for (int ch = 0; ch < 2; ++ch) wetBuffer.setSample (ch, i, wetBuffer.getSample (ch, i) * clearGain);
            }
            if (clearGain <= 0.0f) { engine.clearAll(); clearStage = 2; }
        }
        else if (clearStage == 2)
        {
            wetBuffer.clear (0, n);
        }

        // mémoire pour la réinjection
        for (int ch = 0; ch < 2; ++ch) prevWet.copyFrom (ch, 0, wetBuffer, ch, 0, n);
        prevWetLen = n;

        for (int i = 0; i < n; ++i)
        {
            // ducking : la relecture s'efface quand on joue
            float duck = 1.0f;
            if (duckDb < -0.05f && inCh > 0)
            {
                float x = 0.0f;
                for (int ch = 0; ch < inCh; ++ch) x = juce::jmax (x, std::abs (buffer.getSample (ch, start + i)));
                const float c = x > duckEnv ? duckAtk : duckRel;
                duckEnv = x + c * (duckEnv - x);
                const float envDb = juce::Decibels::gainToDecibels (duckEnv, -100.0f);
                const float amount = juce::jlimit (0.0f, 1.0f, (envDb + 45.0f) / 30.0f);
                duck = juce::Decibels::decibelsToGain (duckDb * amount);
            }

            const float dg = dryGain.getNextValue();
            const float wg = wetGainSmoothed.getNextValue() * duck;
            for (int ch = 0; ch < numOut; ++ch)
            {
                float* d = buffer.getWritePointer (ch, start);
                const float w = numOut == 1 ? 0.5f * (wetBuffer.getSample (0, i) + wetBuffer.getSample (1, i))
                                            : wetBuffer.getSample (juce::jmin (ch, 1), i);
                d[i] = d[i] * dg + w * wg;
            }
        }
        start += n;
    }

    internalPpq += (double) total / (sr * beatSec);
}

juce::AudioProcessorEditor* PalindromeProcessor::createEditor()
{
    return new PalindromeEditor (*this);
}

//==============================================================================
int PalindromeProcessor::getNumPrograms()
{
    return (int) factoryPresets().size();
}

const juce::String PalindromeProcessor::getProgramName (int index)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? p[(size_t) index].name : juce::String();
}

void PalindromeProcessor::setCurrentProgram (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    currentProgram = index;

    // valeurs par défaut, sauf les réglages de banque et les déclencheurs
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (id.startsWith ("slot") || id == ParamIDs::trigger || id == ParamIDs::clearTrig
                || id == ParamIDs::nextStep || id == ParamIDs::hold || id == ParamIDs::killDry
                || id == ParamIDs::tempoSource || id == ParamIDs::internalBpm)
                continue;
            rp->setValueNotifyingHost (rp->getDefaultValue());
        }

    for (auto& [id, value] : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

juce::File PalindromeProcessor::getUserPresetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("Application Support").getChildFile ("Palindrome").getChildFile ("Presets");
#if ! JUCE_MAC
    dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Palindrome").getChildFile ("Presets");
#endif
    return dir;
}

juce::Array<juce::File> PalindromeProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.palindrome");
    files.sort();
    return files;
}

bool PalindromeProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;

    auto dir = getUserPresetFolder();
    dir.createDirectory();
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        return xml->writeTo (dir.getChildFile (clean + ".palindrome"));
    return false;
}

void PalindromeProcessor::applyParamValues (const juce::ValueTree& tree)
{
    for (int i = 0; i < tree.getNumChildren(); ++i)
    {
        const auto child = tree.getChild (i);
        if (! child.hasType ("PARAM")) continue;
        const auto id = child.getProperty ("id").toString();
        if (id == ParamIDs::trigger || id == ParamIDs::clearTrig || id == ParamIDs::nextStep) continue;
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) child.getProperty ("value")));
    }
}

void PalindromeProcessor::loadUserPreset (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        applyParamValues (tree);
        if (tree.hasProperty (ParamIDs::sequenceProp))
            setSequenceText (tree.getProperty (ParamIDs::sequenceProp).toString());
        if (tree.hasProperty (ParamIDs::dirSequenceProp))
            setDirSequenceText (tree.getProperty (ParamIDs::dirSequenceProp).toString());
    }
}

//==============================================================================
static juce::String encodeFlac (const juce::AudioBuffer<float>& buf, double rate)
{
    juce::FlacAudioFormat flac;
    juce::MemoryBlock block;
    {
        auto* out = new juce::MemoryOutputStream (block, false);
        std::unique_ptr<juce::AudioFormatWriter> writer (flac.createWriterFor (out, rate, (unsigned int) buf.getNumChannels(), 24, {}, 0));
        if (writer == nullptr) { delete out; return {}; }
        writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
    }
    return block.toBase64Encoding();
}

static bool decodeFlac (const juce::String& data, juce::AudioBuffer<float>& out, double& rate)
{
    juce::MemoryBlock block;
    if (! block.fromBase64Encoding (data)) return false;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (new juce::MemoryInputStream (block, true), true));
    if (reader == nullptr) return false;
    const int len = (int) juce::jmin ((juce::int64) (PhraseEngine::maxPhraseSeconds * reader->sampleRate * 1.01), reader->lengthInSamples);
    out.setSize ((int) reader->numChannels, len);
    reader->read (&out, 0, len, 0, true, true);
    rate = reader->sampleRate;
    return len > 0;
}

void PalindromeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    apvts.state.setProperty (ParamIDs::locksProp, engine.getLockMask(), nullptr);
    auto state = apvts.copyState();
    auto xml = state.createXml();
    if (xml == nullptr) return;

    writeMidiMap (*xml);

    // captures de la banque, encodées en FLAC (mises en cache tant qu'elles ne changent pas)
    auto* bankXml = xml->createNewChildElement ("BANK");
    for (int k = 0; k < PhraseEngine::maxBankSize; ++k)
    {
        const int version = engine.bankVersion (k);
        if (version != bankCacheVersion[(size_t) k])
        {
            juce::AudioBuffer<float> b;
            bankCache[(size_t) k] = engine.copyBankAudio (k, b) ? encodeFlac (b, engine.getSampleRate()) : juce::String();
            bankCacheVersion[(size_t) k] = version;
        }

        if (bankCache[(size_t) k].isNotEmpty())
        {
            auto* slotXml = bankXml->createNewChildElement ("SLOT");
            slotXml->setAttribute ("index", k);
            slotXml->setAttribute ("flac", bankCache[(size_t) k]);
        }
    }

    copyXmlToBinary (*xml, destData);
}

void PalindromeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    std::vector<PendingSlot> restored;
    if (auto* bankXml = xml->getChildByName ("BANK"))
    {
        for (auto* slotXml : bankXml->getChildIterator())
        {
            PendingSlot p;
            p.slot = slotXml->getIntAttribute ("index", -1);
            if (p.slot >= 0 && p.slot < PhraseEngine::maxBankSize
                && decodeFlac (slotXml->getStringAttribute ("flac"), p.audio, p.rate))
                restored.push_back (std::move (p));
        }
        xml->removeChildElement (bankXml, true);
    }

    if (auto* midiXml = xml->getChildByName ("MIDIMAP"))
    {
        readMidiMap (midiXml);
        xml->removeChildElement (midiXml, true);
    }

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    engine.setSequence (getSequenceText());
    engine.setDirectionSequence (getDirSequenceText());
    engine.setLockMask ((int) apvts.state.getProperty (ParamIDs::locksProp, 0));
    history.clear();
    historyIndex = -1;

    if (! restored.empty())
    {
        engine.clearAll();
        {
            const juce::ScopedLock sl (pendingLock);
            pendingBank = std::move (restored);
        }
        queuePendingBank();
    }
}

//==============================================================================
bool PalindromeProcessor::importFileToSlot (int slot, const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return false;

    const int len = (int) juce::jmin ((juce::int64) (PhraseEngine::maxPhraseSeconds * reader->sampleRate), reader->lengthInSamples);
    if (len < 16) return false;

    juce::AudioBuffer<float> buf ((int) juce::jmax (1u, reader->numChannels), len);
    reader->read (&buf, 0, len, 0, true, true);
    engine.queueImport (slot, buf, reader->sampleRate);
    return true;
}

juce::File PalindromeProcessor::exportSlotToFile (int slot)
{
    juce::AudioBuffer<float> b;
    if (! engine.copyBankAudio (slot, b)) return {};

    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                    .getChildFile ("Palindrome capture " + juce::String (slot + 1) + ".wav");
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) return {};
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), engine.getSampleRate(),
                                                                          (unsigned int) b.getNumChannels(), 24, {}, 0));
    if (writer == nullptr) return {};
    stream.release();
    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return file;
}

juce::String PalindromeProcessor::getSequenceText() const
{
    return apvts.state.getProperty (ParamIDs::sequenceProp, "1 2 3 4").toString();
}

void PalindromeProcessor::setSequenceText (const juce::String& text)
{
    apvts.state.setProperty (ParamIDs::sequenceProp, text, nullptr);
    engine.setSequence (text);
}

juce::String PalindromeProcessor::getDirSequenceText() const
{
    return apvts.state.getProperty (ParamIDs::dirSequenceProp, "R N N R").toString();
}

void PalindromeProcessor::setDirSequenceText (const juce::String& text)
{
    apvts.state.setProperty (ParamIDs::dirSequenceProp, text, nullptr);
    engine.setDirectionSequence (text);
}

void PalindromeProcessor::setSlotLocked (int k, bool locked)
{
    engine.setSlotLocked (k, locked);
    apvts.state.setProperty (ParamIDs::locksProp, engine.getLockMask(), nullptr);
}

//==============================================================================
// Tap tempo : moyenne des 4 derniers intervalles, remise à zéro après 2 s sans tap
void PalindromeProcessor::tapTempo()
{
    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;
    double bpm = 0.0;
    {
        const juce::SpinLock::ScopedLockType l (tapLock);
        if (tapCount > 0 && t - lastTapTime > 2.0) tapCount = 0;
        if (tapCount > 0)
        {
            for (size_t i = tapIntervals.size() - 1; i > 0; --i) tapIntervals[i] = tapIntervals[i - 1];
            tapIntervals[0] = t - lastTapTime;
            const int k = juce::jmin (tapCount, (int) tapIntervals.size());
            double sum = 0.0;
            for (int i = 0; i < k; ++i) sum += tapIntervals[(size_t) i];
            bpm = 60.0 / (sum / k);
        }
        lastTapTime = t;
        ++tapCount;
    }
    tapReset.store (true);                          // le temps 1 tombe sur ce tap
    if (bpm >= 30.0 && bpm <= 300.0) pendingTapBpm.store (bpm);
}

//==============================================================================
// MIDI learn
int PalindromeProcessor::codeForTarget (const juce::String& target) const
{
    static const char* actions[] = { MidiActions::capture, MidiActions::next, MidiActions::clear, MidiActions::tap, MidiActions::undo, MidiActions::abSwap };
    for (int a = 0; a < (int) std::size (actions); ++a)
        if (target == actions[a]) return actionBase + a;
    for (size_t i = 0; i < mappable.size(); ++i)
        if (mappable[i]->getParameterID() == target) return (int) i;
    return noMap;
}

juce::String PalindromeProcessor::targetForCode (int code) const
{
    static const char* actions[] = { MidiActions::capture, MidiActions::next, MidiActions::clear, MidiActions::tap, MidiActions::undo, MidiActions::abSwap };
    if (code >= actionBase && code - actionBase < (int) std::size (actions)) return actions[code - actionBase];
    if (juce::isPositiveAndBelow (code, (int) mappable.size())) return mappable[(size_t) code]->getParameterID();
    return {};
}

juce::String PalindromeProcessor::describeTarget (const PalindromeProcessor& p, const juce::String& target)
{
    if (target == MidiActions::capture) return "Capturer";
    if (target == MidiActions::next)    return "Pas suivant";
    if (target == MidiActions::clear)   return "Vider";
    if (target == MidiActions::tap)     return "Tap tempo";
    if (target == MidiActions::undo)    return "Annuler";
    if (target == MidiActions::abSwap)  return "A / B";
    if (auto* rp = p.apvts.getParameter (target)) return rp->getName (64);
    return target;
}

void PalindromeProcessor::startMidiLearn (const juce::String& target) { learnCode.store (codeForTarget (target)); }
void PalindromeProcessor::cancelMidiLearn()                          { learnCode.store (noMap); }
juce::String PalindromeProcessor::getLearningTarget() const           { return targetForCode (learnCode.load()); }

juce::String PalindromeProcessor::getMidiMappingText (const juce::String& target) const
{
    const int code = codeForTarget (target);
    if (code == noMap) return {};
    juce::StringArray parts;
    for (int i = 0; i < 128; ++i)
    {
        if (ccMap[(size_t) i].load() == code)   parts.add ("CC " + juce::String (i));
        if (noteMap[(size_t) i].load() == code) parts.add ("Note " + juce::MidiMessage::getMidiNoteName (i, true, true, 3));
    }
    return parts.joinIntoString (" + ");
}

juce::StringArray PalindromeProcessor::getMappedTargets() const
{
    juce::StringArray out;
    for (int i = 0; i < 128; ++i)
    {
        for (int c : { ccMap[(size_t) i].load(), noteMap[(size_t) i].load() })
            if (c != noMap) out.addIfNotAlreadyThere (targetForCode (c));
    }
    return out;
}

void PalindromeProcessor::forgetMidiMapping (const juce::String& target)
{
    const int code = codeForTarget (target);
    for (auto* arr : { &ccMap, &noteMap })
        for (auto& m : *arr)
            if (m.load() == code) m.store (noMap);
    saveMidiMapGlobally();
}

void PalindromeProcessor::forgetAllMidiMappings()
{
    for (auto& m : ccMap) m.store (noMap);
    for (auto& m : noteMap) m.store (noMap);
    saveMidiMapGlobally();
}

juce::XmlElement* PalindromeProcessor::writeMidiMap (juce::XmlElement& parent) const
{
    auto* xml = parent.createNewChildElement ("MIDIMAP");
    for (int i = 0; i < 128; ++i)
    {
        if (const int c = ccMap[(size_t) i].load(); c != noMap)
        {
            auto* e = xml->createNewChildElement ("MAP");
            e->setAttribute ("type", "cc"); e->setAttribute ("number", i); e->setAttribute ("target", targetForCode (c));
        }
        if (const int c = noteMap[(size_t) i].load(); c != noMap)
        {
            auto* e = xml->createNewChildElement ("MAP");
            e->setAttribute ("type", "note"); e->setAttribute ("number", i); e->setAttribute ("target", targetForCode (c));
        }
    }
    return xml;
}

void PalindromeProcessor::readMidiMap (const juce::XmlElement* xml)
{
    if (xml == nullptr) return;
    for (auto& m : ccMap) m.store (noMap);
    for (auto& m : noteMap) m.store (noMap);
    for (auto* e : xml->getChildWithTagNameIterator ("MAP"))
    {
        const int num = e->getIntAttribute ("number", -1);
        const int code = codeForTarget (e->getStringAttribute ("target"));
        if (! juce::isPositiveAndBelow (num, 128) || code == noMap) continue;
        (e->getStringAttribute ("type") == "note" ? noteMap : ccMap)[(size_t) num].store (code);
    }
}

void PalindromeProcessor::saveMidiMapGlobally()
{
    juce::XmlElement root ("ROOT");
    auto* xml = writeMidiMap (root);
    globalSettings().setValue ("midiMap", xml->toString());
    globalSettings().saveIfNeeded();
}

// Thread audio : CC et notes -> apprentissage, actions immédiates, ou réglages (via file d'attente)
void PalindromeProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const bool isCC = m.isController();
        const bool isNoteOn = m.isNoteOn();
        if (! isCC && ! isNoteOn) continue;

        midiActivity.fetch_add (1);
        const int number = isCC ? m.getControllerNumber() : m.getNoteNumber();
        const int value = isCC ? m.getControllerValue() : 127;

        if (const int learn = learnCode.load(); learn != noMap)
        {
            for (auto* arr : { &ccMap, &noteMap })
                for (auto& x : *arr)
                    if (x.load() == learn) x.store (noMap);
            (isCC ? ccMap : noteMap)[(size_t) number].store (learn);
            learnCode.store (noMap);
            learnVersion.fetch_add (1);
            continue;
        }

        const int code = (isCC ? ccMap : noteMap)[(size_t) number].load();
        if (code == noMap) continue;
        applyMidiValue (code, number, ! isCC, value);
    }
}

void PalindromeProcessor::applyMidiValue (int code, int number, bool isNote, int value)
{
    // appui = note, ou CC qui passe au-dessus de 64 (pédale)
    bool press = isNote;
    if (! isNote)
    {
        const bool high = value >= 64;
        press = high && ! ccHigh[(size_t) number];
        ccHigh[(size_t) number] = high;
    }

    if (code >= actionBase)
    {
        if (! press) return;
        switch (code - actionBase)
        {
            case 0: engine.requestCapture(); break;
            case 1: engine.requestNext(); break;
            case 2: requestClear(); break;
            case 3: tapTempo(); break;
            case 4: pendingUndo.fetch_add (1); break;
            case 5: pendingAB.fetch_add (1); break;
            default: break;
        }
        return;
    }

    if (! juce::isPositiveAndBelow (code, (int) mappable.size())) return;
    auto* rp = mappable[(size_t) code];
    float target = rp->getValue();

    const bool isBool  = dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr;
    const bool isChoice = dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr;

    if (isBool)
    {
        if (! press) return;
        target = rp->getValue() > 0.5f ? 0.0f : 1.0f;          // une pression = bascule
    }
    else if (isNote)
    {
        if (isChoice)
        {
            // une note fait défiler les choix
            auto* c = static_cast<juce::AudioParameterChoice*> (rp);
            const int nItems = c->choices.size();
            target = rp->convertTo0to1 ((float) ((c->getIndex() + 1) % juce::jmax (1, nItems)));
        }
        else target = rp->getValue() > 0.5f ? 0.0f : 1.0f;
    }
    else
    {
        target = (float) value / 127.0f;
    }

    const auto scope = midiFifo.write (1);
    if (scope.blockSize1 > 0)      midiChanges[(size_t) scope.startIndex1] = { code, target };
    else if (scope.blockSize2 > 0) midiChanges[(size_t) scope.startIndex2] = { code, target };
}

//==============================================================================
// Thread message : changements venus du MIDI, tap tempo, historique
void PalindromeProcessor::timerCallback()
{
    {
        const auto scope = midiFifo.read (midiFifo.getNumReady());
        auto apply = [this] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& c = midiChanges[(size_t) (start + i)];
                auto* rp = mappable[(size_t) c.index];
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (c.value);
                rp->endChangeGesture();
            }
        };
        apply (scope.startIndex1, scope.blockSize1);
        apply (scope.startIndex2, scope.blockSize2);
    }

    if (const double bpm = pendingTapBpm.exchange (0.0); bpm > 0.0)
    {
        if (auto* rp = apvts.getParameter (ParamIDs::internalBpm))
        {
            rp->beginChangeGesture(); rp->setValueNotifyingHost (rp->convertTo0to1 ((float) bpm)); rp->endChangeGesture();
        }
        if (auto* src = apvts.getParameter (ParamIDs::tempoSource); src != nullptr && src->getValue() < 0.5f)
        {
            src->beginChangeGesture(); src->setValueNotifyingHost (1.0f); src->endChangeGesture();
        }
    }

    for (int k = pendingUndo.exchange (0); k > 0; --k) undo();
    if (pendingAB.exchange (0) % 2 == 1) selectAB (1 - abCurrent);

    if (learnVersion.load() != lastLearnSeen) { lastLearnSeen = learnVersion.load(); saveMidiMapGlobally(); }

    if (++historyTick >= 5) { historyTick = 0; updateHistory(); }
}

//==============================================================================
// Annuler / rétablir, A/B
PalindromeProcessor::Snapshot PalindromeProcessor::captureSnapshot() const
{
    Snapshot s;
    s.values.reserve (snapshotParams.size());
    for (auto* p : snapshotParams) s.values.push_back (p->getValue());
    s.sequence = getSequenceText();
    s.dirSequence = getDirSequenceText();
    return s;
}

void PalindromeProcessor::applySnapshot (const Snapshot& snap)
{
    for (size_t i = 0; i < snapshotParams.size() && i < snap.values.size(); ++i)
    {
        auto* p = snapshotParams[i];
        if (std::abs (p->getValue() - snap.values[i]) < 1.0e-7f) continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost (snap.values[i]);
        p->endChangeGesture();
    }
    if (snap.sequence != getSequenceText())       setSequenceText (snap.sequence);
    if (snap.dirSequence != getDirSequenceText()) setDirSequenceText (snap.dirSequence);
    lastSeen = captureSnapshot();
    stableTicks = 0;
}

void PalindromeProcessor::updateHistory()
{
    const auto cur = captureSnapshot();
    if (history.empty()) { history.push_back (cur); historyIndex = 0; lastSeen = cur; return; }
    if (cur == history[(size_t) historyIndex]) { lastSeen = cur; stableTicks = 0; return; }

    // on attend que le réglage soit stable (~0,3 s) avant de l'inscrire
    stableTicks = (cur == lastSeen) ? stableTicks + 1 : 0;
    lastSeen = cur;
    if (stableTicks < 2) return;

    history.resize ((size_t) historyIndex + 1);
    history.push_back (cur);
    if (history.size() > 100) history.erase (history.begin());
    historyIndex = (int) history.size() - 1;
    stableTicks = 0;
}

bool PalindromeProcessor::canUndo() const
{
    return historyIndex > 0 || (historyIndex == 0 && captureSnapshot() != history[0]);
}

bool PalindromeProcessor::canRedo() const
{
    return historyIndex >= 0 && historyIndex + 1 < (int) history.size();
}

void PalindromeProcessor::undo()
{
    if (history.empty()) return;
    const auto cur = captureSnapshot();
    if (cur != history[(size_t) historyIndex])
    {
        // changement pas encore inscrit : on l'inscrit pour pouvoir le rétablir
        history.resize ((size_t) historyIndex + 1);
        history.push_back (cur);
        historyIndex = (int) history.size() - 1;
    }
    if (historyIndex <= 0) return;
    --historyIndex;
    applySnapshot (history[(size_t) historyIndex]);
}

void PalindromeProcessor::redo()
{
    if (! canRedo()) return;
    ++historyIndex;
    applySnapshot (history[(size_t) historyIndex]);
}

void PalindromeProcessor::selectAB (int which)
{
    which = juce::jlimit (0, 1, which);
    abSlots[(size_t) abCurrent] = captureSnapshot();
    abValid[(size_t) abCurrent] = true;
    if (! abValid[(size_t) which])
    {
        abSlots[(size_t) which] = abSlots[(size_t) abCurrent];
        abValid[(size_t) which] = true;
    }
    abCurrent = which;
    applySnapshot (abSlots[(size_t) which]);
}

void PalindromeProcessor::copyToOtherAB()
{
    abSlots[(size_t) (1 - abCurrent)] = captureSnapshot();
    abValid[(size_t) (1 - abCurrent)] = true;
}

//==============================================================================
juce::PropertiesFile& PalindromeProcessor::globalSettings()
{
    if (settingsFile == nullptr)
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Palindrome";
        o.filenameSuffix = ".settings";
        o.folderName = "Palindrome";
        o.osxLibrarySubFolder = "Application Support";
        o.millisecondsBeforeSaving = 500;
        settingsFile = std::make_unique<juce::PropertiesFile> (o);
    }
    return *settingsFile;
}

juce::Point<int> PalindromeProcessor::getUiSize() const
{
    auto parse = [] (const juce::String& t) -> juce::Point<int>
    {
        const auto parts = juce::StringArray::fromTokens (t, " ", "");
        if (parts.size() != 2) return {};
        return { parts[0].getIntValue(), parts[1].getIntValue() };
    };
    auto sz = parse (apvts.state.getProperty (ParamIDs::uiSizeProp).toString());
    if (sz.x <= 0) sz = parse (const_cast<PalindromeProcessor*> (this)->globalSettings().getValue ("uiSize"));
    if (sz.x <= 0) sz = { 1024, 656 };   // 80 % : tient sur un écran de portable
    return sz;
}

void PalindromeProcessor::setUiSize (juce::Point<int> sz)
{
    const juce::String t = juce::String (sz.x) + " " + juce::String (sz.y);
    apvts.state.setProperty (ParamIDs::uiSizeProp, t, nullptr);
    globalSettings().setValue ("uiSize", t);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PalindromeProcessor();
}
