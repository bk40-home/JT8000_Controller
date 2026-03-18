/*
 * CCDefinitions.h — JT-8000 CC Parameter Registry
 *
 * Single source of truth mirroring CCDefs.h from the Teensy firmware.
 *
 * Every CC parameter lives in this table.  The processor, editor, and
 * section components all read from it — add/remove/reorder a CC by
 * changing ONE line here.
 *
 * ControlStyle determines the UI widget:
 *   Knob     — rotary arc knob (default for continuous params)
 *   Slider   — vertical fader with scale marks (ADSR, depth, etc.)
 *   Combo    — dropdown list (enum params with >2 options)
 *   Toggle   — on/off checkbox (2-option enum)
 *
 * sectionRow groups sections onto the same horizontal row in the editor.
 * Sections sharing the same non-negative row number are laid out
 * side-by-side.  Use -1 for "own row" (the default).
 */

#pragma once
#include <juce_core/juce_core.h>

// -----------------------------------------------------------------------
// Section identifiers — order here determines display order in the editor
// -----------------------------------------------------------------------
enum class Section : int
{
    OSC1 = 0, OSC2, MIXER, FILTER,
    AMP_ENV, FILTER_ENV, PITCH_ENV,
    LFO1, LFO2,
    FX_EQ, FX_MOD, FX_MOD_TUNE, FX_DELAY, FX_REVERB, FX_OUTPUT,
    VELOCITY, PERFORMANCE, BPM,
    STEP_SEQ,
    NUM_SECTIONS
};

// Display names — must match Section enum order exactly
static const char* sectionNames[] = {
    "Oscillator 1", "Oscillator 2", "Mixer", "Filter",
    "Amp Envelope", "Filter Envelope", "Pitch Envelope",
    "LFO 1", "LFO 2",
    "EQ & Drive", "Mod FX", "Mod Tuning", "Delay", "Reverb", "Output Mix",
    "Velocity", "Performance", "BPM Clock",
    "Step Sequencer"
};

// Row grouping — sections with the same non-negative value share a row.
// Use -1 to place a section on its own row.
static const int sectionRow[] = {
    -1,     // OSC1         — own row
    -1,     // OSC2         — own row
    -1,     // MIXER        — own row
    -1,     // FILTER       — own row
     0,     // AMP_ENV      — row 0 (envelopes side-by-side)
     0,     // FILTER_ENV   — row 0
     0,     // PITCH_ENV    — row 0
    -1,     // LFO1         — own row
    -1,     // LFO2         — own row
    -1,     // FX_EQ        — own row
    -1,     // FX_MOD       — own row
    -1,     // FX_MOD_TUNE  — own row
    -1,     // FX_DELAY     — own row
    -1,     // FX_REVERB    — own row
     1,     // FX_OUTPUT    — row 1 (small sections side-by-side)
     1,     // VELOCITY     — row 1
     1,     // PERFORMANCE  — row 1 (gets wider due to more controls)
    -1,     // BPM          — own row
    -1,     // STEP_SEQ     — own row
};

// -----------------------------------------------------------------------
// Control style — determines which UI widget is created
// -----------------------------------------------------------------------
enum class ControlStyle : int
{
    Knob = 0,   // Rotary arc knob (continuous 0-127)
    Slider,     // Vertical fader with scale marks (ADSR, depth)
    Combo,      // Dropdown list (enum params)
    Toggle      // On/off checkbox (2-option enum)
};

// -----------------------------------------------------------------------
// Parameter definition
// -----------------------------------------------------------------------
struct CCParam
{
    int               cc;           // MIDI CC number (must match Teensy CCDefs.h)
    const char*       id;           // Unique JUCE parameter ID (no spaces)
    const char*       name;         // User-facing label
    Section           section;      // Which collapsible panel this belongs to
    int               defVal;       // Default value (0-127)
    ControlStyle      style;        // UI widget type
    const char* const* enumNames;   // Named options (nullptr for continuous)
    int               enumCount;    // Number of enum options (0 for continuous)
};

// -----------------------------------------------------------------------
// Named option arrays for enum parameters
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
    "Free","1/16","1/8T","1/8","1/4T","1/4","1/2T","1/2","1 bar","2 bar","3 bar","4 bar"
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
static const char* const kModTapNames[]  = { "1 Tap","3 Tap (Ensemble)" };
static const char* const kModLfoNames[]  = { "Triangle (BBD)","Sine (Smooth)" };
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

// Shorthand macros for readability in the parameter table
#define KNOB    ControlStyle::Knob,   nullptr, 0
#define VSLIDER ControlStyle::Slider, nullptr, 0
#define COMBO(names, n)  ControlStyle::Combo, names, n
#define TOGGLE(names, n) ControlStyle::Combo, names, n

