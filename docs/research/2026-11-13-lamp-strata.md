# The lamps' draw in two strata — 2026-11-13

A high-end NVIDIA laptop GPU, release builds, 1920×1080 shown at the quality upscaling mode, vanilla content. The
lamps' draw split in two (`weighLampStrata`): the lamps the pixel's tile saw last frame and the rest, a reservoir
and a ray each, and their visibility one fraction (`CHANNEL_LAMPED_SHARE`), against the one reservoir and the one
bit before it.

## `noise` on the `lamps` suite

Frame noise against its own mean, mean / p99; its bias against the converged reference; fireflies a thousand; and
the noisiest tile's frame mean (`worstTile`, a tile an eighth of the height on a side).

| place, leg | noise, before / after | bias, before / after | fireflies, before / after | noisiest tile, before / after |
|---|---|---|---|---|
| census-office-wall, still | 1.62 / 11.47 — 0.79 / 2.72 | 3.44 — 1.14 | 3.35 — 0.82 | 5.79 — 1.30 |
| census-office-wall, strafed 150 | 4.29 / 21.53 — 2.51 / 15.63 | 8.18 — 4.71 | 1.47 — 0.05 | 9.54 — 6.71 |
| probe-guild-planter, still | 0.68 / 1.91 — 0.68 / 1.91 | 0.70 — 0.70 | 0.27 — 0.26 | 1.12 — 1.14 |
| probe-guild-planter, strafed 150 | 0.95 / 6.75 — 0.95 / 6.78 | 0.86 — 0.86 | 0.50 — 0.50 | 3.34 — 3.32 |
| balmora-mages-guild, still | 0.72 / 2.50 — 0.72 / 2.50 | 0.85 — 0.85 | 0.59 — 0.54 | 1.51 — 1.48 |
| balmora-mages-guild, strafed 150 | 0.98 / 6.50 — 0.98 / 6.44 | 1.01 — 1.01 | 0.81 — 0.80 | 4.33 — 4.32 |

The office's sixteen unfiltered frames averaged went from 16.70 to 9.44 against their limit, and their bias from
5.25 to 1.41: the band beside the door frame, where the held lamp's ray got through on one pixel in 94, is smooth in
the frame and stands as the reference does. The guild and its planter come out as they did: their tiles' lists
hold every lamp, and the strata are the one draw.

**A bit field a stratum, tried first**, smoothed the band and drew a ragged, speckled edge down the open lamp's
soft shadow, the noisiest tile at 13.71: which stratum a lamp stood in followed the tile's list, so neighbouring
pixels' fields meant two things, and the shadow denoiser blended them.

## Cost

`bench`, three places, 1200 frames, base and candidate back to back three times, the median of each run's median.

| place | frame, base / candidate | `direct`, base / candidate | `clamp`, base / candidate |
|---|---|---|---|
| seyda-neen-ship | 6.06 / 6.19 | 0.450 / 0.519 | 0.393 / 0.409 |
| balmora-mages-guild | 5.14 / 5.37 | 0.346 / 0.515 | 0.395 / 0.432 |
| census-office-wall | 4.17 / 4.28 | 0.228 / 0.335 | 0.343 / 0.367 |

`direct` is the second ray, wherever both strata hold a lamp, and the walk's second reservoir. The direct kernel of
the whole tuple holds 128 registers and keeps 2688 bytes a workgroup in shared memory, 1920 before; asked whole
before the traversals, the two lamp rays' queries had it at 5760, and `direct` at 0.545 ms at the guild. `clamp`
is the lamps' temporal pass over a fraction, whose neighbourhood reads the square's shares; with the shared rows a
fraction's alone and the bits' pass back at 40 registers, it costs 0.02 to 0.04 ms, where a pass that kept the
fraction's rows for the bits too, at 64 registers, cost 0.10.
