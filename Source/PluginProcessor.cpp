/*
 * PluginProcessor.cpp — JT-8000 Controller Processor
 *
 * v3: sendAllCCsPaced() replaces the burst-mode sendAllCCs().
 *     A juce::Timer drip-feeds CCs at 1ms intervals to avoid overflowing
 *     the Teensy's USB MIDI receive buffer.
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"

static const juce::Identifier kPatchNameProp("patchName");

// =============================================================================
// Construction / Destruction
// =============================================================================
JT8000ControllerProcessor::JT8000ControllerProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true))
    , apvts(*this, nullptr, "JT8000State", createParameterLayout())
{
    for (int i = 0; i < kNumParams; ++i)
        apvts.addParameterListener(kAllParams[i].id, this);

    if (!apvts.state.hasProperty(kPatchNameProp))
        apvts.state.setProperty(kPatchNameProp, "Init Patch", nullptr);
}

JT8000ControllerProcessor::~JT8000ControllerProcessor()
{
    stopTimer();

    for (int i = 0; i < kNumParams; ++i)
        apvts.removeParameterListener(kAllParams[i].id, this);

    closeMidiInput();
}

// =============================================================================
// Parameter Layout
// =============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
JT8000ControllerProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.reserve(kNumParams);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];

        if (isChoiceParam(p))
        {
            juce::StringArray choices;
            for (int c = 0; c < p.enumCount; ++c)
                choices.add(p.enumNames[c]);

            params.push_back(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { p.id, 1 }, p.name, choices, p.defVal));
        }
        else
        {
            params.push_back(std::make_unique<juce::AudioParameterInt>(
                juce::ParameterID { p.id, 1 }, p.name, 0, 127, p.defVal));
        }
    }
    return { params.begin(), params.end() };
}

// =============================================================================
// Editor
// =============================================================================
juce::AudioProcessorEditor* JT8000ControllerProcessor::createEditor()
{
    return new JT8000ControllerEditor(*this);
}

// =============================================================================
// processBlock — drain FIFO into host MIDI buffer
// =============================================================================
void JT8000ControllerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    const int ready = fifo.getNumReady();
    if (ready <= 0) return;

    int s1, sz1, s2, sz2;
    fifo.prepareToRead(ready, s1, sz1, s2, sz2);

    auto emitRange = [&](int start, int count)
    {
        for (int i = start; i < start + count; ++i)
            midiMessages.addEvent(
                juce::MidiMessage(messageBuffer[i].data, messageBuffer[i].numBytes), 0);
    };

    emitRange(s1, sz1);
    emitRange(s2, sz2);
    fifo.finishedRead(ready);
}

// =============================================================================
// parameterChanged — convert to CC and send immediately (single knob change)
// =============================================================================
void JT8000ControllerProcessor::parameterChanged(const juce::String& id, float newValue)
{
    const CCParam* p = findParamByID(id);
    if (!p) return;

    int ccVal;
    if (isChoiceParam(*p))
        ccVal = choiceIndexToCC(juce::roundToInt(newValue), p->enumCount, p->ccLUT);
    else
        ccVal = juce::roundToInt(newValue);

    sendCC(p->cc, ccVal);
}

// =============================================================================
// sendCC — single CC, immediate (knob tweak, step grid pair)
// =============================================================================
void JT8000ControllerProcessor::sendCC(int cc, int value)
{
    const int ch = juce::jlimit(1, 16, midiChannel.load());
    queueMidiMessage(
        juce::MidiMessage::controllerEvent(ch, cc & 0x7F, value & 0x7F));
}

// =============================================================================
// queueMidiMessage — push to FIFO + direct output (standalone)
// =============================================================================
void JT8000ControllerProcessor::queueMidiMessage(const juce::MidiMessage& msg)
{
    // Direct output for standalone mode
    {
        const juce::ScopedLock sl(midiOutLock);
        if (directMidiOut != nullptr)
            directMidiOut->sendMessageNow(msg);
    }

    // Queue for processBlock (DAW / VST3 path)
    const int rawSize = juce::jmin(msg.getRawDataSize(), 3);
    if (rawSize <= 0) return;

    int s1, sz1, s2, sz2;
    fifo.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)
    {
        auto& slot = messageBuffer[s1];
        std::memcpy(slot.data, msg.getRawData(), static_cast<size_t>(rawSize));
        slot.numBytes = rawSize;
    }
    fifo.finishedWrite(sz1 + sz2);
}

// =============================================================================
// PACED SEND — timer-based drip feed
//
// Why: The Teensy USB MIDI receive buffer is finite (~64-128 messages).
// Firing 120+ CCs in a tight loop overflows it — messages get silently
// dropped, causing random parameters to not apply.  The HTML editor
// avoids this naturally (JS event loop adds ~0.1-1ms between sends).
//
// How: buildPacedQueue() snapshots all APVTS values into _pacedQueue.
// startTimer(1) fires timerCallback() every 1ms, sending one CC per
// tick.  At 120 CCs, the full patch takes ~120ms — imperceptible.
// =============================================================================

void JT8000ControllerProcessor::buildPacedQueue()
{
    _pacedQueue.clear();
    _pacedQueue.reserve(kNumParams);

    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];
        if (p.style == ControlStyle::StepGrid) continue;

        if (auto* val = apvts.getRawParameterValue(p.id))
        {
            uint8_t ccVal;
            if (isChoiceParam(p))
                ccVal = static_cast<uint8_t>(choiceIndexToCC(
                    juce::roundToInt(val->load()), p.enumCount, p.ccLUT));
            else
                ccVal = static_cast<uint8_t>(juce::roundToInt(val->load()));

            _pacedQueue.push_back({ static_cast<uint8_t>(p.cc), ccVal });
        }
    }
}

void JT8000ControllerProcessor::sendAllCCsPaced()
{
    // If already sending, restart from the beginning
    stopTimer();

    buildPacedQueue();
    _pacedIndex = 0;
    _pacedSendActive.store(true);

    // 1ms per CC — fast enough to feel instant, slow enough for Teensy
    startTimer(1);
}

void JT8000ControllerProcessor::appendPacedCC(int cc, int value)
{
    // Append to the queue. Call AFTER sendAllCCsPaced() but the timer
    // will still be running — new items get picked up naturally since
    // timerCallback checks _pacedQueue.size() each tick.
    _pacedQueue.push_back({
        static_cast<uint8_t>(juce::jlimit(0, 127, cc)),
        static_cast<uint8_t>(juce::jlimit(0, 127, value))
    });
}

void JT8000ControllerProcessor::timerCallback()
{
    if (_pacedIndex >= static_cast<int>(_pacedQueue.size()))
    {
        // Done — stop timer
        stopTimer();
        _pacedSendActive.store(false);
        return;
    }

    const auto& item = _pacedQueue[static_cast<size_t>(_pacedIndex)];
    sendCC(item.cc, item.value);
    ++_pacedIndex;
}

// =============================================================================
// Note / Pitch helpers
// =============================================================================
void JT8000ControllerProcessor::sendNoteOn(int channel, int note, float velocity)
{
    queueMidiMessage(juce::MidiMessage::noteOn(channel, note, velocity));
}

void JT8000ControllerProcessor::sendNoteOff(int channel, int note)
{
    queueMidiMessage(juce::MidiMessage::noteOff(channel, note, (uint8_t)0));
}

void JT8000ControllerProcessor::sendPitchWheel(int channel, int value)
{
    queueMidiMessage(juce::MidiMessage::pitchWheel(channel, value));
}

// =============================================================================
// MIDI output device (standalone)
// =============================================================================
void JT8000ControllerProcessor::setMidiOutput(std::unique_ptr<juce::MidiOutput> output)
{
    const juce::ScopedLock sl(midiOutLock);
    directMidiOut = std::move(output);
}

// =============================================================================
// Patch name
// =============================================================================
juce::String JT8000ControllerProcessor::getPatchName() const
{
    return apvts.state.getProperty(kPatchNameProp, "Init Patch").toString();
}

void JT8000ControllerProcessor::setPatchName(const juce::String& name)
{
    apvts.state.setProperty(kPatchNameProp, name, nullptr);
}

// =============================================================================
// MIDI input device (standalone)
// =============================================================================
void JT8000ControllerProcessor::openMidiInput(const juce::String& deviceIdentifier)
{
    closeMidiInput();
    if (deviceIdentifier.isEmpty()) return;

    auto device = juce::MidiInput::openDevice(deviceIdentifier, this);
    if (device != nullptr)
    {
        device->start();
        const juce::ScopedLock sl(midiInLock);
        midiInputDevice = std::move(device);
        midiInputId = deviceIdentifier;
    }
}

void JT8000ControllerProcessor::closeMidiInput()
{
    const juce::ScopedLock sl(midiInLock);
    if (midiInputDevice != nullptr)
    {
        midiInputDevice->stop();
        midiInputDevice.reset();
    }
    midiInputId.clear();
}

juce::String JT8000ControllerProcessor::getActiveMidiInputId() const
{
    const juce::ScopedLock sl(const_cast<juce::CriticalSection&>(midiInLock));
    return midiInputId;
}

// =============================================================================
// handleIncomingMidiMessage — forward channel voice messages to output
// =============================================================================
void JT8000ControllerProcessor::handleIncomingMidiMessage(
    juce::MidiInput*, const juce::MidiMessage& msg)
{
    if (msg.isNoteOnOrOff() || msg.isController()
        || msg.isPitchWheel() || msg.isAftertouch()
        || msg.isChannelPressure())
    {
        queueMidiMessage(msg);
    }
}

// =============================================================================
// State save / recall
// =============================================================================
void JT8000ControllerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void JT8000ControllerProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
    {
        if (xml->hasTagName(apvts.state.getType()))
        {
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            sendAllCCsPaced();
        }
    }
}

// =============================================================================
// Plugin instantiation
// =============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JT8000ControllerProcessor();
}
