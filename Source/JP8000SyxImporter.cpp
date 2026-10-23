/*
 * JP8000SyxImporter.cpp — JP-8000/8080 SysEx Import Implementation
 *
 * ROLAND DT1 MESSAGE LAYOUT (recap):
 *   F0 41 <dev> 00 06 12 <a0> <a1> <a2> <a3> <data...> <checksum> F7
 *
 *   a0 = address space:  01=temp, 02=user patches, 03=user performances
 *   a1 = slot index:     which performance/patch slot (0x00..0x3F)
 *   a2 = message type:   0x00=perf header, 0x10/0x11=tone common,
 *                         0x40=upper patch, 0x42=lower patch
 *   a3 = sub-offset:     always 0x00 for full-block dumps
 *
 * PERFORMANCE HEADER (48-byte message, a2=0x00):
 *   data[0x00..0x0F] = Performance name (16 ASCII chars)
 *   data[0x10]       = Key Mode: 0=Single, 1=Layer/Dual, 2=Split
 *   data[0x11]       = Split Point: MIDI note number 0-127
 *   data[0x13]       = Balance: 0-127 (64=centre)
 *
 * PATCH DATA (252-byte message, a2=0x40 or 0x42):
 *   data[0x00..0x0F] = Patch name (16 ASCII chars)
 *   data[0x10..]     = Patch parameters (offsets match kSlots[] table)
 */

#include "JP8000SyxImporter.h"
#include "PluginProcessor.h"
#include "JtBridgeTable.h"     // JtBridge::paramIdFromV1Cc — CURRENT generated
                               // v1-CC -> 14-bit ParamID bridge (CI-checked)
#include "gen/JtParamTable.h"  // JtParams::kParams — the REAL APVTS rows (dotted juceId)

