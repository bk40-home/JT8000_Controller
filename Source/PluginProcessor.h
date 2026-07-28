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
#include "NrpnLink.h"

// Phase J2: the embedded firmware engine.  JT8000_HAS_ENGINE is defined by
// CMake only when JT_ENGINE_DIR is set — see CMakeLists.txt.
#ifndef JT8000_HAS_ENGINE
 #define JT8000_HAS_ENGINE 0
#endif
#ifndef JT8000_SYNTH_TARGET
 #define JT8000_SYNTH_TARGET 0
#endif
#if JT8000_HAS_ENGINE
 #include "EngineHost.h"
#endif

class JT8000ControllerProcessor : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::MidiInputCallback
{
public:
    JT8000ControllerProcessor();
    ~JT8000ControllerProcessor() override;

    // --- AudioProcessor overrides -------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
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

    // The identifier of the currently-open direct MIDI-out device, or "" if
    // none.  The editor uses this to restore the MIDI-Out dropdown's shown
    // selection when it is rebuilt (the processor keeps the device open across
    // editor lifetimes, so the combo must be told what is already selected —
    // otherwise it wrongly reads "No MIDI Output" while control traffic still
    // flows).  Reads under midiOutLock: safe from the message thread.
    juce::String getDirectMidiOutId() const
    {
        const juce::ScopedLock sl(midiOutLock);
        return directMidiOutId;
    }

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

    // --- Phase E: NRPN wire (see NrpnLink.h for the protocol contract) ------
    void sendParamNrpn(int v1cc, int ccVal);       // one edit -> 6 short msgs
    // Option B: send a v2-only param (no v1 CC) by ParamID directly — the seq
    // aux lane.  Same 6-CC NRPN, addressed by ParamID instead of via the bridge.
    void sendParamNrpnById(uint16_t paramId, int ccVal);
    void requestResync();                          // full-state pull (0x3F00)
private:
    void queueShortMidi(const NrpnLink::Short3* msgs, size_t n);
    // One decoder per independent input stream (VST3 bus vs standalone
    // direct-in) — NRPN selection state must never interleave.
    NrpnLink::Decoder nrpnDecodeHost_;
    NrpnLink::Decoder nrpnDecodeDirect_;

    // ---- Embedded engine (Phase J2) ----------------------------------------
    // Compiled only when CMake's JT_ENGINE_DIR points at a jteensy8000-v2
    // checkout; without it this class builds exactly as before (controller
    // only) and every engine call site compiles away.
#if JT8000_HAS_ENGINE
public:
    // Modes: Controller = today's behaviour, engine silent, MIDI to hardware.
    //        Sound      = embedded engine audible, hardware MIDI SUPPRESSED.
    //        Both       = engine audible AND hardware fed — A/B the desktop
    //                     engine against the Teensy on the same wire bytes.
    enum EngineMode { kModeController = 0, kModeSound = 1, kModeBoth = 2 };
private:
    std::unique_ptr<EngineHost> _engine;        // created in ctor: no lifecycle
                                                // races vs the audio thread
    juce::MidiMessageCollector  _engineInbox;   // cross-thread MIDI funnel —
                                                // audio thread is the ONLY
                                                // caller into _engine itself
    std::atomic<int> _engineMode {
    #if JT8000_SYNTH_TARGET
        kModeSound                              // the Synth target exists to sound
    #else
        kModeController                         // the Controller stays a controller
    #endif
    };
    int    _engineModePrev   = -1;              // audio-thread edge detect
    double _hostSampleRate   = 0.0;             // for the 44.1 k warning
    std::atomic<bool> _inboxReady { false };    // collector reset() has run —
                                                // producers must not enqueue
                                                // before the rate is known
    bool engineAudible() const
    {
        return _engine != nullptr && _engineMode.load() != kModeController;
    }
#else
    bool engineAudible() const { return false; }
#endif

