# JT-8000 UI — HANDOVER

Everything needed to continue in a fresh thread. Nothing earlier needs re-reading.

**Attach to the new thread:** this file, `DECISIONS.md`, the `Source/` folder,
the `skin/` folder, and the generator scripts listed under "Toolchain".

---

## 1. What this is

A new visual UI for the JT-8000 JUCE plugin, replacing the accordion editor.
Built as an **RmlUi 6.3 skin** (HTML/CSS-like) with a **custom JUCE bridge**
written from scratch. The sound engine is finished and untouched.

**Status:** panel design complete and rendered; C++ bridge complete and
compile-verified; nothing has ever been *run*.

---

## 2. Settled decisions — do not re-open

Full detail in `DECISIONS.md`. The load-bearing ones:

| Decision | Why |
|---|---|
| **RmlUi 6.3 direct (MIT)**, own bridge | gearmulator's `juceRmlUi` is GPL-3 |
| **Software renderer** on `juce::Graphics` | avoids an OpenGL context in a VST3 editor |
| **Sprite artwork, script-generated** | matched to gearmulator's jeTrancy skin to ~2% error |
| **Legends baked into background PNGs** | the reference does this; keeps the renderer tiny |
| **Canvas 3840x2160 dp** shown at 1920x1080 | one `SetDensityIndependentPixelRatio` call |
| **Cells at 90%** — knob 144 dp = 72 px | largest that fits; content 2152 of 2160 dp |
| **`params.yaml` is the single source** | the JUCE emitter was the missing piece |

### Panel arrangement (final)

- **Row 1** — Oscillator 1, Oscillator 2, then Mixer stacked over Filter
- **Row 2** — Amp Env, Filter Env, Pitch Env, LFO 1, LFO 2
- **Layer area** — Main (EQ & Drive, Mod FX, Delay, Output Mix), or Reverb,
  or Step Sequencer, or Arpeggiator, or Setup
- **Header 260 dp** — brand, LCD, preset arrows, 5 layer buttons, SHIFT
- **Shift** is latching, trims only: the 9 envelope curves + 4 LFO globals

160 parameters: 141 visible, 13 on shift, the rest inside the stepgrids.

---

## 3. The three real problems found, and their fixes

These were discovered by reading your repos, and matter more than the UI work.

**a) The JUCE emitter was never written.** `gen_params.py`'s own header says the
ESP32 and JUCE emitters were planned for Phase 5. Only the ESP one exists, which
is why `CCDefinitions.h` is hand-authored and **18 firmware parameters have no
APVTS entry at all** — the whole arpeggiator and the sequencer's aux lane.
→ `emit_juce.py` generates `JtParamTable.h`. **142 existing APVTS ids preserved
byte-for-byte** (they are persisted in saved patches and host automation, so
renaming them would break every session), 18 added.

**b) `JtBridgeTable.h` claimed to be generated and was not.** It carries a
"GENERATED — DO NOT HAND-EDIT, CI runs --check" banner, but no emitter existed.
→ `emit_bridge.py` generates it. Regression-checked: **all 139 existing entries
byte-identical, 0 changed, 0 dropped, 21 added.**

**c) Panel keys and APVTS ids diverged.** `osc1.fine_tune` vs `osc1_fine` —
**only 26 of 160 map by substitution.** Recovered by joining `JtBridgeTable.h`
against `CCDefinitions.h`; seeded into `params.yaml` as `juce_id` and `v1_cc`.

**To apply:** run `seed_ids.py` twice (`--field juce_id`, `--field v1_cc`) then
add four lines to `gen_params.py`'s `main()` — documented at the top of each
emitter. `params.seeded.yaml` is the already-seeded result.

---

## 4. Toolchain — what generates what

```
params.yaml                      SINGLE SOURCE
  ├─ gen_params.py               → ParamTable.h        (firmware, exists)
  ├─ emit_juce.py     [NEW]      → JtParamTable.h      (JUCE APVTS)
  ├─ emit_bridge.py   [NEW]      → JtBridgeTable.h     (v1 CC bridge)
  └─ seed_ids.py      [NEW]      adds juce_id / v1_cc back into params.yaml

rebuild_blocks.py  → blocks.json     (controls, measured legend widths)
pack.py            → block geometry  (packing rules, no overlaps by construction)
render_panel.py    → panel PNGs      (preview + baked backgrounds)
gen_skin.py        → skin/           (jt8000.rml + rcss + 6 backgrounds)

gen_knob_tus.py / gen_combo.py / gen_widgets.py / gen_slider_tus.py
                   → sprite sheets
```

