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
 *   2026-03-31 — CRITICAL FIX: toggles now send 0/127 (not bucket midpoint 32)
 *              — Added ccLUT field to CCParam for firmware-specific thresholds
 *              — Fixed sectionRow[] missing comma (18 entries, was 17)
 *              — Fixed pitch/detune defVals 65→64 (firmware special-cases CC 64)
 *              — Added CC 71 (Hi Damp) and CC 93 (Lo Damp) to reverb section
 *              — Seq defaults match HTML editor (all zero)
 *              — EQ defaults match HTML editor (0, not 64)
 */

#pragma once
#include <juce_core/juce_core.h>

// -----------------------------------------------------------------------
// Section identifiers — order determines display order in the editor
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
    "Step Sequencer"
};

// Row grouping for side-by-side layout. -1 = full width.
// Sections sharing the same non-negative row number are placed side by side.
static const int sectionRow[] = {
    -1, -1, -1, -1,        // OSC1, OSC2, MIXER, FILTER
     0,  0,  0,            // AMP_ENV, FILTER_ENV, PITCH_ENV
    -1, -1,                // LFO1, LFO2
     1,  1,                // FX_EQ, FX_MOD
     2,  2,                // FX_DELAY, FX_REVERB
     3,  3,  3,            // FX_OUTPUT, VELOCITY, PERFORMANCE
    -1, -1,                // BPM, STEP_SEQ
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
    StepGrid
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
};

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

    // Bucket midpoint — works for params where firmware uses (v*N)/128
    return ((2 * index + 1) * 128) / (2 * numChoices);
}

/* Convert a raw CC value (0..127) to a choice index (0..N-1) */
inline int ccToChoiceIndex(int ccVal, int numChoices)
{
    if (numChoices <= 0) return 0;
    int idx = (ccVal * numChoices) / 128;
    return (idx >= numChoices) ? numChoices - 1 : idx;
}

/* Check if a param uses AudioParameterChoice (Combo or Toggle) */
inline bool isChoiceParam(const CCParam& p)
{
    return p.style == ControlStyle::Combo || p.style == ControlStyle::Toggle;
}

