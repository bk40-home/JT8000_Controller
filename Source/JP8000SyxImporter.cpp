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

namespace JP8000SyxImporter
{

// =========================================================================
// Waveform / enum remap tables — mirrors JP-8000 SysEx value encoding
//
// The JP-8000 uses its own enum values for waveforms, filter types, etc.
// These tables map from JP-8000 SysEx byte → JT-8000 CC value.
// =========================================================================
static constexpr uint8_t kOsc1WaveMap[]   = { 123, 77, 13, 68, 113, 86, 31 };
static constexpr uint8_t kOsc2WaveMap[]   = { 113, 31, 86, 68 };
static constexpr uint8_t kLfo1WaveMap[]   = { 0, 1, 2, 3 };
static constexpr uint8_t kFilterTypeMap[] = { 2, 1, 0 };
static constexpr uint8_t kMfxTypeMap[]    = { 10, 10, 0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0 };
static constexpr uint8_t kDelayTypeMap[]  = { 2, 0, 3, 1, 1 };

// =========================================================================
// Safe defaults for JT-8000-exclusive parameters
//
// The JP-8000 SysEx doesn't carry values for these — they're either new
// JT-8000 features (VA filter, cross-mod, reverb) or have different
// default assumptions. Applied before any JP-8000 data is mapped.
// =========================================================================
struct DefaultCC { int cc; int val; };
static constexpr DefaultCC kDefaults[] = {
    // Filter engine & topology — default to OBXa mode
    {113, 96}, {115, 0}, {111, 0}, {114, 0}, {117, 0}, {84, 64},
    // Cross-mod & sync — JP-8000 has no cross-mod
    {19, 0}, {20, 0},
    // Drive — bypass
    {16, 0},
    // Reverb — all JT-8000-exclusive
    {72, 0}, {73, 0}, {94, 127},
    {95, 64}, {96, 36}, {97, 0}, {98, 64},
    // Output mix — full dry
    {74, 127}, {76, 127},
    // Oscillator mix — CRITICAL: SuperSaw (wave 0) maps Ctrl1 to CC 77
    // (SUPERSAW_DETUNE), not CC 60 (OSC1_MIX). If CC 60 isn't explicitly
    // set, it stays at 0 = silence. Default both mix levels here.
    {60, 127}, {61, 0},
    // Sub / noise
    {59, 0}, {58, 0},
    // Unison detune
    {15, 64},
    // Pitch envelope sustain/release — JP-8000 has no pitch env S/R
    {67, 0}, {68, 0},
    // LFO PWM depths — not in JP-8000 SysEx
    {35, 0}, {40, 0},
    // LFO master depths — bipolar centre
    {55, 64}, {52, 64},
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
    FilterType,
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
    {0x15, 91, Xform::Bool127},           // Ring mod on/off
    {0x17, 47, Xform::Signed64},          // Osc balance (bipolar)
    {0x18, 56, Xform::Direct},            // LFO1 destination
    {0x19, 33, Xform::Signed64ToUni},     // LFO1 → pitch depth
    {0x1A, 38, Xform::Signed64ToUni},     // LFO2 → pitch depth
    {0x1B, 69, Xform::Signed64},          // Pitch envelope depth
    {0x1C, 65, Xform::Direct},            // Pitch envelope attack
    {0x1D, 66, Xform::Direct},            // Pitch envelope decay

    // Oscillators
    {0x1E, 21, Xform::Osc1Wave},         // OSC1 waveform
    {0x21, 22, Xform::Osc2Wave},         // OSC2 waveform
    {0x23, 42, Xform::Osc2Range},        // OSC2 pitch range
    {0x24, 46, Xform::Osc2Fine},         // OSC2 fine tune

    // Filter
    {0x27, 112, Xform::FilterType},       // Filter type (HPF/BPF/LPF)
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
    {0x40, 110, Xform::Direct},           // Delay time
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
        case Xform::Osc1Wave:      return (raw < 7)  ? kOsc1WaveMap[raw]   : 86;
        case Xform::Osc2Wave:      return (raw < 4)  ? kOsc2WaveMap[raw]   : 86;
        case Xform::Lfo1Wave:      return (raw < 4)  ? kLfo1WaveMap[raw]   : 0;
        case Xform::FilterType:    return (raw < 3)  ? kFilterTypeMap[raw]  : 0;
        case Xform::MfxType:       return (raw < 13) ? kMfxTypeMap[raw]     : 0;
        case Xform::DelayType:     return (raw < 5)  ? kDelayTypeMap[raw]   : 0;
        case Xform::Osc2Range:     return clamp7((static_cast<int>(raw) * 127 + 25) / 50);
        case Xform::Osc2Fine:      return clamp7((static_cast<int>(raw) * 127 + 50) / 100);
        case Xform::MonoMode:      return (raw == 0) ? 0 : 64;
    }
    return raw;
}

