cmake_minimum_required(VERSION 3.22)

project(MARIA VERSION 1.6.0 LANGUAGES C CXX)

# Code commun a Maria, Rotwang et Grot (un seul exemplaire, dans ../Shared)
set(METRO_SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../Shared)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# --- JUCE -------------------------------------------------------------------
set(MA_JUCE_PATH "" CACHE PATH "Chemin vers une copie locale de JUCE (optionnel)")
if(MA_JUCE_PATH)
    add_subdirectory(${MA_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(MA_SOURCES
    ${METRO_SHARED}/Gui/GuiCommon.cpp
    Source/MaParams.cpp
    Source/MaEngines.cpp
    Source/MaProcessor.cpp
    Source/MaPresets.cpp
    Source/MaMemory.cpp
    Source/MaEditor.cpp)

set(MA_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics)

set(MA_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(MA_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MA_FORMATS AU)
endif()

option(MA_COPY_AFTER_BUILD "Copie le plugin dans ~/Library/Audio/Plug-Ins apres compilation" OFF)

# --- ROTWANG : effet de sound design ------------------------------------------
juce_add_plugin(Maria
    COMPANY_NAME "Antony Soler"
    PRODUCT_NAME "Maria"
    BUNDLE_ID "fr.antonysoler.maria"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Mari
    FORMATS ${MA_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${MA_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)
target_sources(Maria PRIVATE ${MA_SOURCES})
target_include_directories(Maria PRIVATE Source ${METRO_SHARED})
target_compile_definitions(Maria PUBLIC ${MA_DEFS})
target_link_libraries(Maria PRIVATE ${MA_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- Banc de test hors ligne --------------------------------------------------
option(MA_BUILD_TESTS "Construit le banc de test" OFF)
if(MA_BUILD_TESTS)
    juce_add_console_app(MariaTest PRODUCT_NAME "MariaTest")
    target_sources(MariaTest PRIVATE ${MA_SOURCES} Tests/MariaTest.cpp)
    target_include_directories(MariaTest PRIVATE Source ${METRO_SHARED})
    target_compile_definitions(MariaTest PRIVATE ${MA_DEFS} MA_HEADLESS_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MariaTest PRIVATE ${MA_MODULES} juce::juce_recommended_config_flags)
endif()
