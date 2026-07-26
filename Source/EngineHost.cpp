// =============================================================================
// EngineHost.cpp — implementation (Phase J2).  See EngineHost.h for the
// threading and block-adaptation contracts.
// =============================================================================
#include "EngineHost.h"

EngineHost::EngineHost()
    : store_(),
      combPool_  (JT::SynthCore::kCombPoolFloats,   0.0f),
      reverbPool_(JT::SynthCore::kReverbPoolFloats, 0.0f),
      fxPool_    (JT::SynthCore::kFxPoolFloats,     0.0f),
      core_(store_, combPool_.data(), reverbPool_.data(), fxPool_.data()),
      // MidiUsbDev is the origin the firmware assigns to DAW/editor traffic —
      // keeping it means store-write tagging and broadcast-suppression rules
      // behave identically to the hardware receiving from this same app.
      transport_(store_, JT::Origin::MidiUsbDev)
{
}

void EngineHost::controlChange(uint8_t cc, uint8_t value)
{
    // The transport consumes parameter traffic (NRPN clusters, curated CCs)
    // and returns false for everything else — which WE must then route, the
    // same contract the firmware's main.cpp honours.  Only the three
    // channel-level controls the engine exposes are relevant on desktop.
    if (transport_.handleControlChange(cc, value))
        return;

    switch (cc)
    {
        case 64:  core_.sustain(value >= 64); break;   // MIDI sustain convention
        case 120: core_.allSoundOff();        break;
        case 123: core_.allNotesOff();        break;
        default:  break;                               // ignore the rest
    }
}

void EngineHost::noteOn(uint8_t note, uint8_t velocity)
{
    // MIDI convention: NoteOn with velocity 0 is a NoteOff.  JUCE normally
    // pre-converts, but the gate (and any raw feed) may not.
    if (velocity == 0) { core_.noteOff(note); return; }
    core_.noteOn(note, velocity);
}

void EngineHost::noteOff(uint8_t note)          { core_.noteOff(note); }
void EngineHost::pitchBendRaw14(uint16_t v14)   { core_.pitchBend(v14); }
void EngineHost::allNotesOff()                  { core_.allNotesOff(); }

// -----------------------------------------------------------------------------
// render — serve n samples from fixed-size engine blocks.
//
// Copy loop rather than clever pointer maths: at 44.1 kHz stereo this moves
// ~350 KB/s worst case — memory-bandwidth noise next to the engine's own
// work, and obviously correct beats micro-optimised here.
// -----------------------------------------------------------------------------
void EngineHost::render(float* left, float* right, size_t n)
{
    size_t out = 0;
    while (out < n)
    {
        if (stagePos_ == JT::kBlockSize)                // staging empty
        {
            core_.renderBlock(stageL_, stageR_, JT::kBlockSize);
            stagePos_ = 0;
        }
        const size_t avail = JT::kBlockSize - stagePos_;
        const size_t take  = (n - out < avail) ? (n - out) : avail;
        for (size_t i = 0; i < take; ++i)
        {
            left [out + i] = stageL_[stagePos_ + i];
            right[out + i] = stageR_[stagePos_ + i];
        }
        stagePos_ += take;
        out       += take;
    }
}
