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
# Embedded firmware engine (Phase J2) — OPTIONAL, single source of truth.
#
#   cmake .. -DJT_ENGINE_DIR=/path/to/jteensy8000-v2
#
# Points at your firmware CHECKOUT — nothing is copied into this repo, so
# the desktop engine can never drift from the hardware engine.  Unset, this
# project builds exactly as before: controller only, no Sound modes, no
# JT8000_Synth target.
#
# The engine compiles with the firmware's own host-test flag family
# (-DJT_TESTING gates read-only debug hooks only; it is what `make render`
# — the audible regression baseline — uses, so the audition is bit-faithful
# to that baseline).  test/ supplies the AudioStream_F32/progmem stubs.
# ---------------------------------------------------------------------------
set(JT_ENGINE_DIR "" CACHE PATH "Path to a jteensy8000-v2 checkout (empty = controller-only build)")

if(JT_ENGINE_DIR)
    if(NOT EXISTS "${JT_ENGINE_DIR}/src/core/SynthCore.h")
        message(FATAL_ERROR "JT_ENGINE_DIR does not look like jteensy8000-v2: ${JT_ENGINE_DIR}")
    endif()

    # GLOB is deliberate here: the source list belongs to the FIRMWARE repo's
    # Makefile, and duplicating it by hand in this repo is exactly the drift
    # this project forbids.  CONFIGURE_DEPENDS re-globs when files change.
    file(GLOB JT_ENGINE_SOURCES CONFIGURE_DEPENDS
        "${JT_ENGINE_DIR}/src/core/*.cpp"
        "${JT_ENGINE_DIR}/src/core/dsp/*.cpp")

    add_library(jt_engine STATIC ${JT_ENGINE_SOURCES} Source/EngineHost.cpp)
    target_include_directories(jt_engine PUBLIC
        "${JT_ENGINE_DIR}/src"
        "${JT_ENGINE_DIR}/test"      # AudioStream_F32 / progmem host stubs
        "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_compile_definitions(jt_engine PUBLIC JT_TESTING=1)
    target_compile_features(jt_engine PUBLIC cxx_std_17)
    if(MSVC)
        # /W4 without /WX: the firmware's -Werror gate is g++/clang; blocking
        # Kris's MSVC build on warnings in code this repo must not modify
        # would be self-harm.  New warnings still show in the log.
        #
        # /FI force-includes the GCC-builtin shim (__builtin_ctz et al) into
        # every engine TU — MSVC lacks those intrinsics and the firmware
        # sources must stay untouched.  See Source/MsvcBuiltinShim.h.
        target_compile_options(jt_engine PRIVATE /W4
            "/FI${CMAKE_CURRENT_SOURCE_DIR}/Source/MsvcBuiltinShim.h")
    else()
        target_compile_options(jt_engine PRIVATE -Wall -Wextra -Wdouble-promotion -Werror)
    endif()
    message(STATUS "JT-8000: embedded engine ENABLED from ${JT_ENGINE_DIR}")
else()
    message(STATUS "JT-8000: controller-only build (set JT_ENGINE_DIR for Sound modes)")
endif()

# ---------------------------------------------------------------------------
# RmlUi panel (new UI). Fetches RmlUi and builds the embedded skin blob once;
# jt_rmlui_wire() then links it into each target.
# ---------------------------------------------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/JtRmlUi.cmake)
jt_rmlui_prepare()

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

# ---------------------------------------------------------------------------
# Shared application sources + per-target wiring (Phase J2: two targets share
# one source tree; only compile definitions differ)
# ---------------------------------------------------------------------------
set(JT_APP_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/CCDefinitions.h
    Source/ValueLaw.h
    Source/JtBridgeExt.h
    Source/NrpnLink.h
    Source/StatusDots.h
    Source/EngineHost.h
    Source/JT8000LookAndFeel.h
    Source/JT8000LookAndFeel.cpp
    Source/JtLcdMenu.h
    Source/JtLcdMenu.cpp
    Source/JtEncoderStrip.h
    Source/JtEncoderStrip.cpp
    Source/SectionComponent.h
    Source/SectionComponent.cpp
    Source/StepSequencerGrid.h
    Source/StepSequencerGrid.cpp
    Source/ArpPatternGrid.h
    Source/ArpPatternGrid.cpp
    Source/Envelopedisplay.h
    Source/Envelopedisplay.cpp
    Source/JP8000SyxImporter.h
    Source/JP8000SyxImporter.cpp
    Source/JtTheme.h
    Source/PatchLibraryBrowser.h
    Source/PatchLibraryBrowser.cpp
    Source/SyxProtocol.h
    Source/ParamMapJuce.h
    Source/JT8000Log.h

    # ---- RmlUi panel -------------------------------------------------------
    # The bridge (renderer, interfaces, host component) and the custom controls.
    # SectionComponent stays in the build for now: nothing references it once
    # buildSections() is gone, and removing files is a separate commit.
    Source/JtRmlRenderer.h    Source/JtRmlRenderer.cpp
    Source/JtRmlSystem.h      Source/JtRmlSystem.cpp
    Source/JtRmlFile.h        Source/JtRmlFile.cpp
    Source/JtRmlFont.h        Source/JtRmlFont.cpp
    Source/JtRmlKnob.h        Source/JtRmlKnob.cpp
    Source/JtRmlCombo.h       Source/JtRmlCombo.cpp
    Source/JtRmlElements.h    Source/JtRmlElements.cpp
    Source/JtRmlBassGrid.h    Source/JtRmlBassGrid.cpp
    Source/JtRmlComponent.h   Source/JtRmlComponent.cpp
    Source/JtRmlBinding.h     Source/JtRmlBinding.cpp
    Source/JtRmlPanel.h       Source/JtRmlPanel.cpp
)

function(jt_wire_target tgt)
    target_sources(${tgt} PRIVATE ${JT_APP_SOURCES})

    # PRIVATE throughout — PUBLIC definitions leak into moduleinfo.json
    # (the Ableton subcategory corruption, ledgered from Phase D).
    target_compile_definitions(${tgt}
        PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(${tgt}
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

    # RmlUi + the embedded skin, for both targets.
    jt_rmlui_wire(${tgt})

    if(JT_ENGINE_DIR)
        target_compile_definitions(${tgt} PRIVATE JT8000_HAS_ENGINE=1)
        target_link_libraries(${tgt} PRIVATE jt_engine)
    endif()
endfunction()

jt_wire_target(JT8000_Controller)