    // ---- Engine status feed (Phase J1d, NRPN 0x3FFF) -----------------------
    // Word layout per firmware SynthCore::statusWord() — unpack with the
    // ValueLaw::status* helpers.  _statusCount increments once per received
    // status cluster (including the >= 1 Hz heartbeat), so the editor can
    // derive link liveness from count movement, not word changes.
    std::atomic<uint16_t> _statusWord  { 0 };
    std::atomic<uint32_t> _statusCount { 0 };
public:
    uint16_t statusWord()  const { return _statusWord.load(); }
    uint32_t statusCount() const { return _statusCount.load(std::memory_order_relaxed); }

    // Editor-facing engine surface — real when the engine is compiled in,
    // inert stubs otherwise so the editor code needs no #if forest.
#if JT8000_HAS_ENGINE
    bool   engineAvailable() const  { return _engine != nullptr; }
    int    engineMode() const       { return _engineMode.load(); }
    void   setEngineMode(int m);
    double hostSampleRate() const   { return _hostSampleRate; }
    static constexpr double engineSampleRate() { return EngineHost::kEngineSampleRate; }
#else
    bool   engineAvailable() const  { return false; }
    int    engineMode() const       { return 0; }
    void   setEngineMode(int)       {}
    double hostSampleRate() const   { return 0.0; }
    static constexpr double engineSampleRate() { return 44100.0; }
#endif


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
    // Aux lane (Stage B/C/D): step write + dest/depth, all by ParamID (Option B).
    void sendStepSeqAuxEdit(int step, int value);

    juce::String getPatchName() const;
    void         setPatchName(const juce::String& name);

    // --- MIDI input (standalone) --------------------------------------------
    // Multi-input: any number of devices may be open simultaneously. The
    // editor presents a checklist; each ticked device is opened here.
    //   openMidiInputs()  — open exactly the given set, closing the rest.
    //   isMidiInputOpen() — query for the editor checklist state.
    // The legacy single-device methods are retained as thin wrappers so any
    // existing caller keeps working.
    void openMidiInput(const juce::String& deviceIdentifier);   // legacy: open just this one
    void closeMidiInput();                                       // legacy: close all
    juce::String getActiveMidiInputId() const;                  // legacy: first open id, or ""

    void openMidiInputs(const juce::StringArray& deviceIdentifiers);
    bool isMidiInputOpen(const juce::String& deviceIdentifier) const;

    void sendNoteOn(int channel, int note, float velocity);
    void sendNoteOff(int channel, int note);
    void sendPitchWheel(int channel, int value);

    // Forward an already-formed short MIDI message (note/PB/CC/etc.) to the
    // Teensy via the direct output port. Used by the input router. No-op if
    // no direct device is open.
    void forwardToTeensy(const juce::MidiMessage& msg);

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
    // Phase E: a full-patch NRPN burst on the host-bus FALLBACK path is
    // ~150 params x 6 short messages ~= 900 slots; 1024 gives headroom.
    // (With a direct MIDI device open — the primary transport — the FIFO
    // is bypassed entirely, exactly as before.)
    static constexpr int kQueueSize      = 1024;

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

    // --- MIDI output device (standalone AND VST3) ---------------------------
    std::unique_ptr<juce::MidiOutput> directMidiOut;
    juce::String          directMidiOutId;   // identifier of the open out device,
                                              // "" if none. Read by the input
                                              // router to apply the Auto rule
                                              // (input == this id => listen-only).
    mutable juce::CriticalSection midiOutLock;

    // --- MIDI input devices (standalone, multi-input) -----------------------
    // Any number of inputs open at once. handleIncomingMidiMessage dispatches
    // by source: the device whose identifier matches directMidiOutId is the
    // Teensy return channel (listen-only); every other device is a controller
    // whose performance MIDI is forwarded to the Teensy.
    juce::OwnedArray<juce::MidiInput> midiInputDevices;
    juce::CriticalSection midiInLock;
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& msg) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerProcessor)
};
