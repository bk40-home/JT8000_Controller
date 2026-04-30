/*
 * JP8000SyxImporter.h — JP-8000/8080 SysEx Import for JUCE Plugin
 *
 * Parses Roland JP-8000/8080 SysEx (.syx) and Standard MIDI (.mid) files
 * into a structured bank of performances and patches. Supports layer-aware
 * loading: a performance's upper/lower patches can target Layer A/B
 * independently.
 *
 * DATA MODEL:
 *   ParsedBank
 *     └── performances[]     — grouped by slot (header + upper + lower)
 *     └── orphanPatches[]    — patches with no matching performance header
 *
 *   Performance
 *     ├── name              — from the 48-byte performance header
 *     ├── mode              — Single / Layer / Split (header byte 26)
 *     ├── splitPoint        — MIDI note number (header byte 27)
 *     ├── balance           — 0-127, 64=centre (header byte 29)
 *     ├── upper             — patch data (from addr[8]=0x40 message)
 *     └── lower             — patch data (from addr[8]=0x42 message)
 *
 *   Patch
 *     ├── name              — 16-char ASCII from the patch data block
 *     ├── dataOffset        — byte offset into the source buffer
 *     ├── dataLen           — length of the patch parameter block
 *     └── srcBuffer         — pointer to the owning MemoryBlock
 *                             (caller must keep alive while Patch is used)
 *
 * LOAD TARGETS:
 *   LoadTarget::LayerA     — write patch CCs into APVTS, send on channel A
 *   LoadTarget::LayerB     — write patch CCs into APVTS, send on channel B
 *   LoadTarget::Current    — send on whichever editTarget is active
 *
 * USAGE (from PluginEditor):
 *   1. Call parseFile() with the raw file data → returns a ParsedBank
 *   2. Show the picker overlay with the bank contents
 *   3. On user selection, call loadPatch() or loadPerformance()
 */

#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CCDefinitions.h"

// Forward declaration — avoids pulling the full processor header into
// every translation unit that includes this header.
class JT8000ControllerProcessor;

namespace JP8000SyxImporter
{

// =========================================================================
// Data model
// =========================================================================

/** Raw patch data extracted from a single DT1 message (252-byte upper/lower). */
struct Patch
{
    juce::String   name;        // 16-char name trimmed
    int            dataOffset;  // byte offset of parameter block in srcBuffer
    int            dataLen;     // length of parameter block
    const uint8_t* srcBuffer;   // raw file buffer (caller owns lifetime)
};

/** JP-8000 performance mode — maps directly to header byte offset +0x10. */
enum class PerfMode : int
{
    Single = 0,
    Layer  = 1,
    Split  = 2
};

/** A complete performance: header metadata + up to two patches. */
struct Performance
{
    juce::String  name;                         // 16-char performance name
    PerfMode      mode       = PerfMode::Single;
    int           splitPoint = 60;              // MIDI note (Split only)
    int           balance    = 64;              // 0-127, 64 = centre

    Patch         upper;             // always present in a valid perf
    Patch         lower;             // always present (may duplicate upper)
    bool          hasDistinctLower = false;   // true when names differ
};

/** Result of parsing a .syx or .mid file. */
struct ParsedBank
{
    std::vector<Performance>  performances;   // grouped upper/lower
    std::vector<Patch>        orphanPatches;  // patches without a header
};

/** Where to send a loaded patch. */
enum class LoadTarget : int
{
    LayerA  = 0,   // force edit target → A, send on channel A
    LayerB  = 1,   // force edit target → B, send on channel B
    Current = 2    // use whichever edit target is currently active
};

// =========================================================================
// Parsing
// =========================================================================

/** Parse a raw .syx or .mid buffer into a structured bank.
 *  @param buf     pointer to the raw file data
 *  @param len     byte length of the buffer
 *  @param isMidi  true if the file is a Standard MIDI File (.mid)
 *  @return ParsedBank with performances and/or orphan patches
 */
ParsedBank parseFile(const uint8_t* buf, size_t len, bool isMidi);

// =========================================================================
// Loading
// =========================================================================

/** Load a single patch into APVTS (parameter state only, no MIDI send).
 *  Sets JT-8000 defaults for params the JP-8000 lacks, then applies
 *  all slot transforms. Does NOT call sendAllCCsPaced().
 */
void loadPatch(juce::AudioProcessorValueTreeState& apvts,
               const Patch& patch);

/** Load a single patch targeted at a specific layer.
 *  Temporarily sets editTarget, calls loadPatch(), then kicks off a
 *  paced send on the correct channel. Restores editTarget afterwards
 *  unless target == Current.
 */
void loadPatchToLayer(JT8000ControllerProcessor& proc,
                      const Patch& patch,
                      LoadTarget target);

/** Load a full performance: upper → A, lower → B, set perf mode/split/balance.
 *  This is the primary entry point for "Load Performance → A+B".
 *
 *  Sequence:
 *    1. Set perf_mode, split point, balance from the JP-8000 header
 *    2. Load upper patch → Layer A (with paced send)
 *    3. Queue lower patch → Layer B (appended after A's CCs)
 */
void loadPerformance(JT8000ControllerProcessor& proc,
                     const Performance& perf);

// =========================================================================
// Utility — exposed for the SyxPickerOverlay and other callers
// =========================================================================

/** Set a single APVTS parameter by CC number.
 *  Handles AudioParameterChoice vs AudioParameterInt dispatch.
 */
void setParamByCC(juce::AudioProcessorValueTreeState& apvts,
                  int cc, int value);

} // namespace JP8000SyxImporter
