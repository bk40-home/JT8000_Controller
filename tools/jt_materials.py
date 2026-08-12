#!/usr/bin/env python3
# =============================================================================
# jt_materials.py - shared palette and render primitives for JT-8000 artwork
# -----------------------------------------------------------------------------
# Every generated asset (knobs, sliders, buttons) pulls its colours and its
# render helpers from here.  One definition of "bone" for the whole instrument,
# so a knob cap and a slider cap cannot drift out of agreement.
#
# A MATERIAL is a moulded or machined surface:
#     cap_hi     colour at the lit edge of the gradient
#     cap_lo     colour at the shadowed edge
#     rim        specular highlight along the lit rim
#     ink        pointer / centre-line colour that reads against the cap
#
# ACCENT is the lit indication colour, kept separate from the material so any
# cap can carry any accent without duplicating the palette.
# =============================================================================

import math

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# -----------------------------------------------------------------------------
# Accents.  The lit element - value arcs, slot fills, LEDs.
# -----------------------------------------------------------------------------
ACCENTS = {
    "cyan":    (56, 220, 224),
    "amber":   (255, 168, 58),
    "lime":    (184, 230, 46),
    "magenta": (255, 63, 164),
    "ice":     (170, 214, 235),
}

# -----------------------------------------------------------------------------
# Materials.  charcoal and bone are confirmed; the rest are candidates for the
# third "primary control" cap, replacing the rejected vermillion.
# -----------------------------------------------------------------------------
MATERIALS = {
    # --- confirmed ---------------------------------------------------------
    "charcoal": dict(cap_hi=(60, 66, 75),    cap_lo=(23, 27, 33),
                     rim=(120, 128, 140),    ink=(238, 241, 244)),
    "bone":     dict(cap_hi=(238, 234, 222), cap_lo=(150, 145, 132),
                     rim=(255, 255, 250),    ink=(48, 44, 38)),

    # --- candidates for the primary-control cap ----------------------------
    # Brushed aluminium: the machined-metal route, no colour at all.
    "steel":    dict(cap_hi=(206, 212, 220), cap_lo=(112, 119, 129),
                     rim=(255, 255, 255),    ink=(30, 34, 40)),
    # Blue-grey anodising: cool, quiet, sits close to the chassis.
    "slate":    dict(cap_hi=(96, 112, 136),  cap_lo=(32, 40, 54),
                     rim=(158, 176, 202),    ink=(238, 241, 244)),
    # Deep desaturated red: the warmth of vermillion without the Roland shout.
    "oxblood":  dict(cap_hi=(150, 66, 58),   cap_lo=(56, 22, 19),
                     rim=(198, 122, 112),    ink=(240, 226, 222)),
    # Dark olive: unusual on a synth, reads as instrument rather than toy.
    "moss":     dict(cap_hi=(104, 118, 72),  cap_lo=(34, 41, 22),
                     rim=(158, 172, 118),    ink=(238, 241, 232)),
    # Blue-violet: pairs with a cyan accent without competing for the same hue.
    "indigo":   dict(cap_hi=(96, 100, 158),  cap_lo=(30, 32, 62),
                     rim=(152, 156, 208),    ink=(238, 240, 250)),
    # Warm brass: the vintage-hardware option.
    "brass":    dict(cap_hi=(202, 158, 84),  cap_lo=(92, 66, 24),
                     rim=(240, 212, 152),    ink=(38, 30, 12)),
}

CONFIRMED = ["charcoal", "bone"]
CANDIDATES = ["steel", "slate", "oxblood", "moss", "indigo", "brass"]

# -----------------------------------------------------------------------------
# Chassis colours, shared with the CSS look study so previews match the panel.
# -----------------------------------------------------------------------------
PANEL_BG = (21, 24, 29)
WELL = (10, 12, 15)
TRACK = (43, 50, 58)
LABEL = (142, 149, 160)

SS = 4          # supersampling factor for all renders


# =============================================================================
# Primitives
# =============================================================================
def bbox(centre, radius):
    """PIL bounding box for a circle."""
    cx, cy = centre
    return [cx - radius, cy - radius, cx + radius, cy + radius]


def pil_angle(deg):
    """Our angles are clockwise from 12 o'clock; PIL's are from 3 o'clock."""
    return deg - 90.0


def linear_gradient(w, h, top_rgb, bottom_rgb, tilt=0.28):
    """Diagonal gradient - the moulded-surface look.

    `tilt` biases the light axis towards the horizontal so surfaces read as lit
    from the upper left, matching the chassis bevels.
    """
    ys, xs = np.mgrid[0:h, 0:w]
    t = (ys / max(h - 1, 1)) * (1.0 - tilt) + (xs / max(w - 1, 1)) * tilt
    t = np.clip(t, 0.0, 1.0)[..., None]
    rgb = (np.array(top_rgb, np.float32) * (1.0 - t)
           + np.array(bottom_rgb, np.float32) * t)
    return Image.fromarray(rgb.astype(np.uint8), "RGB")


def brushed(w, h, base_rgb, strength=9, seed=7):
    """Horizontal brushed-metal texture, for steel and brass caps.

    A single row of noise smeared vertically: that directional coherence is what
    makes brushed metal read as metal rather than as grain.
    """
    rng = np.random.default_rng(seed)
    line = rng.normal(0.0, strength, size=(1, w))
    field = np.repeat(line, h, axis=0)
    field += rng.normal(0.0, strength * 0.18, size=(h, w))
    rgb = np.clip(np.array(base_rgb, np.float32)[None, None, :] + field[..., None],
                  0, 255)
    return Image.fromarray(rgb.astype(np.uint8), "RGB")


def circle_mask(size, radius, centre, feather=1.5):
    """Anti-aliased circular alpha mask."""
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).ellipse(bbox(centre, radius), fill=255)
    return m.filter(ImageFilter.GaussianBlur(feather))


def rounded_mask(w, h, radius, feather=1.5):
    """Anti-aliased rounded-rectangle alpha mask, for slider caps and slots."""
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], radius, fill=255)
    return m.filter(ImageFilter.GaussianBlur(feather))


def bloom(layer, radius, amount=0.55):
    """Soft glow taken from a layer's own alpha - used for every lit element."""
    b = layer.filter(ImageFilter.GaussianBlur(radius))
    b.putalpha(b.getchannel("A").point(lambda a: int(a * amount)))
    return b


def label(draw, xy, text, fill=LABEL):
    """Preview-sheet caption. Not shipped; contact sheets only."""
    draw.text(xy, text, fill=fill)
