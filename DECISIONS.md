# JT-8000 UI — decisions log

Settled calls. If something here needs to change, change it here first.

## Route

- **Layout engine**: RmlUi **6.3** direct (MIT), our own JUCE bridge. Not
  gearmulator's `juceRmlUi` (GPL-3), not pure JUCE.
- **Renderer**: our own `RenderInterface` against `juce::Graphics`. Consequence:
  the skin may use only solid fills, absolute layout and image decorators. No
  gradients, shadows, transforms or radii — all depth lives in the artwork.
- **Font engine**: `JtRmlFont`, backed by `juce::Font`, so FreeType is not
  needed (`RMLUI_FONT_ENGINE=none`). `none` alone would have been wrong: the
  combo's selected value and the LCD are live text and would render blank.
  Strings are cached whole as one texture each rather than as a glyph atlas —
  a string then costs one `drawImage` on the renderer's quad fast path.
- **Assets**: skin and sprites embedded as `BinaryData`; an optional disk root
  overrides them for iteration without a rebuild.
- **Artwork**: pre-rendered sprite sheets, script-generated, not hand-painted.
  `gen_knob_tus.py`, `gen_slider_tus.py`, `gen_buttons_tus.py`.

## Look

- Matched to gearmulator's **jeTrancy** skin, measured not guessed:
  knob = silver face + dark skirt + black pointer, **no value arc**;
  128 frames at 210 px; body is 54% of the frame, rest is shadow padding.
  Verified to ~2% mean radial error.
- Surface grain is **frame-locked to a fixed seed** — a per-frame field crawls
  during a sweep.
- Red is the only "on" colour. Two mechanisms: a discrete LED on dark bodies,
  full-face illumination on light ones.
- Slider = one thumb PNG, no track graphic. Track is drawn by the skin.
- **Legends are painted into the background**, never drawn at runtime. That is
  why the reference never collides, and it keeps text measurement out of the
  renderer.
- Knob legends sit **above** the knob (as the reference does), combo legends to
  the **left**, fader legends below.
- Context slots are labelled with their **role**, never with one member —
  AMOUNT, not PITCH DEPTH.

## Geometry

- Canvas **3840 x 2160 dp**, displayed at 1920x1080. Scaling is one call to
  `Context::SetDensityIndependentPixelRatio()`.
- Cells at **90%**: knob 144 dp (72 px at 1920). Content 2152 of 2160 dp.
- Header **260 dp** at the top: brand, LCD, preset arrows, layer buttons, shift.
- Reference *geometry* is discarded; reference *style* is kept.
- **All text is measured** with the real font at authoring size and stored in
  `blocks.json`. Every collision after the layout rewrite traced back to
  estimating width from character count.

## Packing rules

- Width goes to whichever block is currently **tallest** - a row's height is the
  max of its members, so only the tallest is worth shrinking. A rebalance pass
  hands width back from the block with most slack.
- A block is never widened past the width at which all its groups fit on one
  line. Blocks with identical content get identical widths.
- Groups flow in **two dimensions** inside a block; a group of <= 5 controls
  never wraps internally.
- Groups may be flagged **column** (oscillator Wave/Octave).
- A row entry may be a **tuple**, meaning those blocks share one column slot and
  stack vertically (Mixer over Filter).
- Within a group: knobs, then toggles, then selectors.
- Single-control groups are merged into their neighbour.

## Containment rules

1. A box is the union of its children plus its own title strip and padding.
2. Siblings never overlap — by construction, not by collision check.
3. A control's box **includes its legend**, so legends cannot collide either.
4. Nesting follows `params.yaml`: section → region, `ui.group` → group.

## Arrangement

- **Row 1** — Oscillator 1, Oscillator 2, then Mixer stacked over Filter
- **Row 2** — Amp Env, Filter Env, Pitch Env, LFO 1, LFO 2
- **Layer area** — Main (EQ & Drive, Mod FX, Delay, Output Mix), or Reverb, or
  Step Sequencer, or Arpeggiator. Packed as a real row; its height is the
  tallest of the four.
- **Menu-driven, still undesigned** — Voice mode, Performance, BPM Clock,
  Velocity (18 params)
- X-Mod sits with Oscillator 1, Osc Sync with Oscillator 2, Mod Dest with
  Oscillator 1's Arbitrary group. Mixer holds only its five levels.

## Interaction

- **Shift is latching**, with a persistent annunciator beside the LCD.
  Rule: trim parameters only, never a control that changes character on its own.
  Currently the envelope curves and the OBXa extras.
- **LFO**: `sync` is the rate mode (its option set begins with Free), so mode and
  divisions are one control and `freq` applies when Free. Destination selects
  which depth the AMOUNT knob edits — four parameters, one knob.
- **Sequencer / Arp**: `step_select` + `step_value` already express
  select-a-step-then-edit, so one `<stepgrid>` element absorbs four parameters
  and shows the pattern shape. Grids are sized to sit on the same line as the
  layer's controls - a wrapped grid adds its full height to the layer.
- **Shift membership is per section**, not by leaf name: the sequencer has its
  own `depth` and `destination` which must stay on the panel.
- **LCD** shows the name and value of whatever is under the pointer. No control
  carries its own numeric readout.

## Verified invariants

- All **160 parameters placed**, zero type mismatches, zero unnamed context
  slots. The generator aborts if any parameter fails to reach the document.
- A widget type that cannot express its parameter (toggle holding a continuous
  value) is reported, never silently forced.


## Bridge (all compile-verified against real RmlUi 6.3 headers, -Wall -Wextra)

| File | Purpose |
|---|---|
| `JtRmlRenderer` | `RenderInterface` on `juce::Graphics`. Classifies triangle pairs; axis-aligned quads take `drawImage`/`fillRect`, anything else falls back to a `Path`. `stats().pathFallbacks` should stay 0. |
| `JtRmlSystem` | Monotonic clock, logging, cursor (recorded, applied by the host), clipboard. |
| `JtRmlFile` | Embedded assets with a disk override. |
| `JtRmlFont` | Whole-string textures, LRU-bounded at 256. |
| `JtRmlKnob` | Sprite-strip rotary, 128 frames. |
| `JtRmlCombo` | Selector with a document-level popup list. |
| `JtRmlElements` | Fader, toggle, stepgrid on a shared `JtRmlControl` base. |
| `JtRmlComponent` | Hosts the context, input, tick, paint. |
| `JtRmlBinding` | APVTS both ways, LCD readout. |

### Traps found by compiling (would each have cost an evening)

- `Rml::StyleSheetSpecification` has no `GetInstance()` — static-only in 6.3.
- `Element::SetProperty` needs the typed `PropertyId` overload.
- `GetClientWidth/Height` are **non-const** — they can force a layout.
- `Rml::StringView` is only forward-declared in `FontEngineInterface.h`.
- JUCE does not specialise `std::hash<juce::String>` — map keys must be
  `std::string`.
- `drag: drag` is **required** in RCSS or a control never receives drag events.
- All four interfaces must be set **before** `Rml::Initialise()`; a font engine
  set afterwards is ignored.

## Still to do

- Editor: instantiate `JtRmlComponent`, register `BinaryData` with `JtRmlFile`,
  construct `JtRmlBinding`, wire the header's layer buttons to `setView()`.
- `gen_skin.py` does not yet emit the header's LCD or layer buttons as elements.
- Nothing has been run. Compile-verified only, against a JUCE shim.
