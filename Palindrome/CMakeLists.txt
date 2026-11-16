cmake_minimum_required(VERSION 3.22)

project(PALINDROME VERSION 2.2.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

# JUCE : soit un dossier local (-DJUCE_PATH=/chemin/vers/JUCE), soit téléchargé automatiquement.
set(JUCE_PATH "" CACHE PATH "Chemin vers une copie locale de JUCE (optionnel)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(PALINDROME_FORMATS AU VST3 Standalone)
else()
    set(PALINDROME_FORMATS VST3 Standalone)
endif()

# Deux plugins à partir des mêmes sources :
#  - Palindrome      : effet audio classique (piste audio), AU aufx + VST3 + Standalone
#  - Palindrome MIDI : effet piloté en MIDI (Logic : piste d'instrument + side chain), AU aumf
function(palindrome_plugin target code product formats aumain)
    juce_add_plugin(${target}
        COMPANY_NAME "Antony"
        PLUGIN_MANUFACTURER_CODE Atny
        PLUGIN_CODE ${code}
        PRODUCT_NAME "${product}"
        DESCRIPTION "Capture de phrase rejouee en differe, reverse/normal, ordre aleatoire, correction tonale"
        FORMATS ${formats}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Delay
        AU_MAIN_TYPE ${aumain}
        COPY_PLUGIN_AFTER_BUILD TRUE)

    target_sources(${target} PRIVATE
        Source/PhraseDSP.cpp
        Source/PhraseEngine.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Visuals.cpp
        Source/Views.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

palindrome_plugin(Palindrome Plnd "Palindrome" "${PALINDROME_FORMATS}" kAudioUnitType_Effect)
if(APPLE)
    palindrome_plugin(PalindromeMIDI Plnm "Palindrome MIDI" "AU" kAudioUnitType_MusicEffect)
endif()

# Test du moteur hors DAW (optionnel) : cmake -DPALINDROME_TESTS=ON
option(PALINDROME_TESTS "Compiler le test du moteur" OFF)
if(PALINDROME_TESTS)
    juce_add_console_app(PalindromeTest PRODUCT_NAME "PalindromeTest")
    target_sources(PalindromeTest PRIVATE Tests/EngineTest.cpp Source/PhraseDSP.cpp Source/PhraseEngine.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Visuals.cpp Source/Views.cpp)
    target_compile_definitions(PalindromeTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 PALINDROME_TEST=1)
    target_link_libraries(PalindromeTest PRIVATE juce::juce_audio_utils juce::juce_dsp
                          PUBLIC juce::juce_recommended_config_flags)
    juce_add_console_app(PalindromeSnapshot PRODUCT_NAME "PalindromeSnapshot")
    target_sources(PalindromeSnapshot PRIVATE Tests/UISnapshot.cpp Source/PhraseDSP.cpp Source/PhraseEngine.cpp
                   Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Visuals.cpp Source/Views.cpp)
    target_compile_definitions(PalindromeSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PalindromeSnapshot PRIVATE juce::juce_audio_utils juce::juce_dsp
                          PUBLIC juce::juce_recommended_config_flags)
    juce_add_console_app(PalindromeGuitarTest PRODUCT_NAME "PalindromeGuitarTest")
    target_sources(PalindromeGuitarTest PRIVATE Tests/GuitarTest.cpp Source/PhraseDSP.cpp)
    target_compile_definitions(PalindromeGuitarTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PalindromeGuitarTest PRIVATE juce::juce_audio_basics juce::juce_dsp
                          PUBLIC juce::juce_recommended_config_flags)
endif()
