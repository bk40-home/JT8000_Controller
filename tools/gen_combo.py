#!/usr/bin/env python3
# =============================================================================
# gen_combo.py - combo / value-display artwork
# -----------------------------------------------------------------------------
# A combo was being drawn with the LED button sprite stretched to width, which
# read as a smear behind the text rather than as a control. It needs its own
# widget.
#
# COLOUR DECISION
#   Combos use the SAME palette as the patch display: amber on a dark warm
#   recess, with the glass scanline. Every place the instrument shows you a
#   value - the patch name, the parameter readout, a selector - then looks like
#   one family, and the red LEDs stay unambiguously "this is on/off" rather
#   than competing as another kind of readout.
#
# NINE-PATCH, not stretch
#   The sprite is drawn once at a reference width and split into three parts:
#     left cap    24dp   the recess edge and its lit lip
#     middle       any   a plain vertical extrusion, so it tiles or stretches
#                        with no visible distortion
#     right cap   60dp   the arrow well
#   Stretching the whole sprite is what made the LED button look wrong; only
#   the middle may be scaled. In RCSS this is `decorator: ninepatch(...)`.
# =============================================================================

import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SS = 4
H = 90                      # 45dp at 2x authoring
W = 300                     # reference width; middle is stretchable
CAP_L, CAP_R = 24, 60

# Patch-display palette. Shared with the LCD so both read as the same surface.
BG_TOP = (46, 17, 3)
BG_BOT = (20, 9, 2)
EDGE = (10, 12, 15)
LIP = (120, 62, 18)
AMBER = (255, 168, 58)
AMBER_DIM = (180, 102, 28)
GLOW = (255, 150, 40)


def _grad(w, h, top, bot):
    ys = np.linspace(0.0, 1.0, max(h, 1))[:, None, None]
    rgb = np.array(top, np.float32) * (1 - ys) + np.array(bot, np.float32) * ys
    return Image.fromarray(np.repeat(rgb.astype(np.uint8), max(w, 1), 1), "RGB")


def render(width=W, height=H, on=False, ss=SS):
    """One combo body. `on` brightens the recess, for the open/focused state."""
    Wp, Hp = width * ss, height * ss
    img = Image.new("RGBA", (Wp, Hp), (0, 0, 0, 0))

    top = tuple(min(255, c + (26 if on else 0)) for c in BG_TOP)
    bot = tuple(min(255, c + (18 if on else 0)) for c in BG_BOT)
    body = _grad(Wp, Hp, top, bot).convert("RGBA")

    mask = Image.new("L", (Wp, Hp), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, Wp - 1, Hp - 1],
                                           ss * 3, fill=255)
    body.putalpha(mask.filter(ImageFilter.GaussianBlur(ss * 0.4)))
    img = Image.alpha_composite(img, body)

    d = ImageDraw.Draw(img)
    # Recess: dark lip along the top, lit lip along the bottom. The inverse of
    # a raised button, which is what makes it read as cut into the panel.
    d.line([(ss * 3, ss * 1.5), (Wp - ss * 3, ss * 1.5)],
           fill=(0, 0, 0, 210), width=max(1, int(ss * 2.0)))
    d.line([(ss * 3, Hp - ss * 2), (Wp - ss * 3, Hp - ss * 2)],
           fill=LIP + (150,), width=max(1, int(ss * 1.4)))
    d.rounded_rectangle([0, 0, Wp - 1, Hp - 1], ss * 3,
                        outline=EDGE + (255,), width=max(1, int(ss * 1.2)))

    # Arrow well on the right, divided by a hairline.
    ax = Wp - CAP_R * ss
    d.line([(ax, ss * 4), (ax, Hp - ss * 4)], fill=(0, 0, 0, 120),
           width=max(1, int(ss * 0.9)))
    cx, cy = ax + CAP_R * ss / 2, Hp / 2
    aw = ss * 9
    d.polygon([(cx - aw, cy - aw * 0.5), (cx + aw, cy - aw * 0.5),
               (cx, cy + aw * 0.7)], fill=AMBER_DIM + (255,))

    # Backlight bloom, so the recess looks lit rather than merely dark.
    glow = Image.new("RGBA", (Wp, Hp), (0, 0, 0, 0))
    ImageDraw.Draw(glow).rounded_rectangle(
        [ss * 4, ss * 4, Wp - ss * 4, Hp - ss * 4], ss * 3,
        fill=GLOW + (34 if not on else 60,))
    img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(ss * 5)))

    out = img.resize((width, height), Image.LANCZOS)

    # Glass scanline, applied after downsampling so the lines stay crisp.
    a = np.array(out, dtype=np.float32)
    a[1::3, :, :3] *= 0.86
    return Image.fromarray(a.astype(np.uint8), "RGBA")


def compose(width, on=False):
    """Nine-patch assembly: caps kept, middle stretched."""
    base = render(W, H, on)
    if width <= W:
        return base.resize((width, H), Image.LANCZOS)
    left = base.crop((0, 0, CAP_L, H))
    mid = base.crop((CAP_L, 0, W - CAP_R, H)).resize(
        (width - CAP_L - CAP_R, H), Image.LANCZOS)
    right = base.crop((W - CAP_R, 0, W, H))
    out = Image.new("RGBA", (width, H), (0, 0, 0, 0))
    out.paste(left, (0, 0))
    out.paste(mid, (CAP_L, 0))
    out.paste(right, (width - CAP_R, 0))
    return out


if __name__ == "__main__":
    out = "/mnt/user-data/outputs/assets"
    os.makedirs(out, exist_ok=True)
    render(W, H, False).save(f"{out}/combo_off.png")
    render(W, H, True).save(f"{out}/combo_on.png")
    open(f"{out}/combo.rcss", "w").write(
        "@spritesheet combo\n{\n"
        f"\tcombo_off: 0px 0px {W}px {H}px;\n"
        "\tsrc: combo_off.png;\n}\n\n"
        ".combo\n{\n"
        f"\tdecorator: ninepatch(combo_off, {CAP_L}px {CAP_R}px);\n"
        "\tcolor: #FFA83A;\n}\n")
    print(f"combo_off.png / combo_on.png  {W}x{H}  caps {CAP_L}/{CAP_R}")
