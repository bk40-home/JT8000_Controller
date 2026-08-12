#!/usr/bin/env python3
# =============================================================================
# gen_widgets.py - toggle and fader-track artwork
# -----------------------------------------------------------------------------
# The last two controls without their own sprite. The toggle was borrowing the
# reference's wide LED bar, which is the wrong shape for a square-ish panel
# switch, and the fader's track was drawn as a plain rounded rectangle with no
# depth at all.
#
# TOGGLE
#   Raised cap, dark when off, with a discrete LED. On, the LED lights and the
#   cap warms slightly - the reference's own idiom, where red is the only "on"
#   colour and a dark body never floods.
#
# FADER TRACK
#   A routed channel, drawn as a nine-patch so it works at any length: 20dp
#   caps top and bottom, the middle a plain vertical extrusion. The lit column
#   is a separate image clipped to the value, so no artwork is stretched.
# =============================================================================

import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SS = 4

TOG_W, TOG_H = 107, 80
TRK_W, TRK_H, TRK_CAP = 26, 300, 20

BODY_HI, BODY_LO = (74, 80, 92), (30, 34, 41)
BODY_ON_HI, BODY_ON_LO = (96, 74, 66), (44, 30, 28)
EDGE = (12, 14, 18)
LED_OFF = (58, 26, 26)
LED_ON = (246, 40, 34)
GLOW = (226, 30, 24)
AMBER = (255, 168, 58)


def _grad(w, h, top, bot):
    ys = np.linspace(0.0, 1.0, max(h, 1))[:, None, None]
    rgb = np.array(top, np.float32) * (1 - ys) + np.array(bot, np.float32) * ys
    return Image.fromarray(np.repeat(rgb.astype(np.uint8), max(w, 1), 1), "RGB")


def render_toggle(on=False, w=TOG_W, h=TOG_H, ss=SS):
    W, H = w * ss, h * ss
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # Cast shadow: the cap stands proud of the panel.
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle(
        [ss * 2, ss * 5, W - ss * 2, H - ss], ss * 5, fill=(0, 0, 0, 170))
    img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(ss * 2.4)))

    hi, lo = (BODY_ON_HI, BODY_ON_LO) if on else (BODY_HI, BODY_LO)
    body = _grad(W, H - ss * 4, hi, lo).convert("RGBA")
    mask = Image.new("L", (W, H - ss * 4), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, W - 1, H - ss * 4 - 1], ss * 5, fill=255)
    body.putalpha(mask.filter(ImageFilter.GaussianBlur(ss * 0.4)))
    img.alpha_composite(body, (0, 0))

    d = ImageDraw.Draw(img)
    d.line([(ss * 5, ss * 1.4), (W - ss * 5, ss * 1.4)],
           fill=(255, 255, 255, 70), width=max(1, int(ss * 1.4)))
    d.line([(ss * 5, H - ss * 5), (W - ss * 5, H - ss * 5)],
           fill=(0, 0, 0, 150), width=max(1, int(ss * 1.6)))
    d.rounded_rectangle([0, 0, W - 1, H - ss * 4 - 1], ss * 5,
                        outline=EDGE + (255,), width=max(1, int(ss)))

    # LED, upper third - where a panel switch's indicator sits.
    lx, ly, lr = W / 2.0, (H - ss * 4) * 0.34, W * 0.075
    if on:
        glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(glow).ellipse(
            [lx - lr * 2.4, ly - lr * 2.4, lx + lr * 2.4, ly + lr * 2.4],
            fill=GLOW + (150,))
        img = Image.alpha_composite(img, glow.filter(ImageFilter.GaussianBlur(lr)))
        d = ImageDraw.Draw(img)
    d.ellipse([lx - lr, ly - lr, lx + lr, ly + lr],
              fill=(LED_ON if on else LED_OFF) + (255,),
              outline=(10, 6, 6, 255), width=max(1, int(ss * 0.8)))

    return img.resize((w, h), Image.LANCZOS)


def render_track(w=TRK_W, h=TRK_H, ss=SS):
    """Routed channel. Nine-patch vertically: caps kept, middle extruded."""
    W, H = w * ss, h * ss
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    body = _grad(W, H, (30, 34, 41), (12, 14, 18)).convert("RGBA")
    # Horizontal shading so the channel looks cut rather than painted.
    arr = np.array(body, dtype=np.float32)
    xs = np.linspace(-1, 1, W)[None, :, None]
    arr[..., :3] *= (0.55 + 0.45 * (1 - xs ** 2))
    body = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W - 1, H - 1], W * 0.42,
                                           fill=255)
    body.putalpha(mask.filter(ImageFilter.GaussianBlur(ss * 0.4)))
    img = Image.alpha_composite(img, body)

    d = ImageDraw.Draw(img)
    d.line([(W * 0.2, ss * 1.2), (W * 0.8, ss * 1.2)], fill=(0, 0, 0, 200),
           width=max(1, int(ss)))
    d.line([(W * 0.2, H - ss * 1.6), (W * 0.8, H - ss * 1.6)],
           fill=(120, 128, 142, 150), width=max(1, int(ss)))
    return img.resize((w, h), Image.LANCZOS)


def render_fill(w=TRK_W, h=TRK_H, ss=SS):
    """Lit column inside the channel, clipped to value at draw time."""
    W, H = w * ss, h * ss
    col = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(col)
    for y in range(H):
        a = int(60 + 130 * (y / max(H - 1, 1)))
        d.line([(W * 0.28, y), (W * 0.72, y)], fill=AMBER + (a,))
    out = Image.alpha_composite(
        col.filter(ImageFilter.GaussianBlur(ss * 1.6)), col)
    return out.resize((w, h), Image.LANCZOS)


def compose_track(height):
    """Track at any length; only the middle is extruded."""
    base = render_track()
    if height <= TRK_H:
        return base.resize((TRK_W, height), Image.LANCZOS)
    top = base.crop((0, 0, TRK_W, TRK_CAP))
    mid = base.crop((0, TRK_CAP, TRK_W, TRK_H - TRK_CAP)).resize(
        (TRK_W, height - TRK_CAP * 2), Image.LANCZOS)
    bot = base.crop((0, TRK_H - TRK_CAP, TRK_W, TRK_H))
    out = Image.new("RGBA", (TRK_W, height), (0, 0, 0, 0))
    out.paste(top, (0, 0))
    out.paste(mid, (0, TRK_CAP))
    out.paste(bot, (0, height - TRK_CAP))
    return out


if __name__ == "__main__":
    out = "/mnt/user-data/outputs/assets"
    os.makedirs(out, exist_ok=True)
    render_toggle(False).save(f"{out}/toggle_off.png")
    render_toggle(True).save(f"{out}/toggle_on.png")
    render_track().save(f"{out}/fader_track.png")
    render_fill().save(f"{out}/fader_fill.png")
    print(f"toggle {TOG_W}x{TOG_H}   track {TRK_W}x{TRK_H} caps {TRK_CAP}")
