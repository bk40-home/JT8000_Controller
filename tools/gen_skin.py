#!/usr/bin/env python3
# =============================================================================
# gen_skin.py - emit the JT-8000 RmlUi skin
# -----------------------------------------------------------------------------
# Geometry comes from pack.py and control positions from render_panel's own
# layout pass, so the preview images and the shipped skin are produced from one
# source and cannot disagree.
#
# BAKED BACKGROUNDS
#   Every legend, rule, section title, well and bevel is painted into one PNG
#   per view; the RML places CONTROLS ONLY. This is what the reference does, and
#   it matters here for a specific reason: we are writing the RenderInterface
#   ourselves, so every RCSS feature used is a feature we must implement. With
#   the background baked, the renderer needs only absolute positioning, solid
#   fills and image decorators - no text measurement, gradients, shadows or
#   corner radii.
#
# VIEWS
#   main, shift, and one per layer. A view is a class on <body>; each control
#   carries the classes of the views it belongs to. Switching view is a class
#   swap - no relayout, no second document - and the background decorator
#   changes with it.
#
# UNITS
#   dp throughout. RmlUi scales dp by the context's density-independent pixel
#   ratio, so 1920x1080 is one call to SetDensityIndependentPixelRatio(0.5).
# =============================================================================

import json
import os

import pack
import render_panel as rp

# -----------------------------------------------------------------------------
# Panel key -> APVTS parameter id.
#
# The two sides name parameters differently: params.yaml uses dotted keys
# (osc1.fine_tune) and the plugin's APVTS uses hand-authored short ids
# (osc1_fine). Only 26 of 160 match by substitution, so the mapping is built by
# joining JtBridgeTable.h (v1 CC -> ParamID, with the yaml key in the comment)
# against CCDefinitions.h (v1 CC -> APVTS id), plus four matched by name.
#
# Emitting the APVTS id straight into the document means the binding needs no
# translation at runtime, and a key with no parameter is caught HERE rather
# than silently producing a dead control.
# -----------------------------------------------------------------------------
KEY_TO_APVTS = json.load(open("key2apvts.json"))

# Lane -> (select, value) per grid, matching the ESP32 controller exactly. The
# arp grid drives ACCENT only; on/off and ratchet are ordinary controls.
STEPGRID_LANES = {
    "Step Sequencer": [("seq.step_select", "seq.step_value"),
                       ("seq.aux_step_select", "seq.aux_step_value")],
    "Arpeggiator":    [("arp.step_select", "arp.step_accent")],
}

OUT = "/mnt/user-data/outputs/skin"
K = rp.K

VIEWS = [("main", None, False), ("shift", None, True),
         ("reverb", "Reverb", False), ("seq", "Step Sequencer", False),
         ("arp", "Arpeggiator", False), ("setup", "Setup", False)]

# Element name per widget kind, matching the custom elements the bridge
# registers. The names follow the reference's vocabulary so the skin stays
# portable if the host layer ever changes.
TAG = {"knob": "knob", "fader": "fader", "combo": "combo",
       "toggle": "toggle", "stepgrid": "stepgrid"}


def control_rect(kind, cell, x, y, k):
    """Where the control sits inside its cell, in dp.

    Mirrors render_panel exactly: knobs centre in a cell that may be wider than
    the knob when the legend is longer, and everything except the fader sits
    below a 34dp legend strip.
    """
    if kind == "knob":
        w = h = int(160 * k)
        cw = max(w, int(cell.get("lw", 0) * k))
        return x + (cw - w) // 2, y + int(34 * k), w, h
    if kind == "fader":
        return x + int(22 * k), y, int(26 * k), int(284 * k)
    if kind == "combo":
        return x, y + int(34 * k), int(cell.get("w", 300) * k), int(45 * k)
    if kind == "toggle":
        return x, y + int(34 * k), int(107 * k), int(80 * k)
    return x, y + int(34 * k), int(cell["w"] * k), int(cell["h"] * k)


def collect(k):
    """Every control, with its absolute rect and the views it appears in."""
    place, _ = pack.build(k)
    layer_names = set(pack.LAYER_BLOCKS)
    main_row = set(pack.ROWS[-1])

    out = []
    for name, (bx, by, bw, bh) in place.items():
        if name in layer_names:
            views = [v for v, ln, _ in VIEWS if ln == name]
        elif name in main_row:
            views = ["main", "shift"]
        else:
            views = [v for v, _, _ in VIEWS]

        for o in rp.layout_block(pack.B[name], bw, k):
            if o["group"]:
                continue
            cell = o["it"]
            x, y = bx + o["x"], by + o["y"]
            rect = control_rect(cell["k"], cell, x, y, k)
            out.append(dict(block=name, kind=cell["k"], cell=cell,
                            rect=rect, views=views))
    return out


