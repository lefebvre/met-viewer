#!/usr/bin/env python3
"""Convert a Natural Earth line GeoJSON into met-viewer's compact binary format.

Format (little-endian):
    magic   : 4 bytes  "MVCL"
    version : uint32    = 1
    nLines  : uint32
    per line:
        nPoints : uint32
        points  : nPoints * (float32 lon, float32 lat)

Usage:
    python3 tools/ne_convert.py in.geojson out.bin
"""
import json
import struct
import sys


def emit_line(out, coords):
    out.write(struct.pack("<I", len(coords)))
    for lon, lat in coords:
        out.write(struct.pack("<ff", float(lon), float(lat)))


def main():
    if len(sys.argv) != 3:
        print("usage: ne_convert.py in.geojson out.bin", file=sys.stderr)
        return 2
    gj = json.load(open(sys.argv[1]))

    lines = []
    for feat in gj.get("features", []):
        geom = feat.get("geometry") or {}
        t = geom.get("type")
        if t == "LineString":
            lines.append(geom["coordinates"])
        elif t == "MultiLineString":
            lines.extend(geom["coordinates"])
        elif t == "Polygon":
            lines.extend(geom["coordinates"])
        elif t == "MultiPolygon":
            for poly in geom["coordinates"]:
                lines.extend(poly)

    with open(sys.argv[2], "wb") as out:
        out.write(b"MVCL")
        out.write(struct.pack("<II", 1, len(lines)))
        for coords in lines:
            emit_line(out, coords)

    npts = sum(len(c) for c in lines)
    print(f"wrote {sys.argv[2]}: {len(lines)} lines, {npts} points")


if __name__ == "__main__":
    sys.exit(main())
