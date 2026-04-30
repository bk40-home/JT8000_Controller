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
// Three-way dispatch:
//
//   1. The special routing params (perf_midi_ch_a, perf_midi_ch_b,
//      perf_edit_target) update the atomic mirrors so the audio thread can
//      read the current routing state without touching APVTS.
//
//   2. All isLocalOnly params (the seven perf CCs 140..146) bail before the
//      CC emit — CCs above 127 cannot travel on USB MIDI and the firmware
//      wouldn't accept them anyway.
//
//   3. Remaining params (the regular patch CCs) go through sendCC() which
//      routes based on editTarget / channelA / channelB.
//
// Echo-suppression guard stays exactly as before: if the change came from
// an incoming CC we don't re-send. See applyIncomingCC() for Guard 1/2.
// =============================================================================
void JT8000ControllerProcessor::parameterChanged(const juce::String& id, float newValue)
{
    const CCParam* p = findParamByID(id);
    if (!p) return;

    // ---- Routing-param mirrors: update atomics regardless of echo guard ----
    // These changes must be visible on the audio thread even when the change
    // came from an incoming CC — the _suppressEcho guard only prevents
    // re-sending, not state update.
    if (id == "perf_midi_ch_a")
    {
        // newValue is a choice index 0..15; +1 gives MIDI channel 1..16.
        channelA.store(juce::jlimit(1, 16, juce::roundToInt(newValue) + 1));
    }
    else if (id == "perf_midi_ch_b")
    {
        channelB.store(juce::jlimit(1, 16, juce::roundToInt(newValue) + 1));
    }
    else if (id == "perf_edit_target")
    {
        const int idx = juce::jlimit(0, 2, juce::roundToInt(newValue));
        editTarget.store(static_cast<EditTarget>(idx));
    }

    // ---- Local-only params never go out as MIDI ----------------------------
    // Includes every entry in CCDefinitions.h with isLocalOnly = true.
    // These are the seven perf CCs (140..146). They live in APVTS purely
    // so state save/restore and UI binding work.
    if (p->isLocalOnly) return;

    // ---- Echo guard: don't re-emit a CC that CAME from the hardware --------
    if (_suppressEcho.load()) return;

    int ccVal;
    if (isChoiceParam(*p))
        ccVal = choiceIndexToCC(juce::roundToInt(newValue), p->enumCount, p->ccLUT);
    else
        ccVal = juce::roundToInt(newValue);

    sendCC(p->cc, ccVal);
}

// =============================================================================
// sendCC — emit a CC to one or both layer channels based on edit target
//
// Routing:
//   LayerA → one message on channelA
//   LayerB → one message on channelB
//   Both   → two messages, one on each channel
//
// channelA and channelB are atomics updated by parameterChanged() when the
// user changes the perf_midi_ch_a / perf_midi_ch_b combos. Both default
// to 1 on a fresh plugin instance.
//
// The "Both" case deliberately sends two messages rather than broadcasting
// on channel 0 (MIDI's omni-transmit channel): the firmware filters by
// exact channel match (no omni support), so we honour its expectation.
// =============================================================================
void JT8000ControllerProcessor::sendCC(int cc, int value)
{
    const int chA = juce::jlimit(1, 16, channelA.load());
    const int chB = juce::jlimit(1, 16, channelB.load());
    const int ccMasked = cc & 0x7F;
    const int valMasked = value & 0x7F;

    switch (editTarget.load())
    {
        case EditTarget::LayerA:
            queueMidiMessage(juce::MidiMessage::controllerEvent(chA, ccMasked, valMasked));
            break;

        case EditTarget::LayerB:
            queueMidiMessage(juce::MidiMessage::controllerEvent(chB, ccMasked, valMasked));
            break;

        case EditTarget::Both:
            queueMidiMessage(juce::MidiMessage::controllerEvent(chA, ccMasked, valMasked));
            // Only emit the second message if channels differ — avoids a
            // duplicate that the firmware would apply twice in "Dual"
            // configurations where both layers share a channel.
            if (chA != chB)
                queueMidiMessage(juce::MidiMessage::controllerEvent(chB, ccMasked, valMasked));
            break;
    }
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

        // Step grids are serialised via a dedicated path (sendCC 17/18 per step).
        if (p.style == ControlStyle::StepGrid) continue;

        // Perf CCs 140..146 can't travel on USB MIDI (>127 hits the 7-bit wall).
        // They're state-only — stored in APVTS for save/restore, never emitted.
        if (p.isLocalOnly) continue;

        if (auto* val = apvts.getRawParameterValue(p.id))
        {
            uint8_t ccVal;
            if (isChoiceParam(p))
                ccVal = static_cast<uint8_t>(choiceIndexToCC(
                    juce::roundToInt(val->load()), p.enumCount, p.ccLUT));
            else
                ccVal = static_cast<uint8_t>(juce::roundToInt(val->load()));

            // channel=0 → route via editTarget at send time
            _pacedQueue.push_back({ static_cast<uint8_t>(p.cc), ccVal, 0 });
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
    // channel=0 → route via editTarget at send time
    _pacedQueue.push_back({
        static_cast<uint8_t>(juce::jlimit(0, 127, cc)),
        static_cast<uint8_t>(juce::jlimit(0, 127, value)),
        0
    });
}

void JT8000ControllerProcessor::appendPacedCC(int cc, int value, int channel)
{
    // channel=1..16 → send directly on that channel, bypassing editTarget
    _pacedQueue.push_back({
        static_cast<uint8_t>(juce::jlimit(0, 127, cc)),
        static_cast<uint8_t>(juce::jlimit(0, 127, value)),
        static_cast<uint8_t>(juce::jlimit(0, 16, channel))
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

    if (item.channel > 0 && item.channel <= 16)
    {
        // Channel-pinned CC — bypass editTarget routing, send directly
        // on the specified channel. Used by loadPerformance() to ensure
        // A-layer CCs go to channel A and B-layer CCs go to channel B
        // regardless of what editTarget is set to at send time.
        queueMidiMessage(juce::MidiMessage::controllerEvent(
            item.channel, item.cc, item.value));
    }
    else
    {
        // channel=0 → route through sendCC() which uses editTarget
        sendCC(item.cc, item.value);
    }

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
