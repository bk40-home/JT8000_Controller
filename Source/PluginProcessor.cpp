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
#include <vector>   // mono-fallback scratch (Phase J2)
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

#if JT8000_HAS_ENGINE
    // Eager construction (~700 KB heap + table setup) so the unique_ptr never
    // changes for the life of the processor — the audio thread can read it
    // without any locking or swap protocol.  Lazy creation would save memory
    // nobody misses and buy a lifecycle race nobody wants.
    _engine = std::make_unique<EngineHost>();
#endif
}

#if JT8000_HAS_ENGINE
// =============================================================================
// prepareToPlay / setEngineMode (Phase J2)
// =============================================================================
void JT8000ControllerProcessor::prepareToPlay(double sampleRate, int)
{
    _hostSampleRate = sampleRate;
    // The collector must know the rate before addMessageToQueue is legal;
    // reset also discards stale cross-thread MIDI from before the restart.
    _engineInbox.reset(sampleRate);
    _inboxReady.store(true);
}

void JT8000ControllerProcessor::setEngineMode(int m)
{
    _engineMode.store(juce::jlimit((int)kModeController, (int)kModeBoth, m));
    // Note-offs on a mode EXIT are handled on the audio thread via the
    // _engineModePrev edge in processBlock — calling the engine from here
    // (message thread) would break the single-caller contract.
}
#else
void JT8000ControllerProcessor::prepareToPlay(double, int) {}
#endif

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
        else if (isIntLatticeParam(p))
        {
            // Canonical `type: int` (Phase J1 / D-J6): the parameter holds
            // the REAL value (seq_steps 1..16), so the host lane, popup and
            // double-click all read in steps — the 0..127 byte is wire-only
            // (ccByteFromPlain / plainFromCCByte).
            params.push_back(std::make_unique<juce::AudioParameterInt>(
                juce::ParameterID { p.id, 1 }, p.name, p.intMin, p.intMax, p.defVal));
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
            // Phase E: NRPN from the firmware decodes to (v1cc, v7) and
            // reflects into APVTS exactly like a plain CC used to.  The
            // cluster numbers (6/38/96-101) COLLIDE with real v1 params
            // (CC 98 = reverb hipass...) so they are consumed either way
            // and must never reach applyIncomingCC raw.
            const int num = msg.getControllerNumber();
            const int val = msg.getControllerValue();
            int v1cc, v7;
            if (nrpnDecodeHost_.feed(num, val, &v1cc, &v7))
                applyIncomingCC(v1cc, v7);
            else if (! NrpnLink::isClusterCC(num))
                applyIncomingCC(num, val);   // plain hardware CC, as before

            // Status feed 0x3FFF (Phase J1d): intercepted inside the
            // decoder BEFORE param routing; here we just latch the word
            // for the editor's poll.  Per-message, not per-block, so the
            // fine (CC38) refinement overwrites the coarse half within
            // the same cluster — the editor only ever sees whole words.
            uint16_t statusWord;
            if (nrpnDecodeHost_.takeStatus(&statusWord))
            {
                // Phase J2: when the embedded engine is audible, ITS status
                // drives the dots/playhead (stored below) — hardware words
                // are still taken (clears the decoder flag) but not latched,
                // so the two sources never fight over the display.
                if (! engineAudible())
                {
                    _statusWord.store(statusWord);
                    _statusCount.fetch_add(1, std::memory_order_relaxed);
                }
            }

#if JT8000_HAS_ENGINE
            // Phase J2: the embedded engine hears the same inbound stream —
            // NRPN clusters (its transport decodes them itself), curated and
            // performance CCs alike.  Teensy-originated broadcasts included:
            // that is what keeps engine and hardware coherent in Both mode.
            if (engineAudible())
                _engine->controlChange((uint8_t)num, (uint8_t)val);
#endif
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
#if JT8000_HAS_ENGINE
            if (engineAudible())
            {
                // Notes / wheel from the host bus drive the engine.  JUCE's
                // getPitchWheelValue() is already RAW 0..16383 — no centring
                // offset (that trap belongs to the Teensy library only).
                if      (msg.isNoteOn())     _engine->noteOn ((uint8_t)msg.getNoteNumber(),
                                                              (uint8_t)msg.getVelocity());
                else if (msg.isNoteOff())    _engine->noteOff((uint8_t)msg.getNoteNumber());
                else if (msg.isPitchWheel()) _engine->pitchBendRaw14((uint16_t)msg.getPitchWheelValue());
            }
            // Sound mode: the hardware wire is silent — don't pass notes on.
            if (_engineMode.load() == kModeSound)
                continue;
#endif
            // VST3 fix: in a host (Ableton) the plugin's OUTPUT MIDI bus is
            // dropped/reordered just like SysEx and NRPN — so host notes that
            // reach us here never make it to the Teensy via `filtered`.  When a
            // direct device is open, send channel-voice messages straight to it
            // (the same reliable path the param NRPN already uses), and DON'T
            // also queue to the host bus, or a standalone-style setup with both
            // active would double-trigger.  With no direct device open (pure
            // standalone-via-host), fall through to `filtered` exactly as before
            // — behaviour unchanged.
            {
                bool sentDirect = false;
                {
                    const juce::ScopedLock sl(midiOutLock);
                    if (directMidiOut != nullptr)
                    {
                        directMidiOut->sendMessageNow(msg);
                        sentDirect = true;
                    }
                }
                if (sentDirect)
                    continue;
            }

            filtered.addEvent(msg, metadata.samplePosition);
        }
    }