Regenerate the skin: `rebuild_blocks.py` → `gen_skin.py`.

---

## 5. C++ — 18 files, 3,505 lines, all compile-clean

Verified with `-Wall -Wextra` against **real RmlUi 6.3 headers** (cloned from
GitHub) plus a minimal JUCE shim. Not linked, not run.

| File | Role |
|---|---|
| `JtRmlRenderer` | `RenderInterface` on `juce::Graphics`. Axis-aligned quads → `drawImage`/`fillRect`; `Path` fallback. `stats().pathFallbacks` should stay 0 |
| `JtRmlSystem` | monotonic clock, logging, cursor, clipboard |
| `JtRmlFile` | embedded `BinaryData` with a disk override for iteration |
| `JtRmlFont` | `juce::Font`-backed; whole strings cached as one texture, LRU 256 |
| `JtRmlKnob` | 128-frame sprite rotary |
| `JtRmlCombo` | selector with document-level popup |
| `JtRmlElements` | `JtRmlControl` base + fader, toggle, **stepgrid** |
| `JtRmlComponent` | hosts the context, input, tick, paint |
| `JtRmlBinding` | APVTS both ways, LCD readout |

### API traps already paid for

- `Rml::StyleSheetSpecification` has **no** `GetInstance()` — static-only in 6.3
- `Element::SetProperty` needs the typed `PropertyId` overload
- `GetClientWidth/Height` are **non-const** — they can force a layout
- `Rml::StringView` is only forward-declared in `FontEngineInterface.h`
- JUCE does **not** specialise `std::hash<juce::String>` — keys must be `std::string`
- `drag: drag` in RCSS is **mandatory** or a control never receives drag events
- All four interfaces must be set **before** `Rml::Initialise()`
- `RMLUI_FONT_ENGINE=none` is **wrong** — combos and the LCD are live text

---

## 6. Stepgrid — protocol matched to the ESP32 controller

From `PHASE9_ARP_ESP_NOTE.md`. The hardware and the plugin must agree, so
`JtRmlStepGrid` implements the same six rules:

1. **Order**: `step_select` (address) first, then `step_value` (data)
2. **Lattice**: step → norm is `step / 15.0f`, not `/16` (Int over 1..16)
3. **Forced value write**: painting the same value on a *different* step writes
   an equal float; a store that skips equal writes drops it (the `D-5` quirk)
4. **Selecting is a READ** — never write the new step's value back out, or
   browsing corrupts the pattern
5. **The cache IS the pattern** — the store only holds the last step written
6. **Lane → pair**: gate `seq.step_select/step_value`,
   aux `seq.aux_step_select/aux_step_value`, arp `arp.step_select/arp.step_accent`.
   The arp grid drives **accent only**; on/off and ratchet are ordinary controls

Also confirmed: `params.yaml` and the ESP `ParamTable.h` **already agree** — all
13 arp IDs identical, 160 params, 18 sections, section 17 = Arpeggiator.

ESP lane colours, for later alignment: gate `0xFC00` orange, aux `0x07FF` cyan,
arp `0xF81F` magenta, playhead white, focus yellow. **Colours deferred.**

---

## 7. Next steps, in order

1. **Editor wiring** — instantiate `JtRmlComponent` in `PluginEditor`, register
   `BinaryData` with `JtRmlFile` (use `getNamedResourceOriginalFilename`;
   JUCE mangles `bg_main.png` → `bg_main_png`), construct `JtRmlBinding`, wire
   the header's layer buttons to `setView()`
2. **Apply the emitters** — seed `params.yaml`, add four lines to
   `gen_params.py`, regenerate across all three repos
3. **`gen_skin.py`** does not yet emit the header's LCD or layer buttons as
   elements, nor the stepgrid's `select`/`value` attributes
4. **Build it** — `JtRmlUi.cmake` fetches RmlUi and embeds the skin
5. Lane colours; `JtRmlCombo` needs `.combo-list` / `.combo-item` RCSS

### Open questions

- 13 controls report **UNBOUND** at skin-emit time until the emitters are applied
- `usb_return_level` exists in the plugin with no `params.yaml` key (the `0x0801`
  regen already in your DEFERRALS ledger)
- `ArpPatternGrid` is referenced by the ESP note but is not in the source you sent

---

## 8. Working style that worked

- Compile against real headers rather than writing blind — it caught 7 API
  errors that would each have cost an evening
- Generators carry **gates**: the build aborts if a parameter is dropped, a
  context slot is unnamed, or a legend overflows
- Measure, never estimate — every layout collision traced back to guessing text
  width from character count