def emit_rcss():
    L = ["/* =========================================================",
         " * jt8000.rcss - generated by gen_skin.py. Do not hand edit.",
         " *",
         " * Feature budget: absolute position, solid fill, image",
         " * decorator. Nothing else - every property here has to exist",
         " * in our own RenderInterface.",
         " * ========================================================= */",
         "",
         "body",
         "{",
         "\twidth: 3840dp;",
         "\theight: 2160dp;",
         "}",
         ""]
    for v, _, _ in VIEWS:
        L.append(f"body.{v} {{ decorator: image(bg_{v}.png); }}")
    L += ["",
          "/* A control appears only in the views it belongs to. */",
          "knob, fader, combo, toggle, stepgrid",
          "{",
          "\tposition: absolute;",
          "\tdisplay: none;",
          "}"]
    for v, _, _ in VIEWS:
        L.append(f"body.{v} .v-{v} {{ display: block; }}")
    L += ["",
          "/* The knob element takes frames + spriteprefix; the prefix is",
          " * concatenated with a three digit index to address a sprite. */",
          "@spritesheet knob_silver",
          "{",
          "\tsrc: knob_silver.png;",
          "}",
          "",
          ".knob",
          "{",
          "\tframes: 128;",
          "\tspriteprefix: knob_silver_;",
          "\tdrag: drag;          /* REQUIRED - no drag events without it */",
          "}",
          "",
          ".combo          { decorator: ninepatch(combo_off, 24px 60px);"
          " color: #FFA83A; }",
          ".toggle         { decorator: image(toggle_off.png); }",
          ".toggle:checked { decorator: image(toggle_on.png); }",
          ".fader          { decorator: ninepatch(fader_track, 20px 20px); }",
          ".fader thumb    { decorator: image(slider_default.png); }",
          ""]
    return "\n".join(L) + "\n"


def emit_rml(controls):
    L = ["<rml>", "<head>", "\t<title>JT-8000</title>",
         '\t<link type="text/rcss" href="jt8000.rcss"/>',
         "</head>", '<body class="main">', ""]
    by_block = {}
    for c in controls:
        by_block.setdefault(c["block"], []).append(c)

    for block, items in by_block.items():
        L.append(f"\t<!-- {block} -->")
        for c in items:
            x, y, w, h = c["rect"]
            cls = " ".join(c["kind"] for _ in (0,)) + " " + \
                  " ".join("v-" + v for v in c["views"])
            extra = ""
            if c["kind"] == "combo":
                # Option strings travel in the document: the element reports a
                # normalised value and an opaque key, and knows nothing about
                # params.yaml - same contract as every other control.
                opts = c["cell"].get("opts") or []
                # Escape: option strings include S&H, which is not valid inside
                # an XML attribute unescaped.
                esc = [o.replace("&", "&amp;").replace('"', "&quot;")
                       .replace("<", "&lt;") for o in opts]
                extra = ' options="%s"' % ", ".join(esc)
            elif c["kind"] == "stepgrid":
                # The grid speaks the controller's select-then-value protocol,
                # so each lane needs its own pair. Emitted as APVTS ids, in
                # lane order.
                pairs = STEPGRID_LANES.get(c["block"], [])
                sel = ",".join(KEY_TO_APVTS.get(a, "") for a, _ in pairs)
                val = ",".join(KEY_TO_APVTS.get(b, "") for _, b in pairs)
                extra = (' lanes="%d" steps="16" select="%s" value="%s"'
                         % (len(pairs) or c["cell"].get("lanes", 2), sel, val))
            key = c["cell"].get("key", "")
            pid = KEY_TO_APVTS.get(key, "")
            L.append(f'\t<{TAG[c["kind"]]} class="{cls}" '
                     f'param="{pid}" key="{key}"{extra} '
                     f'style="left:{x}dp; top:{y}dp; '
                     f'width:{w}dp; height:{h}dp;"/>')
        L.append("")
    L += ["</body>", "</rml>"]
    return "\n".join(L) + "\n"


def main():
    os.makedirs(OUT, exist_ok=True)

    for v, layer, shift in VIEWS:
        img, _ = rp.render(k=K, view=1.0, shift=shift, layer=layer,
                           controls=False)
        img.save(f"{OUT}/bg_{v}.png")
    print("backgrounds : %d at 3840x2160" % len(VIEWS))

    controls = collect(K)
    open(f"{OUT}/jt8000.rcss", "w").write(emit_rcss())
    doc = emit_rml(controls)
    open(f"{OUT}/jt8000.rml", "w").write(doc)

    # --- gate: a skin that silently omits a control is worse than one that
    # fails to build ---------------------------------------------------------
    B = json.load(open("blocks.json"))
    want = {i["key"] for b in B for g in b["groups"] for i in g["items"]
            if "key" in i}
    got = {c["cell"].get("key") for c in controls}
    missing = sorted(want - got)
    if missing:
        raise SystemExit("ABORT: %d controls not emitted: %s"
                         % (len(missing), missing))

    print("jt8000.rml  : %d lines, %d controls"
          % (len(doc.splitlines()), len(controls)))
    print("gate        : all %d controls emitted" % len(want))

    # Controls whose parameter does not exist in the plugin's APVTS. These
    # render but cannot bind; reported rather than left to be discovered by
    # turning a dead knob.
    synthetic = {"step_sequencer.pattern", "arpeggiator.pattern"}
    unbound = sorted(k for k in got
                     if k and k not in KEY_TO_APVTS and k not in synthetic)
    if unbound:
        print("UNBOUND     : %d controls have no APVTS parameter" % len(unbound))
        for k in unbound:
            print("                %s" % k)
    print("written     : %s" % OUT)


if __name__ == "__main__":
    main()
