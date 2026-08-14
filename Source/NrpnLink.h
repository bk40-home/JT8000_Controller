// =============================================================================
// NrpnLink.h — NRPN encode/decode for the JT-8000 v2 wire (Phase E)
// =============================================================================
// The JUCE-side twin of the ESP32's UartMidi seam: the plugin's internal
// model stays 7-bit and v1-CC-keyed (CCDefinitions.h, APVTS, all the ccLUT
// firmware-quirk logic — untouched); ONLY the wire changes, to NRPN keyed by
// frozen ParamID via the generated JtBridgeTable.h.  Same table, same value
// law, same rulebook as the firmware and the ESP32 controller:
//
//   out: v14 = v7 * 129                (lossless — 129*127 == 16383)
//   in:  v7  = (v14 + 64) / 129        (round-nearest; own echoes bit-exact)
//   per param: CC99 id>>7 · CC98 id&127 · CC6 v14>>7 · CC38 v14&127
//              then RPN-null CC101=127 · CC100=127 (park data entry)
//   resync:    NRPN 0x3F00 (firmware MidiParamTransport::kNrpnResyncRequest)
//
// Deliberately JUCE-free (stdint only) so the host test suite compiles it
// with the same -Wall -Wextra -Werror gate as the bridge table.
//
// Why NRPN dissolves the two old ledger items:
//   - CCs 140..155 ("local-only": ExtCC, un-sendable as class-compliant CC)
//     now travel as ordinary NRPN — nine curve params included.
//   - Hosts that drop SysEx pass plain CCs untouched — the VST3
//     SysEx-bypass workaround has nothing left to bypass.
//
// © 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "JtBridgeExt.h"     // generated table + the master.volume {cc7 <-> 0x0800}
                             // extension (Phase J1) — use JtBridgeExt:: lookups,
                             // never JtBridge:: directly, so the one v2-only
                             // param stays reachable from the 7-bit model.

