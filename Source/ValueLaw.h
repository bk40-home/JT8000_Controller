// =============================================================================
// ValueLaw.h — the 7-bit value laws shared by every JT-8000 client (Phase J1)
// =============================================================================
// Pure integer maths, deliberately JUCE-free (stdint only) so the host test
// suite compiles it under the same -Wall -Wextra -Werror gate as NrpnLink and
// the bridge table.  CCDefinitions.h wraps these for the APVTS layer; keeping
// the maths here means the gate exercises the REAL shipping code, not a
// replica of it.
//
// The wire law everywhere is v14 = v7 * 129 (out) and v7 = (v14 + 64) / 129
// (in) — see NrpnLink.h.  These helpers place v7 so that the firmware's
// decode recovers the intended value EXACTLY:
//
//   Select:  firmware runs lroundf(norm * (count-1))         (Curves.cpp)
//            -> bucket-midpoint v7 = ((2*idx + 1) * 128) / (2 * count)
//   Int:     firmware runs min + lroundf(norm * (max-min))   (SynthCore.cpp)
//            -> lattice v7 = round((val-min) * 127 / (max-min))
//
// Round-trips for every option of every set, and every lattice point of the
// int params, are asserted exhaustively in the host gate.  Do not "simplify"
// the rounding — the margins are small (worst case ~0.06 of a bucket).
//
// © 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once

#include <stdint.h>

namespace ValueLaw {

// -----------------------------------------------------------------------------
// Select (choice) params — bucket-midpoint encode, even-bucket decode.
// Matches the firmware pair toOptionIndex()/normFromOptionIndex() through the
// 7-bit quantisation, both directions (proven in the gate for N = 2..17).
// -----------------------------------------------------------------------------
inline int ccFromChoice(int idx, int count)
{
    if (count <= 0) return 0;
    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;
    return ((2 * idx + 1) * 128) / (2 * count);
}

inline int choiceFromCc(int v7, int count)
{
    if (count <= 0) return 0;
    if (v7 < 0) v7 = 0;
    if (v7 > 127) v7 = 127;
    const int idx = (v7 * count) / 128;
    return (idx >= count) ? count - 1 : idx;
}

// -----------------------------------------------------------------------------
// Int-lattice params (canonical `type: int` — seq.steps 1..16, and the
// sequencer's step_select address 0..15).  Encode puts the norm ON the
// firmware's lattice so lroundf(norm * span) lands on the intended point;
// decode is the same round-nearest the firmware applies.  Integer
// round-nearest is (num + den/2) / den — all quantities are non-negative.
// -----------------------------------------------------------------------------
inline int ccFromIntLattice(int val, int minVal, int maxVal)
{
    const int span = maxVal - minVal;
    if (span <= 0) return 0;
    if (val < minVal) val = minVal;
    if (val > maxVal) val = maxVal;
    return ((val - minVal) * 127 + span / 2) / span;
}

inline int intLatticeFromCc(int v7, int minVal, int maxVal)
{
    const int span = maxVal - minVal;
    if (span <= 0) return minVal;
    if (v7 < 0) v7 = 0;
    if (v7 > 127) v7 = 127;
    return minVal + (v7 * span + 63) / 127;   // 63 ~= 127/2: round-nearest
}

// -----------------------------------------------------------------------------
// Status feed (engine -> editor), NRPN address 0x3FFF — reserved, never a
// ParamID.  Packing per SynthCore::statusWord():
//     [12..5] voiceMask:8   [4..1] playStep:4   [0] running:1
// Unpack helpers here so the packing law lives in exactly one JUCE-free
// place on this side of the wire (the gate asserts round-trips).
// -----------------------------------------------------------------------------
inline uint8_t statusVoiceMask(uint16_t w) { return (uint8_t)((w >> 5) & 0xFF); }
inline uint8_t statusPlayStep (uint16_t w) { return (uint8_t)((w >> 1) & 0x0F); }
inline bool    statusRunning  (uint16_t w) { return (w & 0x01) != 0; }

} // namespace ValueLaw