#if JT8000_HAS_ENGINE
    // =========================================================================
    // Embedded engine service (Phase J2) — audio thread, the engine's ONLY
    // caller.  Order: mode edge -> cross-thread inbox -> render -> status.
    // =========================================================================
    {
        const int mode = _engineMode.load();
        if (mode != _engineModePrev)
        {
            // Leaving an audible mode: silence the engine so no voice hangs
            // sustaining into Controller mode where nothing services it.
            if (_engineModePrev != kModeController && mode == kModeController)
                _engine->allNotesOff();
            _engineModePrev = mode;
        }

        if (mode != kModeController)
        {
            // Cross-thread MIDI (direct MIDI-in devices, and the outgoing
            // param tee from queueShortMidi) — collector-buffered, drained
            // here so the single-caller contract holds.
            juce::MidiBuffer inbox;
            _engineInbox.removeNextBlockOfMessages(inbox, buffer.getNumSamples());
            for (const auto meta : inbox)
            {
                const auto m = meta.getMessage();
                if      (m.isController()) _engine->controlChange((uint8_t)m.getControllerNumber(),
                                                                  (uint8_t)m.getControllerValue());
                else if (m.isNoteOn())     _engine->noteOn ((uint8_t)m.getNoteNumber(),
                                                            (uint8_t)m.getVelocity());
                else if (m.isNoteOff())    _engine->noteOff((uint8_t)m.getNoteNumber());
                else if (m.isPitchWheel()) _engine->pitchBendRaw14((uint16_t)m.getPitchWheelValue());
            }

            // Render.  The engine runs at its fixed 44.1 k; rate mismatch is
            // a UI warning (Q4 sign-off), not a hidden resample.
            const size_t n = (size_t)buffer.getNumSamples();
            if (buffer.getNumChannels() >= 2)
            {
                _engine->render(buffer.getWritePointer(0), buffer.getWritePointer(1), n);
            }
            else if (buffer.getNumChannels() == 1)
            {
                // Defensive mono host: render stereo, keep left.
                static thread_local std::vector<float> scratch;
                scratch.resize(n);
                _engine->render(buffer.getWritePointer(0), scratch.data(), n);
            }

            // Engine telemetry replaces the hardware status feed while
            // audible — same word format, same editor consumers (J1d).
            _statusWord.store(_engine->statusWord());
            _statusCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
#endif

    // =========================================================================
    // Host MIDI-bus drain (FALLBACK transport only).
    //
    // When a direct MIDI output device is open, queueSysEx/sendBankDump send
    // immediately and never populate the FIFO or bigOutBuf, so both drains
    // below are no-ops. They carry SysEx only when no direct device is
    // selected (and for hosts that DO accept SysEx on the bus).
    //
    // Per-message timestamp: every emitted message gets a distinct, increasing
    // sample position (clamped to the block) rather than one shared value.
    // Some hosts coalesce or drop multiple SysEx sharing a timestamp; a BANK
    // burst of SET_PARAMs is exactly that case. numSamples-1 is the last valid
    // slot; we never exceed it.
    // =========================================================================
    const int lastSample = juce::jmax(0, buffer.getNumSamples() - 1);
    int       nextPos     = filtered.isEmpty() ? 0
                                               : juce::jmin(lastSample,
                                                            filtered.getLastEventTime());

    auto emitSysEx = [&](const uint8_t* d, int n)
    {
        if (n >= 2 && d[0] == 0xF0 && d[n - 1] == 0xF7)
        {
            // createSysExMessage takes the BODY (between F0 and F7).
            filtered.addEvent(juce::MidiMessage::createSysExMessage(d + 1, n - 2),
                              nextPos);
        }
        else
        {
            // Short raw message (e.g. note / pitch-bend forwarded via FIFO).
            filtered.addEvent(juce::MidiMessage(d, n), nextPos);
        }
        if (nextPos < lastSample) ++nextPos;   // advance, but stay in-block
    };

    // --- Drain per-param SysEx FIFO ----------------------------------------
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        int s1, sz1, s2, sz2;
        fifo.prepareToRead(ready, s1, sz1, s2, sz2);

        for (int i = s1; i < s1 + sz1; ++i)
            emitSysEx(messageBuffer[i].data, (int)messageBuffer[i].numBytes);
        for (int i = s2; i < s2 + sz2; ++i)
            emitSysEx(messageBuffer[i].data, (int)messageBuffer[i].numBytes);

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
            emitSysEx(snapshot.getRawDataPointer(), snapshot.size());
    }

    midiMessages.swapWith(filtered);
}

