cmake_minimum_required(VERSION 3.22)

project(ROTWANG VERSION 1.0.0 LANGUAGES C CXX)

# Code commun a Maria, Rotwang et Grot (un seul exemplaire, dans ../Shared)
set(METRO_SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../Shared)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# --- JUCE -------------------------------------------------------------------
set(RW_JUCE_PATH "" CACHE PATH "Chemin vers une copie locale de JUCE (optionnel)")
if(RW_JUCE_PATH)
    add_subdirectory(${RW_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(RW_SOURCES
    ${METRO_SHARED}/Gui/GuiCommon.cpp
    Source/RwParams.cpp
    Source/RwEngines.cpp
    Source/RwProcessor.cpp
    Source/RwPresets.cpp
    Source/RwEditor.cpp)

set(RW_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics)

set(RW_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(RW_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND RW_FORMATS AU)
endif()

option(RW_COPY_AFTER_BUILD "Copie le plugin dans ~/Library/Audio/Plug-Ins apres compilation" OFF)

# --- ROTWANG : effet de sound design ------------------------------------------
juce_add_plugin(Rotwang
    COMPANY_NAME "Antony Soler"
    PRODUCT_NAME "Rotwang"
    BUNDLE_ID "fr.antonysoler.rotwang"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Rtwg
    FORMATS ${RW_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${RW_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)
target_sources(Rotwang PRIVATE ${RW_SOURCES})
target_include_directories(Rotwang PRIVATE Source ${METRO_SHARED})
target_compile_definitions(Rotwang PUBLIC ${RW_DEFS})
target_link_libraries(Rotwang PRIVATE ${RW_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- Banc de test hors ligne --------------------------------------------------
option(RW_BUILD_TESTS "Construit le banc de test" OFF)
if(RW_BUILD_TESTS)
    juce_add_console_app(RotwangTest PRODUCT_NAME "RotwangTest")
    target_sources(RotwangTest PRIVATE ${RW_SOURCES} Tests/RotwangTest.cpp)
    target_include_directories(RotwangTest PRIVATE Source ${METRO_SHARED})
    target_compile_definitions(RotwangTest PRIVATE ${RW_DEFS} RW_HEADLESS_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(RotwangTest PRIVATE ${RW_MODULES} juce::juce_recommended_config_flags)
endif()
