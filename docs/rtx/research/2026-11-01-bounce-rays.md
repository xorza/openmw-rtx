# Several bounce rays a pixel — 2026-11-01

`[RTX] bounce rays` (`--bounce-rays`): the eye's surface traces `N` bounce rays a frame, each in a
direction of its own and worth one `N`-th of the bounce. Measured at `1331144bf7`, whose rays are
the same at `b07580d6cd`, with the release flavour, the `default` and `interiors` suites at
1920x1080 traced at 1280x720 (upscale quality), vanilla Morrowind.

## The desk

A high-end NVIDIA laptop GPU, NVIDIA 615.78.08, Linux 7.2.11. Two alternated rounds of each count. The frame's
median in ms, the range over the rounds, and in brackets the `bounce` and the `landing` zones'
medians summed.

| place | 1 ray | 2 rays | 4 rays | 8 rays |
|---|---|---|---|---|
| `seyda-neen-ship` | 6.01–6.36 (1.04) | 7.16–7.30 (2.10) | 9.63–9.64 (4.27) | 14.16–14.19 (8.65) |
| `seyda-neen-ship-dawn` | 6.51–6.65 (1.10) | 7.76–8.02 (2.25) | 10.22–10.23 (4.57) | 15.00–15.04 (9.18) |
| `balmora-mages-guild` | 5.29–5.60 (1.28) | 6.83–6.95 (2.66) | 9.57–9.88 (5.43) | 15.25–15.70 (11.00) |
| `seyda-neen-customs` | 4.72–4.77 (0.97) | 5.79–5.95 (1.91) | 7.91–7.97 (4.00) | 12.14–12.16 (8.02) |
| `vivec-canalworks` | 3.18–3.29 (0.22) | 3.55–3.63 (0.43) | 4.02–4.13 (0.88) | 5.07–5.12 (1.81) |
| `addamasartus` | 5.09–5.18 (0.97) | 6.24–6.34 (2.07) | 8.58–8.71 (4.28) | 13.04–13.13 (8.48) |
| `arkngthand` | 2.94–2.96 (0.19) | 3.19–3.27 (0.36) | 3.60–3.71 (0.72) | 4.39–4.40 (1.38) |
| `andrano-tomb` | 4.35–4.46 (0.76) | 5.31–5.35 (1.56) | 7.09–7.11 (3.21) | 10.66–10.67 (6.59) |

