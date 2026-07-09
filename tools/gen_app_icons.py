#!/usr/bin/env python3
"""Derive the Windows .ico (and optionally fill in smaller PNG sizes) from the
largest available app-icon PNG.

The app icon lives under resources/icons/png/app/ as met-viewer_<size>.png. The
Windows installer needs a multi-resolution .ico; this builds it from the biggest
PNG present. Pass --fill to also (re)generate the standard smaller sizes by
downscaling that master. Re-run whenever the master art changes.

Usage:
  python3 tools/gen_app_icons.py            # regenerate met-viewer.ico
  python3 tools/gen_app_icons.py --fill      # also downscale the standard PNG set
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
# .ico's format maximum is 256; embed every standard size up to that.
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def available():
    """Map of size -> path for every met-viewer_<size>.png present."""
    out = {}
    for p in glob.glob(os.path.join(APP_DIR, "met-viewer_*.png")):
        m = re.search(r"met-viewer_(\d+)\.png$", os.path.basename(p))
        if m:
            out[int(m.group(1))] = p
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fill", action="store_true",
                    help="also downscale the master into the standard PNG sizes")
    args = ap.parse_args()

    sizes = available()
    if not sizes:
        raise SystemExit(f"no met-viewer_<size>.png found in {os.path.normpath(APP_DIR)}")
    master_size = max(sizes)
    master = Image.open(sizes[master_size]).convert("RGBA")
    print(f"master: met-viewer_{master_size}.png")

    if args.fill:
        for s in STANDARD_SIZES:
            if s <= master_size:
                master.resize((s, s), Image.LANCZOS).save(
                    os.path.join(APP_DIR, f"met-viewer_{s}.png"))

    master.save(
        os.path.join(ICONS_DIR, "met-viewer.ico"),
        format="ICO",
        sizes=[(s, s) for s in ICO_SIZES if s <= master_size],
    )
    print(f"wrote {os.path.normpath(os.path.join(ICONS_DIR, 'met-viewer.ico'))}")


if __name__ == "__main__":
    main()