namespace NrpnLink {

// Seq aux-lane ParamIDs (v2-only, no v1 CC — addressed by ParamID via
// encodeParamById / sendParamNrpnById).  Must match params.yaml / ParamTable.h
// (StageB: 0x068C..0x068F).  Kept here beside the encoder that uses them.
namespace AuxParamId {
    inline constexpr uint16_t kDestination = 0x068C;
    inline constexpr uint16_t kDepth       = 0x068D;
    inline constexpr uint16_t kStepSelect  = 0x068E;
    inline constexpr uint16_t kStepValue   = 0x068F;
}

// Arpeggiator per-step ParamIDs (v2-only, no v1 CC — Phase 9).  The three
// pattern lanes are addressed by ParamID directly, exactly like the seq aux
// lane above: select the step, then write the lane.  Must match params.yaml /
// ParamTable.h section 17 (0x0889..0x088C).  The nine GLOBAL arp params DO have
// pseudo-CCs (JtBridgeExt 205..213) and travel the normal CC/APVTS path; only
// these per-step lanes bypass it (they were never meant to be automatable, same
// as the sequencer's steps).
namespace ArpParamId {
    inline constexpr uint16_t kStepSelect  = 0x0889;
    inline constexpr uint16_t kStepOnOff   = 0x088A;
    inline constexpr uint16_t kStepAccent  = 0x088B;
    inline constexpr uint16_t kStepRatchet = 0x088C;
}

// ---- Layer addressing (Performance mode) -----------------------------------
// Bit 13 of the NRPN number means "this edit targets layer B".  ParamIDs are
// (section << 7) | index with sections 0..17, so bits 13..11 are free by
// construction, and a build that never sets the bit addresses layer A — the
// correct default — with no special case at either end.
//
// The receive CHANNEL is deliberately NOT used for this.  The ordinary Layer
// setup assigns both layers the same channel so one keyboard plays both, at
// which point a channel-addressed edit could not name a layer at all.
//
// TRAP, and it has bitten twice already: BOTH reserved addresses have bit 13
// set — the resync request is 126 << 7 == 0x3F00 and the status feed is
// 0x3FFF.  They MUST be matched on the raw address before the layer bit is
// stripped, or every one of them decodes as a layer-B write to some real
// parameter.
inline constexpr uint16_t kLayerBit = 0x2000;
inline constexpr uint16_t kIdMask   = 0x1FFF;

// One 3-byte channel-voice message, ready for juce::MidiMessage(d, 3).
struct Short3 { uint8_t b[3]; };

inline Short3 cc(uint8_t channel1to16, uint8_t num, uint8_t val)
{
    return { { (uint8_t)(0xB0u | ((channel1to16 - 1u) & 0x0Fu)),
               (uint8_t)(num & 0x7Fu), (uint8_t)(val & 0x7Fu) } };
}

// Encode one parameter edit.  Returns the number of messages written to
// out[] (6, or 0 when v1cc has no bridge mapping — count those as drops,
// never guess a ParamID).  out[] must hold >= 6 entries.
inline size_t encodeParam(uint16_t v1cc, uint8_t value7,
                          uint8_t channel, Short3* out, uint8_t layer = 0)
{
    const int32_t pid = JtBridgeExt::paramIdFromV1Cc(v1cc);
    if (pid < 0) return 0;

    // The firmware folds a layer-B write to a SHARED parameter (fx.*, seq.*)
    // back onto its single slot, so this end never has to know which
    // parameters bank — it may tag every edit with the layer being edited.
    const uint16_t addr = (uint16_t)(((uint16_t)pid & kIdMask) |
                                     (layer ? kLayerBit : 0u));

    const uint16_t v14 = (uint16_t)value7 * 129u;
    out[0] = cc(channel, 99, (uint8_t)((addr >> 7) & 0x7F));
    out[1] = cc(channel, 98, (uint8_t)( addr       & 0x7F));
    out[2] = cc(channel,  6, (uint8_t)((v14 >> 7) & 0x7F));
    out[3] = cc(channel, 38, (uint8_t)( v14       & 0x7F));
    out[4] = cc(channel, 101, 127);   // one deselect per edit: 18 bytes,
    out[5] = cc(channel, 100, 127);   // robustness > bandwidth here too
    return 6;
}

// Encode one parameter edit BY ParamID directly (no v1-CC bridge lookup).
// For v2-only params that were never assigned a 7-bit v1 CC — the seq aux
// lane (SEQ_AUX_*, 0x068C..0x068F).  Same 6-CC self-contained NRPN as
// encodeParam (address + data + RPN-null park); the only difference is the
// ParamID comes straight in instead of via paramIdFromV1Cc.  out[] >= 6.
inline size_t encodeParamById(uint16_t paramId, uint8_t value7,
                              uint8_t channel, Short3* out, uint8_t layer = 0)
{
    const uint16_t addr = (uint16_t)((paramId & kIdMask) |
                                     (layer ? kLayerBit : 0u));
    const uint16_t v14 = (uint16_t)value7 * 129u;
    out[0] = cc(channel, 99, (uint8_t)((addr >> 7) & 0x7F));
    out[1] = cc(channel, 98, (uint8_t)( addr       & 0x7F));
    out[2] = cc(channel,  6, (uint8_t)((v14 >> 7) & 0x7F));
    out[3] = cc(channel, 38, (uint8_t)( v14       & 0x7F));
    out[4] = cc(channel, 101, 127);
    out[5] = cc(channel, 100, 127);
    return 6;
}

// Encode the full-state pull request (firmware replies with every param).
// out[] must hold >= 5 entries.
inline size_t encodeResyncRequest(uint8_t channel, Short3* out, uint8_t layer = 0)
{
    out[0] = cc(channel, 99, 126);    // 0x3F00 >> 7
    out[1] = cc(channel, 98,   0);    // 0x3F00 &  127
    // The address has no room for a layer, so the DATA byte names it.  It was
    // previously ignored by the firmware, which is what makes this free: an
    // older firmware sees 0/1 and dumps layer A either way.
    out[2] = cc(channel,  6, (uint8_t)(layer ? 1 : 0));
    out[3] = cc(channel, 101, 127);
    out[4] = cc(channel, 100, 127);
    return 5;
}

// True for the CC numbers the NRPN machinery owns.  These COLLIDE with real
// v1 param numbers (v1 CC 98 = reverb hipass, CC 6 = osc mix, ...) so raw
// cluster bytes must never reach findParamByCC/applyIncomingCC.
inline bool isClusterCC(int num)
{
    return num == 6 || num == 38 || (num >= 96 && num <= 101);
}

// -----------------------------------------------------------------------------
// Receive state machine — mirror of the firmware's inbound transport and the
// ESP32's UartMidi decoder.  Coarse (CC6) applies immediately, fine (CC38)
// refines: one rulebook everywhere.  One instance PER INPUT STREAM (the VST3
// bus and a standalone direct-in are independent NRPN streams).
// -----------------------------------------------------------------------------
class Decoder {
public:
    // Feed one CC.  Returns true when a parameter decoded: *v1ccOut is the
    // bridged v1 number, *v7Out the rounded 7-bit value.  Cluster CCs always
    // return false-or-decoded and must then be treated as CONSUMED
    // (check isClusterCC separately — the caller owns that policy).
    // 'layerOut' (optional) receives 0 or 1 for a decoded parameter.  Callers
    // that hold ONE value per parameter — every editor so far does, because a
    // panel shows one layer at a time — must drop values for the layer they
    // are not showing, or the display silently fills with the other patch.
    bool feed(int num, int value, int* v1ccOut, int* v7Out, int* layerOut = nullptr)
    {
        layerOut_ = layerOut;
        const uint8_t v = (uint8_t)(value & 0x7F);
        switch (num) {
            case 99: sel_ = Sel::Nrpn; msb_ = v; dataMsbValid_ = false; return false;
            case 98: sel_ = Sel::Nrpn; lsb_ = v; dataMsbValid_ = false; return false;
            case 101: case 100:
                sel_ = Sel::Rpn; dataMsbValid_ = false; return false;
            case 6:
                if (sel_ != Sel::Nrpn) return false;   // RPN data: swallow
                dataMsb_ = v; dataMsbValid_ = true;
                return decode((uint16_t)v << 7, v1ccOut, v7Out);
            case 38:
                if (sel_ != Sel::Nrpn || !dataMsbValid_) return false;
                return decode((uint16_t)(((uint16_t)dataMsb_ << 7) | v),
                              v1ccOut, v7Out);
            case 96: case 97: return false;            // inc/dec: never sent
            default:          return false;            // not ours
        }
    }

