#!/usr/bin/env python3
# =============================================================================
# gen_knob_tus.py - knob artwork matched to the measured jeTrancy construction
# -----------------------------------------------------------------------------
# MEASURED SPEC, taken by radial profiling of knob_page0.png frame 64.
# All radii are in pixels within the 210 px frame:
#
#   r  0 - 33   FACE     flat, bright (mean 223 at centre, ~210 at edge)
#   r  8 - 31   POINTER  pure black (min 0), the ONLY element that moves
#   r 34 - 39   GROOVE   dark recess between face and skirt (mean 29)
#   r 40 - 43   RIM      lit ring, specular (max 186)
#   r 44 - 57   SKIRT    dark body (mean 42) with a strong highlight (max 211)
#   r 57 - 80   SHADOW   soft drop shadow, alpha decaying 60 -> 0
#
# THE CRITICAL FINDING
#   Differencing frame 0 against frame 127 shows change only inside r < 33.
#   There is NO value arc, NO indicator ring, NO glow.  The entire read-out is
#   a black pointer on a bright face.  My earlier artwork led with a bloomed
#   cyan arc, which is why it sat so far from the reference: I had added a
#   dominant element that does not exist in the original.
#
#   Body diameter is 114 px in a 210 px frame - only 54%.  The remaining 46%
#   is transparent padding carrying the drop shadow.  Layout must account for
#   that or the knobs will look small and over-spaced.
#
# VARIANTS
#   'silver'  page0 - flat bright face, black pointer
#   'dark'    page1 - darker gradient face, same body
#
# USAGE
#   python3 gen_knob_tus.py --out ./assets
# =============================================================================

import argparse
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

FRAME = 210          # match the reference sheet exactly
FRAMES = 128
COLS = 16
SS = 3               # supersample; 210*3 is already heavy, 3 is enough

ANGLE_MIN, ANGLE_MAX = -135.0, 135.0

# Measured grain amplitudes (std, in levels) from the reference artwork.
GRAIN_FACE = 2.5   # pointer-excluded high-pass std then matches the reference
GRAIN_SKIRT = 10.1  # tuned so the measured high-pass std lands on 11.0
GRAIN_SEED = 20250803

_GRAIN_CACHE = {}


def _grain_field(size):
    """Fixed speckle field, cached per size.

    Generated once and shared by every frame so the texture stays locked to the
    cap.  A per-frame field would crawl visibly during a knob sweep.
    """
    if size not in _GRAIN_CACHE:
        rng = np.random.default_rng(GRAIN_SEED)
        n = rng.normal(0.0, 1.0, size=(size, size))
        # Slight smoothing: raw per-pixel noise reads as digital dither rather
        # than as a moulded surface.
        n = np.array(Image.fromarray(((n * 40) + 128).clip(0, 255).astype(np.uint8))
                     .filter(ImageFilter.GaussianBlur(0.45)), dtype=np.float32)
        n = (n - n.mean()) / max(n.std(), 1e-6)
        _GRAIN_CACHE[size] = n
    return _GRAIN_CACHE[size]

# Radii as a fraction of FRAME, from the measured profile.
R_FACE = 35.0 / 210
R_GROOVE_IN = 34.0 / 210
R_RIM_IN = 40.0 / 210
R_RIM_OUT = 43.5 / 210
R_SKIRT = 57.0 / 210
R_SHADOW = 88.0 / 210
R_PTR_IN = 8.0 / 210
R_PTR_OUT = 31.0 / 210
W_PTR = 6.0 / 210     # pointer width

VARIANTS = {
    # Measured means from the reference; kept as plain tuples so they are easy
    # to retune once the panel colour is settled.
    "silver": dict(face_c=(230, 230, 230), face_e=(212, 212, 212),
                   pointer=(0, 0, 0), groove=(22, 22, 22),
                   rim=(150, 150, 150), skirt_hi=(104, 104, 104),
                   skirt_lo=(40, 40, 40)),
    "dark":   dict(face_c=(104, 104, 104), face_e=(64, 64, 64),
                   pointer=(8, 8, 8), groove=(20, 20, 20),
                   rim=(138, 138, 138), skirt_hi=(92, 92, 92),
                   skirt_lo=(32, 32, 32)),
}