namespace JP8000SyxImporter
{

// =========================================================================
// CC -> real APVTS parameter bridge  (Phase J1 rename fix)
//
// The APVTS is built from JtParams::kParams (gen/JtParamTable.h), keyed by
// DOTTED juceId ("osc1.wave").  The importer's slot/default tables are keyed
// by MIDI CC number.  The old setParamByCC looked the CC up in CCDefinitions.h
// and called apvts.getParameter(underscore_id) — but no such id exists in the
// APVTS after the underscore->dot rename, so every write silently no-op'd and
// nothing loaded.
//
// The bridge MUST be JtBridgeTable.h (generated from params.yaml, CI-checked):
//   - ParamMapJuce::paramId14ForCC carries the OLD Phase-0 ParamID scheme and
//     resolves only 22/79 importer CCs against today's table — it is stale.
//   - JtBridge::paramIdFromV1Cc is the current seam and resolves 77/79.
//     (The two it doesn't map — CC 72/73 — are not real params in any table;
//      they were vestigial reverb defaults and correctly no-op.)
//
// Returns nullptr for any CC with no mapping OR no matching JtParams row, so
// the caller skips it silently — never writes to a wrong parameter.
// =========================================================================
static const JtParams::Param* jtParamForCC(int cc)
{
    const int32_t pid = JtBridge::paramIdFromV1Cc(static_cast<uint16_t>(cc));
    if (pid < 0) return nullptr;                   // -1 = unmapped v1 CC

    for (int i = 0; i < JtParams::kNumParams; ++i)
        if (JtParams::kParams[i].paramId == static_cast<uint16_t>(pid))
            return &JtParams::kParams[i];

    return nullptr;
}

// =========================================================================
// Reset-to-init helpers  (Delivery 3 — safe defaults)
//
// A JP-8000 import only writes ~77 params; the other ~147 patch-scope params
// keep whatever the PREVIOUS patch left in the APVTS (arp running, sequencer
// playing, stale envelope curves, LFO2, etc.), and sendBankDump then ships
// those stale values to the engine.  The fix: before applying a patch, reset
// the relevant params to their generated default (JtParams::Param::def — the
// single source of truth from params.yaml, same values a fresh plugin uses).
//
// Split by scope so the two-pass performance load stays correct (decision 2,
// signed off 2026-09-14):
//   - Patch scope   : per-layer — reset on EVERY loadPatch pass.
//   - PatchShared    : reverb/FX/seq singletons — reset ONCE before both
//                      passes, or pass 2 (lower) would wipe the upper's
//                      shared FX out of its own bank dump.
// Performance and Global scope are never reset here (master vol, USB/MIDI
// channels, perf mode/split stay put).
//
// The write is type-correct, mirroring setParamByCC: Select/Int/Toggle set
// via convertTo0to1(def); Continuous stores engineering units, and def is in
// engineering units, so convertTo0to1(def) is correct for every type.
// =========================================================================
static void resetScopeToDefaults(juce::AudioProcessorValueTreeState& apvts,
                                 bool patchScope, bool sharedScope)
{
    for (int i = 0; i < JtParams::kNumParams; ++i)
    {
        const JtParams::Param& p = JtParams::kParams[i];

        const bool isPatch  = (p.scope == JtParams::Scope::Patch);
        const bool isShared = (p.scope == JtParams::Scope::PatchShared);
        if (!((patchScope && isPatch) || (sharedScope && isShared)))
            continue;

        if (auto* pi = apvts.getParameter(p.juceId))
            pi->setValueNotifyingHost(pi->convertTo0to1(p.def));
    }
}

/** Reset per-layer (Patch scope) params to their generated defaults. */
static void resetLayerToDefaults(juce::AudioProcessorValueTreeState& apvts)
{
    resetScopeToDefaults(apvts, /*patchScope=*/true, /*sharedScope=*/false);
}

/** Reset singleton (PatchShared scope) params — reverb/FX/seq — to defaults. */
static void resetSharedToDefaults(juce::AudioProcessorValueTreeState& apvts)
{
    resetScopeToDefaults(apvts, /*patchScope=*/false, /*sharedScope=*/true);
}

// =========================================================================
// Waveform / enum remap tables — JP-8000 SysEx enum byte → JT-8000 CHOICE
// INDEX (decision A, Delivery 2b, 2026-09-14).
//
// setParamByCC writes Select params by choice index into the row's option
// list (gen/JtParamTable.h), so every value here is a 0-based INDEX, not a
// CC byte.  Index columns are annotated with the JT option they select so a
// future edit is a one-line change.  JP source order is per the JP-8000 spec;
// any entry marked (?) needs confirming against a real .syx dump.
// =========================================================================

// OSC1 wave — JP order: 0 SuperSaw, 1 Tri-mod, 2 Noise, 3 Feedback,
//                       4 Square,   5 Saw,     6 Triangle
// JT osc1.wave options: 0 SIN,1 SAW,2 SQR,3 TRI,4 ARB,5 PLS,6 rSAW,7 S&H,
//   8 vTRI,9 BLS,10 rBLS,11 BLSQ,12 BLP,13 SSAW,14 vSAW,15 BLvSAW,16 vTRI,17 BLvTRI,
//   18 JvSAW,19 JvTRI,20 JTRIM  (JP-measured, 2026-10-22 — see project doc
//   osc-shapes/jp-osc1-capture-analysis-2026-10-22.md)
//
// BAND-LIMITED TARGETS (2026-10-22, sign-off item 6): JE-8086 captures show the
// JP-8000's OSC1 SAW/SQR/TRI are band-limited (−42..−52 dB alias at C7) and only
// its SUPER SAW aliases.  So the JP waves land on the band-limited JT waves
// (BLP, JvSAW, JvTRI, JTRIM); SSAW stays the naive-saw supersaw, as measured.
// Ctrl1 needs no change for the SHAPE waves: osc<n>.shape_dc = 2·byte/127 − 1
// gives morph = byte/127 — exactly the calibrated knob the JT laws are fitted to.
static constexpr uint8_t kOsc1WaveMap[] = {
    13,   // 0 SuperSaw   -> SSAW   (JP supersaw aliases: naive is faithful)
    20,   // 1 Tri-mod    -> JTRIM  (measured wrap law; was BLvTRI)
     1,   // 2 Noise      -> SAW (neutral; mix.noise raised separately, M9)
     1,   // 3 Feedback   -> SAW (osc1.feedback_* set separately, M10)
    12,   // 4 Square     -> BLP    (band-limited pulse; was naive PLS)
    18,   // 5 Saw        -> JvSAW  (measured closed form since 2026-10-22)
    19,   // 6 Triangle   -> JvTRI  (re-measured 33-frame table, 2026-10-22)
};

// OSC2 wave — JP order: 0 Square, 1 Saw, 2 Triangle, 3 S&H (M12)
// OSC2 SHAPE was NOT in the 2026-10-22 captures (OSC1 only): it is assumed to
// share OSC1's laws (same JP SHAPE control) — confirm with an OSC2 capture.
static constexpr uint8_t kOsc2WaveMap[] = {
    12,   // 0 Square   -> BLP    (band-limited pulse; was naive PLS)
    18,   // 1 Saw      -> JvSAW
    19,   // 2 Triangle -> JvTRI
     7,   // 3 S&H      -> S&H   (M12 — edit here if the JP order differs)
};

// LFO1 wave — JP order (M13): 0 triangle, 1 saw, 2 pulse, 3 S&H
// JT lfo1.waveform options: 0 SIN,1 TRI,2 SAW,3 SQR,4 S&H,5 NOISE
static constexpr uint8_t kLfo1WaveMap[] = {
     1,   // 0 triangle -> TRI
     2,   // 1 saw      -> SAW
     3,   // 2 pulse    -> SQR (pulse ~= square)
     4,   // 3 S&H      -> S&H
};

// Filter type x slope -> JT filter.va_type CHOICE INDEX  (gap audit
// 2026-10-17, signed off).  Authoritative JP byte orders, from the JE-8086
// parameter table (gearmulator parameterDescriptions_je.json):
//   0x27 Filter Type : 0 HPF, 1 BPF, 2 LPF     (the old map assumed LPF,HPF,BPF)
//   0x28 Cutoff Slope: 0 -12 dB, 1 -24 dB      (previously ignored)
// Rows = JP type byte, columns = JP slope byte.
// JT filter.va_type: 0 SVF LP2,1 SVF HP2,2 SVF BP2,...,8 Diode LP4,...,
//                    15 MoogDV HP4,16 MoogDV BP
// The result goes to filter.va_type (CC 115).  The old slot wrote the type to
// CC 112 = filter.mode (the OBXa mode list) — which is why "2P BP" showed.
static constexpr uint8_t kFilterVaTypeMap[3][2] = {
    //  -12 dB        -24 dB
    {   1,            15 },   // 0 HPF -> SVF HP2   | MoogDV HP4
    {   2,            16 },   // 1 BPF -> SVF BP2   | MoogDV BP
    {   0,             8 },   // 2 LPF -> SVF LP2   | Diode LP4 (M15)
};

// Multi-FX type: JP-8000 chorus-type byte -> JT fx.mod_effect enum index.
//
// N3 (2026-10-22, signed off): EXACT mapping.  The firmware now carries the
// twelve JP-8000 Multi-FX types themselves, appended to fx.mod_effect at
// indices 12..23 in the JP dropdown order (JE-8086 table), so JP byte n lands
// on index 12 + n.  This REPLACES the Delivery-2 mapping, which pointed each
// JP type at the nearest v1 preset (Chorus 1-3 / Flanger 1-3 / Phaser 1-4 /
// Super Chorus) because no JP types existed then.
// Provenance: chorus-fx/jp-chorus-capture-gap-2026-10-22.md and
// chorus-fx/delivery-2026-10-22.md.  The JP voicings in FxChain are still
// PROVISIONAL (tuning waits on the noise capture), but the type identity is
// exact.
//
//   JP byte  JP-8000 type          -> JT enum  (JT option label)
//   0        Super Chorus SLW         12        SChorus SLW
//   1        Super Chorus MID         13        SChorus MID
//   2        Super Chorus FST         14        SChorus FST
//   3        Super Chorus CLR         15        SChorus CLR
//   4        Flanger Slow             16        Flanger SLW
//   5        Flanger Deep             17        Flanger DEEP
//   6        Flanger Fast             18        Flanger FST
//   7        Deep Phasing SLW         19        DeepPhs SLW
//   8        Jet Phasing              20        Jet Phasing
//   9        Twisting                 21        Twisting
//   10       Freeze Phase 1           22        Freeze Ph 1
//   11       Freeze Phase 2           23        Freeze Ph 2
//
// MFX level 0 is silent regardless of type (JP behaviour), so no OFF-gating
// is needed.  Any byte > 11 still clamps to the v1 Super Chorus (11) in
// applyTransform (unchanged); byte 12 (JP-8080 DISTORTION) is overridden
// further down.
static constexpr uint8_t kMfxTypeMap[] = {
    12,   // 0  Super Chorus SLW
    13,   // 1  Super Chorus MID
    14,   // 2  Super Chorus FST
    15,   // 3  Super Chorus CLR
    16,   // 4  Flanger Slow
    17,   // 5  Flanger Deep
    18,   // 6  Flanger Fast
    19,   // 7  Deep Phasing SLW
    20,   // 8  Jet Phasing
    21,   // 9  Twisting
    22,   // 10 Freeze Phase 1
    23,   // 11 Freeze Phase 2
};

// Delay type -> JT fx.delay_effect index.
// JT fx.delay_effect table order: 0 OFF, 1 Mono Short, 2 Mono Long,
//                                 3 Pan L>R, 4 Pan R>L, 5 Pan Stereo.
//
// Verified against the JE-8086 reference bank (2026-09-19):
//   JP delay byte 0 -> Pan L>R (3)   [AirwavePadHigh, confirmed by Kris]
// The whole bank uses byte 0, so only that entry is confirmed; the others are
// FIXME placeholders in JP order.  A patch with delay MIX 0 is silent
// regardless of type (JP behaviour), so no OFF-gating is needed.
static constexpr uint8_t kDelayTypeMap[] = {
     3,   // 0 -> Pan L>R      (confirmed)
     4,   // 1 -> Pan R>L      (FIXME: unconfirmed)
     5,   // 2 -> Pan Stereo   (FIXME: unconfirmed)
     1,   // 3 -> Mono Short   (FIXME: unconfirmed)
     2,   // 4 -> Mono Long    (FIXME: unconfirmed)
};

// =========================================================================
// Safe defaults for JT-8000-exclusive parameters
//
// The JP-8000 SysEx doesn't carry values for these — they're either new
// JT-8000 features (VA filter, cross-mod, reverb) or have different
// default assumptions. Applied before any JP-8000 data is mapped.
// =========================================================================
struct DefaultCC { int cc; int val; };
static constexpr DefaultCC kDefaults[] = {
    // Filter engine & topology — J3c import policy (signed off 2026-07-20):
    // every JP-8000 import lands on the DIODE ladder — engine VA Bank +
    // va_type Diode LP4.  Q2c: the JP's -12/-24 dB slope byte (0x28) is
    // ignored (M15 revisits this).  Decision A: these Select defaults are
    // CHOICE INDICES now, not table-A bucket-midpoint bytes:
    //   cc113 filter.engine  -> 1 (VA Bank)      [was 96, clamped]
    //   cc115 filter.va_type -> 8 (Diode LP4)    [was 64 -> MoogDV BP, WRONG]
    //   cc114 filter.obxa_xpander_mode -> 0 (LP4)
    // cc111/117 (obxa multimode / res-mod depth) and cc84 (octave) are
    // Continuous — 0 and 64 stay as raw bytes.
    {113, 1}, {115, 8}, {111, 0}, {114, 0}, {117, 0}, {84, 64},
    // Cross-mod (CC19) & sync (CC20): NOT defaulted here any more.  The JP
    // DOES have both (0x16 X-MOD depth, 0x22 OSC2 sync) — they are now
    // imported in Step 3 (gap audit 2026-10-17).  The old {19,0},{20,0}
    // "JP-8000 has no cross-mod" entries were wrong and zeroed them.
    // Drive — J3c: saturation defaults SOFT.  Decision A: fx.drive is a Select,
    // so this is the choice INDEX 1 (Soft) [was 32 -> Hard, WRONG].  Bass/treble
    // (Continuous) pre-set to 64 (flat); the dump's tone bytes overwrite later.
    {16, 1}, {99, 64}, {100, 64},
    // Reverb — all JT-8000-exclusive
    {72, 0}, {73, 0}, {94, 127},
    {95, 64}, {96, 36}, {97, 0}, {98, 64},
    // Output mix — full dry
    // J3c: the audition path runs entirely through the JPFX chain — dry
    // mix hard 0, JPFX mix full.
    {74, 0}, {76, 127},
    // Oscillator mix — the JP-8000 has no separate osc level knobs: both
    // oscillators run at FULL and OSC BALANCE (0x17 -> mix.balance) crossfades
    // between them.  So both JT levels must be full on import, or moving the
    // balance toward OSC2 produces silence (osc2 level was 0).  mix.balance
    // then carries the JP balance byte via slot 0x17.
    {60, 127}, {61, 127},
    // Sub / noise
    {59, 0}, {58, 0},
    // Unison detune
    {15, 64},
    // Pitch envelope sustain/release — JP-8000 has no pitch env S/R
    {67, 0}, {68, 0},
    // LFO PWM depths — base value 0.  LFO1 PWM (CC35) is raised in Step 3c
    // when a JP SQR oscillator carries a PWM depth (Ctrl2).  LFO2 PWM has no
    // JP equivalent and stays 0.
    {35, 0}, {40, 0},
    // LFO master depths (lfo1.depth CC55 / lfo2.depth CC52): NOT defaulted
    // here.  These are Continuous 0..1 (def 0.0), and the old {55,64},{52,64}
    // ("bipolar centre") pinned both to 0.5 under decision A — pinning every
    // import to half-depth and bleeding LFO2 mod.  resetLayerToDefaults leaves
    // them at 0.0; the engine owns LFO2 depth (per-destination amount, common
    // rate, linked to mod-wheel).
    // LFO2 dest — none
    {53, 0},
    // Step sequencer — off
    {2, 0},
};

// =========================================================================
// Transform types — how a JP-8000 SysEx byte maps to a JT-8000 CC value
// =========================================================================
enum class Xform
{
    Direct,          // raw byte = CC value (most continuous params)
    Bool127,         // 0 → 0, nonzero → 127
    Signed64,        // bipolar with centre at 64 (pass through)
    Signed64ToUni,   // bipolar → unipolar: abs(raw - 64) * 2
    Osc1Wave,        // JP-8000 waveform index → JT-8000 CC value
    Osc2Wave,
    Lfo1Wave,
    MfxType,
    DelayType,
    Osc2Range,       // JP-8000 range encoding → normalised CC
    Osc2Fine,        // JP-8000 fine encoding → normalised CC
    MonoMode         // 0 → Poly (0), nonzero → Mono (64)
};

// =========================================================================
// Slot table — maps JP-8000 SysEx data offset → JT-8000 CC + transform
//
// Each entry: { sysex_offset, jt8000_cc, transform }
// The sysex_offset is relative to the start of the patch data block
// (i.e., the first byte after the SysEx header, which is the patch name).
// =========================================================================
struct Slot { uint8_t offset; int cc; Xform xf; };

static constexpr Slot kSlots[] = {
    // LFO 1
    {0x10, 62, Xform::Lfo1Wave},         // LFO1 waveform
    {0x11, 54, Xform::Direct},            // LFO1 rate
    {0x12, 37, Xform::Direct},            // LFO1 delay time
    {0x13, 51, Xform::Direct},            // LFO2 rate (JP maps this here)
    {0x17, 47, Xform::Signed64},          // Osc balance (bipolar)
    // 0x18 LFO1 & ENV destination (0 OSC1+2, 1 OSC2, 2 X-MOD) is NOT a slot:
    // it drives mix.pitch_mod_dest (M19) in Step 3.  The old {0x18,56} slot
    // wrote it raw into lfo1.destination, which the firmware never reads.
    {0x19, 33, Xform::Signed64ToUni},     // LFO1 → pitch depth
    {0x1A, 38, Xform::Signed64ToUni},     // LFO2 → pitch depth
    {0x1B, 69, Xform::Signed64},          // Pitch envelope depth
    {0x1C, 65, Xform::Direct},            // Pitch envelope attack
    {0x1D, 66, Xform::Direct},            // Pitch envelope decay

    // Oscillators
    {0x1E, 21, Xform::Osc1Wave},         // OSC1 waveform
    {0x21, 22, Xform::Osc2Wave},         // OSC2 waveform
    {0x24, 46, Xform::Osc2Fine},         // OSC2 fine tune

    // Filter
    // 0x22 OSC2 sync and 0x26 OSC2 Ctrl2 are handled in Step 3/3c.
    // 0x27 type + 0x28 slope are read together in Step 3 (kFilterVaTypeMap).
    {0x29, 23,  Xform::Direct},           // Cutoff frequency
    {0x2A, 24,  Xform::Direct},           // Resonance
    {0x2B, 50,  Xform::Signed64},         // Cutoff key follow
    {0x2C, 34,  Xform::Signed64ToUni},    // Filter LFO1 depth
    {0x2D, 39,  Xform::Signed64ToUni},    // Filter LFO2 depth
    {0x2E, 48,  Xform::Signed64},         // Filter envelope depth

    // Filter envelope ADSR
    {0x2F, 29, Xform::Direct},            // Flt Env Attack
    {0x30, 30, Xform::Direct},            // Flt Env Decay
    {0x31, 31, Xform::Direct},            // Flt Env Sustain
    {0x32, 32, Xform::Direct},            // Flt Env Release

    // Amp
    {0x33, 90, Xform::Direct},            // Amp level
    {0x34, 36, Xform::Signed64ToUni},     // Amp LFO1 depth
    {0x35, 57, Xform::Signed64ToUni},     // Amp LFO2 depth

    // Amp envelope ADSR
    {0x36, 25, Xform::Direct},            // Amp Env Attack
    {0x37, 26, Xform::Direct},            // Amp Env Decay
    {0x38, 27, Xform::Direct},            // Amp Env Sustain
    {0x39, 28, Xform::Direct},            // Amp Env Release

    // EQ / effects
    {0x3B, 99,  Xform::Signed64},         // Tone ctrl bass
    {0x3C, 100, Xform::Signed64},         // Tone ctrl treble
    {0x3D, 103, Xform::MfxType},          // Multi-FX type
    {0x3E, 104, Xform::Direct},           // Multi-FX level / mix
    {0x3F, 107, Xform::DelayType},        // Delay type
    // 0x40 Delay time is NOT a simple slot: fx.delay_time is a Log 10..10000 ms
    // param, and the JP time byte scales to a per-type maximum (manual: Pan
    // 625 ms, Short 156 ms, Mono Long 1250 ms).  Sending the raw byte through
    // the log curve gave ~1900 ms for a JP 472 ms echo.  Handled in Step 5.
    {0x41, 109, Xform::Direct},           // Delay feedback
    {0x42, 108, Xform::Direct},           // Delay mix

    // Performance
    {0x43, 127, Xform::Direct},           // Pitch bend range
    {0x45, 81,  Xform::Bool127},          // Glide on/off
    {0x46, 82,  Xform::Direct},           // Glide time
    {0x47, 14,  Xform::MonoMode},         // Mono/Poly mode
};

// =========================================================================
// Internal helpers
// =========================================================================

/** Clamp an integer to the 7-bit CC range 0-127. */
static int clamp7(int v)
{
    return (v < 0) ? 0 : (v > 127) ? 127 : v;
}

/** Apply a JP-8000 → JT-8000 value transform. */
static int applyTransform(Xform xf, uint8_t raw)
{
    switch (xf)
    {
        case Xform::Direct:        return raw;
        case Xform::Bool127:       return (raw != 0) ? 127 : 0;
        case Xform::Signed64:      return raw;
        case Xform::Signed64ToUni: return clamp7(std::abs(static_cast<int>(raw) - 64) * 2);
        case Xform::Osc1Wave:      return (raw < 7)  ? kOsc1WaveMap[raw]   : 1;  // fallback SAW
        case Xform::Osc2Wave:      return (raw < 4)  ? kOsc2WaveMap[raw]   : 1;  // fallback SAW
        case Xform::Lfo1Wave:      return (raw < 4)  ? kLfo1WaveMap[raw]   : 1;  // fallback TRI
        case Xform::MfxType:       return (raw < 12) ? kMfxTypeMap[raw]    : 11; // >11 -> Super Chorus (Kris)
        case Xform::DelayType:     return (raw < 5)  ? kDelayTypeMap[raw]  : 3;  // fallback Pan L>R
        case Xform::Osc2Range:     return clamp7((static_cast<int>(raw) * 127 + 25) / 50);
        case Xform::Osc2Fine:      return clamp7((static_cast<int>(raw) * 127 + 50) / 100);
        case Xform::MonoMode:      return (raw == 0) ? 0 : 64;
    }
    return raw;
}

/** Map JP-8000 OSC1 waveform index → which CC carries Ctrl1.
 *  M8/M18: on the JP-8000 Ctrl1 is the waveSHAPE control for most waves, so it
 *  maps to osc1.shape_dc (CC 87 = shape/PWM DC offset).  SuperSaw is the
 *  exception — its Ctrl1 is detune (CC 77).  JP OSC1 order:
 *  0 SuperSaw, 1 Tri-mod, 2 Noise, 3 Feedback, 4 Square, 5 Saw, 6 Triangle. */
static int osc1Ctrl1CC(uint8_t wave)
{
    switch (wave)
    {
        case 0:              return 77;   // SuperSaw   → SS Detune
        case 1:                           // Tri-mod    → shape/PWM DC
        case 4:                           // Square     → shape/PWM DC
        case 5: case 6:      return 87;   // Saw/Triangle → shape/PWM DC (osc1.shape_dc)
        default:              return 0;    // Noise/Feedback: no Ctrl1 shape
    }
}

/** Map JP-8000 OSC1 waveform index → which CC carries Ctrl2.
 *  JP OSC1 order: 0 SuperSaw, 1 Tri-mod, 2 Noise, 3 Feedback, 4 Square,
 *  5 Saw, 6 Triangle.  SuperSaw Ctrl2 = SS Mix (CC 78). */
static int osc1Ctrl2CC(uint8_t wave)
{
    switch (wave)
    {
        case 0:              return 78;   // SuperSaw → SS Mix
        default:              return 0;    // other waves: Ctrl2 handled elsewhere
    }
}

/** Map JP-8000 OSC2 waveform index → which CC carries Ctrl1.
 *  M18: Ctrl1 → osc2.shape_dc (CC 89 = shape/PWM DC offset).
 *  JP OSC2 order: 0 Square, 1 Saw, 2 Triangle, 3 S&H. */
static int osc2Ctrl1CC(uint8_t wave)
{
    switch (wave)
    {
        case 0: case 1:
        case 2:              return 89;   // Square/Saw/Triangle → shape/PWM DC (osc2.shape_dc)
        default:              return 0;    // S&H: no Ctrl1 shape
    }
}

// =========================================================================
// Name extraction
// =========================================================================

/** Extract a 16-character ASCII name from a buffer, trimming whitespace. */
static juce::String extractName(const uint8_t* buf, int offset, int maxLen, int bufLen)
{
    juce::String name;
    for (int j = 0; j < maxLen && (offset + j) < bufLen; ++j)
    {
        char c = static_cast<char>(buf[offset + j]);
        name += (c >= 0x20 && c <= 0x7E)
              ? juce::String::charToString(c)
              : " ";
    }
    return name.trim();
}

// =========================================================================
// .syx parser — performance-aware, sequence-based grouping
//
// Some editors (e.g., JE-8086) dump all performances to the same slot
// address (a1=0x00) sequentially.  Grouping by a1 alone would collapse
// an entire bank into one entry.
//
// Strategy: walk messages in file order.  Each performance header
// (a2=0x00) starts a new group; subsequent tone common (a2=0x10/0x11),
// upper (a2=0x40), and lower (a2=0x42) messages join the current group.
// If patches appear before any header they become orphans.
// =========================================================================

/** Intermediate structure for grouping messages into performances. */
struct SlotGroup
{
    // Performance header fields (from 48-byte message, a2=0x00)
    bool          hasHeader   = false;
    juce::String  perfName;
    int           mode        = 0;       // 0=Single, 1=Layer, 2=Split
    int           splitPoint  = 60;
    int           balance     = 64;

