#!/usr/bin/env python3
"""Synthesizes the CRT screen texture as an emissive scan-line trace. Stdlib only.

Generated rather than sourced, for the same three reasons as the machine-room score
(Scripts/MakeAmbience.py): there is no licence question about shipping it, it can be tuned to
this specific set, and a computed image can be made to satisfy a constraint -- here, that it
still reads as a *powered instrument* after being multiplied by 6 into an emissive channel.

    python Scripts/MakeCRTTexture.py --out Deployment/generated/T_CRT_Vector.png

Why this exists at all: a flat-colour screen reads as painted plastic. The set is lit entirely
by explicit lights (GI is OFF on this build), so a screen that is merely bright-coloured sits
DIMMER than the cabinet beside it and looks switched off. What sells a CRT is the raster --
alternating bright and dark lines, a trace with a glow around it, and a black that is not quite
black because the phosphor never fully rests.

Four layers, drawn into a float intensity buffer and coloured at the end:

  RASTER    - every other row darkened. This is the whole illusion; without it the screen is a
              slide, not a display.
  TRACE     - a scope waveform across the full width, drawn with sub-pixel coverage so the
              stroke stays continuous where the curve is steep. A second, dimmer copy trails it
              as phosphor persistence, which is what makes the trace look like it is MOVING in
              a still frame.
  GRATICULE - the instrument's own etched grid, dim and dotted. Reads as glass, not as content.
  READOUT   - a row of block glyphs along the top. Deliberately unreadable: at 34cm wide on a
              screen the camera passes at 2m, legible text would be a distraction and a
              localisation problem, while the RHYTHM of characters is what the eye identifies.

Colour is CI mint (#58ebbf), which sits almost exactly where a period P1/P31 phosphor does --
so the brand accent and the era cue are the same colour with no compromise (Docs/SCENE-AUTHORING.md).

The output is 512x384. 4:3 matches the screen planes author_diorama.py spawns (scaled
0.34 x 0.26), so the raster lines land square instead of stretched, and both dimensions are
powers-of-two-friendly for the importer.
"""
import argparse, math, random, struct, os, zlib

# CI mint #58ebbf, which is also period CRT phosphor. sRGB bytes, used directly: this texture is
# imported as an sRGB base colour and multiplied into emissive, so it is authored in display
# space, NOT linear. Converting here would double-apply the transform and yield a pale mint.
PHOSPHOR = (0x58, 0xEB, 0xBF)


def _blur(buf, w, h, radius, passes=3):
    """Separable box blur, repeated -- three passes of a box approximate a gaussian closely
    enough for a glow, and cost a fraction of a real kernel in pure Python."""
    for _ in range(passes):
        for _axis in (0, 1):
            out = [0.0] * (w * h)
            if _axis == 0:                      # horizontal
                for y in range(h):
                    row = y * w
                    acc = 0.0
                    for x in range(-radius, radius + 1):
                        acc += buf[row + min(w - 1, max(0, x))]
                    inv = 1.0 / (2 * radius + 1)
                    for x in range(w):
                        out[row + x] = acc * inv
                        acc -= buf[row + min(w - 1, max(0, x - radius))]
                        acc += buf[row + min(w - 1, max(0, x + radius + 1))]
            else:                               # vertical
                for x in range(w):
                    acc = 0.0
                    for y in range(-radius, radius + 1):
                        acc += buf[min(h - 1, max(0, y)) * w + x]
                    inv = 1.0 / (2 * radius + 1)
                    for y in range(h):
                        out[y * w + x] = acc * inv
                        acc -= buf[min(h - 1, max(0, y - radius)) * w + x]
                        acc += buf[min(h - 1, max(0, y + radius + 1)) * w + x]
            buf = out
    return buf


def _stroke(buf, w, h, x, y, amp, half=1.15):
    """Deposit one sample of the trace at (x, y) with a soft vertical falloff.

    The trace is drawn column by column with SUB-PIXEL steps rather than one sample per pixel:
    where the waveform is steep a per-pixel walk skips rows and the line breaks into dashes,
    which reads as a corrupted signal instead of a scope trace.
    """
    xi = int(x)
    if xi < 0 or xi >= w:
        return
    lo, hi = int(math.floor(y - half)), int(math.ceil(y + half))
    for yi in range(lo, hi + 1):
        if 0 <= yi < h:
            d = abs(yi + 0.5 - y) / half
            if d < 1.0:
                v = amp * (1.0 - d * d)          # smooth, and exactly 0 at the edge
                if v > buf[xi + yi * w]:
                    buf[xi + yi * w] = v


