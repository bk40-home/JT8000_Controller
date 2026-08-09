# PHASE9_ARP_UI_NOTE.md — JUCE Controller Arpeggiator UI

Adds the Arpeggiator editor page to the JT8000_Controller JUCE plugin so the
Phase 9 firmware arp can be driven and auditioned. UI-side only; the firmware
side shipped separately.

## Architecture followed (diagnosed from the repo, not assumed)

The controller's live wire path is:

    UI (v1-CC-keyed APVTS)
      -> JtBridgeExt / JtBridgeTable  (v1cc <-> 14-bit ParamID)
      -> NrpnLink                     (NRPN cluster on the wire)
      -> firmware

`JtBridgeTable.h`'s ParamIDs match the firmware v2 map exactly (seq 0x0680,
clock 0x0600, arp 0x0880). (Note: `ParamMapJuce.h` carries an OLDER, unused
ParamID map — it is not on this path and was left untouched.)

v2-only params that never had a v1 CC use **out-of-band pseudo-CCs** via
`JtBridgeExt::kExtPairs` — the established `master.volume` (cc 200) / USB-return
(cc 204) precedent. The arp's nine global params follow it; the per-step lanes
go direct-by-ParamID like the seq aux lane.

## What was added

**Global arp params (9) — APVTS, pseudo-CC 205..213 -> firmware 0x0880..0x0888:**
enable, mode, octaves, latch, rate, free_hz, gate, swing, steps.
- `JtBridgeExt.h` — 9 `ExtPair` rows (205..213).
- `CCDefinitions.h` — 9 `CCParam` rows in a new `Section::ARP`; `kArpModeNames`
  and `kArpOctNames` option arrays; rate reuses `kTimingNames` (12).
These bind to normal knobs/combos and round-trip both directions automatically
(outbound via `parameterChanged -> sendParamNrpn`; inbound via
`v1CcFromParamId -> applyIncomingCC -> findParamByCC -> APVTS`).

**Per-step pattern — direct-by-ParamID (no APVTS), like the seq aux lane:**
- `NrpnLink.h` — new `ArpParamId` namespace (0x0889 select, 0x088A on/off,
  0x088B accent, 0x088C ratchet).
- `PluginProcessor.{h,cpp}` — `sendArpStepEdit(step, lane, value)`: select-then-
  value by ParamID, lane 0/1/2 -> on-off/accent/ratchet. Step-select uses the
  same 1..16 int-lattice encode the seq lane uses.

**New widget — `ArpPatternGrid.{h,cpp}`:** 16 columns x 3 stacked lanes —
on/off toggle cells, accent bars (0..127), ratchet mini-bars (1..4). Orange
theme, click/drag editing, playhead marker from the status word. Mirrors
`StepSequencerGrid` idioms. Ratchet encodes on the firmware's decode lattice
{0,42,85,127} (verified round-trip to counts 1..4).

**Section plumbing:**
- `CCDefinitions.h` — `Section::ARP`, name, row; new `ControlStyle::ArpGrid`
  plus a sentinel row (`arp_grid`, placeholder cc 214) special-cased in
  `buildSections`; both `StepGrid` skip points extended to `ArpGrid` so the
  sentinel never emits MIDI.
- `SectionComponent.{h,cpp}` — `addArpGrid()`, `_arpGrids`, `getArpGrids()`.
- `PluginEditor.cpp` — `ArpGrid` case builds the grid wired to `sendArpStepEdit`;
  arp grids added to Send All and the playhead poll.
- `CMakeLists.txt` — `ArpPatternGrid.{h,cpp}` in the app source list.

## Pre-existing issue found and fixed (no-silent-change rule)

`Section` enum, `sectionNames[]` and `sectionRow[]` were misaligned: the arrays
carried a **"Voice mode"** slot that the enum did not name (the enum ended one
short, relying on positional coincidence). Inserting `ARP` naively would have
put the arp panel on the Voice-mode slot. Fixed by adding an explicit
`VOICE_MODE` enum value before `ARP`; all three arrays now align at 21 entries
(verified programmatically). The Voice-mode slot is empty (its params actually
live in `Section::PERFORMANCE`) — that pre-existing quirk is preserved, not
"repaired", since changing it is out of scope.

## Verification (structural — JUCE not host-compilable in my env; build in MSVC)

- Brace/paren balance clean on all 9 edited + 2 new files.
- `kNumParams` auto-derives via `sizeof(kAllParams)`, so all 10 new rows
  propagate to every loop automatically.
- enum/names/rows aligned at 21, checked programmatically.
- Outbound and inbound param round-trips traced end-to-end through the real
  bridge + NRPN path.
- `ValueLaw::ccFromIntLattice/intLatticeFromCc` signatures match the widget's
  use; `statusPlayStep` is 4-bit (0..15) so the 16-step playhead fits.
- Line endings: LF throughout, matching the repo (no diff noise).

## To confirm when you build

1. The three continuous arp knobs (free_hz, gate, swing) send a 0..127 CC byte
   that the firmware maps through its own norm curve — if any range feels off,
   that mapping is the first place to look.
2. Chord mode on the firmware fires rhythmic chord stabs driven by the step
   pattern (not a strum) — the UI just drives it; behaviour is firmware-side.

## Deferred

- Preset/patch save of the per-step arp pattern (the grid is send-only today,
  like the seq grids — the firmware holds the pattern; a GET/bank-dump reflect
  for the arp lanes can come later).
- Bespoke arp look-and-feel polish beyond the shared orange theme.