// =============================================================================
// applyIncomingCC — same as previous phases. Hardware CC reflects into APVTS;
// _suppressEcho prevents the parameterChanged handler from firing a SysEx.
// =============================================================================
void JT8000ControllerProcessor::applyIncomingCC(int cc, int value)
{
    // D-J7 (Phase J1): the old `cc > 127` guard predates the NRPN wire and
    // silently discarded every inbound key above the plain-CC range — so
    // firmware broadcasts and full resyncs never reflected the perf params
    // (140..146), the curve params (147..155), or master volume (pseudo
    // 200) into the UI.  findParamByCC() is the validity gate; unknown keys
    // fall out one line later exactly as before.
    if (cc < 0) return;

    const CCParam* p = findParamByCC(cc);
    if (p == nullptr) return;

    if (p->style == ControlStyle::StepGrid) return;

    auto* param = apvts.getParameter(p->id);
    if (param == nullptr) return;

    // One value law for every shape (choice / int-lattice / raw) —
    // plainFromCCByte is the same code the host gate asserts (Phase J1).
    const float normVal = param->convertTo0to1(plainFromCCByte(*p, value));

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

    // ---- Encode APVTS value to the 7-bit wire form (Phase J1) -------------
    // ccByteFromPlain is the single home of the value law — choice (LUT /
    // bucket midpoint / toggle 0-127), int-lattice (seq_steps), and raw all
    // encode here, and the identical code is asserted by the host gate.
    const int ccVal = juce::jlimit(0, 127, ccByteFromPlain(*p, newValue));

    // Layer routing is GONE (flagged deviation): the v2 engine is
    // single-layer (DEFERRALS_LEDGER "per-layer collapse"), so the layer
    // byte has no receiver.  editTarget still exists in the UI; outbound
    // ignores it.  isLocalOnly no longer gates the send either — the bridge
    // carries the old 128+ ExtCC range (perf 140-146, curves 147-155) as
    // ordinary NRPN, which is precisely what dissolves ledger item 5.
    DBG("[ParamChg] " << id << " v1cc=" << p->cc << " ccVal=" << ccVal
        << " -> NRPN");
    sendParamNrpn(p->cc, ccVal);
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
// sendStepSeqEdit — atomic step sequencer write (two NRPNs back-to-back)
//
// The firmware applies them in arrival order: Step Select then Step Value —
// the same shared-address protocol the v1 CC pair (17 then 18) used, and
// what the v2 sequencer's _seqEditStep expects (DEFERRALS_LEDGER D-5).
// Layer selection removed with the rest of the layer routing (v2 is
// single-engine).
// =============================================================================
void JT8000ControllerProcessor::sendStepSeqEdit(int step, int value)
{
    // D-J1 (Phase J1): seq.step_select is a canonical `type: int` param —
    // the firmware decodes lroundf(norm * 15) (SynthCore.cpp), NOT a raw
    // byte.  Sending the bare index as v7 put norm at step/127, collapsing
    // all 16 bars onto engine steps 0..2 (every grid edit past bar 1
    // corrupted the wrong step).  The lattice encode makes the round-trip
    // exact for all 16 steps — asserted in the host gate.
    const int sel7 = ValueLaw::ccFromIntLattice(juce::jlimit(0, 15, step), 0, 15);
    sendParamNrpn(17, sel7);                         // seq.step_select (v1 CC 17)
    sendParamNrpn(18, juce::jlimit(0, 127, value));  // seq.step_value  (v1 CC 18)
}

// Aux-lane step write (Stage B/C/D).  Same select-then-value protocol as the
// gate lane, but the aux params have no v1 CC, so they're addressed by ParamID
// directly (Option B).  The select uses the identical 1..16 int lattice encode
// so the firmware's lroundf(norm*15) round-trips exactly for all 16 steps.
void JT8000ControllerProcessor::sendStepSeqAuxEdit(int step, int value)
{
    const int sel7 = ValueLaw::ccFromIntLattice(juce::jlimit(0, 15, step), 0, 15);
    sendParamNrpnById(NrpnLink::AuxParamId::kStepSelect, sel7);
    sendParamNrpnById(NrpnLink::AuxParamId::kStepValue, juce::jlimit(0, 127, value));
}

// =============================================================================
// Phase E: NRPN wire helpers.  queueShortMidi mirrors queueSysEx's transport
// selection exactly (direct device is exclusive and primary; host-bus FIFO
// is the fallback) so NRPN inherits the double-send protection unchanged.
// =============================================================================
void JT8000ControllerProcessor::queueShortMidi(const NrpnLink::Short3* msgs,
                                                size_t n)
{
#if JT8000_HAS_ENGINE
    // Phase J2 tee: every locally-generated NRPN cluster (knob moves, step
    // grid, Send All) also reaches the embedded engine — the SAME bytes the
    // Teensy would receive, which is the whole point of Sound mode being a
    // faithful audition.  Collector-buffered: this runs on whatever thread
    // the APVTS callback used.
    if (engineAudible() && _inboxReady.load())
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        for (size_t k = 0; k < n; ++k)
            _engineInbox.addMessageToQueue(juce::MidiMessage(msgs[k].b[0],
                                                             msgs[k].b[1],
                                                             msgs[k].b[2], now));
    }
    if (_engineMode.load() == kModeSound)
        return;   // hardware wire stays silent in Sound mode
#endif
    {
        const juce::ScopedLock sl(midiOutLock);
        if (directMidiOut != nullptr)
        {
            for (size_t k = 0; k < n; ++k)
                directMidiOut->sendMessageNow(juce::MidiMessage(msgs[k].b, 3));
            return;   // exclusive — never also queue to the bus
        }
    }
    for (size_t k = 0; k < n; ++k)
    {
        int s1, sz1, s2, sz2;
        fifo.prepareToWrite(1, s1, sz1, s2, sz2);
        if (sz1 <= 0) { DBG("[queueShortMidi] FIFO full — dropped"); return; }
        auto& slot = messageBuffer[s1];
        slot.data[0] = msgs[k].b[0];
        slot.data[1] = msgs[k].b[1];
        slot.data[2] = msgs[k].b[2];
        slot.numBytes = 3;
        fifo.finishedWrite(1);
    }
}

