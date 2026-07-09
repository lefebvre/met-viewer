#!/usr/bin/env python3
"""Derive the app-icon PNG sets and the Windows .ico from their masters.

The app icon lives under resources/icons/png/app/:
  - dark  variant: met-viewer_<size>.png        (used as the .exe/.desktop icon)
  - light variant: met-viewer_<size>-light.png  (used as the window icon in the
                                                 app's dark theme)

This regenerates the light PNG set (16..512) by downscaling its largest master
and prunes any oversized master (>512) so the 1024 source doesn't live in the
repo. It also (re)builds met-viewer.ico from the largest dark PNG. Pass --fill
to additionally refill the dark PNG set from its own master.

Usage:
  python3 tools/gen_app_icons.py            # light set + .ico
  python3 tools/gen_app_icons.py --fill      # also refill the dark PNG set
Requires: Pillow (PIL).
"""
import argparse
import glob
import os
import re

from PIL import Image

APP_DIR = os.path.join(os.path.dirname(__file__), "..", "resources", "icons", "png", "app")
ICONS_DIR = os.path.join(os.path.dirname(__file__), "..", "resources", "icons")

STANDARD_SIZES = [16, 24, 32, 48, 64, 128, 256, 512]
MAX_COMMITTED = 512  # no master larger than this stays in the repo
# .ico's format maximum is 256; embed every standard size up to that.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def available(suffix: str):
    """Map size -> path for every met-viewer_<size><suffix>.png present."""
    out = {}
    for p in glob.glob(os.path.join(APP_DIR, f"met-viewer_*{suffix}.png")):
        m = re.match(rf"met-viewer_(\d+){re.escape(suffix)}\.png$", os.path.basename(p))
        if m:
            out[int(m.group(1))] = p
    return out


def center_square(img: Image.Image) -> Image.Image:
    """Center-crop to a square (keep the centered icon, drop side margins on a
    wider master). Avoids the warp a stretch-to-square would cause."""
    w, h = img.size
    if w == h:
        return img
    side = min(w, h)
    left, top = (w - side) // 2, (h - side) // 2
    return img.crop((left, top, left + side, top + side))


def alpha_knockout(img: Image.Image, lo: float = 240.0, hi: float = 248.0) -> Image.Image:
    """Make the near-white fill transparent, keeping the (darker) line art. Pixel
    alpha ramps from unchanged at luminance <= lo to fully transparent at >= hi.
    Used for the light app-icon variant so it reads as line art on dark
    backgrounds instead of a solid white box."""
    out = img.copy()
    px = out.load()
    w, h = out.size
    for x in range(w):
        for y in range(h):
            r, g, b, a = px[x, y]
            if not a:
                continue
            lum = (r * 30 + g * 59 + b * 11) / 100.0
            if lum >= hi:
                px[x, y] = (r, g, b, 0)
            elif lum > lo:
                px[x, y] = (r, g, b, int(a * (hi - lum) / (hi - lo)))
    return out


def fill_set(suffix: str, process=None):
    """Center-crop the largest master to a square, optionally post-process it,
    then downscale into the standard sizes (<= its label and <= MAX_COMMITTED)
    and delete any oversized master. Returns the sizes written."""
    sizes = available(suffix)
    if not sizes:
        return []
    master_size = max(sizes)  # keyed by the filename label (see available())
    master = center_square(Image.open(sizes[master_size]).convert("RGBA"))
    if process:
        master = process(master)  # once, at full resolution, for clean edges
    print(f"master: met-viewer_{master_size}{suffix}.png -> {master.size[0]}px square")
    written = [s for s in STANDARD_SIZES if s <= master_size and s <= MAX_COMMITTED]
    for s in written:
        master.resize((s, s), Image.LANCZOS).save(
            os.path.join(APP_DIR, f"met-viewer_{s}{suffix}.png"))
    for sz, path in sizes.items():
        if sz > MAX_COMMITTED:
            os.remove(path)
            print(f"pruned oversized master: {os.path.basename(path)}")
    return written


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fill", action="store_true",
                    help="also refill the dark PNG set from its master")
    args = ap.parse_args()

    # Light variant: crop to square, knock the near-white fill out to alpha, and
    # (re)derive 16..512; prune the oversized master.
    fill_set("-light", process=alpha_knockout)

    if args.fill:
        fill_set("")

    # Windows .ico from the largest dark master.
    dark = available("")
    if dark:
        master = Image.open(dark[max(dark)]).convert("RGBA")
        master.save(os.path.join(ICONS_DIR, "met-viewer.ico"), format="ICO",
                    sizes=[(s, s) for s in ICO_SIZES if s <= max(dark)])
        print(f"wrote {os.path.normpath(os.path.join(ICONS_DIR, 'met-viewer.ico'))}")


if __name__ == "__main__":
    main()
