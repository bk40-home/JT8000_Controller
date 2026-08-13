# JT-8000 UI — HANDOVER

Everything a fresh session needs. Nothing earlier needs re-reading.

**Attach to the new chat:** this file, `LAYOUT.md`, `DECISIONS.md`, the
`Source/` folder, `skin/`, and `tools/`.

---

## 1. Status

A new UI for the JT-8000 JUCE plugin, built as an **RmlUi 6.2 skin** with a
hand-written JUCE bridge. **It builds, runs and works.** Knobs, faders, combos,
toggles and step grids all move and bind; SYX loading updates the display and
the engine; layers and SHIFT switch; the LCD follows the pointer.

What remains is layout polish and the gaps in §6.

---

## 2. The one thing to understand first

**Layout is data, not code.** `tools/layout.json` holds every block rect and
every control position. `tools/JT8000_Editor.html` edits it visually. The
generators read it and never recompute it.

This replaced two computed engines (`pack.py`, then `grid.py`). Both re-derived
the whole panel on any change, so a request to align one row moved things that
were already right. **Do not reintroduce automatic layout.** If the layout looks
wrong, the answer is the editor, not a better algorithm.

Read `LAYOUT.md` before touching anything visual.

---

## 3. Pipeline

```
params.yaml --> rebuild_blocks.py --> blocks.json     (what is on the panel)
                                          |
layout.json ------------------------------+--> gen_skin.py --> skin/
   ^                                      |                   | jt8000.rml
   +-- JT8000_Editor.html                 +--> render_panel.py | jt8000.rcss
                                                              | bg_base.png
                                                              + band_*.png
```

- **One base image** plus five small layer bands, not six full canvases
  (8.5 MB -> 2.8 MB).
- **All lettering is live text**, not baked, so SHIFT is a class swap and text
  stays crisp at any window size.
- Only the chassis, wells and button bodies are painted.

**Canvas 3840 x 2400 dp, shown at 1920 x 1200.** Grown from 2160 because
squeezing cells to fit produced 40 px knobs, which are unusable.

---

## 4. C++ bridge — 11 files in `Source/`

Compile-verified against real RmlUi 6.2 headers with `-Wall -Wextra`.

| File | Role |
|---|---|
| `JtRmlRenderer` | `RenderInterface` on `juce::Graphics`. Axis-aligned quads -> `drawImage`/`fillRect`; `Path` fallback. `stats().pathFallbacks` should stay 0 |
| `JtRmlSystem` | Monotonic clock, log ring, cursor, clipboard |
| `JtRmlFile` | Embedded `BinaryData`, with a disk override for iteration |
| `JtRmlFont` | `juce::Font`-backed; whole strings cached as one texture, LRU 256 |
| `JtRmlKnob` | 128-frame sprite rotary |
| `JtRmlCombo` | Selector with a document-level popup |
| `JtRmlElements` | `JtRmlControl` base + fader, toggle, stepgrid |
| `JtRmlComponent` | Owns the context, input, tick, paint |
| `JtRmlBinding` | APVTS both ways, LCD readout |
| `JtRmlPanel` | Wrapper the editor holds. Assets, actions, voice dots |
| `PluginEditor` | 34 px strip plus the panel |

### Traps already paid for — do not re-learn these

- `Rml::StyleSheetSpecification` has **no** `GetInstance()` in 6.x
- `Element::SetProperty` needs the typed `PropertyId` overload
- `GetClientWidth/Height` are **non-const** — they can force a layout
- `Rml::StringView` is only forward-declared in `FontEngineInterface.h`
- JUCE does **not** specialise `std::hash<juce::String>`
- `drag: drag` in RCSS is **mandatory** on every draggable control
- All four interfaces must be set **before** `Rml::Initialise()`
- `RMLUI_FONT_ENGINE=none` is wrong — combos and the LCD are live text
- **There is no RmlUi 6.3 tag.** Its own CMakeLists says 6.3 because master is
  6.3-dev. Pin **6.2**
- `ninepatch(outer, inner)` takes **two sprite names**, not a sprite plus edges
- A `@spritesheet` with only `src` defines **no sprites** and is discarded
- RmlUi has **no `@font-face`** and **no `pointer-events`**
- Textures must load through `Rml::GetFileInterface()`, not `juce::File` — the
  skin is `BinaryData` and there is no file to open
- Combo items must listen for **Mousedown**, not Click: pressing one moves
  focus, blur hides the list, and a `display:none` element receives no click
- Destroying the popup inside its own event handler is a use-after-free — hide,
  then destroy in `OnUpdate`
- `Rml::Initialise()` is global; the context name must be unique per instance
- `JtRmlFont` must release its texture cache **before** `Rml::Shutdown()`

---

## 5. Parameters

`params.yaml` is the single source across firmware, ESP32 and plugin.

Two emitters were written because **the JUCE emitter never existed** — which is
why 18 firmware parameters had no APVTS entry and `JtBridgeTable.h` was
hand-maintained behind a "GENERATED" banner:

- `emit_juce.py` -> `JtParamTable.h` (142 existing APVTS ids preserved
  byte-for-byte; they are persisted in saved patches and host automation)
- `emit_bridge.py` -> `JtBridgeTable.h` (all 139 existing entries identical)
- `seed_ids.py` adds `juce_id` and `v1_cc` to `params.yaml`

**Not yet wired into `gen_params.py`** — four lines, documented at the top of
each. Until then they are run by hand.

---

## 6. Outstanding

**Layout**
- Osc 1's Arbitrary group wraps awkwardly; LFO Per-Destination wraps 3+2.
  Both are editor jobs now, not code jobs.
- Lane colours not yet aligned with the ESP32's: gate orange `0xFC00`, aux cyan
  `0x07FF`, arp magenta `0xF81F`.

**Function**
- `usb_return_level` is in the APVTS with no `params.yaml` key — the `0x0801`
  regeneration in the deferrals ledger.
- **Send All** now only calls `_proc.sendBankDump()`. The old version also
  re-sent step select/value pairs, which are not in the dump. If the firmware
  needs them, that belongs in the processor. **Untested.**
- `SyxPickerOverlay` is still a JUCE overlay, by choice.
- Preset arrows were removed; there is no preset browser yet.
- `SectionComponent`, `StepSequencerGrid`, `ArpPatternGrid`, `Envelopedisplay`
  remain in the build but unreferenced. Deleting them is its own commit.

---

## 7. How this session went wrong, twice

Worth knowing so it is not repeated.

**Layout churn.** A request to align one envelope row led to rewriting the
layout engine twice. Each rewrite resolved constraints differently and moved
things that were already right. The fix was to stop computing layout at all.
**When something looks wrong by eye, reach for the editor.**

**Scripted edits destroyed a source file, twice.** `PluginEditor.cpp` was wiped
by a Python edit that split on `\r\n` when the file is **LF** — the whole file
became one line, matched, and vanished. Check line endings first, and prefer
line-range edits over pattern matching on large files.

---

## 8. Working style that helped

- Compile against **real** headers, not from memory. It caught a dozen API
  errors that would each have cost an evening.
- Generators carry **gates**: the build aborts if a parameter is dropped, a
  context slot is unnamed, or a legend overflows.
- **Measure, never estimate.** Every layout collision traced back to guessing
  text width from character count.
- Diagnostics belong in the product: the panel prints its own state and RmlUi's
  log when it cannot draw, because a plugin has no console.
