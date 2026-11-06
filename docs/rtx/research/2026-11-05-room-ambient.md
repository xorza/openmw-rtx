# A room's ambient against its traced light — 2026-11-05

A high-end NVIDIA laptop GPU, release builds, vanilla content, `shot --upscale=off` at 1920x1080. Means
are of linear luminance read back off the shown picture, so the display curve is still in them; "black"
is the share of pixels at or under 16 of 255.

## What the record's terms come to

Over the 1,134 interiors of `Morrowind.esm`, by luminance, with `spread` the record's sunlight over
`DAYLIGHT * INV_FOUR_PI`: `spread` is a median 39% of `fill + spread` (21% at the tenth percentile, 75%
at the ninetieth), and the sum a median 1.63 times `fill`.

The share of `fill` in a picture, at a fixed exposure of 12, with `spread` already gone:

| place | of the mean | median pixel | pixels over 75% |
|---|---|---|---|
| `vivec-canalworks` | 48% | 89% | 75% |
| `balmora-mages-guild` | 32% | 16% | 6% |
| `ahemmusa-yurt` | 8% | 10% | 0% |
| `andrano-tomb` | 1% | 6% | 1% |
| `addamasartus` | 1% | 0% | 0% |

## What depth adds

Means at a fixed exposure of 8, with no ambient at all, from a test build tracing each further bounce
inline in the landing. The inline trace is not the wavefront the fork ships, so its frame times say
nothing of the wavefront's.

| place | 0 bounces | 1 | 2 | 3 |
|---|---|---|---|---|
| `vivec-canalworks` | 0.0038 | 0.0041 | 0.0041 | 0.0042 |
| `balmora-mages-guild` | 0.0074 | 0.0075 | 0.0075 | 0.0075 |
| `ahemmusa-yurt` | 0.0132 | 0.0140 | 0.0140 | 0.0140 |
| `andrano-tomb` | 0.0221 | 0.0246 | 0.0250 | 0.0250 |
| `addamasartus` | 0.0187 | 0.0193 | 0.0193 | 0.0193 |
| `arkngthand` | 0.0413 | 0.0413 | 0.0413 | 0.0413 |
| `akulakhan-chamber` | 0.0007 | 0.0119 | 0.0143 | 0.0150 |

## The fill at the landing

Metered exposure, `vivec-canalworks`, no `spread`:

| | median | black | mean |
|---|---|---|---|
| one bounce, no fill | 14.7 | 60.0% | 0.0129 |
| two bounces, no fill | 15.6 | 52.3% | 0.0131 |
| one bounce, fill by a quarter | 24.3 | 3.5% | 0.0165 |
| two bounces, fill by a quarter | 17.0 | 43.3% | 0.0139 |

## The ambient where no bounce is traced

The scale on `fill + spread`, standing on the surface, that brings each room's mean at a fixed exposure
of 8 to a target, solved from the picture with the whole of it and the picture with none:

| place | depth 3, no fill | one bounce, fill by a quarter |
|---|---|---|
| `vivec-canalworks` | 0.01 | 0.03 |
| `balmora-mages-guild` | 0.01 | 0.05 |
| `ahemmusa-yurt` | 0.02 | 0.02 |
| `andrano-tomb` | 0.29 | 0.26 |
| `addamasartus` | 0.15 | 0.14 |
| `arkngthand` | 0.02 | 0.02 |
| `akulakhan-chamber` | 0.19 | 0.15 |

The median of the second column is 0.05.

## The wavefront's depth

`[RTX] bounces`, one ray, metered, `shot --upscale=off`: the median pixel and the share of black, with
the fill by a quarter. At the last landing, as NRD's sample has its ambient, each further wall took it
by one more albedo:

| place | 1 | 2 | 3 |
|---|---|---|---|
| `vivec-canalworks` | 21.0, 11.2% | 14.7, 61.3% | 13.8, 67.1% |
| `balmora-mages-guild` | 22.9, 6.3% | 22.5, 8.3% | 22.5, 8.4% |
| `akulakhan-chamber` | 11.1, 66.1% | 23.8, 19.1% | 25.1, 16.2% |

At the first landing, tried and refused, and with the same picture at one bounce:

| place | 1 | 2 | 3 |
|---|---|---|---|
| `vivec-canalworks` | 21.0, 11.2% | 21.4, 9.6% | 21.4, 9.4% |
| `balmora-mages-guild` | 22.9, 6.3% | 23.0, 6.2% | 23.0, 6.2% |
| `ahemmusa-yurt` | 45.3, 3.3% | 45.5, 3.3% | 45.5, 3.3% |
| `andrano-tomb` | 32.4, 17.1% | 33.5, 13.7% | 33.7, 13.2% |
| `addamasartus` | 37.2, 6.5% | 37.4, 6.3% | 37.4, 6.3% |
| `arkngthand` | 63.6, 29.6% | 63.6, 29.6% | 63.6, 29.6% |
| `akulakhan-chamber` | 11.1, 66.1% | 24.2, 18.6% | 25.5, 15.8% |

Six exteriors, day and night, moved by at most 0.2 of a code in the median. The frame took 0 to 7 ms more
at each further bounce in these shots, which ran with no warm-up under the desktop: no bench.

## The fill at the path's end, raised

What raises the fill at the second landing so that a room's mean at two bounces meets its mean at one,
exposure fixed at 24, solved from the second bounce with no fill and with ten times it:

| place | one bounce | two, no fill | two, ten times | what it takes |
|---|---|---|---|---|
| `vivec-canalworks` | 0.0124 | 0.0104 | 0.0131 | 7.5 |
| `ahemmusa-yurt` | 0.0374 | 0.0368 | 0.0373 | 11 |
| `balmora-mages-guild` | 0.0207 | 0.0205 | 0.0206 | 21 |
| `arkngthand` | 0.1326 | 0.1326 | 0.1326 | no answer |
| `addamasartus` | 0.0463 | 0.0463 | 0.0464 | none: over already |
| `seyda-neen-customs` | 0.0734 | 0.0735 | 0.0737 | none: over already |
| `andrano-tomb` | 0.0549 | 0.0556 | 0.0558 | none: over already |
| `akulakhan-chamber` | 0.0283 | 0.0342 | 0.0342 | none: over already |

At 8, metered, one bounce and then two: the median pixel, the share of black, and the stops between the
means. One bounce's pictures are those before the fill moved, to the bit in the rooms.

| place | 1 | 2 | stops |
|---|---|---|---|
| `vivec-canalworks` | 21.0, 11.2% | 21.2, 9.5% | +0.01 |
| `balmora-mages-guild` | 22.9, 6.3% | 22.6, 7.7% | −0.01 |
| `ahemmusa-yurt` | 45.3, 3.3% | 45.1, 3.4% | −0.01 |
| `andrano-tomb` | 32.4, 17.1% | 33.5, 13.8% | +0.02 |
| `addamasartus` | 37.2, 6.5% | 37.2, 6.4% | +0.00 |
| `arkngthand` | 63.6, 29.6% | 63.6, 29.6% | 0.00 |
| `akulakhan-chamber` | 11.1, 66.1% | 24.2, 18.9% | +0.35 |

Six exteriors, day and night, stayed within 0.00 of a stop.

## Why depth moved so little

`--show=albedo`, read back as linear luminance: `vivec-canalworks` a mean of 0.10, `balmora-mages-guild`
and `akulakhan-chamber` 0.03. So a second bounce returns about a tenth of what the first did at most. By
what each pixel held at one bounce, fixed exposure 8, no ambient, `vivec-canalworks`:

| luminance at one bounce | pixels | light | at one | at two |
|---|---|---|---|---|
| under 0.0005 | 50.7% | 3% | 0.00023 | 0.00029 |
| 0.0005 to 0.002 | 31.9% | 6% | 0.00081 | 0.00086 |
| 0.002 to 0.008 | 10.9% | 11% | 0.00433 | 0.00439 |
| 0.008 to 0.03 | 4.2% | 13% | 0.01286 | 0.01298 |
| over 0.03 | 2.2% | 67% | 0.12899 | 0.12904 |

## A share of the sunlight at the landing

`Akulakhan's Chamber` holds ambient (20, 30, 30) and sunlight (255, 140, 140), and `Vivec, Hlaalu Canalworks`
(55, 50, 45) and (61, 40, 20). One bounce, metered, the fill by a quarter and `spread` by a share `k` beside
it: the median pixel and the share of black.