void JT8000ControllerProcessor::sendParamNrpn(int v1cc, int ccVal)
{
    NrpnLink::Short3 m[6];
    const size_t n = NrpnLink::encodeParam((uint16_t)v1cc,
                                           (uint8_t)juce::jlimit(0, 127, ccVal),
                                           1 /*channel*/, m);
    if (n == 0)
    {
        DBG("[sendParamNrpn] v1cc " << v1cc << " unmapped — dropped");
        return;   // stale control or v2-only slot: never guess a ParamID
    }
    queueShortMidi(m, n);
}

// Option B: address a v2-only param (seq aux lane) by ParamID directly, for
// params that have no v1 CC and thus no bridge entry.  Same wire + transport
// as sendParamNrpn; only the addressing differs.  value7 is the raw 7-bit
// value the firmware decodes (norm = value7 * 129 / 16383).
void JT8000ControllerProcessor::sendParamNrpnById(uint16_t paramId, int ccVal)
{
    NrpnLink::Short3 m[6];
    const size_t n = NrpnLink::encodeParamById(paramId,
                                               (uint8_t)juce::jlimit(0, 127, ccVal),
                                               1 /*channel*/, m);
    queueShortMidi(m, n);
}

void JT8000ControllerProcessor::requestResync()
{
    NrpnLink::Short3 m[5];
    queueShortMidi(m, NrpnLink::encodeResyncRequest(1, m));
}

