// =============================================================================
// DumpPlan.h — which parameters a state dump sends, and to which layer
// =============================================================================
// A "dump" (SEND ALL, a patch load, a performance load) writes many parameters
// to the synth as NRPN clusters.  Two rules decide what goes out:
//
//   1. WHAT: a load only sends the scopes it owns.  A performance owns both
//      layers' Patch params, the PatchShared FX/seq singletons and the
//      Performance params; it does NOT own Global ones (master volume, tempo,
//      clock source), so those are left alone (Kris 5 Nov, option 2a).
//   2. WHERE: Patch-scope params are per layer and carry the layer bit, once
//      per target layer.  Every other scope has a single slot in the firmware
//      (Params::slotFor folds a layer-B write back onto it), so it is sent once,
//      unlayered.  Before this, every write went to layer A (option 1a).
//
// Pure C++ (no JUCE) so the host test can check the plan — layering, scope
// filter, and that a performance fits the outgoing queue.
//
// (c) 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace DumpPlan
{

/** Scope selection bits (combine with |). */
enum What : uint8_t
{
    kPatch  = 1u << 0,   // Scope::Patch        — per layer
    kShared = 1u << 1,   // Scope::PatchShared  — FX / reverb / seq singletons
    kPerf   = 1u << 2,   // Scope::Performance  — mode, split, balance, channels
    kGlobal = 1u << 3,   // Scope::Global       — master, clock, tempo
    kAll    = kPatch | kShared | kPerf | kGlobal
};

/** One send: an index into JtParams::kParams and the layer bit (0 = A, 1 = B). */
struct Item
{
    uint16_t index;
    uint8_t  layer;
};

/** Short MIDI messages per parameter (one self-contained 6-CC NRPN cluster). */
constexpr std::size_t kMsgsPerItem = 6;

/** Fill out[] with the sends for `what`, Patch scope to layer A and/or B.
 *  Returns the number of items written (never more than cap). */
std::size_t build (uint8_t what, bool toA, bool toB, Item* out, std::size_t cap) noexcept;

} // namespace DumpPlan