Each ray costs what the first one does: the two zones double with each doubling of the count, to
within 6 % (the least, `arkngthand`'s 0.19 to 0.36 ms, rounded to a hundredth), and the frame grows
by about what they do. One frame of the guild's first round at four rays took 101.7 ms, against a
p99 of 13.03 ms; its second round's worst was 15.95 ms.

## The AMD integrated GPU

Windows 11, the `default` suite once each. The frame's median and p99 in ms, and the two zones'
shares of the mean summed.

| place | 1 ray | 2 rays | 4 rays | 8 rays |
|---|---|---|---|---|
| `seyda-neen-ship` | 119.8 / 139.6 (36.6) | 156.9 / 176.0 (73.0) | 230.6 / 249.7 (146.7) | 377.2 / 395.6 (291.6) |
| `seyda-neen-ship-dawn` | 148.7 / 170.5 (41.1) | 189.6 / 213.4 (82.2) | 273.5 / 297.3 (164.8) | 439.1 / 467.8 (328.9) |
| `balmora-mages-guild` | 83.9 / 92.9 (31.4) | 115.4 / 123.9 (63.0) | 177.9 / 186.4 (125.0) | 302.6 / 311.1 (249.3) |

The same: each ray costs the first one's, and the frame is the zones' growth and nothing else.

## The AMD handheld

RADV, Mesa 26.2.0-devel (git-5c65d4fc31), the core clock held at 1600 MHz, the `default`
suite once each through `omw release remote`. The frame's median and p99 in ms, and the two zones'
medians summed.

| place | 1 ray | 2 rays | 4 rays | 8 rays |
|---|---|---|---|---|
| `seyda-neen-ship` | 164.8 / 181.8 (55.7) | 221.5 / 246.4 (112.4) | 333.9 / 374.6 (224.8) | 558.9 / 630.2 (449.8) |
| `seyda-neen-ship-dawn` | 190.8 / 215.4 (59.7) | 251.7 / 283.2 (120.5) | 372.8 / 419.3 (241.3) | 614.7 / 692.4 (482.7) |
| `balmora-mages-guild` | 106.5 / 108.6 (48.5) | 155.0 / 159.9 (97.1) | 254.1 / 260.6 (195.9) | 453.5 / 466.1 (395.4) |

The same again. At 1920x1080 the handheld and the integrated GPU are far under the frame rate a player reads, at
one ray as at eight; the slider is for a card with room to spare.

## Noise

`noise --ab=bounce-rays=1,N`, the frame against its own frames averaged, in levels of 255, on the
desk.

**The denoised and upscaled frame barely shows the rays.** The mean noise strafing and walking 150
units, at 1 and 4 rays: the pier 1.31 and 1.30 (strafe), 1.31 and 1.29 (walk); the pond 1.33 and
1.32, 1.31 and 1.30; the guild 0.98 and 0.98, 1.04 and 1.03, and at 2 rays within 0.01 of the 4
rays' figures. The p99 moved by 0.06 at most, but for the pier's walk, 7.19 at 1 ray and 6.69 at
both 2 and 4; bias and fireflies by 0.02 at most. The reading: the denoiser's history already
averages the bounce over many frames, and what is left in the frame is the rest of the light,
which the raw frame below shows the rays do not touch.

**The raw frame does**, standing, with the filter and the upscaler off:

| place | 1 ray | 2 rays | 4 rays | 8 rays |
|---|---|---|---|---|
| `balmora-mages-guild`, noise | 4.16 | 3.90 | 3.70 | 3.51 |
| fireflies in 1000 | 2.31 | 1.23 | 0.86 | 0.59 |
| `seyda-neen-pier`, noise | 28.03 | 25.23 | 24.23 | 23.57 |
| fireflies in 1000 | 16.07 | 14.90 | 14.69 | 14.66 |

The noise falls with the count and levels out at the part of it that is not the bounce: the lamps'
and the sun's draws, which the rays leave as they are. The guild's fireflies, which are the
bounce's, fall to a quarter at eight rays.

## A deeper path, refused

What the first reading of the request built: a bounce off the bounce's landing, `pathEnd` at the
last landing, and a throughput floor. The desk, at `7287507e92`, the same suites and rounds; the
frame's median range and the two zones' medians summed.

| place | depth 1 | 2 | 4 | 8 |
|---|---|---|---|---|
| `seyda-neen-ship` | 6.04–6.15 (1.03) | 6.78–6.85 (1.82) | 7.09–7.17 (2.06) | 7.14–7.20 (2.13) |
| `balmora-mages-guild` | 5.34–5.48 (1.32) | 6.90–6.96 (2.74) | 7.24–7.28 (3.09) | 7.28–7.35 (3.16) |
| `seyda-neen-customs` | 4.73–4.77 (1.01) | 5.88–5.95 (2.06) | 6.23–6.25 (2.39) | 6.31–6.33 (2.50) |
| `andrano-tomb` | 4.40–4.42 (0.78) | 5.33–5.34 (1.57) | 6.60–6.62 (2.80) | 6.63–6.67 (2.85) |
| `vivec-canalworks` | 3.24–3.27 (0.23) | 3.51–3.56 (0.45) | 3.89–3.93 (0.78) | 4.00–4.07 (0.87) |
| `arkngthand` | 2.94–2.97 (0.17) | 3.02 (0.25) | 3.08–3.09 (0.30) | 3.10–3.20 (0.31) |

The second step costs about what the first does, and the floor ends most paths after it. The mages'
guild's shot, from the working tree that became `1b46bef106`, came out darker with depth: a mean
of 23.29 levels at one bounce, 22.24 at two and 22.20 at four. The reading: a path that ends at its
first landing takes the cell's bright `AMBI` there, and each step further takes it by a delit
albedo first, so a vanilla room lit by its ambient loses light.

## Memory

The frame's own device memory (`frame` in a bench record's uses), the desk, `balmora-mages-guild`
at a 1280x720 trace:

| build | 1 ray | 4 rays |
|---|---|---|
| master before the rays (`a82452cf2f`) | 773,060,736 B | — |
| the landing's images at every count (`1331144bf7`) | 864,827,840 B | 864,827,840 B |
| the landing's images only above one ray (`9b92c44508`) | 773,065,984 B | 820,249,344 B |

The 5,248 bytes over master at one ray are the landing's three images a texel wide and a list head a
ray. The 44,578,496 bytes under `1331144bf7` at four rays are not the frame's own landing, which
stands at the frame's size in both builds: they are the pictures' chain, which traces one ray at
every count and so holds its landing's images a texel wide, and each chain's landing list, one
array of the pixels in place of two.
