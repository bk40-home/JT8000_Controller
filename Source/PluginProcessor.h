/*
 * PluginProcessor.h — JT-8000 Controller Processor
 *
 * ADDITIONS (v3):
 *   - sendAllCCsPaced(): timer-based drip feed at ~1ms per CC to avoid
 *     overflowing the Teensy USB MIDI receive buffer.  Replaces the old
 *     burst-mode sendAllCCs() for all UI-triggered "Send All" actions.
 *   - isSendingAll(): lets the editor disable the Send All button while
 *     a paced send is in progress.
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
    std::atomic<int> midiChannel { 1 };

    void setMidiOutput(std::unique_ptr<juce::MidiOutput> output);

    /* Direct CC send — single CC, immediate. Used by knob changes and
       StepSequencerGrid select+value pairs. */
    void sendCC(int cc, int value);

    /* Paced send — snapshots the entire APVTS, then drip-feeds CCs at
       ~1ms intervals via a juce::Timer.  Safe for Teensy USB MIDI.
       Call from UI: Send All, Init, Load Patch, Load SYX. */
    void sendAllCCsPaced();

    /* Append extra CC pairs to the paced queue BEFORE starting the timer.
       Used by the editor to include step sequencer data in Send All. */
    void appendPacedCC(int cc, int value);

    /* True while the timer is drip-feeding CCs */
    bool isSendingAll() const { return _pacedSendActive.load(); }

    // --- Patch name (stored in APVTS state tree) ----------------------------
    juce::String getPatchName() const;
    void         setPatchName(const juce::String& name);

    // --- MIDI input for standalone mode -------------------------------------
    void openMidiInput(const juce::String& deviceIdentifier);
    void closeMidiInput();
    juce::String getActiveMidiInputId() const;

    /* Send note / pitch messages directly */
    void sendNoteOn(int channel, int note, float velocity);
    void sendNoteOff(int channel, int note);
    void sendPitchWheel(int channel, int value);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& id, float newValue) override;

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

    // --- Paced send (timer drip feed) ---------------------------------------
    // Teensy USB MIDI can overflow if 120+ CCs arrive in one burst.
    // The timer sends 1 CC per tick at 1ms intervals (~120ms total).
    struct PacedCC { uint8_t cc; uint8_t value; };
    std::vector<PacedCC> _pacedQueue;
    int _pacedIndex = 0;
    std::atomic<bool> _pacedSendActive { false };
    void timerCallback() override;
    void buildPacedQueue();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerProcessor)
};
