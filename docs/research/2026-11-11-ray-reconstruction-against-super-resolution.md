# Ray Reconstruction against Super Resolution and the renderer's filter

2026-11-11. A high-end NVIDIA laptop GPU, NVIDIA's DLSS libraries at 310.9.16 with their default presets,
a 1920×1080 output, release builds, a quiet desktop, back to back after a warm-up leg. The baseline is
`895570968e` (DLSS Super Resolution after the renderer's own filter), the candidate `cf35536296` (DLSS
Ray Reconstruction in place of both). The runs' reports, records and frame series stand in
`build-release/baselines/2026-11-11-dlss-sr/` and `…-dlss-rr/`.

## Frame time, median and p99, ms

| place | quality SR | quality RR | native SR | native RR |
|---|---|---|---|---|
| seyda-neen-ship | 6.54 / 8.69 | 6.60 / 9.21 | 11.80 / 14.65 | 12.58 / 18.08 |
| seyda-neen-ship-dawn | 7.08 / 9.53 | 6.99 / 9.32 | 12.91 / 15.75 | 13.12 / 16.49 |
| balmora-mages-guild | 5.59 / 7.77 | 5.70 / 8.39 | 10.54 / 14.08 | 11.11 / 16.62 |

The `upscale` zone, a share of the mean: SR 0.77–0.90 at quality and 0.90–0.97 at native, beside the
filter's zones (temporal, clamp, shadow, four wavelet levels: about 2.2 ms at quality at the ship, 4.5 at
native); RR 2.47–2.60 at quality and 5.00–5.44 at native, with no filter zone.

## Memory, MiB

| | quality SR | quality RR | native SR | native RR |
|---|---|---|---|---|
| the allocator's live ranges on the video heap | 1296.6 | 1176.6 | 1783.4 | 1513.4 |
| the driver's usage for the process (`held`), NGX's features among it | 1569.2 | 1532.6 | 2079.8 | 2105.8 |
| frame use, as the reserve counts it | 598.9 | 652.9 | 1085.5 | 1176.9 |

The candidate's frame use counts RR's feature (`MemoryAllocator::holdOutside`), which the baseline's did
not count of SR's. RR's feature at a 1920×1080 output: 182.4 MB at quality (a debug run's NGX stats).

## Noise suite at quality (`noise`), frame against 16 frames averaged, and bias against the reference

| place | SR noise mean / p99 | RR noise mean / p99 | SR bias | RR bias | SR fireflies ‰ | RR fireflies ‰ |
|---|---|---|---|---|---|---|
| seyda-neen-pier | 0.67 / 1.60 | 0.88 / 2.53 | 0.74 | 1.08 | 0.04 | 0.00 |
| seyda-neen-pond | 0.77 / 3.60 | 0.86 / 3.40 | 1.18 | 0.94 | 0.01 | 0.00 |
| balmora-mages-guild | 0.71 / 2.50 | 0.78 / 3.12 | 0.93 | 0.62 | 0.57 | 0.00 |

Every frame of both stands as clean as 16 frames averaged.

## Pictures with nothing upscaling

`./omw shot --views=all --map --upscale=off` at both commits, the candidate `--against` the baseline:
no picture moved, and three (`seyda-neen-underwater` among them) differ by one of 255 on under a
hundredth of a per cent of their pixels, within the denoiser's own noise on this card. The renderer's
filter draws as it did wherever it runs.

## RR's inputs, A/B under Ray Reconstruction at quality

`./omw release noise --ab=<switch> --upscaler=dlss --upscale=quality`, the strafe and the walk legs of
150 units, noise mean / p99 and bias against the reference; runs in `build-release/baselines/2026-11-11-rr-ab/`.

| switch, strafe | pier | pond | guild |
|---|---|---|---|
| noise: blue-noise / white-hash | 1.17, 4.69, 1.16 / 1.23, 5.16, 1.20 | 1.28, 9.03, 1.31 / 1.28, 9.06, 1.32 | 0.99, 6.16, 0.92 / 1.01, 6.40, 0.92 |
| soft-edge dither: on / off | 1.24, 5.28, 1.20 / 1.23, 5.16, 1.20 | 1.28, 9.06, 1.32 / 1.24, 8.00, 1.28 | 1.01, 6.40, 0.92 / 1.00, 6.28, 0.92 |
| water interleave: on / off | 1.24, 5.28, 1.20 / 1.23, 5.16, 1.20 | 1.28, 9.06, 1.32 / 3.21, 22.03, 1.94 | 1.01, 6.40, 0.92 / 1.01, 6.40, 0.92 |

The walk leg tells the same: the hash 1.27 against the tile's 1.22 at the pier, the cut 1.18 against the
dither's 1.23 at the pond (p99 7.47 against 8.60), the pond without the interleave 3.21 against 1.23.