def render(w=512, h=384, seed=11):
    rng = random.Random(seed)
    core = [0.0] * (w * h)      # the trace itself -- sharp, bright
    dim = [0.0] * (w * h)       # graticule + readout -- structure, not signal

    # --- graticule: the instrument's etched grid ----------------------------------------------
    # Dotted rather than solid. A solid grid at this size competes with the trace for attention
    # and, once multiplied by 6 into emissive, turns the screen into a lit window frame.
    for gx in range(0, w, w // 8):
        for y in range(h):
            if y % 4 < 2:
                dim[gx + y * w] = max(dim[gx + y * w], 0.13)
    for gy in range(0, h, h // 6):
        for x in range(w):
            if x % 4 < 2:
                dim[x + gy * w] = max(dim[x + gy * w], 0.13)
    # Centre cross, slightly hotter -- every scope has one and the eye looks for it.
    for x in range(w):
        dim[x + (h // 2) * w] = max(dim[x + (h // 2) * w], 0.20)
    for y in range(h):
        dim[(w // 2) + y * w] = max(dim[(w // 2) + y * w], 0.20)

    # --- readout: a status row of block glyphs -------------------------------------------------
    # Unreadable on purpose (see the module docstring). Widths vary so the row has the cadence of
    # words; a run of identical blocks reads as a progress bar.
    gx = 14
    while gx < w - 40:
        gw = rng.choice((5, 5, 7, 9, 11, 14))
        for x in range(gx, min(w - 4, gx + gw)):
            for y in range(16, 24):
                dim[x + y * w] = max(dim[x + y * w], 0.55)
        gx += gw + rng.choice((4, 4, 6, 12))

    # --- trace: the scope waveform -------------------------------------------------------------
    # A sum of cycle-related partials rather than one sine: a single sine reads as a test pattern,
    # while a few incommensurate components read as a machine displaying something it measured.
    cy = h * 0.58
    def wave(u, phase=0.0):
        # u in 0..1 across the screen. The envelope swells toward the middle so the trace has a
        # subject rather than marching edge to edge at constant height.
        env = 0.35 + 0.65 * math.sin(math.pi * u) ** 0.7
        s = (1.00 * math.sin(math.tau * 3.0 * u + phase)
             + 0.42 * math.sin(math.tau * 7.0 * u + 1.7 + phase)
             + 0.18 * math.sin(math.tau * 13.0 * u + 0.4 + phase)
             + 0.09 * math.sin(math.tau * 23.0 * u + 2.9 + phase))
        return cy - env * (h * 0.20) * s / 1.69

    steps = w * 6           # 6 sub-pixel samples per column: no gaps on the steep flanks
    for i in range(steps + 1):
        u = i / steps
        x = u * (w - 1)
        # Persistence FIRST so the live trace overwrites it where they cross -- a ghost drawn on
        # top of the beam reads as a double exposure rather than as decay.
        _stroke(core, w, h, x, wave(u, phase=0.55), 0.30, half=1.6)
        _stroke(core, w, h, x, wave(u), 1.0)

    # Baseline ticks along the bottom: a second, static element that gives the trace something to
    # be measured against, and fills a corner that is otherwise dead.
    for k in range(0, 33):
        x = 8 + k * ((w - 16) / 32.0)
        for y in range(h - 26, h - 26 + (10 if k % 4 == 0 else 5)):
            _stroke(core, w, h, x, y + 0.5, 0.45, half=1.0)

    # --- bloom ---------------------------------------------------------------------------------
    # A CRT's beam scatters in the phosphor and again in the glass. Without this the trace is a
    # vector-art hairline; with it the screen has a light source in it, which is the entire reason
    # the material is emissive.
    glow = _blur(list(core), w, h, radius=3, passes=3)
    wide = _blur(list(core), w, h, radius=11, passes=2)

    # --- compose -------------------------------------------------------------------------------
    rows = []
    for y in range(h):
        row = bytearray()
        # Raster. Every other line dark -- and the dark line is not black, because a real shadow
        # mask leaks. 0.34 keeps the lines readable at the distance the camera actually passes.
        scan = 1.0 if (y % 2 == 0) else 0.34
        for x in range(w):
            i = x + y * w
            v = core[i] + 0.85 * glow[i] + 0.55 * wide[i] + dim[i] * 0.9
            v *= scan
            # Resting phosphor: even an idle CRT emits. 0.028 is the difference between a screen
            # that is off and one that is showing nothing.
            v += 0.028
            # Vignette. The tube is curved and the corners of a shadow mask are always down.
            dx = (x / (w - 1) - 0.5) * 2.0
            dy = (y / (h - 1) - 0.5) * 2.0
            v *= 1.0 - 0.34 * (dx * dx + dy * dy) ** 1.3
            v = 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)
            for c in PHOSPHOR:
                row.append(int(c * v + 0.5))
        rows.append(bytes(row))
    return rows


def write_png(path, w, h, rows):
    """Minimal 8-bit RGB PNG. zlib and struct are all it takes, and adding Pillow to a repo that
    otherwise needs no Python packages for a build would be a real cost for no gain."""
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))
    raw = b"".join(b"\x00" + r for r in rows)     # filter type 0 (None) on every scanline
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    return len(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=512)
    ap.add_argument("--height", type=int, default=384)
    ap.add_argument("--seed", type=int, default=11)
    ap.add_argument("--out", default="Deployment/generated/T_CRT_Vector.png")
    args = ap.parse_args()

    rows = render(args.width, args.height, args.seed)
    size = write_png(args.out, args.width, args.height, rows)

    # Report the two numbers that decide whether this reads as powered: the mean (the screen must
    # not average out to a lit panel) and the peak (the trace must actually reach the top).
    tot = peak = 0
    for r in rows:
        for i in range(0, len(r), 3):
            g = r[i + 1]
            tot += g
            peak = max(peak, g)
    n = args.width * args.height
    print("%s  %dx%d  %.1f KB" % (args.out, args.width, args.height, size / 1024.0))
    print("  green channel: mean %.1f/255  peak %d/255" % (tot / n, peak))


if __name__ == "__main__":
    main()
