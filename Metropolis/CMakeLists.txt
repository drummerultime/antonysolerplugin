cmake_minimum_required(VERSION 3.22)
project(METROPOLIS VERSION 1.2.0)

if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    find_package(JUCE CONFIG REQUIRED)
endif()

juce_add_plugin(Metropolis
    COMPANY_NAME "Metropolis Audio"
    BUNDLE_ID "audio.atelier.metropolis"
    PLUGIN_MANUFACTURER_CODE Atel
    PLUGIN_CODE Mtro
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Metropolis"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(Metropolis)

target_sources(Metropolis PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_features(Metropolis PRIVATE cxx_std_17)
target_compile_definitions(Metropolis PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Metropolis PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)
