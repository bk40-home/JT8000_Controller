/*
 * PluginProcessor.cpp — JT-8000 Controller Processor
 *
 * Ableton-compatible MIDI CC mapper with dummy stereo audio buses.
 *
 * processBlock():
 *   1. Silence the audio buffer (dummy buses, no audio processing)
 *   2. Reflect incoming CCs into APVTS so the UI tracks hardware state
 *   3. Drain the outgoing CC FIFO into the host MIDI buffer
 *
 * Paced send:
 *   juce::Timer drip-feeds CCs at 1ms intervals to avoid overflowing
 *   the Teensy's USB MIDI receive buffer (~120ms for a full patch).
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"

static const juce::Identifier kPatchNameProp("patchName");

// =============================================================================
// Bus layout — Ableton workaround
//
// Ableton Live refuses to instantiate VST3 plugins with zero audio buses
// (Ableton bug ABLDEV-00057, open since 2019, still present in 2025).
// JUCE's own MidiLoggerPluginDemo uses PluginHostType().isAbletonLive()
// to conditionally add a stereo output.
//
// We always add stereo I/O to keep the moduleinfo.json deterministic —
// the audio buses are silenced in processBlock(). This avoids needing
// two different .vst3 bundles and ensures the Ableton plugin cache
// always sees the same bus layout.
// =============================================================================
juce::AudioProcessor::BusesProperties
JT8000ControllerProcessor::makeBusLayout()
{
    return BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

// =============================================================================
// Construction / Destruction
// =============================================================================
JT8000ControllerProcessor::JT8000ControllerProcessor()
    : AudioProcessor(makeBusLayout())
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
// isStandaloneMode
// =============================================================================
bool JT8000ControllerProcessor::isStandaloneMode() const
{
    return wrapperType == wrapperType_Standalone;
}

// =============================================================================
// processBlock — silence audio, filter MIDI, inject CCs
//
// Audio buses exist only as an Ableton compatibility shim.
//
// MIDI strategy:
//   - Incoming CCs are consumed (reflected into APVTS for UI update)
//     but NOT passed through. This plugin is a one-way CC injector.
//     If incoming CCs were passed through, any CC we send would echo
//     back from Ableton and create an infinite feedback loop.
//   - Notes, pitch bend, aftertouch, channel pressure pass through.
//   - Our own CCs from the FIFO are appended (these are the only CCs
//     that should reach the output).
// =============================================================================
void JT8000ControllerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    // Silence dummy audio buses
    buffer.clear();

    // --- Build a filtered copy: keep non-CC messages, consume CCs -----------
    juce::MidiBuffer filtered;

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isController())
        {
            // Reflect into APVTS for UI update — do NOT pass through
            applyIncomingCC(msg.getControllerNumber(), msg.getControllerValue());
        }
        else
        {
            // Notes, pitch bend, aftertouch etc. — pass through
            filtered.addEvent(msg, metadata.samplePosition);
        }
    }

    // --- Drain outgoing CC FIFO (our CCs only) ------------------------------
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        int s1, sz1, s2, sz2;
        fifo.prepareToRead(ready, s1, sz1, s2, sz2);

        // Place our CCs at the end of the buffer
        const int samplePos = filtered.isEmpty()
                            ? 0
                            : filtered.getLastEventTime();

        auto emitRange = [&](int start, int count)
        {
            for (int i = start; i < start + count; ++i)
            {
                const auto& slot = messageBuffer[i];
                filtered.addEvent(
                    juce::MidiMessage(slot.data, slot.numBytes), samplePos);
            }
        };

        emitRange(s1, sz1);
        emitRange(s2, sz2);
        fifo.finishedRead(ready);
    }

    // Replace the host buffer with our filtered version
    midiMessages.swapWith(filtered);
}

// =============================================================================
// applyIncomingCC — update APVTS/UI from an incoming CC, without sending
//
// Called when a CC arrives from the hardware (via host MidiBuffer or
// standalone MIDI input). Updates the APVTS parameter so the UI reflects
// the hardware state, but must NOT send a CC back out to the Teensy.
//
// Guard 1: The caller (handleIncomingMidiMessage / processBlock) never
//          queues the CC back out — only this function is called.
//
// Guard 2: _suppressEcho is set true around setValueNotifyingHost().
//          parameterChanged() checks this flag and skips sendCC().
//          setValueNotifyingHost fires the listener synchronously on the
//          same call stack, so the flag is guaranteed to be true when
//          parameterChanged runs.
// =============================================================================
void JT8000ControllerProcessor::applyIncomingCC(int cc, int value)
{
    if (cc < 0 || cc > 127)
        return;

    const CCParam* p = findParamByCC(cc);
    if (p == nullptr)
        return;

    if (p->style == ControlStyle::StepGrid)
        return;

    auto* param = apvts.getParameter(p->id);
    if (param == nullptr)
        return;

    float normVal;
    if (isChoiceParam(*p))
    {
        int idx = ccToChoiceIndex(juce::jlimit(0, 127, value), p->enumCount);
        normVal = param->convertTo0to1(static_cast<float>(idx));
    }
    else
    {
        normVal = param->convertTo0to1(static_cast<float>(juce::jlimit(0, 127, value)));
    }

    // Guard: tell parameterChanged() not to send a CC for this update
    _suppressEcho.store(true);
    param->setValueNotifyingHost(normVal);
    _suppressEcho.store(false);
}

// =============================================================================
// parameterChanged — convert to CC and send (UI/automation changes only)
//
// Skips sending when the change originated from an incoming CC
// (_suppressEcho == true). This is Guard 2 of the echo suppression:
//
//   incoming CC → applyIncomingCC → _suppressEcho = true
//              → setValueNotifyingHost → parameterChanged
//              → [guard active, skip sendCC] → done.
//
// UI-initiated changes (slider drag, combo selection) arrive here with
// _suppressEcho == false, so sendCC proceeds normally.
// =============================================================================
void JT8000ControllerProcessor::parameterChanged(const juce::String& id, float newValue)
{
    // Do not send a CC back out if this change came from an incoming CC
    if (_suppressEcho.load())
        return;

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
// sendCC
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
    {
        const juce::ScopedLock sl(midiOutLock);
        if (directMidiOut != nullptr)
            directMidiOut->sendMessageNow(msg);
    }

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
// Paced send
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
    stopTimer();
    buildPacedQueue();
    _pacedIndex = 0;
    _pacedSendActive.store(true);
    startTimer(1);
}

void JT8000ControllerProcessor::appendPacedCC(int cc, int value)
{
    _pacedQueue.push_back({
        static_cast<uint8_t>(juce::jlimit(0, 127, cc)),
        static_cast<uint8_t>(juce::jlimit(0, 127, value))
    });
}

void JT8000ControllerProcessor::timerCallback()
{
    if (_pacedIndex >= static_cast<int>(_pacedQueue.size()))
    {
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
// handleIncomingMidiMessage — standalone MIDI input callback
//
// Runs on the MIDI device thread, NOT the message thread.
//
// ECHO SUPPRESSION: Incoming CCs are consumed here — they update the
// APVTS/UI via applyIncomingCC() but are NEVER forwarded back out.
// Only performance messages (notes, pitch bend, aftertouch) pass through.
//
// This mirrors processBlock() which applies the same CC-filtering rule
// for the VST/DAW path.
// =============================================================================
void JT8000ControllerProcessor::handleIncomingMidiMessage(
    juce::MidiInput*, const juce::MidiMessage& msg)
{
    if (msg.isController())
    {
        // CC from hardware → update UI only, do NOT send back
        applyIncomingCC(msg.getControllerNumber(), msg.getControllerValue());
        return;
    }

    // Performance messages pass through (notes, pitch bend, aftertouch)
    if (msg.isNoteOnOrOff() || msg.isPitchWheel()
        || msg.isAftertouch() || msg.isChannelPressure())
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
