/*
 * PluginProcessor.h — JT-8000 Controller Processor
 *
 * MIDI CC mapper plugin with Ableton Live compatibility.
 *
 * ABLETON WORKAROUND:
 *   Ableton refuses to load VST3 plugins with no audio buses (ABLDEV-00057).
 *   We declare dummy stereo I/O buses and silence them in processBlock().
 *   isMidiEffect() returns false for the same reason — Ableton ignores
 *   MIDI-only plugins. This matches JUCE's MidiLoggerPluginDemo pattern.
 *
 * MIDI ECHO SUPPRESSION:
 *   Both processBlock (VST) and handleIncomingMidiMessage (standalone)
 *   consume incoming CCs without forwarding them back out.
 *   Only notes, pitch bend, aftertouch, and channel pressure pass through.
 *   Incoming CCs update the APVTS/UI via applyIncomingCC() only.
 *
 * LAYER / EDIT-TARGET ROUTING (Phase 2):
 *   The plugin maintains three pieces of per-performance state, exposed as
 *   APVTS params and mirrored to atomics for the audio thread:
 *     - channelA (1..16) — which MIDI channel Layer A of the firmware lives on
 *     - channelB (1..16) — which MIDI channel Layer B of the firmware lives on
 *     - editTarget      — which layer(s) the plugin's UI sends CCs to:
 *                           A     → send on channelA
 *                           B     → send on channelB
 *                           Both  → send on BOTH channels (one msg per)
 *   Notes / pitch bend follow the same rule, except Both uses channelA only
 *   (sending two noteOns would cause the firmware to play each note twice).
 *
 *   The seven perf CCs 140..146 (Perf Mode, Voice Split, Split Note,
 *   Balance, Edit Target, Ch A, Ch B) are LOCAL ONLY — they never go out
 *   as MIDI. CC numbers above 127 can't travel on class-compliant USB MIDI.
 *   These params exist only so state save/restore preserves them and so
 *   the user can view the setup in the plugin UI.
 *
 * FEATURES:
 *   - sendAllCCsPaced(): timer-based drip feed at ~1ms per CC
 *   - Incoming CC reflection: hardware CCs update the APVTS/UI
 */

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "CCDefinitions.h"

class JT8000ControllerProcessor : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::MidiInputCallback,
                                   private juce::Timer
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

    // FALSE — not TRUE. Ableton cannot load MIDI-only VST3 plugins.
    // We use dummy audio buses instead. See class-level comment.
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
    // LAYER ROUTING STATE — atomic mirrors of the APVTS perf params
    //
    // Kept in atomics because the MIDI path reads them from the audio thread
    // (processBlock) and from the MIDI-device thread (handleIncomingMidiMessage).
    // APVTS lookups aren't lock-free. parameterChanged() keeps these mirrors
    // up to date whenever the matching APVTS param changes.
    //
    // Ranges:
    //   channelA/B  — 1..16, defaults to 1.
    //   editTarget  — enum below, defaults to LayerA.
    // =========================================================================
    enum class EditTarget : int { LayerA = 0, LayerB = 1, Both = 2 };

    std::atomic<int>         channelA   { 1 };
    std::atomic<int>         channelB   { 1 };
    std::atomic<EditTarget>  editTarget { EditTarget::LayerA };

    void setMidiOutput(std::unique_ptr<juce::MidiOutput> output);
    void sendCC(int cc, int value);
    void sendAllCCsPaced();
    void appendPacedCC(int cc, int value);
    void appendPacedCC(int cc, int value, int channel);
    bool isSendingAll() const { return _pacedSendActive.load(); }

    juce::String getPatchName() const;
    void         setPatchName(const juce::String& name);

    void openMidiInput(const juce::String& deviceIdentifier);
    void closeMidiInput();
    juce::String getActiveMidiInputId() const;

    void sendNoteOn(int channel, int note, float velocity);
    void sendNoteOff(int channel, int note);
    void sendPitchWheel(int channel, int value);

    /* True when running as standalone (not inside a DAW) */
    bool isStandaloneMode() const;

private:
    // Bus layout — Ableton workaround (see MidiLoggerPluginDemo)
    static juce::AudioProcessor::BusesProperties makeBusLayout();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& id, float newValue) override;

    // --- Incoming CC echo suppression ---------------------------------------
    //
    // When a CC arrives from the hardware we update the APVTS so the UI
    // reflects the change.  setValueNotifyingHost() triggers
    // parameterChanged(), which would normally call sendCC() — creating
    // an infinite feedback loop:
    //
    //   Teensy → CC in → applyIncomingCC → setValueNotifyingHost
    //                   → parameterChanged → sendCC → Teensy → echo → …
    //
    // Two guards work together to prevent this:
    //
    //   1. handleIncomingMidiMessage / processBlock NEVER forward incoming
    //      CCs back out. Only notes, pitch bend, aftertouch pass through.
    //
    //   2. _suppressEcho is set true around setValueNotifyingHost() so
    //      that parameterChanged() skips sendCC() for the notification
    //      triggered by the incoming value.
    //
    // _suppressEcho is atomic because handleIncomingMidiMessage runs on
    // the MIDI device thread (not the message thread).
    // -----------------------------------------------------------------------
    void applyIncomingCC(int cc, int value);
    std::atomic<bool> _suppressEcho { false };

    // --- MIDI output FIFO (DAW processBlock path) ---------------------------
    struct MidiQueueMsg { uint8_t data[3]; int numBytes; };
    static constexpr int kQueueSize = 512;
    juce::AbstractFifo fifo { kQueueSize };
    MidiQueueMsg messageBuffer[kQueueSize];
    void queueMidiMessage(const juce::MidiMessage& msg);

    // --- MIDI output device (standalone) ------------------------------------
    std::unique_ptr<juce::MidiOutput> directMidiOut;
    juce::CriticalSection midiOutLock;

    // --- MIDI input device (standalone) -------------------------------------
    std::unique_ptr<juce::MidiInput> midiInputDevice;
    juce::String midiInputId;
    juce::CriticalSection midiInLock;
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& msg) override;

    // --- Paced send ---------------------------------------------------------
    // Each queued CC carries its own target channel (1-16) so performance
    // loads can interleave A-channel and B-channel CCs in a single queue
    // without racing against editTarget changes.
    // channel=0 means "use editTarget at send time" (legacy behaviour).
    struct PacedCC { uint8_t cc; uint8_t value; uint8_t channel; };
    std::vector<PacedCC> _pacedQueue;
    int _pacedIndex = 0;
    std::atomic<bool> _pacedSendActive { false };
    void timerCallback() override;
    void buildPacedQueue();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerProcessor)
};
