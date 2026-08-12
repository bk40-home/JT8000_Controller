#!/usr/bin/env python3
# =============================================================================
# render_panel.py - draw the arranged panel with the real sprites
# -----------------------------------------------------------------------------
# Geometry comes from pack.py, which decides block widths and heights. This
# file only places controls inside those blocks and draws them.
#
# Canvas 3840x2160 dp, cells at 90% (knob 144dp = 72px at 1920x1080). The
# preview is rendered at 0.5, which IS the display size - so what you see here
# is what the plugin looks like, not a scaled-down impression of it.
#
# Legends are painted, never drawn at runtime: knob legends above, combo
# legends to the left, fader and toggle legends below. A control's cell already
# includes its legend, so nothing can collide.
# =============================================================================

import os

from PIL import Image, ImageDraw, ImageFont

import pack

ASSETS = "/mnt/user-data/outputs/assets"
FONT_DIR = "/usr/share/fonts/truetype/crosextra/"

K = 0.90                     # 2116 of 2160 dp with the header
VIEW = 0.5                   # preview scale = actual display scale

C_BODY_HI, C_BODY_LO = (58, 62, 72), (26, 29, 36)
C_TITLE, C_GROUP, C_LABEL = (238, 240, 244), (150, 158, 172), (198, 204, 214)
C_RULE = (110, 118, 132)

LABEL_H = int(34 * K)
LABEL_W = int(150 * K)


def font(bold, px):
    f = FONT_DIR + ("Carlito-Bold.ttf" if bold else "Carlito-Regular.ttf")
    try:
        return ImageFont.truetype(f, max(7, int(px)))
    except OSError:
        return ImageFont.load_default()


def body(w, h):
    """Anodised chassis: vertical gradient, fine brushed texture, corner radius.

    The brush is one row of noise smeared down the panel - that directional
    coherence is what reads as brushed metal rather than as grain, the same
    trick used for the steel knob caps.
    """
    import numpy as np
    ys = np.linspace(0, 1, h)[:, None, None]
    rgb = (np.array(C_BODY_HI, np.float32) * (1 - ys)
           + np.array(C_BODY_LO, np.float32) * ys)
    rgb = np.repeat(rgb, w, 1)

    rng = np.random.default_rng(20250803)
    line = rng.normal(0.0, 3.2, size=(1, w))
    rgb += np.repeat(line, h, axis=0)[..., None]
    rgb += rng.normal(0.0, 1.1, size=(h, w))[..., None]

    return Image.fromarray(np.clip(rgb, 0, 255).astype("uint8"), "RGB")


