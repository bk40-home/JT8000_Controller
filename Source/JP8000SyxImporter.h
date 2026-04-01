/*
 * JP8000SyxImporter.h — JP-8000/8080 SysEx Import for JUCE Plugin
 *
 * Key change: setParamByCC() now handles the AudioParameterChoice / AudioParameterInt
 * split.  For choice params, the incoming CC value is converted to a choice index
 * via ccToChoiceIndex() before being set on the parameter.  This ensures combo
 * boxes display the correct name and the parameter state is consistent.
 */

#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CCDefinitions.h"

namespace JP8000SyxImporter
{

struct Patch
{
    juce::String name;
    int          dataOffset;
    int          dataLen;
    const uint8_t* srcBuffer;
};

// Waveform / enum remap tables — mirrors JP8000SyxMap.h
static constexpr uint8_t kOsc1WaveMap[] = { 123, 77, 13, 68, 113, 86, 31 };
static constexpr uint8_t kOsc2WaveMap[] = { 113, 31, 86, 68 };
static constexpr uint8_t kLfo1WaveMap[] = { 0, 1, 2, 3 };
static constexpr uint8_t kFilterTypeMap[] = { 2, 1, 0 };
static constexpr uint8_t kMfxTypeMap[] = { 10,10,0,1,3, 0,0,0, 0,0,0,0,0 };
static constexpr uint8_t kDelayTypeMap[] = { 2, 0, 3, 1, 1 };

// Safe defaults for JT-exclusive params
struct DefaultCC { int cc; int val; };
static constexpr DefaultCC kDefaults[] = {
    // Filter engine & topology
    {113, 96}, {115, 0}, {111, 0}, {114, 0}, {117, 0}, {84, 64},
    // Cross mod & sync
    {19, 0}, {20, 0},
    // Drive
    {16, 0},
    // Reverb
    {72, 0}, {73, 0}, {94, 127},
    {95, 64}, {96, 36}, {97, 0}, {98, 64},
    // Output mix
    {74, 127}, {76, 127},
    // Oscillator mix — CRITICAL: must be set here because osc1Ctrl1CC/osc2Ctrl1CC
    // only map to CC 60/61 for certain JP-8000 waveform types. SuperSaw (wave 0)
    // maps Ctrl1 to CC 77 (SUPERSAW_DETUNE), leaving CC 60 untouched → silence.
    {60, 127}, {61, 0},
    // Sub/noise
    {59, 0}, {58, 0},
    // Unison
    {15, 64},
    // Pitch env sustain/release
    {67, 0}, {68, 0},
    // LFO PWM depths
    {35, 0}, {40, 0},
    // LFO master depths
    {55, 64}, {52, 64},
    // LFO2 dest
    {53, 0},
    // Sequencer off
    {2, 0},
};

// Slot table
enum class Xform { Direct, Bool127, Signed64, Signed64ToUni,
                   Osc1Wave, Osc2Wave, Lfo1Wave, FilterType,
                   MfxType, DelayType, Osc2Range, Osc2Fine, MonoMode };
struct Slot { uint8_t offset; int cc; Xform xf; };

static constexpr Slot kSlots[] = {
    {0x10, 62, Xform::Lfo1Wave}, {0x11, 54, Xform::Direct}, {0x12, 37, Xform::Direct},
    {0x13, 51, Xform::Direct}, {0x15, 91, Xform::Bool127}, {0x17, 47, Xform::Signed64},
    {0x18, 56, Xform::Direct}, {0x19, 33, Xform::Signed64ToUni},
    {0x1A, 38, Xform::Signed64ToUni}, {0x1B, 69, Xform::Signed64},
    {0x1C, 65, Xform::Direct}, {0x1D, 66, Xform::Direct},
    {0x1E, 21, Xform::Osc1Wave}, {0x21, 22, Xform::Osc2Wave},
    {0x23, 42, Xform::Osc2Range}, {0x24, 46, Xform::Osc2Fine},
    {0x27, 112, Xform::FilterType}, {0x29, 23, Xform::Direct},
    {0x2A, 24, Xform::Direct}, {0x2B, 50, Xform::Signed64},
    {0x2C, 34, Xform::Signed64ToUni}, {0x2D, 39, Xform::Signed64ToUni},
    {0x2E, 48, Xform::Signed64},
    {0x2F, 29, Xform::Direct}, {0x30, 30, Xform::Direct},
    {0x31, 31, Xform::Direct}, {0x32, 32, Xform::Direct},
    {0x33, 90, Xform::Direct}, {0x34, 36, Xform::Signed64ToUni},
    {0x35, 57, Xform::Signed64ToUni},
    {0x36, 25, Xform::Direct}, {0x37, 26, Xform::Direct},
    {0x38, 27, Xform::Direct}, {0x39, 28, Xform::Direct},
    {0x3B, 99, Xform::Signed64}, {0x3C, 100, Xform::Signed64},
    {0x3D, 103, Xform::MfxType}, {0x3E, 104, Xform::Direct},
    {0x3F, 107, Xform::DelayType}, {0x40, 110, Xform::Direct},
    {0x41, 109, Xform::Direct}, {0x42, 108, Xform::Direct},
    {0x43, 127, Xform::Direct}, {0x45, 81, Xform::Bool127},
    {0x46, 82, Xform::Direct}, {0x47, 14, Xform::MonoMode},
};

inline int clamp7(int v) { return v < 0 ? 0 : (v > 127 ? 127 : v); }

inline int applyTransform(Xform xf, uint8_t raw)
{
    switch (xf)
    {
        case Xform::Direct:        return raw;
        case Xform::Bool127:       return raw ? 127 : 0;
        case Xform::Signed64:      return raw;
        case Xform::Signed64ToUni: return clamp7(std::abs((int)raw - 64) * 2);
        case Xform::Osc1Wave:     return (raw < 7)  ? kOsc1WaveMap[raw]   : 86;
        case Xform::Osc2Wave:     return (raw < 4)  ? kOsc2WaveMap[raw]   : 86;
        case Xform::Lfo1Wave:     return (raw < 4)  ? kLfo1WaveMap[raw]   : 0;
        case Xform::FilterType:   return (raw < 3)  ? kFilterTypeMap[raw]  : 0;
        case Xform::MfxType:      return (raw < 13) ? kMfxTypeMap[raw]    : 0;
        case Xform::DelayType:    return (raw < 5)  ? kDelayTypeMap[raw]  : 0;
        case Xform::Osc2Range:    return clamp7((int)(raw * 127 + 25) / 50);
        case Xform::Osc2Fine:     return clamp7((int)(raw * 127 + 50) / 100);
        case Xform::MonoMode:     return (raw == 0) ? 0 : 64;
    }
    return raw;
}

inline int osc1Ctrl1CC(uint8_t wave) { switch(wave){ case 0:return 77; case 1:case 2:case 4:case 6:return 60; default:return 0; } }
inline int osc1Ctrl2CC(uint8_t wave) { switch(wave){ case 0:return 78; case 1:case 2:return 123; default:return 0; } }
inline int osc2Ctrl1CC(uint8_t wave) { switch(wave){ case 0:case 1:return 61; default:return 0; } }
inline int osc2Ctrl2CC(uint8_t wave) { (void)wave; return 0; }

// =========================================================================
// Set an APVTS parameter by CC number.
//
// For AudioParameterChoice (combo/toggle): convert the CC value to a
// choice index using ccToChoiceIndex(), then set the normalised value.
// For AudioParameterInt (knob/slider): set the raw CC value directly.
// =========================================================================
inline void setParamByCC(juce::AudioProcessorValueTreeState& apvts, int cc, int value)
{
    const CCParam* p = findParamByCC(cc);
    if (!p) return;
    auto* pi = apvts.getParameter(p->id);
    if (!pi) return;

    float normVal;
    if (isChoiceParam(*p))
    {
        // Convert CC value → choice index, then normalise for the choice parameter
        int idx = ccToChoiceIndex(juce::jlimit(0, 127, value), p->enumCount);
        normVal = pi->convertTo0to1(static_cast<float>(idx));
    }
    else
    {
        normVal = pi->convertTo0to1(static_cast<float>(juce::jlimit(0, 127, value)));
    }

    pi->beginChangeGesture();
    pi->setValueNotifyingHost(normVal);
    pi->endChangeGesture();
}

// =========================================================================
// .syx parser
// =========================================================================
inline std::vector<Patch> parseSyxBuffer(const uint8_t* buf, size_t len)
{
    std::vector<Patch> patches;
    size_t i = 0;
    while (i < len)
    {
        if (buf[i] != 0xF0) { ++i; continue; }
        size_t f0 = i; ++i;
        while (i < len && buf[i] != 0xF7) ++i;
        if (i >= len) break;
        size_t msgStart = f0 + 1, msgLen = i - msgStart;
        if (msgLen >= 20 && buf[msgStart] == 0x41 && buf[msgStart+2] == 0x00 &&
            buf[msgStart+3] == 0x06 && buf[msgStart+4] == 0x12)
        {
            int dataOff = static_cast<int>(msgStart + 9);
            int dataLen = static_cast<int>(msgLen - 10);
            if (dataLen >= 74)
            {
                juce::String name;
                for (int j = 0; j < 16 && (dataOff + j) < static_cast<int>(len); ++j)
                {
                    char c = static_cast<char>(buf[dataOff + j]);
                    name += (c >= 0x20 && c <= 0x7E) ? juce::String::charToString(c) : " ";
                }
                patches.push_back({ name.trim(), dataOff, dataLen, buf });
            }
        }
        ++i;
    }
    return patches;
}

// =========================================================================
// .mid parser
// =========================================================================
inline uint32_t readVLQ(const uint8_t* buf, size_t len, size_t& pos)
{
    uint32_t val = 0;
    while (pos < len) { uint8_t b = buf[pos++]; val = (val << 7) | (b & 0x7F); if (!(b & 0x80)) break; }
    return val;
}

inline std::vector<Patch> parseMidBuffer(const uint8_t* buf, size_t len)
{
    std::vector<Patch> patches;
    if (len < 14 || buf[0]!='M'||buf[1]!='T'||buf[2]!='h'||buf[3]!='d') return patches;
    uint32_t hdrLen = (uint32_t(buf[4])<<24)|(uint32_t(buf[5])<<16)|(uint32_t(buf[6])<<8)|buf[7];
    uint16_t nTrk = (uint16_t(buf[10])<<8)|buf[11];
    size_t pos = 8 + hdrLen;
    for (uint16_t t = 0; t < nTrk && pos+8 <= len; ++t)
    {
        if (buf[pos]!='M'||buf[pos+1]!='T'||buf[pos+2]!='r'||buf[pos+3]!='k') break;
        uint32_t tl = (uint32_t(buf[pos+4])<<24)|(uint32_t(buf[pos+5])<<16)|(uint32_t(buf[pos+6])<<8)|buf[pos+7];
        size_t te = pos+8+tl, p = pos+8;
        while (p < te && p < len)
        {
            readVLQ(buf, te, p); if (p>=te) break;
            uint8_t st = buf[p];
            if (st==0xF0) {
                ++p; uint32_t ml = readVLQ(buf,te,p); size_t cl = ml>0?ml-1:0;
                if (cl>=20 && p+ml<=len && buf[p]==0x41 && buf[p+2]==0x00 && buf[p+3]==0x06 && buf[p+4]==0x12) {
                    int dOff=int(p+9), dLen=int(cl-10);
                    if (dLen>=74) {
                        juce::String nm;
                        for (int j=0;j<16&&(dOff+j)<int(len);++j){char c=char(buf[dOff+j]);nm+=(c>=0x20&&c<=0x7E)?juce::String::charToString(c):" ";}
                        patches.push_back({nm.trim(),dOff,dLen,buf});
                    }
                }
                p+=ml;
            } else if (st==0xFF) { p+=2; p+=readVLQ(buf,te,p); }
            else if (st>=0x80) { ++p; if(st>=0xC0&&st<0xE0)p+=1; else if((st>=0x80&&st<0xC0)||(st>=0xE0&&st<0xF0))p+=2; }
            else ++p;
        }
        pos = te;
    }
    return patches;
}

inline std::vector<Patch> parseFile(const uint8_t* buf, size_t len, bool isMidi)
{ return isMidi ? parseMidBuffer(buf, len) : parseSyxBuffer(buf, len); }

// =========================================================================
// loadPatch
// =========================================================================
inline void loadPatch(juce::AudioProcessorValueTreeState& apvts, const Patch& patch)
{
    const uint8_t* buf = patch.srcBuffer;
    auto d = [&](int off) -> uint8_t {
        int idx = patch.dataOffset + off;
        return (idx >= 0 && idx < patch.dataOffset + patch.dataLen) ? buf[idx] : 0;
    };

    for (const auto& def : kDefaults)
        setParamByCC(apvts, def.cc, def.val);

    uint8_t osc1Wave = d(0x1E), osc2Wave = d(0x21), mfxType = d(0x3D);

    for (const auto& slot : kSlots)
        setParamByCC(apvts, slot.cc, applyTransform(slot.xf, d(slot.offset)));

    if (int cc = osc1Ctrl1CC(osc1Wave)) setParamByCC(apvts, cc, d(0x1F));
    if (int cc = osc1Ctrl2CC(osc1Wave)) setParamByCC(apvts, cc, d(0x20));
    if (int cc = osc2Ctrl1CC(osc2Wave)) setParamByCC(apvts, cc, d(0x25));
    if (int cc = osc2Ctrl2CC(osc2Wave)) setParamByCC(apvts, cc, d(0x26));

    if (mfxType >= 8 && mfxType <= 12) {
        setParamByCC(apvts, 16, juce::jmin(127, 32 + (mfxType - 8) * 24));
        setParamByCC(apvts, 103, 0);
    }
    if (mfxType >= 5 && mfxType <= 7) {
        setParamByCC(apvts, 107, 0); setParamByCC(apvts, 108, d(0x3E)); setParamByCC(apvts, 103, 0);
    }
    if (mfxType == 1) setParamByCC(apvts, 105, 100);
}

} // namespace JP8000SyxImporter