| place | 0 | 0.02 | 0.05 | 0.1 | 0.2 |
|---|---|---|---|---|---|
| `akulakhan-chamber` | 11.1, 66.1% | 13.0, 62.1% | 14.9, 55.6% | 17.2, 43.6% | 20.4, 27.4% |
| `vivec-canalworks` | 21.0, 11.2% | 21.3, 9.9% | 21.6, 8.3% | 22.3, 6.4% | 23.5, 4.1% |
| `andrano-tomb` | 32.4, 17.1% | 32.5, 16.8% | 32.6, 16.3% | 32.8, 15.4% | 33.1, 13.4% |
| `balmora-mages-guild` | 22.9, 6.3% | 23.0, 6.2% | 23.0, 6.2% | 23.0, 6.1% | 23.1, 5.8% |
| `ahemmusa-yurt` | 45.3, 3.3% | 45.4, 3.3% | 45.4, 3.3% | 45.5, 3.3% | 45.6, 3.2% |

## Other content

One bounce with a landing's ambient by a quarter and its sunlight by a fifth, `shot --upscale=off`, metered: the
median pixel and the share of black, indirect lighting on and then off. A mod list with Tamriel Rebuilt and the
PBR replacers' profile, at the vanilla rooms.

| place | mod list, on | off | PBR, on | off |
|---|---|---|---|---|
| `vivec-canalworks` | 15.3, 61.3% | 21.9, 8.5% | 21.5, 10.6% | 25.4, 5.6% |
| `balmora-mages-guild` | 23.2, 5.2% | 25.4, 0.5% | 25.8, 0.9% | 27.2, 0.4% |
| `ahemmusa-yurt` | 42.7, 6.8% | 42.2, 7.4% | 49.5, 1.3% | 48.4, 4.4% |
| `andrano-tomb` | 29.1, 23.8% | 26.0, 32.3% | 33.4, 15.4% | 28.4, 31.7% |
| `addamasartus` | 33.4, 7.6% | 33.0, 7.7% | 41.9, 4.6% | 39.9, 5.8% |
| `arkngthand` | 63.6, 29.8% | 63.6, 29.8% | 63.6, 29.4% | 67.3, 29.5% |
| `akulakhan-chamber` | 10.7, 72.5% | 12.6, 72.6% | 24.4, 4.0% | 34.5, 2.9% |

