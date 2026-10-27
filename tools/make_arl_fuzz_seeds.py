#!/usr/bin/env python3
"""Write the ARL fuzzing seeds the reader fixture cannot stand in for.

libFuzzer mutates bytes, so it will not invent a grid-ID thousands character, a
projected grid or a second time period on its own; each needs a seed that
already has it. These are synthetic, in the same layout as
tools/make_arl_fixture.py, and kept small:

  lambert_hybrid.arl  Lambert grid (TANG = REF = 38.5), K_FLAG 4 (hybrid levels),
                      two time periods 15 minutes apart within one hour.
  wide_1001x2.arl     1001 columns, so the I3 header field holds 001 and the
                      label's grid-ID character 'A' carries the thousand.

Usage:  tools/make_arl_fuzz_seeds.py tests/fuzz/corpus
"""
import os
import sys

NEXP = 3  # scale 2^(7-3) = 16
IY, IM, ID, IH = 81, 11, 1, 0


def e14(v):
    return ("%14.7E" % v)[:14].rjust(14)


def label(kvar, ll, var1, grid_id="99", nexp=NEXP, ih=IH):
    s = "%2d%2d%2d%2d%2d%2d" % (IY, IM, ID, ih, 0, ll)
    s += grid_id
    s += "%-4s" % kvar
    s += "%4d" % nexp
    s += e14(0.0)  # prec
    s += e14(var1)
    assert len(s) == 50, len(s)
    return s.encode("ascii")


def indx_header(grid, nx, ny, levels, kflag, minutes):
    h = "%-4s%3d%2d" % ("SEED", 0, minutes)
    h += "".join("%7.2f" % g for g in grid)
    h += "%3d%3d%3d" % (nx % 1000, ny % 1000, len(levels))
    h += "%2d%4d" % (kflag, 0)
    for height, names in levels:
        h += "%6.1f%2d" % (height, len(names))
        for nm in names:
            h += "%-4s%3d " % (nm, 99)
    return h.encode("ascii")


def constant_record(nx, ny):
    # var1 followed by zero differences: every byte 127.
    return bytes([127]) * (nx * ny)


def grid_id(nx, ny):
    # The label's two grid characters carry the thousands of NX and NY when
    # either exceeds 999 ('@' + n); otherwise the usual two digits.
    if nx < 1000 and ny < 1000:
        return "99"
    return chr(64 + nx // 1000) + chr(64 + ny // 1000)


def write(path, nx, ny, grid, levels, kflag, periods):
    gid = grid_id(nx, ny)
    with open(path, "wb") as f:
        for minutes, records in periods:
            f.write(label("INDX", 0, 0.0, gid, nexp=0))
            hdr = indx_header(grid, nx, ny, levels, kflag, minutes)
            assert len(hdr) <= nx * ny, f"header {len(hdr)} > record data {nx * ny}"
            f.write(hdr + b" " * (nx * ny - len(hdr)))
            for ll, kvar, value in records:
                f.write(label(kvar, ll, value, gid))
                f.write(constant_record(nx, ny))


def main():
    if len(sys.argv) != 2:
        print("usage: make_arl_fuzz_seeds.py <corpus-dir>", file=sys.stderr)
        return 2
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)

    # POLE_LAT POLE_LON REF_LAT REF_LON SIZE ORIENT TANG_LAT SYNC_XP SYNC_YP SYNC_LAT SYNC_LON -
    lambert = [90.0, 262.5, 38.5, 262.5, 3.0, 0.0, 38.5, 1.0, 1.0, 21.14, 237.28, 0.0]
    hybrid_levels = [(0.0, ["T02M"]), (15.5, ["TEMP", "PRES"]), (31.0, ["TEMP", "PRES"])]
    records = [(0, "T02M", 290.0), (1, "TEMP", 285.0), (1, "PRES", 990.0),
               (2, "TEMP", 280.0), (2, "PRES", 960.0)]
    write(os.path.join(out, "lambert_hybrid.arl"), 20, 12, lambert, hybrid_levels, 4,
          [(0, records), (15, records)])

    latlon = [90.0, 0.0, 0.25, 0.25, 0.0, 0.0, 0.0, 1.0, 1.0, 20.0, 230.0, 0.0]
    write(os.path.join(out, "wide_1001x2.arl"), 1001, 2, latlon,
          [(0.0, ["T02M"]), (1000.0, ["TEMP"])], 2,
          [(0, [(0, "T02M", 290.0), (1, "TEMP", 280.0)])])

    print(f"wrote 2 seeds to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
