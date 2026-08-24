# ARL fuzzing seed corpus

Seeds for `arl_fuzz`. libFuzzer mutates these, so the corpus only has to cover the
*shapes* of a valid file — one well-formed record sequence is enough to teach the
mutator where the labels, the INDX header and the packed section are.

`small_latlon.arl` is the checked-in reader fixture (20x10 regular lat/lon, two
records), copied rather than symlinked so a fuzzing run cannot write back into
`tests/fixtures/`.

Add a seed here whenever a new ARL variant is supported (a projected grid, a
multi-level file, a grid ID above 999) — the mutator will not invent a grid-ID
thousands character on its own.

Crashers found by a run belong in a `crashes/` directory next to this one, with the
input committed alongside the regression test that covers it.
