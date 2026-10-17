// =============================================================================
// EngineHost.h — the JT-8000 v2 firmware engine, hosted in-process (Phase J2)
// =============================================================================
// Wraps the UNMODIFIED firmware core (SynthCore + MidiParamTransport +
// ParameterStore from jteensy8000-v2) behind a small facade the plugin can
// drive.  Nothing is copied or forked: CMake's JT_ENGINE_DIR points at your
// firmware checkout and these classes compile against it directly — the
// desktop engine IS the hardware engine, same sources, same flags family as
// `make test` / `make render` (JT_TESTING gates only read-only test hooks).
//
// Deliberately JUCE-free (engine headers + stdlib only) so the host gate
// compiles and exercises the whole audition chain: NRPN bytes in → transport
// → store → renderBlock → samples out.
//
// THREADING CONTRACT (single consumer, enforced by the caller):
//   Every method here must be called from ONE thread — the audio thread.
//   SynthCore's internal note ring exists for the firmware's ISR split; in
//   this host the plugin funnels all cross-thread MIDI through a collector
//   and drains it in processBlock, so the engine only ever sees one caller.
//
// BLOCK ADAPTATION:
//   The engine renders fixed kBlockSize (128) sample blocks at a fixed
//   44 100 Hz (AudioConfig.h).  render() serves ANY request size from a
//   staging block, pulling new engine blocks as needed.  The delivered
//   sample stream is bit-identical to back-to-back renderBlock() calls —
//   grouping changes, the call sequence does not (gate-asserted).  Worst
//   added latency: one engine block, 2.9 ms.
//
// © 2026 Kris Bishop — MIT licensed.
// =============================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include "core/AudioConfig.h"
#include "core/ParameterStore.h"
#include "core/MidiParamTransport.h"
#include "core/SynthCore.h"

class EngineHost
{
public:
    EngineHost();

    // ---- MIDI-in facade (audio thread only — see contract above) ----------
    // Feed EVERY Control Change here: NRPN clusters, curated CCs, sustain,
    // channel-mode — the transport routes exactly as the firmware does and
    // returns non-param traffic to us for the panic/sustain handlers.
    void controlChange(uint8_t cc, uint8_t value);

    void noteOn (uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);

    // RAW 14-bit wheel, 0..16383, centre 8192 — the form JUCE's
    // getPitchWheelValue() already returns.  Do NOT add 8192 here: that
    // offset belongs only to the Teensy library's centred callback (the
    // historical double-offset trap — see firmware main.cpp warning).
    void pitchBendRaw14(uint16_t value14);

    void allNotesOff();   // mode change / transport stop safety

    // ---- Audio out --------------------------------------------------------
    // Renders n stereo samples at the ENGINE rate (44 100).  The caller owns
    // rate policy (request 44.1 k from the device; warn otherwise).
    void render(float* left, float* right, size_t n);

    // ---- Telemetry --------------------------------------------------------
    // Same packed word the firmware broadcasts on NRPN 0x3FFF — in Sound
    // mode the plugin feeds its dots/playhead from here instead of MIDI.
    uint16_t statusWord() const { return core_.statusWord(); }

    // Bass-sequencer playhead word (SynthCore::bassStatusWord): [5..1] step,
    // [0] running.  Engine-only for now — the firmware does not broadcast it.
    uint16_t bassStatusWord() const { return core_.bassStatusWord(); }

    static constexpr double kEngineSampleRate = (double)JT::kSampleRate;

private:
    JT::ParameterStore store_;          // ctor applies power-on defaults

    // The firmware's EXTMEM pools, heap-backed: caller-owned by the engine's
    // own design rule ("caller declares EXTMEM, engine takes float*") —
    // which is precisely what makes desktop hosting a non-event.
    std::vector<float> combPool_, reverbPool_, fxPool_;

    JT::SynthCore          core_;
    JT::MidiParamTransport transport_;

    // Staging block for size adaptation; stagePos_ == kBlockSize means empty.
    float  stageL_[JT::kBlockSize];
    float  stageR_[JT::kBlockSize];
    size_t stagePos_ = JT::kBlockSize;
};
