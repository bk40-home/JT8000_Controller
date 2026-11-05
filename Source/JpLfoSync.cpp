// =============================================================================
// JpLfoSync.cpp — JP-8000/8080 part LFO1 SYNC -> JT-8000 lfo1.sync (backlog J6)
// =============================================================================
// See JpLfoSync.h.  One constexpr table, read once per imported part: no
// runtime cost in the synth.
//
// (c) 2026 Kris Bishop — MIT licensed.
// =============================================================================
#include "JpLfoSync.h"

namespace JpLfoSync
{
namespace
{

// JT lfo1.sync option indices (kOpt_timing_mode order, gen/JtParamTable.h).
// Named here so the table below reads as note values, not magic numbers.
enum JtOpt : int
{
    kFree = 0, k4Bars = 1, k2Bars = 2, k1Bar = 3, kHalf = 4, kQuarter = 5,
    kEighth = 6, kSixteenth = 7, kThirtySecond = 8, kQuarterT = 9,
    kEighthT = 10, kSixteenthT = 11, kQuarterD = 12, kEighthD = 13,
    kSixteenthD = 14
};

struct Row
{
    const char* jpName;   // the JP's display name
    int         jt;       // JT option index
    bool        exact;    // false: snapped to the nearest JT cycle length
};

// One row per JP value, in the JP's order.  "q" = cycle length in quarter
// notes; snapped rows give the JP length, the JT length and the tempo ratio
// (the LFO runs that much faster or slower than on the JP).  The choice for
// each snapped row is the smallest |log(ratio)|, asserted by the host test.
constexpr Row kTable[kJpCount] =
{
    { "OFF",   kFree,        true  },   //  0
    { "1/16",  kSixteenth,   true  },   //  1  0.25 q
    { "1/8T",  kEighthT,     true  },   //  2  1/3 q
    { "1/16D", kSixteenthD,  true  },   //  3  0.375 q
    { "1/8",   kEighth,      true  },   //  4  0.5 q
    { "1/4T",  kQuarterT,    true  },   //  5  2/3 q
    { "1/8D",  kEighthD,     true  },   //  6  0.75 q
    { "1/4",   kQuarter,     true  },   //  7  1 q
    { "1/2T",  kQuarterD,    false },   //  8  4/3 q -> 1/4D 1.5 q   (x0.89 rate)
    { "1/4D",  kQuarterD,    true  },   //  9  1.5 q
    { "1/2",   kHalf,        true  },   // 10  2 q
    { "1/1T",  kHalf,        false },   // 11  8/3 q -> 1/2 2 q      (x1.33 rate)
    { "1/2D",  k1Bar,        false },   // 12  3 q   -> 1 Bar 4 q    (x0.75 rate)
    { "1/1",   k1Bar,        true  },   // 13  4 q
    { "2/1T",  k1Bar,        false },   // 14  16/3 q -> 1 Bar 4 q   (x1.33 rate)
    { "1/1D",  k2Bars,       false },   // 15  6 q   -> 2 Bars 8 q   (x0.75 rate)
    { "2/1",   k2Bars,       true  },   // 16  8 q
    { "3M",    k4Bars,       false },   // 17  12 q  -> 4 Bars 16 q  (x0.75 rate)
    { "4M",    k4Bars,       true  },   // 18  16 q
    { "5M",    k4Bars,       false },   // 19  20 q  -> 4 Bars 16 q  (x1.25 rate)
    { "6M",    k4Bars,       false },   // 20  24 q  -> 4 Bars (the JT's longest, x1.5)
    { "7M",    k4Bars,       false },   // 21  28 q  -> 4 Bars (x1.75)
    { "8M",    k4Bars,       false },   // 22  32 q  -> 4 Bars (x2)
};

constexpr bool inRange (int v) noexcept { return v >= 0 && v < kJpCount; }

} // namespace

int jtIndexFromJp (int jpValue) noexcept
{
    return inRange (jpValue) ? kTable[jpValue].jt : kFree;
}

bool isApproximate (int jpValue) noexcept
{
    return inRange (jpValue) && ! kTable[jpValue].exact;
}

const char* jpName (int jpValue) noexcept
{
    return inRange (jpValue) ? kTable[jpValue].jpName : "?";
}

} // namespace JpLfoSync
