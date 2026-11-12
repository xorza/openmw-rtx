# The test binaries at `-Og` against the build's own `-O2` — 2026-11-11

A desk with a 24-core Intel laptop CPU, Linux, GCC 16.2, the `full-linux` preset against the static vcpkg set.
`components-tests`, `openmw-tests` and `openmw-cs-tests`: every object of theirs removed and rebuilt with
ccache off, `-j32`, the libraries they link already built, so the time is their own objects and the three
links. Then the three run once, the GPU suites filtered out. Two rounds, each side once a round.

| | `-O2` | `-Og` |
|---|---|---|
| build, round 1 | 89.7 s | 63.1 s |
| build, round 2 | 91.2 s | 62.9 s |
| run, round 1 | 8.6 s | 9.4 s |
| run, round 2 | 8.5 s | 9.3 s |

`-Og` builds the test objects 30% faster and runs them 10% slower: 27 s saved a rebuild against 0.9 s a run.
MSVC was not measured.