def draw_chassis(img, d, S, place, hidden):
    """Panel bevels, block wells and fixings.

    Blocks get a shallow recessed well rather than a drawn box: an outline
    around every block was rejected earlier because controls legitimately sit
    close to their bounds, but a low-contrast well groups without fencing.
    """
    CW, CH = 3840, 2160
    # --- outer bevel ---------------------------------------------------------
    # Drawn as inset 1px rules, not thick strokes on the boundary: a wide stroke
    # centred on the edge loses half its width off-canvas and the visible half
    # reads as a white band.
    W, H = S(CW) - 1, S(CH) - 1
    for i, (lit, dark) in enumerate((((124, 132, 148), (10, 12, 16)),
                                     ((88, 95, 108), (26, 29, 36)),
                                     ((62, 68, 79), (40, 44, 53)))):
        d.line([i, i, W - i, i], fill=lit)                    # top
        d.line([i, i, i, H - i], fill=lit)                    # left
        d.line([i, H - i, W - i, H - i], fill=dark)           # bottom
        d.line([W - i, i, W - i, H - i], fill=dark)           # right

    # --- header well ---------------------------------------------------------
    M, H = pack.M, pack.HEADER
    d.rounded_rectangle([S(M - 14), S(M - 14), S(CW - M + 14), S(M + H - 6)],
                        S(8), fill=(34, 38, 46), outline=(20, 23, 29))
    d.line([S(M - 8), S(M - 12), S(CW - M + 8), S(M - 12)],
           fill=(96, 104, 118))

    # --- block wells ---------------------------------------------------------
    for name, (bx, by, bw, bh) in place.items():
        if name in hidden:
            continue
        x0, y0 = S(bx - 14), S(by - 10)
        x1, y1 = S(bx + bw + 14), S(by + bh + 10)
        d.rounded_rectangle([x0, y0, x1, y1], S(7), fill=(38, 42, 51))
        d.line([x0 + S(4), y0, x1 - S(4), y0], fill=(86, 94, 108))   # lit lip
        d.line([x0 + S(4), y1, x1 - S(4), y1], fill=(16, 18, 23))    # shadow
        d.line([x0, y0 + S(4), x0, y1 - S(4)], fill=(70, 77, 89))
        d.line([x1, y0 + S(4), x1, y1 - S(4)], fill=(20, 23, 29))

    # --- fixings -------------------------------------------------------------
    # Fixings sit inside the chassis, not on its edge, or they clip.
    for sx, sy in ((22, 22), (CW - 22, 22), (22, CH - 22), (CW - 22, CH - 22)):
        r = S(13)
        d.ellipse([S(sx) - r, S(sy) - r, S(sx) + r, S(sy) + r],
                  fill=(58, 63, 72), outline=(18, 20, 25))
        d.ellipse([S(sx) - r // 2, S(sy) - r // 2, S(sx) + r // 2,
                   S(sy) + r // 2], fill=(28, 31, 38))


def layout_block(b, w, k):
    """Positions for every control in a block. Mirrors pack.reflow exactly."""
    inner = w - pack.PAD * 2
    out = []
    gx, gy, rowH = 0, int(pack.TITLE * k), 0
    for gr in b["groups"]:
        gw, gh = pack.group_size(gr, inner, k)
        if gx and gx + gw > inner:
            gy += rowH + pack.GGAP
            gx, rowH = 0, 0
        out.append(dict(group=True, label=gr["label"],
                        x=pack.PAD + gx, y=gy))
        x, y, rh = 0, gy + int(pack.GROUP_H * k), 0
        for it in gr["items"]:
            cw, ch = pack.item_size(it, k)
            if gr.get("col"):
                out.append(dict(group=False, it=it, cw=cw, ch=ch,
                                x=pack.PAD + gx, y=y))
                y += ch + pack.GAP
                continue
            if x and x + cw > inner:
                y += rh + pack.GAP
                x, rh = 0, 0
            out.append(dict(group=False, it=it, cw=cw, ch=ch,
                            x=pack.PAD + gx + x, y=y))
            x += cw + pack.GAP
            rh = max(rh, ch)
        gx += gw + pack.GGAP
        rowH = max(rowH, gh)
    return out


C_SHIFT = (232, 96, 84)


def draw_header(img, d, S, k, shift, layer=None):
    """Top strip: brand, LCD, preset browser, layer buttons, shift annunciator.

    The LCD is the reason context slots and step editing work at all - it shows
    the name and value of whatever is under the pointer, so no control needs its
    own numeric readout. The shift annunciator has to live here because shift is
    latching: from anywhere on the panel you must be able to see the state.
    """
    import gen_combo
    import gen_widgets
    M, H = pack.M, pack.HEADER
    W = 3840 - M * 2
    f_brand = font(1, S(74))
    f_sub = font(0, S(22))
    f_big = font(1, S(56))
    f_val = font(0, S(30))
    f_btn = font(1, S(26))
    f_lab = font(0, S(22))

    # --- brand ---------------------------------------------------------------
    d.text((S(M), S(M + 52)), "JT-8000", font=f_brand, fill=(240, 243, 248))
    d.text((S(M + 6), S(M + 140)), "VIRTUAL ANALOG SYNTHESIZER",
           font=f_sub, fill=(140, 148, 162))

    # --- LCD: patch name and the live readout --------------------------------
    lx, lw, ly, lh = M + 520, 1420, M + 40, 170
    lcd = gen_combo.compose(S(lw)).resize((S(lw), S(lh)), Image.LANCZOS)
    img.alpha_composite(lcd, (S(lx), S(ly)))
    d.text((S(lx + 34), S(ly + 22)), "A12  SUNRISE BREACH", font=f_big,
           fill=(255, 168, 58))
    d.text((S(lx + 34), S(ly + 104)),
           "DECAY CURVE   +18" if shift else "CUTOFF   1.24 kHz",
           font=f_val, fill=(190, 108, 30))
    # Preset step arrows sit in the LCD's own right-hand well.
    for i, ch in enumerate((-1, 1)):
        ax = S(lx + lw - 108 + i * 46)
        ay = S(ly + lh / 2)
        d.polygon([(ax + (0 if ch < 0 else S(14)), ay),
                   (ax + (S(14) if ch < 0 else 0), ay - S(11)),
                   (ax + (S(14) if ch < 0 else 0), ay + S(11))],
                  fill=(190, 108, 30))

    # --- layer buttons: a row, not a dropdown --------------------------------
    # These select which layer owns the panel, so they are momentary-looking
    # buttons with a lit state, not combos. Reusing the combo sprite made them
    # read as four things you could open.
    bx, bw, bh, bg = M + 1960, 254, 82, 14
    d.text((S(bx), S(M + 26)), "LAYER", font=f_lab, fill=(140, 148, 162))
    for i, name in enumerate(("MAIN", "REVERB", "SEQ", "ARP", "SETUP")):
        x = bx + i * (bw + bg)
        on = (name == "MAIN" and layer is None) or (layer == name)
        d.rounded_rectangle([S(x), S(M + 50), S(x + bw), S(M + 50 + bh)], S(5),
                            fill=(96, 44, 12) if on else (38, 42, 50),
                            outline=(226, 140, 50) if on else (74, 80, 92))
        d.line([S(x + 6), S(M + 52), S(x + bw - 6), S(M + 52)],
               fill=(255, 255, 255, 40))
        tw = d.textlength(name, font=f_btn)
        d.text((S(x + bw / 2) - tw / 2, S(M + 50 + 26)), name, font=f_btn,
               fill=(255, 208, 140) if on else (150, 158, 172))

    # --- shift annunciator, at the far right ---------------------------------
    sx, sw2 = M + 3300, 380
    d.rounded_rectangle([S(sx), S(M + 50), S(sx + sw2), S(M + 132)], S(5),
                        fill=(122, 22, 22) if shift else (40, 26, 26),
                        outline=(226, 66, 52) if shift else (92, 58, 54))
    tw = d.textlength("SHIFT", font=f_btn)
    d.text((S(sx + sw2 / 2) - tw / 2, S(M + 76)), "SHIFT", font=f_btn,
           fill=(255, 216, 212) if shift else (126, 84, 80))
    d.text((S(sx), S(M + 26)), "SECONDARY", font=f_lab, fill=(140, 148, 162))


def render(k=K, view=VIEW, shift=False, layer=None, controls=True):
    place, used = pack.build(k)
    CW, CH = 3840, 2160
    S = lambda v: int(round(v * view))

    img = body(S(CW), S(CH)).convert("RGBA")
    d = ImageDraw.Draw(img)

    # A layer owns the layer area exclusively: the blocks that live there are
    # not drawn while it is shown.
    row3 = set(pack.ROWS[-1])
    hidden = row3 | (set(pack.LAYER_BLOCKS) - {layer}) if layer \
        else set(pack.LAYER_BLOCKS)
    f_title = font(1, S(int(46 * k)))
    f_group = font(1, S(int(26 * k)))
    f_lab = font(0, S(28 * k))       # matches F_LAB in rebuild_blocks.py
    f_val = font(1, S(27 * k))       # matches F_VAL

    draw_chassis(img, d, S, place, hidden)
    draw_header(img, d, S, k, shift, layer)

    knob = Image.open(f"{ASSETS}/knob_silver.png").convert("RGBA")
    thumb = Image.open(f"{ASSETS}/slider_default.png").convert("RGBA")
    import gen_combo
    import gen_widgets
    import gen_widgets
    tog_off = Image.open(f"{ASSETS}/toggle_off.png").convert("RGBA")
    tog_on = Image.open(f"{ASSETS}/toggle_on.png").convert("RGBA")


    def knob_at(norm, px):
        n = int(round(norm * 127))
        f = knob.crop(((n % 16) * 210, (n // 16) * 210,
                       (n % 16) * 210 + 210, (n // 16) * 210 + 210))
        return f.resize((px, px), Image.LANCZOS)

    for name, (bx, by, bw, bh) in place.items():
        if name in hidden:
            continue
        b = pack.B[name]

        # Region title, centred on a broken rule. The title strip is part of the
        # block's height, reserved before any control was placed, so it cannot
        # collide with anything.
        t = name.upper()
        tw = d.textlength(t, font=f_title)
        cx = S(bx + bw / 2)
        d.text((cx - tw / 2, S(by + 10 * k)), t, font=f_title, fill=C_TITLE)
        ry = S(by + 34 * k)
        d.line([S(bx + 12), ry, cx - tw / 2 - S(16), ry], fill=C_RULE)
        d.line([cx + tw / 2 + S(16), ry, S(bx + bw - 12), ry], fill=C_RULE)

        for o in layout_block(b, bw, k):
            x, y = bx + o["x"], by + o["y"]
            if o["group"]:
                d.text((S(x), S(y + 6 * k)), o["label"].upper(), font=f_group,
                       fill=C_GROUP)
                continue

            cell = o["it"]
            is_alt = shift and "sh" in cell
            if is_alt:
                cell = cell["sh"]
            kind, label = cell["k"], cell["t"]
            norm = 0.28 + 0.5 * ((abs(hash(label + name)) % 9) / 9.0)

            if not controls:
                continue

            if kind == "knob":
                px = S(160 * k)
                cw = o.get("cw", int(160 * k))
                mid = S(x + cw / 2)
                d.text((mid - d.textlength(label, font=f_lab) / 2, S(y)),
                       label, font=f_lab, fill=C_SHIFT if is_alt else C_LABEL)
                img.alpha_composite(knob_at(norm, px), (mid - px // 2,
                                                        S(y + 34 * k)))

            elif kind == "fader":
                th = int(284 * k)
                trk = gen_widgets.compose_track(S(th)).resize(
                    (S(26 * k), S(th)), Image.LANCZOS)
                img.alpha_composite(trk, (S(x + 22 * k), S(y)))
                # No lit column: the thumb position IS the value, matching the
                # knobs' pointer-only reading.
                tw2, th2 = S(int(118 * k)), S(int(88 * k))
                img.alpha_composite(thumb.resize((tw2, th2), Image.LANCZOS),
                                    (S(x - 12 * k),
                                     S(y) + int(S(th) * (1 - norm))))
                lw = d.textlength(label, font=f_lab)
                d.text((S(x + 34 * k) - lw / 2, S(y + th + 10 * k)),
                       label, font=f_lab, fill=C_LABEL)

            elif kind == "combo":
                cw = S(cell.get("w", 300) * k)
                ch2 = S(45 * k)
                d.text((S(x), S(y)), label, font=f_lab,
                       fill=C_SHIFT if is_alt else C_LABEL)
                sprite = gen_combo.compose(max(90, cw)).resize(
                    (max(90, cw), ch2), Image.LANCZOS)
                img.alpha_composite(sprite, (S(x), S(y + 34 * k)))
                val = cell.get("v", "")
                if val:
                    d.text((S(x) + S(15 * k), S(y + 34 * k) + S(9 * k)), val,
                           font=f_val, fill=(255, 168, 58))

            elif kind == "stepgrid":
                gw, gh = S(cell["w"] * k), S(cell["h"] * k)
                d.text((S(x), S(y)), label, font=f_lab, fill=C_LABEL)
                gy = S(y + 34 * k)
                d.rectangle([S(x), gy, S(x) + gw, gy + gh],
                            fill=(10, 12, 15), outline=(58, 62, 72))
                lanes = cell.get("lanes", 2)
                cw_step = gw / 16.0
                lh = (gh - S(8)) / lanes
                for ln in range(lanes):
                    ly = gy + S(4) + int(ln * lh)
                    for i in range(16):
                        import math
                        v = 0.2 + 0.75 * abs(math.sin(i * 0.8 + ln * 1.9))
                        bx = S(x) + int(i * cw_step) + 3
                        bw2 = int(cw_step) - 6
                        bar = int((lh - 6) * v)
                        col = ((226, 24, 24) if ln == 1 else
                               (255, 168, 58) if ln == 0 else (110, 190, 220))
                        d.rectangle([bx, ly + int(lh) - 6 - bar,
                                     bx + bw2, ly + int(lh) - 6], fill=col)
                    d.line([S(x), ly + int(lh) - 4, S(x) + gw, ly + int(lh) - 4],
                           fill=(40, 46, 56))

            else:
                # Legend above, as every other control has it. Beneath, it sat
                # tight against the row below and read as a caption.
                d.text((S(x), S(y)), label, font=f_lab, fill=C_LABEL)
                spr = (tog_on if (abs(hash(cell["t"])) % 3 == 0) else tog_off)
                c = spr.resize((S(107 * k), S(80 * k)), Image.LANCZOS)
                img.alpha_composite(c, (S(x), S(y + 34 * k)))

    return img.convert("RGB"), used


if __name__ == "__main__":
    os.makedirs(ASSETS, exist_ok=True)
    img, used = render()
    img.save(f"{ASSETS}/panel.png")
    simg, _ = render(shift=True)
    simg.save(f"{ASSETS}/panel_shift.png")
    for ln, tag in (("Reverb", "reverb"), ("Step Sequencer", "seq"),
                    ("Arpeggiator", "arp"), ("Setup", "setup")):
        limg, _ = render(layer=ln)
        limg.save(f"{ASSETS}/panel_{tag}.png")
        print("panel_%s.png" % tag)
    print("panel.png %dx%d px  (3840x2160 dp at %.2f)  content %d dp"
          % (img.width, img.height, VIEW, used))
