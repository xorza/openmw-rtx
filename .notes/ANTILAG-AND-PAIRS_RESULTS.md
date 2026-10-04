# Results: master, the start, the anti-lag and the paired reuse

Measured 2026-10-04 on the RTX 4090 Laptop, release, 1280×720 traced under FSR `quality` to 1920×1080,
the `bounce` suite. Each configuration ran `bench` once after a warm-up leg of 5 s a place, then
`noise` still, with `--strafe=150` and with `--walk=150`. The configurations ran one after another
on a quiet desktop, not interleaved, so frame times carry the card's clock between runs (about
0.1–0.3 ms); the GPU zones are steadier, and `noise` is deterministic: two runs of one build give
the same figures.

| Configuration | What it is |
|---|---|
| master | The branch's base: no bounce reuse, the accumulator without anti-lag. |
| start | `restir-gi` before this work: ReSTIR GI (spatiotemporal), no anti-lag. |
| anti-lag | Part A as landed: the accumulator's clamp and acceleration, a fast mean of two frames. |
| paired reuse | Part B on top, the branch as it ends: first-hit visibility rays, paired spatial reuse. |

## Cost

Median ms. "Reuse" is the bounce reuse's zones together (validate, temporal, pairs, resolve);
"accumulate" includes the anti-lag's `clamp` zone.

### Frame, median / p99 / worst

| Configuration | Guild | Planter | Yurt | Pier | Pond |
|---|---|---|---|---|---|
| master (892eb665) | 4.89 / 6.36 / 8.24 | 4.99 / 5.86 / 6.93 | 4.57 / 5.26 / 6.25 | 6.09 / 7.19 / 8.77 | 6.84 / 7.88 / 8.63 |
| start (4a59dcb0) | 6.92 / 8.63 / 11.18 | 7.27 / 8.08 / 9.66 | 6.63 / 7.78 / 8.44 | 8.58 / 9.65 / 11.36 | 8.63 / 9.73 / 11.78 |
| anti-lag (9fa07f02) | 7.19 / 9.57 / 11.20 | 7.53 / 8.40 / 9.74 | 6.81 / 7.73 / 8.29 | 8.89 / 9.96 / 10.84 | 8.73 / 9.83 / 10.38 |
| paired reuse (a5801e96, final) | 6.65 / 8.30 / 10.46 | 6.86 / 7.77 / 8.65 | 6.49 / 7.35 / 8.55 | 8.02 / 9.15 / 10.95 | 8.17 / 9.16 / 9.96 |

### Trace

| Configuration | Guild | Planter | Yurt | Pier | Pond |
|---|---|---|---|---|---|
| master (892eb665) | 1.53 | 1.63 | 1.35 | 2.09 | 2.98 |
| start (4a59dcb0) | 1.68 | 1.75 | 1.47 | 2.30 | 3.24 |
| anti-lag (9fa07f02) | 1.66 | 1.74 | 1.46 | 2.32 | 3.23 |
| paired reuse (a5801e96, final) | 1.61 | 1.70 | 1.41 | 2.23 | 3.31 |

### Reuse

| Configuration | Guild | Planter | Yurt | Pier | Pond |
|---|---|---|---|---|---|
| master (892eb665) | — | — | — | — | — |
| start (4a59dcb0) | 1.66 | 1.97 | 1.64 | 2.06 | 1.36 |
| anti-lag (9fa07f02) | 1.64 | 1.96 | 1.64 | 2.06 | 1.35 |
| paired reuse (a5801e96, final) | 1.28 | 1.42 | 1.34 | 1.43 | 0.96 |

### Accumulate + clamp

| Configuration | Guild | Planter | Yurt | Pier | Pond |
|---|---|---|---|---|---|
| master (892eb665) | 0.23 | 0.22 | 0.23 | 0.22 | 0.22 |
| start (4a59dcb0) | 0.26 | 0.25 | 0.25 | 0.24 | 0.23 |
| anti-lag (9fa07f02) | 0.54 | 0.53 | 0.54 | 0.52 | 0.40 |
| paired reuse (a5801e96, final) | 0.52 | 0.52 | 0.52 | 0.51 | 0.38 |

### Reuse zones, paired reuse

| Place | validate | temporal | pairs | resolve |
|---|---|---|---|---|
| Guild | 0.15 | 0.31 | 0.39 | 0.43 |
| Planter | 0.17 | 0.31 | 0.48 | 0.46 |
| Yurt | 0.14 | 0.31 | 0.46 | 0.43 |
| Pier | 0.12 | 0.31 | 0.55 | 0.46 |
| Pond | 0.10 | 0.20 | 0.32 | 0.34 |

## Noise and bias

`noise`: the frame's distance from the mean of its own independent draws, and its bias against a
converged reference, in levels of 255. Lower is better for both.

