#!/usr/bin/env python3
"""Assemble captured Pixel Streaming frames into an animated GIF and a contact sheet.

Why this exists: a still frame cannot show that she is ALIVE. Blinking, the idle animation and
viseme motion are all temporal, and every "is it working?" question so far has been answered with
a single PNG that could equally have been a frozen render. A short loop settles it, and it is
cheap enough to produce on every deploy.

Deliberately GIF and not mp4: the capture host has Playwright but no guaranteed ffmpeg, and a GIF
needs nothing beyond Pillow, which is already required for the frame maths. Quality is not the
point -- proof of motion is.

Usage:
    python3 MakeStreamVideo.py --frames 'shots/seq_*.png' --out stream
        -> stream.gif  (animated loop)
        -> stream_sheet.png (all frames side by side, for a still comparison)
"""

import argparse
import glob
import sys

try:
    from PIL import Image
except ImportError:
    print("Pillow required: pip install Pillow", file=sys.stderr)
    sys.exit(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", default="seq_*.png", help="glob of captured frames, in order")
    ap.add_argument("--out", default="stream", help="output basename")
    ap.add_argument("--ms", type=int, default=400, help="frame duration in the gif")
    ap.add_argument("--max-width", type=int, default=420, help="downscale for a shareable size")
    ap.add_argument("--sheet-cols", type=int, default=6)
    args = ap.parse_args()

    paths = sorted(glob.glob(args.frames))
    if not paths:
        print(f"no frames matched {args.frames}", file=sys.stderr)
        return 1

    frames = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if im.width > args.max_width:
            h = int(im.height * (args.max_width / im.width))
            im = im.resize((args.max_width, h), Image.LANCZOS)
        frames.append(im)

    gif = f"{args.out}.gif"
    frames[0].save(gif, save_all=True, append_images=frames[1:], duration=args.ms, loop=0,
                   optimize=True)

    cols = min(args.sheet_cols, len(frames))
    rows = (len(frames) + cols - 1) // cols
    w, h = frames[0].size
    sheet = Image.new("RGB", (w * cols + 8 * (cols - 1), h * rows + 8 * (rows - 1)), (18, 18, 18))
    for i, im in enumerate(frames):
        sheet.paste(im, ((i % cols) * (w + 8), (i // cols) * (h + 8)))
    sheet_path = f"{args.out}_sheet.png"
    sheet.save(sheet_path)

    print(f"{len(frames)} frames  {w}x{h}  {'PORTRAIT' if h > w else 'LANDSCAPE'}")
    print(f"  -> {gif}")
    print(f"  -> {sheet_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
