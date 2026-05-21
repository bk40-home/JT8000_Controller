/*
 * PluginProcessor.cpp — JT-8000 Controller Processor (Phase 6)
 *
 * SysEx-only output. CC output path removed. The plugin emits SET_PARAM and
 * BANK_DUMP messages in the JT-8000 editor protocol (see SyxProtocol.h).
 *
 * processBlock():
 *   1. Silence the audio buffer (Ableton dummy buses, no audio processing)
 *   2. Reflect incoming CCs into APVTS so the UI tracks hardware state
 *   3. Reflect incoming SysEx PARAM_VALUE messages into APVTS
 *   4. Drain the outgoing per-param SysEx FIFO into the host MIDI buffer
 *   5. If a BANK_DUMP is queued, append it as one large MidiMessage
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamMapJuce.h"
#include "SyxProtocol.h"
#include "JT8000Log.h"

static const juce::Identifier kPatchNameProp("patchName");

// =============================================================================
// Bus layout — Ableton workaround (unchanged from previous phases)
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
    for (int i = 0; i < kNumParams; ++i)
        apvts.removeParameterListener(kAllParams[i].id, this);

    closeMidiInput();
}

// =============================================================================
// Parameter Layout — unchanged. APVTS still stores values in 0..127 / choice
// index form. Conversion to native float happens at SysEx send time.
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

bool JT8000ControllerProcessor::isStandaloneMode() const
{
    return wrapperType == wrapperType_Standalone;
}

// =============================================================================
// processBlock — silence audio, filter MIDI, drain SysEx FIFO
//
// Same MIDI strategy as previous phases: incoming CCs reflect into APVTS via
// applyIncomingCC and are NOT passed through; notes/pitch bend/aftertouch
// pass through unchanged. New in Phase 6: incoming SysEx is parsed for
// PARAM_VALUE messages from the firmware (also NOT passed through), and the
// outbound SysEx FIFO is drained instead of the old CC FIFO.
// =============================================================================
void JT8000ControllerProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    juce::MidiBuffer filtered;

    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        if (msg.isController())
        {
            // Hardware CC -> APVTS. Don't pass through.
            applyIncomingCC(msg.getControllerNumber(), msg.getControllerValue());
        }
        else if (msg.isSysEx())
        {
            // Firmware SysEx (PARAM_VALUE) -> APVTS. Don't pass through.
            // SysEx data excludes F0/F7; rebuild the full F0..F7 buffer that
            // handleIncomingSysEx expects.
            const int sxLen = msg.getSysExDataSize();
            const auto* sxData = msg.getSysExData();
            // Reassemble F0 .. F7 envelope.
            juce::Array<uint8_t> full;
            full.resize(sxLen + 2);
            full.set(0, 0xF0);
            for (int i = 0; i < sxLen; ++i) full.set(i + 1, sxData[i]);
            full.set(sxLen + 1, 0xF7);
            handleIncomingSysEx(full.getRawDataPointer(), full.size());
        }
        else
        {
            filtered.addEvent(msg, metadata.samplePosition);
        }
    }

    // --- Drain per-param SysEx FIFO ----------------------------------------
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        int s1, sz1, s2, sz2;
        fifo.prepareToRead(ready, s1, sz1, s2, sz2);

        const int samplePos = filtered.isEmpty()
                            ? 0
                            : filtered.getLastEventTime();

        auto emitRange = [&](int start, int count)
        {
            for (int i = start; i < start + count; ++i)
            {
                const auto& slot = messageBuffer[i];

                // SysEx messages need explicit construction. JUCE's
                // createSysExMessage takes the BODY (between F0 and F7) and
                // adds the envelope itself. Short messages (notes/PB) use
                // the raw-data constructor.
                if (slot.numBytes >= 2 && slot.data[0] == 0xF0
                                       && slot.data[slot.numBytes - 1] == 0xF7)
                {
                    auto msg = juce::MidiMessage::createSysExMessage(
                        slot.data + 1, (int)slot.numBytes - 2);
                    filtered.addEvent(msg, samplePos);
                }
                else
                {
                    filtered.addEvent(
                        juce::MidiMessage(slot.data, (int)slot.numBytes),
                        samplePos);
                }
            }
        };

        emitRange(s1, sz1);
        emitRange(s2, sz2);
        fifo.finishedRead(ready);
    }

    // --- Drain BANK_DUMP buffer if ready ------------------------------------
    if (bigOutReady.load())
    {
        // Move under the lock so the message thread can immediately re-fill.
        juce::Array<uint8_t> snapshot;
        {
            const juce::ScopedLock sl(bigOutLock);
            snapshot.swapWith(bigOutBuf);
            bigOutReady.store(false);
        }

        if (snapshot.size() >= 2)
        {
            const auto* d = snapshot.getRawDataPointer();
            const int   n = snapshot.size();
            if (d[0] == 0xF0 && d[n - 1] == 0xF7)
            {
                const int samplePos = filtered.isEmpty() ? 0
                                                         : filtered.getLastEventTime();
                auto msg = juce::MidiMessage::createSysExMessage(d + 1, n - 2);
                filtered.addEvent(msg, samplePos);
            }
        }
    }

    midiMessages.swapWith(filtered);
}

// =============================================================================
// applyIncomingCC — same as previous phases. Hardware CC reflects into APVTS;
// _suppressEcho prevents the parameterChanged handler from firing a SysEx.
// =============================================================================
void JT8000ControllerProcessor::applyIncomingCC(int cc, int value)
{
    if (cc < 0 || cc > 127) return;

    const CCParam* p = findParamByCC(cc);
    if (p == nullptr) return;

    if (p->style == ControlStyle::StepGrid) return;

    auto* param = apvts.getParameter(p->id);
    if (param == nullptr) return;

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

    _suppressEcho.store(true);
    param->setValueNotifyingHost(normVal);
    _suppressEcho.store(false);
}

// =============================================================================
// handleIncomingSysEx — parse firmware SysEx and reflect into APVTS
//
// We accept PARAM_VALUE (firmware -> editor) messages here. Other JT-8000
// message types (SET_PARAM, GET_PARAM, BANK_DUMP_REQUEST) are firmware-bound
// commands; the plugin should never receive them. BANK_DUMP from the
// firmware is reserved for a future GET-style request and is currently not
// expected to arrive unsolicited.
// =============================================================================
void JT8000ControllerProcessor::handleIncomingSysEx(const uint8_t* data, int length)
{
    if (length < (int)SyxProto::kMsgBankDumpReqLen) return;

    // Validate envelope.
    if (data[0]                                 != 0xF0)              return;
    if (data[SyxProto::kOffsetMfrId]            != SyxProto::kMfrId)  return;
    if (data[SyxProto::kOffsetSubIdJ]           != SyxProto::kSubIdJ) return;
    if (data[SyxProto::kOffsetSubIdT]           != SyxProto::kSubIdT) return;

    const uint8_t msg = data[SyxProto::kOffsetMsgType];

    // ---- PARAM_VALUE (single param echo from firmware) ----------------------
    if (msg == SyxProto::kMsgParamValue)
    {
        if (length != (int)SyxProto::kMsgParamValueLen) return;
        if (data[length - 1] != 0xF7) return;

        const uint16_t pid = SyxProto::paramIdFromBytes(
            data[SyxProto::kOffsetPidHi], data[SyxProto::kOffsetPidLo]);
        const float value = SyxProto::decodeFloat(&data[SyxProto::kOffsetFloat]);

        for (int i = 0; i < kNumParams; ++i)
        {
            const auto& p = kAllParams[i];
            if (ParamMapJuce::paramId14ForCC(p.cc) != pid) continue;

            const uint8_t cc = ParamMapJuce::nativeFloatToCCByte(pid, value);
            applyIncomingCC(p.cc, (int)cc);
            return;
        }
        return;
    }

    // ---- BANK_DUMP (firmware reply to our BANK_DUMP_REQUEST) ----------------
    if (msg == SyxProto::kMsgBankDump)
    {
        if (length < (int)(SyxProto::kBankDumpHeaderSize + 1)) return;
        if (data[length - 1] != 0xF7) return;

        // Decode 14-bit entry count
        const uint16_t countHi = data[SyxProto::kOffsetMsgType + 1] & 0x7F;
        const uint16_t countLo = data[SyxProto::kOffsetMsgType + 2] & 0x7F;
        const uint16_t entryCount = (uint16_t)((countHi << 7) | countLo);

        // Validate length matches declared count
        const int expectedLen = (int)(SyxProto::kBankDumpHeaderSize
                              + (size_t)entryCount * SyxProto::kBankDumpEntrySize
                              + 1);
        if (length != expectedLen) return;

        // Which layer are we syncing to? If no sync is pending, ignore the dump.
        const uint8_t wantLayer = pendingSyncLayer.load();
        if (wantLayer == 0xFF) return;

        // Suppress echo while applying — we don't want to re-send these back
        _suppressEcho.store(true);

        const uint8_t* p = data + SyxProto::kBankDumpHeaderSize;
        for (uint16_t i = 0; i < entryCount; ++i)
        {
            const uint8_t  layer = p[0];
            const uint16_t pid   = SyxProto::paramIdFromBytes(p[1], p[2]);
            const float    value = SyxProto::decodeFloat(&p[3]);
            p += SyxProto::kBankDumpEntrySize;

            // Only apply entries for the layer we want to display.
            // Perf and GlobalFX scope entries are always applied (they're global).
            const bool isPatchLayer = (layer == SyxProto::kLayerA || layer == SyxProto::kLayerB);
            if (isPatchLayer && layer != wantLayer) continue;

            // Find the CC for this ParamID and apply to APVTS
            for (int j = 0; j < kNumParams; ++j)
            {
                const auto& param = kAllParams[j];
                if (ParamMapJuce::paramId14ForCC(param.cc) != pid) continue;

                const uint8_t cc = ParamMapJuce::nativeFloatToCCByte(pid, value);
                applyIncomingCC(param.cc, (int)cc);
                break;
            }
        }

        _suppressEcho.store(false);
        pendingSyncLayer.store(0xFF);   // sync complete
        return;
    }
}

// =============================================================================
// parameterChanged — APVTS change -> SysEx SET_PARAM
//
// Phase 6 dispatch:
//
//   1. The routing-mirror params (perf_midi_ch_a/b, perf_edit_target) update
//      atomics regardless of source. NO SysEx emitted for these — they're
//      plugin-local UI state, and the firmware learns the layer assignment
//      via the perf CCs (140-146) which travel as regular SET_PARAMs to
//      kLayerPerf.
//
//   2. Echo guard: if the change came from an incoming CC, don't round-trip.
//
//   3. Look up 14-bit ParamID from the CC number.
//      - kLayerPerf for perf CCs (140-146)
//      - kLayerGlobalFx for reverb CCs (70/71/75/93..98)
//      - editTarget-mapped layer for everything else
//
//   4. Convert APVTS value (CC byte form) to native float for hi-res params,
//      or pass the CC byte as a float for non-hi-res (firmware quantises).
//
//   5. Send the 15-byte SET_PARAM SysEx.
// =============================================================================
void JT8000ControllerProcessor::parameterChanged(const juce::String& id, float newValue)
{
    const CCParam* p = findParamByID(id);
    if (!p) { DBG("[ParamChg] " << id << " — no CCParam row, dropped"); return; }

    // ---- Routing-mirror updates (atomics) ----------------------------------
    if (id == "perf_midi_ch_a")
    {
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

        // When switching layers, ask the firmware for the new layer's
        // cached state so the UI reflects the correct values.
        if (!_suppressEcho.load())
            requestLayerSync();
    }

    // ---- Echo guard --------------------------------------------------------
    if (_suppressEcho.load())
    {
        DBG("[ParamChg] " << id << " — echo-suppressed");
        return;
    }

    // ---- Resolve 14-bit ParamID and scope ----------------------------------
    const uint16_t pid = ParamMapJuce::paramId14ForCC(p->cc);
    if (pid == 0)
    {
        DBG("[ParamChg] " << id << " (CC " << p->cc << ") — no ParamID mapping, dropped");
        return;
    }

    int ccVal;
    if (isChoiceParam(*p))
        ccVal = choiceIndexToCC(juce::roundToInt(newValue), p->enumCount, p->ccLUT);
    else
        ccVal = juce::roundToInt(newValue);
    ccVal = juce::jlimit(0, 127, ccVal);

    // ---- Layer routing -----------------------------------------------------
    uint8_t layer;
    const int scope = ParamMapJuce::scopeForCC(p->cc);
    if (scope == 1)         layer = SyxProto::kLayerPerf;
    else if (scope == 2)    layer = SyxProto::kLayerGlobalFx;
    else
    {
        // Patch scope: route per editTarget.
        switch (editTarget.load())
        {
            case EditTarget::LayerA: layer = SyxProto::kLayerA;    break;
            case EditTarget::LayerB: layer = SyxProto::kLayerB;    break;
            case EditTarget::Both:   layer = SyxProto::kLayerBoth; break;
            default:                 layer = SyxProto::kLayerA;    break;
        }
    }

    // ---- Native float conversion + send ------------------------------------
    const float nativeValue = ParamMapJuce::ccByteToNativeFloat(pid, (uint8_t)ccVal);

    // File log — first 5 param changes only to avoid flooding the log file.
    // Enough to confirm the callback fires and reaches the send call.
    static int logCount = 0;
    if (logCount < 5)
    {
        JT8000Log::write("[ParamChg] " + id + " CC=" + juce::String(p->cc)
            + " pid=0x" + juce::String::toHexString((int)pid)
            + " layer=0x" + juce::String::toHexString((int)layer)
            + " ccVal=" + juce::String(ccVal)
            + " native=" + juce::String(nativeValue));
        ++logCount;
        if (logCount == 5)
            JT8000Log::write("[ParamChg] (further entries suppressed)");
    }

    DBG("[ParamChg] " << id << " CC=" << p->cc << " pid=0x"
        << juce::String::toHexString((int)pid) << " layer=0x"
        << juce::String::toHexString((int)layer) << " ccVal=" << ccVal
        << " native=" << nativeValue);

    sendSysExSetParam(layer, pid, nativeValue);
}

// =============================================================================
// sendSysExSetParam — build a 15-byte SET_PARAM and queue it
// =============================================================================
void JT8000ControllerProcessor::sendSysExSetParam(uint8_t layer,
                                                   uint16_t paramId14,
                                                   float nativeValue)
{
    uint8_t buf[SyxProto::kMsgSetParamLen];
    buf[0]                          = 0xF0;
    buf[SyxProto::kOffsetMfrId]     = SyxProto::kMfrId;
    buf[SyxProto::kOffsetSubIdJ]    = SyxProto::kSubIdJ;
    buf[SyxProto::kOffsetSubIdT]    = SyxProto::kSubIdT;
    buf[SyxProto::kOffsetDeviceId]  = SyxProto::kDeviceDefault;
    buf[SyxProto::kOffsetMsgType]   = SyxProto::kMsgSetParam;
    buf[SyxProto::kOffsetLayer]     = layer;
    buf[SyxProto::kOffsetPidHi]     = SyxProto::paramIdHi(paramId14);
    buf[SyxProto::kOffsetPidLo]     = SyxProto::paramIdLo(paramId14);
    SyxProto::encodeFloat(nativeValue, &buf[SyxProto::kOffsetFloat]);
    buf[SyxProto::kMsgSetParamLen - 1] = 0xF7;

    queueSysEx(buf, (int)SyxProto::kMsgSetParamLen);
}

// =============================================================================
// sendStepSeqEdit — atomic step sequencer write (two SET_PARAMs back-to-back)
//
// The firmware applies them in arrival order. Step Select first, then Step
// Value — same semantics the legacy CC pair (CC 17 then CC 18) had.
// =============================================================================
void JT8000ControllerProcessor::sendStepSeqEdit(int step, int value)
{
    // Both messages go to the patch-scope edit target since the step sequencer
    // is per-engine.
    uint8_t layer;
    switch (editTarget.load())
    {
        case EditTarget::LayerA: layer = SyxProto::kLayerA;    break;
        case EditTarget::LayerB: layer = SyxProto::kLayerB;    break;
        case EditTarget::Both:   layer = SyxProto::kLayerBoth; break;
        default:                 layer = SyxProto::kLayerA;    break;
    }

    // Step Select (0x0E10) — value is the step index (0..15) as a float.
    sendSysExSetParam(layer, 0x0E10, (float)juce::jlimit(0, 15, step));

    // Step Value (0x0E11) — value is 0..127 (the actual step value).
    sendSysExSetParam(layer, 0x0E11, (float)juce::jlimit(0, 127, value));
}

// =============================================================================
// queueSysEx — push a fully-formed SysEx message (F0..F7) onto the FIFO and
// emit it directly to the MIDI output device (if one is open).
// =============================================================================
void JT8000ControllerProcessor::queueSysEx(const uint8_t* data, int len)
{
    if (len <= 0 || len > kSysExSlotBytes)
    {
        DBG("[queueSysEx] bad len " << len);
        return;
    }

    // Direct-out path: bypasses host MIDI bus. Essential in VST3 where
    // hosts silently drop SysEx from processBlock output.
    {
        const juce::ScopedLock sl(midiOutLock);

        // File log — first 5 calls only to avoid flooding.
        static int logCount = 0;
        if (logCount < 5)
        {
            JT8000Log::write("[queueSysEx] len=" + juce::String(len)
                + " directMidiOut=" + juce::String(directMidiOut != nullptr ? "YES" : "NO"));
            ++logCount;
            if (logCount == 5)
                JT8000Log::write("[queueSysEx] (further entries suppressed)");
        }

        if (directMidiOut != nullptr)
        {
            if (len >= 2 && data[0] == 0xF0 && data[len - 1] == 0xF7)
            {
                auto msg = juce::MidiMessage::createSysExMessage(data + 1, len - 2);
                directMidiOut->sendMessageNow(msg);
                DBG("[queueSysEx] sent direct " << len << " bytes");
            }
        }
        else
        {
            DBG("[queueSysEx] no directMidiOut — relying on FIFO drain");
        }
    }

    int s1, sz1, s2, sz2;
    fifo.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)
    {
        auto& slot = messageBuffer[s1];
        std::memcpy(slot.data, data, (size_t)len);
        slot.numBytes = (uint8_t)len;
        DBG("[queueSysEx] queued slot=" << s1 << " len=" << len);
    }
    else
    {
        DBG("[queueSysEx] FIFO full!");
    }
    fifo.finishedWrite(sz1 + sz2);
}

// =============================================================================
// sendBankDump — build and queue one BANK_DUMP containing every APVTS-bound
// param's current value. Replaces the 90-CC paced spray for preset loads.
//
// The body format mirrors firmware _handleBankDumpRequest: one entry per
// (layer, paramID, native float). Patch-scope params are routed based on
// the current editTarget:
//   LayerA → kLayerA entries only
//   LayerB → kLayerB entries only
//   Both   → kLayerBoth entries (firmware applies to both engines)
//
// Callers (loadPatchToLayer, loadPerformance) set editTarget BEFORE calling
// this so each pass targets the correct engine without overwriting the other.
//
// For perf and global-fx scope, one entry each with the appropriate layer byte.
//
// The whole message goes into bigOutBuf and is flushed by the next
// processBlock. We don't put it in the per-slot FIFO because BANK_DUMP can
// run ~1000 bytes — far beyond kSysExSlotBytes.
// =============================================================================
void JT8000ControllerProcessor::sendBankDump()
{
    juce::Array<uint8_t> buf;
    buf.ensureStorageAllocated((int)SyxProto::kBankDumpMaxLen);

    auto put = [&](uint8_t b) { buf.add(b); };

    // Header
    put(0xF0);
    put(SyxProto::kMfrId);
    put(SyxProto::kSubIdJ);
    put(SyxProto::kSubIdT);
    put(SyxProto::kDeviceDefault);
    put(SyxProto::kMsgBankDump);
    // Reserve count placeholder — patched at the end.
    const int countOffset = buf.size();
    put(0); put(0);

    // Helper: append one entry (layer + pid + float).
    uint16_t entryCount = 0;
    auto appendEntry = [&](uint8_t layer, uint16_t pid, float value)
    {
        put(layer);
        put(SyxProto::paramIdHi(pid));
        put(SyxProto::paramIdLo(pid));
        uint8_t f5[SyxProto::kFloatBytes];
        SyxProto::encodeFloat(value, f5);
        for (int i = 0; i < (int)SyxProto::kFloatBytes; ++i) put(f5[i]);
        ++entryCount;
    };

    // Walk every APVTS-bound param. For each:
    //   - Look up 14-bit ParamID
    //   - Read APVTS value (CC byte / choice index form)
    //   - Convert to native float
    //   - Emit entry(ies) based on scope
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];
        const uint16_t pid = ParamMapJuce::paramId14ForCC(p.cc);
        if (pid == 0) continue;

        // Read APVTS value -> CC byte form.
        auto* param = apvts.getParameter(p.id);
        if (!param) continue;
        const float zeroToOne = param->getValue();
        const float realValue = param->convertFrom0to1(zeroToOne);

        int ccVal;
        if (isChoiceParam(p))
            ccVal = choiceIndexToCC(juce::roundToInt(realValue), p.enumCount, p.ccLUT);
        else
            ccVal = juce::roundToInt(realValue);
        ccVal = juce::jlimit(0, 127, ccVal);

        const float nativeValue = ParamMapJuce::ccByteToNativeFloat(pid, (uint8_t)ccVal);

        const int scope = ParamMapJuce::scopeForCC(p.cc);
        if (scope == 1)         appendEntry(SyxProto::kLayerPerf,     pid, nativeValue);
        else if (scope == 2)    appendEntry(SyxProto::kLayerGlobalFx, pid, nativeValue);
        else
        {
            // Patch scope — route based on current editTarget.
            //
            // The APVTS holds ONE layer's worth of patch values. The caller
            // sets editTarget before calling sendBankDump() to control which
            // firmware engine receives these values:
            //   LayerA → emit kLayerA only
            //   LayerB → emit kLayerB only
            //   Both   → emit kLayerBoth (firmware applies to both engines)
            switch (editTarget.load())
            {
                case EditTarget::LayerA:
                    appendEntry(SyxProto::kLayerA, pid, nativeValue);
                    break;
                case EditTarget::LayerB:
                    appendEntry(SyxProto::kLayerB, pid, nativeValue);
                    break;
                case EditTarget::Both:
                default:
                    appendEntry(SyxProto::kLayerBoth, pid, nativeValue);
                    break;
            }
        }
    }

    // Patch the count.
    buf.set(countOffset,     (uint8_t)((entryCount >> 7) & 0x7F));
    buf.set(countOffset + 1, (uint8_t)( entryCount       & 0x7F));

    // Trailer.
    put(0xF7);

    // Standalone direct-out path
    {
        const juce::ScopedLock sl(midiOutLock);
        if (directMidiOut != nullptr)
        {
            // SysEx constructor expects body without F0/F7.
            auto msg = juce::MidiMessage::createSysExMessage(
                buf.getRawDataPointer() + 1, buf.size() - 2);
            directMidiOut->sendMessageNow(msg);
        }
    }

    // VST host path: hand off via bigOutBuf for the audio thread to flush.
    {
        const juce::ScopedLock sl(bigOutLock);
        bigOutBuf = buf;
        bigOutReady.store(true);
    }
}

// =============================================================================
// sendBankDumpRequest — ask the firmware for its current per-layer state
//
// Sends a 7-byte BANK_DUMP_REQUEST. The firmware replies with a BANK_DUMP
// containing one entry per cached param, with explicit layer bytes (kLayerA,
// kLayerB, kLayerPerf, kLayerGlobalFx). The incoming BANK_DUMP handler
// filters entries for the pendingSyncLayer and updates APVTS accordingly.
// =============================================================================
void JT8000ControllerProcessor::sendBankDumpRequest()
{
    uint8_t buf[SyxProto::kMsgBankDumpReqLen];
    buf[0]                          = 0xF0;
    buf[SyxProto::kOffsetMfrId]     = SyxProto::kMfrId;
    buf[SyxProto::kOffsetSubIdJ]    = SyxProto::kSubIdJ;
    buf[SyxProto::kOffsetSubIdT]    = SyxProto::kSubIdT;
    buf[SyxProto::kOffsetDeviceId]  = SyxProto::kDeviceDefault;
    buf[SyxProto::kOffsetMsgType]   = SyxProto::kMsgBankDumpRequest;
    buf[SyxProto::kMsgBankDumpReqLen - 1] = 0xF7;

    queueSysEx(buf, (int)SyxProto::kMsgBankDumpReqLen);
}

// =============================================================================
// requestLayerSync — trigger a state refresh from the firmware for the
// currently selected edit target layer.
//
// Called when the user switches editTarget (A/B/Both). Records which layer
// to filter for in the incoming BANK_DUMP, then sends the request. The
// reply arrives asynchronously via handleIncomingSysEx.
// =============================================================================
void JT8000ControllerProcessor::requestLayerSync()
{
    switch (editTarget.load())
    {
        case EditTarget::LayerA: pendingSyncLayer.store(SyxProto::kLayerA); break;
        case EditTarget::LayerB: pendingSyncLayer.store(SyxProto::kLayerB); break;
        default:                 pendingSyncLayer.store(SyxProto::kLayerA); break;
    }
    sendBankDumpRequest();
}

// =============================================================================
// Note / Pitch helpers — unchanged. These ride on the per-slot FIFO too, but
// MidiMessage's raw-bytes constructor handles 3-byte messages fine in slots
// sized for 16-byte SysEx. The unused trailing bytes in the slot don't matter
// because slot.numBytes records the actual length.
// =============================================================================
void JT8000ControllerProcessor::sendNoteOn(int channel, int note, float velocity)
{
    auto m = juce::MidiMessage::noteOn(channel, note, velocity);
    queueSysEx(m.getRawData(), juce::jmin(m.getRawDataSize(), (int)kSysExSlotBytes));
}

void JT8000ControllerProcessor::sendNoteOff(int channel, int note)
{
    auto m = juce::MidiMessage::noteOff(channel, note, (uint8_t)0);
    queueSysEx(m.getRawData(), juce::jmin(m.getRawDataSize(), (int)kSysExSlotBytes));
}

void JT8000ControllerProcessor::sendPitchWheel(int channel, int value)
{
    auto m = juce::MidiMessage::pitchWheel(channel, value);
    queueSysEx(m.getRawData(), juce::jmin(m.getRawDataSize(), (int)kSysExSlotBytes));
}

// =============================================================================
// MIDI output device — used in both standalone and VST3 modes.
// In VST3, direct output bypasses the host's MIDI bus (which drops SysEx).
// =============================================================================
void JT8000ControllerProcessor::setMidiOutput(std::unique_ptr<juce::MidiOutput> output)
{
    const juce::ScopedLock sl(midiOutLock);
    if (output != nullptr)
        JT8000Log::write("[Processor] directMidiOut set: " + output->getName());
    else
        JT8000Log::write("[Processor] directMidiOut cleared");
    directMidiOut = std::move(output);
}

// =============================================================================
// Patch name — unchanged
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
// MIDI input device (standalone) — unchanged
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
// Same semantics as previous phases: incoming CCs reflect into APVTS;
// SysEx PARAM_VALUE replies reflect via handleIncomingSysEx. Notes/pitch
// bend etc. don't apply to the controller plugin's standalone-input path
// (we only track CCs for state sync).
// =============================================================================
void JT8000ControllerProcessor::handleIncomingMidiMessage(juce::MidiInput*,
                                                          const juce::MidiMessage& msg)
{
    if (msg.isController())
    {
        applyIncomingCC(msg.getControllerNumber(), msg.getControllerValue());
    }
    else if (msg.isSysEx())
    {
        const int sxLen = msg.getSysExDataSize();
        const auto* sxData = msg.getSysExData();
        juce::Array<uint8_t> full;
        full.resize(sxLen + 2);
        full.set(0, 0xF0);
        for (int i = 0; i < sxLen; ++i) full.set(i + 1, sxData[i]);
        full.set(sxLen + 1, 0xF7);
        handleIncomingSysEx(full.getRawDataPointer(), full.size());
    }
}

// =============================================================================
// State save / restore — APVTS handles it; just preserve the patch-name prop.
// =============================================================================
void JT8000ControllerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.state.createXml())
        copyXmlToBinary(*xml, dest);
}

void JT8000ControllerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// =============================================================================
// Required factory function — JUCE entry point
// =============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JT8000ControllerProcessor();
}
