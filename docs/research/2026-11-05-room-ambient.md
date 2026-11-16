# A room's ambient against its traced light — 2026-11-05

A high-end NVIDIA laptop GPU, release builds, vanilla content unless named, `shot --upscale=off` at 1920x1080. Means
are of linear luminance read off the shown picture; "black" is the share of pixels at or under 16 of 255.

## What the record holds

Over the 1,134 interiors of `Morrowind.esm`, the record's sunlight spread over the sphere is a median 39% of ambient
plus sunlight (21% at the tenth percentile, 75% at the ninetieth). `TR_Mainland.esm`'s 3,390 interiors hold a median
ambient of 0.031 by luminance, `Sky_Main.esm`'s 348 0.015.

## Why one bounce

`--show=albedo` reads `vivec-canalworks` at a mean linear luminance of 0.10 and `balmora-mages-guild` and
`akulakhan-chamber` at 0.03: a second bounce returns a tenth of the first at most. Traced deeper (a test build, not
the wavefront that ships), each further bounce moved six exteriors by at most 0.2 of a code in the median, for 0 to
7 ms a bounce in unbenched shots.

## The landing's share of the record

What a bounce's landing takes of the record's ambient and sunlight, against the exposure ceiling; the median of the
display luma and the share of black. Measured 2026-11-06 on the user's report that rooms read too bright and flat;
these rooms stood at the ceiling.

| place | no ambient, ceiling 24.75 | 0.25 and 0.2, 24.75 | 0.18 and 0.14, 20.8 (kept) |
|---|---|---|---|
| `seyda-neen-customs` | 47.9, 8.4% | 48.7, 7.6% | 44.3, 10.4% |
| `addamasartus` | 30.7, 26.4% | 31.0, 25.5% | 28.0, 30.2% |
| `andrano-tomb` | 25.8, 36.8% | 27.0, 33.0% | 24.0, 37.9% |
| `balmora-mages-guild` | 14.3, 58.0% | 15.7, 51.6% | 13.5, 59.9% |
| `vivec-canalworks` | 4.4, 82.4% | 17.7, 39.1% | 12.7, 67.7% |
| `akulakhan-chamber` | 3.1, 76.8% | 10.8, 67.4% | 7.8, 74.0% |

A room lit by its lamps barely reads its ambient, so only the exposure dims it; the ambient lifts the shadows of a dim
one. The sunlight's share matters where a record's sunlight is strong (`akulakhan-chamber`: black 66% with none, 27%
at a share of 0.2) and nowhere else. With no bounce traced, an ambient of 0.035 meets the landing's 0.18 and 0.14 in
the mean (`vivec-canalworks` 0.0119 against 0.0116, `seyda-neen-customs` 0.0605 against 0.0645).

## The landing's occlusion ray

Dropped at every landing, the frame 0.06 to 0.27 ms shorter under vanilla and PBR, `noise` no worse with fewer
fireflies, a room's median pixel up a code or two. Out of doors it let the sky in under a lid
(`RtxVisibilityTest`), so it stays there.

## Smaller choices

- **A room's water scatters half the record**: the Puzzle Canal's median pixel 36.8 at the whole, 27.8 at half, 21.1
  at the landing's share; the Hlaalu Underworks, lit by its lamps, within 4 codes at all three. 193 of
  `Morrowind.esm`'s interiors hold water.
- **Eight lamp candidates at a path's end**, not four: four saved 0.12–0.17 ms in the guild and raised its bias.
- **One wavelet level with no bounce traced**: the frame 0.3–0.75 ms shorter than four levels with the still noise the
  same; none nearly doubled the noise after a cut.
- **Indirect lighting off** saved 0.8 to 2.0 ms a frame under the mod list and 1.0 to 1.5 ms under the PBR replacers.
