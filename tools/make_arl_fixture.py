#!/usr/bin/env python3
"""Write a tiny synthetic NOAA ARL packed file for reader round-trip tests.

Follows the exact ARL layout (validated against real NOAA data in the spike):
50-byte record labels, an INDX header defining a lat/lon grid + level inventory,
and 1-byte scaled running differences. The encoder mirrors the decoder so that
decoded values reproduce the packed values within the 1-byte precision.

Grid: 8x6 regular lat/lon, sync point (1,1) at (30N, 250E), 2-degree spacing.
Field: value(i,j) = 273 + 0.5*i + 0.3*j, written as surface T02M and 1000 hPa
TEMP for a single time (1981-11-01 00Z).
"""
import struct
import sys

# NX*NY must exceed the INDX header length (ARL stores the header in one record's
# data section). Real files are large; keep the test grid big enough to fit it.
NX, NY = 20, 10
SYNC_LAT, SYNC_LON = 30.0, 250.0
DLAT = DLON = 2.0
NEXP = 3  # scale = 2^(7-3) = 16 -> precision 1/16
IY, IM, ID, IH = 81, 11, 1, 0
LEVELS = [(0.0, ["T02M"]), (1000.0, ["TEMP"])]  # surface + one pressure level


def e14(v):
    return ("%14.7E" % v)[:14].rjust(14)


def label(kvar, ll, nexp, var1):
    s = "%2d%2d%2d%2d%2d%2d%2d" % (IY, IM, ID, IH, 0, ll, 99)
    s += "%-4s" % kvar
    s += "%4d" % nexp
    s += e14(0.0)  # prec
    s += e14(var1)
    assert len(s) == 50, len(s)
    return s.encode("ascii")


def indx_header():
    grid = [90.0, SYNC_LON, DLAT, DLON, 0.0, 0.0, 0.0, 1.0, 1.0, SYNC_LAT, SYNC_LON, 0.0]
    h = "%-4s%3d%2d" % ("TEST", 0, 0)
    h += "".join("%7.2f" % g for g in grid)
    nz = len(LEVELS)
    h += "%3d%3d%3d" % (NX, NY, nz)
    h += "%2d%4d" % (2, 0)  # kflag, lenh (lenh unused by our reader)
    for height, names in LEVELS:
        h += "%6.1f%2d" % (height, len(names))
        for nm in names:
            h += "%-4s%3d " % (nm, 99)  # name, checksum, 1 space
    return h.encode("ascii")


def pack(values, var1):
    scale = 2.0 ** (7 - NEXP)
    out = bytearray(NX * NY)
    rold = var1
    k = 0
    for j in range(NY):
        rowsav = None
        for i in range(NX):
            diff = values[j][i] - rold
            byte = int(round(diff * scale)) + 127
            byte = max(0, min(255, byte))
            decoded = (byte - 127) / scale + rold
            if i == 0:
                rowsav = decoded
            rold = decoded
            out[k] = byte
            k += 1
        rold = rowsav
    return bytes(out)


def field():
    return [[273.0 + 0.5 * i + 0.3 * j for i in range(NX)] for j in range(NY)]


def main():
    if len(sys.argv) != 2:
        print("usage: make_arl_fixture.py out.arl", file=sys.stderr)
        return 2
    recdata = NX * NY

    with open(sys.argv[1], "wb") as f:
        # INDX record: label + header padded to NX*NY.
        f.write(label("INDX", 0, 0, 0.0))
        hdr = indx_header()
        assert len(hdr) <= recdata, f"header {len(hdr)} > record data {recdata}; grow NX*NY"
        f.write(hdr + b" " * (recdata - len(hdr)))

        vals = field()
        var1 = vals[0][0]
        for ll, kvar in [(0, "T02M"), (1, "TEMP")]:
            f.write(label(kvar, ll, NEXP, var1))
            f.write(pack(vals, var1))

    print(f"wrote {sys.argv[1]}: {NX}x{NY}, 2 records")


if __name__ == "__main__":
    sys.exit(main())