Tamriel Rebuilt's interiors, on the mod list, on and off: `Old Ebonheart, Arnos Clatian's House` 28.6, 5.7% and
29.4, 4.3%; `Firewatch, Antoni Manor` 13.1, 57.9% and 12.8, 58.4%; `Karthwasten Caves, Lower Caverns`, from the
cell's default stand, 0.0, 100% and 9.1, 96.7%.

`--show=albedo`, read back as linear luminance, the mean, at vanilla, the mod list and PBR:

| place | vanilla | mod list | PBR |
|---|---|---|---|
| `vivec-canalworks` | 0.103 | 0.063 | 0.096 |
| `andrano-tomb` | 0.088 | 0.066 | 0.073 |
| `ahemmusa-yurt` | 0.063 | 0.070 | 0.057 |
| `addamasartus` | 0.049 | 0.036 | 0.054 |
| `balmora-mages-guild` | 0.029 | 0.027 | 0.034 |
| `akulakhan-chamber` | 0.032 | 0.015 | 0.034 |
| `arkngthand` | 0.022 | 0.019 | 0.026 |

Over the interiors of the content files: `TR_Mainland.esm`'s 3,390 hold a median ambient of 0.031 by luminance and
a landing's sunlight share 0.27 of its ambient share at the median and 1.34 at the ninetieth percentile;
`Sky_Main.esm`'s 348 hold 0.015, 0.51 and 0.97.

Frame medians, `bench --seconds=6`, one 1080p bounce ray, two runs a side, a high-end NVIDIA laptop GPU,
`seyda-neen-ship`, `balmora-mages-guild`, `vivec-canalworks` and `akulakhan-chamber`: indirect lighting off saved
0.8 to 2.0 ms under the mod list and 1.0 to 1.5 ms under PBR. The two runs under vanilla disagreed by more than the
difference, so they say nothing.

## The landing without its occlusion ray

Dropped at every landing, the mean over two 6 s benches, vanilla then PBR, against the two runs either side of it:
`seyda-neen-ship` 6.32 to 6.09 and 7.09 to 6.93 ms, `balmora-mages-guild` 5.65 to 5.38 and 6.44 to 6.37,
`vivec-canalworks` 4.81 to 4.64 and 5.56 to 5.48, `akulakhan-chamber` 4.05 to 3.90 and 4.87 to 4.81. `noise`, frame
and sixteen averaged, no worse at those places, and fireflies 0.12 to 0.09, 0.62 to 0.52 and 0.15 to 0.12 in a
thousand. Out of doors it let the sky under a lid (`RtxVisibilityTest`), so the ray came back there. In a room,
metered, median and black, vanilla: `vivec-canalworks` 23.5, 4.1% to 25.1, 2.1%, `balmora-mages-guild` 23.1, 5.8%
to 23.3, 4.8%, `ahemmusa-yurt` 45.6, 3.2% to 46.5, 2.5%, `akulakhan-chamber` 20.4, 27.4% to 21.0, 24.5%; six
exteriors moved by at most 1 of 255.

## What lights a room's water

Scatter at the whole of `fill + spread`, at half of it, and at the landing's share, metered, from each cell's default
stand: the median pixel and the mean.

| place | whole | half | the landing's |
|---|---|---|---|
| `Vivec, Puzzle Canal, Center` | 36.8, 0.0156 | 27.8, 0.0085 | 21.1, 0.0046 |
| `Abandoned Shipwreck, Lower Level` | 36.8, 0.0147 | 27.8, 0.0078 | 20.7, 0.0040 |
| `Vivec, Hlaalu Underworks` | 61.6, 0.0575 | 63.9, 0.0627 | 65.4, 0.0656 |

`Morrowind.esm` holds 193 interiors with water.

## Four lamp candidates at a path's end

`--lamp-candidates=4` against 8, two 6 s benches a side, the frame's median, and the landing's share of it:
`balmora-mages-guild` 5.17 to 5.05 ms and 0.67 to 0.59 under vanilla, 6.10 to 5.93 and 0.81 to 0.69 under PBR;
`vivec-canalworks` and `akulakhan-chamber` moved by under 0.1 ms, and `seyda-neen-ship` slower in both runs, within
what the runs disagree by. `noise` over ten places: as clean, and bias at sixteen averaged 0.41 to 0.43 and 0.67 to
0.72 in the guild, 0.88 to 0.94 and 0.93 to 1.07 in `addamasartus`. So 8 stays.

## The wavelet's levels with no bounce traced

`--indirect=false`, four levels, one and none, the frame's median in ms of two 6 s benches, and the frame's mean
noise over `noise --cut=4` and still. No level copied the accumulator's blend over its history.

| place | four | one | none |
|---|---|---|---|
| `seyda-neen-ship` | 5.45 | 4.82 | 4.77 |
| `balmora-mages-guild` | 4.43 | 4.10 | 3.55 |
| `vivec-canalworks` | 4.05 | 3.51 | 3.16 |
| `akulakhan-chamber` | 3.39 | 2.77 | 2.58 |
| PBR, `seyda-neen-ship` | 6.05 | 5.49 | 5.29 |
| PBR, `balmora-mages-guild` | 5.36 | 4.65 | 4.42 |
| PBR, `vivec-canalworks` | 4.84 | 4.09 | 3.87 |
| PBR, `akulakhan-chamber` | 4.02 | 3.39 | 3.15 |

| place, frame noise | still, four | one | none | after a cut, four | one | none |
|---|---|---|---|---|---|---|
| `vivec-canalworks` | 0.66 | 0.67 | 0.78 | 0.93 | 1.07 | 2.23 |
| `balmora-mages-guild` | 0.73 | 0.73 | 0.75 | 1.50 | 1.53 | 1.81 |
| `ahemmusa-yurt` | 0.79 | 0.80 | 0.95 | 2.08 | 2.18 | 3.11 |
| `seyda-neen-ship` | 0.93 | 0.95 | 1.22 | 2.34 | 2.58 | 4.11 |
| `balmora` | 0.70 | 0.71 | 0.93 | 1.34 | 1.53 | 2.98 |

## The ceiling

`EXPOSURE_MAX` from 35 to 24.75 (half a stop), one bounce, fill by a quarter, metered:

| place | stops | median | black |
|---|---|---|---|
| `vivec-canalworks` | −0.38 | 24 to 21 | 3.5% to 11.2% |
| `balmora-mages-guild` | −0.41 | 27 to 23 | 0.1% to 6.3% |
| `ahemmusa-yurt` | −0.37 | 51 to 45 | 1.9% to 3.3% |
| `andrano-tomb` | −0.33 | 38 to 32 | 11.8% to 17.1% |
| `addamasartus` | −0.37 | 43 to 37 | 5.4% to 6.5% |

Three of four clear nights at hour 23 came out identical in every pixel, and the fourth,
`dagoth-ur-caldera`, within 0.00 of a stop.
