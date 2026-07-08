#!/usr/bin/env python3
"""Assemble PNG frames into an animated GIF.

Usage: frames_to_gif.py OUT.gif DELAY_MS SCALE frame1.png frame2.png ...

Requires Pillow (`pip install --user Pillow`). Used by tools/gen_screenshots.sh to
build the time-animation GIF in docs/getting-started.md.
"""
import sys

from PIL import Image


def main() -> int:
    out, delay_ms, scale = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
    paths = sys.argv[4:]
    if not paths:
        print("no frames given", file=sys.stderr)
        return 1

    frames = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if scale != 1.0:
            im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
        frames.append(im)

    frames[0].save(out, save_all=True, append_images=frames[1:],
                   duration=delay_ms, loop=0, optimize=True)
    print(f"wrote {out} ({len(frames)} frames)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
