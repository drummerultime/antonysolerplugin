#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace fragor
{
    // ----- Listes de choix ----------------------------------------------------
    enum class Warp     { Off, BendPlus, BendMinus, Sync, Pwm, Mirror, Quantize, FmOther, FmSub, Ring, Count };
    enum class FltType  { LP24, LP12, BP, HP, Notch, Vowel, CombPlus, CombMinus, Count };
    enum class DistMode { Tube, HardClip, Foldback, SineFold, Rectify, Diode, Overdrive, Count };
    enum class LfoShape { Sine, Triangle, SawDown, SawUp, Square, Wub, Decay, SampleHold, Drift, Custom, Count };
    enum class VoiceMode{ Poly, Mono, Legato };

    enum ModSource { srcNone, srcLfo1, srcLfo2, srcLfo3, srcEnv2, srcEnv3, srcVelocity, srcModWheel,
                     srcNote, srcRandom, srcMacro1, srcMacro2, srcMacro3, srcMacro4, srcPitchEnv,
                     srcAftertouch, srcSlide, numModSources };

    enum ModDest { dstNone,
                   dstAPos, dstAWarp, dstALevel, dstAPitch,
                   dstBPos, dstBWarp, dstBLevel, dstBPitch,
                   dstSubLevel, dstNoiseLevel, dstDetune,
                   dstCutoff, dstReso, dstMorph, dstFltDrive, dstFltMix,
                   dstAmp, dstPan,
                   dstLfo1Rate, dstLfo2Rate,
                   dstDistDrive, dstDistMix, dstCrush, dstOttDepth, dstDelayMix, dstReverbMix,
                   dstCutoff2, dstReso2, dstMorph2, dstDist2Drive, dstDist2Mix,
                   numModDests };

    // Destinations traitées au niveau global (FX), lues sur la voix la plus récente
    inline bool isGlobalDest (int d) { return (d >= dstDistDrive && d <= dstReverbMix) || d == dstDist2Drive || d == dstDist2Mix; }

    // Modules de la chaîne d'effets (ordre libre)
    enum FxModule { fxDist1, fxDist2, fxCrush, fxOtt, fxChorus, fxDelay, fxReverb, numFxModules };
    juce::StringArray fxModuleNames();

    constexpr int kNumMatrixSlots = 12;
    constexpr int kNumSeqSteps    = 16;
    constexpr int kNumMacros      = 4;
    constexpr int kNumSnapshots   = 8;
    constexpr int kNumUserTables  = 2;   // USER A / USER B

    juce::StringArray tableNames();
    juce::StringArray warpNames();
    juce::StringArray filterNames();
    juce::StringArray distNames();
    juce::StringArray lfoShapeNames();
    juce::StringArray divisionNames();     // divisions musicales LFO / delay
    double            divisionBeats (int index);
    juce::StringArray seqStepNames();      // "—", "OPEN", divisions
    double            seqStepBeats (int index); // <0 : -1 = fermé, -2 = ouvert
    juce::StringArray seqLenNames();
    double            seqLenBeats (int index);
    juce::StringArray modSourceNames();
    juce::StringArray modDestNames();

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Utilitaire : identifiant avec préfixe (oscillateurs "a"/"b", LFO "l1"...)
    inline juce::String pid (const juce::String& prefix, const char* name) { return prefix + name; }
}
