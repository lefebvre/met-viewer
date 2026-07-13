#!/usr/bin/env python3
"""Generate PLACEHOLDER UI glyphs for met-viewer's icon palette.

Writes resources/icons/png/{dark,light}/<token>_{16,24,32,48}.png for the 39
tokens documented in resources/icons/png/README.txt. These are simple,
category-distinct line drawings meant only as stand-ins so the UI wiring is
complete and testable; replace them with the real glyph set by dropping in PNGs
of the same names (or re-point this script at the source art).

Convention (from the README): dark/ glyphs are near-black (#1F2933) for LIGHT
toolbars; light/ glyphs are off-white (#EEF2F7) for DARK toolbars.

Usage:  python3 tools/gen_glyph_placeholders.py
Requires: Pillow (PIL).
"""
import math
import os

from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(__file__), "..", "resources", "icons", "png")
SIZES = [16, 24, 32, 48]
N = 192  # master render resolution; downscaled to each target size
DARK = (0x1F, 0x29, 0x33)
LIGHT = (0xEE, 0xF2, 0xF7)

TOKENS = [
    # Modes
    "view-pan", "mode-probe", "mode-section", "mode-skewt", "mode-tseries",
    # Data / files
    "file-open", "data-grid", "view-layers", "axis-level", "axis-time",
    # Visualize
    "render-cmap", "view-cbar", "render-contours", "view-plot2d", "view-map",
    "layer-opacity",
    # Wind
    "wind-barb", "wind-arrow", "wind-streamlines",
    # Basemap
    "base-osm", "base-imagery", "base-terrain", "base-light", "base-dark",
    "base-custom",
    # Overlays
    "overlay-coast", "overlay-borders", "overlay-graticule",
    # Animation
    "anim-play", "anim-pause", "anim-prev", "anim-next", "anim-loop", "anim-fps",
    # General
    "view-zoomin", "view-zoomout", "view-fit", "app-settings", "app-grab",
]


class Pen:
    """Normalized (0..1) drawing helpers over an N-px master canvas."""

    def __init__(self, draw, color):
        self.d = draw
        self.c = color + (255,)
        self.w = int(N * 0.075)

    def _p(self, pts):
        return [(x * N, y * N) for x, y in pts]

    def line(self, pts, w=None):
        self.d.line(self._p(pts), fill=self.c, width=w or self.w, joint="curve")

    def circle(self, cx, cy, r, fill=False):
        box = [(cx - r) * N, (cy - r) * N, (cx + r) * N, (cy + r) * N]
        if fill:
            self.d.ellipse(box, fill=self.c)
        else:
            self.d.ellipse(box, outline=self.c, width=self.w)

    def rect(self, x0, y0, x1, y1, fill=False, r=0.0):
        box = [x0 * N, y0 * N, x1 * N, y1 * N]
        if r:
            self.d.rounded_rectangle(box, radius=r * N, outline=None if fill else self.c,
                                     fill=self.c if fill else None, width=self.w)
        elif fill:
            self.d.rectangle(box, fill=self.c)
        else:
            self.d.rectangle(box, outline=self.c, width=self.w)

    def poly(self, pts):
        self.d.polygon(self._p(pts), fill=self.c)

    def dot(self, cx, cy, r=0.06):
        self.circle(cx, cy, r, fill=True)

    def arrow(self, x0, y0, x1, y1):
        self.line([(x0, y0), (x1, y1)])
        ang = math.atan2(y1 - y0, x1 - x0)
        h = 0.16
        for da in (math.radians(150), math.radians(-150)):
            self.line([(x1, y1), (x1 + h * math.cos(ang + da), y1 + h * math.sin(ang + da))])

    def wave(self, y, amp=0.09, x0=0.12, x1=0.88, phase=0.0):
        pts = []
        steps = 40
        for i in range(steps + 1):
            t = i / steps
            x = x0 + (x1 - x0) * t
            pts.append((x, y + math.sin(t * math.pi * 2 + phase) * amp))
        self.line(pts)