    // Patch data (from 252-byte messages)
    bool          hasUpper    = false;
    Patch         upper;
    bool          hasLower    = false;
    Patch         lower;
};

static ParsedBank parseSyxBuffer(const uint8_t* buf, size_t len)
{
    // Sequential list of groups — one per performance header encountered.
    // Patches arriving before any header go into orphans directly.
    std::vector<SlotGroup> groups;
    ParsedBank bank;

    // Pointer to the group currently being assembled. nullptr until the
    // first performance header is encountered.
    SlotGroup* current = nullptr;

    size_t i = 0;
    while (i < len)
    {
        // Find next SysEx start
        if (buf[i] != 0xF0) { ++i; continue; }

        size_t f0 = i;
        ++i;

        // Find SysEx end
        while (i < len && buf[i] != 0xF7) ++i;
        if (i >= len) break;

        // Message body: bytes between F0 and F7 (exclusive)
        size_t msgStart = f0 + 1;
        size_t msgLen   = i - msgStart;

        // Validate Roland DT1 header:
        //   [0]=0x41 (Roland), [1]=dev, [2]=0x00, [3]=0x06, [4]=0x12 (DT1)
        if (msgLen >= 10
            && buf[msgStart]     == 0x41
            && buf[msgStart + 2] == 0x00
            && buf[msgStart + 3] == 0x06
            && buf[msgStart + 4] == 0x12)
        {
            const int a0      = buf[msgStart + 5];   // ADDRESS SPACE
            const int a2      = buf[msgStart + 7];   // message type (temp
                                                     // space) or patch NUMBER
                                                     // (user-patch space)
            const int dataOff = static_cast<int>(msgStart + 9);
            const int dataLen = static_cast<int>(msgLen) - 10;

            // J3b (D-J9): in the user-patch bank (a0 == 0x02) every
            // patch-sized message is a standalone patch and a2 is just the
            // slot number — typing by a2 here misread patch #0 as a
            // performance header, dropped 0x10 as "tone common", and
            // kidnapped 0x40/0x42 as upper/lower.  The file-header comment
            // always documented a0; the code now finally reads it.
            if (a0 == 0x02)
            {
                if (dataLen >= 74)
                    bank.orphanPatches.push_back(Patch {
                        extractName(buf, dataOff, 16, static_cast<int>(len)),
                        dataOff, dataLen, buf });
            }
            else if (a2 == 0x00 && dataLen >= 20)
            {
                // ---- Performance header — start a new group ----
                groups.emplace_back();
                current = &groups.back();

                current->hasHeader = true;
                current->perfName  = extractName(buf, dataOff, 16, static_cast<int>(len));

                if (dataOff + 0x14 <= static_cast<int>(len))
                {
                    current->mode       = buf[dataOff + 0x10];
                    current->splitPoint = buf[dataOff + 0x11];
                    current->balance    = buf[dataOff + 0x13];
                }
            }
            else if (a2 == 0x40 && dataLen >= 74)
            {
                // ---- Upper patch ----
                Patch p {
                    extractName(buf, dataOff, 16, static_cast<int>(len)),
                    dataOff, dataLen, buf
                };
                if (current != nullptr)
                {
                    current->hasUpper = true;
                    current->upper    = p;
                }
                else
                {
                    // No header yet — orphan patch
                    bank.orphanPatches.push_back(p);
                }
            }
            else if (a2 == 0x42 && dataLen >= 74)
            {
                // ---- Lower patch ----
                Patch p {
                    extractName(buf, dataOff, 16, static_cast<int>(len)),
                    dataOff, dataLen, buf
                };
                if (current != nullptr)
                {
                    current->hasLower = true;
                    current->lower    = p;
                }
                else
                {
                    bank.orphanPatches.push_back(p);
                }
            }
            else if (dataLen >= 74)
            {
                // ---- Unrecognised type with patch-sized data ----
                // Treat as orphan (e.g., patch-only bank from address
                // space 0x02 where a2 might be 0x00).
                Patch p {
                    extractName(buf, dataOff, 16, static_cast<int>(len)),
                    dataOff, dataLen, buf
                };
                bank.orphanPatches.push_back(p);
            }
            // Tone common messages (a2=0x10, 0x11) are small metadata
            // blocks — we don't extract any params from them, so skip.
        }

        ++i;  // skip past F7
    }

    // ---- Build performances from completed groups ----
    for (auto& slot : groups)
    {
        if (slot.hasHeader && slot.hasUpper)
        {
            // Full performance with at least an upper patch
            Performance perf;
            perf.name       = slot.perfName;
            perf.mode       = static_cast<PerfMode>(juce::jlimit(0, 2, slot.mode));
            perf.splitPoint = juce::jlimit(0, 127, slot.splitPoint);
            perf.balance    = juce::jlimit(0, 127, slot.balance);
            perf.upper      = slot.upper;

            if (slot.hasLower)
            {
                perf.lower           = slot.lower;
                perf.hasDistinctLower = (slot.upper.name != slot.lower.name);
            }
            else
            {
                // No lower message — duplicate upper
                perf.lower           = slot.upper;
                perf.hasDistinctLower = false;
            }

            bank.performances.push_back(std::move(perf));
        }
        else
        {
            // No header — treat patches as orphans
            if (slot.hasUpper) bank.orphanPatches.push_back(slot.upper);
            if (slot.hasLower) bank.orphanPatches.push_back(slot.lower);
        }
    }

    return bank;
}

// =========================================================================
// .mid parser — extracts SysEx events from Standard MIDI Files
//
// MIDI files embed SysEx as F0-type events (status byte 0xF0).
// The SysEx payload starts AFTER the F0 and length VLQ, and ends at F7.
// We reconstruct the full buffer for each embedded SysEx, then feed
// the collection through the same grouping logic as parseSyxBuffer.
// =========================================================================

static uint32_t readVLQ(const uint8_t* buf, size_t limit, size_t& pos)
{
    uint32_t val = 0;
    while (pos < limit)
    {
        uint8_t b = buf[pos++];
        val = (val << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) break;
    }
    return val;
}

static std::vector<uint8_t> extractMidSysEx(const uint8_t* buf, size_t len)
{
    // Extract all embedded SysEx payloads into a flat buffer, wrapping
    // each with F0...F7 so the caller can parse it exactly like a .syx.
    // J3a (D-J8): this vector is RETURNED, not parsed here — the old
    // version parsed it locally and returned Patches pointing into it,
    // a use-after-free that read zeros into most imported patches.
    std::vector<uint8_t> assembled;

    if (len < 14 || buf[0] != 'M' || buf[1] != 'T' || buf[2] != 'h' || buf[3] != 'd')
        return {};

    uint32_t hdrLen = (uint32_t(buf[4]) << 24) | (uint32_t(buf[5]) << 16)
                    | (uint32_t(buf[6]) << 8)  | buf[7];
    uint16_t nTrk   = (uint16_t(buf[10]) << 8) | buf[11];
    size_t pos = 8 + hdrLen;

    for (uint16_t t = 0; t < nTrk && pos + 8 <= len; ++t)
    {
        if (buf[pos] != 'M' || buf[pos + 1] != 'T'
            || buf[pos + 2] != 'r' || buf[pos + 3] != 'k')
            break;

        uint32_t trkLen = (uint32_t(buf[pos + 4]) << 24) | (uint32_t(buf[pos + 5]) << 16)
                        | (uint32_t(buf[pos + 6]) << 8)  | buf[pos + 7];
        size_t trkEnd = pos + 8 + trkLen;
        size_t p = pos + 8;

        while (p < trkEnd && p < len)
        {
            readVLQ(buf, trkEnd, p);  // delta time (discarded)
            if (p >= trkEnd) break;

            uint8_t status = buf[p];

            if (status == 0xF0)
            {
                // SysEx event — length-prefixed, payload starts with
                // manufacturer ID (0x41 for Roland), ends at F7.
                ++p;
                uint32_t msgLen = readVLQ(buf, trkEnd, p);

                if (p + msgLen <= len && msgLen > 0)
                {
                    // Reconstruct a complete F0...F7 message
                    assembled.push_back(0xF0);
                    assembled.insert(assembled.end(), buf + p, buf + p + msgLen);
                    // Ensure F7 terminator (some encoders include it, some don't)
                    if (assembled.back() != 0xF7)
                        assembled.push_back(0xF7);
                }
                p += msgLen;
            }
            else if (status == 0xFF)
            {
                // Meta event — skip
                p += 2;
                p += readVLQ(buf, trkEnd, p);
            }
            else if (status >= 0x80)
            {
                // Channel voice/mode message
                ++p;
                if (status >= 0xC0 && status < 0xE0)
                    p += 1;   // program change / channel pressure = 1 data byte
                else if ((status >= 0x80 && status < 0xC0) || (status >= 0xE0 && status < 0xF0))
                    p += 2;   // note on/off, CC, pitch bend = 2 data bytes
            }
            else
            {
                ++p;  // running status or unknown — skip
            }
        }

        pos = trkEnd;
    }

    return assembled;
}

// =========================================================================
// Public: parseFile
// =========================================================================

ParsedBank parseFile(const uint8_t* buf, size_t len, bool isMidi)
{
    // J3a (D-J8): ONE ownership rule for both formats — the returned bank
    // owns the bytes its Patches point into.  Fill `backing` first, parse
    // it, then move it into the result: vector's heap block survives the
    // move, so every Patch::srcBuffer stays valid for the bank's lifetime.
    std::vector<uint8_t> backing = isMidi
        ? extractMidSysEx(buf, len)
        : std::vector<uint8_t>(buf, buf + len);

    if (backing.empty())
        return {};

    ParsedBank bank = parseSyxBuffer(backing.data(), backing.size());
    bank.backing = std::move(backing);
    return bank;
}

// =========================================================================
// Public: setParamByCC
//
// Writes one imported value into the genuine APVTS row for `cc`.  The value's
// DOMAIN depends on the target row's type (decision A, 2026-09-14):
//
//   Continuous (Knob/Fader) : `value` is a 0..127 wire byte.  The plugin's
//                             value law is linear (norm = byte/127) — the
//                             firmware owns any curve — so this mirrors
//                             sendBankDump()/parameterChanged() exactly.
//   Select   (Combo)        : `value` is a CHOICE INDEX (0..optionCount-1).
//                             Clamped into range, then normalised by the
//                             AudioParameterChoice's own law.
//   Toggle   (Bool)         : `value` is 0/off or nonzero(>=64)/on.
//   Int      (seq steps …)  : `value` is the REAL integer (e.g. 1..16).
//
// The per-enum remap tables (kOsc1WaveMap, kOsc2WaveMap, kLfo1WaveMap,
// kFilterTypeMap, kMfxTypeMap, kDelayTypeMap) now hold JT choice INDICES
// (Delivery 2b), so Select rows land on the intended option directly.  Any
// entry still marked (?) in those tables' comments needs the JP byte order
// confirmed against a real .syx dump.
// =========================================================================

void setParamByCC(juce::AudioProcessorValueTreeState& apvts, int cc, int value)
{
    const JtParams::Param* jp = jtParamForCC(cc);
    if (jp == nullptr) return;                 // CC has no ParamID -> skip

    auto* pi = apvts.getParameter(jp->juceId);
    if (pi == nullptr) return;                 // defensive; should not happen

    float normVal = 0.0f;

    switch (jp->type)
    {
        case JtParams::Type::Select:
        {
            // Decision A: `value` is a choice index. Clamp into the option
            // list so a stale remap byte still lands on a real option.
            const int maxIdx = juce::jmax(0, (int)jp->optionCount - 1);
            normVal = pi->convertTo0to1((float)juce::jlimit(0, maxIdx, value));
            break;
        }
        case JtParams::Type::Toggle:
        {
            // Accept either 0/127 (wire byte) or 0/1 (index): >=64 -> on.
            normVal = pi->convertTo0to1(value >= 64 ? 1.0f : 0.0f);
            break;
        }
        case JtParams::Type::Int:
        {
            // Int-lattice (e.g. seq steps 1..16): value is the real integer.
            // convertTo0to1 clamps to the AudioParameterInt's own range.
            normVal = pi->convertTo0to1((float)value);
            break;
        }
        case JtParams::Type::Continuous:
        default:
        {
            // Linear 7-bit law: 0..127 wire byte -> 0..1 normalised. Same
            // conversion the whole plugin uses (norm01ToWire7 inverse).
            normVal = (float)juce::jlimit(0, 127, value) / 127.0f;
            break;
        }
    }

    pi->beginChangeGesture();
    pi->setValueNotifyingHost(normVal);
    pi->endChangeGesture();
}

// =========================================================================
// Public: loadPatch
//
// Writes a JP-8000 patch into the APVTS. Does NOT send MIDI.
// Steps:
//   1. Apply JT-8000 defaults + J3c import policy (Diode LP4, JPFX-only
//      output path, SOFT saturation, flat EQ pre-set)
//   2. Walk the slot table and apply transforms
//   3. Handle waveform-dependent Ctrl1/Ctrl2 routing
//   4. Handle JP-8000 MFX-type overrides (distortion → drive, etc.)
// =========================================================================

void loadPatch(juce::AudioProcessorValueTreeState& apvts, const Patch& patch)
{
    const uint8_t* buf = patch.srcBuffer;

    // Safe data access with bounds checking
    auto d = [&](int off) -> uint8_t
    {
        int idx = patch.dataOffset + off;
        return (idx >= 0 && idx < patch.dataOffset + patch.dataLen)
             ? buf[idx] : 0;
    };

    // Step 0: reset per-layer (Patch scope) params to their generated
    // defaults so anything this JP patch does NOT set returns to init
    // instead of keeping the previous patch's value (arp/seq/curves/LFO2
    // etc.).  PatchShared (reverb/FX/seq singletons) is reset separately by
    // the caller — once, before any layer pass — so a performance's two
    // passes don't wipe each other's shared FX.
    resetLayerToDefaults(apvts);

    // Step 1: set JT-8000-exclusive defaults (JP-import overrides applied
    // ON TOP of the init reset — VA Bank + Diode LP4, JPFX routing, etc.).
    for (const auto& def : kDefaults)
        setParamByCC(apvts, def.cc, def.val);

    // Cache waveform and MFX type for conditional routing
    uint8_t osc1Wave = d(0x1E);
    uint8_t osc2Wave = d(0x21);
    uint8_t mfxType  = d(0x3D);

    // Step 2: apply all slot transforms
    for (const auto& slot : kSlots)
        setParamByCC(apvts, slot.cc, applyTransform(slot.xf, d(slot.offset)));

    // Step 3: waveform-dependent control routing
    // Some JP-8000 waveforms route Ctrl1/Ctrl2 to different CCs depending
    // on which waveform is selected (e.g., SuperSaw Ctrl1 → detune CC 77,
    // PW Ctrl1 → mix level CC 60).
    // ---- Ring / X-MOD / Sync  (gap audit 2026-10-17, signed off) ----
    // JE-8086 parameter table: 0x15 = Ring switch, 0x16 = Cross-mod depth,
    // 0x22 = OSC2 sync.  Ring and X-MOD are INDEPENDENT controls on the JP.
    // (Supersedes Q3/J3, which treated 0x15/0x16 as one switch+depth pair and
    // wrote the X-MOD depth into osc1.ring_mix — losing X-MOD whenever ring
    // was off, and never setting a true ring level.)

    // X-MOD depth -> mix.cross_mod (CC19, Continuous): linear byte/127.
    // Firmware fit against the JE-8086 was ~0.39; this byte law gives 0.35 for
    // AI AI AI (byte 44) — close; A/B to confirm before adding any scale.
    setParamByCC(apvts, 19, d(0x16));

    // OSC2 sync -> mix.osc_sync (CC20, Toggle: >=64 = on).  Firmware already
    // follows the JP topology: OSC1 master, OSC2 slave.
    setParamByCC(apvts, 20, d(0x22) != 0 ? 127 : 0);

    // Ring (option a — import-only bake).  On the JP the ring product
    // (OSC1 x OSC2) REPLACES OSC2 in the mix, and OSC BALANCE crossfades OSC1
    // against the ring.  The JT adds ring ON TOP with its own gain and no
    // balance term, so on import:
    //   - mix.osc2 (CC61) = 0  : OSC2 still runs as the ring modulator
    //                            (firmware need2 stays true via ringG) but is
    //                            not heard directly;
    //   - osc2.ring_mix (CC92) = the OSC2-side balance gain the JT applies,
    //     computed from the SAME byte law setParamByCC uses for mix.balance
    //     (norm = byte/127 -> -1..1), so ring sits where OSC2 would have.
    // Limitation: the ring level is fixed at load; moving BALANCE afterwards
    // does not track it (option b = firmware mode, deferred).
    // osc1.ring_mix (CC91) stays at its reset default 0 — firmware SUMS the
    // two ring gains, so only one carries the level.
    if (d(0x15) != 0)
    {
        const float bal   = (float)juce::jlimit(0, 127, (int)d(0x17)) / 127.0f * 2.0f - 1.0f;
        const float balG2 = (bal < 0.0f) ? (1.0f + bal) : 1.0f;   // firmware balance law
        setParamByCC(apvts, 61, 0);                                // mix.osc2 off
        setParamByCC(apvts, 92, juce::roundToInt(balG2 * 127.0f)); // osc2.ring_mix
    }

    // Filter type x slope -> filter.va_type (CC115, Select index).  Engine is
    // already VA from kDefaults.  Out-of-range bytes fall back to LPF -24.
    {
        const uint8_t fType  = d(0x27);
        const uint8_t fSlope = d(0x28);
        const int vaType = (fType < 3 && fSlope < 2) ? kFilterVaTypeMap[fType][fSlope]
                                                     : 8;          // Diode LP4
        setParamByCC(apvts, 115, vaType);
    }

    // J3d (signed off 2026-07-21): Oscillator Shift 0x49 (±2 octaves, OSC
    // COMMON on the JP; the reference bank uses 0..3) folds into the
    // 5-option pitch-offset lattice — dropping it left imported patches an
    // octave or two off pitch.  Q1a: osc1 takes shift alone; osc2 takes the
    // JP's summed shift + Range (0x23, ±25 semis), clamped to the lattice
    // (lossy only past ±24).  The old range-only 0x23 slot is superseded.
    {
        const int shiftSemis = (juce::jlimit(0, 4, (int)d(0x49)) - 2) * 12;
        const int rangeSemis =  juce::jlimit(0, 50, (int)d(0x23)) - 25;

        auto latticeIdx = [](int semis)
        { return juce::jlimit(0, 4, juce::roundToInt((float)semis / 12.0f) + 2); };

        // osc[12].pitch_offset are 5-option Selects.  Decision A: setParamByCC
        // wants the CHOICE INDEX (0..4) directly — the old choiceIndexToCC()
        // wrapper produced a CC byte for the retired table-A value law.
        setParamByCC(apvts, 41, latticeIdx(shiftSemis));
        setParamByCC(apvts, 42, latticeIdx(shiftSemis + rangeSemis));
    }

    if (int cc = osc1Ctrl1CC(osc1Wave)) setParamByCC(apvts, cc, d(0x1F));
    if (int cc = osc1Ctrl2CC(osc1Wave)) setParamByCC(apvts, cc, d(0x20));
    if (int cc = osc2Ctrl1CC(osc2Wave)) setParamByCC(apvts, cc, d(0x25));

    // Step 3b: JP OSC1 waves with no direct JT wave equivalent (Delivery 2b).
    //   M9  — JP Noise osc: the wave map lands osc1.wave on SAW (neutral); the
    //         noise character comes from the JT mixer noise source instead.
    //   M10 — JP Feedback osc: wave map lands SAW; the feedback character comes
    //         from the JT osc feedback path.  0x1F (Ctrl1) is the JP feedback
    //         amount for this wave.
    // CC 59 = mix.noise, CC 123 = osc1.feedback_amount (both resolve via JtBridge).
    if (osc1Wave == 2)                       // Noise
    {
        setParamByCC(apvts, 59, 127);        // mix.noise full
        setParamByCC(apvts, 60, 0);          // mix.osc1 off — noise replaces it
    }
    else if (osc1Wave == 3)                  // Feedback
    {
        setParamByCC(apvts, 123, d(0x1F));   // osc1.feedback_amount from Ctrl1
    }

    // M14: LFO2 waveform is not carried in the JP-8000 patch, so import it as
    // triangle.  Set via CC 63 (lfo2.waveform, index 1 = TRI).  Without this,
    // resetLayerToDefaults leaves lfo2.waveform at def 0 = SIN.
    setParamByCC(apvts, 63, 1);              // lfo2.waveform -> TRI

    // Step 3c: JP SQR oscillator PWM depth (Ctrl2) -> per-osc LFO1->PW scale.
    // JE-8086 table: for SQR, Ctrl1 = pulse width (already -> shape_dc above),
    // Ctrl2 = PWM depth (LFO1).  JT: pwm = LFO1 x lfo1.pwm_depth (master,
    // CC35) x osc<n>.pwm_lfo1_depth (per-osc scale, def 1.0).  kDefaults zeroed
    // the master, so no import ever had LFO1 PWM.  When either osc is SQR the
    // master goes full and each osc's scale carries its own JP depth; a non-SQR
    // osc gets scale 0 so it does not pick up the other's PWM.  Untouched when
    // neither osc is SQR (defaults stand, master stays 0).
    // osc<n>.pwm_lfo1_depth are unmapped in JtBridge -> set by juceId.
    // Swing calibration (JP depth 127 vs JT width excursion) to be A/B'd.
    {
        const bool osc1Sqr = (osc1Wave == 4);   // JP OSC1 order: 4 = SQR
        const bool osc2Sqr = (osc2Wave == 0);   // JP OSC2 order: 0 = SQR
        if (osc1Sqr || osc2Sqr)
        {
            setParamByCC(apvts, 35, 127);       // lfo1.pwm_depth master full

            auto setScale = [&](const char* id, uint8_t byte)
            {
                if (auto* p = apvts.getParameter(id))
                    p->setValueNotifyingHost(p->convertTo0to1((float)byte / 127.0f));
            };
            setScale("osc1.pwm_lfo1_depth", osc1Sqr ? d(0x20) : 0);
            setScale("osc2.pwm_lfo1_depth", osc2Sqr ? d(0x26) : 0);
        }
    }

    // PWM law (2026-10-22, sign-off 5b): JP imports use the measured JP-8000
    // pulse law — width + LFO floored at 50 % (mix.pwm_law = JpPwm, index 1).
    // Only pulse waves read it, so it is set unconditionally.  Unmapped in
    // JtBridge (no v1 CC) -> set by juceId.
    if (auto* p = apvts.getParameter("mix.pwm_law"))
        p->setValueNotifyingHost(p->convertTo0to1(1.0f));

    // M19: LFO1 & ENV destination.  The JP routes OSC LFO1 depth AND pitch-env
    // depth to OSC1+2 / OSC2 / X-MOD from ONE byte, 0x18 (JE-8086 table,
    // "Lfo1AndEnvelopeDestination", 0..2) — the byte the old TODO waited for.
    // mix.pitch_mod_dest uses the same order ([0] OSC1+2 [1] OSC2 [2] X-MOD)
    // and the firmware routes both LFO1 and pitch-env through it.  LFO2 pitch
    // is always both, so it needs no destination write.  Unmapped in JtBridge
    // (no v1 CC) -> set by juceId.
    {
        const int pitchModDestIdx = juce::jlimit(0, 2, (int)d(0x18));
        if (auto* p = apvts.getParameter("mix.pitch_mod_dest"))
            p->setValueNotifyingHost(p->convertTo0to1((float)pitchModDestIdx));
    }

    // Step 4: JP-8080 Distortion MFX (gap audit 2026-10-17, signed off).
    // JP chorus-type byte order (JE-8086 table): 0-3 Super Chorus SLW/MID/FST/
    // CLR, 4-6 Flanger Slow/Deep/Fast, 7 Deep Phasing, 8 Jet, 9 Twisting,
    // 10-11 Freeze Phase 1/2, 12 DISTORTION (rack JP-8080 only).
    // The old overrides treated 5-7 as "delay variants" and 8-12 as
    // "distortion", which silently undid kMfxTypeMap for 5-11 (mod FX off,
    // delay hijacked or drive forced).  Only byte 12 needs an override:
    // drive Hard, mod FX off.  fx.drive is a Select -> choice INDEX 2 (Hard);
    // the old raw 32..127 bytes only worked by clamping.
    if (mfxType == 12)
    {
        setParamByCC(apvts, 16, 2);   // fx.drive -> Hard
        setParamByCC(apvts, 103, 0);  // fx.mod_effect -> OFF (drive replaces it)
    }

    // Step 5: import-time fixups (2026-09-19)
    //
    // fx.mod_rate (CC 105): the JT uses a PRESET mod type, so the modulator's
    // own rate is not driven from the JP patch.  Its default is 0.5 (mid),
    // which audibly fights the preset — force it to 0 on import.  (This also
    // supersedes the old "MFX type 1 -> CC 105 = 100" line, which was the
    // source of the mod rate sitting at mid.)
    setParamByCC(apvts, 105, 0);

    // lfo2.mod_wheel (0x040A, Toggle): added in firmware to mimic the JP-8000
    // LFO2 behaviour (LFO2 amount follows the mod wheel).  Every JP import must
    // enable it.  UNMAPPED in JtBridge (no v1 CC), so set directly by juceId.
    if (auto* p = apvts.getParameter("lfo2.mod_wheel"))
        p->setValueNotifyingHost(p->convertTo0to1(1.0f));   // index 1 = On

    // fx.delay_time: AUTHENTIC JP-8000 delay time (import authenticity, signed
    // off 2026-09-19).  The JP TIME knob scales 0..max where max depends on the
    // delay TYPE (manual "Changing the type of delay"):
    //   Pan L>R / Pan R>L          625 ms
    //   Panning Short / Mono Short 156 ms
    //   Mono Long                 1250 ms
    // JP ms = (timeByte / 127) * typeMax.  fx.delay_time is a Log 10..10000 ms
    // param, so we set it BY ENGINEERING VALUE (ms) via convertTo0to1 — the raw
    // 0x40 byte through the log curve gave a wildly wrong time (~1900 ms for a
    // 472 ms echo), which is why 0x40 is no longer a plain slot.
    {
        const uint8_t dType = d(0x3F);          // JP delay-type byte (0..4)
        const uint8_t dTime = d(0x40);          // JP delay-time byte (0..127)
        float typeMaxMs;
        switch (dType)
        {
            case 2: typeMaxMs = 156.0f;  break;   // Panning Short
            case 3: typeMaxMs = 156.0f;  break;   // Mono Short
            case 4: typeMaxMs = 1250.0f; break;   // Mono Long
            default: typeMaxMs = 625.0f; break;   // Panning L>R / R>L
        }
        const float ms = juce::jlimit(10.0f, 10000.0f,
                                      (float) dTime / 127.0f * typeMaxMs);
        if (auto* p = apvts.getParameter("fx.delay_time"))
            p->setValueNotifyingHost(p->convertTo0to1(ms));
    }
}

// =========================================================================
// Public: loadPatchToLayer  (Phase 6: BANK_DUMP path)
//
// Writes the patch into APVTS with _suppressEcho set so individual
// parameterChanged callbacks don't fire SET_PARAM messages, then sends
// ONE BANK_DUMP containing all params at the end. The firmware applies
// the dump under AudioNoInterrupts — atomic, no audible knob-sweep.
//
// editTarget is set BEFORE the bank dump build so the patch-scope entries
// in the dump go to the chosen layer. We deliberately don't restore
// editTarget after — matches the user's mental model "I loaded into
// Layer A, so I'm now editing Layer A."
// =========================================================================
void loadPatchToLayer(JT8000ControllerProcessor& proc,
                      const Patch& patch,
                      LoadTarget target)
{
    // Set editTarget first so the BANK_DUMP entries route correctly.
    // Order matters: the appendEntry calls inside sendBankDump() read
    // editTarget for patch-scope params.
    if (target == LoadTarget::LayerA)
        proc.editTarget.store(JT8000ControllerProcessor::EditTarget::LayerA);
    else if (target == LoadTarget::LayerB)
        proc.editTarget.store(JT8000ControllerProcessor::EditTarget::LayerB);
    // target == Current: keep existing editTarget

    // Suppress per-param SET_PARAM emission while we mass-update APVTS.
    // The 90+ parameterChanged callbacks that fire from loadPatch() will
    // skip their SysEx send because of this flag.
    proc._suppressEcho.store(true);
    // Reset shared (reverb/FX/seq) params to defaults ONCE before the patch;
    // loadPatch() resets the per-layer params itself.
    resetSharedToDefaults(proc.apvts);
    loadPatch(proc.apvts, patch);
    proc.setPatchName(patch.name);
    proc._suppressEcho.store(false);

    // Now flush the entire current state to the firmware in one go.
    proc.sendBankDump();
}

// =========================================================================
// Public: loadPerformance  (Phase 6: BANK_DUMP path)
//
// Loads upper patch into APVTS as the displayed layer, sets perf-global
// params (mode, split, balance), and sends one BANK_DUMP containing all
// patch + perf + global FX state. The firmware applies under
// AudioNoInterrupts.
//
// Note on dual layers: the plugin's APVTS holds ONE layer's worth of patch
// values. BANK_DUMP emits patch-scope entries for BOTH SyxProto::kLayerA
// and kLayerB with the same values — so loading a JP-8000 performance
// (which has distinct upper + lower patches) currently can't represent
// the lower patch's distinct values via BANK_DUMP alone.
//
// Two-pass approach: build BANK_DUMP for the upper, send it; then load
// lower into APVTS (which mutates the displayed state), build a second
// BANK_DUMP for the lower targeting kLayerB, send it. The firmware sees
// two BANK_DUMPs — one for A, one for B — and applies them in order.
// =========================================================================
void loadPerformance(JT8000ControllerProcessor& proc,
                     const Performance& perf)
{
    // ---- Set performance-global params via APVTS ----
    // Keyed by the REAL dotted juceIds (Phase J1 rename fix); the old
    // underscore ids resolved to nullptr and silently dropped every perf
    // write.  With _suppressEcho these don't fire SET_PARAMs individually;
    // they're picked up in the BANK_DUMP.
    //
    // Value law per row TYPE (gen/JtParamTable.h):
    //   perf.mode       (0x0700) Select  3 opt -> choice INDEX (0/1/2)
    //   perf.split_note (0x0705) Continuous 0..127 -> raw MIDI note
    //   perf.balance    (0x0706) Continuous 0..127 -> raw balance byte
    // (The perf CCs 140/142/143 in ParamMapJuce map to stale 0x100x ParamIDs
    //  with no JtParams row, so these are set by juceId directly, not via CC.)

    proc._suppressEcho.store(true);

    // Reset shared (reverb/FX/seq) params to defaults ONCE, before either
    // layer pass.  loadPatch() resets per-layer params on each pass, but the
    // PatchShared singletons must NOT be reset between passes or pass 2
    // (lower) would drop the values pass 1 (upper) put in its bank dump.
    resetSharedToDefaults(proc.apvts);

    if (auto* param = proc.apvts.getParameter("perf.mode"))
        param->setValueNotifyingHost(param->convertTo0to1(
            static_cast<float>(static_cast<int>(perf.mode))));

    if (auto* param = proc.apvts.getParameter("perf.split_note"))
        param->setValueNotifyingHost(param->convertTo0to1(
            static_cast<float>(perf.splitPoint)));

    if (auto* param = proc.apvts.getParameter("perf.balance"))
        param->setValueNotifyingHost(param->convertTo0to1(
            static_cast<float>(perf.balance)));

    // ---- Pass 1: upper patch -> Layer A bank dump ----
    proc.editTarget.store(JT8000ControllerProcessor::EditTarget::LayerA);
    loadPatch(proc.apvts, perf.upper);

    proc._suppressEcho.store(false);
    proc.sendBankDump();    // flush current APVTS as kLayerA entries only

    // ---- Pass 2: lower patch -> Layer B bank dump ----
    // Re-suppress, swap APVTS to lower, send a Layer-B-targeted dump.
    proc._suppressEcho.store(true);
    proc.editTarget.store(JT8000ControllerProcessor::EditTarget::LayerB);
    loadPatch(proc.apvts, perf.lower);
    proc._suppressEcho.store(false);
    proc.sendBankDump();

    // After the second dump the firmware has correct A + B state. The
    // plugin's APVTS now reflects the lower patch — switching editTarget
    // back to A would mismatch the visible UI. Leave editTarget on B so
    // user edits land where the UI is showing.
    proc.editTarget.store(JT8000ControllerProcessor::EditTarget::LayerB);

    // Show the performance name with sub-patch names in the LCD.
    // For dual-layer: "PerfName (Upper / Lower)"
    // For single: just "PerfName (PatchName)" if the patch name differs
    juce::String displayName = perf.name;
    if (perf.hasDistinctLower)
        displayName += " (" + perf.upper.name.substring(0, 12)
                     + " / " + perf.lower.name.substring(0, 12) + ")";
    else if (perf.upper.name.isNotEmpty() && perf.upper.name != perf.name)
        displayName += " (" + perf.upper.name + ")";

    proc.setPatchName(displayName);
}

} // namespace JP8000SyxImporter