// =============================================================================
// queueSysEx — emit one fully-formed SysEx message (F0..F7).
//
// TRANSPORT SELECTION (mutually exclusive):
//   - If a direct MIDI output device is open (any wrapper type), send it
//     immediately and RETURN. The per-param FIFO is left untouched, so
//     processBlock does no SysEx work — the host MIDI bus carries nothing.
//   - If no direct device is open, fall back to the host MIDI bus: push the
//     message onto the FIFO for processBlock to drain into midiMessages.
//
// This prevents the double-send that would otherwise occur in a VST3 where
// both a direct device AND host-bus routing are active. directMidiOut is the
// primary transport because stock JUCE drops outgoing SysEx on the VST3 bus.
// =============================================================================
void JT8000ControllerProcessor::queueSysEx(const uint8_t* data, int len)
{
#if JT8000_HAS_ENGINE
    // Sound mode: SysEx is hardware-only traffic (patch ops, SET_PARAM) the
    // embedded engine does not parse — suppress it with the rest of the wire.
    if (_engineMode.load() == kModeSound) return;
#endif

    if (len <= 0 || len > kSysExSlotBytes)
    {
        DBG("[queueSysEx] bad len " << len);
        return;
    }

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
            return;   // direct device is exclusive — do NOT also queue to the bus
        }
    }

    // Fallback: no direct device — hand to the host MIDI bus via the FIFO.
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
    // Phase E: "send patch to synth" is now simply every APVTS-bound param
    // as NRPN — the same single code path as a knob turn.  The v1 BANK_DUMP
    // blob (layer bytes, 5-byte floats, count patching) has no v2 receiver;
    // the firmware assembles state param-by-param, then its own broadcast
    // mirrors the result to every other editor (live view for free).
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kAllParams[i];
        if (p.style == ControlStyle::StepGrid) continue;   // grid has its own path

        auto* param = apvts.getParameter(p.id);
        if (param == nullptr) continue;

        // Same single value law as parameterChanged — ccByteFromPlain covers
        // choice, int-lattice, and raw rows (Phase J1).
        const float plain = param->convertFrom0to1(param->getValue());
        sendParamNrpn(p.cc, juce::jlimit(0, 127, ccByteFromPlain(p, plain)));
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
    // Phase E: the pull is the reserved resync NRPN (0x3F00) — the firmware
    // replies with EVERY parameter, paced, to every port.  Name kept so the
    // editor's existing call sites stay valid.
    requestResync();
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
    // Phase E: the v2 engine is single-layer — there is no per-layer cache
    // to select.  A target switch now just pulls the (one) current state.
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
    {
        directMidiOutId = output->getIdentifier();   // remember which device is
                                                      // the Teensy, so the input
                                                      // router can mark it
                                                      // listen-only (Auto rule).
        JT8000Log::write("[Processor] directMidiOut set: " + output->getName());
    }
    else
    {
        directMidiOutId.clear();
        JT8000Log::write("[Processor] directMidiOut cleared");
    }
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
// MIDI input devices (standalone, multi-input)
//
// openMidiInputs() opens exactly the requested set and closes everything not
// in it (idempotent — safe to call on every checklist change). The legacy
// single-device methods are thin wrappers so older callers still compile.
// =============================================================================
void JT8000ControllerProcessor::openMidiInputs(const juce::StringArray& ids)
{
    const juce::ScopedLock sl(midiInLock);

    // Close devices that are no longer requested.
    for (int i = midiInputDevices.size(); --i >= 0;)
    {
        if (! ids.contains(midiInputDevices[i]->getIdentifier()))
        {
            midiInputDevices[i]->stop();
            midiInputDevices.remove(i);   // OwnedArray deletes the device
        }
    }

    // Open devices that are newly requested (skip any already open).
    for (const auto& id : ids)
    {
        if (id.isEmpty()) continue;

        bool alreadyOpen = false;
        for (auto* d : midiInputDevices)
            if (d->getIdentifier() == id) { alreadyOpen = true; break; }
        if (alreadyOpen) continue;

        if (auto device = juce::MidiInput::openDevice(id, this))
        {
            device->start();
            midiInputDevices.add(device.release());
        }
    }
}

