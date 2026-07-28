// =============================================================================
// JtBridgeExt.h — local extension over the GENERATED JtBridgeTable.h (Phase J1)
// =============================================================================
// The generated bridge carries only the params that had v1 CC numbers.
// Canonical params WITHOUT a v1 alias (master.volume from Phase J1, and the
// three G1/G2 additions) get pseudo-CC pairs here instead.  Rather than
// hand-editing the generated file (forbidden — CI would reject it) or
// teaching the whole plugin a second key space, this header extends the
// lookup with out-of-band pairs (see kExtPairs below):
//
//     pseudo-cc 200  <->  ParamID 0x0800  (master.volume)
//
// 200 is OUT OF BAND on purpose (host gate enforced): every number 0..155
// is either a real v1 CC in the generated bridge — including 7, which is
// seq.rate, NOT volume; the yaml's curated `cc: 7` names the FIRMWARE'S
// real-MIDI map, a different namespace — or reachable as a plain hardware
// CC (wire max 127).  A >127 key can only ever be produced by the NRPN
// decode path, so it can never collide with either.
//
// Callers use these wrappers INSTEAD of JtBridge:: directly — the generated
// table is consulted first, so a future regeneration that adds a real
// mapping for 0x0800 wins automatically (this file then becomes dead weight
// to delete, not a conflict).
//
// Sign-off: Phase J1 Q2a (2026-07-19).
//
// © 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once

#include <stdint.h>

#include "JtBridgeTable.h"   // GENERATED — v1cc <-> ParamID, do not hand-edit

namespace JtBridgeExt {

// The extension pairs.  A table, not more named constants: the gate asserts
// against the table, and any future v2-only param lands as exactly one row.
// 201..203 obey the same out-of-band rule as 200 (see header): >155, so they
// can never collide with a real v1 CC or a wire CC, and only the NRPN decode
// path can produce them.
//
// Sign-off: G1/G2 JUCE alignment Q1 (2026-07-25).
inline constexpr uint16_t kMasterVolumeCc  = 200;   // out-of-band: see header
inline constexpr uint16_t kMasterVolumeId  = 0x0800;

struct ExtPair { uint16_t cc; uint16_t paramId; };

inline constexpr ExtPair kExtPairs[] = {
    { kMasterVolumeCc, kMasterVolumeId },   // master.volume        (Phase J1)
    { 201,             0x000D         },    // osc1.pwm_lfo1_depth  (G2)
    { 202,             0x008D         },    // osc2.pwm_lfo1_depth  (G2)
    { 203,             0x0107         },    // mix.pitch_mod_dest   (G1)
    { 204,             0x0801         },    // master.usb_return_level (USB soundcard return)
};

inline int32_t paramIdFromV1Cc(uint16_t v1cc)
{
    const int32_t pid = JtBridge::paramIdFromV1Cc(v1cc);
    if (pid >= 0) return pid;                       // generated table wins
    for (const ExtPair& p : kExtPairs)
        if (p.cc == v1cc) return (int32_t)p.paramId;
    return -1;
}

inline int32_t v1CcFromParamId(uint16_t paramId)
{
    const int32_t cc = JtBridge::v1CcFromParamId(paramId);
    if (cc >= 0) return cc;                         // generated table wins
    for (const ExtPair& p : kExtPairs)
        if (p.paramId == paramId) return (int32_t)p.cc;
    return -1;
}

} // namespace JtBridgeExt
