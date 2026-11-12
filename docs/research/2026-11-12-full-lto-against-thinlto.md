# Full LTO against ThinLTO — 2026-11-12

A desk with a 24-core Intel laptop CPU and a high-end NVIDIA laptop GPU, Linux, the pinned Clang 23 and lld. The
release flavour with `OMW_LTO=ON`, which CMake compiles with `-flto=thin`, against the same with `-flto=full`, the
harness alone built each way. Its link was measured alone, then `bench`'s default suite ran a warm-up and the two
binaries back to back, twice each, on a hot card.

| | ThinLTO | full LTO |
|---|---|---|
| the harness's link | 33 s | 200 s |
| its peak memory, anonymous | 2.88 GB | 6.46 GB |
| its peak memory, resident | 3.77 GB | 7.11 GB |
| the binary | 324.2 MB | 322.0 MB |

Medians and the p99, in milliseconds, in the order the runs went:

| place, row | thin 1 | full 1 | thin 2 | full 2 |
|---|---|---|---|---|
| ship, frame | 6.06 / 7.24 | 6.12 / 8.09 | 6.16 / 7.18 | 6.15 / 7.18 |
| ship, update | 1.02 / 1.49 | 1.23 / 1.86 | 1.04 / 1.55 | 1.02 / 1.44 |
| ship, walk | 0.48 / 0.86 | 0.64 / 0.97 | 0.50 / 0.84 | 0.48 / 0.74 |
| ship at dawn, frame | 6.57 / 7.75 | 6.64 / 8.61 | 6.58 / 7.36 | 6.67 / 8.20 |
| ship at dawn, update | 1.12 / 1.62 | 1.14 / 1.67 | 1.01 / 1.49 | 1.15 / 1.64 |
| mages guild, frame | 5.15 / 7.01 | 5.20 / 6.20 | 5.21 / 5.93 | 5.26 / 7.27 |
| mages guild, update | 0.52 / 0.87 | 0.54 / 0.93 | 0.49 / 0.77 | 0.55 / 0.90 |

The frame thread ran 3% fewer instructions under full LTO (22.3 against 23.1 billion at the ship, 10.3 against
10.5 at the guild) in as many cycles, and no row's median moved past what two runs of one binary differ by. The
frame waits on the card for about 4 of its 5 to 6.5 ms.
