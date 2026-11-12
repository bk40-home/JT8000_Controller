// =============================================================================
// DumpPlan.cpp — see DumpPlan.h
// =============================================================================
#include "DumpPlan.h"
#include "gen/JtParamTable.h"

namespace DumpPlan
{

std::size_t build (uint8_t what, bool toA, bool toB, Item* out, std::size_t cap) noexcept
{
    std::size_t n = 0;
    auto push = [&] (int index, uint8_t layer)
    {
        if (n < cap) out[n++] = Item { static_cast<uint16_t>(index), layer };
    };

    for (int i = 0; i < JtParams::kNumParams; ++i)
    {
        switch (JtParams::kParams[i].scope)
        {
            case JtParams::Scope::Patch:
                if ((what & kPatch) == 0) break;
                if (toA) push (i, 0);          // layer A
                if (toB) push (i, 1);          // layer B (NRPN layer bit)
                break;
            case JtParams::Scope::PatchShared:
                if (what & kShared) push (i, 0);   // one slot: sent once
                break;
            case JtParams::Scope::Performance:
                if (what & kPerf) push (i, 0);
                break;
            case JtParams::Scope::Global:
                if (what & kGlobal) push (i, 0);
                break;
        }
    }
    return n;
}

} // namespace DumpPlan
