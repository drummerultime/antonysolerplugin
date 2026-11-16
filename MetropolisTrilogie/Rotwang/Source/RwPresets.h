#pragma once
#include <juce_core/juce_core.h>

namespace rw
{
class RotwangProcessor;
int numFactoryPresets();
juce::String factoryPresetName (int i);
void applyFactoryPreset (RotwangProcessor&, int i);
} // namespace rw
