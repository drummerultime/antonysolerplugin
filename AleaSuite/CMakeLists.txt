cmake_minimum_required(VERSION 3.22)

project(ALEA_SUITE VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Sur macOS : binaire universel Intel + Apple Silicon, compatible 10.13+
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# --- JUCE -------------------------------------------------------------------
# Par defaut JUCE est telecharge automatiquement (FetchContent).
# Copie locale :  cmake -B build -DALEA_JUCE_PATH=/chemin/vers/JUCE
set(ALEA_JUCE_PATH "" CACHE PATH "Chemin vers une copie locale de JUCE (optionnel)")
if(ALEA_JUCE_PATH)
    add_subdirectory(${ALEA_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# --- Sources ----------------------------------------------------------------
set(SHARED_SOURCES
    Shared/Gui/GuiCommon.cpp
    Shared/Gui/LinkPanel.cpp
    Shared/Link/AleaLink.cpp)

set(ALEA_SOURCES
    Alea/Source/Common.cpp
    Alea/Source/PluginProcessor.cpp
    Alea/Source/Generators.cpp
    Alea/Source/PluginEditor.cpp
    Alea/Source/Gui/Widgets.cpp)

set(TALEA_SOURCES
    Talea/Source/TaleaCommon.cpp
    Talea/Source/TaleaProcessor.cpp
    Talea/Source/TaleaGenerators.cpp
    Talea/Source/TaleaEditor.cpp
    Talea/Source/Gui/BassWidgets.cpp)

set(COLOR_SOURCES
    Color/Source/ColorCommon.cpp
    Color/Source/ColorTheory.cpp
    Color/Source/ColorProcessor.cpp
    Color/Source/ColorGenerators.cpp
    Color/Source/ColorEditor.cpp
    Color/Source/Gui/ChordWidgets.cpp)

set(CANTUS_SOURCES
    Cantus/Source/CantusCommon.cpp
    Cantus/Source/CantusProcessor.cpp
    Cantus/Source/CantusGenerators.cpp
    Cantus/Source/CantusEditor.cpp
    Cantus/Source/Gui/MelodyWidgets.cpp)

set(SUITE_MODULES
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics)

set(SUITE_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(SUITE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SUITE_FORMATS AU)
endif()

option(ALEA_COPY_AFTER_BUILD "Copie les plugins dans ~/Library/Audio/Plug-Ins apres compilation" ON)

# --- ALEA : percussions -------------------------------------------------------
juce_add_plugin(Alea
    COMPANY_NAME "Antony Soler"
    PRODUCT_NAME "Alea"
    BUNDLE_ID "fr.antonysoler.alea"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Alea
    FORMATS ${SUITE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${ALEA_COPY_AFTER_BUILD}
    VST3_CATEGORIES Instrument Drum
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)
target_sources(Alea PRIVATE ${ALEA_SOURCES} ${SHARED_SOURCES})
target_include_directories(Alea PRIVATE Alea/Source Shared)
target_compile_definitions(Alea PUBLIC ${SUITE_DEFS})
target_link_libraries(Alea PRIVATE ${SUITE_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- TALEA : basse, satellite d'Alea ------------------------------------------
juce_add_plugin(Talea
    COMPANY_NAME "Antony Soler"
    PRODUCT_NAME "Talea"
    BUNDLE_ID "fr.antonysoler.talea"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Tale
    FORMATS ${SUITE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${ALEA_COPY_AFTER_BUILD}
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)
target_sources(Talea PRIVATE ${TALEA_SOURCES} ${SHARED_SOURCES})
target_include_directories(Talea PRIVATE Talea/Source Shared)
target_compile_definitions(Talea PUBLIC ${SUITE_DEFS})
target_link_libraries(Talea PRIVATE ${SUITE_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- COLOR : harmonie (accords, nappes, arpeges), satellite d'Alea et Talea --------
juce_add_plugin(Color
    COMPANY_NAME "Antony Soler"
    PRODUCT_NAME "Color"
    BUNDLE_ID "fr.antonysoler.color"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Colr
    FORMATS ${SUITE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD ${ALEA_COPY_AFTER_BUILD}
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)
target_sources(Color PRIVATE ${COLOR_SOURCES} ${SHARED_SOURCES})
target_include_directories(Color PRIVATE Color/Source Shared)
target_compile_definitions(Color PUBLIC ${SUITE_DEFS})
target_link_libraries(Color PRIVATE ${SUITE_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- CANTUS : melodie (lead), improvise sur les accords de Color, dialogue avec Talea ---
juce_add_plugin(Cantus
    COMPANY_NAME "Antony Soler"
    PRODUCT_NAME "Cantus"
    BUNDLE_ID "fr.antonysoler.cantus"
    PLUGIN_MANUFACTURER_CODE Asol
    PLUGIN_CODE Cant
    FORMATS ${SUITE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${ALEA_COPY_AFTER_BUILD}
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)
target_sources(Cantus PRIVATE ${CANTUS_SOURCES} ${SHARED_SOURCES})
target_include_directories(Cantus PRIVATE Cantus/Source Shared)
target_compile_definitions(Cantus PUBLIC ${SUITE_DEFS})
target_link_libraries(Cantus PRIVATE ${SUITE_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- CLAP (optionnel, non teste) ---------------------------------------------
option(ALEA_CLAP "Construit aussi le format CLAP" OFF)
if(ALEA_CLAP)
    include(FetchContent)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap_juce_extensions)
    clap_juce_extensions_plugin(TARGET Alea CLAP_ID "fr.antonysoler.alea" CLAP_FEATURES instrument drum-machine stereo)
    clap_juce_extensions_plugin(TARGET Talea CLAP_ID "fr.antonysoler.talea" CLAP_FEATURES instrument synthesizer mono stereo)
    clap_juce_extensions_plugin(TARGET Color CLAP_ID "fr.antonysoler.color" CLAP_FEATURES instrument synthesizer stereo)
    clap_juce_extensions_plugin(TARGET Cantus CLAP_ID "fr.antonysoler.cantus" CLAP_FEATURES instrument synthesizer mono stereo)
endif()

# --- Bancs de test hors ligne -------------------------------------------------
option(ALEA_BUILD_TESTS "Construit les bancs de test hors ligne" OFF)
if(ALEA_BUILD_TESTS)
    juce_add_console_app(AleaTest PRODUCT_NAME "AleaTest")
    target_sources(AleaTest PRIVATE ${ALEA_SOURCES} ${SHARED_SOURCES} Tests/AleaTest.cpp)
    target_include_directories(AleaTest PRIVATE Alea/Source Shared)
    target_compile_definitions(AleaTest PRIVATE ${SUITE_DEFS} ALEA_HEADLESS_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AleaTest PRIVATE ${SUITE_MODULES} juce::juce_recommended_config_flags)

    # Talea + Alea dans le meme binaire : test de synchro AleaLink
    juce_add_console_app(TaleaTest PRODUCT_NAME "TaleaTest")
    target_sources(TaleaTest PRIVATE ${TALEA_SOURCES} ${ALEA_SOURCES} ${SHARED_SOURCES} Tests/TaleaTest.cpp)
    target_include_directories(TaleaTest PRIVATE Talea/Source Alea/Source Shared)
    target_compile_definitions(TaleaTest PRIVATE ${SUITE_DEFS} ALEA_HEADLESS_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1 ALEA_NO_PLUGIN_FILTER=1)
    target_link_libraries(TaleaTest PRIVATE ${SUITE_MODULES} juce::juce_recommended_config_flags)

    # Color + Talea + Alea : harmonie, rendu, et suivi des accords via AleaLink
    juce_add_console_app(ColorTest PRODUCT_NAME "ColorTest")
    target_sources(ColorTest PRIVATE ${COLOR_SOURCES} ${TALEA_SOURCES} ${ALEA_SOURCES} ${SHARED_SOURCES} Tests/ColorTest.cpp)
    target_include_directories(ColorTest PRIVATE Color/Source Talea/Source Alea/Source Shared)
    target_compile_definitions(ColorTest PRIVATE ${SUITE_DEFS} ALEA_HEADLESS_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1 ALEA_NO_PLUGIN_FILTER=1)
    target_link_libraries(ColorTest PRIVATE ${SUITE_MODULES} juce::juce_recommended_config_flags)

    # Cantus + Color + Talea + Alea : melodie sur la grille, dialogue, forme partagee (quatuor)
    juce_add_console_app(CantusTest PRODUCT_NAME "CantusTest")
    target_sources(CantusTest PRIVATE ${CANTUS_SOURCES} ${COLOR_SOURCES} ${TALEA_SOURCES} ${ALEA_SOURCES} ${SHARED_SOURCES} Tests/CantusTest.cpp)
    target_include_directories(CantusTest PRIVATE Cantus/Source Color/Source Talea/Source Alea/Source Shared)
    target_compile_definitions(CantusTest PRIVATE ${SUITE_DEFS} ALEA_HEADLESS_TEST=1 JUCE_MODAL_LOOPS_PERMITTED=1 ALEA_NO_PLUGIN_FILTER=1)
    target_link_libraries(CantusTest PRIVATE ${SUITE_MODULES} juce::juce_recommended_config_flags)
endif()

if(ALEA_BUILD_TESTS)
    juce_add_console_app(ColorTheoryProbe PRODUCT_NAME "ColorTheoryProbe")
    target_sources(ColorTheoryProbe PRIVATE Color/Source/ColorTheory.cpp Tests/ColorTheoryProbe.cpp)
    target_include_directories(ColorTheoryProbe PRIVATE Color/Source)
    target_compile_definitions(ColorTheoryProbe PRIVATE ${SUITE_DEFS})
    target_link_libraries(ColorTheoryProbe PRIVATE juce::juce_gui_basics juce::juce_audio_basics juce::juce_recommended_config_flags)
endif()
