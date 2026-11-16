#pragma once
#include <juce_core/juce_core.h>

namespace gr
{
class GrotProcessor;
int numFactoryPresets();
juce::String factoryPresetName (int i);
void applyFactoryPreset (GrotProcessor&, int i);
void applySourceStarter (GrotProcessor&, int source);   // "Adapter a la source"
} // namespace gr