// -----------------------------------------------------------------------
// Named option arrays
// -----------------------------------------------------------------------
static const char* const kWaveNames[] = {
    "Sine","Sawtooth","Square","Triangle","Arbitrary","Pulse",
    "Saw Rev","S&H","Tri Var","BL Saw","BL Saw Rev","BL Square","BL Pulse","Supersaw"
};
static const char* const kLfoWaveNames[] = {
    "Sine","Sawtooth","Square","Triangle","Arbitrary","Pulse",
    "Saw Rev","S&H","Tri Var","BL Saw","BL Saw Rev","BL Square","BL Pulse"
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
static const char* const kVaTypeNames[] = {
    "SVF LP2","SVF HP2","SVF BP2","SVF Notch","SVF AP",
    "Moog LP4","Moog LP2","Moog BP2","Diode LP4","Korg35 LP","Korg35 HP","TPT1 LP","TPT1 HP"
};
static const char* const kXpModeNames[] = {
    "LP4","LP3","LP2","LP1","HP3","HP2","HP1","BP4","BP2",
    "N2","PH3","HP2+LP1","HP3+LP1","N2+LP1","PH3+LP1"
};
static const char* const kClockNames[]   = { "Internal","External" };
static const char* const kPolyNames[]    = { "Poly","Mono","Unison" };
static const char* const kOnOffNames[]   = { "Off","On" };
static const char* const kBypassNames[]  = { "Active","Bypass" };
static const char* const kArbBankNames[] = {
    "BwBlended","BwPerfect","BwSaw","BwSawBright","BwSawGap",
    "BwSawRound","BwSin","BwSqu","BwSquRound","BwTri"
};
static const char* const kSeqDirNames[]  = { "Forward","Reverse","Bounce","Random" };
static const char* const kSeqDestNames[] = { "None","Pitch","Filter","PWM","Amp" };

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
    { 41,  "osc1_pitch",     "OSC1 Pitch",      Section::OSC1,     64,  KNOB },
    { 45,  "osc1_fine",      "OSC1 Fine",       Section::OSC1,     64,  KNOB },
    { 43,  "osc1_detune",    "OSC1 Detune",     Section::OSC1,     64,  KNOB },
    { 77,  "osc1_ss_det",    "SS1 Detune",      Section::OSC1,     0,   KNOB },
    { 78,  "osc1_ss_mix",    "SS1 Mix",         Section::OSC1,     0,   KNOB },
    { 123, "osc1_fb_amt",    "OSC1 FB Amt",     Section::OSC1,     0,   KNOB },
    { 125, "osc1_fb_mix",    "OSC1 FB Mix",     Section::OSC1,     0,   KNOB },
    { 86,  "osc1_freq_dc",   "OSC1 Freq DC",    Section::OSC1,     0,   KNOB },
    { 87,  "osc1_shape_dc",  "OSC1 Shape DC",   Section::OSC1,     0,   KNOB },
    { 91,  "osc1_ring",      "Ring 1",          Section::OSC1,     0,   KNOB },
    { 101, "osc1_arb_bank",  "OSC1 Bank",       Section::OSC1,     0,   COMBO(kArbBankNames, 10) },
    { 83,  "osc1_arb_idx",   "OSC1 Table",      Section::OSC1,     0,   KNOB },

    // === OSC 2 ===
    { 22,  "osc2_wave",      "OSC2 Wave",       Section::OSC2,     0,   COMBO(kWaveNames, 14) },
    { 42,  "osc2_pitch",     "OSC2 Pitch",      Section::OSC2,     64,  KNOB },
    { 46,  "osc2_fine",      "OSC2 Fine",       Section::OSC2,     64,  KNOB },
    { 44,  "osc2_detune",    "OSC2 Detune",     Section::OSC2,     64,  KNOB },
    { 79,  "osc2_ss_det",    "SS2 Detune",      Section::OSC2,     0,   KNOB },
    { 80,  "osc2_ss_mix",    "SS2 Mix",         Section::OSC2,     0,   KNOB },
    { 124, "osc2_fb_amt",    "OSC2 FB Amt",     Section::OSC2,     0,   KNOB },
    { 126, "osc2_fb_mix",    "OSC2 FB Mix",     Section::OSC2,     0,   KNOB },
    { 88,  "osc2_freq_dc",   "OSC2 Freq DC",    Section::OSC2,     0,   KNOB },
    { 89,  "osc2_shape_dc",  "OSC2 Shape DC",   Section::OSC2,     0,   KNOB },
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
    { 20,  "osc_sync",       "Osc Sync",        Section::MIXER,    0,   TOGGLE(kOnOffNames, 2) },

    // === FILTER ===
    { 23,  "flt_cut",        "Cutoff",          Section::FILTER,   127, KNOB },
    { 24,  "flt_res",        "Resonance",       Section::FILTER,   0,   KNOB },
    { 48,  "flt_env",        "Flt Env Amt",     Section::FILTER,   64,  KNOB },
    { 50,  "flt_key",        "Key Track",       Section::FILTER,   64,  KNOB },
    { 84,  "flt_oct",        "Oct Ctrl",        Section::FILTER,   0,   KNOB },
    { 113, "flt_engine",     "Engine",          Section::FILTER,   1,   COMBO_LUT(kFltEngNames, 2, kMidpointLUT) },
    { 112, "flt_mode",       "OBXa Mode",       Section::FILTER,   0,   COMBO(kFltModeNames, 6) },
    { 115, "va_type",        "VA Type",         Section::FILTER,   0,   COMBO(kVaTypeNames, 13) },
    { 111, "flt_multi",      "Multimode",       Section::FILTER,   0,   KNOB },
    { 117, "flt_resmod",     "Res Mod",         Section::FILTER,   0,   KNOB },
    { 114, "flt_xp",         "Xpander Mode",    Section::FILTER,   0,   COMBO(kXpModeNames, 15) },

    // === AMP ENV ===
    { 25,  "amp_a",  "Atk",   Section::AMP_ENV,    0,   VSLIDER },
    { 26,  "amp_d",  "Dec",   Section::AMP_ENV,    0,   VSLIDER },
    { 27,  "amp_s",  "Sus",   Section::AMP_ENV,    127, VSLIDER },
    { 28,  "amp_r",  "Rel",   Section::AMP_ENV,    0,   VSLIDER },

    // === FILTER ENV ===
    { 29,  "fe_a",   "Atk",   Section::FILTER_ENV, 0,   VSLIDER },
    { 30,  "fe_d",   "Dec",   Section::FILTER_ENV, 0,   VSLIDER },
    { 31,  "fe_s",   "Sus",   Section::FILTER_ENV, 127, VSLIDER },
    { 32,  "fe_r",   "Rel",   Section::FILTER_ENV, 0,   VSLIDER },

    // === PITCH ENV ===
    { 65,  "pe_a",     "Atk",       Section::PITCH_ENV, 0,   VSLIDER },
    { 66,  "pe_d",     "Dec",       Section::PITCH_ENV, 0,   VSLIDER },
    { 67,  "pe_s",     "Sus",       Section::PITCH_ENV, 0,   VSLIDER },
    { 68,  "pe_r",     "Rel",       Section::PITCH_ENV, 0,   VSLIDER },
    { 69,  "pe_depth", "Depth",     Section::PITCH_ENV, 64,  KNOB },

    // === LFO 1 ===
    { 62,  "l1_wave",  "LFO1 Wave",    Section::LFO1, 0,  COMBO(kLfoWaveNames, 13) },
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
    { 63,  "l2_wave",  "LFO2 Wave",    Section::LFO2, 0,  COMBO(kLfoWaveNames, 13) },
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
    { 70,  "fx_rv_size",   "Rev Size",    Section::FX_REVERB, 0, KNOB },
    { 71,  "fx_rv_damp",   "Hi Damp",     Section::FX_REVERB, 0, KNOB },
    { 93,  "fx_rv_lodamp", "Lo Damp",     Section::FX_REVERB, 0, KNOB },
    { 75,  "fx_rv_mix",    "Rev Mix",     Section::FX_REVERB, 0, KNOB },
    { 94,  "fx_rv_bypass", "Rev Bypass",  Section::FX_REVERB, 0, COMBO_LUT(kBypassNames, 2, kMidpointLUT) },

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
    { 3,   "seq_steps",     "Steps",      Section::STEP_SEQ, 0,   KNOB },
    { 4,   "seq_gate",      "Gate",       Section::STEP_SEQ, 0,   KNOB },
    { 5,   "seq_slide",     "Slide",      Section::STEP_SEQ, 0,   KNOB },
    { 6,   "seq_dir",       "Direction",  Section::STEP_SEQ, 0,   COMBO_LUT(kSeqDirNames, 4, kSeqDirLUT) },
    { 7,   "seq_rate",      "Rate",       Section::STEP_SEQ, 0,   KNOB },
    { 8,   "seq_depth",     "Depth",      Section::STEP_SEQ, 0,   KNOB },
    { 9,   "seq_dest",      "Destination",Section::STEP_SEQ, 0,   COMBO_LUT(kSeqDestNames, 5, kSeqDestLUT) },
    { 13,  "seq_retrigger", "Retrigger",  Section::STEP_SEQ, 0,   TOGGLE(kOnOffNames, 2) },
    { 116, "seq_sync",      "Sync",       Section::STEP_SEQ, 0,   COMBO(kTimingNames, 12) },
    { 17,  "seq_step_sel",  "Step Grid",  Section::STEP_SEQ, 0,   ControlStyle::StepGrid, nullptr, 0, nullptr },
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
