#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <array>
#include "Params.h"

namespace fragor
{
    // Point d'une courbe de LFO dessinée : x,y dans 0..1, c = courbure du segment suivant (-1..1)
    struct CurvePoint { float x, y, c; };
    using LfoCurve = std::vector<CurvePoint>;

    LfoCurve defaultCurve();
    void renderCurve (const LfoCurve& curve, float* table, int size);
    juce::String curveToString (const LfoCurve& c);
    LfoCurve curveFromString (const juce::String& s);

    // Formes prêtes à l'emploi pour l'éditeur de courbes
    juce::StringArray curvePresetNames();
    LfoCurve curvePreset (int index);

    // Données d'un patch qui ne sont pas des paramètres automatisables
    struct PatchExtras
    {
        std::array<LfoCurve, 3> curves;
        std::array<int, numFxModules> order;
        void reset();
    };
    std::array<int, numFxModules> defaultFxOrder();

    // Outil d'écriture de presets en unités réelles (Hz, s, index de choix...)
    class PatchBuilder
    {
    public:
        PatchBuilder (juce::AudioProcessorValueTreeState& s, juce::StringArray& macroNames, PatchExtras& ex)
            : state (s), macros (macroNames), extras (ex) {}

        PatchBuilder& set (const juce::String& id, float value);
        PatchBuilder& on  (const juce::String& id, bool v = true) { return set (id, v ? 1.0f : 0.0f); }
        PatchBuilder& mod (int slot, int src, int dst, float amt);      // slot 1..12
        PatchBuilder& nextMod (int src, int dst, float amt);            // premier slot libre
        PatchBuilder& seq (std::initializer_list<int> steps, int stepLen); // pas du séquenceur LFO1
        PatchBuilder& macroNames (const juce::String& a, const juce::String& b, const juce::String& c, const juce::String& d);
        PatchBuilder& curve (int lfo, const LfoCurve& c);               // lfo 1..3 (passe la forme en CUSTOM)
        PatchBuilder& fxOrder (std::initializer_list<int> order);
        void defaults();     // remet tous les paramètres à leur valeur par défaut

        juce::AudioProcessorValueTreeState& state;
        juce::StringArray& macros;
        PatchExtras& extras;
        int modSlot = 1;
    };

    struct FactoryPreset
    {
        juce::String category, name;
        std::function<void (PatchBuilder&)> build;
    };

    const std::vector<FactoryPreset>& factoryPresets();

    juce::StringArray generatorStyles();
    void generatePatch (PatchBuilder& b, int style, juce::Random& rng);
    void mutatePatch (juce::AudioProcessorValueTreeState& s, PatchExtras& ex, juce::Random& rng, float amount);
}
