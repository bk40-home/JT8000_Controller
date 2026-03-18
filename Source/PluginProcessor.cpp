#include "PluginProcessor.h"
#include "PluginEditor.h"

JT8000ControllerProcessor::JT8000ControllerProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "JT8000State", createParameterLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        apvts.addParameterListener(kAllParams[i].id, this);
}

JT8000ControllerProcessor::~JT8000ControllerProcessor()
{
    for (int i = 0; i < kNumParams; ++i)
        apvts.removeParameterListener(kAllParams[i].id, this);
}

juce::AudioProcessorValueTreeState::ParameterLayout
JT8000ControllerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.reserve(kNumParams);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];
        params.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { p.id, 1 }, p.name, 0, 127, p.defVal));
    }
    return { params.begin(), params.end() };
}

juce::AudioProcessorEditor* JT8000ControllerProcessor::createEditor()
{
    return new JT8000ControllerEditor(*this);
}

void JT8000ControllerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        int s1, sz1, s2, sz2;
        fifo.prepareToRead(ready, s1, sz1, s2, sz2);
        const int ch = juce::jlimit(1, 16, midiChannel.load());

        for (int i = s1; i < s1 + sz1; ++i)
            if (messageBuffer[i].cc <= 127)
                midiMessages.addEvent(juce::MidiMessage::controllerEvent(
                    ch, messageBuffer[i].cc, messageBuffer[i].value), 0);
        for (int i = s2; i < s2 + sz2; ++i)
            if (messageBuffer[i].cc <= 127)
                midiMessages.addEvent(juce::MidiMessage::controllerEvent(
                    ch, messageBuffer[i].cc, messageBuffer[i].value), 0);

        fifo.finishedRead(ready);
    }
}

void JT8000ControllerProcessor::parameterChanged(const juce::String& id, float newValue)
{
    const CCParam* p = findParamByID(id);
    if (p) sendCC(p->cc, juce::roundToInt(newValue));
}

// Send via BOTH paths: direct MIDI (standalone) and FIFO (DAW)
void JT8000ControllerProcessor::sendCC(int cc, int value)
{
    const uint8_t ccByte  = static_cast<uint8_t>(juce::jlimit(0, 127, cc));
    const uint8_t valByte = static_cast<uint8_t>(juce::jlimit(0, 127, value));

    // Direct MIDI output (standalone mode — processBlock MidiBuffer is ignored)
    {
        const juce::ScopedLock sl(midiOutLock);
        if (directMidiOut != nullptr && ccByte <= 127)
        {
            const int ch = juce::jlimit(1, 16, midiChannel.load());
            directMidiOut->sendMessageNow(
                juce::MidiMessage::controllerEvent(ch, ccByte, valByte));
        }
    }

    // FIFO queue for DAW mode (processBlock reads this)
    int s1, sz1, s2, sz2;
    fifo.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)
        messageBuffer[s1] = { ccByte, valByte };
    fifo.finishedWrite(sz1 + sz2);
}

void JT8000ControllerProcessor::sendAllCCs()
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];
        if (auto* val = apvts.getRawParameterValue(p.id))
            sendCC(p.cc, juce::roundToInt(val->load()));
    }
}

void JT8000ControllerProcessor::setMidiOutput(std::unique_ptr<juce::MidiOutput> output)
{
    const juce::ScopedLock sl(midiOutLock);
    directMidiOut = std::move(output);
}

void JT8000ControllerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void JT8000ControllerProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            sendAllCCs();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JT8000ControllerProcessor();
}
