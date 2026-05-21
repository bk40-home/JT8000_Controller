/*
 * PluginProcessor.h — JT-8000 Controller Processor (Phase 6)
 *
 * SysEx-only output. The plugin emits SET_PARAM and BANK_DUMP messages in
 * the JT-8000 editor protocol (see SyxProtocol.h). CC output is gone.
 * Hardware controllers can still SEND CCs into the plugin (those reflect
 * into APVTS via applyIncomingCC) — only the OUTPUT side is SysEx.
 *
 * ABLETON WORKAROUND:
 *   Ableton refuses to load VST3 plugins with no audio buses (ABLDEV-00057).
 *   We declare dummy stereo I/O buses and silence them in processBlock().
 *   isMidiEffect() returns false for the same reason — Ableton ignores
 *   MIDI-only plugins. Unchanged from previous phases.
 *
 * MIDI ECHO SUPPRESSION:
 *   Both processBlock (VST) and handleIncomingMidiMessage (standalone)
 *   consume incoming CCs without forwarding them back out. Incoming CCs
 *   update the APVTS/UI via applyIncomingCC() which sets _suppressEcho
 *   so parameterChanged() doesn't fire a SysEx round-trip.
 *
 * LAYER ROUTING:
 *   Phase 6 replaces channel-based routing with explicit layer bytes in the
 *   SysEx message. editTarget still selects which layer a UI change affects
 *   (LayerA / LayerB / Both → SyxProto::kLayerA / kLayerB / kLayerBoth).
 *   The channelA / channelB atomics still exist because hardware MIDI input
 *   uses MIDI channels — each incoming CC is matched to a layer based on
 *   which channel it arrived on.
 *
 * PACED SEND REMOVED:
 *   The 1ms-per-CC paced output (timer + _pacedQueue) is gone. Phase 2's
 *   BANK_DUMP delivers an entire patch in one SysEx message; the firmware
 *   applies it under AudioNoInterrupts. No pacing required.
 */

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "CCDefinitions.h"
#include "SyxProtocol.h"

class JT8000ControllerProcessor : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::MidiInputCallback
{
public:
    JT8000ControllerProcessor();
    ~JT8000ControllerProcessor() override;

    // --- AudioProcessor overrides -------------------------------------------
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "JT-8000 Controller"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return true; }

    // FALSE — Ableton cannot load MIDI-only VST3 plugins. Dummy audio
    // buses workaround. See class-level comment.
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // --- Public API ---------------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    // =========================================================================
    // LAYER ROUTING STATE — atomic mirrors of the APVTS perf params.
    //
    // editTarget selects which layer byte goes into outbound SysEx messages
    // for UI-driven changes. channelA/B are used only on the INPUT side
    // (matching incoming hardware CCs to a layer).
    // =========================================================================
    enum class EditTarget : int { LayerA = 0, LayerB = 1, Both = 2 };

    std::atomic<int>         channelA   { 1 };
    std::atomic<int>         channelB   { 1 };
    std::atomic<EditTarget>  editTarget { EditTarget::LayerA };

    // When a BANK_DUMP_REQUEST is in flight, this records which layer's
    // values should be applied to APVTS when the reply arrives.
    // 0xFF = no sync pending. Set by requestLayerSync(), cleared by the
    // incoming BANK_DUMP handler.
    std::atomic<uint8_t>     pendingSyncLayer { 0xFF };

    void setMidiOutput(std::unique_ptr<juce::MidiOutput> output);

    // =========================================================================
    // SysEx output API (Phase 6)
    // =========================================================================

    // Send one SET_PARAM message. layer is one of SyxProto::kLayerA / kLayerB
    // / kLayerBoth / kLayerPerf / kLayerGlobalFx.
    void sendSysExSetParam(uint8_t layer, uint16_t paramId14, float nativeValue);

    // Send one BANK_DUMP containing every APVTS-bound param's current value.
    // Used by JP8000SyxImporter::loadPatch for atomic preset loading. The
    // firmware applies the whole dump under AudioNoInterrupts.
    void sendBankDump();

    // Request a BANK_DUMP from the firmware (7-byte BANK_DUMP_REQUEST).
    // The firmware replies with a full per-layer state dump that the plugin
    // parses in handleIncomingSysEx to update APVTS with the requested
    // layer's values.
    void sendBankDumpRequest();

    // Called when editTarget changes. Sends a BANK_DUMP_REQUEST to the
    // firmware so the UI can show the newly-selected layer's actual values.
    void requestLayerSync();

    // Step sequencer atomic edit — two SET_PARAMs back-to-back (Step Select
    // 0x0E10, then Step Value 0x0E11). Called by PluginEditor's grid handler
    // to bypass APVTS for transient step writes.
    void sendStepSeqEdit(int step, int value);

    juce::String getPatchName() const;
    void         setPatchName(const juce::String& name);

    void openMidiInput(const juce::String& deviceIdentifier);
    void closeMidiInput();
    juce::String getActiveMidiInputId() const;

    void sendNoteOn(int channel, int note, float velocity);
    void sendNoteOff(int channel, int note);
    void sendPitchWheel(int channel, int value);

    // =========================================================================
    // Echo suppression — public so JP8000SyxImporter can guard the BANK_DUMP
    // construction phase. Set true around bulk APVTS writes; the parameterChanged
    // callback skips firing SET_PARAM when it's true.
    // =========================================================================
    std::atomic<bool> _suppressEcho { false };

    bool isStandaloneMode() const;