# =============================================================================
def _radial(size, centre, inner, outer, c_in, c_out):
    """RGBA ring with a radial colour ramp between two radii.

    Used for the face (bright centre falling off to the edge) and for the
    skirt.  Built with numpy because PIL has no radial gradient.
    """
    yy, xx = np.mgrid[0:size, 0:size]
    r = np.hypot(yy - centre, xx - centre)
    t = np.clip((r - inner) / max(outer - inner, 1e-6), 0.0, 1.0)[..., None]
    rgb = np.array(c_in, np.float32) * (1 - t) + np.array(c_out, np.float32) * t
    a = np.where((r >= inner - 0.5) & (r <= outer), 255, 0).astype(np.float32)
    out = np.dstack([rgb, a[..., None]]).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def _angular_shade(size, centre, inner, outer, base, hi, light_deg=-40.0,
                   spread=110.0):
    """Ring shaded by angle - the specular highlight on the skirt.

    The reference's skirt peaks at 211 against a mean of 42, so the highlight
    is narrow and strong rather than a broad gradient.  `light_deg` places it
    up and to the left, consistent with the panel bevels.
    """
    yy, xx = np.mgrid[0:size, 0:size]
    dy, dx = yy - centre, xx - centre
    r = np.hypot(dy, dx)
    ang = np.degrees(np.arctan2(dx, -dy))                 # 0 = up, clockwise
    delta = np.abs(((ang - light_deg + 180) % 360) - 180)
    k = np.clip(1.0 - delta / spread, 0.0, 1.0) ** 2.2     # narrow falloff
    rgb = (np.array(base, np.float32)[None, None, :]
           + (np.array(hi, np.float32) - np.array(base, np.float32))[None, None, :]
           * k[..., None])
    a = np.where((r >= inner) & (r <= outer), 255, 0).astype(np.float32)
    return Image.fromarray(
        np.dstack([np.clip(rgb, 0, 255), a[..., None]]).astype(np.uint8), "RGBA")


def render_frame(norm, variant="silver", frame=FRAME, ss=SS):
    """One knob frame, matched to the measured construction."""
    v = VARIANTS[variant]
    S = frame * ss
    c = S / 2.0
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # --- drop shadow: alpha falling from the body edge outwards -------------
    yy, xx = np.mgrid[0:S, 0:S]
    r = np.hypot(yy - c, xx - c)
    t = np.clip((r - R_SKIRT * S) / max((R_SHADOW - R_SKIRT) * S, 1e-6), 0, 1)
    a = np.where(r <= R_SKIRT * S, 190, (95 * (1 - t) ** 0.75)).astype(np.uint8)
    sh = Image.fromarray(np.dstack([np.zeros((S, S, 3), np.uint8), a[..., None]]),
                         "RGBA").filter(ImageFilter.GaussianBlur(ss * 3.0))
    img = Image.alpha_composite(img, sh)

    # --- skirt: dark body with a narrow specular highlight ------------------
    skirt = _angular_shade(S, c, R_GROOVE_IN * S, R_SKIRT * S,
                           v["skirt_lo"], v["skirt_hi"])
    skirt = skirt.filter(ImageFilter.GaussianBlur(ss * 0.5))
    img = Image.alpha_composite(img, skirt)

    # --- groove: dark recess separating face from skirt ---------------------
    groove = _radial(S, c, R_GROOVE_IN * S, R_RIM_IN * S,
                     v["groove"], v["groove"])
    img = Image.alpha_composite(img, groove)

    # --- rim: lit ring at the top of the groove wall ------------------------
    rim = _angular_shade(S, c, R_RIM_IN * S, R_RIM_OUT * S,
                         v["skirt_lo"], v["rim"], spread=150.0)
    img = Image.alpha_composite(img, rim.filter(ImageFilter.GaussianBlur(ss * 0.4)))

    # --- face: flat bright disc, very slight radial falloff -----------------
    face = _radial(S, c, 0, R_FACE * S, v["face_c"], v["face_e"])
    face = face.filter(ImageFilter.GaussianBlur(ss * 0.35))
    img = Image.alpha_composite(img, face)

    # --- pointer: the only moving element -----------------------------------
    th = math.radians(ANGLE_MIN + (ANGLE_MAX - ANGLE_MIN) * norm)
    ptr = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(ptr).line(
        [c + R_PTR_IN * S * math.sin(th), c - R_PTR_IN * S * math.cos(th),
         c + R_PTR_OUT * S * math.sin(th), c - R_PTR_OUT * S * math.cos(th)],
        fill=v["pointer"] + (255,), width=max(1, int(W_PTR * S)))
    # Clip the pointer to the face so it cannot spill into the groove.
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse(
        [c - R_FACE * S, c - R_FACE * S, c + R_FACE * S, c + R_FACE * S], fill=255)
    ptr.putalpha(Image.composite(ptr.getchannel("A"), Image.new("L", (S, S), 0), mask))
    img = Image.alpha_composite(img, ptr)

    img = img.resize((frame, frame), Image.LANCZOS)

    # --- surface grain, applied at final resolution -------------------------
    # Doing this before the downsample would average most of it away.
    g = _grain_field(frame)
    yy, xx = np.mgrid[0:frame, 0:frame]
    rr = np.hypot(yy - frame / 2.0, xx - frame / 2.0)
    amp = np.where(rr < R_FACE * frame, GRAIN_FACE,
                   np.where((rr > R_RIM_IN * frame) & (rr < R_SKIRT * frame),
                            GRAIN_SKIRT, 0.0))
    arr = np.array(img, dtype=np.float32)
    arr[..., :3] = np.clip(arr[..., :3] + (g * amp)[..., None], 0, 255)
    return Image.fromarray(arr.astype(np.uint8), "RGBA")


