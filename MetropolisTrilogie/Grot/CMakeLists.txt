cmake_minimum_required(VERSION 3.22)

project(GROT VERSION 1.0.0 LANGUAGES C CXX)

# Code commun a Maria, Rotwang et Grot (un seul exemplaire, dans ../Shared)
set(METRO_SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../Shared)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# --- JUCE -------------------------------------------------------------------
set(GR_JUCE_PATH "" CACHE PATH "Chemin vers une copie locale de JUCE (optionnel)")
if(GR_JUCE_PATH)
    add_subdirectory(${GR_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(GR_SOURCES
    ${METRO_SHARED}/Gui/GuiCommon.cpp
    Source/GrParams.cpp
    Source/GrEngines.cpp
    Source/GrProcessor.cpp
    Source/GrPresets.cpp
    Source/GrEditor.cpp)

set(GR_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics)

set(GR_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(GR_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND GR_FORMATS AU)
endif()

option(GR_COPY_AFTER_BUILD "Copie le plugin dans ~/Library/Audio/Plug-Ins apres compilation" OFF)

# --- ROTWANG : effet de sound design ------------------------------------------
juce_add_plugin(Grot
    COMPANY_NAME "Antony Soler"
    PRODUCT_NAME "Grot"
    BUNDLE_ID "fr.antonysoler.grot"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Grot
    FORMATS ${GR_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${GR_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx Dynamics Distortion
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)
target_sources(Grot PRIVATE ${GR_SOURCES})
target_include_directories(Grot PRIVATE Source ${METRO_SHARED})
target_compile_definitions(Grot PUBLIC ${GR_DEFS})
target_link_libraries(Grot PRIVATE ${GR_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- Banc de test hors ligne --------------------------------------------------
option(GR_BUILD_TESTS "Construit le banc de test" OFF)
if(GR_BUILD_TESTS)
    juce_add_console_app(GrotTest PRODUCT_NAME "GrotTest")
    target_sources(GrotTest PRIVATE ${GR_SOURCES} Tests/GrotTest.cpp)
    target_include_directories(GrotTest PRIVATE Source ${METRO_SHARED})
    target_compile_definitions(GrotTest PRIVATE ${GR_DEFS} GR_HEADLESS_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(GrotTest PRIVATE ${GR_MODULES} juce::juce_recommended_config_flags)
endif()
