# Lamps under the fog's phase, falling sprites out of the self-shadow, a Lanczos table: 2026-11-14

On a high-end NVIDIA laptop GPU, release build, three 20-second legs each side after a warm-up, the zones'
medians and p99 out of `bench --json`; pictures by `shot --upscale=off --against`, noise by `noise`.

## Lamps scatter by the fog's phase (`lampsInAir`)

| | `air` at `seyda-neen-ship` under snow, median / p99 (ms) |
|---|---|
| isotropic | 0.245 / 0.31, 0.247 / 0.51, 0.246 / 0.29 |
| the phase | 0.266 / 0.55, 0.266 / 0.54, 0.266 / 0.53 |

At `balmora-fog-night` the zone held at 0.226 ms either way. The pictures moved where lamps light the fog:
`balmora-fog-night` 24.85 % of pixels, worst 112 of 255 (halos toward the eye round the lamps down the
street); `balmora-mages-guild` 91.88 %, worst 41. The noise at `balmora-fog-night`: frame mean 0.64 / p99 1.34
against 16 averaged either way, bias 0.30 to 0.34, fireflies 0.01 to 0.06 in a thousand, as clean as its bar.

## Falling sprites out of the self-shadow (`spriteShadeWants`)

| | `shade` under snow, median / p99 (ms) |
|---|---|
| shaded | 0.947 / 1.29, 0.951 / 1.30, 0.949 / 1.30 |
| not | 0.031 / 0.032 (all three) |

The pictures under snow moved by at most 2 of 255 on 0.37 % of pixels, under rain on 0.44 %.

## The reprojection's Lanczos table (`SampleLanczos2Weight`)

| | `upscale` at `balmora-fog-night`, median / p99 (ms) |
|---|---|
| reference | 0.433 / 1.33, 0.436 / 1.07, 0.438 / 1.03 |
| table | 0.401 / 0.69, 0.404 / 0.69, 0.405 / 0.69 |
