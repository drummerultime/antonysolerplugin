cmake_minimum_required(VERSION 3.22)

# macOS : binaire universel Intel + Apple Silicon (à définir avant project())
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures macOS")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Version macOS minimale")

project(FRAGOR VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Chemin vers JUCE : sous-dossier ./JUCE par défaut, ou -DJUCE_DIR=/chemin/vers/JUCE
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/JUCE)
endif()
add_subdirectory(${JUCE_DIR} JUCE)

option(FRAGOR_BUILD_RENDER "Construit l'outil de rendu hors-ligne des presets (tests)" OFF)

set(FRAGOR_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/Presets.cpp
    Source/dsp/Wavetables.cpp
    Source/dsp/Engine.cpp
    Source/dsp/Effects.cpp
    Source/ui/FragorLookAndFeel.cpp
    Source/ui/Widgets.cpp
    Source/ui/Overlays.cpp
)

juce_add_plugin(Fragor
    COMPANY_NAME "Antony Soler"
    BUNDLE_ID "fr.antonysoler.fragor"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Frgr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "FRAGOR"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
)

target_sources(Fragor PRIVATE ${FRAGOR_SOURCES})

set(FRAGOR_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_compile_definitions(Fragor PUBLIC ${FRAGOR_DEFS})

target_link_libraries(Fragor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(FRAGOR_BUILD_RENDER)
    juce_add_console_app(FragorRender PRODUCT_NAME "FragorRender")
    target_sources(FragorRender PRIVATE ${FRAGOR_SOURCES} Source/render/RenderMain.cpp)
    target_compile_definitions(FragorRender PRIVATE ${FRAGOR_DEFS} FRAGOR_RENDER_TOOL=1)
    target_link_libraries(FragorRender
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags)
endif()
