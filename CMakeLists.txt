cmake_minimum_required(VERSION 3.22)
project(JT8000_Controller VERSION 1.0.0)

# ============================================================================
# JT-8000 Controller — JUCE CC Mapper Plugin
#
# BUILD:
#   git clone https://github.com/juce-framework/JUCE.git
#   mkdir build && cd build
#   cmake .. -DJUCE_DIR=/path/to/JUCE
#   cmake --build . --config Release
# ============================================================================

if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE framework")
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce_build)

juce_add_plugin(JT8000_Controller
    COMPANY_NAME               "JT8000"
    PLUGIN_MANUFACTURER_CODE   JT8K
    PLUGIN_CODE                JT8C
    FORMATS                    VST3 Standalone
    PRODUCT_NAME               "JT-8000 Controller"
    JUCE_GENERATE_JUCE_HEADER  TRUE
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          TRUE
    IS_MIDI_EFFECT             FALSE
    JUCE_DISPLAY_SPLASH_SCREEN FALSE
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

target_compile_definitions(JT8000_Controller PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(JT8000_Controller
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
