#!/usr/bin/env python3
# =============================================================================
# gen_slider_tus.py - slider thumb matched to the jeTrancy construction
# -----------------------------------------------------------------------------
# MEASURED SPEC, taken from slider.png / slider2.png.
#
#   canvas       165 x 140, content occupying x 22-143, y 25-116
#   thumb        122 x 92 - landscape, wider than tall
#   body         dark, mean around 12-22 levels
#   indicator    a bright band at roughly 40% of the height, mean jumping to
#                111 - not centred, sitting above the midline
#   edges        soft; alpha ramps rather than cutting hard, and the lower
#                third fades out into a cast shadow
#
# HOW IT IS USED IN THE SKIN
#   .sliderV sliderbar { decorator: image(slider.png contain); }
#   The track is plain RCSS on the parent element - there is NO track PNG and
#   no fill graphic.  One image, moved.  That is the whole slider.
#
#   slider2.png is the same thumb in an alternate colour, swapped in for the
#   :assignvelocity and :assigncontrol states.
#
# USAGE
#   python3 gen_slider_tus.py --out ./assets
# =============================================================================

import argparse
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

CANVAS_W, CANVAS_H = 165, 140     # match the reference asset exactly
THUMB_W, THUMB_H = 122, 92
OFF_X, OFF_Y = 22, 25             # content offset within the canvas

LINE_Y = 0.40                     # indicator band position, measured
SS = 4

GRAIN = 6.0                       # surface texture, same idea as the knob
GRAIN_SEED = 20250803

VARIANTS = {
    # Default thumb: dark body, bright machined band.
    "default": dict(body_hi=(58, 58, 58), body_lo=(14, 14, 14),
                    line=(168, 168, 168), rim=(96, 96, 96)),
    # Assigned state, mirroring slider2.png's role.
    "assigned": dict(body_hi=(96, 44, 40), body_lo=(30, 12, 11),
                     line=(206, 150, 142), rim=(140, 66, 60)),
}


def _linear(w, h, top, bottom):
    """Vertical gradient - the moulded face of the thumb."""
    ys = np.linspace(0.0, 1.0, h)[:, None, None]
    rgb = np.array(top, np.float32) * (1 - ys) + np.array(bottom, np.float32) * ys
    return Image.fromarray(np.repeat(rgb.astype(np.uint8), w, axis=1), "RGB")


def render_thumb(variant="default", ss=SS):
    """One thumb image on the reference canvas, ready for `decorator: image()`."""
    v = VARIANTS[variant]
    W, H = CANVAS_W * ss, CANVAS_H * ss
    tw, th = THUMB_W * ss, THUMB_H * ss
    ox, oy = OFF_X * ss, OFF_Y * ss
    r = tw * 0.055

    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))

    # --- cast shadow, offset down: the thumb sits proud of the track --------
    sh = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(sh).rounded_rectangle(
        [ox + ss * 2, oy + ss * 6, ox + tw - ss * 2, oy + th + ss * 2],
        r, fill=(0, 0, 0, 165))
    img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(ss * 3.0)))

    # --- body ---------------------------------------------------------------
    body = _linear(tw, th, v["body_hi"], v["body_lo"]).convert("RGBA")
    mask = Image.new("L", (tw, th), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, tw - 1, th - 1], r, fill=255)
    body.putalpha(mask.filter(ImageFilter.GaussianBlur(ss * 0.5)))
    img.alpha_composite(body, (ox, oy))

    d = ImageDraw.Draw(img)

    # --- rim: lit top edge, dark bottom edge --------------------------------
    d.line([(ox + tw * 0.06, oy + ss * 1.5), (ox + tw * 0.94, oy + ss * 1.5)],
           fill=v["rim"] + (190,), width=max(1, int(ss * 1.6)))
    d.line([(ox + tw * 0.06, oy + th - ss * 2.0),
            (ox + tw * 0.94, oy + th - ss * 2.0)],
           fill=(0, 0, 0, 170), width=max(1, int(ss * 2.0)))

    # --- indicator band at 40% height, the read-off datum -------------------
    ly = oy + th * LINE_Y
    d.rectangle([ox + tw * 0.04, ly - ss * 2.2, ox + tw * 0.96, ly + ss * 2.2],
                fill=v["line"] + (255,))
    # Shadow directly beneath the band, so it reads as a raised ridge.
    d.line([(ox + tw * 0.04, ly + ss * 3.2), (ox + tw * 0.96, ly + ss * 3.2)],
           fill=(0, 0, 0, 150), width=max(1, int(ss * 1.2)))

    out = img.resize((CANVAS_W, CANVAS_H), Image.LANCZOS)

    # --- surface grain, at final resolution, fixed seed ---------------------
    rng = np.random.default_rng(GRAIN_SEED)
    n = rng.normal(0.0, 1.0, size=(CANVAS_H, CANVAS_W))
    arr = np.array(out, dtype=np.float32)
    inside = arr[..., 3] > 40
    arr[..., :3] = np.clip(arr[..., :3] + (n * GRAIN * inside)[..., None], 0, 255)
    return Image.fromarray(arr.astype(np.uint8), "RGBA")


def render_contact():
    """Preview: thumbs on a track, at sprite size and at panel size."""
    sheet = Image.new("RGB", (620, 300), (23, 25, 30))
    d = ImageDraw.Draw(sheet)
    d.text((14, 8), "THUMB AT SPRITE SIZE (165x140 canvas)", fill=(200, 200, 200))
    for i, v in enumerate(VARIANTS):
        t = render_thumb(v)
        sheet.paste(t, (14 + i * 175, 26), t)

    d.text((14, 178), "ON A TRACK, AT PANEL SIZE", fill=(200, 200, 200))
    # Track drawn the way RCSS would: a plain recessed column, no PNG.
    for i, val in enumerate([0.15, 0.42, 0.68, 0.92]):
        x = 30 + i * 70
        d.rounded_rectangle([x, 200, x + 8, 290], 4, fill=(10, 12, 15),
                            outline=(48, 54, 62))
        t = render_thumb("default").resize((40, 34), Image.LANCZOS)
        y = int(290 - (290 - 200) * val) - 17
        sheet.paste(t, (x - 16, y), t)
    return sheet


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="assets")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for v in VARIANTS:
        p = os.path.join(args.out, f"slider_{v}.png")
        render_thumb(v).save(p)
        print(p)
    render_contact().save(os.path.join(args.out, "slider_tus_contact.png"))
    print("contact sheet written")


if __name__ == "__main__":
    main()
