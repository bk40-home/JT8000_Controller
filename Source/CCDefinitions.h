/*
 * CCDefinitions.h — JT-8000 CC Parameter Registry
 *
 * Single source of truth mirroring CCDefs.h from the Teensy firmware.
 *
 * PARAMETER TYPES:
 *   Knob / Slider → AudioParameterInt (0-127), sent as raw CC value
 *   Combo / Toggle → AudioParameterChoice (0..N-1 index)
 *
 * CC VALUE CONVERSION:
 *   The firmware does NOT use a single bucket formula for all enums.
 *   Each CC has its own decode logic:
 *     - Toggles: some use (value >= 1), others (value >= 64)
 *     - Poly mode: custom 3-zone thresholds (0-42, 43-84, 85-127)
 *     - Drive: custom 3-zone (<16, <64, >=64)
 *     - Mod FX / Delay FX: zero = Off, rest = bucket
 *     - Waveforms, filter types: standard bucket (v*N/128)
 *
 *   choiceIndexToCC() now checks for a per-param ccLUT (lookup table).
 *   If ccLUT is non-null, it returns the exact CC value for that index.
 *   If ccLUT is null, it falls back to the bucket-midpoint formula.
 *
 * SYNC LOG:
 *   2026-07-19 — Phase J1: aligned to jt8000-params canonical table.
 *              — LFO wave lists 13→6, VA types 13→17 (D-J2/D-J3, wire-law fixes)
 *              — osc pitch, voice split, layer balance knob→select (D-J4/D-J5)
 *              — Ch B default Ch 2; balance renamed "Layer Bal" (canonical)
 *              — seq_steps type:int 1..16 via new intMin/intMax (D-J6)
 *              — master_volume row (cc 7 pseudo — see JtBridgeExt.h) (J1c)
 *              — ONE value law: ccByteFromPlain/plainFromCCByte, maths in
 *                ValueLaw.h under the host gate; three duplicates removed
 *   2026-03-31 — CRITICAL FIX: toggles now send 0/127 (not bucket midpoint 32)
 *              — Added ccLUT field to CCParam for firmware-specific thresholds
 *              — Fixed sectionRow[] missing comma (18 entries, was 17)
 *              — Fixed pitch/detune defVals 65→64 (firmware special-cases CC 64)
 *              — Added CC 71 (Hi Damp) and CC 93 (Lo Damp) to reverb section
 *              — Seq defaults match HTML editor (all zero)
 *              — EQ defaults match HTML editor (0, not 64)
 *   2026-04-23 — Phase 2: Layering support (firmware refactor)
 *              — Added seven perf CCs (140..146) as local-only APVTS params
 *              — isLocalOnly flag on CCParam skips MIDI emit (CCs > 127
 *                cannot travel on class-compliant USB MIDI)
 *              — Added PERFORMANCE_GLOBAL pseudo-section (header-only,
 *                not rendered as a collapsible panel)
 *              — Added kChannelNames/kMidiChannelLUT for Ch A/B combos
 *              — Plugin now tracks Layer A/B channels and Edit Target
 *              — Live MIDI routing uses Edit Target to pick Ch A / Ch B
 */

#pragma once
#include <juce_core/juce_core.h>
#include "ValueLaw.h"   // JUCE-free 7-bit laws — host-gate-asserted (Phase J1)

// -----------------------------------------------------------------------
// Section identifiers — order determines display order in the editor
// -----------------------------------------------------------------------
// PERFORMANCE_GLOBAL is a pseudo-section: the seven perf params (CCs
// 140..146) live here purely so they become APVTS parameters (for state
// save/restore). They are NOT rendered inside a section panel — the main
// editor lays them out directly in the top header bar. PluginEditor
// skips this section when building the collapsible section list.
// -----------------------------------------------------------------------
enum class Section : int
{
    OSC1 = 0, OSC2, MIXER, FILTER,
    AMP_ENV, FILTER_ENV, PITCH_ENV,
    LFO1, LFO2,
    FX_EQ, FX_MOD, FX_DELAY,
    FX_REVERB,
    FX_OUTPUT,
    VELOCITY, PERFORMANCE, BPM,
    STEP_SEQ,
    VOICE_MODE,
    ARP,
    PERFORMANCE_GLOBAL,   // pseudo: rendered in header, not as a section
    NUM_SECTIONS
};

static const char* sectionNames[] = {
    "Oscillator 1", "Oscillator 2", "Mixer", "Filter",
    "Amp Envelope", "Filter Envelope", "Pitch Envelope",
    "LFO 1", "LFO 2",
    "EQ & Drive", "Mod FX", "Delay",
    "Reverb",
    "Output Mix",
    "Velocity", "Performance", "BPM Clock",
    "Step Sequencer",
    "Voice mode",
    "Arpeggiator",
    "Performance Global",   // PERFORMANCE_GLOBAL — header-only, no panel
};

// Row grouping for side-by-side layout. -1 = full width.
// Sections sharing the same non-negative row number are placed side by side.
static const int sectionRow[] = {
    -1, -1, -1, -1,        // OSC1, OSC2, MIXER, FILTER
    -1, -1, -1,            // AMP_ENV, FILTER_ENV, PITCH_ENV (full width for envelope display)
    -1, -1,                // LFO1, LFO2
     0,  0,                // FX_EQ, FX_MOD
     1,  1,                // FX_DELAY, FX_REVERB
     2,  2,  2,            // FX_OUTPUT, VELOCITY, PERFORMANCE
    -1, -1,                // BPM, STEP_SEQ
    3,                     // VOICE_MODE
    -1,                    // ARP (full width — global row + tri-lane grid)
    -1,                    // PERFORMANCE_GLOBAL (ignored by section builder)
};