/** Map JP-8000 OSC1 waveform index → which CC carries Ctrl1. */
static int osc1Ctrl1CC(uint8_t wave)
{
    switch (wave)
    {
        case 0:              return 77;   // SuperSaw → SS Detune
        case 1: case 2:
        case 4: case 6:      return 60;   // PW/Square/etc → OSC1 Level
        default:              return 0;    // no Ctrl1
    }
}

/** Map JP-8000 OSC1 waveform index → which CC carries Ctrl2. */
static int osc1Ctrl2CC(uint8_t wave)
{
    switch (wave)
    {
        case 0:              return 78;   // SuperSaw → SS Mix
        case 1: case 2:      return 123;  // PW/Square → FB Amount
        default:              return 0;
    }
}

/** Map JP-8000 OSC2 waveform index → which CC carries Ctrl1. */
static int osc2Ctrl1CC(uint8_t wave)
{
    switch (wave)
    {
        case 0: case 1:      return 61;   // Pulse/Triangle → OSC2 Level
        default:              return 0;
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
            const int a2      = buf[msgStart + 7];   // message type
            const int dataOff = static_cast<int>(msgStart + 9);
            const int dataLen = static_cast<int>(msgLen) - 10;

            if (a2 == 0x00 && dataLen >= 20)
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

static ParsedBank parseMidBuffer(const uint8_t* buf, size_t len)
{
    // First pass: extract all embedded SysEx payloads into a flat buffer,
    // wrapping each with F0...F7 so we can reuse parseSyxBuffer.
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

    if (assembled.empty())
        return {};

    // Parse the assembled buffer exactly as if it were a .syx file
    return parseSyxBuffer(assembled.data(), assembled.size());
}

// =========================================================================
// Public: parseFile
// =========================================================================

ParsedBank parseFile(const uint8_t* buf, size_t len, bool isMidi)
{
    return isMidi ? parseMidBuffer(buf, len) : parseSyxBuffer(buf, len);
}

// =========================================================================
// Public: setParamByCC
//
// For AudioParameterChoice (combo/toggle): convert the incoming CC value
// to a choice index via ccToChoiceIndex(), then set the normalised value.
// For AudioParameterInt (knob/slider): set the raw CC value directly.
// =========================================================================

void setParamByCC(juce::AudioProcessorValueTreeState& apvts, int cc, int value)
{
    const CCParam* p = findParamByCC(cc);
    if (p == nullptr) return;

    auto* pi = apvts.getParameter(p->id);
    if (pi == nullptr) return;

    float normVal;
    if (isChoiceParam(*p))
    {
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
// Public: loadPatch
//
// Writes a JP-8000 patch into the APVTS. Does NOT send MIDI.
// Steps:
//   1. Apply JT-8000 defaults (for params the JP-8000 lacks)
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

    // Step 1: set JT-8000-exclusive defaults
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
    if (int cc = osc1Ctrl1CC(osc1Wave)) setParamByCC(apvts, cc, d(0x1F));
    if (int cc = osc1Ctrl2CC(osc1Wave)) setParamByCC(apvts, cc, d(0x20));
    if (int cc = osc2Ctrl1CC(osc2Wave)) setParamByCC(apvts, cc, d(0x25));

    // Step 4: JP-8000 MFX type overrides
    // MFX types 8-12 are distortion variants — map to JT-8000 drive CC
    if (mfxType >= 8 && mfxType <= 12)
    {
        setParamByCC(apvts, 16, juce::jmin(127, 32 + (mfxType - 8) * 24));
        setParamByCC(apvts, 103, 0);  // disable mod FX (drive replaces it)
    }
    // MFX types 5-7 are delay variants — map to JT-8000 delay params
    if (mfxType >= 5 && mfxType <= 7)
    {
        setParamByCC(apvts, 107, 0);          // delay type off
        setParamByCC(apvts, 108, d(0x3E));    // delay mix from MFX level
        setParamByCC(apvts, 103, 0);          // mod FX off
    }
    // MFX type 1 (Chorus with fixed rate) — set a sensible default rate
    if (mfxType == 1)
        setParamByCC(apvts, 105, 100);
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
    // These are perf-scope CCs (140-146). With _suppressEcho they don't
    // fire SET_PARAMs individually; they're picked up in the BANK_DUMP.

    proc._suppressEcho.store(true);

    if (auto* param = proc.apvts.getParameter("perf_mode"))
        param->setValueNotifyingHost(param->convertTo0to1(
            static_cast<float>(static_cast<int>(perf.mode))));

    if (auto* param = proc.apvts.getParameter("perf_split_note"))
        param->setValueNotifyingHost(param->convertTo0to1(
            static_cast<float>(perf.splitPoint)));

    if (auto* param = proc.apvts.getParameter("perf_balance"))
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
