# JT-8000 Controller — Architecture & Layout Guide

## Project Structure

```
JT8000_Controller/
  CMakeLists.txt              ← Build config
  Source/
    CCDefinitions.h           ← THE MASTER FILE — all CCs, names, sections, styles
    PluginProcessor.h/.cpp    ← Audio/MIDI engine (zero DSP, CC queue)
    PluginEditor.h/.cpp       ← Main window, header bar, viewport, multi-column layout
    SectionComponent.h/.cpp   ← Collapsible section panel (knobs, sliders, combos)
    KnobComponent.h/.cpp      ← Custom rotary knob drawing (arc + dot)
    SliderComponent.h/.cpp    ← Custom vertical fader drawing (track + scale + thumb)
    Envelopedisplay.h/.cpp    ← Custom vertical fader drawing (track + scale + thumb)
```

## How it all connects

```
CCDefinitions.h (data — params, sections, styles, row grouping)
       │
       ▼
PluginProcessor (creates JUCE parameters from kAllParams[])
       │
       ▼
PluginEditor (reads kAllParams[], creates SectionComponents, multi-column layout)
       │
       ▼
SectionComponent (holds knobs/sliders/combos/toggles, draws header)
       │
       ├──▶ KnobComponent   (ControlStyle::Knob)
       ├──▶ SliderComponent  (ControlStyle::Slider)
       ├──▶ ComboBox         (ControlStyle::Combo)
       └──▶ ToggleButton     (ControlStyle::Toggle)
```

**The key insight**: Everything is driven by the `kAllParams[]` table and
`sectionRow[]` array in `CCDefinitions.h`.  Adding/removing/reordering a CC
means changing ONE line in that table.

---

## CCDefinitions.h — The single source of truth

### ControlStyle enum

Each parameter declares its UI widget via `ControlStyle`:

```cpp
enum class ControlStyle : int
{
    Knob = 0,   // Rotary arc knob (continuous 0-127)
    Slider,     // Vertical fader with scale marks (ADSR, depth)
    Combo,      // Dropdown list (enum params)
    Toggle      // On/off checkbox (2-option enum)
};
```

### Table macros

The parameter table uses shorthand macros for readability:

```cpp
#define KNOB    ControlStyle::Knob,   nullptr, 0
#define VSLIDER ControlStyle::Slider, nullptr, 0
#define COMBO(names, n)  ControlStyle::Combo, names, n
```

Example entries:
```cpp
{ 25,  "amp_a",  "Atk",  Section::AMP_ENV,  0,  VSLIDER },          // Vertical fader
{ 23,  "flt_cut","Cutoff",Section::FILTER,  127, KNOB },             // Rotary knob
{ 113, "flt_engine","Engine",Section::FILTER,0,  COMBO(kFltEngNames,2) }, // Dropdown
```

### Section row grouping

`sectionRow[]` controls which sections appear side-by-side:

```cpp
static const int sectionRow[] = {
    -1,     // OSC1         — own row
    -1,     // OSC2         — own row
     0,     // AMP_ENV      — row 0 (all 3 envelopes side-by-side)
     0,     // FILTER_ENV   — row 0
     0,     // PITCH_ENV    — row 0
     1,     // FX_OUTPUT    — row 1 (small sections grouped)
     1,     // VELOCITY     — row 1
     1,     // PERFORMANCE  — row 1
    -1,     // BPM          — own row
};
```

Sections sharing the same row number (>= 0) are placed side-by-side
with equal width.  -1 means the section gets its own full-width row.

---

## SliderComponent — Vertical fader

Used for ADSR envelope parameters.  Visual layout:

```
    ┃ │     ← scale tick marks (left of track)
    ┃ │
    ┃ ████  ← orange fill (proportional to value)
    ┃─████──← thumb line (horizontal bar at value)
    ┃ │     ← dark track below
    ┃ │
     Atk    ← label text
```

Preferred size: 36×110px (narrower than knobs, taller for fader travel).

Scale divisions: 4 tick marks at 0%, 25%, 50%, 75%, 100%.

Same hidden-slider-wrapping pattern as KnobComponent for APVTS binding.

---

## KnobComponent — Rotary knob

Same as before but without the orange value readout below the label.
The pointer dot indicator is kept.

```
    ╭──── arc track (dark, 270°)
   ╱
┌─╱──────╮
│ ╭────╮ │ ← arc fill (orange, proportional to value)
│ │ ·  │ │ ← pointer dot (white, follows value angle)
│ ╰────╯ │ ← knob body (dark circle)
└────────┘
  LABEL       ← param name (grey)
```

Preferred size: 58×64px (reduced from 72px due to removed value text).

---

## Multi-column section layout

The editor's `layoutSections()` processes sections sequentially:

1. If `sectionRow[idx] == -1`: place on its own full-width row
2. If `sectionRow[idx] >= 0`: collect all consecutive sections with the
   same row number, divide width equally, query heights, use the tallest

This means the three envelope sections (Amp, Filter, Pitch) appear on
one row, each getting ~1/3 of the width with their ADSR sliders.

---

## Common recipes

### Change a parameter from knob to slider

In `CCDefinitions.h`, change the style macro:
```cpp
// Before:
{ 55, "l1_depth", "LFO1 Depth", Section::LFO1, 0, KNOB },
// After:
{ 55, "l1_depth", "LFO1 Depth", Section::LFO1, 0, VSLIDER },
```

### Put two sections side-by-side

1. Give them the same row number in `sectionRow[]`
2. Make sure they are consecutive in the `Section` enum

### Add a new section to an existing row

1. Add the `Section` enum value adjacent to the other sections in that row
2. Add its name to `sectionNames[]` at the same position
3. Set the same row number in `sectionRow[]`

---

## Build commands (quick reference)

```cmd
:: First time / after CMakeLists.txt changes:
rmdir /s /q C:\Users\pcsadmin\Documents\JT8000_Controller\
mkdir build && cd build
cmake .. -DJUCE_DIR=C:\Users\pcsadmin\Documents\JUCE
cmake --build . --config Release

:: After source file changes only:
cd C:\Users\pcsadmin\Documents\JT8000_Controller\build
cmake --build . --config Release
```

Output locations:
```
build\JT8000_Controller_artefacts\Release\VST3\      ← .vst3 for Ableton
build\JT8000_Controller_artefacts\Release\Standalone\ ← .exe for testing
```
