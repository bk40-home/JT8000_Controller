#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "CCDefinitions.h"

class JT8000ControllerProcessor : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener
{
public:
    JT8000ControllerProcessor();
    ~JT8000ControllerProcessor() override;

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "JT-8000 Controller"; }
    bool acceptsMidi()  const override { return false; }
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

    juce::AudioProcessorValueTreeState apvts;
    void sendAllCCs();
    std::atomic<int> midiChannel { 1 };

    // Direct MIDI output for standalone mode (set from editor)
    void setMidiOutput(std::unique_ptr<juce::MidiOutput> output);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& id, float newValue) override;

    // Send a CC immediately via direct MIDI output AND queue for processBlock
    void sendCC(int cc, int value);

    // FIFO for processBlock (DAW mode)
    struct CCMessage { uint8_t cc; uint8_t value; };
    static constexpr int kQueueSize = 256;
    juce::AbstractFifo fifo { kQueueSize };
    CCMessage messageBuffer[kQueueSize];

    // Direct MIDI output device (standalone mode)
    std::unique_ptr<juce::MidiOutput> directMidiOut;
    juce::CriticalSection midiOutLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JT8000ControllerProcessor)
};