### Still: noise / bias

| Configuration | Guild | Planter | Yurt | Pier | Pond |
|---|---|---|---|---|---|
| master (892eb665) | 0.79 / 1.67 | 0.96 / 2.33 | 0.76 / 1.78 | 0.50 / 1.71 | 0.47 / 1.53 |
| start (4a59dcb0) | 0.65 / 1.75 | 0.70 / 2.33 | 0.74 / 1.67 | 0.49 / 1.71 | 0.46 / 1.47 |
| anti-lag (9fa07f02) | 0.61 / 1.79 | 0.65 / 2.38 | 0.72 / 1.72 | 0.49 / 1.71 | 0.46 / 1.47 |
| paired reuse (a5801e96, final) | 0.62 / 1.80 | 0.67 / 2.39 | 0.73 / 1.72 | 0.49 / 1.73 | 0.46 / 1.48 |

### Strafed in, 150 units: noise / bias

| Configuration | Guild | Planter | Yurt | Pier | Pond |
|---|---|---|---|---|---|
| master (892eb665) | 1.57 / 1.98 | 2.17 / 2.29 | 2.47 / 2.62 | 1.18 / 1.44 | 1.19 / 1.75 |
| start (4a59dcb0) | 1.37 / 2.02 | 1.83 / 2.37 | 2.26 / 2.67 | 1.18 / 1.41 | 1.19 / 1.72 |
| anti-lag (9fa07f02) | 1.33 / 2.04 | 1.72 / 2.43 | 2.22 / 2.71 | 1.18 / 1.41 | 1.19 / 1.72 |
| paired reuse (a5801e96, final) | 1.33 / 2.04 | 1.71 / 2.44 | 2.26 / 2.71 | 1.18 / 1.41 | 1.19 / 1.73 |

### Walked in, 150 units: noise / bias

| Configuration | Guild | Planter | Yurt | Pier | Pond |
|---|---|---|---|---|---|
| master (892eb665) | 1.82 / 2.42 | 1.79 / 3.43 | 2.96 / 3.12 | 1.18 / 2.07 | 1.14 / 1.63 |
| start (4a59dcb0) | 1.50 / 2.44 | 1.48 / 3.29 | 2.66 / 3.12 | 1.18 / 2.16 | 1.14 / 1.67 |
| anti-lag (9fa07f02) | 1.41 / 2.46 | 1.40 / 3.30 | 2.62 / 3.14 | 1.18 / 2.16 | 1.14 / 1.67 |
| paired reuse (a5801e96, final) | 1.41 / 2.46 | 1.43 / 3.31 | 2.65 / 3.15 | 1.18 / 2.15 | 1.14 / 1.66 |

## Trail behind a moving occluder

`RtxBounceTrailTest`: a bar moving over a floor, 96 pixels square, native, against a still picture
of it; lag of the trailing edge in pixels, and the darkness left behind in columns. Not measurable
on master, which has no such test; its accumulator is the start's without the reuse.

| Configuration | Sky's fill: lag / tail | Sun: lag / tail |
|---|---|---|
| master (accumulator as at the start, reuse off) | 10.28 / 2.04 | 0.27 / 0.15 |
| start | 16.61 / 2.61 | 0.27 / 0.15 |
| anti-lag | 7.66 / 1.51 | 0.27 / 0.15 |
| paired reuse | 7.86 / 1.54 | 0.27 / 0.15 |

## Reading it

- **Against master, the branch as it ends** takes 1.3–1.9 ms more a frame at the median: the
  bounce reuse, 0.96–1.43 ms, and the accumulator's anti-lag, about 0.3 ms. In return, every frame
  of the suite is as clean or cleaner: the still guild 0.79 → 0.62, the still planter 0.96 → 0.67,
  the strafed planter 2.17 → 1.71, the walked guild 1.82 → 1.41. The bias stands within 0.15 of
  master's, at the strafed planter (2.29 → 2.44), and within 0.13 still, at the guild.
- **The anti-lag** halves the trail an actor's darkness leaves in the sky's fill (16.6 → 7.7
  pixels) and takes noise off every place still, strafed and walked, for about 0.3 ms
  (`ANTILAG-AND-PAIRS_QUESTIONS.md`, Q1). The sun's shadow does not change: its 0.27 pixels are the
  shadow denoiser's, and under FSR the upscaler's band (`ANTILAG-AND-PAIRS.md`, A1).
- **The paired reuse** takes 0.30–0.63 ms off the reuse's zones and 0.32–0.87 ms off the
  median frame, for noise within 0.02 still and 0.04 in motion of the anti-lag's, and at or under the
  start's everywhere. The guild's zones stand at 1.28 ms against the plan's 1.0 (Q3).