private:
    static juce::AudioProcessor::BusesProperties makeBusLayout();
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& id, float newValue) override;

    // --- Incoming CC reflection (echo suppression) --------------------------
    // Hardware controllers send CCs; we update APVTS without firing a
    // round-trip SysEx by setting _suppressEcho around setValueNotifyingHost.
    void applyIncomingCC(int cc, int value);

    // --- Incoming SysEx (firmware -> plugin) --------------------------------
    // Phase 2 PARAM_VALUE replies and (future) unsolicited firmware pushes
    // arrive here. handleIncomingSysEx parses and reflects into APVTS the
    // same way applyIncomingCC does.
    void handleIncomingSysEx(const uint8_t* data, int length);

    // =========================================================================
    // SysEx-output FIFO
    //
    // Each slot holds one full SET_PARAM (15 bytes). BANK_DUMP is too large
    // for a fixed-size slot; it goes through a separate dynamic buffer
    // queued for output via the bigOutBuf member below.
    //
    // FIFO is single-producer (UI / parameterChanged on message thread)
    // single-consumer (audio thread, processBlock). juce::AbstractFifo
    // handles the lock-free synchronisation.
    // =========================================================================
    static constexpr int kSysExSlotBytes = 16;   // SET_PARAM is 15; round up
    static constexpr int kQueueSize      = 256;  // ~256 SET_PARAMs queued max

    struct SysExSlot { uint8_t data[kSysExSlotBytes]; uint8_t numBytes; };
    juce::AbstractFifo fifo { kQueueSize };
    SysExSlot          messageBuffer[kQueueSize];

    // Push a fully-formed SysEx message (F0 .. F7) onto the per-param FIFO.
    // Used by sendSysExSetParam and sendStepSeqEdit. Both callers know the
    // message fits in kSysExSlotBytes; longer messages should go via the
    // bigOutBuf path.
    void queueSysEx(const uint8_t* data, int len);

    // --- Large SysEx buffer (BANK_DUMP) -------------------------------------
    // BANK_DUMP can run ~1500-1700 bytes. Held in a dynamic byte buffer with
    // a flag set to indicate "ready for next processBlock to flush". Mutex
    // guards the buffer between message-thread writer and audio-thread
    // reader.
    juce::Array<uint8_t>   bigOutBuf;
    std::atomic<bool>      bigOutReady { false };
    juce::CriticalSection  bigOutLock;

    // --- MIDI output device (standalone) ------------------------------------
    std::unique_ptr<juce::MidiOutput> directMidiOut;
    juce::CriticalSection midiOutLock;

    // --- MIDI input device (standalone) -------------------------------------
    std::unique_ptr<juce::MidiInput> midiInputDevice;
    juce::String midiInputId;
    juce::CriticalSection midiInLock;
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& msg) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerProcessor)
};