def draw(token, pen):
    p = pen
    if token == "view-pan":
        for a in (0, 90, 180, 270):
            r = math.radians(a)
            p.arrow(0.5, 0.5, 0.5 + 0.32 * math.cos(r), 0.5 + 0.32 * math.sin(r))
    elif token == "mode-probe":
        p.line([(0.5, 0.12), (0.5, 0.88)])
        p.line([(0.12, 0.5), (0.88, 0.5)])
        p.circle(0.5, 0.5, 0.16)
    elif token == "mode-section":
        p.rect(0.18, 0.2, 0.82, 0.8)
        p.line([(0.5, 0.2), (0.5, 0.8)])
        p.wave(0.5, amp=0.12, x0=0.18, x1=0.5)
    elif token == "mode-skewt":
        p.line([(0.22, 0.15), (0.22, 0.85), (0.85, 0.85)])
        p.line([(0.3, 0.82), (0.75, 0.2)])
    elif token == "mode-tseries":
        p.line([(0.15, 0.85), (0.85, 0.85)])
        p.line([(0.15, 0.6), (0.35, 0.35), (0.55, 0.55), (0.85, 0.2)])
    elif token == "file-open":
        p.line([(0.14, 0.28), (0.42, 0.28), (0.5, 0.38), (0.86, 0.38)])
        p.rect(0.14, 0.28, 0.86, 0.78)
    elif token == "data-grid":
        p.rect(0.18, 0.18, 0.82, 0.82)
        p.line([(0.18, 0.4), (0.82, 0.4)])
        p.line([(0.18, 0.6), (0.82, 0.6)])
        p.line([(0.4, 0.18), (0.4, 0.82)])
        p.line([(0.6, 0.18), (0.6, 0.82)])
    elif token == "view-layers":
        for dy in (0.0, 0.16, 0.32):
            p.poly([(0.5, 0.2 + dy), (0.82, 0.36 + dy), (0.5, 0.52 + dy), (0.18, 0.36 + dy)])
            break  # top sheet filled; others outline
        for dy in (0.16, 0.32):
            p.line([(0.5, 0.2 + dy), (0.82, 0.36 + dy), (0.5, 0.52 + dy), (0.18, 0.36 + dy), (0.5, 0.2 + dy)])
    elif token == "axis-level":
        p.line([(0.3, 0.12), (0.3, 0.88)])
        for y in (0.25, 0.45, 0.65):
            p.line([(0.3, y), (0.6, y)])
    elif token == "axis-time":
        p.line([(0.12, 0.7), (0.88, 0.7)])
        for x in (0.3, 0.5, 0.7):
            p.line([(x, 0.7), (x, 0.45)])
    elif token == "render-cmap":
        for i, x in enumerate((0.2, 0.4, 0.6)):
            p.rect(x, 0.25, x + 0.2, 0.75, fill=True)
    elif token == "view-cbar":
        p.rect(0.34, 0.15, 0.56, 0.85)
        for y in (0.3, 0.5, 0.7):
            p.line([(0.56, y), (0.72, y)])
    elif token == "render-contours":
        p.circle(0.5, 0.5, 0.34)
        p.circle(0.5, 0.5, 0.2)
        p.dot(0.5, 0.5, 0.05)
    elif token == "view-plot2d":
        p.rect(0.18, 0.18, 0.82, 0.82)
        p.line([(0.24, 0.7), (0.42, 0.45), (0.58, 0.6), (0.78, 0.3)])
    elif token == "view-map":
        p.circle(0.5, 0.5, 0.36)
        p.line([(0.14, 0.5), (0.86, 0.5)])
        p.circle(0.5, 0.5, 0.15)
        p.line([(0.5, 0.14), (0.5, 0.86)])
    elif token == "layer-opacity":
        p.circle(0.5, 0.5, 0.34)
        p.poly([(0.5, 0.16), (0.5, 0.84), (0.84, 0.66), (0.84, 0.34)])
    elif token == "wind-barb":
        p.line([(0.2, 0.72), (0.8, 0.32)])
        for t in (0.0, 0.18, 0.36):
            bx, by = 0.8 - 0.6 * t, 0.32 + 0.4 * t
            p.line([(bx, by), (bx - 0.02, by - 0.2)])
    elif token == "wind-arrow":
        p.arrow(0.2, 0.72, 0.8, 0.28)
    elif token == "wind-streamlines":
        p.wave(0.35, amp=0.07)
        p.wave(0.55, amp=0.07, phase=math.pi)
        p.wave(0.72, amp=0.07)
    elif token == "base-osm":
        p.poly([(0.5, 0.14), (0.72, 0.42), (0.5, 0.86), (0.28, 0.42)])
        p.circle(0.5, 0.4, 0.1, fill=False)
    elif token == "base-imagery":
        p.rect(0.16, 0.2, 0.84, 0.8)
        p.circle(0.66, 0.36, 0.08, fill=True)
        p.poly([(0.16, 0.8), (0.42, 0.5), (0.62, 0.72), (0.84, 0.44), (0.84, 0.8)])
    elif token == "base-terrain":
        p.poly([(0.12, 0.78), (0.4, 0.34), (0.62, 0.78)])
        p.poly([(0.5, 0.78), (0.72, 0.46), (0.9, 0.78)])
    elif token == "base-light":
        p.circle(0.5, 0.5, 0.18, fill=True)
        for a in range(0, 360, 45):
            r = math.radians(a)
            p.line([(0.5 + 0.28 * math.cos(r), 0.5 + 0.28 * math.sin(r)),
                    (0.5 + 0.4 * math.cos(r), 0.5 + 0.4 * math.sin(r))])
    elif token == "base-dark":
        p.circle(0.52, 0.5, 0.32, fill=True)
        # bite out a crescent by overpainting is not possible here; approximate.
        p.circle(0.66, 0.42, 0.26, fill=False)
    elif token == "base-custom":
        p.rect(0.2, 0.2, 0.8, 0.8, r=0.12)
        p.line([(0.5, 0.32), (0.5, 0.68)])
        p.line([(0.32, 0.5), (0.68, 0.5)])
    elif token == "overlay-coast":
        p.wave(0.5, amp=0.14, x0=0.14, x1=0.86)
    elif token == "overlay-borders":
        p.d.line(p._p([(0.2, 0.25), (0.8, 0.2), (0.75, 0.8), (0.25, 0.78), (0.2, 0.25)]),
                 fill=p.c, width=max(1, int(N * 0.05)))
    elif token == "overlay-graticule":
        p.rect(0.16, 0.16, 0.84, 0.84)
        for v in (0.39, 0.61):
            p.line([(v, 0.16), (v, 0.84)])
            p.line([(0.16, v), (0.84, v)])
    elif token == "anim-play":
        p.poly([(0.34, 0.24), (0.34, 0.76), (0.78, 0.5)])
    elif token == "anim-pause":
        p.rect(0.32, 0.24, 0.44, 0.76, fill=True)
        p.rect(0.56, 0.24, 0.68, 0.76, fill=True)
    elif token == "anim-prev":
        p.rect(0.28, 0.26, 0.36, 0.74, fill=True)
        p.poly([(0.72, 0.26), (0.72, 0.74), (0.42, 0.5)])
    elif token == "anim-next":
        p.rect(0.64, 0.26, 0.72, 0.74, fill=True)
        p.poly([(0.28, 0.26), (0.28, 0.74), (0.58, 0.5)])
    elif token == "anim-loop":
        p.d.arc([0.2 * N, 0.2 * N, 0.8 * N, 0.8 * N], 30, 300, fill=p.c, width=p.w)
        p.arrow(0.72, 0.28, 0.82, 0.44)
    elif token == "anim-fps":
        p.circle(0.5, 0.5, 0.34)
        p.line([(0.5, 0.5), (0.5, 0.28)])
        p.line([(0.5, 0.5), (0.66, 0.6)])
    elif token == "view-zoomin":
        p.circle(0.44, 0.44, 0.26)
        p.line([(0.62, 0.62), (0.84, 0.84)])
        p.line([(0.44, 0.32), (0.44, 0.56)])
        p.line([(0.32, 0.44), (0.56, 0.44)])
    elif token == "view-zoomout":
        p.circle(0.44, 0.44, 0.26)
        p.line([(0.62, 0.62), (0.84, 0.84)])
        p.line([(0.32, 0.44), (0.56, 0.44)])
    elif token == "view-fit":
        for cx, cy, sx, sy in [(0.2, 0.2, 1, 1), (0.8, 0.2, -1, 1),
                               (0.2, 0.8, 1, -1), (0.8, 0.8, -1, -1)]:
            p.line([(cx, cy + 0.16 * sy), (cx, cy), (cx + 0.16 * sx, cy)])
    elif token == "app-settings":
        p.circle(0.5, 0.5, 0.14)
        for a in range(0, 360, 45):
            r = math.radians(a)
            p.line([(0.5 + 0.24 * math.cos(r), 0.5 + 0.24 * math.sin(r)),
                    (0.5 + 0.4 * math.cos(r), 0.5 + 0.4 * math.sin(r))], w=int(N * 0.1))
    elif token == "app-grab":
        p.rect(0.14, 0.3, 0.86, 0.78)
        p.poly([(0.36, 0.3), (0.42, 0.22), (0.58, 0.22), (0.64, 0.3)])
        p.circle(0.5, 0.54, 0.14)
    else:
        p.rect(0.2, 0.2, 0.8, 0.8, r=0.1)  # fallback


def render(token, color):
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    draw(token, Pen(ImageDraw.Draw(img), color))
    return img


def main():
    for variant, color in (("dark", DARK), ("light", LIGHT)):
        d = os.path.join(OUT, variant)
        os.makedirs(d, exist_ok=True)
        for token in TOKENS:
            master = render(token, color)
            for s in SIZES:
                master.resize((s, s), Image.LANCZOS).save(
                    os.path.join(d, f"{token}_{s}.png"))
    print(f"Wrote {len(TOKENS)} tokens x {len(SIZES)} sizes x 2 variants to "
          f"{os.path.normpath(OUT)}/{{dark,light}}/")


if __name__ == "__main__":
    main()
