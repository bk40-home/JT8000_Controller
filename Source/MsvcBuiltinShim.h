// =============================================================================
// MsvcBuiltinShim.h — GCC builtins for MSVC (Phase J2 hotfix)
// =============================================================================
// The firmware core uses __builtin_ctz (ParameterStore's dirty-mask scan) —
// a GCC/Clang intrinsic MSVC lacks.  The firmware's own host gate is g++,
// so this only surfaces here.  Rather than modifying firmware sources (a
// cross-repo event), CMake force-includes this header (/FI) into every
// jt_engine translation unit on MSVC only.
//
// Contract preserved: like the GCC builtins, results are UNDEFINED for x==0
// — the firmware only ever scans non-zero masks.  clz/popcount included
// pre-emptively; unused shims cost nothing.
//
// © 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once

#if defined(_MSC_VER) && !defined(__clang__)

#include <intrin.h>

static __forceinline int __builtin_ctz(unsigned int x)
{
    unsigned long i;
    _BitScanForward(&i, x);
    return (int)i;
}

static __forceinline int __builtin_clz(unsigned int x)
{
    unsigned long i;
    _BitScanReverse(&i, x);
    return 31 - (int)i;
}

static __forceinline int __builtin_popcount(unsigned int x)
{
    return (int)__popcnt(x);
}

#endif // _MSC_VER && !__clang__
