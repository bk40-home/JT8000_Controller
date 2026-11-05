// =============================================================================
// JpLfoSync.h — JP-8000/8080 part LFO1 SYNC -> JT-8000 lfo1.sync (backlog J6)
// =============================================================================
// On the JP the LFO1 tempo sync is a PART parameter (performance part block,
// address 03 pp 10 = upper / 03 pp 11 = lower, offset 5), not a patch one, so
// the importer has to carry it next to the patch.  The JP has 23 values
// (OFF + 22 note values); the JT's lfo1.sync select has 15 options
// (kOpt_timing_mode in gen/JtParamTable.h).  13 JP values (OFF included) have
// an exact JT match; the other 10 snap to the JT option whose cycle length is nearest on a
// log scale (i.e. the smallest tempo ratio) — see the table in JpLfoSync.cpp.
//
// Import-time only: no runtime CPU, no firmware / params.yaml / NRPN change.
// Pure integer code, deliberately JUCE-free so it can be host-tested under
// -Wall -Wextra -Werror like ValueLaw.h.
//
// (c) 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once

namespace JpLfoSync
{

/** Number of JP LFO1 SYNC values: 0 = OFF, 1..22 = 1/16 .. 8 measures. */
constexpr int kJpCount = 23;

/** Offset of LFO1 SYNC inside the JP part block (03 pp 10 / 03 pp 11). */
constexpr int kPartOffset = 5;

/** JT lfo1.sync option index (0 = Free .. 14 = 1/16D) for a JP value.
 *  Anything outside 0..22 returns 0 (Free), as the JP itself treats it. */
int jtIndexFromJp (int jpValue) noexcept;

/** True when the JT has no exact match and the value was snapped. */
bool isApproximate (int jpValue) noexcept;

/** The JP's own name for a value ("OFF", "1/4D", "3M" ...); "?" if out of range. */
const char* jpName (int jpValue) noexcept;

} // namespace JpLfoSync
