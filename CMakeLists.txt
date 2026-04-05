cmake_minimum_required(VERSION 3.22)
project(JT8000_Controller VERSION 1.0.0)

# ============================================================================
# JT-8000 Controller — JUCE MIDI CC Mapper Plugin
#
# ABLETON LIVE COMPATIBILITY:
#   Ableton refuses to load VST3 plugins with no audio buses (bug ABLDEV-00057).
#   This is a known limitation documented since 2019, confirmed still present
#   in 2025. JUCE's own MidiLoggerPluginDemo has this same workaround.
#
#   The solution: declare dummy stereo audio buses (silenced in processBlock),
#   and do NOT set IS_MIDI_EFFECT TRUE. Instead we rely on NEEDS_MIDI_INPUT
#   + NEEDS_MIDI_OUTPUT. The plugin appears as an audio effect in Ableton's
#   device list, but passes MIDI through correctly.
#
#   In Ableton, place the plugin on a MIDI track. Route MIDI in from your
#   keyboard/clip, and MIDI out to the Teensy.
#
# BUILD:
#   git clone https://github.com/juce-framework/JUCE.git
#   mkdir build && cd build
#   cmake .. -DJUCE_DIR=/path/to/JUCE
#   cmake --build . --config Release
#
# ABLETON CACHE:
#   After every rebuild:
#     1. Delete the .vst3 bundle from C:\Program Files\Common Files\VST3\
#     2. Delete %APPDATA%\Ableton\Live x.x.x\Preferences\PluginCache\
#     3. Open Ableton → Preferences → Plug-ins → Rescan
# ============================================================================

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE framework")
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce_build)

# ---------------------------------------------------------------------------
# Plugin target
#
# IS_MIDI_EFFECT is FALSE — Ableton cannot load MIDI-only VST3 plugins.
# We declare stereo buses as a compatibility shim and silence them in
# processBlock. This is the standard JUCE workaround (MidiLoggerPluginDemo).
# ---------------------------------------------------------------------------
juce_add_plugin(JT8000_Controller
    COMPANY_NAME               "JT8000"
    PLUGIN_MANUFACTURER_CODE   JT8K
    PLUGIN_CODE                JT8C
    FORMATS                    VST3 Standalone
    PRODUCT_NAME               "JT-8000 Controller"
    JUCE_GENERATE_JUCE_HEADER  TRUE
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           TRUE
    NEEDS_MIDI_OUTPUT          TRUE
    IS_MIDI_EFFECT             FALSE
)

target_sources(JT8000_Controller PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/CCDefinitions.h
    Source/JT8000LookAndFeel.h
    Source/JT8000LookAndFeel.cpp
    Source/SectionComponent.h
    Source/SectionComponent.cpp
    Source/StepSequencerGrid.h
    Source/StepSequencerGrid.cpp
    Source/JP8000SyxImporter.h
)

# Compile definitions — PRIVATE prevents leaking into moduleinfo.json
target_compile_definitions(JT8000_Controller
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

# Link libraries — all PRIVATE. juce_audio_devices required for Windows MIDI.
target_link_libraries(JT8000_Controller
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_data_structures
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
