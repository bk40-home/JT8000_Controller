# =============================================================================
# JtRmlUi.cmake — RmlUi and the embedded skin
# -----------------------------------------------------------------------------
# include() this from the project's CMakeLists BEFORE juce_add_plugin, then:
#
#     jt_rmlui_prepare()                    # once: fetch RmlUi, build the skin blob
#     jt_rmlui_wire(JT8000_Controller)      # per target
#
# WHY TWO FUNCTIONS
#   This project builds the same sources into two targets (Controller and
#   Synth) through jt_wire_target. juce_add_binary_data creates a TARGET, and
#   creating it twice would clash. So the blob is made once and linked per
#   target.
#
# WHY FREETYPE IS OFF
#   RMLUI_FONT_ENGINE=none would leave every combo and the LCD blank - they are
#   live text. The font engine is JtRmlFont, backed by juce::Font, so FreeType
#   is not needed and is switched off to keep the dependency list at RmlUi.
#
# WHY THE SKIN IS EMBEDDED
#   A VST3 is copied, moved and sandboxed; it cannot rely on files beside it.
#   Every skin asset becomes BinaryData and is served by JtRmlFile, which also
#   accepts an optional disk root that overrides it during development.
# =============================================================================

include(FetchContent)

# 6.2 is the latest RELEASE. The repository's own CMakeLists says 6.3 because
# master is 6.3-in-development, and there is no 6.3 tag - asking for one makes
# FetchContent's clone fail with a bare "Build step for rmlui failed: 1", which
# says nothing about the cause.
#
# Verified: all ten bridge sources compile clean against 6.2 headers with
# -Wall -Wextra. Bump this when 6.3 is actually tagged.
set(JT_RMLUI_TAG "6.2" CACHE STRING "RmlUi release tag")

function(jt_rmlui_prepare)
    if(TARGET RmlUi::RmlUi)
        return()                                   # already prepared
    endif()

    # Plain variables, NOT cache-with-FORCE. Under CMP0077 (NEW since CMake
    # 3.13) option() respects an existing normal variable, and these are
    # function-scoped so they reach RmlUi's add_subdirectory without being
    # written into the cache. Forcing BUILD_SHARED_LIBS into the cache would
    # change it for JUCE and for every later target too, which is not ours to
    # decide.
    set(RMLUI_FONT_ENGINE "none")     # JtRmlFont supplies the engine
    set(RMLUI_SAMPLES      OFF)
    set(RMLUI_LUA_BINDINGS OFF)
    set(RMLUI_SVG_PLUGIN   OFF)
    set(BUILD_SHARED_LIBS  OFF)       # a DLL would need copying beside the VST3

    # GIT_SHALLOW with a TAG needs a git that supports fetching a single ref;
    # it works with anything modern, but the failure if it does not is opaque,
    # so the tag is checked up front where the message can be useful.
    FetchContent_Declare(RmlUi
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG        ${JT_RMLUI_TAG}
        GIT_SHALLOW    TRUE)

    message(STATUS "JT-8000: fetching RmlUi ${JT_RMLUI_TAG} (first run only)")
    FetchContent_MakeAvailable(RmlUi)

    if(NOT TARGET RmlUi::RmlUi)
        message(FATAL_ERROR
            "RmlUi was fetched but RmlUi::RmlUi does not exist. Most likely the "
            "tag '${JT_RMLUI_TAG}' is not a real release - check "
            "https://github.com/mikke89/RmlUi/tags and set -DJT_RMLUI_TAG=<tag>.")
    endif()

    file(GLOB JT_SKIN_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/skin/*.rml"
        "${CMAKE_CURRENT_SOURCE_DIR}/skin/*.rcss"
        "${CMAKE_CURRENT_SOURCE_DIR}/skin/*.png")

    if(NOT JT_SKIN_FILES)
        message(FATAL_ERROR
            "No skin in ${CMAKE_CURRENT_SOURCE_DIR}/skin. "
            "It is generated: run tools/gen_skin.py.")
    endif()

    juce_add_binary_data(JT8000_Skin SOURCES ${JT_SKIN_FILES})

    list(LENGTH JT_SKIN_FILES _n)
    message(STATUS "JT-8000: embedding ${_n} skin assets")
endfunction()

function(jt_rmlui_wire tgt)
    target_link_libraries(${tgt} PRIVATE RmlUi::RmlUi JT8000_Skin)

    # SYSTEM so this project's warning flags do not fail on RmlUi's headers.
    get_target_property(_inc RmlUi::RmlUi INTERFACE_INCLUDE_DIRECTORIES)
    if(_inc)
        target_include_directories(${tgt} SYSTEM PRIVATE ${_inc})
    endif()
endfunction()