// -----------------------------------------------------------------------
// Control style — determines widget type AND JUCE parameter type
// -----------------------------------------------------------------------
enum class ControlStyle : int
{
    Knob = 0,
    Slider,
    Combo,
    Toggle,
    StepGrid,
    ArpGrid       // Phase 9: bespoke tri-lane arp pattern grid (on/off/accent/ratchet)
};

// -----------------------------------------------------------------------
// Parameter definition
// -----------------------------------------------------------------------
struct CCParam
{
    int               cc;           // MIDI CC number (must match firmware CCDefs.h)
    const char*       id;           // Unique JUCE parameter ID (no spaces)
    const char*       name;         // User-facing label
    Section           section;      // Which collapsible panel this belongs to
    int               defVal;       // Default: CC value for Knob/Slider, choice INDEX for Combo/Toggle
    ControlStyle      style;        // Widget + parameter type selector
    const char* const* enumNames;   // Named options (nullptr for continuous)
    int               enumCount;    // Number of enum options (0 for continuous)
    const int*        ccLUT;        // Per-choice CC values (nullptr = use bucket formula)
    bool              isLocalOnly;  // Phase J1: meaning has DRIFTED since Phase 2.
                                    // NRPN made every param sendable, so this no
                                    // longer gates transmission anywhere.  Its one
                                    // remaining consumer is the editor's Init Patch,
                                    // which SKIPS these rows: true now means "this
                                    // is performance/global setup, not patch data —
                                    // preserve it when the patch is initialised".
                                    // True on the seven perf params and on
                                    // master_volume; false everywhere else.
    int               intMin = 0;   // Phase J1: canonical `type: int` support.
    int               intMax = 0;   // intMax > intMin marks an int-lattice param
                                    // (APVTS holds intMin..intMax, the wire byte
                                    // sits on the firmware's decode lattice — see
                                    // ValueLaw.h).  0/0 = plain 0..127 row; the
                                    // defaults keep every existing row untouched.
};

/* Check if a param is an int-lattice row (canonical `type: int`) */
inline bool isIntLatticeParam(const CCParam& p)
{
    return p.intMax > p.intMin;
}

// -----------------------------------------------------------------------
// CC conversion — respects firmware decode logic per parameter
//
// If a param has a ccLUT, use it directly.
// Otherwise, fall back to bucket-midpoint formula (for waveforms, filter
// types, timing modes, LFO dests, arb banks — all use (v*N)/128 decode).
// -----------------------------------------------------------------------

/* Convert a choice index (0..N-1) to the CC value the firmware expects */
inline int choiceIndexToCC(int index, int numChoices, const int* lut = nullptr)
{
    if (numChoices <= 0) return 0;
    if (index < 0) index = 0;
    if (index >= numChoices) index = numChoices - 1;

    // Per-param lookup table — exact match to firmware thresholds
    if (lut != nullptr)
        return lut[index];

    // Bucket midpoint — the maths lives in ValueLaw.h so the host gate
    // asserts the exact shipping formula against the firmware decode law.
    return ValueLaw::ccFromChoice(index, numChoices);
}

/* Convert a raw CC value (0..127) to a choice index (0..N-1) */
inline int ccToChoiceIndex(int ccVal, int numChoices)
{
    return ValueLaw::choiceFromCc(ccVal, numChoices);
}

/* Check if a param uses AudioParameterChoice (Combo or Toggle) */
inline bool isChoiceParam(const CCParam& p)
{
    return p.style == ControlStyle::Combo || p.style == ControlStyle::Toggle;
}

// -----------------------------------------------------------------------
// One value law, three shapes (Phase J1).
//
// Before J1 the CC-byte <-> APVTS-plain conversion was written out three
// times (parameterChanged, sendBankDump, the SYX importer) and drifted was
// only a matter of time.  These two helpers are now the ONLY place the law
// lives on the JUCE side; the pure maths they call is in ValueLaw.h so the
// host gate compiles and exhaustively asserts the identical shipping code.
//
//   Choice:      plain = option index      byte = LUT or bucket midpoint
//   Int-lattice: plain = intMin..intMax    byte = firmware decode lattice
//   Raw:         plain = 0..127            byte = plain
// -----------------------------------------------------------------------
inline int ccByteFromPlain(const CCParam& p, float plain)
{
    const int v = (int)(plain + (plain >= 0.0f ? 0.5f : -0.5f));   // round-nearest
    if (isChoiceParam(p))
        return choiceIndexToCC(v, p.enumCount, p.ccLUT);
    if (isIntLatticeParam(p))
        return ValueLaw::ccFromIntLattice(v, p.intMin, p.intMax);
    return v < 0 ? 0 : (v > 127 ? 127 : v);
}

inline float plainFromCCByte(const CCParam& p, int v7)
{
    if (isChoiceParam(p))
        return (float)ccToChoiceIndex(v7 < 0 ? 0 : (v7 > 127 ? 127 : v7), p.enumCount);
    if (isIntLatticeParam(p))
        return (float)ValueLaw::intLatticeFromCc(v7, p.intMin, p.intMax);
    return (float)(v7 < 0 ? 0 : (v7 > 127 ? 127 : v7));
}

