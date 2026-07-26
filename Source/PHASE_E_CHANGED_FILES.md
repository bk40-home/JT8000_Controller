# Phase E (JUCE NRPN swap) — changed files

Minimal cut, same shape as Phase C: internal model (APVTS, CCDefinitions,
every ccLUT firmware quirk) UNTOUCHED — only the wire changed.

## New files (drop into Source/)
- JtBridgeTable.h   — SAME generated file as the ESP32 (one table, three
                      clients); regenerate via gen_params.py, never hand-edit
- NrpnLink.h        — encode/decode + per-stream Decoder; pure C++, verified
                      here under -Wall -Wextra -Werror (loopback for every
                      mapped param, RPN swallow, v2-only drop counting)

## Replaced (drop-in)
- PluginProcessor.h   — kQueueSize 256->1024 (full-patch burst on the
                        host-bus fallback ~900 short msgs); NRPN members
- PluginProcessor.cpp — parameterChanged -> sendParamNrpn (layer routing
                        REMOVED — v2 is single-engine; editTarget UI left
                        inert); StepGrid -> NRPN CCs 17/18; processBlock +
                        standalone inputs decode NRPN first and shield the
                        cluster from applyIncomingCC; sendBankDump -> NRPN
                        burst; sendBankDumpRequest -> resync 0x3F00;
                        requestLayerSync -> plain resync; queueShortMidi
                        mirrors queueSysEx's exclusive direct-device rule

## Ledger effect (update DEFERRALS_LEDGER.md in the firmware repo)
- Item 5 CLOSED: the nine curve params (147-155) already existed as APVTS
  rows marked local-only; the bridge carries the ExtCC range as NRPN, so
  they now transmit.  isLocalOnly no longer gates sends.
- Item 6 CLOSED: NRPN is plain CCs — hosts that drop SysEx pass it
  untouched.  The direct-device path REMAINS as primary transport
  (unchanged), but nothing depends on SysEx surviving the host any more.

## Deliberately NOT changed (flagged for your decision)
- SysEx-in parsing + sendSysExSetParam kept but dead: the v2 firmware never
  sends PARAM_VALUE/BANK_DUMP.  Delete in a cleanup pass when confident.
- JP8000SyxImporter untouched (file import — unrelated to the wire).
- v1 DISCREPANCY FOUND, preserved as-is: handleIncomingMidiMessage's comment
  says controller CCs reach the synth via the APVTS round-trip, but
  applyIncomingCC sets _suppressEcho — so a hardware controller into the
  plugin moves the UI only, and nothing reaches the synth.  If that is not
  intended, the fix is one line (send after apply); say the word.

## Gates
- NrpnLink + bridge: host-compiled and asserted green here.
- JUCE build (CMake) NOT run here — user gate.  Remember the Ableton VST3
  cache delete after rebuild (%APPDATA%\Ableton\...).

## Bench acceptance
1. Standalone + Teensy on USB: open plugin, pick the Teensy as direct MIDI
   out/in -> resync pull repaints every control from the synth.
2. Turn a plugin knob -> synth changes AND the ESP32 cell updates (firmware
   broadcast); turn an ESP32 encoder -> plugin control moves.  Three-way
   live view, no echo storms (watch nrpnApplied vs txSent on the status line).
3. Curve sliders (Atk/Dec/Rel Curve) now audibly reach the synth — first
   time ever over the wire.
4. VST3 in Ableton with NO direct device: params still arrive (host bus
   carries plain CCs) — the old SysEx-drop failure mode is gone.
