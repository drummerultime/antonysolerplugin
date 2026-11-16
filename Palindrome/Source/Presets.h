#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <utility>

// Presets d'usine : valeurs exprimées dans l'unité des paramètres (s, dB, %, index de liste...).
// Les paramètres absents reprennent leur valeur par défaut.
struct FactoryPreset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Init", {} },

        { juce::String::fromUTF8 ("Violon superposé"), {
            { "order", 6 }, { "stereoMode", 1 }, { "width", 100 }, { "repetitions", 6 },
            { "decay", -4 }, { "damping", 50 }, { "attack", 300 }, { "modDepth", 20 }, { "modRate", 0.5f },
            { "diffusion", 30 } } },

        { juce::String::fromUTF8 ("Écho qui monte"), {
            { "order", 4 }, { "scale", 1 }, { "drift", 1 }, { "repetitions", 8 }, { "decay", -2.5f },
            { "delayFrom", 1 }, { "delay", 2 }, { "stereoMode", 3 }, { "damping", 25 } } },

        { "Memory Man", {
            { "order", 4 }, { "repetitions", 10 }, { "decay", -3 }, { "damping", 60 }, { "tone", 6000 },
            { "modDepth", 35 }, { "modRate", 0.8f }, { "drive", 30 }, { "delayFrom", 1 }, { "delay", 1 },
            { "randomness", 0 } } },

        { juce::String::fromUTF8 ("Chaos contrôlé"), {
            { "randomness", 100 }, { "noteReverse", 40 }, { "skipProb", 20 }, { "repeatProb", 25 },
            { "octaveProb", 20 }, { "melodicJitter", 30 }, { "order", 5 }, { "stereoMode", 4 },
            { "repetitions", 6 }, { "scale", 2 } } },

        { juce::String::fromUTF8 ("Banque en séquence"), {
            { "captureMode", 4 }, { "bankSize", 4 }, { "repetitions", 2 }, { "silenceMin", 2 }, { "silenceMax", 4 },
            { "delayFrom", 1 }, { "delay", 3 } } },

        { "Nappe granulaire", {
            { "textureMix", 80 }, { "grainSize", 220 }, { "grainDensity", 30 }, { "grainSpray", 60 },
            { "diffusion", 60 }, { "roomSize", 85 }, { "attack", 400 }, { "decay", -2 }, { "repetitions", 6 },
            { "order", 6 }, { "stereoMode", 1 } } },

        { "Bande au ralenti", {
            { "speedMode", 4 }, { "repetitions", 6 }, { "damping", 40 }, { "modDepth", 25 }, { "drive", 20 },
            { "decay", -2 } } },

        { juce::String::fromUTF8 ("Écho rythmique (tempo)"), {
            { "syncMode", 1 }, { "captureGrid", 2 }, { "phraseBeats", 8 }, { "delayBeats", 8 }, { "delayFrom", 1 },
            { "quantGrid", 4 }, { "quantAmount", 80 }, { "repetitions", 4 }, { "order", 1 } } },

        { "Sound on sound", {
            { "captureMode", 0 }, { "feedback", 60 }, { "repetitions", 1 }, { "delayFrom", 1 }, { "delay", 1 },
            { "order", 1 }, { "duckAmount", -6 } } },
    };
    return presets;
}