// -----------------------------------------------------------------------
// Named option arrays
// -----------------------------------------------------------------------
static const char* const kWaveNames[] = {
    "Sine","Sawtooth","Square","Triangle","Arbitrary","Pulse",
    "Saw Rev","S&H","Tri Var","BL Saw","BL Saw Rev","BL Square","BL Pulse","Supersaw"
};
// Canonical l_f_o_wave (params.yaml) — SIX options.  The previous 13-entry
// v1-era list mis-bucketed every send (defect D-J2): the firmware decodes
// lroundf(norm * 5), so a 13-bucket midpoint landed on the wrong waveform
// and offered seven options that don't exist.  Order is frozen by the yaml.
static const char* const kLfoWaveNames[] = {
    "Sine","Triangle","Sawtooth","Square","S&H","Noise"
};
static const char* const kLfoDestNames[]  = { "None","Pitch","Filter","PWM","Amp" };
static const char* const kTimingNames[] = {
    "Free","4 Bar","2 Bar","1 Bar","1/2","1/4","1/8","1/16","1/32",
    "1/4 T","1/8 T","1/16 T"
};
static const char* const kModFxNames[] = {
    "Off","Chorus 1","Chorus 2","Chorus 3",
    "Flanger 1","Flanger 2","Deep Flanger",
    "Phaser 1","Phaser 2","Phaser 3","Phaser 4","Super Chorus"
};
static const char* const kDelayFxNames[] = {
    "Off","Mono Short","Mono Long","Pan L>R","Pan R>L","Pan Stereo"
};
static const char* const kDriveNames[]   = { "Bypass","Soft Clip","Hard Clip" };
static const char* const kFltEngNames[]  = { "OBXa","VA Bank" };
static const char* const kFltModeNames[] = {
    "4-Pole LP","2-Pole LP","2-Pole BP","2-Pole Push","Xpander","Xpander+M"
};
// Canonical v_a_filter (params.yaml) — SEVENTEEN options.  The previous
// 13-entry list (defect D-J3) made the four MoogDV types (D'Angelo–Välimäki,
// ICASSP 2013) unreachable and mis-bucketed the upper types on the wire.
static const char* const kVaTypeNames[] = {
    "SVF LP2","SVF HP2","SVF BP2","SVF Notch","SVF AP",
    "Moog LP4","Moog LP2","Moog BP2","Diode LP4","Korg35 LP","Korg35 HP",
    "TPT1 LP","TPT1 HP","MoogDV LP4","MoogDV LP2","MoogDV HP4","MoogDV BP"
};
static const char* const kXpModeNames[] = {
    "LP4","LP3","LP2","LP1","HP3","HP2","HP1","BP4","BP2",
    "N2","PH3","HP2+LP1","HP3+LP1","N2+LP1","PH3+LP1"
};
static const char* const kClockNames[]   = { "Internal","External" };
static const char* const kPolyNames[]    = { "Poly","Mono","Unison" };
static const char* const kOnOffNames[]   = { "Off","On" };
// G1: mix.pitch_mod_dest (0x0107) — JP-8000 "LFO1 & ENV Destination".
// Order MUST match the canonical option set in params.yaml (bucket encode).
static const char* const kPitchModDestNames[] = { "OSC1+2", "OSC2", "X-MOD" };
static const char* const kBypassNames[]  = { "Active","Bypass" };
static const char* const kArbBankNames[] = {
    "BwBlended","BwPerfect","BwSaw","BwSawBright","BwSawGap",
    "BwSawRound","BwSin","BwSqu","BwSquRound","BwTri"
};
static const char* const kSeqDirNames[]  = { "Forward","Reverse","Bounce","Random" };
static const char* const kSeqDestNames[] = { "None","Pitch","Filter","PWM","Amp" };
// Aux lane (Stage B/C/D) has its OWN 5-destination set (order matches
// kOpt_seq_aux_dest in ParamTable.h).  Same option COUNT as the gate lane,
// so the 5-band kSeqDestLUT {0,32,64,95,127} maps 1:1 here too.
static const char* const kSeqAuxDestNames[] = { "None","Filter","Pan","DelaySend","Drive" };
// Arpeggiator note-ordering modes — order FROZEN with firmware ArpMode /
// params.yaml arp_mode.  UpDn Inc repeats the top & bottom notes; Exc doesn't.
static const char* const kArpModeNames[] = {
    "Up","Down","UpDn Inc","UpDn Exc","AsPlayed","Random","Chord"
};
// Arp octave span (1..4 octaves) — choice index 0..3.
static const char* const kArpOctNames[]  = { "1","2","3","4" };

// -----------------------------------------------------------------------
// CC lookup tables for params with firmware-specific decode thresholds.
// Each array maps choice index → exact CC value the firmware expects.
// -----------------------------------------------------------------------

// Toggles: firmware uses (value >= 1) or (value >= 64) → send 0 or 127
static const int kToggleLUT[]   = { 0, 127 };

// 2-choice with >= 64 threshold (BPM source, filter engine)
static const int kMidpointLUT[] = { 0, 127 };

// Poly mode: firmware uses <=42 / <=84 / >84
static const int kPolyLUT[]     = { 0, 63, 106 };

// Drive: firmware uses <16 / <64 / >=64
static const int kDriveLUT[]    = { 0, 32, 96 };

// Mod FX: firmware checks rawVal==0 for Off, then maps rest via bucket
// Off=0, then bucket midpoints for indices 1-11 within range 1-127
static const int kModFxLUT[]    = { 0, 12, 23, 35, 46, 58, 69, 81, 92, 104, 115, 127 };

// Delay FX: firmware checks rawVal==0 for Off, then maps rest
static const int kDelayFxLUT[]  = { 0, 22, 43, 64, 85, 107 };

// Seq direction: firmware uses (v*3)/127
static const int kSeqDirLUT[]   = { 0, 42, 85, 127 };

// Seq destination: firmware uses (v*4)/127
static const int kSeqDestLUT[]  = { 0, 32, 64, 95, 127 };