bool JT8000ControllerProcessor::isMidiInputOpen(const juce::String& id) const
{
    const juce::ScopedLock sl(const_cast<juce::CriticalSection&>(midiInLock));
    for (auto* d : midiInputDevices)
        if (d->getIdentifier() == id) return true;
    return false;
}

// --- Legacy single-device wrappers ------------------------------------------
void JT8000ControllerProcessor::openMidiInput(const juce::String& deviceIdentifier)
{
    openMidiInputs(deviceIdentifier.isEmpty() ? juce::StringArray()
                                              : juce::StringArray(deviceIdentifier));
}

void JT8000ControllerProcessor::closeMidiInput()
{
    openMidiInputs({});   // open the empty set => close everything
}

juce::String JT8000ControllerProcessor::getActiveMidiInputId() const
{
    const juce::ScopedLock sl(const_cast<juce::CriticalSection&>(midiInLock));
    return midiInputDevices.isEmpty() ? juce::String()
                                      : midiInputDevices.getFirst()->getIdentifier();
}

// =============================================================================
// forwardToTeensy — send a short performance message out the direct port.
// No-op if no direct device is open. Locks midiOutLock (same as queueSysEx).
// =============================================================================
void JT8000ControllerProcessor::forwardToTeensy(const juce::MidiMessage& msg)
{
#if JT8000_HAS_ENGINE
    // Sound mode = desktop audition only; the hardware hears nothing (J2).
    if (_engineMode.load() == kModeSound) return;
#endif
    const juce::ScopedLock sl(midiOutLock);
    if (directMidiOut != nullptr)
        directMidiOut->sendMessageNow(msg);
}

