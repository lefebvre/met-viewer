# ARL fuzzing seed corpus

Seeds for `arl_fuzz`. libFuzzer mutates these, so the corpus only has to cover the
*shapes* of a valid file — one well-formed record sequence is enough to teach the
mutator where the labels, the INDX header and the packed section are.

`small_latlon.arl` is the checked-in reader fixture (20x10 regular lat/lon: an
INDX record and two data records), copied rather than symlinked so a fuzzing run
cannot write back into `tests/fixtures/`. `tools/make_fixtures.sh` refreshes the
copy when it regenerates the fixture.

The other seeds are written by `tools/make_arl_fuzz_seeds.py` and carry what
the mutator will not invent on its own:

- `lambert_hybrid.arl` — a Lambert grid, hybrid levels (K_FLAG 4) with a
  pressure field, and two time periods 15 minutes apart in one hour.
- `wide_1001x2.arl` — 1001 columns, so the label's grid-ID character carries
  the thousand that the I3 header field cannot.

Add a seed here whenever a new ARL variant is supported. `Arl.EveryFuzzSeedOpensAndDecodes`
checks that every `.arl` seed in this directory is a file the reader accepts, since a
seed it refuses only teaches the mutator what an error looks like.

Crashers found by a run belong in a `crashes/` directory next to this one, with the
input committed alongside the regression test that covers it.