// -----------------------------------------------------------------------
// Master parameter table — one entry per CC
// -----------------------------------------------------------------------
static const CCParam kAllParams[] =
{
    // === OSC 1 ===
    { 21,  "osc1_wave",      "OSC1 Wave",       Section::OSC1,     0,   COMBO(kWaveNames, 14) },
    { 41,  "osc1_pitch",     "OSC1 Pitch",      Section::OSC1,     65,  KNOB },
    { 45,  "osc1_fine",      "OSC1 Fine",       Section::OSC1,     64,  KNOB },
    { 43,  "osc1_detune",    "OSC1 Detune",     Section::OSC1,     65,  KNOB },
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
    { 42,  "osc2_pitch",     "OSC2 Pitch",      Section::OSC2,     65,  KNOB },
    { 46,  "osc2_fine",      "OSC2 Fine",       Section::OSC2,     64,  KNOB },
    { 44,  "osc2_detune",    "OSC2 Detune",     Section::OSC2,     65,  KNOB },
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

    // === FILTER ===
    { 23,  "flt_cut",        "Cutoff",          Section::FILTER,   127, KNOB },
    { 24,  "flt_res",        "Resonance",       Section::FILTER,   0,   KNOB },
    { 48,  "flt_env",        "Flt Env Amt",     Section::FILTER,   65,  KNOB },
    { 50,  "flt_key",        "Key Track",       Section::FILTER,   65,  KNOB },
    { 84,  "flt_oct",        "Oct Ctrl",        Section::FILTER,   0,   KNOB },
    { 113, "flt_engine",     "Engine",          Section::FILTER,   0,   COMBO(kFltEngNames, 2) },
    { 112, "flt_mode",       "OBXa Mode",       Section::FILTER,   0,   COMBO(kFltModeNames, 6) },
    { 115, "va_type",        "VA Type",         Section::FILTER,   0,   COMBO(kVaTypeNames, 13) },
    { 111, "flt_multi",      "Multimode",       Section::FILTER,   0,   KNOB },
    { 117, "flt_resmod",     "Res Mod",         Section::FILTER,   0,   KNOB },
    { 114, "flt_xp",         "Xpander Mode",    Section::FILTER,   0,   COMBO(kXpModeNames, 15) },

    // === AMP ENV (vertical sliders for ADSR) ===
    { 25,  "amp_a",  "Atk",   Section::AMP_ENV,    0,   VSLIDER },
    { 26,  "amp_d",  "Dec",   Section::AMP_ENV,    0,   VSLIDER },
    { 27,  "amp_s",  "Sus",   Section::AMP_ENV,    127, VSLIDER },
    { 28,  "amp_r",  "Rel",   Section::AMP_ENV,    0,   VSLIDER },

    // === FILTER ENV (vertical sliders for ADSR) ===
    { 29,  "fe_a",   "Atk",   Section::FILTER_ENV, 0,   VSLIDER },
    { 30,  "fe_d",   "Dec",   Section::FILTER_ENV, 0,   VSLIDER },
    { 31,  "fe_s",   "Sus",   Section::FILTER_ENV, 127, VSLIDER },
    { 32,  "fe_r",   "Rel",   Section::FILTER_ENV, 0,   VSLIDER },

    // === PITCH ENV (vertical sliders + knob for depth) ===
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
    { 99,  "fx_bass",   "Bass",    Section::FX_EQ, 64, KNOB },
    { 100, "fx_treble", "Treble",  Section::FX_EQ, 64, KNOB },
    { 16,  "fx_drive",  "Drive",   Section::FX_EQ, 0,  COMBO(kDriveNames, 3) },

    // === FX MOD ===
    { 103, "fx_mod_type", "Mod Type",  Section::FX_MOD, 0, COMBO(kModFxNames, 12) },
    { 104, "fx_mod_mix",  "Mod Mix",   Section::FX_MOD, 0, KNOB },
    { 105, "fx_mod_rate", "Mod Rate",  Section::FX_MOD, 0, KNOB },
    { 106, "fx_mod_fb",   "Mod FB",    Section::FX_MOD, 0, KNOB },

    // === FX MOD TUNE ===
    { 95,  "fx_mod_depth", "Mod Depth",     Section::FX_MOD_TUNE, 4,  KNOB },
    { 96,  "fx_mod_base",  "Mod Base Dly",  Section::FX_MOD_TUNE, 13, KNOB },
    { 97,  "fx_mod_taps",  "Mod Taps",      Section::FX_MOD_TUNE, 0,  COMBO(kModTapNames, 2) },
    { 98,  "fx_mod_lfo",   "Mod LFO",       Section::FX_MOD_TUNE, 0,  COMBO(kModLfoNames, 2) },

    // === FX DELAY ===
    { 107, "fx_dly_type", "Delay Type",  Section::FX_DELAY, 0, COMBO(kDelayFxNames, 6) },
    { 110, "fx_dly_time", "Delay Time",  Section::FX_DELAY, 0, KNOB },
    { 108, "fx_dly_mix",  "Delay Mix",   Section::FX_DELAY, 0, KNOB },
    { 109, "fx_dly_fb",   "Delay FB",    Section::FX_DELAY, 0, KNOB },

    // === FX REVERB ===
    { 70,  "fx_rv_size",   "Rev Size",    Section::FX_REVERB, 0, KNOB },
    { 71,  "fx_rv_damp",   "Rev HiDamp",  Section::FX_REVERB, 0, KNOB },
    { 93,  "fx_rv_lodamp", "Rev LoDamp",  Section::FX_REVERB, 0, KNOB },
    { 75,  "fx_rv_mix",    "Rev Mix",     Section::FX_REVERB, 0, KNOB },
    { 94,  "fx_rv_bypass", "Rev Bypass",  Section::FX_REVERB, 0, COMBO(kBypassNames, 2) },

    // === FX OUTPUT ===
    { 74,  "fx_dry",  "Dry Mix",  Section::FX_OUTPUT, 127, KNOB },
    { 76,  "fx_jpfx", "JPFX Mix", Section::FX_OUTPUT, 127, KNOB },

    // === VELOCITY ===
    { 10,  "vel_amp", "Vel Amp",    Section::VELOCITY, 0, KNOB },
    { 11,  "vel_flt", "Vel Filter", Section::VELOCITY, 0, KNOB },
    { 12,  "vel_env", "Vel Env",    Section::VELOCITY, 0, KNOB },

    // === PERFORMANCE ===
    { 81,  "glide_on",   "Glide",        Section::PERFORMANCE, 0,   COMBO(kOnOffNames, 2) },
    { 82,  "glide_time", "Glide Time",   Section::PERFORMANCE, 0,   KNOB },
    { 127, "bend_range", "Bend Range",   Section::PERFORMANCE, 12,  KNOB },
    { 90,  "amp_level",  "Amp Level",    Section::PERFORMANCE, 127, KNOB },
    { 14,  "poly_mode",  "Voice Mode",   Section::PERFORMANCE, 21,  COMBO(kPolyNames, 3) },
    { 15,  "uni_det",    "Unison Det",   Section::PERFORMANCE, 0,   KNOB },

    // === BPM ===
    { 118, "bpm_src",   "Clock",      Section::BPM, 0, COMBO(kClockNames, 2) },
    { 119, "bpm_tempo", "Int BPM",    Section::BPM, 0, KNOB },
    { 122, "dly_sync",  "Delay Sync", Section::BPM, 0, COMBO(kTimingNames, 12) },

    // === STEP SEQUENCER ===
    { 2,   "seq_enable",    "Enable",     Section::STEP_SEQ, 0,   COMBO(kOnOffNames, 2) },
    { 3,   "seq_steps",     "Steps",      Section::STEP_SEQ, 64,  KNOB },
    { 4,   "seq_gate",      "Gate",       Section::STEP_SEQ, 127, KNOB },
    { 5,   "seq_slide",     "Slide",      Section::STEP_SEQ, 0,   KNOB },
    { 6,   "seq_dir",       "Direction",  Section::STEP_SEQ, 0,   COMBO(kSeqDirNames, 4) },
    { 7,   "seq_rate",      "Rate",       Section::STEP_SEQ, 0,   KNOB },
    { 8,   "seq_depth",     "Depth",      Section::STEP_SEQ, 0,   KNOB },
    { 9,   "seq_dest",      "Destination",Section::STEP_SEQ, 0,   COMBO(kSeqDestNames, 5) },
    { 13,  "seq_retrigger", "Retrigger",  Section::STEP_SEQ, 127, COMBO(kOnOffNames, 2) },
    { 116, "seq_sync",      "Sync",       Section::STEP_SEQ, 0,   COMBO(kTimingNames, 12) },
    { 17,  "seq_step_sel",  "Step Sel",   Section::STEP_SEQ, 0,   KNOB },
    { 18,  "seq_step_val",  "Step Val",   Section::STEP_SEQ, 64,  KNOB },
};

// Undefine table macros — they are only for use inside kAllParams[]
#undef KNOB
#undef VSLIDER
#undef COMBO
#undef TOGGLE

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