// =============================================================================
// handleIncomingMidiMessage — standalone MIDI input router
//
// Dispatch by source identity (the Auto no-feedback rule):
//   - source == the open MIDI-OUT device (the Teensy)  -> LISTEN-ONLY.
//         Its CC/SysEx replies sync the UI; nothing is forwarded, so the
//         Teensy can never echo its own messages back to itself.
//   - any other source (a controller)                  -> FORWARD.
//         Performance MIDI (notes, PB, aftertouch, PC, non-param CCs) is
//         forwarded raw to the Teensy. A CC that maps to a plugin param is
//         instead reflected into the APVTS (UI tracks the knob); the normal
//         SET_PARAM SysEx path then carries the value to the Teensy, and the
//         redundant raw CC is suppressed so the param isn't sent twice.
//
// Reading directMidiOutId here takes midiOutLock briefly; MIDI callbacks are
// not the audio thread, so this is safe.
// =============================================================================
void JT8000ControllerProcessor::handleIncomingMidiMessage(juce::MidiInput* source,
                                                          const juce::MidiMessage& msg)
{
#if JT8000_HAS_ENGINE
    // Phase J2: every performance/param message also feeds the embedded
    // engine via the collector (this runs on the MIDI system thread — the
    // collector is the bridge that keeps the audio thread the engine's only
    // caller).  SysEx is excluded: the engine's transport is CC/NRPN-only,
    // recorded in the ledger.
    if (engineAudible() && _inboxReady.load() && ! msg.isSysEx())
        _engineInbox.addMessageToQueue(
            juce::MidiMessage(msg, juce::Time::getMillisecondCounterHiRes() * 0.001));
#endif

    // Determine role: is this message from the Teensy (listen-only) device?
    bool fromTeensy = false;
    {
        const juce::ScopedLock sl(midiOutLock);
        fromTeensy = (source != nullptr)
                  && directMidiOutId.isNotEmpty()
                  && (source->getIdentifier() == directMidiOutId);
    }

    if (msg.isController())
    {
        const int cc = msg.getControllerNumber();

        // Phase E: NRPN from the Teensy decodes first (independent stream
        // state from the VST3 bus); cluster CCs never reach the param table.
        {
            int v1cc, v7;
            const bool decoded =
                nrpnDecodeDirect_.feed(cc, msg.getControllerValue(), &v1cc, &v7);

            // Status feed 0x3FFF (Phase J1d) — latch before any early-out.
            // The direct stream is per-message, so a coarse-only word can be
            // latched for one poll tick; the CC38 refinement corrects it a
            // message later (worst case one wrong-playhead frame).
            uint16_t statusWord;
            if (nrpnDecodeDirect_.takeStatus(&statusWord))
            {
                if (! engineAudible())   // engine status wins while audible (J2)
                {
                    _statusWord.store(statusWord);
                    _statusCount.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (decoded)
            {
                applyIncomingCC(v1cc, v7);
                return;
            }
            if (NrpnLink::isClusterCC(cc))
                return;   // consumed: selection/deselect bytes
        }

        // Does this CC map to a plugin parameter?
        const bool isParamCC = (findParamByCC(cc) != nullptr);

        if (isParamCC)
        {
            // Reflect into APVTS (UI + SysEx round-trip). This covers both the
            // Teensy listen-only case and the controller case. For a controller
            // we deliberately do NOT also forward the raw CC: the SET_PARAM
            // SysEx fired by the APVTS change is the canonical path, keeping
            // parameter edits on the SysEx wire (not the CC wire we moved off).
            // (Phase J1 note: plain CC 7 lands here as v1 seq.rate — the
            // firmware's curated `cc: 7` = master volume map applies only to
            // controllers wired to the Teensy directly, not through us.
            // Pre-existing behaviour, recorded in the ledger, unchanged.)
            applyIncomingCC(cc, msg.getControllerValue());
        }
        else if (! fromTeensy)
        {
            // Non-param CC (mod=1, sustain=64, expression=11, ...) from a
            // controller: forward raw — these are performance controls the
            // firmware handles natively and have no SysEx equivalent.
            forwardToTeensy(msg);
        }
        // Non-param CC from the Teensy: ignore (listen-only, not a param).
    }
    else if (msg.isSysEx())
    {
        // SysEx from the Teensy = a PARAM_VALUE/state reply -> sync the UI.
        // SysEx from a controller (rare) -> forward raw.
        if (fromTeensy)
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
        else
        {
            forwardToTeensy(msg);
        }
    }
    else if (! fromTeensy)
    {
        // Notes, pitch bend, aftertouch, channel pressure, program change,
        // clock/transport, etc. from a controller -> forward raw to the Teensy.
        // This is what lets standalone play the Teensy without a host.
        forwardToTeensy(msg);
    }
    // Any other message type from the Teensy: ignore (listen-only).
}

// =============================================================================
// State save / restore — APVTS handles it; just preserve the patch-name prop.
// =============================================================================
void JT8000ControllerProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.state.createXml())
    {
#if JT8000_HAS_ENGINE
        // App-level setting, not patch data: rides the state XML root so a
        // reopened session keeps its mode, but Init Patch never touches it.
        xml->setAttribute("engineMode", _engineMode.load());
#endif
        copyXmlToBinary(*xml, dest);
    }
}

void JT8000ControllerProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
#if JT8000_HAS_ENGINE
        setEngineMode(xml->getIntAttribute("engineMode", _engineMode.load()));
#endif
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

// =============================================================================
// Required factory function — JUCE entry point
// =============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JT8000ControllerProcessor();
}