// -----------------------------------------------------------------------
// Performance-global parameter tables (CCs 140..146 — local-only params)
//
// These are NOT sent as live MIDI (CC numbers above 127 can't travel on
// class-compliant USB MIDI), so they exist purely as APVTS parameters
// for UI display and state save/restore. The plugin header renders
// these controls directly; they are NOT placed into section panels.
// -----------------------------------------------------------------------

// Perf Mode (CC 140): firmware decodes <=42 / <=84 / >84 → Single/Layer/Split.
// LUT values are bucket midpoints so round-tripping the choice index
// through choiceIndexToCC → ccToChoiceIndex is stable.
static const char* const kPerfModeNames[] = { "Single", "Layer", "Split" };
static const int        kPerfModeLUT[]   = { 21, 63, 106 };

// Edit Target (CC 144): same three-zone decode as Perf Mode.
// A / B / Both controls which layer receives edits AND which channel
// the plugin sends CCs on (PluginProcessor handles the routing).
static const char* const kEditTargetNames[] = { "A", "B", "Both" };
static const int        kEditTargetLUT[]   = { 21, 63, 106 };

// MIDI Channel A / B (CC 145, 146): firmware decodes as
//   channel = ((v * 16) / 128) + 1   (range 1..16, JP-8000 authentic).
// The reverse map is v = (channel-1) * 8 + 4 (centre of each bucket).
// Used for CCs 145/146 and in PluginProcessor when selecting the
// outgoing channel number.
static const char* const kChannelNames[] = {
    "Ch 1","Ch 2","Ch 3","Ch 4","Ch 5","Ch 6","Ch 7","Ch 8",
    "Ch 9","Ch 10","Ch 11","Ch 12","Ch 13","Ch 14","Ch 15","Ch 16"
};
static const int kMidiChannelLUT[] = {
    4, 12, 20, 28, 36, 44, 52, 60, 68, 76, 84, 92, 100, 108, 116, 124
};
// (Phase J1 note: the "NOT sent as live MIDI" commentary above predates the
// NRPN wire — every perf param transmits like any other now.  The rows stay
// header-rendered and isLocalOnly-marked only so Init Patch preserves them.)

// Osc pitch offset (CC 41/42) — canonical pitch_offset select, five options
// on the semitone lattice.  Was a raw knob (defect D-J4): coarsely worked
// through the continuous law but sat off-lattice and read as 0..127.
static const char* const kPitchOffsetNames[] = { "-24","-12","0","+12","+24" };

// Voice Split (CC 141) — canonical voice_split select: layer A+B allocation,
// always summing to the 8 hardware voices.  Was a raw knob (defect D-J5).
static const char* const kVoiceSplitNames[] = {
    "1+7","2+6","3+5","4+4","5+3","6+2","7+1"
};

// Layer Balance (CC 143) — canonical layer_balance select: which layer the
// panel EDITS (A / both / B).  Not an audio crossfade, hence a selector and
// never slewed.  Was a raw knob (defect D-J5).
static const char* const kLayerBalNames[] = { "A","AB","B" };

// -----------------------------------------------------------------------
// Shorthand macros for kAllParams[] — includes ccLUT field
// -----------------------------------------------------------------------
#define KNOB              ControlStyle::Knob,   nullptr, 0, nullptr
#define VSLIDER           ControlStyle::Slider, nullptr, 0, nullptr
#define COMBO(names, n)   ControlStyle::Combo,  names, n, nullptr
#define COMBO_LUT(names, n, lut) ControlStyle::Combo, names, n, lut
#define TOGGLE(names, n)  ControlStyle::Toggle, names, n, kToggleLUT
#define TOGGLE_LUT(names, n, lut) ControlStyle::Toggle, names, n, lut