# =============================================================================
def render_sheet(variant, frames=FRAMES, frame=FRAME, cols=COLS):
    """Row-major sheet, identical layout to the reference (16 x 8 at 210 px)."""
    rows = (frames + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * frame, rows * frame), (0, 0, 0, 0))
    for i in range(frames):
        f = render_frame(i / (frames - 1.0), variant, frame=frame)
        sheet.paste(f, ((i % cols) * frame, (i // cols) * frame))
    return sheet


def write_rcss(path, name, frames=FRAMES, frame=FRAME, cols=COLS, src=None):
    """Emit the @spritesheet block RmlUi needs, in the reference's own format.

    The knob element requires `frames` and `spriteprefix`; the prefix is
    concatenated with a three digit number to address each sprite.
    """
    lines = [f"@spritesheet {name}", "{"]
    for i in range(frames):
        x, y = (i % cols) * frame, (i // cols) * frame
        lines.append(f"\t{name}_{i:03d}: {x}px {y}px {frame}px {frame}px;")
    lines.append(f"\tsrc: {src or name + '.png'};")
    lines.append("}")
    lines.append("")
    lines.append(f".{name}")
    lines.append("{")
    lines.append(f"\tframes: {frames};")
    lines.append(f"\tspriteprefix: {name}_;")
    lines.append("}")
    open(path, "w").write("\n".join(lines) + "\n")


def render_contact():
    """Preview at full sprite size and at realistic panel size, side by side."""
    positions = [0.0, 0.25, 0.5, 0.75, 1.0]
    small = 44                       # realistic on-panel size
    pad = 12
    w = pad + len(positions) * (FRAME + pad)
    h = 30 + FRAME + 30 + small + 30
    sheet = Image.new("RGB", (w, h), (23, 25, 30))
    d = ImageDraw.Draw(sheet)

    d.text((pad, 8), "SILVER  -  sprite size 210px", fill=(200, 200, 200))
    for i, p in enumerate(positions):
        f = render_frame(p, "silver")
        sheet.paste(f, (pad + i * (FRAME + pad), 26), f)

    y = 30 + FRAME + 10
    d.text((pad, y), "SILVER and DARK at 44px  -  actual panel size",
           fill=(200, 200, 200))
    for i, p in enumerate(positions):
        f = render_frame(p, "silver").resize((small, small), Image.LANCZOS)
        sheet.paste(f, (pad + i * (small + 8), y + 18), f)
    for i, p in enumerate(positions):
        f = render_frame(p, "dark").resize((small, small), Image.LANCZOS)
        sheet.paste(f, (pad + 5 * (small + 8) + 30 + i * (small + 8), y + 18), f)

    return sheet


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="assets")
    ap.add_argument("--sheets", action="store_true",
                    help="also render the full 128-frame sheets (slow)")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    render_contact().save(os.path.join(args.out, "knob_tus_contact.png"))
    print("contact sheet written")

    if args.sheets:
        for v in VARIANTS:
            name = f"knob_{v}"
            render_sheet(v).save(os.path.join(args.out, name + ".png"))
            write_rcss(os.path.join(args.out, name + ".rcss"), name,
                       src=name + ".png")
            print("sheet + rcss:", name)


if __name__ == "__main__":
    main()
