# Lamp shadow rays at the Census and Excise Office — 2026-11-13

A high-end NVIDIA laptop GPU, release builds, 2560×1440. The place is the corner the player looked at in Seyda
Neen's Census and Excise Office, standing at 1057.3, -529.3, 322.7 and looking at 181.7, -59.3, 211.7, 09:10: a
plastered wall facing the camera, lit from the left, beside a wooden door frame on the right. M[FR]'s profile and
vanilla's were each measured; the figures below are vanilla's unless named.

## What the frame shows

The denoised frame holds blotches 10 to 20 pixels across over the darker half of the wall, from about x 1000 to
the door frame at x 1460, where the mean of 16 frames and the converged reference are smooth. Vanilla's frame
shows them as M[FR]'s does, so neither a replacer's maps nor its relief are the cause. With the bounce off
(`--indirect=0`) the band is as blotchy, and without the upscaler (`--upscale=off`) as well: the noise is the
lamps' direct light.

## How often the held lamp's ray got through

`shot --upscale=off --filter=0 --indirect=0 --exposure=1`, the lamps' light alone and unfiltered. A pixel whose
held lamp's ray got through shows the lamps' whole unshadowed sum and one whose ray was stopped shows next to
nothing, so the frame is two-valued: no pixel of either region below lies between 8 and 32 of 255.

| region, rows 300–1100 | pixels at 8 or more of 255 | mean |
|---|---|---|
| the lit wall, x 100–600 | 28.0% | 18.6 |
| the dark band, x 1050–1400 | 1.06% | 1.63 |

So over the band about one pixel in 94 carries all of the light, and even where the wall is lit, about one in
3.6. The lamps the reservoir weighs most there are stopped before they reach the wall: their unshadowed weight is
in the exact sum the bit multiplies, and their rays are most of the draws.

## `noise` at the place

| | frame noise, mean / p99 | 16 averaged, mean / p99 | bias, frame / 16 averaged | fireflies, a thousand |
|---|---|---|---|---|
| vanilla | 1.61 / 11.66 | 16.70 / 95.50 | 3.40 / 5.26 | 2.48 |
| M[FR] | 1.72 / 13.19 | 17.26 / 102.60 | 3.68 / 6.05 | 3.65 |

The verdict was "as clean" for both: the frame's figures are over the whole frame, which the smooth left wall
and the dark door frame fill, and the band is about a seventh of its width.
