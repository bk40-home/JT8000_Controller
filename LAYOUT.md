# LAYOUT — how to change where things are

**The layout is data, not code.** It lives in `tools/layout.json`, you edit it
visually, and nothing recomputes it. A block sits where you put it and stays
there.

That was not always true. Earlier versions computed the layout — first fitting
blocks to their content, then to a column grid — and both re-derived the whole
panel on every change, so asking to line up one row moved things that were
already right. That is what algorithms do, and it is why they were replaced.

---

## The loop

```
tools/JT8000_Editor.html   →   layout.json   →   gen_skin.py   →   skin/
```

1. Open `tools/JT8000_Editor.html` in a browser. Just double-click it — no
   server, no build; the layout is baked into the page.
2. Move things.
3. **Download layout.json** and drop it over `tools/layout.json`.
4. `cd tools && python gen_skin.py`
5. Copy `skin/` into the project and rebuild.

That is the whole loop. Steps 4–5 take seconds.

---

## The editor

**Two levels.** You start in block mode, moving the section boxes.
**Double-click a block** to go inside it and place its controls and group
labels. **Escape** comes back out.

| | |
|---|---|
| Drag | move |
| Drag the right or bottom edge | resize (blocks only) |
| Shift-click | add to selection |
| Arrow keys | nudge 1 dp |
| Shift + arrows | nudge 10 dp |
| Double-click | enter a block |
| Escape | leave the block |

**Snap** — three modes:

- **Grid** — the 24-column rhythm. Keeps vertical divides lining up between
  rows, which is the thing that is tedious to do by eye.
- **Edges** — snaps to any other block's edge, for when a row should agree with
  the row above it.
- **Off** — by eye. Sometimes that is right.

Snapping is off inside a block: control positions are relative to their block,
so snapping them to panel columns would mean nothing.

**Alignment** — select two or more, then Left / Right / Top / Bottom / Same W /
Same H / Space H. This is the reason an editor beats editing numbers.

**View** cycles MAIN → REVERB → SEQ → ARP → SETUP, so you can lay out each layer
against the blocks that stay put.

**The message bar reports, it never corrects.** Overlaps, blocks off the canvas,
controls outside their block — all named, none moved. Offending blocks turn red.
Whether an overlap is a mistake is your call; a layer covering the Main layer's
blocks is expected and is not flagged.

**Reset** restores the layout as generated. It asks first.

---

## What the editor does not do

**It does not change what is on the panel.** Which controls exist, their
groups, options and defaults all come from `params.yaml` via
`rebuild_blocks.py`. The editor places what is there.

To add or remove a control, change `params.yaml`, run `rebuild_blocks.py`, then
regenerate the editor:

```
cd tools
python rebuild_blocks.py     # params.yaml -> blocks.json
python mk_editor.py          # blocks.json + layout.json -> JT8000_Editor.html
```

New controls appear at their old positions where the name matches, and at the
block origin where it does not — so a new control needs placing once.

---

## Files

| File | Role |
|---|---|
| `tools/layout.json` | **the layout.** Block rects; control and group positions |
| `tools/JT8000_Editor.html` | the editor. Self-contained, offline |
| `tools/layout.py` | reads `layout.json`; the generators import it as `pack` |
| `tools/mk_editor.py` | rebuilds the editor with current data inlined |
| `tools/blocks.json` | what is on the panel, from `params.yaml` |
| `tools/grid.py` | the old computed grid. Kept for reference only |
| `tools/pack.py` | the older content-fitted packer. Reference only |

`render_panel.py` and `gen_skin.py` both do `import layout as pack`. Swapping
that line to `import grid as pack` returns to a computed layout, which is worth
knowing if you ever want to re-derive a starting point rather than nudge one.

---

## If something looks wrong after a rebuild

- **A control is in the wrong place** — it is in `layout.json`; the editor is
  the fastest way to see and fix it.
- **A label is in the wrong place** — labels are elements positioned from the
  same data as their control, so they cannot drift independently. If one has,
  that is a bug in `gen_skin.py`, not in the data.
- **The panel is clipped** — a block runs past the canvas. The editor shows
  this in red before you ever rebuild.
