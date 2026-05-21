// =============================================================================
// ParamMapJuce.h — JUCE plugin's ParamID table + native-form converters
//
// The plugin uses CCDefinitions.h for its UI parameter table (CCParam rows
// keyed by JUCE param IDs). The firmware uses 14-bit ParamIDs from
// ParamMap.h. This file bridges the two:
//
//   - ParamId14 lookup by CC number (for outbound SysEx).
//   - isHiRes() — same set of 13 params as firmware Phase 3.
//   - cc-byte -> native-float conversion for hi-res params.
//
// The native converters mirror Mapping.h from the firmware. Keeping them here
// avoids depending on the firmware source tree from a JUCE build, at the cost
// of needing to keep both copies in sync. Curve formulas are simple enough
// that drift is unlikely; sync_cc_defs.py can be extended to validate.
//
// © 2025 Kris Bishop — MIT licensed.
// =============================================================================

#pragma once

#include <stdint.h>
#include <cmath>

namespace ParamMapJuce {

// -----------------------------------------------------------------------------
// 14-bit ParamID constants for the SysEx-only abstractions and a few special
// addresses. Most ParamIDs are looked up via paramId14ForCC() at runtime —
// we don't need a constant for every one.
// -----------------------------------------------------------------------------
namespace HiResIds {
    static constexpr uint16_t kFilterCutoff       = 0x0400;
    static constexpr uint16_t kFilterResonance    = 0x0401;
    static constexpr uint16_t kAmpAttack          = 0x0500;
    static constexpr uint16_t kAmpDecay           = 0x0501;
    static constexpr uint16_t kAmpRelease         = 0x0503;
    static constexpr uint16_t kFilterEnvAttack    = 0x0600;
    static constexpr uint16_t kFilterEnvDecay     = 0x0601;
    static constexpr uint16_t kFilterEnvRelease   = 0x0603;
    static constexpr uint16_t kPitchEnvAttack     = 0x0700;
    static constexpr uint16_t kPitchEnvDecay      = 0x0701;
    static constexpr uint16_t kPitchEnvRelease    = 0x0703;
    static constexpr uint16_t kLFO1Frequency      = 0x0800;
    static constexpr uint16_t kLFO2Frequency      = 0x0900;
}

// -----------------------------------------------------------------------------
// paramId14ForCC — map a CC number (0..155) to its 14-bit ParamID.
//
// Source of truth: firmware ParamMap.cpp. Kept in lock-step. Returns 0
// (sentinel) for any CC that doesn't map (CC 0 is reserved as "no param" so
// no false positive).
//
// Implemented as a switch rather than a generated table because:
//   - Table is sparse (90+ patch CCs scattered across 0..127, plus 7 perf
//     CCs at 140..146, plus 9 GlobalFX CCs).
//   - A switch lets the compiler optimise to a jump table or a binary-search
//     decision tree depending on density.
//   - Easier to audit by eye against the Phase 0 v2.2 markdown table.
// -----------------------------------------------------------------------------
inline uint16_t paramId14ForCC(int cc)
{
    switch (cc)
    {
        // 0x01XX — Oscillator
        case 21:  return 0x0100;  case 22:  return 0x0101;
        case 41:  return 0x0110;  case 42:  return 0x0111;
        case 43:  return 0x0112;  case 44:  return 0x0113;
        case 45:  return 0x0114;  case 46:  return 0x0115;
        case 47:  return 0x0120;  case 60:  return 0x0121;  case 61:  return 0x0122;
        case 77:  return 0x0130;  case 78:  return 0x0131;
        case 79:  return 0x0132;  case 80:  return 0x0133;
        case 101: return 0x0140;  case 102: return 0x0141;
        case 83:  return 0x0142;  case 85:  return 0x0143;

        // 0x02XX — Sub / Noise / Ring / DC
        case 58:  return 0x0200;  case 59:  return 0x0201;
        case 91:  return 0x0210;  case 92:  return 0x0211;
        case 86:  return 0x0220;  case 88:  return 0x0221;
        case 87:  return 0x0222;  case 89:  return 0x0223;

        // 0x03XX — Cross-mod / Sync / Feedback
        case 19:  return 0x0300;  case 20:  return 0x0301;
        case 123: return 0x0310;  case 124: return 0x0311;
        case 125: return 0x0312;  case 126: return 0x0313;

        // 0x04XX — Filter
        case 23:  return 0x0400;  case 24:  return 0x0401;
        case 48:  return 0x0402;  case 50:  return 0x0403;  case 84: return 0x0404;
        case 113: return 0x0410;  case 112: return 0x0411;
        case 115: return 0x0412;  case 114: return 0x0413;
        case 111: return 0x0414;  case 117: return 0x0415;

        // 0x05XX — Amp envelope
        case 25:  return 0x0500;  case 26:  return 0x0501;
        case 27:  return 0x0502;  case 28:  return 0x0503;
        case 147: return 0x0504;  case 148: return 0x0505;  case 149: return 0x0506;

        // 0x06XX — Filter envelope
        case 29:  return 0x0600;  case 30:  return 0x0601;
        case 31:  return 0x0602;  case 32:  return 0x0603;
        case 150: return 0x0604;  case 151: return 0x0605;  case 152: return 0x0606;

        // 0x07XX — Pitch envelope
        case 65:  return 0x0700;  case 66:  return 0x0701;
        case 67:  return 0x0702;  case 68:  return 0x0703;  case 69: return 0x0704;
        case 153: return 0x0705;  case 154: return 0x0706;  case 155: return 0x0707;

        // 0x08XX — LFO 1
        case 54:  return 0x0800;  case 55:  return 0x0801;
        case 56:  return 0x0802;  case 62:  return 0x0803;
        case 33:  return 0x0810;  case 34:  return 0x0811;
        case 35:  return 0x0812;  case 36:  return 0x0813;  case 37: return 0x0820;

        // 0x09XX — LFO 2
        case 51:  return 0x0900;  case 52:  return 0x0901;
        case 53:  return 0x0902;  case 63:  return 0x0903;
        case 38:  return 0x0910;  case 39:  return 0x0911;
        case 40:  return 0x0912;  case 57:  return 0x0913;  case 64: return 0x0920;

        // 0x0AXX — Velocity
        case 10:  return 0x0A00;  case 11:  return 0x0A01;  case 12: return 0x0A02;

        // 0x0BXX — Glide / Voice / Pitch bend
        case 81:  return 0x0B00;  case 82:  return 0x0B01;
        case 14:  return 0x0B10;  case 15:  return 0x0B11;
        case 127: return 0x0B20;  case 90:  return 0x0B30;

        // 0x0CXX — FX (Patch-scope)
        case 16:  return 0x0C00;
        case 99:  return 0x0C10;  case 100: return 0x0C11;
        case 103: return 0x0C20;  case 104: return 0x0C21;
        case 105: return 0x0C22;  case 106: return 0x0C23;
        case 107: return 0x0C30;  case 108: return 0x0C31;
        case 109: return 0x0C32;  case 110: return 0x0C33;
        case 74:  return 0x0C40;  case 76:  return 0x0C41;

        // 0x0DXX — BPM / Clock / Timing
        case 118: return 0x0D00;  case 119: return 0x0D01;
        case 120: return 0x0D10;  case 121: return 0x0D11;  case 122: return 0x0D12;

        // 0x0EXX — Step sequencer
        case 2:   return 0x0E00;  case 3:   return 0x0E01;
        case 4:   return 0x0E02;  case 5:   return 0x0E03;  case 6:  return 0x0E04;
        case 7:   return 0x0E05;  case 8:   return 0x0E06;  case 9:  return 0x0E07;
        case 13:  return 0x0E08;  case 17:  return 0x0E10;  case 18: return 0x0E11;
        case 116: return 0x0E20;

        // 0x10XX — Performance (LayerManager-owned)
        case 140: return 0x1000;  case 141: return 0x1001;
        case 142: return 0x1002;  case 143: return 0x1003;
        case 144: return 0x1004;  case 145: return 0x1005;  case 146: return 0x1006;

        // 0x11XX — Global FX (shared reverb)
        case 70:  return 0x1100;  case 71:  return 0x1101;
        case 93:  return 0x1102;  case 75:  return 0x1103;
        case 94:  return 0x1104;  case 95:  return 0x1105;
        case 96:  return 0x1106;  case 97:  return 0x1107;  case 98: return 0x1108;

        default:  return 0;
    }
}

// -----------------------------------------------------------------------------
// scopeForCC — returns which scope a CC belongs to, used by the plugin to
// decide which SysEx layer byte to emit. Must agree with firmware ParamMap.h.
//
//   0 = patch (engine A/B based on plugin edit target)
//   1 = performance (always SyxProto::kLayerPerf)
//   2 = global FX (always SyxProto::kLayerGlobalFx)
// -----------------------------------------------------------------------------
inline int scopeForCC(int cc)
{
    // Performance CCs 140..146.
    if (cc >= 140 && cc <= 146) return 1;
    // GlobalFX CCs (sparse: 70, 71, 75, 93..98).
    if (cc == 70 || cc == 71 || cc == 75 ||
        (cc >= 93 && cc <= 98))
        return 2;
    return 0;
}

// True iff this 14-bit ParamID has a hi-res native-float path on the firmware.
// Mirrors firmware ParamMap::isHiRes(). The plugin uses this to decide whether
// to send the value in native units (true hi-res) or as a CC-byte float
// (firmware will quantise via its CC handler).
inline bool isHiRes(uint16_t paramId)
{
    switch (paramId)
    {
        case HiResIds::kFilterCutoff:
        case HiResIds::kFilterResonance:
        case HiResIds::kAmpAttack:
        case HiResIds::kAmpDecay:
        case HiResIds::kAmpRelease:
        case HiResIds::kFilterEnvAttack:
        case HiResIds::kFilterEnvDecay:
        case HiResIds::kFilterEnvRelease:
        case HiResIds::kPitchEnvAttack:
        case HiResIds::kPitchEnvDecay:
        case HiResIds::kPitchEnvRelease:
        case HiResIds::kLFO1Frequency:
        case HiResIds::kLFO2Frequency:
            return true;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// Native-form converters — mirror firmware Mapping.h.
//
// The plugin stores values in CC-byte form inside APVTS (legacy from the CC
// era). For hi-res SysEx output we convert CC byte (0..127) to native unit
// using the same curves the firmware uses. For incoming SysEx PARAM_VALUE
// replies we convert the other way.
//
// These are inline because each is short and called once per param send.
// -----------------------------------------------------------------------------

// Filter cutoff: 20 Hz .. 20 kHz exponential.
inline float ccToCutoffHz(uint8_t cc)
{
    // Same shape as firmware: 20 * 1000^(cc/127).
    return 20.0f * std::pow(1000.0f, cc / 127.0f);
}
inline uint8_t cutoffHzToCC(float hz)
{
    if (hz <= 20.0f)    return 0;
    if (hz >= 20000.0f) return 127;
    const float n = std::log(hz / 20.0f) / std::log(1000.0f);
    int b = (int)(n * 127.0f + 0.5f);
    return (uint8_t)(b < 0 ? 0 : (b > 127 ? 127 : b));
}

// Filter resonance — engine-dependent. OBXa caps at 0.97; VA goes to 1.0.
// Plugin doesn't always know engine choice at send time; we encode for OBXa
// since that's the more common default. The firmware's hi-res setter takes
// the float verbatim and applies its own clamping per-engine.
inline float ccToResonance01(uint8_t cc, bool isVA = false)
{
    const float n = cc / 127.0f;
    return isVA ? n : (n * 0.97f);
}
inline uint8_t resonance01ToCC(float r, bool isVA = false)
{
    const float top = isVA ? 1.0f : 0.97f;
    if (r <= 0.0f) return 0;
    if (r >= top)  return 127;
    int b = (int)((r / top) * 127.0f + 0.5f);
    return (uint8_t)(b < 0 ? 0 : (b > 127 ? 127 : b));
}

// Envelope time: 1 ms .. 11880 ms log curve (cc_to_time_ms in firmware).
inline float ccToTimeMs(uint8_t cc)
{
    return std::pow(11880.0f, cc / 127.0f);
}
inline uint8_t timeMsToCC(float ms)
{
    if (ms <= 1.0f)     return 0;
    if (ms >= 11880.0f) return 127;
    const float n = std::log(ms) / std::log(11880.0f);
    int b = (int)(n * 127.0f + 0.5f);
    return (uint8_t)(b < 0 ? 0 : (b > 127 ? 127 : b));
}

// LFO frequency: 0.03 Hz .. 39 Hz exponential.
inline float ccToLfoHz(uint8_t cc)
{
    return 0.03f * std::pow(1300.0f, cc / 127.0f);
}
inline uint8_t lfoHzToCC(float hz)
{
    if (hz <= 0.03f) return 0;
    if (hz >= 39.0f) return 127;
    const float n = std::log(hz / 0.03f) / std::log(1300.0f);
    int b = (int)(n * 127.0f + 0.5f);
    return (uint8_t)(b < 0 ? 0 : (b > 127 ? 127 : b));
}

// -----------------------------------------------------------------------------
// ccByteToNativeFloat — top-level converter: takes a 14-bit ParamID and a
// CC byte, returns the native-form float to send over SysEx.
//
// For hi-res params, returns the curve-converted native value (Hz, ms, etc).
// For everything else, returns the CC byte cast to float. The firmware's CC
// handler will quantise back to a CC byte and apply normally.
// -----------------------------------------------------------------------------
inline float ccByteToNativeFloat(uint16_t paramId14, uint8_t ccValue)
{
    using namespace HiResIds;
    switch (paramId14)
    {
        case kFilterCutoff:        return ccToCutoffHz(ccValue);
        case kFilterResonance:     return ccToResonance01(ccValue, /*isVA=*/false);
        case kAmpAttack:
        case kAmpDecay:
        case kAmpRelease:
        case kFilterEnvAttack:
        case kFilterEnvDecay:
        case kFilterEnvRelease:
        case kPitchEnvAttack:
        case kPitchEnvDecay:
        case kPitchEnvRelease:     return ccToTimeMs(ccValue);
        case kLFO1Frequency:
        case kLFO2Frequency:       return ccToLfoHz(ccValue);
        default:                   return (float)ccValue;
    }
}

// Inverse: PARAM_VALUE float -> CC byte for APVTS update.
inline uint8_t nativeFloatToCCByte(uint16_t paramId14, float value)
{
    using namespace HiResIds;
    switch (paramId14)
    {
        case kFilterCutoff:        return cutoffHzToCC(value);
        case kFilterResonance:     return resonance01ToCC(value, /*isVA=*/false);
        case kAmpAttack:
        case kAmpDecay:
        case kAmpRelease:
        case kFilterEnvAttack:
        case kFilterEnvDecay:
        case kFilterEnvRelease:
        case kPitchEnvAttack:
        case kPitchEnvDecay:
        case kPitchEnvRelease:     return timeMsToCC(value);
        case kLFO1Frequency:
        case kLFO2Frequency:       return lfoHzToCC(value);
        default:
        {
            // Firmware echoes back our CC byte as a float; clamp to 0..127.
            int b = (int)(value + 0.5f);
            return (uint8_t)(b < 0 ? 0 : (b > 127 ? 127 : b));
        }
    }
}

} // namespace ParamMapJuce