// -----------------------------------------------------------------------
// Master parameter table
// defVal: raw CC 0-127 for Knob/Slider, choice index 0..N-1 for Combo/Toggle
// -----------------------------------------------------------------------
static const CCParam kAllParams[] =
{
    // === OSC 1 ===
    { 21,  "osc1_wave",      "OSC1 Wave",       Section::OSC1,     0,   COMBO(kWaveNames, 14) },
    { 41,  "osc1_pitch",     "OSC1 Pitch",      Section::OSC1,     2,   COMBO(kPitchOffsetNames, 5) },   // D-J4: select lattice, def "0"
    { 45,  "osc1_fine",      "OSC1 Fine",       Section::OSC1,     64,  KNOB },
    { 43,  "osc1_detune",    "OSC1 Detune",     Section::OSC1,     64,  KNOB },
    { 77,  "osc1_ss_det",    "SS1 Detune",      Section::OSC1,     0,   KNOB },
    { 78,  "osc1_ss_mix",    "SS1 Mix",         Section::OSC1,     0,   KNOB },
    { 123, "osc1_fb_amt",    "OSC1 FB Amt",     Section::OSC1,     0,   KNOB },
    { 125, "osc1_fb_mix",    "OSC1 FB Mix",     Section::OSC1,     0,   KNOB },
    { 86,  "osc1_freq_dc",   "OSC1 Freq DC",    Section::OSC1,     0,   KNOB },
    { 87,  "osc1_shape_dc",  "OSC1 Shape DC",   Section::OSC1,     0,   KNOB },
    // G2: per-osc share of LFO1's PWM (JP-8000 SQR Control 2).  Pseudo-cc
    // 201 — no v1 alias; JtBridgeExt maps it onto ParamID 0x000D for the
    // NRPN wire.  Default 127 = engine 1.0 = pre-split behaviour.
    { 201, "osc1_pwm_amt",   "PWM Amt",         Section::OSC1,     127, KNOB },
    { 91,  "osc1_ring",      "Ring 1",          Section::OSC1,     0,   KNOB },
    { 101, "osc1_arb_bank",  "OSC1 Bank",       Section::OSC1,     0,   COMBO(kArbBankNames, 10) },
    { 83,  "osc1_arb_idx",   "OSC1 Table",      Section::OSC1,     0,   KNOB },

    // === OSC 2 ===
    { 22,  "osc2_wave",      "OSC2 Wave",       Section::OSC2,     0,   COMBO(kWaveNames, 14) },
    { 42,  "osc2_pitch",     "OSC2 Pitch",      Section::OSC2,     2,   COMBO(kPitchOffsetNames, 5) },   // D-J4: select lattice, def "0"
    { 46,  "osc2_fine",      "OSC2 Fine",       Section::OSC2,     64,  KNOB },
    { 44,  "osc2_detune",    "OSC2 Detune",     Section::OSC2,     64,  KNOB },
    { 79,  "osc2_ss_det",    "SS2 Detune",      Section::OSC2,     0,   KNOB },
    { 80,  "osc2_ss_mix",    "SS2 Mix",         Section::OSC2,     0,   KNOB },
    { 124, "osc2_fb_amt",    "OSC2 FB Amt",     Section::OSC2,     0,   KNOB },
    { 126, "osc2_fb_mix",    "OSC2 FB Mix",     Section::OSC2,     0,   KNOB },
    { 88,  "osc2_freq_dc",   "OSC2 Freq DC",    Section::OSC2,     0,   KNOB },
    { 89,  "osc2_shape_dc",  "OSC2 Shape DC",   Section::OSC2,     0,   KNOB },
    // G2 twin of osc1_pwm_amt — pseudo-cc 202 <-> ParamID 0x008D.
    { 202, "osc2_pwm_amt",   "PWM Amt",         Section::OSC2,     127, KNOB },
    { 92,  "osc2_ring",      "Ring 2",          Section::OSC2,     0,   KNOB },
    { 102, "osc2_arb_bank",  "OSC2 Bank",       Section::OSC2,     0,   COMBO(kArbBankNames, 10) },
    { 85,  "osc2_arb_idx",   "OSC2 Table",      Section::OSC2,     0,   KNOB },

    // === MIXER ===
    { 47,  "osc_bal",        "Osc Balance",     Section::MIXER,    0,   KNOB },
    { 60,  "osc1_mix",       "OSC1 Level",      Section::MIXER,    127, KNOB },
    { 61,  "osc2_mix",       "OSC2 Level",      Section::MIXER,    0,   KNOB },
    { 58,  "sub_mix",        "Sub",             Section::MIXER,    0,   KNOB },
    { 59,  "noise_mix",      "Noise",           Section::MIXER,    0,   KNOB },
    { 19,  "xmod_depth",     "XMod Depth",      Section::MIXER,    0,   KNOB },
    // G1: JP-8000 "LFO1 & ENV Destination" — routes (LFO1-pitch + pitch-env)
    // to both oscs / OSC2 only / cross-mod depth.  Pseudo-cc 203 <-> ParamID
    // 0x0107.  Bucket encode (v*3/128) matches the firmware's table decode —
    // no ccLUT needed.  Default index 0 = pre-G1 behaviour.
    { 203, "pitch_mod_dest", "Pitch Dest",      Section::MIXER,    0,   COMBO(kPitchModDestNames, 3) },
    { 20,  "osc_sync",       "Osc Sync",        Section::MIXER,    0,   TOGGLE(kOnOffNames, 2) },

    // === FILTER ===
    { 23,  "flt_cut",        "Cutoff",          Section::FILTER,   127, KNOB },
    { 24,  "flt_res",        "Resonance",       Section::FILTER,   0,   KNOB },
    { 50,  "flt_key",        "Key Track",       Section::FILTER,   64,  KNOB },
    { 84,  "flt_oct",        "Oct Ctrl",        Section::FILTER,   0,   KNOB },
    { 113, "flt_engine",     "Engine",          Section::FILTER,   1,   COMBO_LUT(kFltEngNames, 2, kMidpointLUT) },
    { 112, "flt_mode",       "OBXa Mode",       Section::FILTER,   0,   COMBO(kFltModeNames, 6) },
    { 115, "va_type",        "VA Type",         Section::FILTER,   0,   COMBO(kVaTypeNames, 17) },   // D-J3: canonical count
    { 111, "flt_multi",      "Multimode",       Section::FILTER,   0,   KNOB },
    { 117, "flt_resmod",     "Res Mod",         Section::FILTER,   0,   KNOB },
    { 114, "flt_xp",         "Xpander Mode",    Section::FILTER,   0,   COMBO(kXpModeNames, 15) },

    // === AMP ENV ===
    { 25,  "amp_a",       "Atk",       Section::AMP_ENV,    0,   VSLIDER },
    { 147, "amp_atk_crv", "Atk Curve", Section::AMP_ENV,    64,  VSLIDER },
    { 26,  "amp_d",       "Dec",       Section::AMP_ENV,    0,   VSLIDER },
    { 148, "amp_dec_crv", "Dec Curve", Section::AMP_ENV,    64,  VSLIDER },
    { 27,  "amp_s",       "Sus",       Section::AMP_ENV,    127, VSLIDER },
    { 28,  "amp_r",       "Rel",       Section::AMP_ENV,    0,   VSLIDER },
    { 149, "amp_rel_crv", "Rel Curve", Section::AMP_ENV,    64,  VSLIDER },

    // === FILTER ENV ===
    { 29,  "fe_a",        "Atk",       Section::FILTER_ENV, 0,   VSLIDER },
    { 150, "fe_atk_crv",  "Atk Curve", Section::FILTER_ENV, 64,  VSLIDER },
    { 30,  "fe_d",        "Dec",       Section::FILTER_ENV, 0,   VSLIDER },
    { 151, "fe_dec_crv",  "Dec Curve", Section::FILTER_ENV, 64,  VSLIDER },
    { 31,  "fe_s",        "Sus",       Section::FILTER_ENV, 127, VSLIDER },
    { 48,  "flt_env",     "Env Amt",   Section::FILTER_ENV, 64,  VSLIDER },
    { 32,  "fe_r",        "Rel",       Section::FILTER_ENV, 0,   VSLIDER },
    { 152, "fe_rel_crv",  "Rel Curve", Section::FILTER_ENV, 64,  VSLIDER },

    // === PITCH ENV ===
    { 65,  "pe_a",        "Atk",       Section::PITCH_ENV, 0,   VSLIDER },
    { 153, "pe_atk_crv",  "Atk Curve", Section::PITCH_ENV, 64,  VSLIDER },
    { 66,  "pe_d",        "Dec",       Section::PITCH_ENV, 0,   VSLIDER },
    { 154, "pe_dec_crv",  "Dec Curve", Section::PITCH_ENV, 64,  VSLIDER },
    { 67,  "pe_s",        "Sus",       Section::PITCH_ENV, 0,   VSLIDER },
    { 69,  "pe_depth",    "Depth",     Section::PITCH_ENV, 64,  VSLIDER },
    { 68,  "pe_r",        "Rel",       Section::PITCH_ENV, 0,   VSLIDER },
    { 155, "pe_rel_crv",  "Rel Curve", Section::PITCH_ENV, 64,  VSLIDER },

    // === LFO 1 ===
    { 62,  "l1_wave",  "LFO1 Wave",    Section::LFO1, 0,  COMBO(kLfoWaveNames, 6) },    // D-J2: canonical six
    { 54,  "l1_freq",  "LFO1 Rate",    Section::LFO1, 0,  KNOB },
    { 55,  "l1_depth", "LFO1 Depth",   Section::LFO1, 0,  KNOB },
    { 56,  "l1_dest",  "LFO1 Dest",    Section::LFO1, 0,  COMBO(kLfoDestNames, 5) },
    { 120, "l1_sync",  "LFO1 Sync",    Section::LFO1, 0,  COMBO(kTimingNames, 12) },
    { 33,  "l1_p_dep", "L1 Pitch",     Section::LFO1, 0,  KNOB },
    { 34,  "l1_f_dep", "L1 Filter",    Section::LFO1, 0,  KNOB },
    { 35,  "l1_w_dep", "L1 PWM",       Section::LFO1, 0,  KNOB },
    { 36,  "l1_a_dep", "L1 Amp",       Section::LFO1, 0,  KNOB },
    { 37,  "l1_delay", "L1 Delay",     Section::LFO1, 0,  KNOB },

    // === LFO 2 ===
    { 63,  "l2_wave",  "LFO2 Wave",    Section::LFO2, 0,  COMBO(kLfoWaveNames, 6) },    // D-J2: canonical six
    { 51,  "l2_freq",  "LFO2 Rate",    Section::LFO2, 0,  KNOB },
    { 52,  "l2_depth", "LFO2 Depth",   Section::LFO2, 0,  KNOB },
    { 53,  "l2_dest",  "LFO2 Dest",    Section::LFO2, 0,  COMBO(kLfoDestNames, 5) },
    { 121, "l2_sync",  "LFO2 Sync",    Section::LFO2, 0,  COMBO(kTimingNames, 12) },
    { 38,  "l2_p_dep", "L2 Pitch",     Section::LFO2, 0,  KNOB },
    { 39,  "l2_f_dep", "L2 Filter",    Section::LFO2, 0,  KNOB },
    { 40,  "l2_w_dep", "L2 PWM",       Section::LFO2, 0,  KNOB },
    { 57,  "l2_a_dep", "L2 Amp",       Section::LFO2, 0,  KNOB },
    { 64,  "l2_delay", "L2 Delay",     Section::LFO2, 0,  KNOB },

    // === FX EQ ===
    { 99,  "fx_bass",   "Bass",    Section::FX_EQ, 0,  KNOB },
    { 100, "fx_treble", "Treble",  Section::FX_EQ, 0,  KNOB },
    { 16,  "fx_drive",  "Drive",   Section::FX_EQ, 0,  COMBO_LUT(kDriveNames, 3, kDriveLUT) },

    // === FX MOD ===
    { 103, "fx_mod_type", "Mod Type",  Section::FX_MOD, 0, COMBO_LUT(kModFxNames, 12, kModFxLUT) },
    { 104, "fx_mod_mix",  "Mod Mix",   Section::FX_MOD, 0, KNOB },
    { 105, "fx_mod_rate", "Mod Rate",  Section::FX_MOD, 0, KNOB },
    { 106, "fx_mod_fb",   "Mod FB",    Section::FX_MOD, 0, KNOB },

    // === FX DELAY ===
    { 107, "fx_dly_type", "Delay Type",  Section::FX_DELAY, 0, COMBO_LUT(kDelayFxNames, 6, kDelayFxLUT) },
    { 110, "fx_dly_time", "Delay Time",  Section::FX_DELAY, 0, KNOB },
    { 108, "fx_dly_mix",  "Delay Mix",   Section::FX_DELAY, 0, KNOB },
    { 109, "fx_dly_fb",   "Delay FB",    Section::FX_DELAY, 0, KNOB },

    // === FX REVERB ===
    // Core controls — tank character and output level
    { 70,  "fx_rv_size",    "Rev Size",   Section::FX_REVERB, 0, KNOB },
    { 71,  "fx_rv_damp",    "Hi Damp",    Section::FX_REVERB, 0, KNOB },
    { 93,  "fx_rv_lodamp",  "Lo Damp",    Section::FX_REVERB, 0, KNOB },
    { 75,  "fx_rv_mix",     "Rev Mix",    Section::FX_REVERB, 0, KNOB },
    { 94,  "fx_rv_bypass",  "Rev Bypass", Section::FX_REVERB, 0, COMBO_LUT(kBypassNames, 2, kMidpointLUT) },
    // Extended — performance controls (CC 95-98)
    // Shimmer: pitch-shifted feedback inside the tank. Zero CPU when off.
    { 95,  "fx_rv_shimmer", "Shimmer",    Section::FX_REVERB, 0, KNOB },
    // Freeze: saves all params, sets decay=1.0, mutes input. Toggle >63 = on.
    { 96,  "fx_rv_freeze",  "Freeze",     Section::FX_REVERB, 0, TOGGLE(kOnOffNames, 2) },
    // Post-tank output EQ on wet signal only. Different from Hi/Lo Damp
    // which act inside the tank loop and affect how quickly the tail decays.
    { 97,  "fx_rv_lp",      "Out LP",     Section::FX_REVERB, 0, KNOB },
    { 98,  "fx_rv_hp",      "Out HP",     Section::FX_REVERB, 0, KNOB },

    // === FX OUTPUT ===
    { 74,  "fx_dry",  "Dry Mix",  Section::FX_OUTPUT, 127, KNOB },
    { 76,  "fx_jpfx", "JPFX Mix", Section::FX_OUTPUT, 127, KNOB },

    // === VELOCITY ===
    { 10,  "vel_amp", "Vel Amp",    Section::VELOCITY, 0, KNOB },
    { 11,  "vel_flt", "Vel Filter", Section::VELOCITY, 0, KNOB },
    { 12,  "vel_env", "Vel Env",    Section::VELOCITY, 0, KNOB },

    // === PERFORMANCE ===
    { 81,  "glide_on",   "Glide",        Section::PERFORMANCE, 0,   TOGGLE(kOnOffNames, 2) },
    { 82,  "glide_time", "Glide Time",   Section::PERFORMANCE, 0,   KNOB },
    { 127, "bend_range", "Bend Range",   Section::PERFORMANCE, 12,  KNOB },
    { 90,  "amp_level",  "Amp Level",    Section::PERFORMANCE, 127, KNOB },
    { 14,  "poly_mode",  "Voice Mode",   Section::PERFORMANCE, 0,   COMBO_LUT(kPolyNames, 3, kPolyLUT) },
    { 15,  "uni_det",    "Unison Det",   Section::PERFORMANCE, 0,   KNOB },

    // === BPM ===
    { 118, "bpm_src",   "Clock",      Section::BPM, 0, COMBO_LUT(kClockNames, 2, kMidpointLUT) },
    { 119, "bpm_tempo", "Int BPM",    Section::BPM, 0, KNOB },
    { 122, "dly_sync",  "Delay Sync", Section::BPM, 0, COMBO(kTimingNames, 12) },

    // === STEP SEQUENCER ===
    { 2,   "seq_enable",    "Enable",     Section::STEP_SEQ, 0,   TOGGLE(kOnOffNames, 2) },
    { 3,   "seq_steps",     "Steps",      Section::STEP_SEQ, 9,   KNOB, false, 1, 16 },   // D-J6: type:int — displays 1..16, wire on the firmware lattice
    { 4,   "seq_gate",      "Gate",       Section::STEP_SEQ, 0,   KNOB },
    { 5,   "seq_slide",     "Slide",      Section::STEP_SEQ, 0,   KNOB },
    { 6,   "seq_dir",       "Direction",  Section::STEP_SEQ, 0,   COMBO_LUT(kSeqDirNames, 4, kSeqDirLUT) },
    { 7,   "seq_rate",      "Rate",       Section::STEP_SEQ, 0,   KNOB },
    { 8,   "seq_depth",     "Depth",      Section::STEP_SEQ, 0,   KNOB },
    { 9,   "seq_dest",      "Destination",Section::STEP_SEQ, 0,   COMBO_LUT(kSeqDestNames, 5, kSeqDestLUT) },
    { 13,  "seq_retrigger", "Retrigger",  Section::STEP_SEQ, 0,   TOGGLE(kOnOffNames, 2) },
    { 116, "seq_sync",      "Sync",       Section::STEP_SEQ, 0,   COMBO(kTimingNames, 12) },
    { 17,  "seq_step_sel",  "Step Grid",  Section::STEP_SEQ, 0,   ControlStyle::StepGrid, nullptr, 0, nullptr },

    // === ARPEGGIATOR (Phase 9) ===========================================
    // Nine GLOBAL params on out-of-band pseudo-CCs 205..213 (JtBridgeExt maps
    // each onto its firmware ParamID 0x0880..0x0888 over NRPN).  The per-step
    // pattern (on/off + accent + ratchet) is NOT here — it's the tri-lane
    // ArpPatternGrid, sent direct-by-ParamID via NrpnLink::ArpParamId, exactly
    // like the seq aux lane.  defVal for combos/toggles = choice INDEX; for
    // knobs = CC value 0..127.
    //   rate: choice 7 == "1/16" (trance staple).  octaves: choice 0 == "1".
    //   gate/swing/free: CC 64 == 0.5 norm.  steps: int-lattice 1..16, def 16.
    { 205, "arp_enable",   "Enable",   Section::ARP, 0,  TOGGLE(kOnOffNames, 2) },
    { 206, "arp_mode",     "Mode",     Section::ARP, 0,  COMBO(kArpModeNames, 7) },
    { 207, "arp_octaves",  "Octaves",  Section::ARP, 0,  COMBO(kArpOctNames, 4) },
    { 208, "arp_latch",    "Latch",    Section::ARP, 0,  TOGGLE(kOnOffNames, 2) },
    { 209, "arp_rate",     "Rate",     Section::ARP, 7,  COMBO(kTimingNames, 12) },
    { 210, "arp_free",     "Free Hz",  Section::ARP, 64, KNOB },
    { 211, "arp_gate",     "Gate",     Section::ARP, 64, KNOB },
    { 212, "arp_swing",    "Swing",    Section::ARP, 64, KNOB },
    { 213, "arp_steps",    "Steps",    Section::ARP, 16, KNOB, false, 1, 16 },   // type:int 1..16 (D-J6 lattice)
    // Tri-lane pattern grid sentinel (no CC/APVTS — sent direct-by-ParamID via
    // ArpPatternGrid -> sendArpStepEdit).  cc 214 is a placeholder key never put
    // on the wire; buildSections special-cases ControlStyle::ArpGrid.
    { 214, "arp_grid",     "Pattern",  Section::ARP, 0,  ControlStyle::ArpGrid, nullptr, 0, nullptr },

    // === PERFORMANCE GLOBAL — local-only (not sent as MIDI) ===============
    // These seven entries have isLocalOnly = true. All upstream code paths
    // (parameterChanged, buildPacedQueue) check this flag and skip emitting
    // MIDI. They exist only so APVTS state save/restore works and the
    // editor header can bind UI controls to them.
    // defVal for combos is the CHOICE INDEX (not raw CC value):
    //   perf_mode:        0 = Single
    //   perf_edit_target: 0 = Layer A
    //   perf_midi_ch_a/b: 0 = Ch 1
    // ======================================================================
    { 140, "perf_mode",         "Perf Mode",    Section::PERFORMANCE_GLOBAL, 0,  COMBO_LUT(kPerfModeNames,   3,  kPerfModeLUT),   true },
    { 141, "perf_voice_split",  "Voice Split",  Section::PERFORMANCE_GLOBAL, 3,  COMBO(kVoiceSplitNames, 7),                        true },   // D-J5: def "4+4"
    { 142, "perf_split_note",   "Split Note",   Section::PERFORMANCE_GLOBAL, 60, KNOB,                                              true },
    { 143, "perf_balance",      "Layer Bal",    Section::PERFORMANCE_GLOBAL, 1,  COMBO(kLayerBalNames, 3),                          true },   // D-J5: def "AB"
    { 144, "perf_edit_target",  "Edit Target",  Section::PERFORMANCE_GLOBAL, 0,  COMBO_LUT(kEditTargetNames, 3,  kEditTargetLUT), true },
    { 145, "perf_midi_ch_a",    "Ch A",         Section::PERFORMANCE_GLOBAL, 0,  COMBO_LUT(kChannelNames,    16, kMidiChannelLUT),true },
    { 146, "perf_midi_ch_b",    "Ch B",         Section::PERFORMANCE_GLOBAL, 1,  COMBO_LUT(kChannelNames,    16, kMidiChannelLUT),true },   // D-J5: canonical def Ch 2

    // === Master (J1c) — canonical master.volume 0x0800, the one v2-only
    // param.  Keyed by PSEUDO-cc 200 (JtBridgeExt maps it onto the NRPN
    // wire): 7 was NOT available — v1 CC 7 is seq.rate; the yaml's curated
    // `cc: 7` lives in the firmware's real-MIDI namespace, not this table.
    // >127 also guarantees no plain hardware CC can ever alias this row.
    // defVal 102 == round(0.8 * 127), the canonical default.
    { 200, "master_volume",     "Master Vol",   Section::PERFORMANCE_GLOBAL, 102, KNOB,                                             true },

    // USB audio return level — the v2 "synth as soundcard" monitor path.
    // Global, v2-only (canonical ParamID 0x0801, no v1 CC), so keyed by
    // PSEUDO-cc 204 (JtBridgeExt maps it onto the NRPN wire, same scheme as
    // master_volume's 200).  >127 guarantees no plain hardware CC can alias it.
    // isLocalOnly = true: it is global output setup, not patch data, so Init
    // Patch preserves it.  defVal 127 == round(1.0 * 127), the canonical
    // default (return at unity — the firmware mixer sums it under the synth).
    { 204, "usb_return_level",  "USB Return",   Section::PERFORMANCE_GLOBAL, 127, KNOB,                                             true },
};

#undef KNOB
#undef VSLIDER
#undef COMBO
#undef COMBO_LUT
#undef TOGGLE
#undef TOGGLE_LUT

static constexpr int kNumParams = sizeof(kAllParams) / sizeof(kAllParams[0]);

// -----------------------------------------------------------------------
// Lookup helpers
// -----------------------------------------------------------------------
inline const CCParam* findParamByCC(int cc)
{
    for (int i = 0; i < kNumParams; ++i)
        if (kAllParams[i].cc == cc) return &kAllParams[i];
    return nullptr;
}

inline const CCParam* findParamByID(const juce::String& id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (id == kAllParams[i].id) return &kAllParams[i];
    return nullptr;
}