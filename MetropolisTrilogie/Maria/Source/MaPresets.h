#pragma once
#include <juce_core/juce_core.h>

namespace ma
{
class MariaProcessor;
int numFactoryPresets();
juce::String factoryPresetName (int i);
void applyFactoryPreset (MariaProcessor&, int i);
void applySourceStarter (MariaProcessor&, int source);   // "Adapter a la source"
} // namespace ma