    uint32_t droppedCount() const { return dropped_; }  // v2-only ParamIDs

    // ---- Status feed (Phase J1d) -------------------------------------------
    // Engine telemetry arrives on the reserved address 0x3FFF (never a
    // ParamID — see firmware ParamBroadcast::kStatusAddr).  decode()
    // intercepts it BEFORE the bridge lookup so it can never be misrouted
    // as a parameter, and never counts as a drop.  takeStatus() is
    // level-not-edge on the word but edge on RECEIPT: it returns true once
    // per received status cluster, so the caller can also use it as a link
    // heartbeat (the firmware re-sends at >= 1 Hz even when unchanged).
    //
    // Torn-word note: the coarse (CC6) half of the cluster stores a word
    // whose low bits (playStep/running) are still zero; the fine (CC38)
    // half overwrites it a message later.  Callers poll AFTER feeding a
    // whole block, so the refined word is what they see — worst case under
    // per-message polling is one wrong-playhead UI frame, self-correcting.
    static constexpr uint16_t kStatusAddr = 0x3FFF;
    bool takeStatus(uint16_t* wordOut)
    {
        if (!statusFresh_) return false;
        statusFresh_ = false;
        *wordOut = statusWord_;
        return true;
    }

private:
    bool decode(uint16_t v14, int* v1ccOut, int* v7Out)
    {
        const uint16_t raw = (uint16_t)(((uint16_t)msb_ << 7) | lsb_);

        // RAW, before the layer bit is stripped: 0x3FFF has bit 13 set, so
        // masking first would turn every status word into a layer-B parameter
        // write.  See the kLayerBit note at the top of this header.
        if (raw == kStatusAddr)                        // telemetry, not a param
        {
            statusWord_  = (uint16_t)(v14 & 0x3FFF);
            statusFresh_ = true;
            return false;
        }

        const uint16_t pid = (uint16_t)(raw & kIdMask);
        if (layerOut_ != nullptr) *layerOut_ = (raw & kLayerBit) ? 1 : 0;

        const int32_t v1cc = JtBridgeExt::v1CcFromParamId(pid);
        if (v1cc < 0) { ++dropped_; return false; }    // v2-only, unmapped
        *v1ccOut = (int)v1cc;
        *v7Out   = (int)(((uint32_t)v14 + 64u) / 129u);
        return true;
    }

    int* layerOut_ = nullptr;   // borrowed for the duration of one feed()

    enum class Sel : uint8_t { None, Nrpn, Rpn };
    Sel      sel_          = Sel::None;
    uint8_t  msb_          = 127;   // 127/127 == null selection
    uint8_t  lsb_          = 127;
    uint8_t  dataMsb_      = 0;
    bool     dataMsbValid_ = false;
    uint32_t dropped_      = 0;
    uint16_t statusWord_   = 0;
    bool     statusFresh_  = false;
};

} // namespace NrpnLink
